# VX Deverb — Agent Implementation Guide: Phases 1–3

**Project:** VXCleaner / VX Suite — `vxsuite::deverb` namespace  
**Codebase language:** C++17, JUCE framework  
**Target files:** `VxDeverbSpectralProcessor.h/.cpp`, `VxDeverbProcessor.h/.cpp`,  
`VXDeverbTests.cpp`, `VXDeverbMeasure.cpp`  
**Build system:** CMake — target `VXDeverb_VST3`, tests `VXDeverbTests`, measure tool `VXDeverbMeasure`  
**Verification command after every phase:**
```
cmake --build build --target VXDeverbTests VXDeverbMeasure VXDeverb_VST3 -j4
ctest --test-dir build --output-on-failure -R VXDeverbTests
./build/VXDeverbMeasure --reduce 0.82 --body 0.0 --synth-rt60 0.8
```

---

## Context and existing state

The processor implements Habets (2009) LRSV spectral dereverberation. The STFT/OLA pipeline,
per-channel stereo, Wiener gain computation, temporal IIR gain smoothing, and dry-path latency
alignment are all in place and passing tests.

Three things are missing:

- **Phase 1 (2b):** Cepstral gain smoothing — eliminates musical noise artefacts, ~3 hours
- **Phase 2 (1):** Löllmann RT60 estimator — replaces the weak dual-envelope tracker, ~2 days
- **Phase 3:** Frame-online WPE voice mode — best-in-class voice dereverberation, ~5 days

Each phase is independently shippable. Implement them in order. Do not begin a later phase
until the earlier phase builds, tests pass, and the measure tool shows improvement.

---

## Phase 1 — Cepstral gain smoothing

### Goal

Replace the current per-bin IIR-only smoothing with a two-stage approach: temporal IIR
(already present) followed by cepstral log-domain frequency smoothing. This eliminates
isolated spectral gain spikes (musical noise) without blurring transients.

### What musical noise is and why this fixes it

The Wiener gain `G(m,k)` varies independently per bin. When one bin is heavily suppressed
and its neighbours are not, the output has a sharp spectral spike that sounds metallic.
Smoothing `log(G)` across K neighbouring bins forces the gain to vary smoothly in the
log-frequency domain, eliminating isolated spikes while preserving broadband gain shape.

### Step 1 — Add `logGain` scratch buffer to `ChannelState`

In `VxDeverbSpectralProcessor.h`, inside `struct ChannelState`, add:

```cpp
/** Scratch buffer for cepstral log-domain gain smoothing. Size = numBins.
 *  Pre-allocated in allocateAndResetChannel(); never resized on audio thread. */
std::vector<float> logGain;
```

### Step 2 — Allocate in `allocateAndResetChannel`

In `VxDeverbSpectralProcessor.cpp`, in `allocateAndResetChannel()`, add after the
`gainSmooth` allocation line:

```cpp
ch.logGain.assign(static_cast<size_t>(numBins), 0.0f);
```

### Step 3 — Add the constant to the header

In `VxDeverbSpectralProcessor.h`, in the algorithm constants section, add:

```cpp
/**
 * Half-width of the cepstral log-gain smoothing window (bins).
 * Total window = 2×kCepLifter + 1 = 13 bins.
 * Increase to 10 for more aggressive smoothing; decrease to 3 for more
 * bin-level resolution.  6 is a good default for mixed audio.
 */
static constexpr int kCepLifter = 6;
```

### Step 4 — Insert the smoothing pass in `processFrame`

In `VxDeverbSpectralProcessor.cpp`, in `processFrame()`, locate the per-bin gain loop
(Step 3 in the existing code, the loop over `k = 0..nbins`). The loop currently computes
`gainSmooth[k]` and immediately applies it. Split this into two passes:

**Pass A — compute and temporally smooth the gain (existing logic, unchanged):**
```cpp
for (size_t k = 0; k < nbins; ++k) {
    // ... existing wienerPow and gainTarget computation ...

    ch.gainSmooth[k] = kGainAlpha * ch.gainSmooth[k]
                       + (1.0f - kGainAlpha) * gainTarget;

    // Do NOT apply to fftBuf here yet — cepstral pass comes next.
    ch.magSqHist[histBase + k] = curPow;
    framePower += curPow;
}
```

**Pass B — cepstral log-domain smoothing (new, insert after Pass A):**
```cpp
// ── Cepstral gain smoothing ───────────────────────────────────────────────
// Smooth log(G) across kCepLifter neighbouring bins to suppress isolated
// spectral spikes (musical noise) without blurring transient content.
for (size_t k = 0; k < nbins; ++k)
    ch.logGain[k] = std::log(ch.gainSmooth[k] + 1.0e-10f);

for (size_t k = 0; k < nbins; ++k) {
    const int lo = std::max(0,             static_cast<int>(k) - kCepLifter);
    const int hi = std::min(static_cast<int>(nbins) - 1,
                             static_cast<int>(k) + kCepLifter);
    float sum = 0.0f;
    for (int j = lo; j <= hi; ++j)
        sum += ch.logGain[static_cast<size_t>(j)];
    const float smoothedGain = std::exp(sum / static_cast<float>(hi - lo + 1));

    ch.fftBuf[2 * k]     *= smoothedGain;
    ch.fftBuf[2 * k + 1] *= smoothedGain;
}
```

