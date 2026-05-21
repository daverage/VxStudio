# Phase 2 Dataset Setup — Summary & Status

**Date:** 2026-05-21  
**Status:** In Progress (LibriSpeech downloading)

---

## What We've Prepared

### 1. ✓ Test Infrastructure
- **Synthetic Reverb Generator:** `whamr_subset/generate_synthetic_reverb.py`
  - Creates reverberant versions of existing voice corpus (RT60: 0.3s, 0.6s, 1.0s)
  - Uses `pyroomacoustics` for realistic room simulation
  - Ready to run: `python3 whamr_subset/generate_synthetic_reverb.py`

- **Noisy Speech Generator:** `dns_challenge/generate_noisy_speech.py`
  - Adds synthetic noise to voice samples at 5 SNR levels (-5 to +15 dB)
  - Includes white and pink noise variants
  - Ready to run: `python3 dns_challenge/generate_noisy_speech.py`

### 2. → In Progress (Background)
- **LibriSpeech Test-Clean Dataset**
  - Downloading from Hugging Face (50–100 utterances recommended)
  - Size: ~500MB–1GB
  - ETA: Running in background (check `/private/tmp/.../bs0u5ham3.output`)

### 3. Test Methodology
- **Comprehensive guide:** `PHASE2_AUDIT_METHODOLOGY.md`
  - Per-product test protocol (Deverb, Denoiser, DeepFilterNet, Leveler, OptoComp)
  - Success criteria and known limitations for each
  - Equipment/software requirements
  - Listening test methodology

- **Quick audit script:** `quick_test_audit.sh`
  - Runs all audit tests in sequence
  - Shows dataset availability
  - Provides command-line examples

---

## Directory Structure

```
/Users/andrzejmarczewski/Documents/GitHub/VxStudio/data/voice_corpus/
├── raw/                              # Original audio files
│   ├── churchill_be_ye_men_of_valour.ogg
│   ├── edward_viii_abdication.ogg
│   ├── old_letters_librivox.mp3
│   └── princess_elizabeth_21st_birthday.oga
│
├── wav/                              # Converted to 48kHz mono PCM
│   ├── churchill_be_ye_men_of_valour.wav
│   ├── edward_viii_abdication.wav
│   ├── old_letters_librivox.wav
│   └── princess_elizabeth_21st_birthday.wav
│
├── whamr_subset/                     # Deverb testing (reverberant speech)
│   ├── generate_synthetic_reverb.py  # ← Run this
│   ├── churchill_be_ye_men_of_valour_rt60_0.3.wav
│   ├── churchill_be_ye_men_of_valour_rt60_0.6.wav
│   └── churchill_be_ye_men_of_valour_rt60_1.0.wav
│
├── dns_challenge/                    # Denoiser testing (noisy speech)
│   ├── generate_noisy_speech.py      # ← Run this
│   ├── churchill_be_ye_men_of_valour_white_-5dB.wav
│   ├── churchill_be_ye_men_of_valour_white_0dB.wav
│   ├── churchill_be_ye_men_of_valour_pink_+10dB.wav
│   └── ...
│
├── librispeech_test/                 # DeepFilterNet testing (diverse samples)
│   └── [downloading...]              # 500MB–1GB of speech samples
│
├── PHASE2_AUDIT_METHODOLOGY.md       # Comprehensive test guide
├── PHASE2_SETUP_SUMMARY.md           # This file
├── quick_test_audit.sh               # Run all audits
├── download_phase2_datasets.py       # Downloader (running)
└── README.md                         # Original corpus documentation
```

---

## Quick Start

### Step 1: Wait for LibriSpeech Download
Monitor background task:
```bash
tail -20 /private/tmp/claude-501/-Users-andrzejmarczewski-Documents-GitHub-VxStudio/ff55417d-06ad-488f-84c8-ad75e1a8cdd1/tasks/bs0u5ham3.output
```

### Step 2: Generate Synthetic Test Data
Once LibriSpeech download completes, run:
```bash
# Generate reverberant versions of existing audio (for Deverb)
python3 /Users/andrzejmarczewski/Documents/GitHub/VxStudio/data/voice_corpus/whamr_subset/generate_synthetic_reverb.py

# Generate noisy versions at various SNRs (for Denoiser)
python3 /Users/andrzejmarczewski/Documents/GitHub/VxStudio/data/voice_corpus/dns_challenge/generate_noisy_speech.py
```

