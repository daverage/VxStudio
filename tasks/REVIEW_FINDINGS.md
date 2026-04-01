# VxStudio DSP & Framework Review - Comprehensive Findings

**Review Date:** 2026-04-01
**Scope:** 12 products + framework code (Source/vxsuite/)
**Focus:** Realtime safety, memory efficiency, CPU efficiency, DRY principles, framework compliance

---

## Executive Summary

**Framework Quality:** ⭐⭐⭐⭐⭐ Excellent design. Clean processor/editor separation, realtime-safe patterns, reusable components.

**Product Compliance:** ⭐⭐⭐⭐☆ Strong. 91.7% correct ProcessorBase adoption. Framework rules well-followed.

**Code Efficiency:** ⭐⭐⭐☆☆ Good core DSP, but **1 critical realtime violation** + **1 significant code duplication pattern** + minor inefficiencies.

**Critical Issues:** 1 (denoiser buffer allocation in processBlock)
**Major Findings:** 4 (code duplication, inconsistent patterns, minor wastage)
**Minor Findings:** 6 (optimization opportunities, consistency improvements)

---

## CRITICAL ISSUES

### 1. ⛔ Denoiser: Buffer Reallocation in Audio Thread (Realtime Violation)

**Location:** `Source/vxsuite/products/denoiser/VxDenoiserProcessor.cpp:175-176`

```cpp
void VXDenoiserAudioProcessor::processProduct(juce::AudioBuffer<float>& buffer, ...) {
    // ...
    if (stereo) {
        leftScratch.setSize(1, numSamples, false, false, true);  // ❌ Can allocate!
        rightScratch.setSize(1, numSamples, false, false, true);  // ❌ Can allocate!
        // ...
    }
}
```

**Problem:**
- `AudioBuffer::setSize()` can trigger heap allocation if the requested size exceeds current capacity
- Called in `processProduct()` (realtime-critical path) every block
- Violates VX Suite Realtime Rule: "no heap allocation in `processBlock`"
- Blocks audio thread → potential XRuns, clicks, glitches

**Root Cause:**
Buffers are pre-allocated in `prepareSuite` with `samplesPerBlock` size, but `processProduct` can request different `numSamples` (e.g., partial blocks, dynamic block sizes).

**Fix (Priority: IMMEDIATE):**

Pre-allocate to maximum possible block size in `prepareSuite`:
```cpp
void VXDenoiserAudioProcessor::prepareSuite(const double sampleRate, const int samplesPerBlock) {
    // Pre-allocate for max typical block size (typically samplesPerBlock + headroom)
    const int maxBlockSize = std::max(samplesPerBlock, 4096);  // safety headroom
    leftScratch.setSize(1, maxBlockSize, false, false, true);
    rightScratch.setSize(1, maxBlockSize, false, false, true);
    // ... rest of setup
}

void VXDenoiserAudioProcessor::processProduct(...) {
    // No setSize() call — just use what's pre-allocated
    if (stereo) {
        leftScratch.copyFrom(0, 0, buffer, 0, 0, numSamples);
        rightScratch.copyFrom(0, 0, buffer, 1, 0, numSamples);
        denoiserDspLeft.processInPlace(leftScratch, ...);   // will only touch numSamples
        // ...
    }
}
```

**Impact:** High. Violates core realtime contract.

---

## MAJOR FINDINGS

### 2. Code Duplication: Control Smoothing Pattern (Affects 11/12 Products)

**Pattern found in:** Tone, Cleanup, Denoiser, Deverb, Finish, OptoComp, Subtract, Proximity, Leveler, Polish, Rebalance

Every product repeats the same boilerplate:

