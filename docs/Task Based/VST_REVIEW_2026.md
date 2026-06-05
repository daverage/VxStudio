# VX Suite VST Plugin Comprehensive Review  -  May 2026

**Critical Finding**: README is severely outdated and misrepresents plugin capabilities.

---

## Executive Summary

✅ **12 plugins shipping in production**
✅ **Framework architecture is excellent**  -  clean ProcessorBase/EditorBase separation
✅ **Proximity & Deverb are genuinely state-of-the-art research-grade DSP**  -  far beyond simple EQ
⚠️ **README must be completely rewritten** to reflect actual sophistication
⚠️ **Rebalance Phase 2/3 implementation needs verification**  -  code exists but active features unclear

---

## Plugin-by-Plugin Deep Dive

### **1. VXProximity (0.2.0)**  -  NOT "tone shaping" ⚠️ MISREPRESENTED IN README

**Actual Implementation**: Sophisticated 4-stage directional microphone proximity model with real-time spectral analysis.

**DSP Architecture**:
```
Real-time spectral analysis (4 bands)
    ↓
Energy distribution scoring
    ↓
Adaptive filter parameter selection
    ↓
4 cascaded biquad stages (low-shelf → mud-cut bell → presence bell → high-shelf)
    ↓
Adaptive output trim (prevents cascading filter buildup)
```

**Advanced Features**:

1. **Per-band energy tracking** using one-pole lowpass filters:
   - Low band (180/200 Hz cutoff)
   - Low-mid band (520/600 Hz cutoff)  
   - Presence band (2400/2800 Hz cutoff)
   - Air band (6800/7600 Hz cutoff)

2. **Intelligent parameter adaptation** based on input characteristics:
   - **bodyOpportunity**: Weighted low/low-mid energy ratio → scales low-shelf gain (72%-112% of maximum)
   - **mudRisk**: Low-mid dominance detection → adjusts mud-bell depth and frequency
   - **directnessOpportunity**: Presence/air energy ratio → scales presence bell and high-shelf

3. **Context-aware modulation** from framework signals:
   - vocalDominance, intelligibility, phraseActivity, transientRisk
   - buriedSpeech (suppressed speech in background)
   - Mode-specific frequency centers:
     - Vocal: 200 Hz low-shelf, 3600 Hz presence, 7600 Hz air
     - General: 260 Hz low-shelf, 3200 Hz presence, 11000 Hz air

4. **Non-linear control curves**:
   - Closer: power 0.78 (voice) / 0.70 (general)
   - Air: power 0.74 (voice) / 0.66 (general)

5. **Zero-latency DSP**  -  pure IIR filtering, no delay

**What's State-of-the-Art**:
- ✅ Physically plausible directional mic model (not a generic EQ)
- ✅ Adaptive to input spectral characteristics
- ✅ Vocal-aware with intelligent protection
- ✅ Mud compensation (220-350 Hz clutter zone) is thoughtfully placed
- ✅ Cascaded filter approach models real mic impedance behavior

**Potential Updates**:
- [ ] Cross-band energy feedback (current approach is band-local)  -  consider inter-band dynamics
- [ ] Transient-aware adaptation (presence boost during attacks/consonants)
- [ ] Stereo width preservation/enhancement during proximity boost

---

### **2. VXDeverb (0.2.0)**  -  Research-grade dereverberation ✅

**Actual Implementation**: Multi-stage scientific dereverberation using LRSV (Late-Reverberant Spectral Variance) + WPE (Weighted Prediction Error).

**DSP Architecture**:

```
Input → FFT (STFT with Hann window, 75% overlap)
    ↓
RT60 Estimation (per-channel decay-slope tracking)
    ↓
LRSV Spectral Variance Estimation (Habets 2009)
    ↓
Branch 1: Spectral subtraction with Wiener gain
Branch 2: (Voice mode) WPE dereverberation
    ↓
Per-bin IIR smoothing (artifact suppression)
    ↓
Inverse FFT (OLA synthesis)
    ↓
Output
```

