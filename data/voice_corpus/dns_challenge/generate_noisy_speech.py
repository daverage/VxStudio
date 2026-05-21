#!/usr/bin/env python3
"""Generate noisy speech samples at various SNRs using existing corpus + synthetic noise."""

import os
import numpy as np
from pathlib import Path

try:
    import soundfile as sf
except ImportError:
    print("Required: pip install soundfile")
    exit(1)

CORPUS_ROOT = Path("/Users/andrzejmarczewski/Documents/GitHub/VxStudio/data/voice_corpus")
INPUT_WAV = CORPUS_ROOT / "wav"
OUTPUT_DIR = Path("/Users/andrzejmarczewski/Documents/GitHub/VxStudio/data/voice_corpus/dns_challenge")
OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

# Simple synthetic noise generators
def generate_white_noise(length, sr):
    return np.random.randn(length) * 0.1

def generate_pink_noise(length, sr):
    """Rough pink noise approximation."""
    white = np.random.randn(length)
    # Simple low-pass filter
    filtered = np.zeros_like(white)
    alpha = 0.99
    filtered[0] = white[0]
    for i in range(1, len(white)):
        filtered[i] = alpha * filtered[i-1] + (1 - alpha) * white[i]
    return filtered * 0.1

def add_noise_at_snr(signal, noise, snr_db):
    """Add noise to signal at target SNR."""
    signal_power = np.mean(signal ** 2)
    noise_power = np.mean(noise ** 2)

    # Scale noise to achieve target SNR
    snr_linear = 10 ** (snr_db / 10)
    noise_scale = np.sqrt(signal_power / (snr_linear * noise_power))

    noisy = signal + noise_scale * noise[:len(signal)]
    return np.clip(noisy, -1, 1)

# Generate noisy versions
if INPUT_WAV.exists():
    snr_levels = [-5, 0, 5, 10, 15]  # dB

    for wav_file in INPUT_WAV.glob("*.wav"):
        data, sr = sf.read(wav_file)
        if len(data.shape) > 1:
            data = data[:, 0]

        print(f"Processing {wav_file.stem}...")

        # Generate at multiple SNRs with different noise types
        for noise_type in ["white", "pink"]:
            if noise_type == "white":
                noise = generate_white_noise(len(data), sr)
            else:
                noise = generate_pink_noise(len(data), sr)

            for snr in snr_levels:
                noisy = add_noise_at_snr(data, noise, snr)
                output_file = OUTPUT_DIR / f"{wav_file.stem}_{noise_type}_{snr:+d}dB.wav"
                sf.write(output_file, noisy, sr)
                print(f"  ✓ {output_file.name}")

    print(f"\n✓ Generated {len(snr_levels)} SNR levels × 2 noise types")
else:
    print(f"✗ Input directory not found: {INPUT_WAV}")