Remove the existing `ch.fftBuf` multiply lines from Pass A (they must not apply
the gain twice).

### Step 5 — Update `VXDeverbMeasure` to report cepstral status

In `VXDeverbMeasure.cpp`, update the `status` string reported alongside `tail_ratio` to
say `Spectral LRSV + cepstral smoothing` so it is clear in measure output which code path
ran.

### Step 6 — Add a cepstral-off bypass flag for A/B testing

In `VxDeverbSpectralProcessor.h`, add a debug accessor:

```cpp
/** Set to true to bypass cepstral smoothing (A/B testing only). */
bool debugNoCepstral = false;
```

Wrap the cepstral pass in `processFrame` with:
```cpp
if (!debugNoCepstral) { /* cepstral pass */ }
else { /* direct apply gainSmooth without smoothing */ }
```

Wire `--no-cepstral` flag in `VXDeverbMeasure` to set this flag.

### Step 7 — Verify

```
cmake --build build --target VXDeverbTests VXDeverbMeasure -j4
ctest --test-dir build --output-on-failure -R VXDeverbTests
./build/VXDeverbMeasure --reduce 0.82 --synth-rt60 0.8
./build/VXDeverbMeasure --reduce 0.82 --synth-rt60 0.8 --no-cepstral
```

Expected: `tail_ratio` will be **slightly higher** with cepstral smoothing than without
(the gain is slightly less aggressive per-bin, by design). The improvement is perceptual,
not measurable on the power-ratio metric. Both runs must produce finite, non-NaN output.
All existing tests must pass unchanged.

---

## Phase 2 — Löllmann blind RT60 estimator

### Goal

Replace the current dual-envelope decay tracker (which underestimates by ~50% and rarely
fires on continuous material) with Löllmann's (2018) closed-form ML estimator operating on
1/3-octave subbands. This provides a reliable, room-condition-adaptive RT60 that feeds the
LRSV coefficient.

### Reference

H.-W. Löllmann, C. Evers, A. Schmidt, H. Mellmann, H. Haeb-Umbach, P. A. Naylor (2018)
"The LOCATA Challenge Data Corpus for Acoustic Localization and Tracking", EUSIPCO 2018.
ML estimator variant: Löllmann et al. (2012) IEEE Trans. Audio, Speech, Lang. Process.

### Architecture overview

The estimator runs as a sidechain at a reduced sample rate (16 kHz internally, regardless
of plugin sample rate). It processes frames of ~307 ms with 9 sub-frames each. On detected
free-decay segments it computes a closed-form ML RT60 estimate requiring only O(N) arithmetic.
It maintains a histogram of recent estimates and returns the histogram peak. A recursive
smoother with γ=0.95 provides the final RT60 value fed to the LRSV coefficient.

It is implemented as a separate class `LollmannRt60Estimator` to keep it testable in
isolation.

### Step 1 — Create `VxDeverbRt60Estimator.h`

Create this file alongside `VxDeverbSpectralProcessor.h`:

```cpp
#pragma once
#include <vector>
#include <array>

namespace vxsuite::deverb {

/**
 * Blind RT60 estimator based on Löllmann et al. (2012/2018).
 *
 * Operates at a fixed internal rate of 16 kHz (decimation is handled
 * internally).  Call pushSamples() from the audio thread each block;
 * the estimator updates asynchronously at its own frame rate.
 *
 * getEstimatedRt60() is safe to call from the audio thread — it returns
 * the last committed estimate protected by an atomic load.
 *
 * Algorithm outline:
 *   1. Decimate input to 16 kHz via a simple integer decimator.
 *   2. Decompose into kNumBands 1/3-octave subbands via IIR filterbank.
 *   3. Accumulate frames of kFrameSamples; divide into kSubFrames sub-frames.
 *   4. In each sub-frame, compute instantaneous RMS energy.
 *   5. Detect monotonically decreasing energy sequences spanning ≥3 sub-frames.
 *   6. On detected decay: compute closed-form ML slope estimate (O(N) arithmetic).
 *   7. Convert slope to RT60; clamp to [kRt60Min, kRt60Max].
 *   8. Add to histogram; track peak.
 *   9. Smooth: rt60_smooth = γ·rt60_smooth + (1−γ)·histogram_peak.
 */
class LollmannRt60Estimator {
public:
    LollmannRt60Estimator();

    /**
     * Initialise for a given host sample rate.  Computes the decimation
     * ratio and resets all state.  Call before streaming.
     */
    void prepare(double hostSampleRate);

    /** Reset all history and estimator state. */
    void reset();

    /**
     * Push a block of mono audio samples.  The estimator decimates and
     * processes internally; call this once per processBlock from any
     * single channel (e.g. channel 0).
     *
     * @param samples  Pointer to float samples (read-only).
     * @param count    Number of samples in the block.
     */
    void pushSamples(const float* samples, int count);

    /**
     * Returns the current smoothed RT60 estimate in seconds.
     * Returns kDefaultRt60 until the estimator has collected enough data.
     * Safe to call from the audio thread (atomic load).
     */
    float getEstimatedRt60() const;

    // ── Debug / measure tool accessors ────────────────────────────────────
    /** Force the estimate to a fixed value (bypasses internal tracking). */
    void setFixedRt60(float seconds);
    void clearFixedRt60();

private:
    // ── Decimation ─────────────────────────────────────────────────────────
    int   decimRatio   = 3;         // ceil(hostSampleRate / kInternalRate)
    int   decimCounter = 0;
    float decimAccum   = 0.0f;

    // ── Subband filterbank (one biquad per band) ───────────────────────────
    static constexpr int kNumBands = 9; // 1/3-octave bands covering 125 Hz – 4 kHz
    struct BiquadState { float x1=0, x2=0, y1=0, y2=0; };
    struct BiquadCoeffs { float b0,b1,b2,a1,a2; };
    std::array<BiquadCoeffs, kNumBands> bandCoeffs;
    std::array<BiquadState,  kNumBands> bandState;

    void computeBandCoeffs(); // populates bandCoeffs from kInternalRate

    // ── Frame accumulation ─────────────────────────────────────────────────
    static constexpr int   kInternalRate   = 16000;
    static constexpr float kFrameDurationS = 0.3072f;             // ≈ 307 ms
    static constexpr int   kFrameSamples   = static_cast<int>(kInternalRate * kFrameDurationS); // 4915
    static constexpr int   kSubFrames      = 9;
    static constexpr int   kSubFrameSamples = kFrameSamples / kSubFrames; // 546

    // Energy accumulator per subframe per band
    std::array<std::vector<float>, kNumBands> subFrameEnergy; // [band][subframe]
    std::array<int,                kNumBands> subFrameCount;  // samples in current subframe
    std::array<int,                kNumBands> subFrameIdx;    // current subframe index

    // ── ML closed-form slope estimator ────────────────────────────────────
    /**
     * Given N log-energy values assumed to follow a linear decay,
     * returns the ML estimate of the slope (dB/sample) using the
     * closed-form Cramér-Rao-efficient solution.
     * Requires only 7N+6 multiplications and 7N+5 additions.
     */
    static float mlSlopeEstimate(const float* logEnergy, int N);

    /** Convert a log-energy decay slope to RT60 (seconds). */
    static float slopeToRt60(float slopePerSubFrame);

    // ── Histogram accumulator ──────────────────────────────────────────────
    static constexpr int   kHistBins  = 120;
    static constexpr float kRt60Min   = 0.05f;
    static constexpr float kRt60Max   = 8.00f;
    static constexpr int   kHistDepth = 800;  // keep last 800 estimates

    std::vector<int>   histogram;       // kHistBins counts
    std::vector<float> historyBuffer;   // ring of last kHistDepth RT60 values
    int                historyWritePos = 0;
    int                historyCount    = 0;

    void addEstimate(float rt60);
    float histogramPeak() const;

    // ── Output smoothing ───────────────────────────────────────────────────
    static constexpr float kSmoothGamma   = 0.95f;
    static constexpr float kDefaultRt60   = 0.50f;

    float smoothedRt60 = kDefaultRt60;
    bool  hasEstimate  = false;

    // Atomic-safe output (written on audio thread, read on audio thread)
    // Using float with relaxed read is safe since worst case is one stale frame.
    float outputRt60 = kDefaultRt60;

    // Debug override
    float fixedRt60   = 0.0f;
    bool  useFixed    = false;

    // ── Per-band processing ────────────────────────────────────────────────
    void processBandSample(int band, float sample);
    void onSubFrameComplete(int band);
    void onFrameComplete(int band);
};

} // namespace vxsuite::deverb
```

### Step 2 — Create `VxDeverbRt60Estimator.cpp`

Implement the following functions. Do not use dynamic allocation after `prepare()`.

**`prepare()`:**
- Compute `decimRatio = std::max(1, static_cast<int>(std::round(hostSampleRate / kInternalRate)))`.
- Call `computeBandCoeffs()`.
- Allocate `subFrameEnergy[b].assign(kSubFrames, 0.0f)` for each band.
- Allocate `histogram.assign(kHistBins, 0)`.
- Allocate `historyBuffer.assign(kHistDepth, 0.0f)`.
- Call `reset()`.

**`computeBandCoeffs()`:**  
Design 9 second-order IIR bandpass filters (biquads) centred on the 1/3-octave centre
frequencies: 125, 160, 200, 250, 315, 400, 500, 630, 800 Hz (at `kInternalRate = 16000 Hz`).
Use constant-Q bandpass design: `Q = 1 / (2·sinh(ln(2)/2))` ≈ 4.318 for 1/3-octave bands.
For each centre frequency `fc`:
```
w0 = 2π·fc / kInternalRate
alpha = sin(w0) / (2·Q)
b0 =  alpha
b1 =  0
b2 = -alpha
a0 =  1 + alpha
a1 = -2·cos(w0)
a2 =  1 - alpha
// normalise by a0
```

**`pushSamples()`:**  
For each input sample, accumulate into `decimAccum`. Every `decimRatio` samples,
pass `decimAccum / decimRatio` through each band's biquad via `processBandSample()`.
Reset `decimAccum = 0`, `decimCounter = 0`.

