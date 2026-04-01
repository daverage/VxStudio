#!/usr/bin/env python3
"""
Prepare and evaluate ReBalance listening-test fixtures.

The workflow uses the official MUSDB 7-second preview dataset for legal,
repeatable music fixtures, then derives the requested listening cases:

- Studio vocal + guitar
- Live room band clip
- Phone speech + acoustic guitar
- Drum-heavy loop
- Bass-heavy mix

Cases are written as stem folders compatible with `VXRebalanceMeasure`.
"""

from __future__ import annotations

import argparse
import json
import math
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Dict

import musdb
import numpy as np
import soundfile as sf


ROOT = Path(__file__).resolve().parents[1]
FIXTURE_ROOT = ROOT / "tests" / "fixtures" / "rebalance"
MUSDB_ROOT = FIXTURE_ROOT / "musdb_preview"
CASE_ROOT = FIXTURE_ROOT / "listening_cases"
MANIFEST_PATH = CASE_ROOT / "manifest.json"

SAMPLE_RATE = 44100
STEM_FILE_NAMES = (
    "_original.wav",
    "_vocals.wav",
    "_drums.wav",
    "_bass.wav",
    "_guitar.wav",
    "_piano.wav",
    "_other.wav",
)


@dataclass(frozen=True)
class CaseConfig:
    name: str
    description: str
    source_track: str
    recording_type: str
    primary_check: str


CASE_CONFIGS = (
    CaseConfig(
        name="studio_vocal_guitar",
        description="Studio-style singer-songwriter balance with vocals and guitar foregrounded.",
        source_track="Aimee Norwich - Child",
        recording_type="studio",
        primary_check="Vocals +100, Guitar -100, Neutral",
    ),
    CaseConfig(
        name="live_room_band",
        description="Band-style mix with room coloration derived from a MUSDB rock excerpt.",
        source_track="ANiMAL - Rockshow",
        recording_type="live",
        primary_check="Neutral, Vocals +100, Guitar -100",
    ),
    CaseConfig(
        name="phone_speech_acoustic_guitar",
        description="Synthetic phone-band speech plus guitar-accompaniment proxy.",
        source_track="Aimee Norwich - Child",
        recording_type="phone",
        primary_check="Vocals +100, Guitar -100, Neutral",
    ),
    CaseConfig(
        name="drum_heavy_loop",
        description="Drum-led loop-style excerpt with supporting bass and accompaniment.",
        source_track="ANiMAL - Clinic A",
        recording_type="studio",
        primary_check="Drums -100, Neutral",
    ),
    CaseConfig(
        name="bass_heavy_mix",
        description="Bass-weighted mix built from a MUSDB preview excerpt.",
        source_track="Actions - One Minute Smile",
        recording_type="studio",
        primary_check="Bass -100, Neutral",
    ),
)


def db_tracks() -> dict[str, musdb.audio_classes.MultiTrack]:
    db = musdb.DB(root=str(MUSDB_ROOT), download=True, subsets=["train"])
    return {track.name: track for track in db}


def ensure_dir(path: Path) -> None:
    path.mkdir(parents=True, exist_ok=True)


def stereo(audio: np.ndarray) -> np.ndarray:
    if audio.ndim == 1:
        return np.stack([audio, audio], axis=1).astype(np.float32)
    if audio.shape[1] == 1:
        return np.repeat(audio, 2, axis=1).astype(np.float32)
    return audio.astype(np.float32)


def silence_like(reference: np.ndarray) -> np.ndarray:
    return np.zeros_like(reference, dtype=np.float32)


def peak_normalize(audio: np.ndarray, peak: float = 0.92) -> np.ndarray:
    max_abs = float(np.max(np.abs(audio)))
    if max_abs <= 1.0e-8:
        return audio.astype(np.float32)
    return (audio * (peak / max_abs)).astype(np.float32)