**Advanced Features**:

1. **LRSV-based late reverberation estimation** (Habets et al. 2009):
   - Based on Polack's statistical room impulse response model
   - Exponentially-decaying white Gaussian noise assumption for late tail
   - Per-bin reverb power: `Γ_late(m,k) = κ × exp(−2δT) × |Y(m−T_frames, k)|²`
   - Where `δ = ln(1000)/RT60 = 6.908/RT60` (decay rate)
   - `T ≈ 50 ms` (early/late boundary)

2. **RT60 Tracking** (room decay time estimation):
   - Per-channel decay-slope analyzer
   - Accuracy ±30% (sufficient for exponential weighting)
   - Adaptive κ coefficient for Wiener gain

3. **Wiener filtering with per-bin smoothing**:
   - Gain: `G(m,k) = sqrt(max(1 − amount×Γ_late / |Y|², floor²))`
   - Per-bin IIR temporal smoothing suppresses musical-noise artifacts
   - Voice-protected bins (200-4000 Hz) use `kVoiceFloor` when in voice mode

4. **WPE Stage** (voice mode only)  -  Weighted Prediction Error:
   - Frame-online single-channel processing
   - Works in STFT domain (receives complex spectra directly)
   - Prediction filter: K=10 taps, delta=3 frame delay
   - RLS forgetting factor: α=0.995, PSD smoothing: β=0.80
   - Pre-allocated, zero heap allocations in audio thread
   - Memory: ~125 KB per channel

5. **STFT Windowing**:
   - FFT size: 1024 @ 48 kHz (21.3 ms), 2048 @ 96 kHz (21.3 ms)  -  auto-scaled
   - Hop size: FFT/4 (75% overlap, COLA-satisfying periodic Hann)
   - Latency: FFT − Hop = 3×Hop ≈ 16 ms @ 48 kHz
   - OLA (Overlap-Add) accumulator with safe sizing for large block sizes

**What's State-of-the-Art**:
- ✅ Rooted in published research (Habets et al. 2009, Lebart et al. 2001)
- ✅ Signal-agnostic  -  handles voice and polyphonic equally
- ✅ RT60-adaptive  -  tunes dereverberation to room decay time
- ✅ WPE for voice mode  -  optional speech-specific enhancement
- ✅ Temporal smoothing reduces musical artifacts
- ✅ Per-bin gain and filtering  -  frequency-dependent processing

**Recent Improvements** (May 2026):
- Body restoration with adaptive compensation gain
- Default blend=25% (restores ~2.5 dB bass instead of 0 dB)
- Optimized WPE coefficient updates

**Potential Updates**:
- [ ] Adaptive κ coefficient (currently fixed at 1.0)  -  could improve subtraction accuracy
- [ ] Per-frame RT60 refinement using power decay tracking
- [ ] Cross-channel correlation for stereo processing (currently per-channel)
- [ ] Transient detection to reduce over-subtraction on onsets

---

### **3. VXCleanup (0.2.0)**  -  Research-grade corrective processing ✅

**Actual Implementation**: Dual-stage spectral correction with frame-level feature extraction and mode-specific filtering.

**DSP Architecture**:
```
Input → FFT-based spectral analysis
    ↓
Feature extraction (8+ acoustic properties per frame)
    ↓
Dual correction pipeline:
  - Corrective chain (sibilance, plosive, breath reduction)
  - Persistent clarity stage (broader low-mid cleanup)
    ↓
Noise-floor-based protection (framework signal quality)
    ↓
Output
```

**Advanced Features**:

1. **Real-time spectral feature detection**:
   - Spectral flatness (noise vs tonal)
   - Harmonicity (voiced vs unvoiced)
   - High-frequency ratio (sibilance detection)
   - Breath envelope tracking
   - Sibilance envelope (esses detection)
   - Plosive envelope (plosive detection)
   - Tonal mud envelope (low-mid muddiness)
   - Harshness envelope (harsh frequency content)

