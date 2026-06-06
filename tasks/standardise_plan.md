# DSP Standardisation Plan
# Reference: standardise.md gap report (2026-06-05)
# Principle: Framework-first — every primitive built once, adopted by all products.
# STATUS: COMPLETED 2026-06-05. All items below are implemented and build passes.

---

## Phase 1 — Framework Primitives (no product changes, purely additive)

These go in `Source/vxstudio/framework/`. No existing file is touched.

---

### 1.1  VxStudioParamSmoother.h  — sample-accurate scalar ramp

**Problem:** `BlockSmoothedControl` only smooths at block rate (one value per `processBlock`).
For filter-coefficient parameters (Proximity closerAmount, airAmount, finish output gain) a
discontinuous jump mid-block causes clicks and audible zipper noise.

**Solution:** A thin wrapper around `juce::SmoothedValue` that:
- Is constructed with a ramp length in seconds
- Offers `setTarget(float, double sampleRate)` to arm the ramp
- Offers `snapTo(float)` for first-prepare (no ramp)
- Offers `getNext()` for per-sample use inside a DSP loop
- Offers `getCurrentValue()` for block-rate reads that don't need per-sample precision

```cpp
// Source/vxstudio/framework/VxStudioParamSmoother.h
namespace vxsuite {

class ParamSmoother {
public:
    void prepare(double sampleRate, float rampSeconds) noexcept;
    void snapTo(float value) noexcept;            // instant, no ramp
    void setTarget(float value) noexcept;         // arms ramp
    float getNext() noexcept;                     // per-sample, advances ramp
    float getCurrentValue() const noexcept;
    bool isSmoothing() const noexcept;
private:
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoother;
};

} // namespace vxsuite
```

**Adopt in:**
- `ProximityDsp` — smooth `closerAmount`, `airAmount`, `mudAmount` before coefficient update
- `ProximityClassicDsp` — same
- `finish::Dsp` — smooth `outputGainDb` parameter
- `SpeechClarity` DSPs — smooth outer `strength` param before envelope scaling
- `ToneRefine` DSPs — smooth `strength` param
- `ClarityDsp` — smooth `clean` / `focus`

---

### 1.2  VxStudioSilenceGuard.h  — shared idle detection

**Problem:** `DenoiserDsp`, `SpectralProcessor`, `RebalanceDsp`, `DeepFilterNetService` run full
FFT/neural pipelines on silent input. Costly in multi-instance sessions.

**Solution:** A stateless RMS helper + a stateful hysteresis guard, both usable from the
processor layer without touching DSP internals.

```cpp
// Source/vxstudio/framework/VxStudioSilenceGuard.h
namespace vxsuite {

// Stateless: compute block RMS in one call
float blockRmsLinear(const juce::AudioBuffer<float>& buffer) noexcept;

// Stateful: hysteresis so we don't flap on fade-outs
class SilenceGuard {
public:
    // threshold: linear RMS (default ≈ −54 dBFS = 0.002f)
    // holdBlocks: how many silent blocks before declaring idle (default 8)
    void prepare(float thresholdLinear = 0.002f, int holdBlocks = 8) noexcept;
    void reset() noexcept;

    // Call once per processBlock BEFORE calling DSP.
    // Returns true  → skip DSP (silent)
    // Returns false → run DSP normally
    bool update(const juce::AudioBuffer<float>& buffer) noexcept;

    bool isSilent() const noexcept { return silentBlockCount >= holdBlocks; }

private:
    float threshold = 0.002f;
    int   holdBlocks = 8;
    int   silentBlockCount = 0;
};

} // namespace vxsuite
```

**Adopt in processor layer (not DSP layer):**
- `VxDenoiserProcessor::processProduct()` — guard before `denoiserDsp.processInPlace()`
- `VxDeverbProcessor::processProduct()` — guard before `deverbProcessor.processInPlace()`
- `VxRebalanceProcessor::processProduct()` — guard before `dsp.process()`
- `VxDeepFilterNetProcessor::processProduct()` — guard before `engine.processRealtime()`
- `VxLevelerProcessor::processProduct()` — guard (activity state, not skip; leveler needs to track silence)

Note: The DSP internal pipelines keep their state (FIFOs stay intact). The guard only prevents
calling `processBlock` on them; on resume the latency pre-warm means the first ~16 ms output is
the tail from before silence, which is correct behaviour.

