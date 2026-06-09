Yes. I would now move to **v2 for all four modules**, not just de-click.

The current versions are useful stepping stones, but “best in class without AI” means each module needs to become more event-aware and less threshold-reactive. Your current de-esser and plosive are still mostly continuous reducers, while de-breath is still sample-score driven. The de-clicker should also move beyond simple short click repair into separate hard-click and mouth-click behaviour.

Below is the prompt I would give the agent.

---

# Agent prompt: V2 non-AI vocal cleanup architecture

You are working on a JUCE/C++ VST vocal cleanup plugin.

The plugin has these DSP modules:

```text
VxDeClickDsp
VxDePolosiveDsp
VxDeBreathDsp
VxDeEsserDsp
```

The goal is to upgrade all modules to **best-in-class non-AI v2** using deterministic DSP only.

Do not use:

```text
AI
ML
neural inference
model files
external DSP libraries
cloud processing
runtime allocations inside process()
```

Keep the existing public control model:

```text
Click dial
Plosive dial
Breath dial
Sibilance dial
Speech / General mode
```

Do not rename `DePolosiveDsp`.

---

# 1. Required processing order

The modules must run in this order:

```cpp
deClick.process(buffer, clickParams);
dePolosive.process(buffer, plosiveParams);
deBreath.process(buffer, breathParams);
deEsser.process(buffer, sibilanceParams);
```

Do not change this order.

Reason:

```text
DeClick repairs tiny waveform defects first.
DePolosive controls low-frequency pressure bursts.
DeBreath attenuates longer breath/noise regions.
DeEsser handles high-frequency consonant excess last.
```

---

# 2. Shared mode support

Use a single shared header:

```text
VxCleanupMode.h
```

```cpp
#pragma once

namespace vxsuite {
namespace speech_clarity {

enum class CleanupMode {
    Speech,
    General
};

} // namespace speech_clarity
} // namespace vxsuite
```

Every module must have:

```cpp
void setMode(CleanupMode newMode) noexcept;
```

Every module must store:

```cpp
CleanupMode mode = CleanupMode::Speech;
```

Do not duplicate the enum in multiple files.

---

# 3. Control philosophy

Each dial is `0.0f` to `1.0f`.

A dial must not be a raw threshold. It must control:

```text
sensitivity
maximum repair/reduction
duration limits
false-positive protection
timing
naturalness
```

General behaviour:

```text
0.00 = bypass
0.25 = light cleanup
0.50 = natural default
0.75 = strong cleanup
1.00 = aggressive but still controlled
```

---

# 4. DeClick v2: hard click + mouth click repair

The current de-clicker should be upgraded into a two-lane repair system.

## Purpose

The module must repair:

```text
tongue clicks
lip ticks
saliva clicks
short wet mouth noises
small edit clicks
tiny digital ticks
```

It must not repair:

```text
normal T/K/CH consonants
sibilance
plosives
breaths
instrument attacks
```

## Required architecture

Inside `VxDeClickDsp`, implement two internal detection lanes:

```text
HardClick lane:
  very short ticks, edit clicks, tongue ticks
  typical duration: 0.3 ms to 5 ms

MouthClick lane:
  lip smacks, saliva ticks, small wet mouth noises
  typical duration: 3 ms to 30 ms
```

Both lanes share the same output repair buffer.

## Latency

Use mode-dependent latency:

```text
Speech mode:
  15 ms lookahead

General mode:
  8 ms lookahead
```

Clamp:

```cpp
lookaheadSamples = juce::jlimit(128, 1024, lookaheadSamples);
```

Expose:

```cpp
int getLatencySamples() const noexcept;
```

The processor must call:

```cpp
setLatencySamples(deClick.getLatencySamples());
```

when de-click is active.

## HardClick lane

Detector:

```text
high-pass residual
first-difference slope
median deviation
very short duration
```

Settings:

```cpp
hardHpCutoffHz = mode == Speech ? 2200.0f : 3000.0f;
hardMedianWindow = 9;
hardFastMs = 0.25f;
hardSlowMs = 12.0f;
hardMaxMs = mode == Speech ? 5.0f : 3.5f;
```