```cpp
// Header
bool controlsPrimed = false;
float smoothedPrimary = 0.5f;
float smoothedSecondary = 0.5f;

// In resetSuite()
void resetSuite() {
    smoothedPrimary = 0.5f;
    smoothedSecondary = 0.5f;
    controlsPrimed = false;
}

// In processProduct()
void processProduct(AudioBuffer<float>& buffer, MidiBuffer&) {
    const float primaryTarget = readNormalized(parameters, primaryParamId, 0.5f);
    const float secondaryTarget = readNormalized(parameters, secondaryParamId, 0.5f);

    if (!controlsPrimed) {
        smoothedPrimary = primaryTarget;
        smoothedSecondary = secondaryTarget;
        controlsPrimed = true;
    } else {
        smoothedPrimary = smoothBlockValue(smoothedPrimary, primaryTarget,
                                           currentSampleRateHz, numSamples, 0.060f);
        smoothedSecondary = smoothBlockValue(smoothedSecondary, secondaryTarget,
                                             currentSampleRateHz, numSamples, 0.080f);
    }
    // ... use smoothedPrimary, smoothedSecondary
}
```

**Why it matters:**
- Identical across 11 products = 11 copies of 20 lines = 220 lines of dead duplicate code
- Error-prone: if smoothing logic needs a fix, must touch 11 files
- No DRY principle: each product invents its own local state names/flags
- Wastes maintenance surface area

**Solution (Priority: HIGH):**

Extract a reusable helper class in framework:

```cpp
// VxSuiteBlockSmoothedControl.h
namespace vxsuite {

class BlockSmoothedControlPair {
public:
    struct Values {
        float primary = 0.5f;
        float secondary = 0.5f;
    };

    void reset(float primaryDefault = 0.5f, float secondaryDefault = 0.5f) noexcept {
        primary = primaryDefault;
        secondary = secondaryDefault;
        primed = false;
    }

    Values process(float primaryTarget, float secondaryTarget,
                   double sampleRate, int numSamples,
                   float primaryTimeSeconds, float secondaryTimeSeconds) noexcept {
        if (!primed) {
            primary = primaryTarget;
            secondary = secondaryTarget;
            primed = true;
        } else {
            primary = smoothBlockValue(primary, primaryTarget,
                                      sampleRate, numSamples, primaryTimeSeconds);
            secondary = smoothBlockValue(secondary, secondaryTarget,
                                        sampleRate, numSamples, secondaryTimeSeconds);
        }
        return { primary, secondary };
    }

private:
    float primary = 0.5f;
    float secondary = 0.5f;
    bool primed = false;
};

} // namespace vxsuite
```

Products then simplify to:

```cpp
// In processor header
vxsuite::BlockSmoothedControlPair controls;

// In resetSuite()
controls.reset(0.5f, 0.5f);

// In processProduct()
const auto [smoothedBass, smoothedTreble] = controls.process(
    bassTarget, trebleTarget, currentSampleRateHz, numSamples, 0.060f, 0.060f);
```

**Impact:** Eliminates 220 lines of duplication, improves maintainability, reduces bug surface.

---

### 3. Framework Boundary: Analyser Product Doesn't Inherit ProcessorBase

**Location:** `Source/vxsuite/products/analyser/VXStudioAnalyserProcessor.h`

**Current implementation:**
```cpp
class VXStudioAnalyserAudioProcessor final : public juce::AudioProcessor {
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    // ... reimplements everything manually
};
```

**Problem:**
- Analyser is a diagnostic tool inside the suite but doesn't use ProcessorBase
- Rebuilds parameter layout, editor, telemetry path manually
- Misses framework safety nets (OutputTrimmer, latency alignment, etc.)
- Inconsistent with suite standards

**Note:** This is acceptable if Analyser has fundamentally different needs (e.g., needs raw access to audio without DSP), but if it's just because it predates ProcessorBase adoption, it should migrate.

**Recommendation:** Review Analyser's special requirements. If none are fatal, migrate to ProcessorBase.

**Impact:** Medium. Inconsistency in maintenance surface, potential for subtle bugs.

---

### 4. Rebalance: Custom Editor Adds Debug Panel (Framework Deviation)

**Location:** `Source/vxsuite/products/rebalance/VxRebalanceEditor.h`

```cpp
class VXRebalanceEditor final : public vxsuite::EditorBase {
    // Adds custom debug panel on top of EditorBase
};
```

