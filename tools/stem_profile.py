#!/usr/bin/env python3
from __future__ import annotations

import argparse
import math
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Tuple

import librosa
import matplotlib.pyplot as plt
import numpy as np
from scipy import signal


STEM_ORDER = ["vocals", "drums", "bass", "guitar", "piano", "other"]
SUPPORTED_AUDIO_EXTENSIONS = (".wav", ".mp3", ".m4a", ".flac", ".aif", ".aiff")


@dataclass
class AudioData:
    samples: np.ndarray  # shape: channels, samples
    sample_rate: int

    @property
    def mono(self) -> np.ndarray:
        return np.mean(self.samples, axis=0)


def load_audio(path: Path, target_sr: int | None = None) -> AudioData:
    samples, sr = librosa.load(path.as_posix(), sr=target_sr, mono=False)
    if samples.ndim == 1:
        samples = np.expand_dims(samples, axis=0)
    return AudioData(samples=samples.astype(np.float32), sample_rate=int(sr))


def find_audio_file(stem_dir: Path, suffix: str) -> Path | None:
    exact_candidates = [stem_dir / f"{suffix}{ext}" for ext in SUPPORTED_AUDIO_EXTENSIONS]
    for candidate in exact_candidates:
        if candidate.exists():
            return candidate

    for ext in SUPPORTED_AUDIO_EXTENSIONS:
        matches = sorted(stem_dir.glob(f"*{suffix}{ext}"))
        if matches:
            return matches[0]

    return None


def match_length(audio: AudioData, num_samples: int) -> AudioData:
    channels, current = audio.samples.shape
    if current == num_samples:
        return audio
    out = np.zeros((channels, num_samples), dtype=np.float32)
    copy = min(current, num_samples)
    out[:, :copy] = audio.samples[:, :copy]
    return AudioData(out, audio.sample_rate)


def stft_power(mono: np.ndarray, sr: int, n_fft: int = 4096, hop: int = 1024) -> Tuple[np.ndarray, np.ndarray, np.ndarray]:
    freqs, times, stft = signal.stft(
        mono,
        fs=sr,
        window="hann",
        nperseg=n_fft,
        noverlap=n_fft - hop,
        boundary=None,
        padded=False,
    )
    power = np.abs(stft) ** 2
    return freqs, times, power


def hz_to_str(hz: float) -> str:
    if hz >= 1000.0:
        return f"{hz / 1000.0:.1f} kHz"
    return f"{hz:.0f} Hz"


def signal_quality(audio: AudioData) -> Dict[str, float]:
    left = audio.samples[0]
    right = audio.samples[1] if audio.samples.shape[0] > 1 else left
    mid = 0.5 * (left + right)
    side = 0.5 * (left - right)
    mono_score = float(np.clip(1.0 - (np.mean(np.abs(side)) / max(1e-9, np.mean(np.abs(mid)) * 0.08)), 0.0, 1.0))

    peak = float(np.max(np.abs(mid)))
    rms = float(np.sqrt(np.mean(mid * mid) + 1e-9))
    crest = peak / max(1e-9, rms)
    compression_score = float(1.0 - np.clip((crest - 1.5) / (6.0 - 1.5), 0.0, 1.0))

    freqs, _, power = stft_power(mid, audio.sample_rate)
    mean_power = np.mean(power, axis=1)
    lo = float(np.mean(mean_power[freqs < 500.0])) if np.any(freqs < 500.0) else 0.0
    hi = float(np.mean(mean_power[freqs > 4000.0])) if np.any(freqs > 4000.0) else 0.0
    tilt_ratio = lo / max(1e-9, hi)
    tilt_score = float(np.clip((tilt_ratio - 3.0) / (8.0 - 3.0), 0.0, 1.0))

    separation_confidence = float(1.0 - np.clip(
        0.45 * mono_score + 0.35 * compression_score + 0.20 * tilt_score,
        0.0,
        1.0,
    ))

    return {
        "mono_score": mono_score,
        "compression_score": compression_score,
        "tilt_score": tilt_score,
        "separation_confidence": separation_confidence,
    }