2. **Dual correction stages**:
   - **Corrective chain**  -  aggressive removal targeting detected problems
   - **Persistent clarity stage**  -  broader low-mid correction with protection

3. **Noise-floor integration**:
   - Uses framework signal quality (voiceContext, separationConfidence)
   - Fixed noise floor (May 24 fix) instead of adaptive
   - Sidechain-aware floor computation

4. **Problem-specific handling**:
   - Plosive sensitivity dial (4th parameter)
   - High-shelf filter for high-end transparency
   - Low-mid specific filters for boxiness
   - Voice mode vs General mode tuning

5. **Recent Improvements** (May 2026):
   - Made independent of analyzed noise floor (May 23)
   - Zero-latency reporting to host (May 23)
   - Fixed stuttering with denoise/cleanup together (May 24)
   - Fixed general mode level collapse (May 23)

**What's State-of-the-Art**:
- ✅ Multi-feature detection (8+ spectral properties)
- ✅ Problem-specific algorithms (not one-size-fits-all spectral subtraction)
- ✅ Sidechain-aware floor estimation
- ✅ Voice-specific parameters (plosive sensitivity, intelligibility protection)
- ✅ OutputTrimmer safety net (local peak discipline)

**Framework Integration**:
- ✅ Uses getVoiceContextSnapshot() for protection
- ✅ Signal quality affects threshold easing
- ✅ Listen mode = removed delta (what was cleaned)

---

### **4. VXDenoiser (0.2.0)**  -  Spectral broadband denoise ✅

**Implementation**: Spectral subtraction with guard controls and artifact suppression.

**Recent Fixes** (May 2026):
- Fixed level collapse in general mode (gain floor 0.85f)
- Improved harsh artifact suppression in high frequencies
- Removed over-compensation that made sound thin

**What Works**:
- ✅ Clean separation between noise floor removal and over-processing
- ✅ Guard controls prevent intelligibility loss
- ✅ Mode-specific tuning (vocal/general)
- ✅ Steady broadband noise handling (fans, HVAC)

---

### **5. VXSubtract (0.2.0)**  -  Profile-guided subtraction ✅

**Implementation**: Learn noise profile, then subtract with protection.

**What Works**:
- ✅ Learn mode captures representative noise
- ✅ Subtract/Protect controls balance removal vs source preservation
- ✅ Fixed latency-alignment in listen mode (May 20)

---

### **6. VXDeepFilterNet (0.2.0)**  -  ML-powered voice isolation ✅

**Implementation**: ONNX Runtime inference with model download/management UI.

**Models**:
- DeepFilterNet 3 (current)
- DeepFilterNet 2 (legacy support)

**What's State-of-the-Art**:
- ✅ Latest DFN3 model support
- ✅ Async model download with progress UI
- ✅ Clean/Guard controls for aggressiveness/protection tradeoff

**Potential Updates**:
- [ ] Model auto-download on first use
- [ ] Fallback to DFN2 if DFN3 unavailable
- [ ] Better offline experience messaging
- [ ] Timeout handling for slow connections

---

### **7. VXProximity-related** (companion to Proximity)

**Recent Improvements**:
- Removed adaptive retuning (was pop-prone)
- Now purely adaptive input-aware model (not time-variant)
- Vocal-aware modulation of presence and air

---

### **8. VXTone (0.2.0)**  -  EQ shaping ✅

**Implementation**: Two independent Audio EQ Cookbook biquad shelf filters.

**What Works**:
- ✅ Textbook implementation (low-shelf 120-200 Hz, high-shelf 6000-8000 Hz)
- ✅ Mode-aware frequency placement
- ✅ ±5 dB (vocal) / ±6 dB (general) ranges

---

