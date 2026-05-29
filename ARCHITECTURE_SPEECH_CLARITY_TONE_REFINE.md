# Speech Clarity & Tone Refine Architecture

## Overview
Split Cleanup into two focused products, each with independent DSP detection and strength dials.

```
┌─────────────────────────────────────────┐
│ Speech Clarity (Artifact Removal)       │
├─────────────────────────────────────────┤
│ • Sibilance Control    [====] 🟠        │
│ • Plosive Control      [====] 🔴        │
│ • Breath Control       [====] 🟡        │
│                                         │
│ (LEDs show intensity: orange=subtle,    │
│  red=strong. Off=not detected)          │
└─────────────────────────────────────────┘

┌─────────────────────────────────────────┐
│ Tone Refine (Tonal Correction)          │
├─────────────────────────────────────────┤
│ • Mud Removal          [====] 🟠        │
│ • Harshness Removal    [====] 🔴        │
│ • Intelligent Smooth   [====] 🟡        │
└─────────────────────────────────────────┘
```

---

## Architecture: Adaptive Pre-Analysis

### Industry Standard Approach
1. **Pre-analysis pass:** Full lookahead scan of audio to establish signal characteristics
2. **Adaptive baselines:** Compute per-artifact thresholds based on actual signal
3. **Consistent processing:** Use fixed thresholds for entire buffer (not learning as it goes)
4. **LED feedback:** Intensity based on detection strength relative to threshold

### Data Flow
```
Audio Buffer
    ↓
[Pre-Analysis Phase] ← Lookahead scan
    ├─ Measure spectral baselines
    ├─ Establish noise floor
    ├─ Detect average levels per artifact type
    ↓
[Compute Adaptive Thresholds]
    ├─ sibilance_threshold = f(spectral_content, sample_rate)
    ├─ plosive_threshold = f(low_freq_energy, transient_density)
    ├─ breath_threshold = f(noise_floor, spectral_flatness)
    ↓
[Process with Fixed Thresholds] ← Consistent for entire track
    ├─ Detect artifacts per-block
    ├─ Compute LED intensity (0-1 based on detection strength / threshold)
    ├─ Apply user dial strength (0-1)
    ↓
Output
```

---

## Speech Clarity: Detection Algorithms

### 1. Sibilance Detection (DeEsser)
**Purpose:** Identify and measure /s/ and /z/ sounds

**Detection Method:**
```
For each block:
  1. Band-pass filter 4.5-8.0 kHz (sibilance band)
  2. Compute envelope: |filtered_signal| smoothed (fast envelope follower)
  3. Peak detection: local maxima in envelope
  4. Threshold: adaptive_sibilance_threshold
  5. Detection strength = (peak_envelope - threshold) / adaptive_range
  6. LED intensity = clamp01(detection_strength)
```

**Adaptive Threshold Calculation** (pre-analysis pass):
```
percentile_95_sibilance_energy = compute_percentile(
    sibilance_band_energy_history, 0.95)
adaptive_sibilance_threshold = percentile_95_sibilance_energy * 1.5
```

**Distinguish from False Positives:**
- Check harmonicity: true /s/ has noise-like character in the band
- Check ratio to adjacent bands: sibilance is sharp, not broad spectrum
- Ignore if signal has strong voicing in the sibilance band (voiced /z/)

**Dial Behavior:**
- User dials 0.0 (off) → no processing, LED off
- User dials 0.5+ and sibilance detected → LED lights orange/red
- Dial strength controls reduction amount in sibilance band

---

### 2. Plosive Detection (DePolosive)
**Purpose:** Identify /p, b, t, d, k, g/ bursts

**Detection Method:**
```
For each block:
  1. Low-pass filter 0-300 Hz (plosive energy band)
  2. Envelope follower (very fast attack ~5ms, slow release ~50ms)
  3. Onset detection: compute dEnv/dt (envelope slope)
  4. Burst character: rapid onset + energy concentration
  5. Threshold: adaptive_plosive_threshold
  6. Detection strength = burst_energy / adaptive_range
  7. LED intensity = clamp01(detection_strength)
```

**Adaptive Threshold Calculation** (pre-analysis pass):
```
// Measure average low-frequency transient activity
transient_activity = measure_onset_peaks_in_lowfreq()
percentile_90 = compute_percentile(transient_activity, 0.90)
adaptive_plosive_threshold = percentile_90 * 2.0
```

**Distinguish from False Positives:**
- Check decay pattern: plosives decay within 50-200ms; room rumble is sustained
- Check frequency distribution: plosive energy is concentrated in very low freq (50-150Hz)
- Avoid: Kick drums (too much energy), wind noise (different envelope shape)

**Dial Behavior:**
- User dials 0.0 (off) → no gate
- User dials 0.5+ and plosive burst detected → LED lights
- Dial strength controls gate threshold (deeper reduction when dial is high)

---

### 3. Breath Detection (DeBreath)
**Purpose:** Identify breathing sounds and wind noise

