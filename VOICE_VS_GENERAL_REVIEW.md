# Voice vs General Mode Usage Review

## Architecture Overview

**Framework Pattern (Correct Understanding):**

There IS a common baseline protection framework applied at the framework level via `ProcessOptions` struct. Each effect:
1. Detects mode: `bool voiceMode = readMode(parameters, productIdentity) == Mode::vocal`
2. Calculates vocal priority from voice context
3. Builds `ProcessOptions` with voice-aware protection values (shared framework)
4. Applies effect-specific modifications on top

```cpp
// Framework baseline — all effects use this
struct ProcessOptions {
    bool isVoiceMode = true;              // Mode flag
    float voiceProtect = 0.75f;           // Voice-specific protection
    float sourceProtect = 0.75f;          // Source material protection
    float lateTailAggression = 0.55f;     // Reverb/noise tail handling
    float stereoWidthProtect = 0.75f;     // Stereo image preservation
    float guardStrictness = 0.75f;        // Speech guard harshness
    float speechFocus = 0.75f;            // Speech frequency emphasis
    bool learningActive = false;
    float subtract = 0.0f;
    float sensitivity = 0.0f;
    bool isPrimary = true;
    bool labRawMode = false;
};
```

**Effect Pattern (Common):**
```cpp
// Every effect does this
const bool voiceMode = readMode(parameters, productIdentity) == Mode::vocal;
const float vocalPriority = voiceMode ? clamp01(0.4*vocalDom + 0.3*intel + ...) : 0.0f;

ProcessOptions options {};
options.isVoiceMode = voiceMode;
options.sourceProtect = voiceMode 
    ? clamp01(baseVoice + scaleVoice * priority)
    : clamp01(baseGeneral + scaleGeneral * priority);
// ... etc for other fields
```

**Effect-Specific Modifications:**
- Tone: adjusts EQ frequency ranges & gains
- Leveler: state machine & phrase tracking
- OptoComp/Finish: compressor scaling
- Cleanup: artifact suppression coefficients
- etc.

---

## Summary: How Voice/General Actually Works

### The Answer to "Is there a common framework protection?"

**YES.** The `ProcessOptions` struct in the framework provides baseline voice/general protection. Every effect:

1. **Builds identical ProcessOptions structure** with voice-aware values
2. **Uses the same vocal priority calculation** pattern (weighted sum of voice context metrics)
3. **Passes these options to DSP layer** which applies universal protection (floors, gates, restoration)
4. **Adds effect-specific tuning** only for algorithm-specific behavior

**What's NOT centralized:** The exact coefficients, frequency ranges, and state machines. Each effect tunes how aggressively it applies its processing in voice vs general mode.

**Example: Denoiser vs Subtract**
Both use ProcessOptions baseline:
- `sourceProtect`: voice=0.48-0.88, general=0.28-0.80 (scaled by vocal priority)
- `guardStrictness`: voice=0.55-0.70, general=0.35-0.85 (protection against artifacts)
- `speechFocus`: voice≥0.78, general≥0.18 (speech band emphasis)

Then each applies its own logic:
- **Denoiser**: multiplies these values into spectral floor calculations
- **Subtract**: uses `speechFocus` to adjust speech preservation blend

---

## Effects Summary

### 1. **Leveler** — Most Sophisticated Mode Differentiation
**File:** `Source/vxstudio/products/leveler/dsp/VxLevelerDsp.cpp`

**Voice Mode Implementation:**
- Implements state machine with 4 states: `neutral`, `voiceLeading`, `guitarDominant`, `voiceBuried`
- Uses `voiceDecision` (levelBias, speechLift, transientTame) based on detected state
- Monitors `phraseStart`, `phraseActivity`, `phraseEnd` from voice context
- Has separate `vocalPhraseAnchor` tracking for phrase-based processing
- Applies different time coefficients for attack/release:
  - Voice: levelEnvAttack = 0.035s, levelEnvRelease = 0.220s
  - General: levelEnvAttack = 0.080s, levelEnvRelease = 0.300s

**General Mode Implementation:**
- Separate `processGeneralMode()` function with dedicated signal processing
- Simpler decision logic based on `signalTrust`, `monoPenalty`, `compressionPenalty`
- Uses weighted level calculation: `0.06×low + 0.82×mono + 0.12×lowMid + 0.42×presence + 0.18×high`
- Different state transitions and confidence handling
- Offline analysis support for mix target curve optimization