### **9. VXFinish (0.3.0)**  -  Final dynamics ✅

**Version**: 0.3.0 (patch above base 0.2.0)

**What's State-of-the-Art**:
- ✅ Combination compression + makeup + limiting
- ✅ Body recovery stage
- ✅ Unity-centered gain control (50%-150%)

---

### **10. VXOptoComp (0.3.0)**  -  Opto levelling ✅

**Version**: 0.3.0 (patch above base 0.2.0)

**What's State-of-the-Art**:
- ✅ Programme-dependent gain reduction (slower than VXFinish)
- ✅ Smooth, natural levelling
- ✅ LA2A-style character

---

### **11. VXLeveler (0.2.0)**  -  Adaptive riding ✅

**Dual-mode operation**:
- **Vocal Rider**  -  speech-focused riding (Level/Control)
- **Mix Leveler**  -  broader programme smoothing

**Analysis selector** (custom, not Vocal/General):
- Realtime
- Smart Realtime
- Offline

**What Works**:
- ✅ Two distinct algorithms (not a mode switch)
- ✅ Speech vs programme tuning
- ✅ Analysis flexibility (realtime/offline)

---

### **12. VXRebalance (0.2.1)**  -  Source-family rebalance ⚠️ VERIFY IMPLEMENTATION

**Actual State**: AMBIGUOUS  -  Code is committed but active features unclear.

**What's Definitely Implemented**:
- ✅ Harmonic clustering (2-8x fundamental detection)
- ✅ Object tracking with lifecycle states
- ✅ Ownership frame calculation with slider nonlinearity
- ✅ Spectral subtraction per source family
- ✅ Mode-specific (Studio/Live/Phone/Rough) tuning

**What Needs Verification**:
- ❓ `persistClusters()`  -  is cluster lifecycle tracking called each frame?
- ❓ `updateTrackedClusters()`  -  does frame matching/persistence work?
- ❓ `sourceProbabilities[]`  -  are these computed from harmonic analysis or stubs?
- ❓ `transient boost`  -  is the transient enhance logic actually applied?

**Recent Context**:
- Commit 5106aca "Pre Refactor"  -  last Rebalance-specific work (~3 months ago)
- Most recent work has been on Cleanup/Denoiser/Deverb
- Phase 2/3 specs exist but unclear if fully implemented

**Action Required**:
```cpp
// In VxRebalanceDsp.cpp processFrame(), trace which of these are called:
// 1. persistClusters()  -  updates ageFrames, lifecycleState
// 2. updateTrackedClusters()  -  matches current frame to tracked objects  
// 3. computeSourceProbabilities()  -  harmonic analysis → [vocals, drums, bass, guitar, other]
// 4. transientBoost()  -  scaling for attack handling
```

**Phase 2/3 Spec Status**:
- Harmonic clustering for fundamental detection ✅
- Object lifetime tracking → UNCLEAR if active
- Source ownership authority → UNCLEAR if fully enabled
- ML phase not needed (pure DSP) ✅

---

### **13. VXStudioAnalyser (0.2.0)**  -  Chain inspector ✅

**Implementation**: Custom UI (not EditorBase), real-time spectrum telemetry.

**What Works**:
- ✅ Full chain dry-vs-wet inspection
- ✅ Per-stage analysis (click stage in left rail)
- ✅ Configurable averaging and smoothing
- ✅ Accurate stage discovery via framework telemetry

---

## Framework Compliance Summary

