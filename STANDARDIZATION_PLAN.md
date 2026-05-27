# Voice/General Mode Standardization Plan

## Target Architecture: Unified ProcessOptions Pattern

All effects will follow this standardized pattern based on **Denoiser** (best implementation):

### Standard Pattern (Template)

```cpp
void VXEffectAudioProcessor::processProduct(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    // ... calculate parameters ...
    
    // 1. DETECT MODE (Universal)
    const bool isVoice = vxsuite::readMode(parameters, productIdentity) == vxsuite::Mode::vocal;
    const auto& policy = currentModePolicy();
    const auto voiceContext = getVoiceContextSnapshot();
    
    // 2. CALCULATE VOCAL PRIORITY (Universal Pattern)
    const float vocalPriority = isVoice
        ? vxsuite::clamp01(0.40f * voiceContext.vocalDominance
                         + 0.30f * voiceContext.intelligibility  
                         + 0.20f * voiceContext.phraseActivity    // or other context metric
                         + 0.10f * voiceContext.speechPresence)
        : 0.0f;
    
    // 3. BUILD ProcessOptions (Framework Pattern - Minor effect-specific tweaks)
    vxsuite::ProcessOptions options {};
    options.isVoiceMode = isVoice;
    options.sourceProtect = isVoice 
        ? vxsuite::clamp01(baseVoiceValue + scaleVoice * control + voiceBoost * vocalPriority)
        : vxsuite::clamp01(baseGeneralValue + scaleGeneral * control);
    options.guardStrictness = isVoice 
        ? vxsuite::clamp01(baseVoiceValue + scaleVoice * control + voiceBoost * vocalPriority)
        : vxsuite::clamp01(baseGeneralValue + scaleGeneral * control);
    options.speechFocus = isVoice 
        ? juce::jmax(minVoice, policy.speechFocus + voiceBoost * vocalPriority)
        : juce::jmax(minGeneral, policy.speechFocus);
    options.lateTailAggression = policy.lateTailAggression;
    
    // 4. EFFECT-SPECIFIC MODIFICATIONS (Non-framework)
    // If effect needs contentMode for DSP layer:
    dspParams.contentMode = isVoice ? 0 : 1;
    
    // If effect needs additional voiceMode-specific scaling:
    const float effectSpecificGain = isVoice ? (baseVoice + vocalPriority * boost) : baseGeneral;
    
    // 5. PASS OPTIONS TO DSP
    effectDsp.processInPlace(buffer, amount, options);
    // OR if DSP doesn't take options, pass dspParams with contentMode
}
```

---

## Current State & Required Changes

### ✅ Already Correct (Denoiser, Subtract)
- Using ProcessOptions pattern ✓
- Building all framework fields ✓
- Passing to DSP layer ✓
- **No changes needed**

### ⚠️ Partial Implementation (OptoComp, Cleanup, Finish)
- Using contentMode pattern (simplified)
- **Change Required:** Migrate to ProcessOptions

#### Current (OptoComp):
```cpp
dspParams.contentMode = voiceMode ? 0 : 1;
dspParams.peakReduction *= (voiceMode ? ... : 1.08f);
```

#### Target (OptoComp):
```cpp
ProcessOptions options {};
options.isVoiceMode = voiceMode;
options.sourceProtect = voiceMode ? ... : ...;
// ... other options ...
optoDsp.setParams(dspParams);  // contentMode still set
optoDsp.process(buffer, options);  // NEW: pass options
```

### ❌ Non-Standard (Tone, Leveler)
- No ProcessOptions usage
- Direct parameter scaling
- **Change Required:** Introduce ProcessOptions framework

#### Tone Current:
```cpp
const float maxGainDb = voiceMode
    ? kVocalMaxGainDb * (1.0f - 0.05f * vocalPriority)
    : kGeneralMaxGainDb;
```

#### Tone Target:
```cpp
vxsuite::ProcessOptions options {};
options.isVoiceMode = voiceMode;
options.sourceProtect = voiceMode ? (0.8f + 0.2f * vocalPriority) : 0.5f;
options.speechFocus = voiceMode ? 0.9f : 0.3f;
// ... etc ...
// Then use options in DSP to modulate EQ curves
toneDsp.setVoiceOptions(options);
toneDsp.process(buffer);
```

---

## Implementation Order

### Phase 1: Establish DSP Layer Support (If Needed)
- [ ] Verify all DSP layers can accept ProcessOptions
- [ ] Update DSP signatures that don't yet accept options

### Phase 2: Migrate contentMode Effects (Lower Risk)
- [ ] OptoComp: Add ProcessOptions, keep contentMode
- [ ] Finish: Add ProcessOptions, keep contentMode
- [ ] Cleanup: Add ProcessOptions, keep contentMode

### Phase 3: Introduce Framework to Direct-Scaling Effects (Higher Risk)
- [ ] Tone: Add ProcessOptions, use in EQ modulation
- [ ] Leveler: Add ProcessOptions, use in state machine logic

### Phase 4: Validation & Testing
- [ ] Unit tests for ProcessOptions calculation
- [ ] Regression tests (voice vs general behavior unchanged)
- [ ] Integration tests (all effects properly scaled)

---

## ProcessOptions Field Mapping (Reference)

| Field | Voice Mode | General Mode | Used By |
|-------|-----------|--------------|---------|
| isVoiceMode | true | false | All |
| sourceProtect | 0.48–0.88 | 0.28–0.80 | Denoiser, Subtract, Cleanup |
| guardStrictness | 0.55–0.70 | 0.35–0.85 | Denoiser, Subtract |
| speechFocus | 0.78+ | 0.18+ | Denoiser, Subtract |
| voiceProtect | 0.75 (default) | 0.75 (default) | Framework layer |
| lateTailAggression | Policy-driven | Policy-driven | Deverb, Denoiser |
| stereoWidthProtect | Policy-driven | Policy-driven | Leveler |

---

## Vocal Priority Formula (Standardized)

Each effect should use one of these (listed by effect):

### Default Formula (4-factor)
```cpp
0.40f * voiceContext.vocalDominance
+ 0.30f * voiceContext.intelligibility
+ 0.20f * voiceContext.phraseActivity
+ 0.10f * voiceContext.speechPresence
```
**Used by:** Denoiser, Subtract, Deverb

### Leveler Formula (4-factor, phrase-aware)
```cpp
0.36f * voiceContext.vocalDominance
+ 0.28f * voiceContext.intelligibility
+ 0.18f * voiceContext.phraseActivity
+ 0.10f * voiceContext.speechPresence
+ 0.08f * voiceContext.centerConfidence
```

### Tone Formula (4-factor, transient-aware)
```cpp
0.40f * voiceContext.vocalDominance
+ 0.30f * voiceContext.intelligibility
+ 0.20f * voiceContext.transientRisk
+ 0.10f * voiceContext.speechPresence
```

### OptoComp/Finish Formula (5-factor)
```cpp
0.38f * voiceContext.vocalDominance
+ 0.26f * voiceContext.intelligibility
+ 0.18f * voiceContext.phraseActivity
+ 0.10f * voiceContext.speechPresence
+ 0.08f * voiceContext.centerConfidence
```

---

## Success Criteria

✅ **All 7 effects use ProcessOptions framework**
✅ **Vocal priority calculation follows one of 3 approved patterns**
✅ **No hardcoded voice/general values in processor layer** (only in options building)
✅ **contentMode still set for DSP layers that need it** (backward compatible)
✅ **All regression tests pass** (behavior unchanged, only structure improved)
✅ **Code review shows consistent patterns across all effects**
