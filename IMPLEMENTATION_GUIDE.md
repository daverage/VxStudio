# Voice/General Mode Unification - Implementation Guide

## Status: Phase 2 In Progress

### ✅ Completed Changes

#### OptoComp (VxOptoCompProcessor.cpp)
- [x] Refactored to build ProcessOptions with framework pattern
- [x] Added vocal priority calculation
- [x] Builds: isVoiceMode, sourceProtect, guardStrictness, speechFocus, voiceProtect, lateTailAggression
- [x] Updated: `optoDsp.process(buffer, options)` call signature

#### Finish (VxFinishProcessor.cpp) 
- [x] Refactored to build ProcessOptions with framework pattern
- [x] Added vocal priority calculation  
- [x] Builds: isVoiceMode, sourceProtect, guardStrictness, speechFocus, voiceProtect, lateTailAggression
- [x] Updated: `finishChain.process(buffer, options)` call signature

#### VxStudioFinishDsp (Framework)
- [x] Added ProcessOptions parameter to process() signature
- [x] Added include for VxStudioProcessOptions.h
- [x] Updated process() implementation to use `options.isVoiceMode` instead of `params.contentMode`

### ⚠️ In Progress

#### Cleanup (VxCleanupProcessor.cpp) 
- [ ] Needs refactoring (complex Params struct, multiple analysis passes)
- [ ] Approach: Add ProcessOptions parameter to clarity::Dsp::process()
- [ ] Keep existing Params struct for effect-specific parameters
- [ ] Extract protection-related logic to consume ProcessOptions

#### Leveler (VxLevelerProcessor.cpp)
- [ ] Needs ProcessOptions wrapper around existing state machine
- [ ] Approach: Build ProcessOptions, pass to Leveler DSP
- [ ] Leveler's state machine logic stays unchanged
- [ ] ProcessOptions modulates protection envelopes

#### Tone (VxToneProcessor.cpp)
- [ ] Simplest to convert (no complex DSP)
- [ ] Approach: Build ProcessOptions, use in EQ curve modulation
- [ ] Pass vocal priority into filter coefficient calculation

#### Deverb (VxDeverbProcessor.cpp)
- [ ] Already uses ProcessOptions partially
- [ ] Approach: Ensure all ProcessOptions fields are consistently populated
- [ ] Verify vocal priority weighting is correct

#### Denoiser (VxDenoiserProcessor.cpp)
- [ ] Already fully implements ProcessOptions pattern ✓
- [ ] No changes needed

#### Subtract (VxSubtractProcessor.cpp)
- [ ] Already fully implements ProcessOptions pattern ✓
- [ ] No changes needed

---

## Standardized Pattern (Template)

All effects must follow this structure:

```cpp
void VXEffectAudioProcessor::processProduct(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    // ... read control parameters ...
    
    // ──────────────────────────────────────────────────────────────
    // FRAMEWORK SECTION (Copy-paste for all effects)
    // ──────────────────────────────────────────────────────────────
    
    // 1. Detect mode
    const bool isVoice = vxsuite::readMode(parameters, productIdentity) == vxsuite::Mode::vocal;
    const auto& policy = currentModePolicy();
    const auto voiceContext = getVoiceContextSnapshot();
    
    // 2. Calculate vocal priority (choose one of 3 formulas below)
    const float vocalPriority = isVoice ? vxsuite::clamp01(
        0.40f * voiceContext.vocalDominance +
        0.30f * voiceContext.intelligibility +
        0.20f * voiceContext.phraseActivity +  // or other context metric
        0.10f * voiceContext.speechPresence
    ) : 0.0f;
    
    // 3. Build ProcessOptions (effect can customize base/scale values)
    vxsuite::ProcessOptions options {};
    options.isVoiceMode = isVoice;
    options.sourceProtect = isVoice 
        ? vxsuite::clamp01(baseVoice + scaleVoice * control + voiceBoost * vocalPriority)
        : vxsuite::clamp01(baseGeneral + scaleGeneral * control);
    options.guardStrictness = isVoice 
        ? vxsuite::clamp01(baseVoice + scaleVoice * control + voiceBoost * vocalPriority)
        : vxsuite::clamp01(baseGeneral + scaleGeneral * control);
    options.speechFocus = isVoice 
        ? juce::jmax(minVoice, policy.speechFocus + voiceBoost * vocalPriority)
        : juce::jmax(minGeneral, policy.speechFocus);
    options.voiceProtect = isVoice ? 0.85f : 0.60f;  // effect can customize
    options.lateTailAggression = policy.lateTailAggression;
    
    // ──────────────────────────────────────────────────────────────
    // EFFECT-SPECIFIC SECTION (Customize per effect)
    // ──────────────────────────────────────────────────────────────
    
    // Calculate effect-specific parameters
    // (may use isVoice and/or vocalPriority for scaling)
    const float effectGain = isVoice ? (baseVoice + vocalPriority * boost) : baseGeneral;
    
    // Set DSP parameters
    effectDsp.setParams({
        .contentMode = isVoice ? 0 : 1,  // optional, for backward compat
        .customField1 = value1,
        .customField2 = value2,
    });
    
    // Process with ProcessOptions
    effectDsp.process(buffer, options);
}
```

---

## Implementation Checklist

### Phase 1: Foundation (Already Done)
- [x] ProcessOptions header established
- [x] ProcessOptions struct defined with framework fields
- [x] Denoiser & Subtract fully implement pattern
- [x] finish::Dsp updated to accept ProcessOptions

