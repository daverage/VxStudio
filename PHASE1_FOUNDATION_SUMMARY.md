# Phase 1: Foundation Summary

## What's Been Built

### 1. Architecture Document ✓
- **File:** `ARCHITECTURE_SPEECH_CLARITY_TONE_REFINE.md`
- Full design for both products with industry-standard adaptive detection
- Pre-analysis pass establishes consistent thresholds
- Intensity-based LED feedback system
- Independent DSP per artifact with single strength dial each

### 2. Detection Utilities ✓
- **File:** `Source/vxstudio/framework/VxStudioArtifactDetectors.h`
- Core DSP primitives:
  - `OnePoleLowpass` — envelope following
  - `EnvelopeFollower` — fast attack / slow release
  - `BiquadFilter` — band-pass filtering
  - `SpectralAnalyzer` — flatness, harmonicity, band energy
  - `OnsetDetector` — plosive burst detection
  - Statistical helpers (percentile, median, RMS, peak)

### 3. Speech Clarity Product Skeleton ✓
- **Files:**
  - `VxSpeechClarityProcessor_CLEAN.h`
  - `VxSpeechClarityProcessor_CLEAN.cpp`
- Core structure:
  - Pre-analysis phase for adaptive threshold setup
  - Per-block detection (sibilance, plosive, breath)
  - LED intensity feedback (0-1 range smoothed)
  - Placeholder for DSP processing
- **Ready for:** DSP implementation + framework integration

---

## Detection Algorithm (Implemented)

### Sibilance Detection
```
Pre-analysis: Measure max high-frequency energy → set adaptive threshold
Per-block:
  1. Envelope follow signal (~5kHz region)
  2. Measure peak envelope
  3. If peak > threshold: intensity = (peak - threshold) / (threshold * 0.5)
  4. Smooth LED intensity (90% old + 10% new)
```

### Plosive Detection
```
Pre-analysis: Measure max burst energy → set adaptive threshold
Per-block:
  1. Envelope follow signal (~100Hz region, fast attack)
  2. Measure peak envelope
  3. If peak > threshold: intensity = (peak - threshold) / (threshold * 0.5)
  4. Smooth LED intensity
```

### Breath Detection
```
Pre-analysis: Measure max low-frequency energy → set adaptive threshold
Per-block:
  1. Envelope follow signal (sub-500Hz, very slow attack)
  2. Measure peak envelope
  3. If peak > threshold: intensity = (peak - threshold) / (threshold * 0.5)
  4. Smooth LED intensity
```

**Key behavior:** Detection is **consistent across entire track** (pre-analysis pass happens once), but **responsive to block-level changes** (detection updates per-block).

---

## What Remains: Phase 2-4

### Phase 2: Speech Clarity DSP (Next)

Need to implement three corrective DSP engines:

#### 2a. DeEsser (Sibilance Reduction)
- **Input:** buffer, sibilanceStrength (0-1), sibilanceDetectionIntensity (0-1)
- **Output:** de-essed audio
- **Algorithm:**
  - Band-pass filter 4.5-8 kHz
  - Measure sibilance envelope
  - Apply gentle gain reduction proportional to strength + detection intensity
  - Preserve phase/color (not aggressive EQ)

#### 2b. DePolosive (Plosive Gate)
- **Input:** buffer, plosiveStrength (0-1), plosiveDetectionIntensity (0-1)
- **Output:** plosive-reduced audio
- **Algorithm:**
  - Detect plosive bursts (fast onset in low freq)
  - Apply soft gate/compression during burst
  - Strength dial controls how hard the gate
  - Detection intensity gates the gate itself (only acts if detected)

#### 2c. DeBreath (Breath Reduction)
- **Input:** buffer, breathStrength (0-1), breathDetectionIntensity (0-1)
- **Output:** breath-reduced audio
- **Algorithm:**
  - Detect noise-like low-frequency content
  - Selective subtraction (not full gating)
  - Preserves voice fundamental
  - Strength controls intensity of subtraction

---

### Phase 3: Tone Refine Product

Similar structure but three different DSP:

#### 3a. DeMud (Low-Mid Reduction)
- Detect 100-500 Hz buildup
- Selective EQ reduction
- Independent of Speech Clarity (runs after)

#### 3b. Harshness (Presence Peak Reduction)
- Detect sharp peaks 2-5 kHz
- Gentle tilt/notch filtering
- Preserve speech intelligibility

#### 3c. IntelligentSmooth (Tonal Smoothing)
- Measure spectral derivative (roughness)
- Apply transparent smoothing
- Most subtle of the three

---

### Phase 4: LED Feedback & Validation

- **Wire LED colors:** dim orange (subtle) → bright red (strong)
- **Test with real problematic audio:**
  - Heavy sibilance
  - Mic pops / plosives
  - Breathing noises
  - Muddy low-mid
  - Harsh presence peaks
- **Validate detection accuracy** before shipping

---

## Implementation Notes

### Detection Quality
Current implementation uses simple envelope following + thresholds. This is **good enough for MVP** but can be enhanced later:
- Add spectral masking (detect sibilance around formants, not just high energy)
- Add attack envelope detection (distinguish plosives from sustained noise)
- Add harmonicity checks (ignore sibilance on harmonic /z/ in deep voices)

### Performance
- Pre-analysis pass: single-threaded scan (fast, happens once per session)
- Per-block detection: minimal CPU (just envelope following + comparison)
- DSP processing: placeholder (will be efficient band-limited operations)

### Design Constraints Maintained
- **Independent controls:** Each dial works independently (no macro coupling)
- **Consistent detection:** Pre-analysis ensures first 100ms sounds same as second 100m
- **"Set and forget":** User dials what they need, LEDs show what's present
- **Uncluttered UI:** 3 dials per product, that's it

---

## Files & Structure

```
Source/vxstudio/
  framework/
    VxStudioArtifactDetectors.h       [Detection utilities - READY]
  products/
    speech_clarity/
      VxSpeechClarityProcessor_CLEAN.h      [Product skeleton - READY]
      VxSpeechClarityProcessor_CLEAN.cpp    [Detection + LED - READY]
      dsp/
        VxDeEsserDsp.h                      [TODO: implement]
        VxDePlosiveDsp.h                    [TODO: implement]
        VxDeBreathDsp.h                     [TODO: implement]
    tone_refine/
      VxToneRefineProcessor.h               [TODO: skeleton]
      VxToneRefineProcessor.cpp             [TODO: detection]
      dsp/
        VxDeMudDsp.h                        [TODO: implement]
        VxDeHarshnesseDsp.h                 [TODO: implement]
        VxDeSmooothDsp.h                    [TODO: implement]

Docs/
  ARCHITECTURE_SPEECH_CLARITY_TONE_REFINE.md    [Full spec - REFERENCE]
  PHASE1_FOUNDATION_SUMMARY.md                   [This file]
```

---

## Next Steps

1. **Create DSP components** (DeEsser, DePolosive, DeBreath)
2. **Wire DSP into Speech Clarity processor** (replace TODO placeholders)
3. **Test detection accuracy** with real problematic audio
4. **Repeat for Tone Refine**
5. **Add LED color feedback** (intensity visualization)

**Est. effort:**
- DeEsser: 4-6 hours (band-selective processing)
- DePolosive: 3-4 hours (envelope + gating)
- DeBreath: 3-4 hours (noise detection + selective subtraction)
- Tone Refine (3 DSP): 6-8 hours
- Testing & validation: 4-6 hours

**Total: ~25-30 hours for complete, tested implementation**

---

## Quality Checklist

- [x] Architecture designed
- [x] Detection logic implemented
- [x] LED feedback system designed
- [x] Pre-analysis / adaptive thresholds working
- [ ] DeEsser DSP implemented
- [ ] DePolosive DSP implemented
- [ ] DeBreath DSP implemented
- [ ] Speech Clarity processor complete
- [ ] Tone Refine processor complete
- [ ] Real audio testing
- [ ] Detection accuracy validation
- [ ] LED color feedback visualization
- [ ] Documentation complete