**Observation:**
- Rebalance extends EditorBase with a diagnostic panel (understandable for ML-based product)
- Good pattern: uses framework base + extends where needed
- ✅ Correctly layered

**Status:** No action needed. This is good framework extension practice.

---

## MAJOR PATTERNS & INEFFICIENCIES

### 5. Duplicate RMS Calculation Functions

**Found in:**
- `denoiser/VxDenoiserProcessor.cpp`: `computeBufferRms()` + `computeChannelRms()` (lines 16-48)
- Other products likely have their own RMS helpers

**Pattern:**
```cpp
// Denoiser
float computeBufferRms(const juce::AudioBuffer<float>& buffer) {
    double sumSquares = 0.0;
    for (int ch = 0; ch < channels; ++ch)
        for (int i = 0; i < samples; ++i)
            sumSquares += buffer[ch][i] * buffer[ch][i];
    return std::sqrt(sumSquares / count);
}
```

**Problem:**
- Audio analysis primitives (RMS, peak, spectral centroid, etc.) should be shared
- If multiple products compute RMS, they likely have identical logic
- Framework already provides `VxSuiteSignalQuality.h` — could extend with analysis helpers

**Recommendation:** Create `VxSuiteLightAnalysis.h` with efficient inline helpers:
- `rms(buffer)`, `rmsChannel(buffer, ch)`
- `peak(buffer)`, `peakChannel(buffer, ch)`
- `spectralCentroid(fftBins)` (if multiple products need it)

Keep these simple, zero-allocation, per-block suitable.

**Impact:** Low-medium. Reduces code duplication, improves consistency.

---

### 6. Sample Rate Caching Inconsistency

**Pattern:**
Every processor caches `currentSampleRateHz`:
```cpp
double currentSampleRateHz = 48000.0;  // All 12 products
```

Only **7 of 12** products also cache `currentBlockSize`:
```cpp
int currentBlockSize;  // Tone, Cleanup, Denoiser, Deverb, Finish, Subtract, ...
```

**Issue:**
- If a product needs block size for windowing (FFT, spectral analysis), it must cache it
- Inconsistency makes it harder to know which products have prepared for variable block sizes
- Frame-based DSP (spectral) often needs to know if block size changed to rebuild windows

**Recommendation:**
- Add clear comment in ProcessorBase header or framework doc: "Products should cache sample rate always; cache block size only if used for windowing or state sizing"
- Consider adding optional `hasVariableBlockSize` flag to ProductIdentity to signal which products need it
- Verify that caching is consistently done during `prepareSuite`, not on-the-fly

**Impact:** Low. Mostly a consistency/clarity issue.

---

## POSITIVE FINDINGS

### ✅ Strong Framework Compliance

- **91.7% ProcessorBase adoption** (11/12 products) — Excellent
- **83% Mode parameter adoption** (10/12 products) — Correct: Leveler, Rebalance, Analyser don't need mode
- **75% Listen adoption** (9/12 products) — Correct: not every effect benefits from delta audition
- All products use `readNormalized()` and `readMode()` helpers ✅
- All products properly report latency via `setReportedLatencySamples()` ✅
- All products use `ScopedNoDenormals` in processProduct ✅

### ✅ Realtime Safety (Except Denoiser)

- No dynamic allocations in processBlock (except denoiser)
- All DSP state allocated in prepareSuite / resetSuite
- No mutex locks or blocking I/O on audio thread
- Clean per-channel state management (vector<State> pattern)
- Good use of per-block coefficient computation (avoids per-sample math)

### ✅ DSP Encapsulation

- Products properly implement `AudioProcessStage` abstract interface
- Finish DSP reused by OptoComp and Cleanup (good code sharing)
- Mode-specific behavior cleanly mapped via ModePolicy
- Signal quality snapshot integrated without blocking

### ✅ Parameter Safety

- All parameters [0, 1] with neutral 0.5 — consistent UI semantics
- Block smoothing with primer flag — prevents clicks on init
- Parameter IDs stable and semantic

---

## MINOR FINDINGS & OPTIMIZATIONS

### 7. Unnecessary Macro/Const Duplication

