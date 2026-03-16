# Dynamic troubleSmooth — Technical Specification

**Component:** `vxsuite::polish::Dsp`
**Files affected:** `VxPolishDsp.h`, `VxPolishDsp.cpp`
**Status:** Proposed replacement for static multi-band notch implementation

---

## 1. Motivation

The current `troubleSmooth` stage applies six fixed-depth peaking EQ cuts at 1400, 2400, 3600, 5200, 7600, and 10500 Hz whenever `troubleAmt > 0`. The cut depth is static — modulated only by `troubleAmt` and a scalar `smoothCtx` computed once per block from `artifactRisk` and `speechPresence`. This means:

- A voice with no excess upper-mid energy is still notched permanently.
- The stage cannot distinguish a harsh transient from balanced spectral content.
- It is inconsistent with the dynamic, content-aware behaviour of every other stage in the chain (deMud, deEss, plosive, compressor).

The replacement makes each band's cut depth proportional to how much that band's energy exceeds a reference level at that moment, following the same detect-and-reduce pattern used by deMud.

---

## 2. Design Overview

Each of the six bands gets an independent envelope detector. A shared wideband reference envelope is computed from a low-pass filtered version of the signal. Per-sample, each band's ratio of detected energy to reference energy is compared against a per-band threshold. The excess above threshold (in dB) drives the cut depth of that band's peaking EQ filter. When band energy is at or below the threshold, the cut is zero and the filter passes through transparently.

The output filter topology is unchanged — six `processBiquadDf2` calls in series. Only the `gainDb` argument passed to `makePeakingEq` changes from a static value to a per-sample dynamic value.

---

## 3. Signal Flow

```
input signal
    │
    ├──► one-pole LP @ 800 Hz ──► RMS envelope ──► referenceEnv
    │
    ├──► bandpass[0] @ 1400 Hz ──► RMS envelope[0] ──┐
    ├──► bandpass[1] @ 2400 Hz ──► RMS envelope[1] ──┤
    ├──► bandpass[2] @ 3600 Hz ──► RMS envelope[2] ──┤──► per-band ratio vs reference
    ├──► bandpass[3] @ 5200 Hz ──► RMS envelope[3] ──┤──► excess above threshold
    ├──► bandpass[4] @ 7600 Hz ──► RMS envelope[4] ──┤──► dynamic cut depth (dB)
    └──► bandpass[5] @ 10500 Hz ─► RMS envelope[5] ──┘
                                                           │
                                                           ▼
    input ──► peakEQ[0] ──► peakEQ[1] ──► ... ──► peakEQ[5] ──► output
              (dynamic gain computed per-sample from envelope ratios)
```

The detection bandpass filters operate on a mono mix. The output peaking EQ filters operate per-channel, as they do today.

---

## 4. Detection: Reference Envelope

A single wideband reference envelope tracks overall signal level, providing context for what is "normal" energy in the upper-mid range.

**One-pole LP cutoff:** 800 Hz (computed in `prepare()`, cached as `cTroubleRefA`)

```
troubleRefLp = cTroubleRefA * troubleRefLp + (1 - cTroubleRefA) * monoLinear
```

**RMS envelope** (shared time constant with deMud, ~8ms):

```
troubleRefRms = cRmsA * troubleRefRms + (1 - cRmsA) * (troubleRefLp * troubleRefLp)
referenceEnv  = sqrt(troubleRefRms + 1e-12)
```

`cRmsA` is already computed in `prepare()` (`std::exp(-1 / (0.008 * sr))`). Reuse it.

---

## 5. Detection: Per-Band Bandpass Envelopes

Six bandpass filters, one per band, run on `monoLinear`. Each uses `makeBandpass()` with the same centre frequencies and Q values as the current static implementation.

**Band parameters (unchanged from current):**

| Band | Centre (Hz) | Q    |
|------|-------------|------|
| 0    | 1400        | 1.10 |
| 1    | 2400        | 1.20 |
| 2    | 3600        | 1.20 |
| 3    | 5200        | 1.15 |
| 4    | 7600        | 1.05 |
| 5    | 10500       | 0.90 |