def clamp_audio(audio: np.ndarray) -> np.ndarray:
    return np.clip(audio, -0.999, 0.999).astype(np.float32)


def bandlimit(audio: np.ndarray, sample_rate: int, low_hz: float, high_hz: float) -> np.ndarray:
    n = audio.shape[0]
    spectrum = np.fft.rfft(audio, axis=0)
    freqs = np.fft.rfftfreq(n, d=1.0 / sample_rate)
    mask = (freqs >= low_hz) & (freqs <= high_hz)
    spectrum[~mask, :] = 0.0
    filtered = np.fft.irfft(spectrum, n=n, axis=0)
    return filtered.astype(np.float32)


def simple_room(audio: np.ndarray, sample_rate: int) -> np.ndarray:
    wet = np.zeros_like(audio, dtype=np.float32)
    taps = (
        (0.013, 0.28),
        (0.027, 0.20),
        (0.041, 0.14),
        (0.059, 0.10),
    )
    for delay_seconds, gain in taps:
        delay = int(sample_rate * delay_seconds)
        if delay <= 0 or delay >= audio.shape[0]:
            continue
        wet[delay:, :] += audio[:-delay, :] * gain
        wet[delay:, 0] += audio[:-delay, 1] * (gain * 0.12)
        wet[delay:, 1] += audio[:-delay, 0] * (gain * 0.12)

    out = 0.82 * audio + 0.48 * wet
    out = np.tanh(out * 1.1).astype(np.float32)
    return peak_normalize(out, 0.9)


def trim_or_loop(audio: np.ndarray, samples: int) -> np.ndarray:
    if audio.shape[0] == samples:
        return audio.astype(np.float32)
    if audio.shape[0] > samples:
        return audio[:samples, :].astype(np.float32)
    reps = int(math.ceil(samples / max(1, audio.shape[0])))
    tiled = np.tile(audio, (reps, 1))
    return tiled[:samples, :].astype(np.float32)


def resample_linear(audio: np.ndarray, input_rate: int, output_rate: int) -> np.ndarray:
    if input_rate == output_rate:
        return audio.astype(np.float32)
    input_samples = audio.shape[0]
    output_samples = int(round(input_samples * output_rate / input_rate))
    src = np.linspace(0.0, 1.0, num=input_samples, endpoint=False)
    dst = np.linspace(0.0, 1.0, num=output_samples, endpoint=False)
    channels = []
    for ch in range(audio.shape[1]):
        channels.append(np.interp(dst, src, audio[:, ch]))
    return np.stack(channels, axis=1).astype(np.float32)


def write_audio(path: Path, audio: np.ndarray, sample_rate: int = SAMPLE_RATE) -> None:
    ensure_dir(path.parent)
    sf.write(path, clamp_audio(audio), sample_rate, subtype="FLOAT")


def render_speech(sample_count: int) -> np.ndarray:
    say_bin = shutil.which("say")
    if say_bin is None:
        raise RuntimeError("The macOS `say` command is required to build the phone speech fixture.")

    text = (
        "This is the VX Rebalance phone speech test. "
        "The voice should move forward without simply getting brighter. "
        "The guitar should tuck back without the whole mix sounding dull."
    )

    with tempfile.TemporaryDirectory() as tmpdir:
        aiff_path = Path(tmpdir) / "speech.aiff"
        subprocess.run([say_bin, "-o", str(aiff_path), text], check=True)
        speech, sr = sf.read(aiff_path, always_2d=True, dtype="float32")

    speech = stereo(speech)
    speech = resample_linear(speech, sr, SAMPLE_RATE)

    speech = trim_or_loop(speech, sample_count)
    speech = bandlimit(speech, SAMPLE_RATE, 280.0, 3400.0)
    speech *= 0.9
    return peak_normalize(speech, 0.85)


