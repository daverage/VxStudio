#!/usr/bin/env python3
"""
VX Rebalance v2.0 Open-Unmix bridge

Two practical jobs:
1. Export the local official 4-head Open-Unmix bundle to ONNX.
2. Run those ONNX heads on an audio file and write mask tensors for tuning.

This intentionally uses the lightweight v2.0 contract:
  vocals / drums / bass / other

`guitar` is derived later in DSP or offline analysis from `other`.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Dict, List

import librosa
import numpy as np
import onnxruntime as ort
import soundfile as sf
import torch
import torch.nn as nn
import openunmix.utils as umx_utils


TARGETS = ["vocals", "drums", "bass", "other"]
SAMPLE_RATE = 44100
N_FFT = 4096
HOP = 1024
BINS = N_FFT // 2 + 1


class CombinedOpenUnmixHeads(nn.Module):
    def __init__(self, target_models: Dict[str, nn.Module]) -> None:
        super().__init__()
        self.vocals = target_models["vocals"]
        self.drums = target_models["drums"]
        self.bass = target_models["bass"]
        self.other = target_models["other"]

    def forward(self, mix_mag: torch.Tensor) -> torch.Tensor:
        outputs = [
            self.vocals(mix_mag),
            self.drums(mix_mag),
            self.bass(mix_mag),
            self.other(mix_mag),
        ]
        return torch.stack(outputs, dim=1)


def export_heads(model_dir: Path, output_dir: Path, frames: int, opset: int, validate: bool) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    models = umx_utils.load_target_models(TARGETS, model_str_or_path=str(model_dir), device="cpu", pretrained=True)
    combined = CombinedOpenUnmixHeads(models).eval()

    dummy = torch.randn(1, 2, BINS, frames, dtype=torch.float32)
    manifest = {
        "format": "vx_rebalance_umx4_onnx",
        "source_model_dir": str(model_dir),
        "sample_rate": SAMPLE_RATE,
        "nfft": N_FFT,
        "nhop": HOP,
        "targets": TARGETS,
        "input_shape": ["batch", 2, BINS, "frames"],
        "output_shape": ["batch", 4, 2, BINS, "frames"],
        "opset": opset,
    }

    onnx_path = output_dir / "vx_rebalance_umx4.onnx"
    torch.onnx.export(
        combined,
        dummy,
        onnx_path,
        input_names=["mix_mag"],
        output_names=["source_mag4"],
        dynamic_axes={
            "mix_mag": {0: "batch", 3: "frames"},
            "source_mag4": {0: "batch", 4: "frames"},
        },
        opset_version=opset,
        do_constant_folding=True,
        dynamo=False,
    )

    if validate:
        session = ort.InferenceSession(str(onnx_path), providers=["CPUExecutionProvider"])
        expected = combined(dummy).detach().cpu().numpy()
        actual = session.run(None, {"mix_mag": dummy.cpu().numpy()})[0]
        max_err = float(np.max(np.abs(expected - actual)))
        print(f"combined: exported {onnx_path} max_abs_err={max_err:.6g}")
    else:
        print(f"combined: exported {onnx_path}")

    with open(output_dir / "rebalance_umx4.json", "w", encoding="utf-8") as handle:
        json.dump(manifest, handle, indent=2)
    print(f"wrote {output_dir / 'rebalance_umx4.json'}")


def load_audio(audio_path: Path) -> np.ndarray:
    audio, rate = sf.read(audio_path, always_2d=True)
    audio = audio.astype(np.float32)
    audio = audio.T
    if audio.shape[0] == 1:
        audio = np.repeat(audio, 2, axis=0)
    elif audio.shape[0] > 2:
        audio = audio[:2, :]

    if rate != SAMPLE_RATE:
        audio = np.stack(
            [librosa.resample(channel, orig_sr=rate, target_sr=SAMPLE_RATE) for channel in audio],
            axis=0,
        )
    return audio


def stft_mag(audio: np.ndarray) -> np.ndarray:
    mags: List[np.ndarray] = []
    for channel in audio:
        stft = librosa.stft(channel, n_fft=N_FFT, hop_length=HOP, center=True, window="hann")
        mags.append(np.abs(stft).astype(np.float32))
    return np.stack(mags, axis=0)


def infer_masks(audio_path: Path,
                onnx_dir: Path,
                output_npz: Path,
                window_frames: int,
                stride_frames: int) -> None:
    manifest_path = onnx_dir / "rebalance_umx4.json"
    if not manifest_path.exists():
        raise FileNotFoundError(f"Missing manifest: {manifest_path}")

    session = ort.InferenceSession(str(onnx_dir / "vx_rebalance_umx4.onnx"),
                                   providers=["CPUExecutionProvider"])

    audio = load_audio(audio_path)
    mix_mag = stft_mag(audio)
    total_frames = mix_mag.shape[-1]
    accum = {target: np.zeros_like(mix_mag, dtype=np.float32) for target in TARGETS}
    counts = np.zeros((1, 1, total_frames), dtype=np.float32)

    start = 0
    while start < total_frames:
        end = min(total_frames, start + window_frames)
        window = mix_mag[:, :, start:end]
        if window.shape[-1] < window_frames:
            pad = window_frames - window.shape[-1]
            window = np.pad(window, ((0, 0), (0, 0), (0, pad)), mode="constant")

        model_input = window[np.newaxis, ...]
        valid_frames = min(window_frames, total_frames - start)
        outputs = session.run(None, {"mix_mag": model_input})[0][0, :, :, :, :valid_frames]
        for index, target in enumerate(TARGETS):
            accum[target][:, :, start:start + valid_frames] += outputs[index]

        counts[:, :, start:start + valid_frames] += 1.0
        if end == total_frames:
            break
        start += stride_frames

    counts = np.maximum(counts, 1.0)
    for target in TARGETS:
        accum[target] /= counts

    stacked = np.stack([accum[target] for target in TARGETS], axis=0)
    masks4 = stacked / np.maximum(np.sum(stacked, axis=0, keepdims=True), 1.0e-8)

    hz = np.linspace(0.0, SAMPLE_RATE * 0.5, BINS, dtype=np.float32)
    guitar_band = (
        0.55 * np.exp(-0.5 * ((hz - 420.0) / 220.0) ** 2)
        + 0.85 * np.exp(-0.5 * ((hz - 1500.0) / 700.0) ** 2)
        + 0.55 * np.exp(-0.5 * ((hz - 3200.0) / 850.0) ** 2)
    ).astype(np.float32)[:, np.newaxis]
    other_mask = masks4[3]
    vocal_mask = masks4[0]
    drum_mask = masks4[1]
    bass_mask = masks4[2]

    delta = np.abs(np.diff(other_mask, axis=-1, prepend=other_mask[..., :1]))
    steady = 1.0 - np.clip(delta / np.maximum(other_mask, 1.0e-6), 0.0, 1.0)
    guitar = other_mask * np.clip(guitar_band * (0.25 + 0.45 * steady), 0.0, 1.0)
    guitar *= (1.0 - 0.75 * vocal_mask)
    guitar *= (1.0 - 0.60 * drum_mask)
    guitar *= (1.0 - 0.30 * bass_mask)
    guitar = np.clip(guitar, 0.0, other_mask)
    other_residual = np.clip(other_mask - guitar, 0.0, 1.0)

    masks5 = np.stack([vocal_mask, drum_mask, bass_mask, guitar, other_residual], axis=0)
    masks5 /= np.maximum(np.sum(masks5, axis=0, keepdims=True), 1.0e-8)

    summary = {
        "audio_path": str(audio_path),
        "onnx_dir": str(onnx_dir),
        "sample_rate": SAMPLE_RATE,
        "nfft": N_FFT,
        "nhop": HOP,
        "window_frames": window_frames,
        "stride_frames": stride_frames,
        "targets4": TARGETS,
        "targets5": ["vocals", "drums", "bass", "guitar", "other"],
        "mean_share_4": {target: float(np.mean(masks4[i])) for i, target in enumerate(TARGETS)},
        "mean_share_5": {
            target: float(np.mean(masks5[i]))
            for i, target in enumerate(["vocals", "drums", "bass", "guitar", "other"])
        },
    }

    output_npz.parent.mkdir(parents=True, exist_ok=True)
    np.savez_compressed(
        output_npz,
        mix_mag=mix_mag,
        masks4=masks4,
        masks5=masks5,
        hz=hz,
        summary_json=json.dumps(summary, indent=2),
    )
    with open(output_npz.with_suffix(".json"), "w", encoding="utf-8") as handle:
        json.dump(summary, handle, indent=2)
    print(f"wrote {output_npz}")
    print(f"wrote {output_npz.with_suffix('.json')}")


def main() -> None:
    parser = argparse.ArgumentParser(description="VX Rebalance v2.0 Open-Unmix export/inference bridge")
    sub = parser.add_subparsers(dest="command", required=True)

    export_parser = sub.add_parser("export", help="Export local Open-Unmix heads to ONNX")
    export_parser.add_argument("--model-dir", type=Path,
                               default=Path("assets/rebalance/models/openunmix_umxhq_spec"))
    export_parser.add_argument("--output-dir", type=Path,
                               default=Path("assets/rebalance/models/openunmix_umxhq_spec_onnx"))
    export_parser.add_argument("--frames", type=int, default=64)
    export_parser.add_argument("--opset", type=int, default=18)
    export_parser.add_argument("--validate", action="store_true")

    infer_parser = sub.add_parser("infer", help="Run ONNX heads on audio and write mask tensors")
    infer_parser.add_argument("audio_file", type=Path)
    infer_parser.add_argument("--onnx-dir", type=Path,
                              default=Path("assets/rebalance/models/openunmix_umxhq_spec_onnx"))
    infer_parser.add_argument("--output", type=Path,
                              default=Path("tasks/reports/rebalance-openunmix-v20/inference_masks.npz"))
    infer_parser.add_argument("--window-frames", type=int, default=64)
    infer_parser.add_argument("--stride-frames", type=int, default=32)

    args = parser.parse_args()
    if args.command == "export":
        export_heads(args.model_dir, args.output_dir, args.frames, args.opset, args.validate)
    elif args.command == "infer":
        infer_masks(args.audio_file, args.onnx_dir, args.output, args.window_frames, args.stride_frames)


if __name__ == "__main__":
    main()