Candidate:

```cpp
start when score > 0.70f
continue while score > 0.35f
confirm if length >= 2 samples && length <= hardMaxSamples
```

Repair:

```text
cubic Hermite / Catmull-Rom interpolation
short pre/post margin
high repair blend
```

Margins:

```cpp
hardPreMarginMs  = 0.35f;
hardPostMarginMs = 0.70f;
```

## MouthClick lane

Detector:

```text
lower high-pass residual
median deviation
local roughness
short-region energy burst
duration guard
```

Settings:

```cpp
mouthHpCutoffHz = mode == Speech ? 1200.0f : 2200.0f;
mouthMedianWindow = 15;
mouthFastMs = 0.8f;
mouthSlowMs = 30.0f;
mouthMaxMs = mode == Speech ? 30.0f : 10.0f;
mouthMinMs = 1.5f;
```

Candidate:

```cpp
start when score > 0.62f
continue while score > 0.30f
confirm if length between mouthMinSamples and mouthMaxSamples
```

Repair:

For short regions under 8 ms:

```text
cubic interpolation across full region
```

For longer mouth regions above 8 ms:

```text
multi-point interpolation with crossfaded repair
or median-smoothed residual replacement
```

Required simple implementation for longer regions:

```text
1. Generate cubic interpolation between clean boundary samples.
2. Apply a raised-cosine blend so the repair is strongest at the centre.
3. Do not fully replace the whole region unless strength is high.
```

Repair amount:

```cpp
baseRepairBlend = strength;
```

Speech mode:

```cpp
hardRepairBlend  = strength > 0.001f ? 0.35f + 0.65f * strength : 0.0f;
mouthRepairBlend = strength > 0.001f ? 0.25f + 0.65f * strength : 0.0f;
```

General mode:

```cpp
hardRepairBlend  = strength;
mouthRepairBlend = strength * 0.60f;
```

## DeClick false-positive protection

Implement all of these:

```text
cooldown after repair
duration rejection
reject long sustained high-frequency events
reject low-frequency-dominant events
do not repair if region resembles plosive
do not repair if event is too periodic/harmonic
```

Minimum guards:

```cpp
lowBandRatioGuard:
  if lowEnv / fullEnv > 0.55f, reduce click score by 70%

sustainedGuard:
  if candidate exceeds max duration, discard and cooldown

cooldown:
  4 ms after hard click
  8 ms after mouth click
```

## DeClick output metering

Expose optional getters:

```cpp
float getLastHardClickReduction() const noexcept;
float getLastMouthClickReduction() const noexcept;
int getRecentRepairCount() const noexcept;
```

These are for UI/debug. They must not allocate.

---

# 5. DePolosive v2: dynamic high-pass crossfade

The current plosive module uses low-pass extraction and low-band subtraction. That is acceptable for v1, but v2 must move to **dynamic high-pass crossfade**.

The current class already tracks low/full envelopes and applies low-band reduction.  Replace the processing stage, not the whole idea.

## Required DePolosive v2 architecture

Detector:

```text
low band: 20 Hz to 180 Hz
mid band: 250 Hz to 1500 Hz
low/mid ratio
low-band onset speed
duration guard
```

Processor:

```text
normal high-pass signal
strong high-pass signal
crossfade toward strong HPF only during plosive
```

Do not broadband duck.

## Filters

Add:

```text
detector low-pass at 180 Hz
detector band-pass or high/low pair for 250-1500 Hz
normal HPF
strong HPF
```

Settings:

Speech mode:

```cpp
normalHpfHz = 70.0f;
strongHpfMinHz = 140.0f;
strongHpfMaxHz = 260.0f;
```

General mode:

```cpp
normalHpfHz = 50.0f;
strongHpfMinHz = 100.0f;
strongHpfMaxHz = 180.0f;
```

## Plosive score

Replace:

```cpp
lowEnv / fullEnv
```

with:

```cpp
lowEnv / max(midEnv, 1e-6f)
```

Score:

Speech mode:

```cpp
ratioScore = smoothStep(1.8f, 5.0f, lowMidRatio);
onsetScore = smoothStep(1.5f, 4.0f, lowOnsetRatio);
score = upstream * (0.75f * ratioScore + 0.25f * onsetScore);
```

General mode:

```cpp
ratioScore = smoothStep(2.5f, 7.0f, lowMidRatio);
onsetScore = smoothStep(2.0f, 5.5f, lowOnsetRatio);
score = upstream * (0.80f * ratioScore + 0.20f * onsetScore);
```

Clamp score to `0..1`.

## Dynamic HPF amount

Use the dial to control HPF strength:

```cpp
modeStrength = mode == Speech ? strength : strength * 0.65f;
```

Compute target cutoff:

```cpp
targetStrongHz = strongHpfMinHz
               + (strongHpfMaxHz - strongHpfMinHz) * modeStrength * score;
```

Crossfade amount:

```cpp
xfade = score * modeStrength;
```

Output:

```cpp
normal = normalHpf.process(x);
strong = dynamicStrongHpf.process(x, targetStrongHz);
y = normal * (1.0f - xfade) + strong * xfade;
```

If dynamic coefficient recalculation per sample is too expensive, smooth the cutoff and recalculate coefficients once per small control block, not per sample.

## Timing

Speech mode:

```cpp
attackMs = 1.0f;
holdMs = 35.0f;
releaseMs = 160.0f;
```

General mode:

```cpp
attackMs = 2.0f;
holdMs = 20.0f;
releaseMs = 100.0f;
```

Implement a simple hold counter so the plosive does not flutter.

## Plosive acceptance

At `Plosive = 0.50` in Speech mode:

Pass:

```text
P/B low thumps reduce clearly
voice body remains warm
no low-end flutter
no general thinning
no broadband level dips
```

Fail:

```text
whole voice gets high-passed
plosive onset survives untouched
low end pumps after each word
```

---

# 6. DeBreath v2: region state machine

The current de-breath has useful ingredients but is still sample-score driven. It also currently has a speech reference attack set to 3 seconds in the uploaded file, which must be fixed. 

V2 must become a **region-based breath attenuator**.

## Immediate required fix

Replace:

```cpp
speechRefAttCoeff = std::exp(-1.0f / (fs * 3.000f));
```

with:

```cpp
speechRefAttCoeff = std::exp(-1.0f / (fs * 0.030f));
```

Keep:

```cpp
speechRefRelCoeff = std::exp(-1.0f / (fs * 1.200f));
```

Replace:

```cpp
0.25f + 0.75f * ...
```

with:

```cpp
0.10f + 0.90f * ...
```

## Required DeBreath v2 architecture

Add state machine:

```cpp
enum class BreathState {
    Idle,
    Candidate,
    Active,
    Release
};
```

Per-channel state:

```cpp
BreathState state = BreathState::Idle;
int candidateSamples = 0;
int activeSamples = 0;
int releaseSamples = 0;
float regionScore = 0.0f;
float currentGain = 1.0f;
```

## Candidate rules

A breath candidate starts when:

```cpp
instantScore > candidateStartThreshold
```

Candidate continues while:

```cpp
instantScore > candidateContinueThreshold
```

Candidate becomes Active only if:

```text
candidateSamples >= minBreathSamples
```

Settings:

Speech mode:

```cpp
minBreathMs = 90.0f;
maxBreathMs = 1600.0f;
candidateStartThreshold = 0.55f;
candidateContinueThreshold = 0.30f;
```

General mode:

```cpp
minBreathMs = 180.0f;
maxBreathMs = 1000.0f;
candidateStartThreshold = 0.75f;
candidateContinueThreshold = 0.45f;
```

If candidate is shorter than min duration:

```text
discard it
do not attenuate
```

This protects `h`, `f`, `s`, `th` and soft word starts.

## Breath score

Use existing components:

```text
breathBandScore
lowPenalty
levelScore
quietComparedToSpeech
```

Current de-breath already calculates these concepts. 

Compute:

```cpp
instantScore =
    upstream
  * breathBandScore
  * quietComparedToSpeech
  * (1.0f - 0.85f * lowPenalty)
  * levelScore;
```

Add a phrase-boundary score:

```cpp
speechWasRecentlyHigh = smoothStep(0.0005f, 0.005f, speechRefEnv);
currentBelowSpeech = 1.0f - smoothStep(0.45f, 0.90f, relativeLevel);
phraseScore = speechWasRecentlyHigh * currentBelowSpeech;
```

Then:

```cpp
instantScore *= 0.70f + 0.30f * phraseScore;
```

## Breath reduction

Do not exceed:

Speech mode:

```cpp
maxReductionDb = -15.0f * strength;
```

General mode:

```cpp
maxReductionDb = -6.0f * strength;
```

Timing:

Speech mode:

```cpp
attackMs = 80.0f;
releaseMs = 350.0f;
```

General mode:

```cpp
attackMs = 120.0f;
releaseMs = 500.0f;
```

During Active:

```cpp
targetGain = dbToGain(maxReductionDb * regionScore);
```

During Release:

```cpp
targetGain = 1.0f;
```

## Breath acceptance

At `Breath = 0.50` in Speech mode:

Pass:

```text
audible breaths are reduced
breaths are not chopped
word starts remain intact
h/f/s/th remain intact
room tone does not pump
voice does not become airless
```

Fail:

```text
short consonants are reduced
breath module dominates all.wav
general phrase level ducks
```

---

# 7. DeEsser v2: dual-band sibilance control

The current de-esser uses one bandpass around 6.5 kHz and subtracts the reduced band.  V2 must become dual-band and use a speech-body reference.

## Required architecture

Add three detector bands:

```text
body band: 700 Hz to 3500 Hz
lower sibilance band: 4.2 kHz to 6.5 kHz
upper sibilance band: 6.5 kHz to 10.5 kHz
```

Processing bands:

```text
lower sibilance extraction
upper sibilance extraction
separate gain for each
subtractive recombination
```

Do not use broadband compression.

## Detector

Compute:

```cpp
lowerRatio = lowerSibEnv / max(bodyEnv, 1e-6f);
upperRatio = upperSibEnv / max(bodyEnv, 1e-6f);
```

Speech mode:

```cpp
lowerScore = smoothStep(0.35f, 1.20f, lowerRatio) * upstream;
upperScore = smoothStep(0.30f, 1.10f, upperRatio) * upstream;
```

General mode:

```cpp
lowerScore = smoothStep(0.55f, 1.60f, lowerRatio) * upstream;
upperScore = smoothStep(0.50f, 1.50f, upperRatio) * upstream;
```

Clamp each score.

## Reduction

Speech mode:

```cpp
lowerMaxDb = -7.0f * strength;
upperMaxDb = -10.0f * strength;
```

General mode:

```cpp
lowerMaxDb = -5.0f * strength * 0.70f;
upperMaxDb = -7.0f * strength * 0.70f;
```

Timing:

```cpp
attackMs = 1.0f;
releaseMs = mode == Speech ? 55.0f : 35.0f;
```

Output:

```cpp
y = x
  - lowerBand * (1.0f - lowerGain)
  - upperBand * (1.0f - upperGain);
```

## Add tone bias internally

No new UI control yet.

Speech mode:

```text
balanced lower/upper response
```

General mode:

```text
bias toward upper band only, to avoid dulling music
```

Implement simply by multiplying lowerScore in General mode:

```cpp
lowerScore *= 0.75f;
```

## DeEsser acceptance

At `Sibilance = 0.50` in Speech mode:

Pass:

```text
S/SH/CH harshness reduces
no obvious lisp
vowels remain bright
air remains natural
```

Fail:

```text
whole voice dulls
lower mids are affected
sibilance remains unchanged
vowels after S duck
```

---

# 8. Event ownership v2