def build_band_table(freqs: np.ndarray, stem_powers: Dict[str, np.ndarray], top_k: int = 3) -> List[Dict[str, object]]:
    edges = np.geomspace(20.0, min(20000.0, freqs[-1]), num=19)
    rows: List[Dict[str, object]] = []
    total_power = np.zeros_like(freqs)
    for power in stem_powers.values():
        total_power += power

    for low, high in zip(edges[:-1], edges[1:]):
        mask = (freqs >= low) & (freqs < high)
        if not np.any(mask):
            continue

        per_stem = {}
        stem_values = []
        for stem, power in stem_powers.items():
            value = float(np.mean(power[mask]))
            per_stem[stem] = value
            stem_values.append(value)

        total = sum(stem_values)
        if total <= 1e-12:
            continue

        shares = {stem: value / total for stem, value in per_stem.items()}
        ordered = sorted(shares.items(), key=lambda item: item[1], reverse=True)
        dominance = ordered[0][1]
        second = ordered[1][1] if len(ordered) > 1 else 0.0
        entropy = -sum(share * math.log(max(share, 1e-12), 2.0) for share in shares.values())
        conflict = float(np.clip(entropy / math.log(len(shares), 2.0), 0.0, 1.0))

        rows.append({
            "low_hz": float(low),
            "high_hz": float(high),
            "dominant_stem": ordered[0][0],
            "dominance": dominance,
            "second_share": second,
            "conflict": conflict,
            "top_stems": ordered[:top_k],
        })
    return rows


def recommend_regions(bands: List[Dict[str, object]], stem_name: str) -> Tuple[List[str], List[str]]:
    safe = []
    conflict = []
    for band in bands:
        label = f"{hz_to_str(float(band['low_hz']))} - {hz_to_str(float(band['high_hz']))}"
        dominant = str(band["dominant_stem"])
        dominance = float(band["dominance"])
        band_conflict = float(band["conflict"])
        second_share = float(band["second_share"])

        if dominant == stem_name and dominance >= 0.58 and band_conflict <= 0.62 and second_share <= 0.26:
            safe.append(label)
        if dominant == stem_name and (band_conflict >= 0.75 or second_share >= 0.32):
            conflict.append(label)
    return safe[:6], conflict[:6]


def plot_profiles(freqs: np.ndarray,
                  stem_powers: Dict[str, np.ndarray],
                  bands: List[Dict[str, object]],
                  out_dir: Path) -> Tuple[Path, Path]:
    profile_path = out_dir / "stem_profiles.png"
    conflict_path = out_dir / "stem_conflict.png"

    plt.figure(figsize=(14, 7))
    for stem in STEM_ORDER:
        if stem not in stem_powers:
            continue
        db = 10.0 * np.log10(np.maximum(stem_powers[stem], 1e-12))
        plt.semilogx(freqs[1:], db[1:], label=stem.capitalize(), linewidth=2)
    plt.xlim(20, min(20000.0, freqs[-1]))
    plt.ylim(-120, None)
    plt.grid(True, which="both", alpha=0.2)
    plt.xlabel("Frequency (Hz)")
    plt.ylabel("Mean power (dB)")
    plt.title("Stem Spectral Profiles")
    plt.legend()
    plt.tight_layout()
    plt.savefig(profile_path, dpi=160)
    plt.close()

    centers = np.array([math.sqrt(float(row["low_hz"]) * float(row["high_hz"])) for row in bands], dtype=np.float32)
    dominance = np.array([float(row["dominance"]) for row in bands], dtype=np.float32)
    conflict = np.array([float(row["conflict"]) for row in bands], dtype=np.float32)

    plt.figure(figsize=(14, 6))
    plt.semilogx(centers, dominance, label="Dominant stem share", linewidth=2)
    plt.semilogx(centers, conflict, label="Conflict score", linewidth=2)
    plt.xlim(20, min(20000.0, freqs[-1]))
    plt.ylim(0.0, 1.0)
    plt.grid(True, which="both", alpha=0.2)
    plt.xlabel("Frequency (Hz)")
    plt.ylabel("0-1")
    plt.title("Band Dominance vs Conflict")
    plt.legend()
    plt.tight_layout()
    plt.savefig(conflict_path, dpi=160)
    plt.close()

    return profile_path, conflict_path