**Detection Method:**
```
For each block:
  1. Low-pass filter 0-500 Hz (breath fundamental range)
  2. Measure spectral flatness (FFT-based entropy)
  3. Check harmonicity: compare to harmonic content
  4. Envelope shape: gradual onset (not sharp like plosive)
  5. Threshold: adaptive_breath_threshold
  6. Detection strength = (spectral_flatness * non_harmonicity) / range
  7. LED intensity = clamp01(detection_strength)
```

**Adaptive Threshold Calculation** (pre-analysis pass):
```
// Measure noise-like character of signal
noise_floor_estimate = compute_spectral_flatness_baseline()
harmonic_content = compute_average_harmonicity()
adaptive_breath_threshold = 
    (noise_floor_estimate + 0.3) * (1.0 - harmonic_content * 0.5)
```

**Distinguish from False Positives:**
- Check temporal pattern: breath has gradual buildup, not sudden spikes
- Spectral shape: breath is noise-like (flat spectrum) vs. room tone (has modes)
- Harmonic check: actual voice (harmonic) vs. breath (inharmonic noise)

**Dial Behavior:**
- User dials 0.0 (off) → no reduction
- User dials 0.5+ and breath detected → LED lights
- Dial strength controls reduction amount (higher = more aggressive)

---

## Tone Refine: Detection Algorithms

### 1. Mud Detection (DeMud)
**Purpose:** Identify low-mid buildup (muddiness, boxiness)

**Detection Method:**
```
For each block:
  1. Band focus: 100-500 Hz (low-mid mud range)
  2. Measure energy ratio: low-mid_energy / total_energy
  3. Compare to voice baseline: expected ratio for speech
  4. Excess = (actual_ratio - baseline_ratio) / expected_range
  5. Threshold: adaptive_mud_threshold
  6. Detection strength = excess magnitude
  7. LED intensity = clamp01(detection_strength)
```

**Adaptive Threshold Calculation** (pre-analysis pass):
```
baseline_low_mid_ratio = compute_percentile(
    low_mid_energy_ratio_history, 0.50)  // median
adaptive_mud_threshold = baseline_low_mid_ratio * 1.3
```

---

### 2. Harshness Detection (DeHarsh)
**Purpose:** Identify presence peak/brittleness (2-5 kHz harshness)

**Detection Method:**
```
For each block:
  1. Band focus: 2.0-5.0 kHz (presence peak range)
  2. Measure sharpness: spectral peak prominence
  3. Compare to adjacent bands: is presence isolated or broad?
  4. Harshness score = peak_height * peak_narrowness
  5. Threshold: adaptive_harsh_threshold
  6. Detection strength = harshness_score
  7. LED intensity = clamp01(detection_strength)
```

---

### 3. Intelligent Smooth (DeSmooth)
**Purpose:** Detect and apply subtle smoothing to reduce artifacts

**Detection Method:**
```
For each block:
  1. Measure smoothness: spectral derivative (rate of change)
  2. High derivative = sharp/rough character
  3. Smooth character = gentle spectral slope
  4. Detection strength = 1.0 - smoothness_metric
  5. LED intensity = clamp01(detection_strength * 0.5)  // subtle
```

---

## Implementation: ProcessProduct Outline

### Speech Clarity Product