---

### 1.3  VxStudioMeteringSnapshot.h  — standard metering contract

**Problem:** Each product exposes ad-hoc metering getters. The UI can't query them uniformly.
No LUFS or delta monitoring flag is standardised.

**Solution:** A common snapshot struct + virtual getter on `ProcessorBase`.

```cpp
// Source/vxstudio/framework/VxStudioMeteringSnapshot.h
namespace vxsuite {

struct MeteringSnapshot {
    float gainReductionDb      = 0.0f;  // 0 = no GR; positive = reduction amount
    float noiseFloorDb         = -80.0f;
    float signalPresence       = 0.0f;  // 0–1 (speech/signal activity)
    float compActivity         = 0.0f;  // 0–1 (compressor/leveler ride)
    float limiterActivity      = 0.0f;  // 0–1
    float inputLufsShort       = -100.0f;
    float outputLufsShort      = -100.0f;

    // Per-band activity for multiband products (0 if unused)
    static constexpr int kMaxBands = 6;
    std::array<float, kMaxBands> bandActivity {};
    int activeBandCount = 0;
};

} // namespace vxsuite
```

Add to `ProcessorBase`:
```cpp
virtual MeteringSnapshot getMeteringSnapshot() const noexcept { return {}; }
```

**Adopt in:** Every product processor overrides `getMeteringSnapshot()` by reading from its
DSP objects. Products currently with nothing exposed: Proximity, ProximityClassic, ToneRefine,
SpeechClarity, Clarity, Cleanup.

---

## Phase 2 — DSP Bug Fixes (high-priority correctness)

These are specific DSP source changes. Each is self-contained.

---

### 2.1  DenoiserDsp: SR-adaptive FFT size

**File:** `Source/vxstudio/products/denoiser/dsp/VxDenoiserDsp.h` / `.cpp`

**Problem:** `kFftOrder = 10` (1024 bins) is hardcoded. At 88.2/96 kHz each bin is 93.75 Hz
wide — bass frequency resolution halves. Bark-scale layout and ERB kernels adapt numerically
but the noise estimator loses low-end precision.

**Fix:** Mirror the pattern already used in `SpectralProcessor` (VxDeverb):

```cpp
// In prepare():
const float kTargetWindowMs = 21.3f;
const int order = juce::nextPowerOfTwo(
    static_cast<int>(static_cast<float>(sr) * kTargetWindowMs / 1000.0f));
// fftSize = 1 << order  →  1024 @ 44.1/48k,  2048 @ 88.2/96k
```

Remove `static constexpr int kFftOrder = 10` and `kFftSize`. Make them runtime members.
Resize all `std::vector` state in `prepare()` from `kFftSize` to the computed `fftSize`.
Keep `kBins`, `kHop` as derived runtime values.

**Impact:** VxDenoiser + VxRepair (embeds denoiser DSP). Test both after this change.

---

### 2.2  RebalanceDsp: SR-adaptive FFT size

**File:** `Source/vxstudio/products/rebalance/dsp/VxRebalanceDsp.h` / `.cpp`

**Problem:** Same fixed `kFftOrder = 10`. Source-separation band profiles have `lo/hi` in Hz;
at high SR those map to fewer bins, collapsing sources.

**Fix:** Same adaptive order calculation as 2.1. The `kDebugBins = 96` can remain a cap.
All spectral state vectors must be resized from `kFftSize → fftSize` in `prepare()`.

---

### 2.3  finish::Dsp latency reporting

**File:** `Source/vxstudio/framework/VxStudioFinishDsp.cpp`

**Problem:** If `processLimiter()` uses a lookahead delay buffer, latency is unreported.

**Fix:** Audit `processLimiter()`. If `limitEnv` introduces any sample delay, add
`getLatencySamples()` and have the MixStudio processor call `setReportedLatencySamples()`.
If it is truly lookahead-free (peak detected and applied same block), document explicitly.

---

### 2.4  ProximityDsp / ProximityClassicDsp: smooth model state

**Files:** `VxProximityDsp.cpp`, `VxProximityClassicDsp.cpp`