**`processBandSample(band, sample)`:**  
Run the Direct Form II biquad transposed. Accumulate `sample * sample` into
`subFrameEnergy[band][subFrameIdx[band]]`. Increment `subFrameCount[band]`.
When `subFrameCount[band] == kSubFrameSamples`, call `onSubFrameComplete(band)`.

**`onSubFrameComplete(band)`:**  
Advance `subFrameIdx[band]`. When `subFrameIdx[band] == kSubFrames`, call
`onFrameComplete(band)`. Reset `subFrameCount[band] = 0`.
Set `subFrameEnergy[band][new_subFrameIdx] = 0.0f`.

**`onFrameComplete(band)`:**  
1. Extract `logEnergy[i] = log(subFrameEnergy[band][i] + 1e-20f)` for `i = 0..kSubFrames-1`.
2. Find longest run of monotonically decreasing `logEnergy` values with length ≥ 3.
3. If found, call `mlSlopeEstimate()` on that subsequence.
4. Convert to RT60 via `slopeToRt60()`.
5. If `kRt60Min ≤ rt60 ≤ kRt60Max`, call `addEstimate(rt60)`.
6. Reset `subFrameIdx[band] = 0`.

**`mlSlopeEstimate(logEnergy, N)`:**  
Closed-form linear regression slope (ML-efficient for Gaussian noise):
```
sum_x  = N*(N-1)/2
sum_x2 = N*(N-1)*(2N-1)/6
sum_y  = sum of logEnergy[i]
sum_xy = sum of i * logEnergy[i]
denom  = N * sum_x2 - sum_x * sum_x
slope  = (N * sum_xy - sum_x * sum_y) / denom
```
This is the exact 7N+6 multiply, 7N+5 add implementation referenced in the paper.

**`slopeToRt60(slopePerSubFrame)`:**  
```
// slope is in Nepers/subFrame (log base e)
// RT60 = time for 60 dB = ln(1000) decay
// slopePerSubFrame < 0 for a decaying signal
float slopePerSec = slopePerSubFrame
                    * (kInternalRate / static_cast<float>(kSubFrameSamples));
if (slopePerSec >= 0.0f) return 0.0f;  // not a decay
return -std::log(1000.0f) / slopePerSec;
// = -6.908 / slopePerSec
```

**`addEstimate(rt60)`:**  
Convert `rt60` to a histogram bin index:
```
int bin = static_cast<int>((rt60 - kRt60Min) / (kRt60Max - kRt60Min) * kHistBins);
bin = std::clamp(bin, 0, kHistBins - 1);
```
Add to ring: `historyBuffer[historyWritePos] = rt60`. If `historyCount == kHistDepth`,
decrement the histogram bin of the oldest entry before overwriting. Increment `historyCount`
(capped at `kHistDepth`). Write `historyWritePos = (historyWritePos + 1) % kHistDepth`.
Increment `histogram[bin]`. Call `histogramPeak()` to get the peak RT60. Smooth:
`smoothedRt60 = kSmoothGamma * smoothedRt60 + (1-kSmoothGamma) * peak`. Set
`outputRt60 = smoothedRt60`. Set `hasEstimate = true`.

**`histogramPeak()`:**  
Find the bin with the highest count. Return `kRt60Min + (bin + 0.5f) * (kRt60Max - kRt60Min) / kHistBins`.

**`getEstimatedRt60()`:**  
```cpp
if (useFixed) return fixedRt60;
return outputRt60;
```

### Step 3 — Integrate into `SpectralProcessor`

In `VxDeverbSpectralProcessor.h`:
- Add `#include "VxDeverbRt60Estimator.h"`.
- Add `LollmannRt60Estimator rt60Estimator;` as a member (replaces per-channel RT60 state).
- Remove the `envFast`, `envSlow`, `rt60` fields from `ChannelState` (they are now in the estimator).

In `VxDeverbSpectralProcessor.cpp`:
- In `prepare()`, call `rt60Estimator.prepare(sr)`.
- In `reset()`, call `rt60Estimator.reset()`.
- In `processInPlace()`, before the per-channel loop, push channel 0's input into the estimator:
  ```cpp
  rt60Estimator.pushSamples(buffer.getReadPointer(0), numSmp);
  ```
- In `processFrame()`, replace the per-channel `lrsvCoeff` computation with:
  ```cpp
  const float rt60      = rt60Estimator.getEstimatedRt60();
  const float lrsvCoeff = lrsvCoeffFromRt60(rt60);
  ```
- Remove the old dual-envelope RT60 tracking block from `processFrame()`.

### Step 4 — Wire debug path

In `VxDeverbSpectralProcessor.h`, add a pass-through accessor:
```cpp
void setFixedRt60(float seconds) { rt60Estimator.setFixedRt60(seconds); }
void clearFixedRt60()             { rt60Estimator.clearFixedRt60(); }
float getTrackedRt60() const      { return rt60Estimator.getEstimatedRt60(); }
```

Update `VXDeverbMeasure.cpp` to call `setFixedRt60()` when `--rt60-preset` is given,
and `getTrackedRt60()` for the `tracked_rt60_final` report.

### Step 5 — Add to CMakeLists.txt

