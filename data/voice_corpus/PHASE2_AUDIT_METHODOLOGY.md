# Phase 2: Algorithm Quality Audits — Test Methodology

## Overview
Phase 2 focuses on perceptual and algorithmic validation of five core audio processors through A/B listening tests and measurements. No code changes—pure research.

---

## 1. Deverb WPE Algorithm Validation

### Test Goal
Verify that WPE (Weighted Prediction Error) dereverberation performs well on real room impulse responses and that the current RT60 estimation is accurate.

### Test Set
**Reverberant Speech Samples:**
- Source: WHAMR (Google DeepMind reverberant speech corpus) or synthetic reverb simulation
- Content: 10 utterances at RT60 values: 0.3s, 0.6s, 1.0s (small, medium, large rooms)
- Duration: ~2 min total
- Format: 48 kHz mono

### Methodology
```bash
# 1. Process each reverberant sample through Deverb
./build/VXDeverbMeasure <input_reverberant.wav> <output_deverb.wav> voice 1.0 1.0

# 2. A/B listen:
#   - A: Original reverberant
#   - B: Deverb processed
#   - Assess: clarity, attack transient definition, any artifacts

# 3. Measure RT60 estimate accuracy
#   Use getTestTrackedRt60Seconds() to read estimated vs actual
```

### Success Criteria
- Speech clarity improves (no degradation in articulation)
- Reverb tail is reduced by ≥50% perceptually
- RT60 estimates within ±0.2s of actual
- No artifacts (pumping, phase distortion, metallic tones)

### Known Limitation
Deverb is optimized for close-mic sources. Performance may degrade on far-field recordings with extreme reverberation (RT60 > 2.0s).

---

## 2. Denoiser OM-LSA vs MMSE-LSA Comparison

### Test Goal
Validate that OM-LSA (Optimal Modified Log-Spectral Amplitude) suppression preserves speech quality better than MMSE-LSA (Minimum Mean-Square Error) alternatives at various SNR levels.

### Test Set
**Noisy Speech Samples:**
- Source: DNS Challenge test set (or synthetic noise added to your voice corpus)
- Content: Speech at 5 SNR levels: -5dB, 0dB, 5dB, 10dB, 15dB
- Noise types: white, pink, cafe, street (if available; otherwise synthesized)
- Format: 48 kHz mono

### Methodology
```bash
# 1. Generate or download test set with known SNR
python3 ./data/voice_corpus/dns_challenge/generate_noisy_speech.py

# 2. Process each noisy sample through Denoiser
./build/VXDenoiserMeasure <input_noisy.wav> <output_denoised.wav> voice 0.5 1.0

# 3. A/B listen per SNR level:
#   - A: Original noisy
#   - B: Denoiser processed
#   - Assess: noise reduction, articulation preservation, artifacts (distortion, ringing)

# 4. Measure SNR improvement
#   Calculate output SNR: SNR_out = 20*log10(speech_rms / noise_rms)
```

### Success Criteria
- SNR improves by ≥6dB at all input levels
- Speech is intelligible and clear (no lisping, plosive suppression)
- No spectral ringing or musical artifacts
- At low SNR (-5dB), some audible noise remains (acceptable tradeoff)

### Key Question to Answer
Does the current OM-LSA implementation preserve consonant articulation better than the alternative MMSE-LSA approach? (Answer: Yes, by design—OM-LSA is more conservative.)

---

## 3. DeepFilterNet DFN3 vs DFN2 Quality Regression

### Test Goal
Confirm that upgrading from DFN2 to DFN3 (if not already done) or validate current model performance across diverse speech types.

### Test Set
**Diverse Speech Samples:**
- Source: LibriSpeech test-clean subset (50 random utterances)
- Content: American English read speech, varied speakers, background conditions
- Format: 16 kHz → resample to 48 kHz for testing
- Metadata: Speaker ID, phonetic content

### Methodology
```bash
# 1. Process each sample through DeepFilterNet
./build/VXDeepFilterNetMeasure <input.wav> <output.wav> voice 1.0 1.0

# 2. A/B listen on subset (10 samples):
#   - A: Original (with any background noise)
#   - B: DeepFilterNet processed
#   - Assess: speech presence, artifacts, any "muffled" character

# 3. Run framework analysis snapshot to check speech safety gating
#   (Added in Phase 1 Sprint 1—verify speechSafetyFactor is working)

# 4. Measure inference latency
#   Time per frame: (output_latency_samples / sample_rate) * 1000 ms
```

### Success Criteria
- No model version mismatch detected (frame length validation passes)
- Speech clarity improves or stays equivalent to input
- No excessive muffling or phase artifacts
- Inference time < 5ms per processed frame (real-time capable)

### Known Limitation
DFN3 is more aggressive than DFN2. At low SNR, some speech detail may be lost. ReadabilityGuard post-pass (added in Phase 1) mitigates this by blending wet/dry.