def source_stems(track: musdb.audio_classes.MultiTrack) -> Dict[str, np.ndarray]:
    stems = {
        "vocals": stereo(track.targets["vocals"].audio),
        "drums": stereo(track.targets["drums"].audio),
        "bass": stereo(track.targets["bass"].audio),
        "other": stereo(track.targets["other"].audio),
    }
    return stems


def build_case_audio(case: CaseConfig, stems: Dict[str, np.ndarray]) -> Dict[str, np.ndarray]:
    reference = stems["vocals"]
    zeros = silence_like(reference)

    if case.name == "studio_vocal_guitar":
        out = {
            "vocals": stems["vocals"] * 1.00,
            "drums": stems["drums"] * 0.18,
            "bass": stems["bass"] * 0.18,
            "guitar": stems["other"] * 0.95,
            "piano": zeros,
            "other": zeros,
        }
    elif case.name == "live_room_band":
        out = {
            "vocals": simple_room(stems["vocals"] * 0.80, SAMPLE_RATE),
            "drums": simple_room(stems["drums"] * 0.95, SAMPLE_RATE),
            "bass": simple_room(stems["bass"] * 0.85, SAMPLE_RATE),
            "guitar": simple_room(stems["other"] * 0.90, SAMPLE_RATE),
            "piano": zeros,
            "other": zeros,
        }
    elif case.name == "phone_speech_acoustic_guitar":
        speech = render_speech(reference.shape[0])
        guitar = bandlimit(stems["other"] * 0.72, SAMPLE_RATE, 110.0, 4200.0)
        guitar = peak_normalize(guitar, 0.55)
        out = {
            "vocals": speech,
            "drums": zeros,
            "bass": zeros,
            "guitar": guitar,
            "piano": zeros,
            "other": zeros,
        }
    elif case.name == "drum_heavy_loop":
        out = {
            "vocals": stems["vocals"] * 0.10,
            "drums": stems["drums"] * 1.15,
            "bass": stems["bass"] * 0.28,
            "guitar": stems["other"] * 0.22,
            "piano": zeros,
            "other": zeros,
        }
    elif case.name == "bass_heavy_mix":
        out = {
            "vocals": stems["vocals"] * 0.14,
            "drums": stems["drums"] * 0.32,
            "bass": stems["bass"] * 1.18,
            "guitar": zeros,
            "piano": zeros,
            "other": stems["other"] * 0.32,
        }
    else:
        raise ValueError(f"Unhandled case: {case.name}")

    original = np.zeros_like(reference, dtype=np.float32)
    for key, value in out.items():
        if key != "piano":
            original += value
    out["original"] = peak_normalize(original, 0.9)
    return {name: peak_normalize(audio, 0.9) for name, audio in out.items()}


def write_case(case: CaseConfig, audio_by_stem: Dict[str, np.ndarray]) -> dict:
    case_dir = CASE_ROOT / case.name
    ensure_dir(case_dir)

    file_map = {
        "_original.wav": audio_by_stem["original"],
        "_vocals.wav": audio_by_stem["vocals"],
        "_drums.wav": audio_by_stem["drums"],
        "_bass.wav": audio_by_stem["bass"],
        "_guitar.wav": audio_by_stem["guitar"],
        "_piano.wav": audio_by_stem["piano"],
        "_other.wav": audio_by_stem["other"],
    }

    for file_name, audio in file_map.items():
        write_audio(case_dir / file_name, audio)

    metadata = {
        "name": case.name,
        "description": case.description,
        "source_track": case.source_track,
        "recording_type": case.recording_type,
        "primary_check": case.primary_check,
        "path": str(case_dir.relative_to(ROOT)),
        "files": sorted(file_map.keys()),
    }
    with open(case_dir / "case.json", "w", encoding="utf-8") as handle:
        json.dump(metadata, handle, indent=2)
        handle.write("\n")
    return metadata