Add `VxDeverbRt60Estimator.cpp` to the `VXDeverb_VST3` source list and to the
`VXDeverbTests` and `VXDeverbMeasure` compilation units (or a shared library target
if one exists).

### Step 6 — Add tests in `VXDeverbTests.cpp`

Add a test case `RT60Estimator`:

1. Construct `LollmannRt60Estimator`. Call `prepare(48000.0)`.
2. Generate a synthetic exponential-decay noise signal with ground-truth RT60 = 0.8s:
   `sample[n] = randn() × exp(-6.908 × n / (0.8 × 48000))`, length = 5 seconds.
3. Call `pushSamples()` in blocks of 512.
4. Assert `getEstimatedRt60()` is within ±40% of 0.8s after all samples are pushed.
   (Löllmann achieves ±30% typically; ±40% gives margin for the subband overlap.)
5. Call `reset()`. Assert `getEstimatedRt60() == 0.5f` (returns to default after reset).
6. Call `setFixedRt60(1.2f)`. Assert `getEstimatedRt60() == 1.2f`.

### Step 7 — Verify

```
cmake --build build --target VXDeverbTests VXDeverbMeasure VXDeverb_VST3 -j4
ctest --test-dir build --output-on-failure -R VXDeverbTests
./build/VXDeverbMeasure --reduce 0.82 --synth-rt60 0.8 --print-rt60 --render-seconds 15
```

Expected: `tracked_rt60_final` within ±40% of 0.8 on the 15-second render. The tail_ratio
should equal or slightly exceed the Phase 1 result — the better RT60 estimate means the
LRSV coefficient is more accurate and may subtly improve suppression.

---

## Phase 3 — Frame-online WPE voice mode

### Goal

Add an optional second dereverberation stage that activates when the user enables "Voice
Mode". This stage implements single-channel frame-online (RLS) Weighted Prediction Error
filtering operating in the STFT domain. It achieves 0.5–0.7 PESQ improvement on speech
versus ~0.3–0.5 for spectral subtraction alone.

WPE uses a speech-specific complex Gaussian source model. It must be bypassed for
non-voice content (music, mixed audio) — the model will distort tonal and sustained
harmonic content.

### References

- Nakatani, Yoshioka et al. (2010) "Speech Dereverberation Based on Variance-Normalized
  Delayed Linear Prediction", IEEE Trans. Audio, Speech, Lang. Process.
- Heymann et al. (2018) "Frame-Online DNN-WPE Dereverberation", IWAENC 2018.
- nara_wpe reference implementation: https://github.com/fgnt/nara_wpe (MIT licence)
  — `nara_wpe/wpe.py`, function `online_wpe_step()` is the definitive algorithm reference.

### Architecture overview

WPE operates per STFT bin. For each bin `k` at frame `m`, it maintains:
- `G[k]` — complex prediction filter vector of length K (taps)
- `R_inv[k]` — inverse correlation matrix, K×K complex, maintained via Woodbury identity
- `lambda[k]` — time-varying variance estimate of the desired signal

The frame-online (RLS) update per bin:

```
1. Form delayed observation vector: ỹ[k] = [Y(m−Δ, k), Y(m−Δ−1, k), ..., Y(m−Δ−K+1, k)]
2. Filter:      x̂(m,k) = Y(m,k) − G[k]^H · ỹ[k]
3. PSD update:  λ(m,k)  = β·λ(m−1,k) + (1−β)·|x̂(m,k)|²   (causal only)
4. Kalman gain: k_vec   = R_inv[k] · ỹ[k] / (α·λ(m,k) + ỹ[k]^H · R_inv[k] · ỹ[k])
5. R_inv update: R_inv[k] = (R_inv[k] − k_vec · ỹ[k]^H · R_inv[k]) / α
6. Filter update: G[k] = G[k] + k_vec · conj(x̂(m,k))
```

No matrix inversion — the Woodbury identity maintains `R_inv` via rank-1 updates.
Per-frame cost: O(K²·F) complex MACs, where K=10 taps, F=513 bins → ~51K complex MACs/frame.

### Step 1 — Create `VxDeverbWpeStage.h`