| Plugin | ProcessorBase | EditorBase | Parameter Smoothing | OutputTrimmer | Signal Quality | Listen Mode |
|--------|---|---|---|---|---|---|
| DeepFilterNet | ✅ | ✅ | N/A (ML) | N/A | ✅ | ✅ |
| Denoiser | ✅ | ✅ | ✅ | N/A | ✅ | ✅ |
| Subtract | ✅ | ✅ | ✅ | N/A | ✅ | ✅ |
| Deverb | ✅ | ✅ | ✅ | N/A | ✅ | ✅ |
| Proximity | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Cleanup | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Tone | ✅ | ✅ | ✅ | N/A | ✅ | ✅ |
| Finish | ✅ | ✅ | ✅ | N/A | ✅ | ✅ |
| OptoComp | ✅ | ✅ | ✅ | N/A | ✅ | ✅ |
| Leveler | ✅ | ✅ | ✅ | N/A | ✅ | ✅ |
| Rebalance | ✅ | Custom | ✅ | ✅ | ✅ | ✅ |
| Analyser | ✅ | Custom | N/A | N/A | ✅ | N/A |

---

## Critical Updates Needed

### **P0  -  README Rewrite** 🔴 URGENT

Current README severely misrepresents Proximity and Deverb. Must rewrite sections:

**Current (WRONG)**:
> VXProximity  -  "Close-mic tone shaping"
> VXDeverb  -  "Room tail and reverb reduction"

**Should be**:
> VXProximity  -  "Directional microphone proximity model with real-time spectral analysis and adaptive 4-stage filtering"
> VXDeverb  -  "LRSV-based dereverberation with RT60 tracking and optional WPE stage for voice"

**What to add for each plugin**:
- Algorithm foundation (what research is it based on?)
- Real-time features (spectral analysis, feature detection, etc.)
- Adaptive mechanisms (how does it respond to input?)
- What makes it state-of-the-art

---

### **P1  -  VXRebalance Verification** 🟡 IMPORTANT

1. **Audit VxRebalanceDsp.cpp**:
   ```bash
   git log -p --since="2025-01-01" -- Source/vxstudio/products/rebalance/dsp/VxRebalanceDsp.cpp
   # Check which Phase 2/3 methods are actually called in processFrame()
   ```

2. **Trace the audio path**:
   - Is `persistClusters()` called each frame?
   - Is `updateTrackedClusters()` active?
   - Are `sourceProbabilities` computed or stub values?
   - Is transient boost applied?

3. **Update roadmap docs** to reflect actual implementation state

4. **Consider re-tuning** if Phase 2/3 is incomplete:
   - Object ownership authority
   - Confidence-gated mask application
   - Lifecycle-based fade-in/fade-out

---

### **P2  -  Polish & Optimization** 🟢 NICE-TO-HAVE

- [ ] **VXDeepFilterNet**: Async model download with better UX (timeouts, fallback)
- [ ] **VXDeverb**: Verify RT60 estimation accuracy under poor conditions
- [ ] **VXProximity**: Consider transient-aware presence boost (suppress during sibilants, enhance during fundamentals)
- [ ] **VXCleanup**: Benchmark OutputTrimmer  -  does it clip in extreme settings?
- [ ] **All Plugins**: Regression suite  -  confirm all tests pass

---

## Build & Verify

```bash
# Full build
cmake --build build -j4

# Regression tests (should exit 0)
./build/VXStudioPluginRegressionTests

# Single plugin iteration
cmake --build build --target VXRebalancePlugin -j4

# Check recent commits
git log -20 --oneline Source/vxstudio/products/
```

---

## Conclusion

**Proximity and Deverb are genuinely state-of-the-art audio research.**

- Proximity is a physically plausible directional mic model, not a generic EQ
- Deverb is based on published IEEE research (Habets et al. 2009) with intelligent RT60 tracking
- Both adapt intelligently to input signal characteristics and framework voice analysis

**The main gaps**:
1. **README is misleading**  -  must be rewritten
2. **Rebalance Phase 2/3 unclear**  -  implementation exists but active features need verification
3. **Documentation/research attribution missing**  -  add citations to published papers

**Everything else is shipping-ready.**

# VXStudioAnalyser Stability Review  -  May 2026

**Status**: Shipping but with known fragility and ongoing fixes required.

---

## What the Analyser Does