Add a lightweight shared event mask only if it does not require major architecture changes.

Preferred shared structure:

```cpp
struct CleanupFrameFlags {
    float click = 0.0f;
    float plosive = 0.0f;
    float breath = 0.0f;
    float sibilance = 0.0f;
};
```

If adding this is too intrusive, skip it for now and rely on chain order.

If implemented:

```text
DeClick should suppress DeBreath/DeEsser reaction for repaired windows.
DePolosive should suppress DeBreath reaction during low-frequency burst windows.
DeBreath should not suppress DeEsser unless the region is clearly non-speech breath.
```

Do not let event masking create audible holes.

---

# 9. Metering and debug hooks

Each module should expose simple non-allocating getters.

## DeClick

```cpp
float getLastHardClickRepair() const noexcept;
float getLastMouthClickRepair() const noexcept;
int getRecentRepairCount() const noexcept;
```

## DePolosive

```cpp
float getLastReductionDb() const noexcept;
float getLastPlosiveScore() const noexcept;
```

## DeBreath

```cpp
float getLastReductionDb() const noexcept;
float getLastBreathScore() const noexcept;
bool isBreathActive() const noexcept;
```

## DeEsser

```cpp
float getLastLowerReductionDb() const noexcept;
float getLastUpperReductionDb() const noexcept;
```

These are for UI/debug only.

Do not allocate.

Do not use atomics unless the project already uses them for meter values.

---

# 10. Source mode defaults

Speech mode defaults:

```text
Click:      0.45
Plosive:    0.45
Breath:     0.35
Sibilance:  0.40
```

General mode defaults:

```text
Click:      0.35
Plosive:    0.20
Breath:     0.00
Sibilance:  0.25
```

---

# 11. Acceptance renders

Render or test with:

```text
raw.wav
click.wav
plosive.wav
breath.wav
sibilance.wav
all.wav
```

## Global pass criteria

At default Speech settings:

```text
cleaner than raw
no obvious pumping
no broad dulling
no airless speech
no lisping
no low-end thinning across normal speech
clicks repaired rather than ducked
combined processing does not sound like one module dominates
```

## Global fail criteria

```text
clicks become thuds
word starts disappear
S/T/K consonants are softened by click or breath modules
breath module ducks whole phrases
plosive module high-passes the whole vocal
de-esser dulls the entire voice
latency is not compensated
process() allocates memory
```

---

# 12. Implementation phases

Do this in order.

## Phase 1: shared mode and latency

```text
add VxCleanupMode.h
add setMode() to all modules
ensure processor reports DeClick latency
ensure module order is correct
```

## Phase 2: DeClick v2

```text
split hard-click and mouth-click lanes
add separate thresholds and durations
add long mouth repair blend
add false-positive guards
add meters
```

## Phase 3: DePolosive v2

```text
add mid-band reference
replace low/full ratio with low/mid ratio
replace LP subtraction with dynamic HPF crossfade
add hold logic
add meters
```

## Phase 4: DeBreath v2

```text
fix speechRef attack to 30 ms
add BreathState machine
enforce min/max duration
add phrase-boundary score
add meters
```

## Phase 5: DeEsser v2

```text
add body band
add lower and upper sibilance bands
separate lower/upper reduction
add meters
```

Do not skip phases unless compilation requires a different order.

---

# 13. Final non-negotiables

```text
No AI.
No ML.
No runtime allocations inside process().
No hard breath mute.
No broadband plosive ducking.
No broadband de-essing.
No click gain-ducking as the main repair.
No renaming DePolosiveDsp.
No changing public Params structs unless asked.
No silent architectural drift.
```

---

## Strategic note

Yes, this is the right direction. The current modules are no longer the target; they are the baseline.

The real v2 target is:

```text
DeClick:
  two-lane short-region repair

DePolosive:
  dynamic high-pass crossfade

DeBreath:
  region state machine

DeEsser:
  dual-band dynamic sibilance control
```

That is the point where the plugin starts to behave like a serious non-AI restoration system rather than a set of smart gates.