def write_report(out_path: Path,
                 mix_name: str,
                 quality: Dict[str, float],
                 bands: List[Dict[str, object]],
                 safe_regions: Dict[str, List[str]],
                 conflict_regions: Dict[str, List[str]],
                 plots: Tuple[Path, Path]) -> None:
    profile_path, conflict_path = plots
    lines: List[str] = []
    lines.append(f"# Stem Profile Report: {mix_name}")
    lines.append("")
    lines.append("## Recording condition")
    lines.append(f"- Mono score: `{quality['mono_score']:.3f}`")
    lines.append(f"- Compression score: `{quality['compression_score']:.3f}`")
    lines.append(f"- Tilt score: `{quality['tilt_score']:.3f}`")
    lines.append(f"- Separation confidence: `{quality['separation_confidence']:.3f}`")
    lines.append("")
    lines.append("## Plots")
    lines.append(f"- [Stem spectral profiles]({profile_path.name})")
    lines.append(f"- [Band dominance vs conflict]({conflict_path.name})")
    lines.append("")
    lines.append("## Suggested safe regions")
    for stem in STEM_ORDER:
        values = safe_regions.get(stem, [])
        label = ", ".join(values) if values else "No strongly safe regions found"
        lines.append(f"- `{stem}`: {label}")
    lines.append("")
    lines.append("## High-conflict regions")
    for stem in STEM_ORDER:
        values = conflict_regions.get(stem, [])
        label = ", ".join(values) if values else "No dominant high-conflict regions flagged"
        lines.append(f"- `{stem}`: {label}")
    lines.append("")
    lines.append("## Dominant band summary")
    for band in bands[:14]:
        top = ", ".join(f"{stem}:{share:.2f}" for stem, share in band["top_stems"])  # type: ignore[index]
        lines.append(
            f"- `{hz_to_str(float(band['low_hz']))} - {hz_to_str(float(band['high_hz']))}`: "
            f"dominant `{band['dominant_stem']}` share `{float(band['dominance']):.2f}`, "
            f"conflict `{float(band['conflict']):.2f}`; top `{top}`"
        )
    lines.append("")
    lines.append("## Rebalance notes")
    lines.append("- Use the safe regions as heuristic candidates, not as hard ownership bands.")
    lines.append("- Treat high-conflict bands as low-confidence areas where sliders should move less aggressively.")
    lines.append("- Compare multiple recording conditions before turning any region into a product-level prior.")

    out_path.write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description="Profile split stems for VX Rebalance R&D.")
    parser.add_argument("stem_dir", type=Path, help="Directory containing an original mix plus split stems named with *_original / *_vocals / *_drums / *_bass / *_guitar / *_piano / *_other suffixes.")
    parser.add_argument("--out-dir", type=Path, default=Path("tasks/reports/stem-profile-brightside"))
    args = parser.parse_args()

    stem_dir = args.stem_dir
    out_dir = args.out_dir
    out_dir.mkdir(parents=True, exist_ok=True)

    original_path = find_audio_file(stem_dir, "_original")
    if original_path is None:
        raise FileNotFoundError(f"Could not find an original mix in {stem_dir} using '*_original' plus a supported audio extension.")

    original = load_audio(original_path)
    stems: Dict[str, AudioData] = {}
    for stem in STEM_ORDER:
        path = find_audio_file(stem_dir, f"_{stem}")
        if path is not None:
            stems[stem] = match_length(load_audio(path, target_sr=original.sample_rate), original.samples.shape[1])

    quality = signal_quality(original)

    freqs, _, _ = stft_power(original.mono, original.sample_rate)
    stem_powers: Dict[str, np.ndarray] = {}
    for stem, audio in stems.items():
        stem_freqs, _, power = stft_power(audio.mono, audio.sample_rate)
        if len(stem_freqs) != len(freqs):
            raise RuntimeError("STFT frequency grid mismatch")
        stem_powers[stem] = np.mean(power, axis=1)

    bands = build_band_table(freqs, stem_powers)
    safe_regions: Dict[str, List[str]] = {}
    conflict_regions: Dict[str, List[str]] = {}
    for stem in STEM_ORDER:
        if stem not in stem_powers:
            continue
        safe, conflict = recommend_regions(bands, stem)
        safe_regions[stem] = safe
        conflict_regions[stem] = conflict

    plots = plot_profiles(freqs, stem_powers, bands, out_dir)
    report_path = out_dir / "report.md"
    write_report(report_path, stem_dir.name, quality, bands, safe_regions, conflict_regions, plots)
    print(f"Wrote report: {report_path}")
    print(f"Wrote plots: {plots[0]}, {plots[1]}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
