# Unified Voice/General Mode Model

## Executive Summary

The VxStudio voice/general mode system is now **standardized** around the `ProcessOptions` framework, which provides:

1. **Common baseline protection** — all effects apply voice-aware safety floors
2. **Uniform vocal priority calculation** — consistent across effects  
3. **Framework-managed scaling** — protection levels adapt to content
4. **Effect-specific customization** — each effect tunes how aggressively it applies protection

---

## The Model

### Architecture: Three-Layer Stack

```
┌─────────────────────────────────────────────────────────────────────────┐
│ LAYER 1: Framework (VxStudioProcessOptions.h)                           │
│ ─────────────────────────────────────────────────────────────────────── │
│ struct ProcessOptions {                                                  │
│     bool isVoiceMode;                    // Mode flag (true = voice)    │
│     float sourceProtect [0.28-0.88];     // Signal preservation         │
│     float guardStrictness [0.35-0.85];   // Guard harshness             │
│     float speechFocus [0.18-0.9];        // Speech band emphasis        │
│     float voiceProtect [0.6-0.85];       // Voice safety margin         │
│     float lateTailAggression [0.0-1.0];  // Tail handling per mode      │
│     float stereoWidthProtect [0.0-1.0];  // Stereo preservation         │
│ };                                                                       │
│                                                                          │
│ RESPONSIBILITY: Define baseline protection envelope                    │
│ MANAGED BY: ProcessorBase → currentModePolicy()                        │
└─────────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────────┐
│ LAYER 2: Processor (Effect-Specific)                                    │
│ ─────────────────────────────────────────────────────────────────────── │
│ // All effects follow this pattern:                                     │
│                                                                          │
│ 1. Read: isVoice = readMode(parameters, productIdentity) == Mode::vocal │
│ 2. Calculate: vocalPriority = mix of voice context metrics              │
│ 3. Build: ProcessOptions with voiceMode-aware defaults                 │
│ 4. Scale: Effect-specific parameters using vocalPriority                │
│ 5. Process: effectDsp.process(buffer, options)                          │
│                                                                          │
│ RESPONSIBILITY: Adaptive parameter selection based on content           │
│ EXAMPLE VALUES FOR sourceProtect:                                       │
│   Voice: 0.48f + 0.40f*control + 0.16f*priority  → [0.48-0.88]        │
│   General: 0.28f + 0.52f*control                  → [0.28-0.80]        │
└─────────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────────┐
│ LAYER 3: DSP (Algorithm Implementation)                                 │
│ ─────────────────────────────────────────────────────────────────────── │
│ void process(buffer, ProcessOptions& opts) {                            │
│     // Use options for:                                                 │
│     // - Gain floor multipliers                                         │
│     // - Spectral floor thresholds                                      │
│     // - Guard strictness scaling                                       │
│     // - Speech band weight adjustment                                  │
│                                                                          │
│     // Keep effect-specific logic (state machines, algorithms)          │
│ }                                                                        │
│                                                                          │
│ RESPONSIBILITY: Apply protection at algorithm level                    │
│ EXAMPLE: denoiser gain = baseline * (1 - sourceProtect * sensitivity)  │
└─────────────────────────────────────────────────────────────────────────┘
```

### Standardized Vocal Priority Calculation

All effects calculate priority as a **normalized weighted sum** of voice context metrics:

```cpp
const float vocalPriority = isVoice
    ? vxsuite::clamp01(
        w1 * voiceContext.vocalDominance +
        w2 * voiceContext.intelligibility +
        w3 * voiceContext.phraseActivity +    // or alternative metric
        w4 * voiceContext.speechPresence
    )
    : 0.0f;
```

**Key properties:**
- Result is always in [0.0, 1.0]
- In general mode: always 0.0 (no priority boost)
- In voice mode: 0.0 = uncertain content, 1.0 = confident voice
- Weights (w1+w2+w3+w4) sum to 1.0 for normalized contribution

**Approved weight sets:**
- **Denoiser/Subtract/Deverb:** (0.40, 0.30, 0.20, 0.10) - balanced
- **Leveler:** (0.36, 0.28, 0.18, 0.10, +0.08 center) - phrase-aware
- **Tone:** (0.40, 0.30, 0.20 transient, 0.10) - transient-aware
- **OptoComp/Finish:** (0.38, 0.26, 0.18, 0.10, +0.08 center) - compressor-tuned

### Parameter Scaling Formula

Each effect scales its parameters using this universal pattern:

```cpp
// Voice mode: escalate protection + apply priority boost
param_voice = baseValue_voice + (userControl * scale_voice) + (vocalPriority * boost_voice)

// General mode: conservative protection, no priority scaling
param_general = baseValue_general + (userControl * scale_general)

// Select based on mode
finalParam = isVoice ? param_voice : param_general
```

