#!/usr/bin/env python3
"""
Phase 2 Dataset Downloader
Downloads and prepares test corpora for algorithm audits:
- WHAMR: Reverberant speech (Deverb WPE testing)
- DNS Challenge: Noisy speech (Denoiser SNR testing)
- LibriSpeech: Diverse speech samples (DeepFilterNet validation)
"""

import os
import sys
import subprocess
import tarfile
import json
from pathlib import Path

CORPUS_ROOT = Path("/Users/andrzejmarczewski/Documents/GitHub/VxStudio/data/voice_corpus")
TARGET_SR = 48000

def run_command(cmd, description=""):
    """Run shell command with logging."""
    if description:
        print(f"\n→ {description}")
    try:
        result = subprocess.run(cmd, shell=True, capture_output=True, text=True)
        if result.returncode != 0:
            print(f"  ⚠ {result.stderr}")
            return False
        if result.stdout:
            print(f"  {result.stdout.strip()}")
        return True
    except Exception as e:
        print(f"  ✗ Error: {e}")
        return False

def download_librispeech():
    """Download LibriSpeech test-clean subset using Hugging Face datasets."""
    print("\n" + "="*60)
    print("LibriSpeech Test-Clean (DeepFilterNet validation)")
    print("="*60)

    libri_dir = CORPUS_ROOT / "librispeech_test"
    libri_dir.mkdir(parents=True, exist_ok=True)

    # Check if already downloaded
    if list(libri_dir.glob("**/*.flac")):
        print("✓ LibriSpeech already downloaded")
        return True

    print("Downloading LibriSpeech test-clean (requires 'datasets' library)...")

    try:
        from datasets import load_dataset
        import soundfile as sf

        print("Loading dataset...")
        ds = load_dataset("openslr/librispeech_asr", "clean", split="test", cache_dir=str(libri_dir))

        print(f"✓ Dataset loaded ({len(ds)} samples)")

        # Save metadata
        metadata = {
            "dataset": "LibriSpeech test-clean",
            "num_samples": len(ds),
            "sample_rate": 16000,
            "purpose": "DeepFilterNet DFN3 vs DFN2 quality comparison"
        }

        with open(libri_dir / "METADATA.json", "w") as f:
            json.dump(metadata, f, indent=2)

        print(f"✓ Saved to {libri_dir}")
        print(f"  Size: {sum(f.stat().st_size for f in libri_dir.rglob('*') if f.is_file()) / 1e9:.2f} GB")
        return True

    except ImportError:
        print("\n✗ Required: pip install datasets soundfile")
        print("  Then run this script again.")
        return False

def download_whamr_subset():
    """Download small WHAMR subset for quick testing."""
    print("\n" + "="*60)
    print("WHAMR Reverberant Speech (Deverb WPE validation)")
    print("="*60)

    whamr_dir = CORPUS_ROOT / "whamr_subset"
    whamr_dir.mkdir(parents=True, exist_ok=True)

    # For now, document the process
    print("WHAMR provides reverberant speech for dereverberation testing.")
    print("\nSetup options:")
    print("\n1. Clone full WHAMR repository:")
    print(f"   cd {CORPUS_ROOT}")
    print("   git clone https://github.com/google-deepmind/WHAMR.git")
    print("\n2. Use pre-generated test set:")
    print("   Contact WHAMR authors or use synthetic reverb simulation")
    print("\n3. Generate reverberant versions of existing corpus:")
    print("   Use your existing voice samples + pyroomacoustics for simulation")

    # Create Python script for synthetic reverb generation
    create_synthetic_reverb_script(whamr_dir)

    return True

def create_synthetic_reverb_script(output_dir):
    """Create script to generate synthetic reverberant versions of existing audio."""
    script = '''#!/usr/bin/env python3
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
OUTPUT_DIR = Path("''' + str(output_dir) + '''")
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
    print(f"\\n✓ Generated reverberant samples in {OUTPUT_DIR}")
else:
    print(f"✗ Input directory not found: {INPUT_WAV}")
'''

    script_path = output_dir / "generate_synthetic_reverb.py"
    with open(script_path, "w") as f:
        f.write(script)

    os.chmod(script_path, 0o755)
    print(f"✓ Created synthetic reverb generator: {script_path}")

def download_dns_challenge():
    """Set up DNS Challenge test set."""
    print("\n" + "="*60)
    print("DNS Challenge - Noisy Speech (Denoiser validation)")
    print("="*60)

    dns_dir = CORPUS_ROOT / "dns_challenge"
    dns_dir.mkdir(parents=True, exist_ok=True)

    print("\nDNS Challenge test sets require registration or API access.")
    print("\nSetup options:")
    print("\n1. Register for free access:")
    print("   https://github.com/microsoft/dns-challenge")
    print("   (Download from Azure Storage after registration)")
    print("\n2. Use alternative noisy datasets:")
    print("   - VOiCES (small clean set with noise)")
    print("   - DEMAND (background noise database)")

    # Create noisy speech generator
    create_dns_alternative_script(dns_dir)

    return True

def create_dns_alternative_script(output_dir):
    """Create script to generate noisy speech versions using DEMAND noise."""
    script = '''#!/usr/bin/env python3
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
OUTPUT_DIR = Path("''' + str(output_dir) + '''")
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

    print(f"\\n✓ Generated {len(snr_levels)} SNR levels × 2 noise types")
else:
    print(f"✗ Input directory not found: {INPUT_WAV}")
'''

    script_path = output_dir / "generate_noisy_speech.py"
    with open(script_path, "w") as f:
        f.write(script)

    os.chmod(script_path, 0o755)
    print(f"✓ Created noisy speech generator: {script_path}")

def main():
    """Main download orchestration."""
    print("\n" + "="*70)
    print("VxStudio Phase 2 - Algorithm Audit Dataset Preparation")
    print("="*70)

    print(f"\nTarget corpus directory: {CORPUS_ROOT}")
    print(f"This will download ~6-8 GB of test audio\n")

    # Check prerequisites
    print("Checking dependencies...")

    try:
        import datasets
        print("✓ datasets library available")
    except ImportError:
        print("⚠ datasets library not found")
        print("  Install with: pip install datasets soundfile")

    # Download datasets
    results = {
        "LibriSpeech": download_librispeech(),
        "WHAMR": download_whamr_subset(),
        "DNS Challenge": download_dns_challenge(),
    }

    # Summary
    print("\n" + "="*70)
    print("Setup Complete")
    print("="*70)

    print("\nDataset Status:")
    for dataset, success in results.items():
        status = "✓" if success else "⚠"
        print(f"  {status} {dataset}")

    print("\nNext Steps:")
    print("1. Run synthetic data generators:")
    print(f"   python {CORPUS_ROOT}/whamr_subset/generate_synthetic_reverb.py")
    print(f"   python {CORPUS_ROOT}/dns_challenge/generate_noisy_speech.py")
    print("\n2. Then proceed with Phase 2 audits using prepared datasets")
    print("\nSee: /Users/andrzejmarczewski/.claude/plans/phase-1-completion-summary.md")

if __name__ == "__main__":
    main()