**Problem:** `SmoothedModelState::primed` snaps on first call but does not ramp on
subsequent parameter changes. An automation move from `closerAmount=0` to `closerAmount=1`
applies a jump in biquad coefficients between two consecutive `processInPlace()` calls.

**Fix:** Embed one `ParamSmoother` per parameter (`closerAmount`, `airAmount`, `mudAmount`)
in each DSP. In `processInPlace()`:
1. Call `smoother.setTarget(closerAmount)` before the sample loop.
2. In the sample loop, call `smoother.getNext()` and recompute coefficients only when the
   smoothed value changes by more than a small epsilon (to avoid per-sample coefficient
   recalculation — use a per-block interpolation instead).

A simpler acceptable approach: smooth the three scalars at block rate using the existing
`BlockSmoothedControl`, and apply the smoothed values to the coefficient calculator. This
gives ~5ms granularity which is sufficient for proximity automation (it is not a filter
with fast attack requirements).

---

## Phase 3 — Delta Monitoring (listen to what's removed)

**Problem:** Infrastructure exists (`LatencyAlignedListenBuffer`, `ProcessCoordinator::renderRemovedDelta`,
`renderListenOutput` virtual on `ProcessorBase`) but most products don't wire it up.

**Fix:** For each product that performs subtraction-style processing, add a `listenParamId` to
their `ProductIdentity` and implement `renderListenOutput()` to call
`processCoordinator.renderRemovedDelta(outputBuffer)`.

**Products to wire:**
| Product | What "removed" means |
|---------|----------------------|
| VxDenoiser | Removed noise floor |
| VxDeverb | Removed reverb tail |
| VxRepair | Removed artefacts per tool |
| VxSpeechClarity | Removed breaths / sibilance / plosives |
| VxToneRefine | Removed mud / harshness |
| VxSubtract | Already has subtract mode — trivial |

**Framework change needed:** None. The machinery is there. This is purely product wiring.

---

## Phase 4 — Metering Per-Product Adoption

Wire `getMeteringSnapshot()` (Phase 1.3) into each processor.

**Priority order:**
1. `VxProximity` — expose shelf gain amounts (already in `analysisLow/Mid/Air` fields)
2. `VxSpeechClarity` — deEss, deBreath, dePlosive activity (not currently in DSP; add `getActivity()` to each)
3. `VxToneRefine` — mudActivity, harshnessActivity (already computed as `lastReductionDb`)
4. `VxClarityDsp` — band gain activity (in `bandGainDb` array already)
5. `VxCleanup` — already has getDeMudActivity() etc. — just pipe to snapshot

Add LUFS tracking to `ProcessorBase` directly (input + output, short-term):
- Add a shared `juce::dsp::BallisticsFilter` or simple ITU-R BS.1770 K-weighted estimator
  to `ProcessorBase`. Run it on the pre-process and post-process buffers.
- Populate `inputLufsShort` / `outputLufsShort` in the base class so every product gets it
  for free with no per-product work.

---

## Phase 5 — Oversampling Wrapper (framework + finish DSP)

**Problem:** No oversampling anywhere. Highest priority target: `finish::Dsp` limiter.

### 5.1  VxStudioOversamplingWrapper.h

```cpp
// Source/vxstudio/framework/VxStudioOversamplingWrapper.h
namespace vxsuite {

// Wraps a lambda/callable DSP block with optional JUCE oversampling.
// Factor 1 = bypass (no overhead), 2/4/8 = real oversampling.
// Linear phase mode for mastering; minimum phase for low-latency.
class OversamplingWrapper {
public:
    enum class Mode { minPhase, linearPhase };

    void prepare(double sampleRate, int maxBlockSize, int numChannels,
                 int factor = 1, Mode mode = Mode::minPhase);
    void reset();
    int addedLatencySamples() const noexcept;

    // Upsample buffer, call fn(upsampled buffer), downsample back.
    // fn signature: void(juce::AudioBuffer<float>&)
    template <typename Fn>
    void process(juce::AudioBuffer<float>& buffer, Fn&& fn);

private:
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    int factor_ = 1;
};

} // namespace vxsuite
```

### 5.2  Wire to finish::Dsp

Wrap the limiter stage only (not the opto compressor) at 2x by default.
MixStudio processor reports the added latency from `wrapper.addedLatencySamples()`.

---

## Phase 6 — Safe Bypass Tail Drain