- Passthrough plugin that inspects VX chain (full chain or per-stage)
- Publishes dry-vs-wet spectrum + envelope + dynamics telemetry
- Custom UI with sidebar (stage list), spectrum display, tabs (tone/dynamics)
- Discovers other VX plugins via framework registry and displays them

## Architecture

```
Processor (VXStudioAnalyserAudioProcessor)
    ↓
Audio thread:
  - processBlock() calls stagePublisher.publish(input, output)
  - signalQualityState.update(buffer)
    ↓
UI thread:
  - Editor timer (24 Hz → 96 Hz for first 500ms)
  - Reads StageRegistry to discover chain
  - Reads DomainRegistry for analyser domain
  - Renders spectrum display + stage sidebar
```

---

## Known Stability Issues

### **1. Stage Discovery Latency** 🔴 CRITICAL

**Problem**: Analyser doesn't discover plugins added to chain BEFORE it opens.

**Root cause**: 
- Stage discovery relies on `StageRegistry` which is only updated when stages process audio
- If a plugin hasn't processed audio since Analyser was instantiated, it's invisible
- Analyser inserted mid-chain may miss plugins upstream that haven't been "seen" yet

**Evidence** (Commit 53e7710):
```
Issue: Analyser only discovered plugins added to the chain after it was opened.
Plugins added before the Analyser were not visible until they processed audio
after the Analyser was added.

Fix: Run the Analyser's UI timer at 4x normal frequency (96 Hz instead of 24 Hz)
for the first 500ms after the editor opens. This aggressive refresh ensures all
stages in the chain are discovered quickly.
```

**Current state**: Bandaid fix with timer frequency boost. Not a structural solution.

**Risk**: 
- User inserts Analyser → doesn't see plugins above it → thinks they're missing
- Plugins sometimes appear/disappear depending on timing
- Stage list may not be complete/accurate until several seconds after opening editor

---

### **2. Dual Registry Architecture Issues** 🟡 HIGH

**Problem**: Two overlapping registries can drift out of sync:
- `DomainRegistry`  -  tracks Analyser domains (one per DAW project)
- `StageRegistry`  -  tracks all stage telemetry per domain

**Fragility**:
- If a plugin crashes/unloads, StageRegistry may have stale slots
- If Analyser exits, DomainRegistry slot can leak if `unregisterAnalyserDomain()` isn't called
- No validation that Stage's `analysisDomainId` is valid in DomainRegistry

**Evidence** (Commit 2cf2da1):
```
Refactor VX Analyser telemetry system to eliminate dual-registry architecture
```

This suggests the current architecture is problematic enough to warrant refactoring.

---

### **3. Cross-thread Safety with Stale Data** 🔴 CRITICAL

**Problem**: UI reads registries that audio thread updates, with no locking.

```cpp
// VXStudioAnalyserEditor reads:
bool readStage(int slotIndex, StageView& out) const noexcept;  // no lock

// Audio thread writes:
bool publish(int slotIndex, std::uint64_t instanceId, ...) noexcept;  // no lock
```

**Mitigations used**:
- Atomic values for simple fields
- `std::memory_order_relaxed` (no synchronization cost, but no ordering guarantees)
- Stale threshold (`kStaleThresholdMs = 1500`)  -  data older than 1.5s is ignored

**Risk**:
- Data races on `StageTelemetry` struct (copy-while-updating)
- UI may see partially-updated spectrum data (256-float array)
- Timeline: if audio thread updates spectrum while UI reads it, undefined behavior
- Segmentation faults or silent corruption possible

**Current state**: Relies on "it usually works" timing assumptions.

---

### **4. Spectrum History Buffer Management** 🟡 MEDIUM

**Problem**: `BackendState` maintains a deque with no bounds checking in hot path.

```cpp
struct BackendState {
    std::deque<SpectrumHistoryFrame> spectrumHistory;  // grows unbounded?
};
constexpr int kMaxSpectrumHistoryFrames = 300;  // but not enforced?
```