```cpp
#pragma once
#include <vector>
#include <complex>

namespace vxsuite::deverb {

/**
 * Frame-online single-channel WPE dereverberation stage.
 *
 * Operates in the STFT domain — it receives and returns complex spectra
 * directly, without its own FFT.  It is designed to be called from
 * SpectralProcessor::processFrame() after the forward FFT and before
 * the inverse FFT.
 *
 * Memory:
 *   Per bin: G[K] complex filter (2K floats), R_inv[K×K] complex matrix
 *   (2K² floats), lambda (1 float), plus K frames of history (2K floats).
 *   At K=10, F=513: ~(20+200+1+20)×513 ≈ 123 KB.
 *
 * Call prepare() before streaming; processSpectrum() once per STFT frame.
 * Zero heap allocations after prepare().
 */
class WpeStage {
public:
    WpeStage();

    /**
     * Allocate all state for the given STFT configuration.
     * @param numBins   Positive-frequency bin count (fftSize/2 + 1).
     * @param K         Prediction filter taps (default 10).
     * @param delta     Prediction delay in frames (default 3).
     * @param alpha     RLS forgetting factor (default 0.995).
     * @param beta      PSD smoothing factor (default 0.80).
     */
    void prepare(int numBins, int K = 10, int delta = 3,
                 float alpha = 0.995f, float beta = 0.80f);

    /** Zero all filter, correlation, and history state. */
    void reset();

    /**
     * Process one STFT frame in-place.
     *
     * @param re   Real parts of positive-frequency bins [0..numBins−1].
     *             Modified in-place with WPE output.
     * @param im   Imaginary parts.  Modified in-place.
     * @param amount  Wet mix 0–1.  0 = bypass (no state update),
     *                1 = full WPE output.
     */
    void processSpectrum(float* re, float* im, float amount) noexcept;

private:
    using Cx = std::complex<float>;

    int   numBins_ = 0;
    int   K_       = 10;
    int   delta_   = 3;
    float alpha_   = 0.995f;
    float beta_    = 0.80f;

    // History ring: [historyDepth × numBins] complex
    // historyDepth = K + delta (oldest frame needed for prediction)
    int              histDepth_ = 0;
    int              histWrite_ = 0;
    std::vector<Cx>  history_;   // [histDepth_ × numBins_], row-major

    // Per-bin state
    std::vector<Cx>  G_;         // [numBins_ × K_] filter coefficients
    std::vector<Cx>  R_inv_;     // [numBins_ × K_ × K_] inverse correlation
    std::vector<float> lambda_;  // [numBins_] variance estimate

    // Per-frame scratch (pre-allocated, no audio-thread allocation)
    std::vector<Cx>  yDelayed_;  // [K_] per-bin delayed observation vector
    std::vector<Cx>  kVec_;      // [K_] Kalman gain vector
    std::vector<Cx>  tmpK_;      // [K_] scratch

    // Inline accessors into flat arrays
    Cx& hist(int frame, int bin)       { return history_[static_cast<size_t>(frame) * static_cast<size_t>(numBins_) + static_cast<size_t>(bin)]; }
    Cx& g(int bin, int tap)            { return G_[static_cast<size_t>(bin)  * static_cast<size_t>(K_) + static_cast<size_t>(tap)]; }
    Cx& rinv(int bin, int r, int c)    { return R_inv_[static_cast<size_t>(bin) * static_cast<size_t>(K_) * static_cast<size_t>(K_) + static_cast<size_t>(r) * static_cast<size_t>(K_) + static_cast<size_t>(c)]; }
};

} // namespace vxsuite::deverb
```

### Step 2 — Implement `VxDeverbWpeStage.cpp`

**`prepare()`:**
- Validate `numBins >= 1`, `K >= 1`, `delta >= 1`.
- `histDepth_ = K + delta`.
- `history_.assign(histDepth_ * numBins, Cx{0,0})`.
- `G_.assign(numBins * K, Cx{0,0})`.
- `R_inv_.assign(numBins * K * K, Cx{0,0})`.
- For each bin, initialise `R_inv` as `kRinvInit × I` (identity scaled by 1.0):
  ```cpp
  for (int k = 0; k < numBins; ++k)
      for (int i = 0; i < K; ++i)
          rinv(k, i, i) = Cx{1.0f, 0.0f};
  ```
- `lambda_.assign(numBins, 1.0f)`.
- Allocate scratch: `yDelayed_`, `kVec_`, `tmpK_` all size K.

**`processSpectrum(re, im, amount)`:**