### Phase 2: Low-Risk Migration (In Progress)
- [x] OptoComp → ProcessOptions
- [x] Finish → ProcessOptions
- [ ] Cleanup → ProcessOptions (complex, needs careful handling)

### Phase 3: High-Impact Migrations (Not Started)
- [ ] Leveler → ProcessOptions (state machine integration)
- [ ] Tone → ProcessOptions (EQ modulation)
- [ ] Deverb → ProcessOptions (verify completeness)

### Phase 4: Validation & Polish
- [ ] All 7 effects use ProcessOptions framework
- [ ] Regression tests pass
- [ ] Documentation updated
- [ ] Code review complete

---

## Effect-Specific Notes

### Cleanup (Highest Complexity)
**Current:** Uses clarity::Dsp::Params with 20+ fields
**Challenge:** Large Params struct already contains effect-specific logic
**Solution:** 
1. Add optional ProcessOptions parameter to process()
2. Keep Params struct unchanged for backward compatibility
3. Extract voiceMode scaling from Params, move to ProcessOptions
4. Update clarity::Dsp to optionally consume ProcessOptions for protection scaling

**Files to Update:**
- `Source/vxstudio/products/cleanup/VxCleanupProcessor.cpp` (processor)
- `Source/vxstudio/products/cleanup/dsp/VxClarityDsp.h` (header)
- `Source/vxstudio/products/cleanup/dsp/VxClarityDsp.cpp` (implementation)

### Leveler (Medium Complexity)
**Current:** Complex state machine, direct voiceMode checks scattered
**Challenge:** State machine tightly couples to voiceMode detection
**Solution:**
1. Extract mode detection to uniform section
2. Keep state machine logic unchanged
3. Wrap with ProcessOptions for framework consistency
4. State machine output can consume options.voiceProtect for scaling

**Files to Update:**
- `Source/vxstudio/products/leveler/VxLevelerProcessor.cpp` (processor)
- `Source/vxstudio/products/leveler/dsp/VxLevelerDsp.h` (header)
- `Source/vxstudio/products/leveler/dsp/VxLevelerDsp.cpp` (implementation)

### Tone (Lowest Complexity)
**Current:** Simple EQ with voiceMode scaling, no ProcessOptions
**Challenge:** Straightforward to refactor
**Solution:**
1. Build ProcessOptions (copy pattern from Denoiser)
2. Pass vocal priority into EQ coefficient calculation
3. No DSP layer signature change needed if EQ calculations are in processor

**Files to Update:**
- `Source/vxstudio/products/tone/VxToneProcessor.cpp` (processor only)

### Deverb (Already Mostly There)
**Current:** Uses ProcessOptions, but verify completeness
**Files to Review:**
- `Source/vxstudio/products/deverb/VxDeverbProcessor.cpp`
- `Source/vxstudio/products/deverb/dsp/VxDeverbSpectralProcessor.cpp`

---

## Vocal Priority Formula Reference

Use ONE of these formulas for each effect:

### Formula A (Default - 4 factors)
```cpp
0.40f * voiceContext.vocalDominance +
0.30f * voiceContext.intelligibility +
0.20f * voiceContext.phraseActivity +
0.10f * voiceContext.speechPresence
```
**Used by:** Denoiser, Subtract, Deverb

### Formula B (Phrase-aware - 5 factors)  
```cpp
0.36f * voiceContext.vocalDominance +
0.28f * voiceContext.intelligibility +
0.18f * voiceContext.phraseActivity +
0.10f * voiceContext.speechPresence +
0.08f * voiceContext.centerConfidence
```
**Used by:** Leveler (with state machine)

### Formula C (Transient-aware - 4 factors)
```cpp
0.40f * voiceContext.vocalDominance +
0.30f * voiceContext.intelligibility +
0.20f * voiceContext.transientRisk +
0.10f * voiceContext.speechPresence
```
**Used by:** Tone (for EQ awareness)

### Formula D (Compressor - 5 factors)
```cpp
0.38f * voiceContext.vocalDominance +
0.26f * voiceContext.intelligibility +
0.18f * voiceContext.phraseActivity +
0.10f * voiceContext.speechPresence +
0.08f * voiceContext.centerConfidence
```
**Used by:** OptoComp, Finish (compressor-specific)

---

## Testing Strategy

After each effect is migrated:

1. **Functional Test:** Voice mode behavior unchanged
2. **Protection Test:** sourceProtect/guardStrictness values are reasonable
3. **Priority Test:** vocalPriority formula produces expected 0.0-1.0 range
4. **Regression Test:** Compare output with vs without voice mode

### Test Cases
```
✓ Vocal content (tight take) → vocalPriority ≈ 0.7-0.9 → protection scales up
✓ Instrumental (drums) → vocalPriority = 0.0 → general protection applies
✓ Mixed content → vocalPriority ≈ 0.3-0.6 → balanced scaling
✓ Buried vocals → uses voiceContext.buriedSpeech for extra boost
```

---

## Commit Strategy

Commit in this order (one per PR):
1. Foundation: Update finish::Dsp, then OptoComp + Finish processors
2. Phase 2a: Tone (simplest, no DSP changes)
3. Phase 2b: Cleanup (complex, high risk)
4. Phase 3: Leveler (complex state machine)
5. Phase 4: Deverb verification & final review

Each PR should:
- [ ] Include standardization plan excerpt in description
- [ ] Show before/after processor comparison
- [ ] Include regression test results
- [ ] Get approval before merge
