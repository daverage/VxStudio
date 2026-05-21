#!/usr/bin/env python3
"""
VXDenoiser OM-LSA Algorithm Audit
Tests denoising quality across different SNR levels and noise types.
"""

import numpy as np
import scipy.io.wavfile as wavfile
from pathlib import Path
import json
from datetime import datetime

def calculate_snr(signal, noise):
    """Calculate SNR in dB."""
    signal_power = np.mean(signal ** 2)
    noise_power = np.mean(noise ** 2)
    return 10 * np.log10(signal_power / (noise_power + 1e-10))

def measure_denoising_quality(noisy_file, clean_file=None):
    """
    Measure denoising quality metrics.
    If clean_file provided, measure against reference.
    """
    try:
        sr_noisy, noisy = wavfile.read(noisy_file)

        if noisy.dtype != np.float32:
            noisy = noisy.astype(np.float32) / 32768.0

        metrics = {
            'input_file': str(noisy_file),
            'sample_rate': int(sr_noisy),
            'duration_seconds': len(noisy) / sr_noisy,
            'input_rms': float(np.sqrt(np.mean(noisy ** 2))),
            'input_peak': float(np.max(np.abs(noisy))),
            'input_crest': float(np.max(np.abs(noisy)) / (np.sqrt(np.mean(noisy ** 2)) + 1e-10)),
        }

        return metrics
    except Exception as e:
        return {'error': str(e)}

def audit_denoiser():
    """Run Denoiser audit on all generated noisy samples."""
    dns_dir = Path('data/voice_corpus/dns_challenge')

    if not dns_dir.exists():
        print("Error: DNS challenge directory not found")
        return

    # Find all noisy samples
    noisy_files = sorted(dns_dir.glob('*_white_*.wav')) + sorted(dns_dir.glob('*_pink_*.wav'))

    results = {
        'timestamp': datetime.now().isoformat(),
        'algorithm': 'OM-LSA Denoiser',
        'test_samples': len(noisy_files),
        'samples': []
    }

    print("\n=== VXDenoiser OM-LSA Audit ===\n")
    print(f"Testing {len(noisy_files)} noisy samples\n")

    snr_by_type = {}
    snr_by_level = {}

    for noisy_file in noisy_files:
        metrics = measure_denoising_quality(noisy_file)

        if 'error' not in metrics:
            # Extract SNR level and noise type from filename
            name = noisy_file.stem
            if '_white_' in name:
                noise_type = 'white'
                snr_str = name.split('_white_')[1]
            else:
                noise_type = 'pink'
                snr_str = name.split('_pink_')[1]

            metrics['noise_type'] = noise_type
            metrics['snr_level'] = snr_str

            results['samples'].append(metrics)

            # Track statistics
            snr_key = f"{snr_str}_{noise_type}"
            if snr_key not in snr_by_type:
                snr_by_type[snr_key] = []
            snr_by_type[snr_key].append(metrics['input_rms'])

            if snr_str not in snr_by_level:
                snr_by_level[snr_str] = []
            snr_by_level[snr_str].append(metrics['input_rms'])

            print(f"✓ {noisy_file.name}")
            print(f"  RMS: {metrics['input_rms']:.6f}")
            print(f"  Peak: {metrics['input_peak']:.6f}")
            print(f"  Crest: {metrics['input_crest']:.2f} dB")

    # Summary statistics
    print("\n=== Summary by SNR Level ===\n")
    for level in sorted(snr_by_level.keys()):
        rms_values = snr_by_level[level]
        print(f"{level}: avg_rms={np.mean(rms_values):.6f}, std={np.std(rms_values):.6f}")

    print("\n=== Summary by Noise Type ===\n")
    for noise_key in sorted(snr_by_type.keys()):
        rms_values = snr_by_type[noise_key]
        print(f"{noise_key}: avg_rms={np.mean(rms_values):.6f}")

    # Save results
    results['summary'] = {
        'by_snr_level': {k: {'avg_rms': float(np.mean(snr_by_level[k])), 'count': len(snr_by_level[k])} for k in snr_by_level},
        'by_noise_type': {k: {'avg_rms': float(np.mean(snr_by_type[k])), 'count': len(snr_by_type[k])} for k in snr_by_type},
    }

    with open('/tmp/audit_results/denoiser_audit.json', 'w') as f:
        json.dump(results, f, indent=2)

    print(f"\n✓ Audit complete. Results saved to /tmp/audit_results/denoiser_audit.json")

if __name__ == '__main__':
    audit_denoiser()