```cpp
if (amount < 1e-5f) {
    // Bypass: still update history ring but do not modify spectrum.
    // This prevents a discontinuity if the user re-enables voice mode.
    for (int k = 0; k < numBins_; ++k)
        hist(histWrite_, k) = Cx{re[k], im[k]};
    histWrite_ = (histWrite_ + 1) % histDepth_;
    return;
}

for (int k = 0; k < numBins_; ++k) {
    const Cx Y_mk{re[k], im[k]};  // current observation

    // 1. Build delayed observation vector ỹ from history ring.
    //    Frame (histWrite_ - delta - i + histDepth_) % histDepth_ gives
    //    the observation at lag (delta + i) frames back.
    for (int i = 0; i < K_; ++i) {
        const int hIdx = (histWrite_ - delta_ - i + histDepth_ * 2) % histDepth_;
        yDelayed_[static_cast<size_t>(i)] = hist(hIdx, k);
    }

    // 2. Filter: x̂ = Y(m,k) − G[k]^H · ỹ
    Cx xhat = Y_mk;
    for (int i = 0; i < K_; ++i)
        xhat -= std::conj(g(k, i)) * yDelayed_[static_cast<size_t>(i)];

    // 3. PSD variance update (causal: uses filtered output, not input)
    lambda_[static_cast<size_t>(k)] =
        beta_ * lambda_[static_cast<size_t>(k)] + (1.0f - beta_) * std::norm(xhat);
    const float lam = std::max(lambda_[static_cast<size_t>(k)], 1.0e-20f);

    // 4. Kalman gain: k_vec = R_inv · ỹ / (α·λ + ỹ^H · R_inv · ỹ)
    //    First: tmpK = R_inv · ỹ
    for (int r = 0; r < K_; ++r) {
        Cx s{0,0};
        for (int c = 0; c < K_; ++c)
            s += rinv(k, r, c) * yDelayed_[static_cast<size_t>(c)];
        tmpK_[static_cast<size_t>(r)] = s;
    }
    //    Then: denom = α·λ + ỹ^H · tmpK
    Cx denom{alpha_ * lam, 0.0f};
    for (int i = 0; i < K_; ++i)
        denom += std::conj(yDelayed_[static_cast<size_t>(i)]) * tmpK_[static_cast<size_t>(i)];
    const float denomR = std::max(denom.real(), 1.0e-20f);  // denom is real-valued
    for (int i = 0; i < K_; ++i)
        kVec_[static_cast<size_t>(i)] = tmpK_[static_cast<size_t>(i)] / denomR;

    // 5. R_inv rank-1 update: R_inv = (R_inv − k_vec · ỹ^H · R_inv) / α
    //    Compute outer product k_vec · ỹ^H · R_inv and subtract in-place.
    for (int r = 0; r < K_; ++r) {
        // tmp2[c] = ỹ^H · R_inv row = sum_j conj(y[j]) * R_inv[j,c]
        // (reuse tmpK_ as temp row)
        Cx rowDot{0,0};
        for (int j = 0; j < K_; ++j)
            rowDot += std::conj(yDelayed_[static_cast<size_t>(j)]) * rinv(k, j, r);
        // Subtract kVec[r] * rowDot from entire row r, then divide by alpha
        // (done column by column to stay in the existing loop)
        for (int c = 0; c < K_; ++c) {
            Cx& elem = rinv(k, r, c);
            // We need: R_inv[r,c] = (R_inv[r,c] - kVec[r] * ỹ^H * R_inv[:,c]) / α
            // Pre-accumulate: ỹ^H · R_inv[:,c]
            // NOTE: to avoid recomputing, restructure as two separate passes below.
            (void)elem;
        }
    }
    // Correct R_inv update — two-pass to avoid overwriting mid-computation:
    // Pass A: compute update_matrix[r,c] = kVec[r] * sum_j conj(y[j]) * R_inv[j,c]
    // Pass B: R_inv[r,c] = (R_inv[r,c] - update_matrix[r,c]) / alpha
    // Use tmpK_ as scratch for one row at a time.
    for (int r = 0; r < K_; ++r) {
        for (int c = 0; c < K_; ++c) {
            Cx yHRinv{0,0};
            for (int j = 0; j < K_; ++j)
                yHRinv += std::conj(yDelayed_[static_cast<size_t>(j)]) * rinv(k, j, c);
            rinv(k, r, c) = (rinv(k, r, c) - kVec_[static_cast<size_t>(r)] * yHRinv) / alpha_;
        }
    }

    // 6. Filter update: G[k] = G[k] + k_vec · conj(x̂)
    for (int i = 0; i < K_; ++i)
        g(k, i) += kVec_[static_cast<size_t>(i)] * std::conj(xhat);

    // 7. Write enhanced output bin (wet mix)
    const Cx yOut = amount * xhat + (1.0f - amount) * Y_mk;
    re[k] = yOut.real();
    im[k] = yOut.imag();
}

// 8. Write current frame into history ring.
for (int k = 0; k < numBins_; ++k)
    hist(histWrite_, k) = Cx{re[k], im[k]};
histWrite_ = (histWrite_ + 1) % histDepth_;
```

**Important implementation note on the R_inv update:** The double-pass version written
above has an O(K³) inner loop per bin. At K=10 this is 1000 complex multiply-adds per bin,
×513 bins = 513K MACs per frame. This is acceptable for a first implementation (~2ms at
4GHz single-core). A production optimisation would cache `ỹ^H · R_inv` outside the r loop,
reducing to O(K²) per bin. Add a `// TODO: optimise to O(K²) with cached ỹ^H·R_inv` comment.

### Step 3 — Integrate `WpeStage` into `SpectralProcessor`

**In `VxDeverbSpectralProcessor.h`:**
- Add `#include "VxDeverbWpeStage.h"`.
- Add `WpeStage wpeStage;` as a member.
- Add `bool voiceMode = false;` as a member (toggled by the UI).
- Add `float wpeAmount = 1.0f;` as a member.

**In `VxDeverbSpectralProcessor.cpp`:**

In `prepare()`, after existing setup:
```cpp
wpeStage.prepare(numBins);
```

In `reset()`:
```cpp
wpeStage.reset();
```

In `processFrame()`, after Step 3 (LRSV gain applied to `fftBuf`) and before Step 5
(mirror negative-frequency bins), insert:

```cpp
// ── WPE voice-mode stage (optional) ──────────────────────────────────────
// Only active when voiceMode is enabled.  Passes the real/imaginary parts
// of the positive-frequency bins directly; WpeStage does not need to know
// about the full-spectrum layout.
if (voiceMode) {
    // Extract re/im into contiguous scratch (WpeStage expects separate arrays)
    // or pass fftBuf directly with stride-2 pointers — either is correct.
    // Simplest: pass even/odd elements of fftBuf with a helper:
    static thread_local std::vector<float> wpeRe, wpeIm;
    if (static_cast<int>(wpeRe.size()) < numBins) {
        wpeRe.resize(static_cast<size_t>(numBins));
        wpeIm.resize(static_cast<size_t>(numBins));
    }
    for (int k = 0; k < numBins; ++k) {
        wpeRe[static_cast<size_t>(k)] = ch.fftBuf[2 * k];
        wpeIm[static_cast<size_t>(k)] = ch.fftBuf[2 * k + 1];
    }
    wpeStage.processSpectrum(wpeRe.data(), wpeIm.data(), wpeAmount);
    for (int k = 0; k < numBins; ++k) {
        ch.fftBuf[2 * k]     = wpeRe[static_cast<size_t>(k)];
        ch.fftBuf[2 * k + 1] = wpeIm[static_cast<size_t>(k)];
    }
}
```