**Detection state per band** (mono scalar, not per-channel):

```
troubleBandZ1[b]   // biquad state z1 for detection bandpass
troubleBandZ2[b]   // biquad state z2 for detection bandpass
troubleBandRms[b]  // running RMS envelope
```

The detection biquad coefficients are computed once in `prepare()` and stored as `troubleDetBpf[6]` (array of `BiquadCoeffs`).

**Per-sample detection:**

```
bandSample = processBiquadDf2(monoLinear, troubleDetBpf[b],
                               troubleBandZ1[b], troubleBandZ2[b])
troubleBandRms[b] = cRmsA * troubleBandRms[b] + (1 - cRmsA) * (bandSample * bandSample)
bandEnv[b] = sqrt(troubleBandRms[b] + 1e-12)
```

---

## 6. Per-Band Gain Computation

**Ratio and threshold:**

```
ratio[b]   = bandEnv[b] / referenceEnv          // linear ratio
ratioDb[b] = 20 * log10(ratio[b] + 1e-12)
excessDb[b] = max(0, ratioDb[b] - threshold[b])
```

**Per-band thresholds** (dB above reference). Lower bands of the presence range are naturally louder in voiced speech, so they require a higher threshold before cutting:

| Band | Centre (Hz) | Threshold (dB) | Rationale |
|------|-------------|----------------|-----------|
| 0    | 1400        | 2.0            | Upper-mid body; legitimate speech energy |
| 1    | 2400        | 1.5            | Boxiness zone; moderate tolerance |
| 2    | 3600        | 0.5            | Presence peak; tighter tolerance |
| 3    | 5200        | 0.0            | Harshness zone; act immediately |
| 4    | 7600        | 0.0            | Brilliance/sibilance; act immediately |
| 5    | 10500       | 1.0            | Air; some excess is acceptable |

These are initial tuning values and should be validated against real material.

**Dynamic range** (how many dB of excess maps to full cut):

```
range[b] = 6.0 dB  (all bands, uniform)
```

**Normalised drive:**

```
drive[b] = clamp(excessDb[b] / range, 0, 1)
```

**Context modifiers** (same as current static implementation, moved here):

```
smoothCtx = clamp(1.0 + 0.30 * artifactRisk - 0.03 * speechPresence, 0.90, 1.35)
```

**Final cut depth:**

```
cutDb[b] = -maxCutDb[b] * troubleAmt * drive[b] * smoothCtx
```

`maxCutDb` values are unchanged from the current implementation: `{3.0, 4.5, 5.5, 6.0, 5.5, 4.5}`.

**EQ coefficients recomputed each sample** (same as current):

```
troubleCoeffs[b] = makePeakingEq(sr, centers[b], qVals[b], cutDb[b])
```

The output biquad filter chain is then applied per-channel as today.

---

## 7. Timing: Attack and Release

The current implementation has no temporal smoothing on the EQ gain — it jumps directly to whatever the static level is. The dynamic version should smooth the envelope to avoid zippering artefacts on fast transients.

Rather than smoothing the gain directly, the RMS envelope naturally provides smoothing. The `cRmsA` time constant (~8ms) is appropriate for this band. No additional gain smoothing is needed beyond what the RMS envelope provides.

However, for the release (when a harshness burst ends), an asymmetric envelope with a slower release prevents pumping. Each band's RMS envelope uses attack/release coefficients:

```
cTroubleAtk = exp(-1 / (0.006 * sr))   // ~6ms attack
cTroubleRel = exp(-1 / (0.080 * sr))   // ~80ms release
```

Applied as:

```
const float tA = bandSample*bandSample > troubleBandRms[b] ? cTroubleAtk : cTroubleRel;
troubleBandRms[b] = tA * troubleBandRms[b] + (1 - tA) * (bandSample * bandSample);
```

---

## 8. New State Variables