Every product re-declares identical parameter ID constants:
```cpp
namespace {
    constexpr std::string_view kProductName  = "Tone";
    constexpr std::string_view kShortTag     = "TNE";
    constexpr std::string_view kBassParam    = "bass";
    constexpr std::string_view kTrebleParam  = "treble";
    constexpr std::string_view kModeParam    = "mode";
    constexpr std::string_view kListenParam  = "listen";
}
```

These are needed, but the naming pattern is inconsistent. Some use `kParam`, some `kId`. Minor but fixable.

### 8. Help Content Generation

Each product hardcodes help HTML. Consider centralizing help generation from a simple markdown source (if not already done via VxSuiteHelpContent.h).

### 9. Activity Light Count Inconsistency

Some products report activity lights, some don't:
- Cleanup: 4 lights ✅
- Finish: 4 lights ✅
- OptoComp: Has low/high shelf activity + 4 main lights
- Others: Return 0 (no lights)

Consider whether all corrective products should expose activity lights for UI feedback.

### 10. OutputTrimmer Applied Twice?

ProcessorBase applies an automatic OutputTrimmer. Some products (e.g., Denoiser, Finish) also have local OutputTrimmer:

```cpp
// ProcessorBase does this:
outputSafetyTrimmer.process(buffer, ...);  // framework safety net

// Denoiser also does:
outputTrimmer.process(buffer, ...);  // local trimmer
```

**Question:** Is this intentional (per-product control + final safety) or redundant? If intentional, document it. If redundant, remove product-local ones.

**Impact:** Minor. Verify design intent.

---

## SUMMARY TABLE

| Finding | Severity | Type | Effort | Impact |
|---------|----------|------|--------|--------|
| Denoiser buffer realloc in processBlock | 🔴 CRITICAL | Realtime safety | 30 min | High |
| Control smoothing duplication (11 products) | 🟠 MAJOR | Code quality | 2 hours | High |
| Analyser not using ProcessorBase | 🟠 MAJOR | Consistency | 1 hour | Medium |
| Duplicate RMS helpers across products | 🟡 MEDIUM | Code quality | 1 hour | Medium |
| Sample rate/block size caching inconsistency | 🟡 MEDIUM | Clarity | 1 hour | Low |
| Parameter ID naming inconsistency | 🟢 MINOR | Style | 30 min | Low |
| Activity light coverage | 🟢 MINOR | Consistency | varies | Low |
| OutputTrimmer double-application clarification | 🟢 MINOR | Clarity | 15 min | Low |

---

## RECOMMENDATIONS (Prioritized)

### Tier 1: Realtime Safety (Do immediately)
1. **Fix denoiser buffer allocation** — Pre-allocate scratch buffers in prepareSuite
2. Run full realtime safety audit on changed code (valgrind --tool=drd on JACK/CoreAudio)

### Tier 2: Code Quality (Next sprint)
1. **Extract BlockSmoothedControlPair helper** — Reduces 220 lines of duplication
2. **Create VxSuiteLightAnalysis.h** — Centralize RMS and peak analysis
3. **Migrate Analyser to ProcessorBase** (if no fundamental blocker)

### Tier 3: Consistency (Polish)
1. Standardize parameter ID constant naming
2. Document OutputTrimmer layering intent
3. Add framework guidance on block size caching expectations

### Tier 4: Opportunities (Future)
1. Consider activity light coverage for all corrective products
2. Investigate centralizing help content generation
3. Profile CPU cost of per-block analysis (voice analysis, signal quality snapshots) across chain

---

## Conclusion

**VxStudio framework and products are well-designed, with strong realtime discipline and clean architecture.**

**One critical realtime violation must be fixed immediately (denoiser).** All other issues are code quality / consistency improvements that should follow in the normal development cycle.

**Framework adoption is excellent.** The 11→12 ProcessorBase inheritance and correct use of shared utilities (ModePolicy, Listen, Telemetry) shows good architectural discipline across the suite.

**Next steps:** Fix the denoiser, extract the smoothing helper, then integrate both as 1-2 point story.