---

## 4. Leveler Offline Mode Accuracy

### Test Goal
Verify that offline loudness analysis produces accurate target levels and that the online gain riding matches the offline plan.

### Test Set
**Dynamic Speech:**
- Source: Existing voice corpus (varied source levels across files)
- Content: Mix of quiet and loud passages within single file
- Format: 48 kHz mono

### Methodology
```bash
# 1. Run offline analysis
./build/VXLevelerMeasure <input.wav> <offline_plan.txt> voice 0.0 0.0

# 2. Capture offline target loudness from analysis output
#    (Expected: target level around -20dB LUFS for voice)

# 3. Apply online gain riding
./build/VXLevelerMeasure <input.wav> <output.wav> voice 0.15 1.0

# 4. Measure loudness of output
#    (Expected: output within ±2 LUFS of offline plan)

# 5. Compare manual vs offline gain riding
#    - Apply manual gain ride on same file
#    - Assess if Leveler matches manual intent
```

### Success Criteria
- Offline analysis detects quiet/loud passages correctly
- Online gain riding produces target level within ±2 LUFS
- No audible pumping or level stuttering
- Playhead repositioning (Phase 1 Leveler C) not critical—defer if API mismatch

### Known Limitation
Offline analysis assumes static content. If sample rate changes mid-session, analysis must be cleared (fixed in Phase 1).

---

## 5. OptoComp/Finish LA-2A Time Constants Verification

### Test Goal
Confirm that VxStudioOptoCompressorLA2A implements LA-2A envelope times matching the original hardware spec.

### Specifications (Hardware Reference)
| Parameter | Value | Source |
|-----------|-------|--------|
| Attack time | ~10 ms | Program-dependent, optical cell |
| Release (50%) | ~60 ms | Fast release phase |
| Release (full) | 0.5–5 s | Slow release phase (optical memory) |
| Compression ratio | ~3:1 | Fixed (electroluminescent) |

### Methodology
```bash
# 1. Inspect implementation
#    File: Source/vxstudio/framework/VxStudioOptoCompressorLA2A.cpp
#    Verify envelope time constants match spec

# 2. Create synthetic test signal
#    - Short transient (impulse) at t=0
#    - Measure attack time from uncompressed level to -3dB gain reduction

# 3. Process through OptoComp/Finish
./build/VXOptoCompMeasure <impulse.wav> <output.wav> 1.0 1.0

# 4. Analyze gain reduction envelope:
#    - Attack slope should reach -3dB in ~10ms
#    - Release should show two-stage behavior: fast (60ms) + slow (2-5s)

# 5. A/B listen against hardware reference (if available from UAD or Waves)
```

### Success Criteria
- Attack time measures ~10ms ± 2ms
- Release shows two-stage envelope (60ms + tail)
- Compression ratio holds at ~3:1
- No zipper artifacts or envelope instability

### Assessment (Already Validated in Phase 1)
✓ Time constants in code match LA-2A spec
✓ Dual-stage release implemented correctly
✓ No critical issues identified

**Conclusion:** OptoComp is production-ready. No code changes needed for time constant verification.

---

## Execution Order

1. **Generate test sets** (parallel):
   ```bash
   python3 ./data/voice_corpus/download_phase2_datasets.py
   python3 ./data/voice_corpus/dns_challenge/generate_noisy_speech.py
   python3 ./data/voice_corpus/whamr_subset/generate_synthetic_reverb.py
   ```

2. **Run audits in priority order:**
   - Deverb WPE (high complexity, subjective)
   - Denoiser OM-LSA (well-established, reference available)
   - DeepFilterNet (model validation, quick A/B)
   - Leveler offline (functional validation, internal consistency)
   - OptoComp (spec validation, already confirmed)

3. **Document findings** in per-product audit reports

4. **Phase 3:** Update research docs with methodology and results

---

## Equipment / Software Required

- **DAW or audio editor:** For A/B listening (Reaper, Logic, Audacity)
- **Measurement tools:** (Built-in via VXxxMeasure binaries)
- **Reference audio:** Hardware LA-2A recordings (optional; spec document sufficient)
- **Time commitment:** ~4–6 hours for full audit + listening

---

## Reference Documents

- [Phase 1 Completion Summary](/Users/andrzejmarczewski/.claude/plans/phase-1-completion-summary.md)
- [VX Suite Framework](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/docs/VX_SUITE_FRAMEWORK.md)
- [VX Suite Research](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/docs/VX_SUITE_RESEARCH.md)

---

## Notes

- All audits are **listening tests** + measurement validations, not code changes
- Findings should be recorded in per-product audit reports (e.g., `deverb-audit-findings.md`)
- No changes to production code unless critical issues discovered
- Focus on **perceptual quality** and **algorithm correctness**, not micro-optimizations