**Key Differences:**
- Voice mode: stateful, phrase-aware, dynamic anchor tracking
- General mode: more stable, simpler heuristics, suitable for mixed/instrumental content

---

### 2. **Deverb** — Reverb Tail Handling
**File:** `Source/vxstudio/products/deverb/VxDeverbProcessor.cpp`

**Voice Mode:**
- `setVoiceMode()` / `isVoiceMode()` methods for mode management
- Vocal priority calculated as: `0.36×vocalDominance + 0.28×intelligibility + 0.18×phraseActivity + 0.10×speechPresence + 0.08×centerConfidence`
- Protects speech formants more aggressively in deverb spectral processing

**General Mode:**
- Less aggressive tail removal to preserve transient character of instruments
- Different tail likelihood weighting

**Spectral Processor:**
- Voice-mode-specific tail evidence weighting in frequency domain

---

### 3. **Denoiser** — Spectral Floor & Protection
**File:** `Source/vxstudio/products/denoiser/dsp/VxDenoiserDsp.cpp`

**Voice Mode Differences:**
- Aggression base: 0.42f (vs. 0.56f general)
- Late-tail aggression boost: 0.76f (vs. 1.08f general)
- Global floor range: 0.010–0.085f (vs. 0.0045–0.085f general)
- Higher speech band protection: `speechBandWeight × speechFocus` (vs. `0.45×speechFocus`)
- Stricter gain floor: 0.16f minimum (vs. 0.12f general)
- Higher speech floor: up to 0.18f (vs. 0.07f general)
- More generous restoration: 
  - Tonalness boost: 0.28f (vs. 0.18f)
  - Transient boost: 0.34f (vs. 0.20f)
  - Speech-weighted restoration: 0.18f (vs. 0.10f)

---

### 4. **Cleanup** — Artifact Suppression Intensity
**File:** `Source/vxstudio/products/cleanup/VxCleanupProcessor.cpp`

**Voice Mode Adjustments:**
```
contentMode = voiceMode ? 0 : 1
```

**Sibilance (deEss):**
- Voice: 1.26× intensity scaling (vs. 1.14× general)
- Character control: 0.92× (vs. 0.78× general)

**Breath Suppression:**
- Voice: 0.90× (vs. 0.56× general)
- More conservative to preserve natural breathing

**Plosive Suppression:**
- Voice: 1.08× (vs. 0.86× general)
- Enhanced focus with voiceMode protection

**Harsh/High-Band Suppression:**
- Voice: 1.22× (vs. 1.10× general)
- Higher Q for more targeted processing

**Voice Preserve Parameter:**
```cpp
0.56f + 0.30f×(voiceMode ? sourceProtect : 0.55×sourceProtect) + ...
```
- Much higher voice preservation boost in voice mode

---

### 5. **Tone** — EQ Curve Adaptation
**File:** `Source/vxstudio/products/tone/VxToneProcessor.cpp`

**Voice Mode vs General:**

| Parameter | Voice | General |
|-----------|-------|---------|
| Max Gain | `kVocalMaxGainDb × (1 - 0.05×priority)` | `kGeneralMaxGainDb` |
| Mid Max Gain | `kVocalMidMaxGainDb` | `kGeneralMidMaxGainDb` |
| Bass Freq Range | 120–210 Hz | 72–125 Hz |
| Bass Shift | -28×priority - 24×excursion | -20×excursion only |
| Mid Freq Range | 1200–3500 Hz | 600–1600 Hz |
| Mid Shift | +600×priority + 300×excursion | -200×excursion |
| Treble Freq Range | 5000–6900 Hz | 8600–12000 Hz |
| Treble Shift | +760×priority + 420×excursion | +900×excursion |
| Mid Q | 0.85 | 0.70 |
| Output Trim Limit | 6.0 dB | 7.0 dB |

**Vocal Priority:** `0.40×vocalDominance + 0.30×intelligibility + 0.20×transientRisk + 0.10×speechPresence`

---

### 6. **OptoComp** — Compressor Adaptation
**File:** `Source/vxstudio/products/OptoComp/VxOptoCompProcessor.cpp`

**Vocal Priority:**
```cpp
0.38×vocalDominance + 0.26×intelligibility + 0.18×phraseActivity + 
0.10×speechPresence + 0.08×centerConfidence
```