**Problem:** Bypassing an STFT DSP mid-stream leaves `latencySamples` of wet audio in the
OLA accumulator that will never drain. Host dry-bypass path plays non-latency-compensated dry
while the wet tail is stuck.

**Fix — framework:** Add `drainToBuffer(juce::AudioBuffer<float>& out, int numSamples)` to
`AudioProcessStage` interface (default no-op). STFT stages implement it by filling the OLA
accumulator with zeros until the tail is flushed.

**Fix — processor layer:** Override `processBlockBypassed()` in `ProcessorBase` subclasses
that use latent stages to call `stage.drain()` for up to `latencySamples` blocks after bypass
is activated, blending the drain output with host dry via the existing crossfade helper.

---

## Execution Order

| Priority | Phase | Effort | Status | Affects |
|----------|-------|--------|--------|---------|
| P0 | 2.1 DenoiserDsp SR-adaptive FFT | Medium | ✅ DONE | VxDenoiser, VxRepair |
| P0 | 2.2 RebalanceDsp SR-adaptive FFT | Medium | ✅ DONE | VxRebalance |
| P1 | 1.2 SilenceGuard framework + wiring | Small | ✅ DONE | Denoiser, Deverb, Rebalance, DeepFilter |
| P1 | 2.4 Proximity param smoothing | Small | ✅ DONE (SR-normalised α) | VxProximity, VxProximityClassic |
| P1 | 2.3 finish::Dsp latency audit | Small | ✅ DONE (confirmed zero-latency, doc'd) | VxMixStudio |
| P2 | 1.1 ParamSmoother framework | Small | ✅ DONE | VxStudioParamSmoother.h |
| P2 | 3.x Delta monitoring wiring | Small × 6 | ✅ DONE (Denoiser renderListenOutput added; Deverb/ToneRefine/SpeechClarity/Subtract already wired) | |
| P2 | 1.3 MeteringSnapshot + virtual in base | Medium | ✅ DONE | VxStudioMeteringSnapshot.h + ProcessorBase |
| P3 | 4.x Per-product metering adoption | Small × 5 | ✅ DONE | Proximity, SpeechClarity, ToneRefine, Clarity, Cleanup, Finish |
| P3 | 5.1 OversamplingWrapper + finish DSP | Large | ✅ DONE (2× min-phase on Finish limiter) | VxMixStudio |
| P4 | 6.x Safe bypass tail drain | Medium | ✅ DONE (drain() virtual + DenoiserDsp impl) | All latent DSPs |

---

## Files to create (framework)
- `Source/vxstudio/framework/VxStudioParamSmoother.h`
- `Source/vxstudio/framework/VxStudioSilenceGuard.h`
- `Source/vxstudio/framework/VxStudioMeteringSnapshot.h`
- `Source/vxstudio/framework/VxStudioOversamplingWrapper.h`

## Files to modify (framework)
- `VxStudioAudioProcessStage.h` — add `drain()` virtual (Phase 6)
- `VxStudioProcessorBase.h/.cpp` — add `getMeteringSnapshot()` virtual + LUFS tracking (Phase 4)

## Files to modify (DSP, each isolated)
- `denoiser/dsp/VxDenoiserDsp.h/.cpp` — runtime fftSize (Phase 2.1)
- `rebalance/dsp/VxRebalanceDsp.h/.cpp` — runtime fftSize (Phase 2.2)
- `framework/VxStudioFinishDsp.h/.cpp` — latency audit (Phase 2.3)
- `proximity/dsp/VxProximityDsp.h/.cpp` — param smoothing (Phase 2.4)
- `proximityClassic/dsp/VxProximityClassicDsp.h/.cpp` — param smoothing (Phase 2.4)

## Files to modify (processors, wiring only)
- `VxDenoiserProcessor.cpp` — silence guard + delta monitoring
- `VxDeverbProcessor.cpp` — silence guard + delta monitoring
- `VxRebalanceProcessor.cpp` — silence guard
- `VxDeepFilterNetProcessor.cpp` — silence guard
- `VxSpeechClarityProcessor.cpp` — delta monitoring + metering
- `VxToneRefineProcessor.cpp` — delta monitoring + metering
- `VxProximityProcessor.cpp` — metering
- `VxProximityClassicProcessor.cpp` — metering
