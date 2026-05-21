#!/usr/bin/env python3
"""Generate simple reverberant versions of voice samples using impulse convolution."""

import numpy as np
import soundfile as sf
from pathlib import Path
from scipy import signal

CORPUS_ROOT = Path("/Users/andrzejmarczewski/Documents/GitHub/VxStudio/data/voice_corpus")
INPUT_WAV = CORPUS_ROOT / "wav"
OUTPUT_DIR = Path("/Users/andrzejmarczewski/Documents/GitHub/VxStudio/data/voice_corpus/whamr_subset")
OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

def generate_impulse_response(rt60, sr=48000, room_size=50.0):
    """Generate synthetic room impulse response using exponential decay."""
    # Simple exponential decay to simulate reverberation
    duration = rt60  # Seconds
    samples = int(sr * duration)

    # Create decay envelope (exponential)
    decay = np.exp(-np.arange(samples) / (sr * rt60 / 6.9))  # -60dB at rt60

    # Add some reflections (early echoes) before decay
    impulse = np.zeros(samples)
    impulse[0] = 1.0  # Direct sound

    # Add early reflections at ~20ms intervals
    for i in range(1, 5):
        delay = int(i * 0.020 * sr)
        if delay < samples:
            impulse[delay] = 0.5 / i

    # Apply decay envelope
    impulse = impulse * decay

    return impulse / np.max(np.abs(impulse))

def add_reverb(signal_data, rt60, sr=48000):
    """Convolve signal with synthetic impulse response."""
    impulse = generate_impulse_response(rt60, sr)
    # Convolve and trim to approximate original length
    reverberant = signal.fftconvolve(signal_data, impulse, mode='same')
    # Normalize to prevent clipping
    return np.clip(reverberant / np.max(np.abs(reverberant)), -1, 1)

# Generate reverberant versions
if INPUT_WAV.exists():
    print(f"Generating reverberant speech samples in {OUTPUT_DIR}...")

    for wav_file in sorted(INPUT_WAV.glob("*.wav")):
        data, sr = sf.read(wav_file)
        if len(data.shape) > 1:
            data = data[:, 0]  # Convert to mono

        print(f"\nProcessing {wav_file.stem}...")

        # Generate at 3 RT60 values (small, medium, large rooms)
        for rt60 in [0.3, 0.6, 1.0]:
            try:
                reverberant = add_reverb(data, rt60, sr)
                output_file = OUTPUT_DIR / f"{wav_file.stem}_rt60_{rt60:.1f}.wav"
                sf.write(output_file, reverberant, sr)
                print(f"  ✓ {output_file.name}")
            except Exception as e:
                print(f"  ✗ RT60 {rt60}: {e}")

    print(f"\n✓ Generated reverberant samples in {OUTPUT_DIR}")
    print(f"  Files: {len(list(OUTPUT_DIR.glob('*.wav')))}")
else:
    print(f"✗ Input directory not found: {INPUT_WAV}")
