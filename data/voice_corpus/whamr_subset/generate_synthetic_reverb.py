#!/usr/bin/env python3
"""Generate synthetic reverberant versions of voice corpus for Deverb testing."""

import os
from pathlib import Path

try:
    import pyroomacoustics as pra
    import numpy as np
    import soundfile as sf
except ImportError:
    print("Required: pip install pyroomacoustics soundfile numpy")
    exit(1)

CORPUS_ROOT = Path("/Users/andrzejmarczewski/Documents/GitHub/VxStudio/data/voice_corpus")
INPUT_WAV = CORPUS_ROOT / "wav"
OUTPUT_DIR = Path("/Users/andrzejmarczewski/Documents/GitHub/VxStudio/data/voice_corpus/whamr_subset")
OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

def generate_reverberant_versions(input_file, output_dir, rt60_list=[0.3, 0.6, 1.0]):
    """Generate reverberant versions at different T60 times."""

    # Load audio
    data, sr = sf.read(input_file)
    if len(data.shape) > 1:
        data = data[:, 0]  # Convert to mono

    print(f"Processing {input_file.stem}...")

    for rt60 in rt60_list:
        # Create shoebox room (8m x 6m x 3m)
        room_dim = [8, 6, 3]
        room = pra.ShoeBox(room_dim, fs=sr, materials=pra.Material(rt60),
                           ray_tracing=True, air_absorption=True)

        # Microphone at (4, 3, 1.5) and source at (2, 2, 1)
        room.add_microphone([4, 3, 1.5])
        room.add_source([2, 2, 1], signal=data)

        # Simulate
        room.simulate()
        reverberant = room.mic_array.signals[0, :]

        # Save
        output_file = output_dir / f"{input_file.stem}_rt60_{rt60:.1f}.wav"
        sf.write(output_file, reverberant, sr)
        print(f"  ✓ {output_file.name}")

# Generate reverberant versions
if INPUT_WAV.exists():
    for wav_file in INPUT_WAV.glob("*.wav"):
        generate_reverberant_versions(wav_file, OUTPUT_DIR)
    print(f"\n✓ Generated reverberant samples in {OUTPUT_DIR}")
else:
    print(f"✗ Input directory not found: {INPUT_WAV}")