### Step 3: Run Audits
```bash
# Quick overview of all audits
bash /Users/andrzejmarczewski/Documents/GitHub/VxStudio/data/voice_corpus/quick_test_audit.sh

# Or run individual audits manually
./build/VXDeverbMeasure <input.wav> <output.wav> voice 0.5 1.0
./build/VXDenoiserMeasure <noisy.wav> <denoised.wav> voice 0.5 1.0
./build/VXDeepFilterNetMeasure <input.wav> <output.wav> voice 1.0 1.0
./build/VXLevelerMeasure <input.wav> <output.wav> voice 0.15 1.0
```

### Step 4: Document Findings
Create audit reports:
```
/Users/andrzejmarczewski/.claude/plans/
├── deverb-audit-findings.md
├── denoiser-audit-findings.md
├── deepfilternet-audit-findings.md
├── leveler-audit-findings.md
└── optocomp-audit-findings.md
```

---

## System Requirements

### Required Libraries
For synthetic data generation:
```bash
pip install pyroomacoustics soundfile numpy datasets
```

### Optional (for advanced testing)
```bash
pip install librosa scipy scikit-learn  # For audio analysis
pip install matplotlib                   # For spectrograms
```

### Build Requirements
All audit scripts assume the VxStudio build is complete:
```bash
cd /Users/andrzejmarczewski/Documents/GitHub/VxStudio
cmake --build build -j$(nproc)
```

---

## What Each Dataset Is For

| Dataset | Purpose | Size | Use Case |
|---------|---------|------|----------|
| **whamr_subset/** | Deverb validation | ~50MB | Test WPE dereverberation across different RT60 values |
| **dns_challenge/** | Denoiser validation | ~100MB | Test OM-LSA across SNR levels (-5 to +15 dB) |
| **librispeech_test/** | DeepFilterNet validation | ~500MB–1GB | Test DFN3 across diverse speech samples |
| **Existing wav/** | All audits | ~65MB | Baseline tests (historical speeches) |

---

## Timeline

- **Now:** LibriSpeech downloading (ETA: 15–45 min depending on connection)
- **After download:** Generate synthetic data (5 min)
- **During/After:** Run quick audit tests (30 min)
- **Total Phase 2:** ~2–3 hours listening tests + analysis

---

## Known Issues / Workarounds

### LibriSpeech Download Slow
- Uses Hugging Face datasets library (requires registration)
- Alternative: Download OpenSLR tarball directly
  ```bash
  cd /Users/andrzejmarczewski/Documents/GitHub/VxStudio/data/voice_corpus/librispeech_test
  wget https://www.openslr.org/resources/12/test-clean.tar.gz
  tar xzf test-clean.tar.gz
  ```

### Synthetic Data Generation Requires Packages
- Install with: `pip install pyroomacoustics soundfile`
- If not available, use existing corpus as-is (smaller test set)

### WHAMR Full Setup Complex
- We're using synthetic reverb instead of full WHAMR dataset
- More lightweight and works with existing corpus
- See `generate_synthetic_reverb.py` for details

---

## Estimated Disk Usage

| Component | Size | Location |
|-----------|------|----------|
| Existing corpus | 65 MB | `raw/` + `wav/` |
| Synthetic reverb | 50 MB | `whamr_subset/` |
| Synthetic noisy | 100 MB | `dns_challenge/` |
| LibriSpeech | 500–1000 MB | `librispeech_test/` |
| **Total** | **~700 MB–1.2 GB** | |

---

## Next: Phase 2 Execution

Once data is ready, follow the audit methodology:
1. **Deverb:** A/B listen on reverberant samples, validate RT60 estimates
2. **Denoiser:** A/B listen across SNR levels, check for artifacts
3. **DeepFilterNet:** Test on diverse speech, verify safety gating
4. **Leveler:** Offline analysis accuracy, level consistency
5. **OptoComp:** Spec validation (already done in Phase 1)

Document findings in individual audit reports.

---

## Contact / Support

For issues with data setup:
1. Check `download_phase2_datasets.py` output
2. Verify pip dependencies: `pip list | grep -E "datasets|pyroomacoustics|soundfile"`
3. Ensure build is recent: `git log -1 --oneline`
4. See `PHASE2_AUDIT_METHODOLOGY.md` for detailed test protocols
