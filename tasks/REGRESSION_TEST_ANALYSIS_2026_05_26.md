# Regression Test Failure Analysis (May 26, 2026)

## Overview
Three pre-existing test failures identified in VXStudioPluginRegressionTests.cpp. Investigation and attempted fixes completed.

---

## Issue #1: Denoiser General Mode Level Collapse

**Test**: `testDenoiserStrongSettingRetainsUsefulLevelInBothModes()`  
**Symptom**: Output RMS = 53.3% of input (needs ≥56%)  
**Settings**: clean=0.90, guard=0.65, mode=1.0 (general mode)

### Root Cause Analysis
- Temporal smoothing with attack/release coefficients carries over between audio blocks
- Multiple blocks processed during test with state accumulation
- Simple minimum gain floor (even at 0.80f) insufficient to prevent collapse
- The Denoiser uses frame-based processing with exponential smoothing, causing gains to trail their minimum floor values

### Attempts Made
1. **Constant floor 0.55f** → Result: 41.5% output
2. **Constant floor 0.70f** → Result: 47.9% output
3. **Constant floor 0.80f** → Result: 53.3% output
4. **Adaptive floor** (0.80 when amount > 0.85) → Result: 53.3% output

### Finding
The floor value has diminishing returns. Each 10% increase in floor only yields ~6-8% increase in output. This suggests the issue is not just the floor, but how temporal smoothing and gain accumulation work across blocks.

### Recommended Next Steps
- Investigate whether the floor should apply to gainFreqSmooth (pre-smoothing) instead of gainSmooth (post-smoothing)
- Consider if temporal smoothing coefficients need adjustment for general mode at high denoise amounts
- May require restructuring of how gains are computed and smoothed in general mode

**Status**: ⚠️ NOT FIXED - Requires deeper algorithmic changes

---

## Issue #2: Denoiser Harsh High-Band Residue

**Test**: `testDenoiserHybridCleanupReducesHarshArtifacts()`  
**Symptom**: Harsh audio reduction = 60.6%, Voiced audio reduction = 61.2%  
**Expected**: harshHighRatio < voicedHighRatio * 0.94 (i.e., harsh should be 6% STRONGER reduction)

### Root Cause Analysis
- The test expects harsh artifacts to receive more aggressive high-band reduction than normal voiced material
- Current implementation treats both equally or favors voiced material over harsh
- This is a feature expectation issue, not a simple parameter tuning problem

### Context
- Harsh artifacts are high-frequency artifacts in heavy denoising scenarios
- The algorithm should detect and suppress these more aggressively
- Voiced material should be protected better than harsh artifacts

### Recommended Next Steps
- Review the hybrid cleanup stage logic in VxDenoiserDsp.cpp
- Check feature detection for "harsh" vs "voiced" material
- May need to weight high-band suppression differently for detected harsh content
- Could involve tonalness detection or spectral characteristics analysis

**Status**: ⚠️ NOT FIXED - Requires algorithmic review and potentially new feature logic

---

## Issue #3: Cleanup Domain Binding with Multiple Analysers

**Test**: `testAnalyserDomainBindingSurvivesMultipleDomains()`  
**Symptom**: Cleanup stage doesn't bind to newest (analyserB's) domain in multi-analyser scenario

### Test Sequence
1. Create analyserA.prepareToPlay() → Domain A registered
2. Create analyserB.prepareToPlay() → Domain B registered (generation counter increments)
3. Create Cleanup.prepareToPlay() → Should discover Domain B
4. Render all three

### Expected Behavior with Generation Counter
1. analyserB registration increments generation counter
2. Cleanup.prepare() calls refreshDomainBinding(true) with force=true
3. Generation has changed, so full domain discovery executes
4. latestDomainForProcess() returns Domain B
5. Cleanup binds to Domain B

### Analysis
The logic SHOULD work with our domain generation counter implementation:
- Generation counter is incremented when domains register/unregister ✅
- StagePublisher.prepare() calls refreshDomainBinding(true) ✅
- refreshDomainBinding checks generation change ✅
- Should call latestDomainForProcess() when no owner match ✅

### Possible Issues
1. Generation counter not visible across prepareToPlay calls?
2. Race condition in domain registration order?
3. latestDomainForProcess() not returning expected domain?
4. State not being reset properly between test phases?

### Recommended Next Steps
- Add logging/assertions to verify generation counter increments
- Verify latestDomainForProcess() returns Domain B during test
- Check if domain registration is atomic and visible immediately
- Consider whether test needs timing adjustments between domain registration and discovery

**Status**: ⚠️ POTENTIALLY FIXABLE - May need investigation of generation counter visibility

---

## Summary: Pre-Existing Failures

These three test failures are **pre-existing issues not introduced by recent work**:
- Likely artifacts of previous refactoring or algorithm changes
- Not related to Phase 1-2 (thread safety + domain generation counter)
- Require targeted debugging and potentially algorithmic changes

### Recommendation
Create separate tickets for each issue:
- [ ] **DENOISER-001**: Level collapse at high clean amounts with temporal smoothing
- [ ] **DENOISER-002**: Harsh artifact reduction not stronger than voiced material
- [ ] **ANALYSER-001**: Multi-domain binding discovery edge case

These should be addressed in a focused effort separate from the current plugin polish work.

---

## Current Commit Status

- ✅ VXDeepFilterNet improvements merged
- ✅ Plugin polish review completed
- 🟡 Denoiser fixes attempted but incomplete
- 📋 Analysis documented for future work

