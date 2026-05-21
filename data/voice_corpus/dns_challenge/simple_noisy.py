#!/usr/bin/env python3
"""Generate noisy speech samples at various SNR levels without external dependencies."""

import numpy as np
import soundfile as sf
from pathlib import Path

CORPUS_ROOT = Path("/Users/andrzejmarczewski/Documents/GitHub/VxStudio/data/voice_corpus")
INPUT_WAV = CORPUS_ROOT / "wav"
OUTPUT_DIR = Path("/Users/andrzejmarczewski/Documents/GitHub/VxStudio/data/voice_corpus/dns_challenge")
OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

def generate_white_noise(length, sr):
    """Generate white noise."""
    return np.random.randn(length) * 0.1

def generate_pink_noise(length, sr):
    """Generate pink noise using simple filtering."""
    white = np.random.randn(length)
    # Simple first-order low-pass for pink noise approximation
    filtered = np.zeros_like(white)
    alpha = 0.95  # Time constant
    filtered[0] = white[0]
    for i in range(1, len(white)):
        filtered[i] = alpha * filtered[i - 1] + (1 - alpha) * white[i]
    return filtered * 0.1

def add_noise_at_snr(signal_data, noise, snr_db):
    """Add noise to signal at target SNR."""
    signal_rms = np.sqrt(np.mean(signal_data ** 2))
    noise_rms = np.sqrt(np.mean(noise ** 2))

    if noise_rms == 0:
        return signal_data

    # SNR = 20 * log10(signal_rms / noise_rms)
    snr_linear = 10 ** (snr_db / 20)
    noise_scale = signal_rms / (snr_linear * noise_rms)

    noisy = signal_data + noise_scale * noise[:len(signal_data)]
    return np.clip(noisy, -1, 1)

# Generate noisy versions
if INPUT_WAV.exists():
    print(f"Generating noisy speech samples in {OUTPUT_DIR}...")

    snr_levels = [-5, 0, 5, 10, 15]  # dB

    for wav_file in sorted(INPUT_WAV.glob("*.wav")):
        data, sr = sf.read(wav_file)
        if len(data.shape) > 1:
            data = data[:, 0]  # Convert to mono

        print(f"\nProcessing {wav_file.stem}...")

        # Generate with different noise types
        for noise_type in ["white", "pink"]:
            # Generate noise for this file
            if noise_type == "white":
                noise = generate_white_noise(len(data), sr)
            else:
                noise = generate_pink_noise(len(data), sr)

            for snr in snr_levels:
                try:
                    noisy = add_noise_at_snr(data, noise, snr)
                    output_file = OUTPUT_DIR / f"{wav_file.stem}_{noise_type}_{snr:+d}dB.wav"
                    sf.write(output_file, noisy, sr)
                    print(f"  ✓ {output_file.name}")
                except Exception as e:
                    print(f"  ✗ {noise_type} {snr}dB: {e}")

    print(f"\n✓ Generated noisy speech samples in {OUTPUT_DIR}")
    print(f"  Files: {len(list(OUTPUT_DIR.glob('*.wav')))}")
else:
    print(f"✗ Input directory not found: {INPUT_WAV}")