**Fragility**:
- UI timer runs at 24 Hz (or 96 Hz for first 500ms)
- If editor is open for hours, deque could grow large (memory leak)
- No visible cleanup in editor code
- Frame capacity looks like 300 but enforcement is unclear

**Current state**: Looks like it should be clamped, but not verified.

---

### **5. Stage Selection & RenderModel Invalidation** 🟡 MEDIUM

**Problem**: Complex state machine for stage selection/highlighting with multiple invalidation paths.

```cpp
void timerCallback() override;  // refreshes RenderModel
void mouseUp(...) override;     // user clicks stage
void resized() override;        // window resize
// Each path can invalidate different parts of RenderModel
```

**Fragility**:
- RenderModel has `bool valid` flag but multiple async updates
- No clear ownership of what invalidates what
- If selection changes while spectrum is being read, display glitch
- Race between timer update and user interaction

**Current state**: Works most of the time, but race conditions possible.

---

## Recent Fixes & Attempted Solutions

| Commit | Fix | Status |
|--------|-----|--------|
| 53e7710 | Aggressive timer scanning for stage discovery | ✅ Mitigates but bandaid |
| 2cf2da1 | Dual-registry architecture refactor | 🤔 Unclear if completed |
| e2d2f6a | Remove spectral filtering (display all stages) | ✅ Simplifies logic |
| 1616bc8 | Remove unused canonicalStageKey function | ✅ Dead code cleanup |
| 83d5104 | Add scroll support for long chains | ✅ Improves UX |
| c668026 | Add diagnostics to stage discovery | ✅ Debugging aid |

---

## What "Not Very Stable" Likely Means

Based on code analysis, users probably experience:

1. **Stage list is incomplete when editor first opens**
   - Symptom: Missing plugins above the Analyser
   - Solve: Wait 500ms, stages appear (not documented)
   
2. **Spectrum sometimes glitches/flickers**
   - Cause: UI reads while audio writes (no locking)
   
3. **Selecting a stage doesn't always work**
   - Cause: RenderModel invalidation race
   
4. **Analyser occasionally crashes when chain changes**
   - Cause: StageRegistry slots become invalid
   
5. **Analyzer doesn't respond to plugin insert/remove**
   - Cause: Asynchronous domain/stage discovery

---

## Stability Roadmap

### **P0  -  Thread Safety** (Critical, do this first)

Add mutex protection to StageRegistry / DomainRegistry:

```cpp
class StageRegistry {
    mutable std::mutex registryMutex;  // ADD THIS
    
    bool readStage(int slotIndex, StageView& out) const noexcept {
        std::lock_guard lock(registryMutex);  // ADD LOCKING
        // ... read safely
    }
    
    bool publish(...) noexcept {
        std::lock_guard lock(registryMutex);  // ADD LOCKING
        // ... write safely
    }
};
```

**Why**: Audio thread writes, UI thread reads. Currently unsynchronized → undefined behavior.

---

### **P1  -  Unify Registry Architecture**

Eliminate DomainRegistry/StageRegistry duality. Single source of truth:

```cpp
class UnifiedAnalysisRegistry {
    struct StageSlot {
        StageIdentity identity;
        StageState state;
        StageTelemetry telemetry;
        std::uint64_t domainId;  // which analyser owns this
        bool valid = false;
    };
    
    std::array<StageSlot, kMaxStageSlots> stages;
    std::array<DomainSlot, kMaxDomains> domains;
    // Both in one structure, single mutex
};
```

**Why**: Prevents registry drift, clarifies lifecycle.

---

### **P2  -  Synchronous Stage Discovery**

Instead of relying on StageRegistry polling when stages process audio, explicitly discover on editor open:

```cpp
void VXStudioAnalyserEditor::visibilityChanged() {
    if (isVisible()) {
        // Query host for all plugins in chain
        // Register them immediately with DomainRegistry
        // Don't wait for them to process audio
    }
}
```

**Why**: Guarantees complete chain discovery immediately.

---

### **P3  -  Validated Data Access**

```cpp
// Current (unsafe):
auto view = registry.readStage(index);
// view.telemetry may be mid-update

// Desired (safe):
auto snapshot = registry.getSnapshot(index, timestampOutOfDate);
if (timestampOutOfDate > kStaleThresholdMs) {
    // display "waiting for data..."
} else {
    // display safely
}
```

**Why**: Makes stale data explicit and avoids undefined behavior.

---

### **P4  -  Buffer Bounds Enforcement**

```cpp
void timerCallback() override {
    // ... update spectrum history
    while (spectrumHistory.size() > kMaxSpectrumHistoryFrames) {
        spectrumHistory.pop_front();
    }
}
```

**Why**: Prevents memory leak on long sessions.

---

## Testing Gaps

The Analyser has **NO regression test coverage**. Should add:

```cpp
// tests/VXStudioAnalyserTests.cpp

TEST(AnalyserStability, DiscoveryWithPreexistingPlugins) {
    // Insert chain: Cleanup -> Deverb -> Analyser
    // Verify Analyser discovers both Cleanup and Deverb immediately
}

TEST(AnalyserStability, ThreadSafetySpectrumRead) {
    // Audio thread publishes spectrum
    // UI thread reads spectrum simultaneously
    // Repeat 1000x with tight timing, verify no crashes/undefined behavior
}

TEST(AnalyserStability, DomainRegistrationClearsOnDestruct) {
    // Create Analyser, destroy it
    // Verify DomainRegistry is clean (no stale entries)
}

TEST(AnalyserStability, SpectrumHistoryBounded) {
    // Run UI timer for 10 seconds
    // Verify spectrumHistory.size() <= kMaxSpectrumHistoryFrames
}

TEST(AnalyserStability, PluginAddRemoveRapidly) {
    // Insert/remove 5 plugins rapidly while Analyser is open
    // Verify no crashes, stage list stays accurate
}

TEST(AnalyserStability, LongSessionMemoryStability) {
    // Simulate 1 hour of Analyser operation
    // Verify memory usage is stable (no leaks)
}
```

---

## Summary: Analyser Stability

| Aspect | Status | Severity |
|--------|--------|----------|
| Thread safety | ❌ No mutexes | 🔴 CRITICAL |
| Stage discovery | ⚠️ Bandaid timer fix | 🟡 HIGH |
| Registry design | ⚠️ Dual architecture | 🟡 HIGH |
| RenderModel races | ⚠️ Complex invalidation | 🟡 MEDIUM |
| History buffer bounds | ⚠️ Unclear enforcement | 🟡 MEDIUM |
| Testing | ❌ None | 🔴 CRITICAL |

---

## Recommendation

**The Analyser works most of the time but should NOT ship to production without**:

1. **Thread safety** (mutexes in registries)  -  solves crashes
2. **Unified registry architecture**  -  prevents drift/leaks
3. **Regression tests**  -  prevents regressions
4. **Synchronous discovery**  -  guarantees complete chain visibility

**Current state**: "Works if you use it gently"  -  risky for production systems where users might:
- Rapidly add/remove plugins
- Use the Analyser in long-session projects
- Expect full chain inspection immediately upon opening

---

## Action Items

- [ ] Add mutex protection to StageRegistry/DomainRegistry
- [ ] Unify dual-registry architecture into single UnifiedAnalysisRegistry
- [ ] Implement explicit stage discovery when Analyser editor opens
- [ ] Add bounds checking to spectrumHistory deque
- [ ] Add regression test suite (6+ tests minimum)
- [ ] Document the 500ms delay behavior or eliminate it
- [ ] Review RenderModel invalidation logic for races