**Example (from OptoComp):**
```cpp
options.sourceProtect = isVoice 
    ? clamp01(0.64f + 0.36f*body + 0.12f*priority)    // voice: 0.64-1.0
    : clamp01(0.30f + 0.50f*body);                    // general: 0.30-0.80
```

---

## Effects Implementation Status

| Effect | Status | Pattern | Vocal Priority | Notes |
|--------|--------|---------|-----------------|-------|
| **Denoiser** | ✅ Complete | ProcessOptions | Standard 4-factor | Framework reference |
| **Subtract** | ✅ Complete | ProcessOptions | Standard 4-factor | Framework reference |
| **OptoComp** | ✅ Complete | ProcessOptions | 5-factor (compressor) | Just migrated |
| **Finish** | ✅ Complete | ProcessOptions | 5-factor (compressor) | Just migrated |
| **Tone** | ⚠️ Partial | Direct scaling | 4-factor transient | Needs ProcessOptions wrapper |
| **Cleanup** | ⚠️ Partial | contentMode flag | Direct in Params | Needs ProcessOptions integration |
| **Leveler** | ⚠️ Partial | Direct state machine | State-based | State machine + ProcessOptions needed |
| **Deverb** | ✅ Complete | ProcessOptions | Standard 4-factor | Verify completeness |

---

## What Gets Standardized

### ✅ STANDARDIZED (Framework-managed, no duplication)
- Mode detection: `readMode(parameters, productIdentity) == Mode::vocal`
- Vocal priority calculation: weighted sum of voice context metrics
- ProcessOptions construction: baseline protection values
- Protection field meanings: sourceProtect, guardStrictness, speechFocus, etc.

### ⚠️ CUSTOMIZABLE (Per-effect, based on ProcessOptions)
- Base values for ProcessOptions fields (e.g., 0.64 vs 0.30 for sourceProtect)
- Scale factors applied to user controls
- Priority boost multipliers (e.g., 0.12f vs 0.16f boost)
- Effect-specific vocal priority formula (if different from standard)

### ❌ NOT STANDARDIZED (Effect-specific algorithms)
- State machines (Leveler's 4-state detector)
- Spectral processing (Deverb's cepstral analysis)
- Artifact detection (Cleanup's sibilance/plosive classification)
- EQ curves (Tone's filter design)

---

## Why This Works

### 1. **Separation of Concerns**
- Framework: "What are the protection constraints?"
- Processor: "Which protection level is appropriate?"
- DSP: "How do we apply this protection?"

### 2. **Consistency**
- All effects use same mode detection
- All effects calculate priority the same way
- All effects scale protection parameters uniformly

### 3. **Flexibility**
- Each effect can customize base/scale values
- State machines stay independent
- Algorithm-specific logic untouched

### 4. **Maintainability**
- Bug fix in priority formula → fixed everywhere
- New mode policy → automatically applies to all effects
- New context metric → can be incorporated uniformly

---

## Usage Reference

### For New Effects

1. **Copy the processor template** from IMPLEMENTATION_GUIDE.md
2. **Choose a vocal priority formula** (4-5 factors from approved set)
3. **Define base/scale values** for ProcessOptions fields
4. **Build ProcessOptions in processor**, pass to DSP
5. **Update DSP signature** to accept ProcessOptions parameter

### For Existing Effects (Migration)

1. **Extract mode detection** → standardized pattern
2. **Replace scattered voiceMode checks** → single ProcessOptions build
3. **Update DSP signatures** → accept ProcessOptions parameter
4. **Migrate DSP implementation** → use options.sourceProtect etc instead of hardcoded values

---

## Testing Checklist

Before considering an effect "done":

- [ ] Vocal content has vocalPriority > 0.6
- [ ] Instrumental content has vocalPriority = 0.0
- [ ] sourceProtect values are in expected range
- [ ] guardStrictness values scale with vocalPriority
- [ ] Regression tests pass (behavior unchanged)
- [ ] ProcessOptions all fields populated
- [ ] Code review approved

---

## Success Metrics

**When standardization is complete:**

✅ All 7 effects build ProcessOptions in processor  
✅ All 7 effects pass ProcessOptions to DSP layer  
✅ All 7 effects use one of 4 approved vocal priority formulas  
✅ No hardcoded voiceMode values in effect-specific code  
✅ Regression tests: all effects behave identically before/after migration  
✅ Code consistency: processor patterns are copy-paste identical  
✅ Documentation: implementation guide updated with actual results  
✅ Architecture review: approved by team

---

## Next Steps

1. **Phase 2:** Complete Cleanup migration (in progress)
2. **Phase 3:** Add Tone ProcessOptions wrapper
3. **Phase 4:** Integrate Leveler state machine with ProcessOptions
4. **Phase 5:** Verify Deverb completeness
5. **Phase 6:** Final validation & documentation

See IMPLEMENTATION_GUIDE.md for detailed migration instructions.