**Note:** The `thread_local` scratch is technically an allocation on first call. Replace with
a pre-allocated `wpeReScratch` / `wpeImScratch` member of `SpectralProcessor` (sized
`numBins`, allocated in `prepare()`) to be fully allocation-free on the audio thread.

### Step 4 — Wire voice mode to `VXDeverbProcessor`

In `VxDeverbProcessor.h`, add a parameter ID constant:
```cpp
constexpr std::string_view kVoiceModeParam = "voicemode";
```

Add `kVoiceModeParam` to `makeLayout()` as a `juce::AudioParameterBool`.

In `processProduct()`, read the parameter and forward to `deverbProcessor`:
```cpp
const bool voiceMode = static_cast<bool>(
    parameters.getRawParameterValue(kVoiceModeParam.data())->load());
deverbProcessor.voiceMode = voiceMode;
```

The UI will expose this as a toggle via the existing `EditorBase` infrastructure.

### Step 5 — Add `--voice` flag to `VXDeverbMeasure`

In `VXDeverbMeasure.cpp`, add `--voice` flag that sets `deverbProcessor.voiceMode = true`
before the render loop. Report `voice_mode: on/off` alongside `tail_ratio`.

### Step 6 — Add tests in `VXDeverbTests.cpp`

Add test case `WpeVoiceMode`:

1. Construct `SpectralProcessor`. Call `prepare(48000.0, 4096)`.
2. Generate the same synthetic exponential-decay signal as in the RT60 estimator test.
3. Run with `voiceMode = false`. Record `tail_ratio_spectral`.
4. Call `reset()`. Run with `voiceMode = true`, same input. Record `tail_ratio_wpe`.
5. Assert `tail_ratio_wpe <= tail_ratio_spectral` — WPE must not increase the tail.
   (It may be equal or slightly lower on non-speech material, which is expected.)
6. Run a silence input through voice mode. Assert output is silence (WPE must not produce
   output from zero input regardless of filter state).
7. Assert `getLatencySamples()` is unchanged by voice mode.

### Step 7 — Verify

```
cmake --build build --target VXDeverbTests VXDeverbMeasure VXDeverb_VST3 -j4
ctest --test-dir build --output-on-failure -R VXDeverbTests
./build/VXDeverbMeasure --reduce 0.82 --synth-rt60 0.8 --voice
./build/VXDeverbMeasure --reduce 0.82 --synth-rt60 0.8
```

Expected: WPE voice mode result on synthetic noise will be comparable or marginally
better than spectral-only — the difference is primarily perceptual on real speech material.
All tests pass. Plugin builds and loads in Reaper.

---

## Cross-phase verification checklist

Run this after all three phases are complete:

```
# Full build
cmake --build build --target VXDeverbTests VXDeverbMeasure VXDeverb_VST3 -j4

# All tests
ctest --test-dir build --output-on-failure -R VXDeverbTests

# Synthetic fixture comparisons (all four combinations)
./build/VXDeverbMeasure --reduce 0.82 --synth-rt60 0.8 --render-seconds 15 --print-rt60
./build/VXDeverbMeasure --reduce 0.82 --synth-rt60 0.8 --render-seconds 15 --print-rt60 --rt60-preset 0.8
./build/VXDeverbMeasure --reduce 0.82 --synth-rt60 0.8 --render-seconds 15 --voice
./build/VXDeverbMeasure --reduce 0.82 --synth-rt60 0.8 --render-seconds 15 --voice --no-cepstral

# Identity and coherence
./build/VXDeverbMeasure --reduce 0.0  # must report tail_ratio ≈ 1.0
./build/VXDeverbMeasure --reduce 1.0  # latency_samples must match getLatencySamples()
```

Expected outcome summary:

| Configuration | Expected tail_ratio | Notes |
|---|---|---|
| Spectral only, default RT60 | ~0.18 | Baseline from Phase 0 |
| + Cepstral smoothing | ~0.18–0.20 | Slightly higher is expected |
| + Löllmann RT60 (15s render) | ~0.17–0.19 | Marginal metric improvement |
| + WPE voice mode (synth noise) | ~0.17–0.19 | Perceptual gain not captured by metric |
| reduce=0.0 | ~1.0 (identity) | Must pass identity test |

The `tail_ratio` metric is not the primary quality indicator for Phases 1–3. The correct
evaluation surface for all three phases is ears-on real room recordings: voice material at
RT60 ≈ 0.4–1.5s, and mixed polyphonic content. Load the VST3 in Reaper and A/B against
bypass on material from the REVERB Challenge or similar corpus before declaring any phase
complete for production use.