**Peak Reduction Scaling:**
```cpp
voiceMode ? (1.05f - 0.08f×priority + 0.04f×buriedSpeech) : 1.08f
```
- Voice mode: reduced compression for natural vocal dynamics
- Priority reduces reduction to protect loud vocal passages

**contentMode = voiceMode ? 0 : 1** → DSP algorithm changes

---

### 7. **Finish** — Limiter & Polish
**File:** `Source/vxstudio/products/finish/VxFinishProcessor.cpp`

**Vocal Priority:** (same formula as OptoComp)

**Peak Reduction:**
```cpp
voiceMode ? (1.0f - 0.10f×priority + 0.06f×buriedSpeech) : 1.0f
```
- General mode: standard 1.0× (no adaptive scaling)
- Voice mode: reduced for dynamics, increased for buried speech

**contentMode = voiceMode ? 0 : 1** → Algorithm selection

---

### 8. **Subtract** — Spectral Subtraction Control
**File:** `Source/vxstudio/products/subtract/dsp/VxSubtractDsp.cpp`

**Speech Protection Masking:**
```cpp
protectMask = voiceMode ? 1.0f : 0.55f
```
- Voice mode: stronger mask protection

**Subtraction Alpha Reduction:**
```cpp
(voiceMode ? 0.48f : 0.32f) × protectMask - (voiceMode ? 0.34f : 0.0f) × speechProt
```

**Speech Floor:**
```cpp
speechFloor = voiceMode ? lerp(0.01f, 0.14f, speechProt) : 1.0e-4f
```

**Gain Relaxation for Speech:**
```cpp
voiceMode: lerp(gSub, 1.0f, 0.20×speechProt)
```
- Prevents over-subtraction that would mute speech

---

### 9. **Corrective Stage** (Framework Level)
**File:** `Source/vxstudio/framework/VxStudioCorrectiveStage.cpp`

**Uses voiceMode for:**
- Corrective EQ coefficient adjustment
- Breath/sibilance protection intensity
- Filter bandwidth adaptation

---

### 10. **Finish DSP** (Framework Limiter)
**File:** `Source/vxstudio/framework/VxStudioFinishDsp.cpp`

**Peak Protection:**
```cpp
voiceMode ? 185 dB reduction threshold : 188 dB
```

**Attack/Release Time Modulation:**
- Voice mode: shorter times for natural transient handling

---

## Common Patterns (Framework + Effect-Specific)

### Framework Level — All Effects Implement This

**1. Mode Detection (Universal)**
```cpp
const bool voiceMode = readMode(parameters, productIdentity) == Mode::vocal;
```

**2. Vocal Priority Calculation (Universal)**
Each effect calculates priority from voice context:
```cpp
vocalPriority = voiceMode 
    ? clamp01(wA*vocalDominance + wB*intelligibility + wC*phraseActivity + ...)
    : 0.0f;
```

**3. ProcessOptions Construction (Universal Framework Pattern)**
```cpp
ProcessOptions options {};
options.isVoiceMode = voiceMode;
options.sourceProtect = voiceMode 
    ? clamp01(baseVoiceValue + scaleVoice * priority + policy_modulation)
    : clamp01(baseGeneralValue + scaleGeneral * priority + policy_modulation);
options.guardStrictness = voiceMode ? higher_voice_values : lower_general_values;
options.speechFocus = voiceMode ? strong_focus : weak_focus;
options.lateTailAggression = from_mode_policy;
```

### Effect-Specific Modifications (Non-Universal)

**1. contentMode Flag**
Framework-level effects use `contentMode`:
```cpp
params.contentMode = voiceMode ? 0 : 1;  // Switches CorrectiveStage, Cleanup, etc
```

**2. Effect-Specific Parameter Scaling**
- **Tone**: EQ frequency ranges shift based on `vocalPriority`
- **Leveler**: State machine detects 4 modes (neutral, voiceLeading, etc)
- **OptoComp/Finish**: Compressor gain adaptation
- **Denoiser**: Spectral floor multipliers  
- **Cleanup**: Artifact suppression coefficients per type
- **Subtract**: Blindfold aggression scaling

**3. Signal Processing Time Constants**
Voice mode differences (where applicable):
- **Leveler**: levelEnvAttack voice=0.035s vs general=0.080s
- **Denoiser**: global floor voice=0.010f vs general=0.0045f
- **Cleanup**: protection scaling differs per artifact type