### In `VxPolishDsp.h`, add to private section:

```cpp
// troubleSmooth dynamic detection
float cTroubleRefA   = 0.0f;       // one-pole coeff for reference LP
float cTroubleAtk    = 0.0f;       // per-band RMS attack coeff
float cTroubleRel    = 0.0f;       // per-band RMS release coeff
std::array<BiquadCoeffs, 6> troubleDetBpf {};  // detection bandpass coeffs (computed in prepare)
float troubleRefLp   = 0.0f;       // reference LP state
float troubleRefRms  = 0.0f;       // reference RMS state
std::array<float, 6> troubleBandZ1 {};   // detection biquad z1 per band
std::array<float, 6> troubleBandZ2 {};   // detection biquad z2 per band
std::array<float, 6> troubleBandRms {};  // per-band RMS envelope
```

The existing `troubleEqZ1` and `troubleEqZ2` (per-channel output filter state, `std::array<std::vector<float>, 6>`) are unchanged.

---

## 9. Changes to `prepare()`

Add after existing coefficient calculations:

```cpp
cTroubleRefA = onePoleCoeff(sr, 800.0f);
cTroubleAtk  = std::exp(-1.0f / (0.006f * fsr));
cTroubleRel  = std::exp(-1.0f / (0.080f * fsr));
{
    const std::array<float, 6> centers { 1400.0f, 2400.0f, 3600.0f, 5200.0f, 7600.0f, 10500.0f };
    const std::array<float, 6> qVals   { 1.10f,   1.20f,   1.20f,   1.15f,   1.05f,   0.90f   };
    for (size_t b = 0; b < 6; ++b)
        troubleDetBpf[b] = makeBandpass(sr, centers[b], qVals[b]);
}
```

---

## 10. Changes to `reset()`

Add:

```cpp
troubleRefLp  = 0.0f;
troubleRefRms = 0.0f;
troubleBandZ1.fill(0.0f);
troubleBandZ2.fill(0.0f);
troubleBandRms.fill(0.0f);
```

---

## 11. Changes to `processCorrective()`

### Remove (current static block):

```cpp
std::array<BiquadCoeffs, 6> troubleCoeffs{};
if (troubleAmt > 1.0e-6f) {
    const std::array<float, 6> centers { ... };
    const std::array<float, 6> qVals   { ... };
    const std::array<float, 6> maxCutDb{ ... };
    const float smoothCtx = ...;
    for (size_t b = 0; b < troubleCoeffs.size(); ++b) {
        const float cutDb = -maxCutDb[b] * troubleAmt * smoothCtx;
        troubleCoeffs[b] = makePeakingEq(sr, centers[b], qVals[b], cutDb);
    }
}
```

### Add before the per-sample loop (constants only):

```cpp
std::array<BiquadCoeffs, 6> troubleCoeffs{};  // computed per-sample below
const std::array<float, 6> troubleCenters { 1400.0f, 2400.0f, 3600.0f, 5200.0f, 7600.0f, 10500.0f };
const std::array<float, 6> troubleQVals   { 1.10f,   1.20f,   1.20f,   1.15f,   1.05f,   0.90f   };
const std::array<float, 6> troubleMaxCut  { 3.0f,    4.5f,    5.5f,    6.0f,    5.5f,    4.5f    };
const std::array<float, 6> troubleThresh  { 2.0f,    1.5f,    0.5f,    0.0f,    0.0f,    1.0f    };
constexpr float             troubleRange  = 6.0f;
const float smoothCtx = juce::jlimit(0.90f, 1.35f,
    1.00f + 0.30f * params.artifactRisk - 0.03f * speechPresence);
```

### Inside the per-sample loop, after computing `monoLinear`, add detection block:

```cpp
// troubleSmooth detection
if (troubleAmt > 1.0e-6f) {
    // Reference envelope
    troubleRefLp  = cTroubleRefA * troubleRefLp + (1.0f - cTroubleRefA) * monoLinear;
    troubleRefRms = cRmsA        * troubleRefRms + (1.0f - cRmsA)        * (troubleRefLp * troubleRefLp);
    const float refEnv = std::sqrt(troubleRefRms + 1.0e-12f);

    for (size_t b = 0; b < 6; ++b) {
        const float bs = processBiquadDf2(monoLinear, troubleDetBpf[b],
                                          troubleBandZ1[b], troubleBandZ2[b]);
        const float bsq = bs * bs;
        const float tA = bsq > troubleBandRms[b] ? cTroubleAtk : cTroubleRel;
        troubleBandRms[b] = tA * troubleBandRms[b] + (1.0f - tA) * bsq;
        const float bandEnv  = std::sqrt(troubleBandRms[b] + 1.0e-12f);
        const float ratioDb  = 20.0f * std::log10(bandEnv / refEnv + 1.0e-12f);
        const float excessDb = std::max(0.0f, ratioDb - troubleThresh[b]);
        const float drive    = juce::jlimit(0.0f, 1.0f, excessDb / troubleRange);
        const float cutDb    = -troubleMaxCut[b] * troubleAmt * drive * smoothCtx;
        troubleCoeffs[b]     = makePeakingEq(sr, troubleCenters[b], troubleQVals[b], cutDb);
    }
}
```

The existing per-channel output filter application block is unchanged:

```cpp
if (troubleAmt > 1.0e-6f) {
    for (size_t b = 0; b < troubleCoeffs.size(); ++b) {
        x = processBiquadDf2(x, troubleCoeffs[b],
                             troubleEqZ1[b][ch], troubleEqZ2[b][ch]);
    }
}
```

---

## 12. `troubleActivity` Reporting

The current implementation estimates activity from a fixed `troubleDb = 2.25 * troubleAmt`. Replace with the actual mean cut applied across all bands:

```cpp
// Inside per-sample loop, after computing drive[] values:
float bandCutSum = 0.0f;
for (size_t b = 0; b < 6; ++b)
    bandCutSum += troubleMaxCut[b] * troubleAmt * drive[b];
troubleAcc += bandCutSum / 6.0f;
```

Then at the block end (unchanged pattern):

```cpp
troubleActivity = juce::jlimit(0.0f, 1.0f, (troubleAcc / numSamples) / 4.0f);
```

---

## 13. Performance Considerations

The main cost increase is six additional `processBiquadDf2` calls on a mono signal per sample (detection), plus six `makePeakingEq` calls per sample (coefficient computation). `makePeakingEq` involves `std::pow`, `std::cos`, and `std::sin` — six calls per sample is likely to be the dominant cost.

**Mitigation:** Coefficient computation can be throttled to every N samples (e.g., every 4) since the detection envelopes have ~6ms time constants and sub-millisecond coefficient updates provide no audible benefit. This reduces trig cost by 4×.

This optimisation can be deferred until profiling confirms it is needed.

---

## 14. Tuning Notes

The threshold values in section 6 are starting estimates based on typical speech spectral balance. They should be validated with:

- Male and female close-mic vocal recordings
- Room mic recordings (more upper-mid buildup expected)
- Instrument sources in general mode

Key things to listen for:
- **Over-cutting on normal speech:** raise thresholds for bands 2–4.
- **Insufficient action on harsh sources:** lower thresholds or raise `maxCutDb`.
- **Pumping or modulation audible on sustained vowels:** increase `cTroubleRel` (slower release).
- **Slow response on transient harshness:** decrease `cTroubleAtk` (faster attack).

---

## 15. What Does Not Change

- The six output peaking EQ filter centre frequencies, Q values, and maximum cut depths.
- The output filter biquad state arrays (`troubleEqZ1`, `troubleEqZ2`).
- The `troubleAmt` parameter range and meaning.
- The `smoothCtx` context modifier (preserved as-is).
- The early-exit guard `if (troubleAmt <= 1e-6f)`.
- All other stages: deMud, deEss, plosive, compressor, recovery, limiter.