```cpp
void VXSpeechClarityAudioProcessor::processProduct(
    juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // 1. PRE-ANALYSIS PASS (first call or on settings change)
    if (needsPreAnalysis) {
        preAnalysisPass(buffer);
        // Computes:
        // - adaptiveSibilanceThreshold
        // - adaptivePlosiveThreshold
        // - adaptiveBreathThreshold
        needsPreAnalysis = false;
    }

    // 2. READ USER DIALS
    const float sibilanceStrength = readNormalized(
        parameters, kSibilanceStrengthParam, 0.0f);
    const float plosiveStrength = readNormalized(
        parameters, kPlosiveStrengthParam, 0.0f);
    const float breathStrength = readNormalized(
        parameters, kBreathStrengthParam, 0.0f);

    // 3. DETECT ARTIFACTS (per-block)
    const float sibilanceDetection = detectSibilance(
        buffer, adaptiveSibilanceThreshold);
    const float plosiveDetection = detectPlosive(
        buffer, adaptivePlosiveThreshold);
    const float breathDetection = detectBreath(
        buffer, adaptiveBreathThreshold);

    // 4. UPDATE LED FEEDBACK
    ledSibilance = clamp01(sibilanceDetection);
    ledPlosive = clamp01(plosiveDetection);
    ledBreath = clamp01(breathDetection);

    // 5. PROCESS EACH DSP (only if dial > 0)
    if (sibilanceStrength > 0.001f) {
        deEsserDsp.setSibilanceStrength(
            sibilanceStrength * sibilanceDetection);
        deEsserDsp.process(buffer);
    }

    if (plosiveStrength > 0.001f) {
        dePolosiveDsp.setPlosiveStrength(
            plosiveStrength * plosiveDetection);
        dePolosiveDsp.process(buffer);
    }

    if (breathStrength > 0.001f) {
        deBreathDsp.setBreathStrength(
            breathStrength * breathDetection);
        deBreathDsp.process(buffer);
    }

    outputTrimmer.process(buffer, currentSampleRateHz);
}

// PRE-ANALYSIS: Lookahead pass (called once per session or on reset)
void preAnalysisPass(const juce::AudioBuffer<float>& buffer)
{
    // Scan entire buffer to establish adaptive thresholds
    float maxSibilanceEnergy = 0.0f;
    float maxPlosiveEnergy = 0.0f;
    float maxBreathEnergy = 0.0f;

    for (int block = 0; block < totalBlocks; ++block) {
        maxSibilanceEnergy = std::max(
            maxSibilanceEnergy,
            measureSibilanceBandEnergy(buffer, block));
        maxPlosiveEnergy = std::max(
            maxPlosiveEnergy,
            measureLowFreqOnsetEnergy(buffer, block));
        maxBreathEnergy = std::max(
            maxBreathEnergy,
            measureBreathCharacter(buffer, block));
    }

    // Set thresholds at percentile of measured activity
    adaptiveSibilanceThreshold = maxSibilanceEnergy * 0.8f;
    adaptivePlosiveThreshold = maxPlosiveEnergy * 0.8f;
    adaptiveBreathThreshold = maxBreathEnergy * 0.8f;
}

// DETECTION: Per-block sibilance
float detectSibilance(
    const juce::AudioBuffer<float>& buffer,
    float threshold)
{
    float sibilanceBandEnergy = measureSibilanceBandEnergy(buffer, currentBlock);
    float harmonicity = measureHarmonicity(buffer, currentBlock);

    // Only count if noise-like (low harmonicity) in sibilance band
    if (harmonicity > 0.6f) return 0.0f;

    return clamp01((sibilanceBandEnergy - threshold) / 
                   (adaptiveSibilanceThreshold * 0.5f));
}

// DETECTION: Per-block plosive
float detectPlosive(
    const juce::AudioBuffer<float>& buffer,
    float threshold)
{
    float burstEnergy = measureLowFreqOnsetEnergy(buffer, currentBlock);
    float decayRate = measureBurstDecay(buffer, currentBlock);

    // Only count if fast decay (characteristic of plosive)
    if (decayRate < 0.3f) return 0.0f;

    return clamp01((burstEnergy - threshold) / 
                   (adaptivePlosiveThreshold * 0.5f));
}

// DETECTION: Per-block breath
float detectBreath(
    const juce::AudioBuffer<float>& buffer,
    float threshold)
{
    float spectralFlatness = measureSpectralFlatness(buffer, currentBlock);
    float harmonicity = measureHarmonicity(buffer, currentBlock);
    float breathScore = spectralFlatness * (1.0f - harmonicity);

    return clamp01((breathScore - threshold) / 
                   (adaptiveBreathThreshold * 0.5f));
}
```

---

## Key DSP Primitives Needed

These reuse/adapt from existing framework:

| Function | Purpose | Reuse From |
|----------|---------|-----------|
| `envelopeFollower()` | Fast attack/release on band energy | VxStudioFinishDsp |
| `bandPassFilter()` | Isolate frequency ranges | VxClarityDsp |
| `onsetDetector()` | Measure envelope rise time | VxRebalanceDsp.detectTransientEvents |
| `harmonicityMeasure()` | Compare harmonic vs. noise content | VxStudioAnalysisEvidence |
| `spectralFlatness()` | Compute spectral entropy | Existing in cleanup |
| `measurePercentile()` | Statistical analysis for baselines | New utility |

---

## Control Behavior Summary

### Speech Clarity
| Dial | Detection OFF | Detection ON |
|------|---|---|
| 0.0 | No processing | No processing (dial off) |
| 0.5 | No processing | LED shows intensity, moderate reduction |
| 1.0 | No processing | LED shows intensity, full reduction |

**LED Color:**
- Off (gray): Artifact not detected, or dial is 0
- Orange (dim): Artifact present, subtle intensity (< 0.5)
- Red (bright): Artifact present, strong intensity (≥ 0.5)

### Tone Refine
Same control behavior, three independent dials.

---

## Reset & Recalibration

**When to recalculate adaptive thresholds:**
- Plugin loaded
- Sample rate changes
- User clicks "Recalibrate" button (optional)
- Track switches
- After 60+ seconds of silence (reset to prevent learning drift)

**Not during playback** (ensures consistency across entire track).

---

## Priority Tuning Order

1. **Plosive detection first** — most binary (clear on/off), easiest to validate
2. **Sibilance detection** — most common issue, easy to A/B test
3. **Breath detection** — can be subtle, easiest to tweak once others work
4. **Tone Refine** — higher-level, refinement only