### Key Insight: Separation of Concerns

| Responsibility | Layer | Implementation |
|---|---|---|
| Voice vs General baseline | Framework | `ProcessOptions` struct with defaults |
| Mode-aware protection scaling | Framework | Universal vocal priority formula |
| Effect-specific adaptation | Product | Mode-specific coefficients, state machines |
| Algorithm selection | DSP Layer | `contentMode` flag → changes filter curves, aggressiveness |

---

## Signal Flow Context

All effects receive voice context from `ProcessorBase`:
- `getVoiceContextSnapshot()` → `VoiceContextSnapshot`
- `getVoiceAnalysisSnapshot()` → `VoiceAnalysisSnapshot`

Key context fields used:
- `vocalDominance` - how strong the vocal presence is
- `intelligibility` - speech clarity
- `phraseActivity` / `phraseStart` / `phraseEnd` - phrase boundaries (Leveler only)
- `buriedSpeech` - how much speech is masked by instruments
- `speechPresence` - presence of speech-like content
- `centerConfidence` - if voice is in center/mono
- `stereoSpread` - stereo width (Leveler)
- `transientRisk` / `transientStrength` - attack characteristics

---

## Architectural Insights

### Framework-First Design
All effects share:
- **ProcessOptions baseline** — defines the common protection envelope
- **Vocal priority formula** — quantifies how "vocal" the content is
- **Mode policy** — ModePolicy object provides per-product voice/general tuning

Each product then **only adds what it needs** on top:

### Simple Adaptation (Parameter Scaling Only)
Effects that just scale existing parameters:
- **Tone** — Shifts EQ curves with vocal priority
- **OptoComp/Finish** — Scales compressor gain with priority
- **Subtract** — Adjusts noise subtraction aggressiveness

### Complex Adaptation (State Machine + Scaling)
Effects with content-specific logic:
- **Leveler** — 4-state machine (neutral, voiceLeading, guitarDominant, voiceBuried)
- **Deverb** — Reverb tail threshold changes per mode
- **Cleanup** — per-artifact-type coefficients (sibilance, breath, plosive, harsh)
- **Denoiser** — Spectral floor multipliers + speech preservation blend

---

## Testing Implications

### Voice Mode Test Cases
- Tight vocal takes (should preserve dynamics)
- Buried vocals (should enhance)
- Breathy voices (should not mute breath)
- Sibilant voices (should not over-suppress)
- Multi-vocal stacks (should preserve separation)

### General Mode Test Cases
- Instrumental music (should not color tone)
- Podcasts with background (should be resilient)
- Mixed speech+music (should balance both)
- Test signals (pink noise, drums) (should be neutral)

---

## Guidelines for New Effects

### Framework Baseline (Required for All)
1. **Detect mode** via `readMode(parameters, productIdentity) == Mode::vocal`
2. **Calculate vocal priority** from 4–5 voice context metrics (copy formula from similar effect)
3. **Build ProcessOptions** with framework-aware defaults:
   ```cpp
   options.isVoiceMode = voiceMode;
   options.sourceProtect = voiceMode ? (0.5f + 0.3f*priority) : 0.3f;
   options.guardStrictness = voiceMode ? (0.6f + 0.2f*priority) : 0.3f;
   // ... etc
   ```
4. **Use currentModePolicy()** to get product-specific voice/general tuning

### Effect-Specific Adaptation (Add Only What's Needed)
1. **If just scaling**: multiply/blend parameters based on voiceMode flag
2. **If complex**: implement state machines or per-artifact coefficients
3. **Always protect speech floors** more strictly in voice mode (use `sourceProtect`)
4. **Reduce aggression** at high vocal priority to preserve dynamics
5. **Use contentMode flag** (0=voice, 1=general) when passing to shared DSP

### Don't Over-Engineer
- ❌ Don't create independent voice/general paths if scaling works
- ❌ Don't hardcode protection values — use ProcessOptions framework
- ✅ Do copy vocal priority formula from existing similar effect
- ✅ Do test with real vocal content to verify priority weighting

### Testing Strategy
- **Tight vocal takes** — should preserve dynamics (voice mode)
- **Buried vocals** — should enhance (high vocal priority in ProcessOptions)
- **Breathy/sibilant voices** — should not over-suppress
- **Instrumental music** — should be neutral (general mode uses 0.0f priority)
- **Test signals** (pink noise, drums) — should bypass or be neutral