def prepare_cases() -> None:
    ensure_dir(CASE_ROOT)
    tracks = db_tracks()
    manifest = {"cases": []}

    for case in CASE_CONFIGS:
        if case.source_track not in tracks:
            raise RuntimeError(f"MUSDB preview track not found: {case.source_track}")
        stems = source_stems(tracks[case.source_track])
        case_audio = build_case_audio(case, stems)
        metadata = write_case(case, case_audio)
        manifest["cases"].append(metadata)

    with open(MANIFEST_PATH, "w", encoding="utf-8") as handle:
        json.dump(manifest, handle, indent=2)
        handle.write("\n")

    print(f"Wrote {len(manifest['cases'])} listening cases to {CASE_ROOT}")


def run_measure(binary: Path, case_path: Path, boost_db: float, recording_type: str, lane_id: str | None) -> str:
    cmd = [str(binary), str(case_path), f"{boost_db:g}", recording_type, "dsp"]
    if lane_id is not None:
        cmd.append(lane_id)
    result = subprocess.run(cmd, cwd=ROOT, text=True, capture_output=True, check=True)
    return result.stdout


def generate_report(binary: Path) -> None:
    if not MANIFEST_PATH.exists():
        raise RuntimeError("Fixtures are missing. Run `prepare` first.")
    if not binary.exists():
        raise RuntimeError(f"Measure binary not found: {binary}")

    manifest = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
    report_lines = ["# ReBalance Listening Protocol Report", ""]

    checks = {
        "studio_vocal_guitar": [("vocals", 24.0), ("guitar", -24.0)],
        "live_room_band": [("vocals", 24.0), ("guitar", -24.0)],
        "phone_speech_acoustic_guitar": [("vocals", 24.0), ("guitar", -24.0)],
        "drum_heavy_loop": [("drums", -24.0)],
        "bass_heavy_mix": [("bass", -24.0)],
    }

    for case in manifest["cases"]:
        case_name = case["name"]
        case_path = ROOT / case["path"]
        report_lines.append(f"## {case_name}")
        report_lines.append("")
        report_lines.append(case["description"])
        report_lines.append("")

        neutral = run_measure(binary, case_path, 0.0, case["recording_type"], None)
        report_lines.append("### Neutral")
        report_lines.append("")
        report_lines.append("```text")
        report_lines.append(neutral.strip())
        report_lines.append("```")
        report_lines.append("")

        for lane, boost_db in checks.get(case_name, []):
            output = run_measure(binary, case_path, boost_db, case["recording_type"], lane)
            label = f"{lane} {'+' if boost_db >= 0 else ''}{boost_db:g} dB"
            report_lines.append(f"### {label}")
            report_lines.append("")
            report_lines.append("```text")
            report_lines.append(output.strip())
            report_lines.append("```")
            report_lines.append("")

    report_path = ROOT / "tasks" / "reports" / "rebalance_listening_protocol_report.md"
    ensure_dir(report_path.parent)
    report_path.write_text("\n".join(report_lines), encoding="utf-8")
    print(f"Wrote report to {report_path}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Prepare and evaluate ReBalance listening fixtures.")
    sub = parser.add_subparsers(dest="command", required=True)

    sub.add_parser("prepare", help="Download sources and generate fixture cases.")

    report = sub.add_parser("report", help="Run VXRebalanceMeasure against the generated cases.")
    report.add_argument(
        "--binary",
        type=Path,
        default=ROOT / "build" / "VXRebalanceMeasure",
        help="Path to the VXRebalanceMeasure executable.",
    )

    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        if args.command == "prepare":
            prepare_cases()
        elif args.command == "report":
            generate_report(args.binary)
        else:
            raise ValueError(f"Unknown command: {args.command}")
    except subprocess.CalledProcessError as exc:
        sys.stderr.write(exc.stderr or str(exc))
        return exc.returncode or 1
    except Exception as exc:  # pragma: no cover - CLI surface
        sys.stderr.write(f"{exc}\n")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
