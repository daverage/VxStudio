# VX Width — Phase 1.2 Stereo Width + Stereo Double Engine Correction

The current Phase 1 / 1.1 engine still fails the original real-world acceptance case on genuine stereo material.

Observed behaviour:

* Mono material sounds great and must not be broken.
* A mono guitar processed by VX Width, rendered to stereo, then processed again can become slightly wider.
* A genuine stereo singer recording shows effectively no audible increase in Width or Double.
* On that real stereo vocal, positive Width narrows correctly toward mono on the negative side, but the positive half saturates:

  * around the vertical/halfway-positive position it already sounds as wide as it gets;
  * turning Width fully clockwise to +100 produces little or no further audible widening.
* Double on that same real stereo vocal is also effectively inaudible.

This is not a UI issue and not an analyser issue anymore.

Treat this as a DSP engine-policy correction.

Do NOT begin Phase 2 UI work until this is solved.

---

# 1. Preserve the mono path

The current mono Width / Double sound is considered successful.

Hard requirement:

> For effectively mono input, the current production mono behaviour must remain unchanged unless a change is explicitly proven to improve it.

Do not redesign the mono widening engine.

Do not weaken the existing mono safety work just to make stereo fixtures wider.

Existing mono regressions should remain sample-identical where practical, or within the already-defined tolerances if exact identity is impossible.

---

# 2. Current root cause — positive Width saturates Region B at 35%

The current processor contains:

```cpp
constexpr float kRegionBCeiling = 35.0f;
constexpr float kRegionBMaxGainDb = 4.5f;
```

and:

```cpp
float expandedSideGain(const float widthGapPercent,
                       const float monoRiskRestraint) noexcept
{
    const float clamped =
        juce::jlimit(0.0f, kRegionBCeiling, widthGapPercent);

    const float gainDb =
        (clamped / kRegionBCeiling)
        * kRegionBMaxGainDb
        * juce::jlimit(0.0f, 1.0f, monoRiskRestraint);

    return juce::Decibels::decibelsToGain(gainDb);
}
```

This means direct existing-Side widening becomes constant as soon as:

```text
widthGapPercent >= 35
```

For an input estimated around W0=0.30:

```text
Width +50:
gap = (1 - .30) * 50 = 35
```

Region B is already at maximum.

At Width +100:

```text
gap = (1 - .30) * 100 = 70
```

Region B is still exactly the same +4.5 dB.

Therefore the actual existing stereo content is no wider at +100 than +50.

Only Region C continues to increase.

That exactly matches the listening result.

This is the first issue to fix.

---

# 3. Existing stereo Side must carry the full positive Width journey

For genuinely stereo input, positive Width must primarily act on the existing stereo image.

Required user behaviour:

```text
Width 0    = original input width
Width +25  = clearly wider
Width +50  = wider again
Width +75  = very wide
Width +100 = widest useful version of the existing image
```

The complete 0→+100 range must remain perceptually active.

Do NOT implement positive stereo Width as:

```text
Region B expands a bit
then stops
then generated decorrelation tries to do everything else
```

Instead:

```text
existing Side
    ↓
continuous target-driven expansion across 0→100
    +
supplementary generated width if required
```

Region C should support the final result, not define the second half of the Width control.

---

# 4. Keep mono and stereo Width strategies distinct but continuous

Do not globally remove the Region B ceiling if doing so changes the mono sound.

The correct architecture is content-aware blending between:

```text
mono-like input:
    preserve current proven generated-width architecture

stereo-like input:
    expand existing Side relative to current width
    continuously across the full positive range
```

Use continuous weighting rather than a hard branch if practical.

Conceptually:

```cpp
float stereoEvidence = ...; // 0 mono-like, 1 clearly stereo

float monoPathSide = existingCurrentMonoWidthBehaviour(...);

float stereoPathSide = newTargetDrivenExistingSideExpansion(...);

side =
    lerp(monoPathSide,
         stereoPathSide,
         stereoEvidence);
```

Do not literally implement this exact pseudocode if there is a cleaner architecture.

The behavioural requirement matters more than the exact structure.

---

# 5. Target-driven existing-Side expansion

Use the existing:

```text
estimatedInputWidth01 = W0
```

and requested control position:

```text
WidthSigned ∈ [0, +100]
```

to derive:

```text
requestedOutputWidth01 =
    W0 + (1 - W0) * (WidthSigned / 100)
```

This already exists conceptually.

The problem is that the actual Side gain does not continue tracking that target.

Design a direct existing-Side expansion law that maps:

```text
current Side/Mid relationship
    ↓
target width
    ↓
required Side scaling
```

The existing stereo path should attempt to move the measured image toward the requested target.

Do NOT pretend physical angle estimation is exact.

This is a perceptual target.

Possible internal structure:

```text
currentMeasuredWidth = W0
targetWidth = T

requiredExpansion =
    f(W0, T, currentMidRms, currentSideRms)
```

Then derive a bounded Side gain appropriate to the target.

Important:

* monotonic across the whole range;
* no abrupt handover at 35;
* no +50/+100 plateau;
* stable at 44.1–192 kHz;
* no block-size-dependent jumps.

---

# 6. Do not use unlimited Side gain

This is not permission to turn M/S Side gain into an unbounded maximiser.

The target-driven stereo path must still be safe.

Use reasonable limits based on:

* current Side/Mid ratio;
* correlation;
* mono downmix;
* phase risk;
* low-frequency risk;
* programme content.

But safety must act as an exceptional restraint.

It must not make the second half of the control routinely inactive.

If a safe source is only moderately wide, Width +100 should produce substantially more spatial extent than +50.

---

# 7. Region C becomes supplementary for stereo

For genuine stereo material:

```text
primary mechanism:
    expand existing Side

secondary mechanism:
    generated decorrelated width
```

Region C should fill remaining perceptual width where direct expansion alone cannot achieve the requested result safely.

Do not make Region C carry all width above the old 35% Region B ceiling.

For mono material, preserve the existing Region B/C behaviour unless required otherwise.

---

# 8. Diagnostic test — isolate Region B

Before tuning the new combined engine, add a developer/test-only diagnostic mode:

```text
Region C disabled
Double disabled
```

Run a genuine stereo fixture with meaningful existing Side:

```text
Width 0
Width +25
Width +50
Width +75
Width +100
```

Measure final Side/Mid or equivalent output-width metric.

Required:

```text
W(+25)  > W(0)
W(+50)  > W(+25)
W(+75)  > W(+50)
W(+100) > W(+75)
```

This test must pass through existing-Side expansion alone.

Do not proceed to generated-width tuning until this is true.

---

# 9. Current root cause — Double is centre-gated too aggressively

Current code:

```cpp
constexpr float kDoubleCentreGateFloor = 0.25f;

const float doubleCentreGate =
    kDoubleCentreGateFloor
    + (1.0f - kDoubleCentreGateFloor)
      * juce::jlimit(0.0f, 1.0f, voice.centerConfidence);

const float doubleSideAmount =
    smoothed.secondary
    * doubleCentreGate
    * monoDownmixRestraint;

const float doubleMidAmount =
    smoothed.secondary
    * 0.35f
    * doubleCentreGate;
```

This means a real stereo recording with low centreConfidence can receive approximately:

```text
DoubleSide = only ~25% of requested amount
DoubleMid  = only ~8.75% at Double=100
```

before the rest of the generated-content safety chain.

This directly explains why Double can be effectively inaudible.

This is the second issue to fix.

---

# 10. Double knob must control effect amount

User contract:

```text
Double 0   = no synthetic performance
Double 100 = maximum useful natural doubling
```

The user's Double amount must remain the primary amplitude control.

Do NOT multiply the user's request by broadband centre confidence in a way that turns Double 100 into Double 25.

Centre confidence may influence:

* which part of the stereo signal is best to double;
* how much Mid vs stereo-derived material feeds the ADT;
* how spatially the generated voices are placed;
* how conservative safety processing should be.

But it must not simply make the effect disappear.

---

# 11. Diagnostic test — remove the centre gate temporarily

Add a developer/test-only diagnostic path equivalent to:

```cpp
doubleCentreGate = 1.0f;
```

Keep the existing ADT voices.

First test with the real/genuine stereo fixture.

Run:

```text
Double 0
Double 25
Double 50
Double 75
Double 100
```

Measure:

* raw Voice A RMS;
* raw Voice B RMS;
* DoubleMid RMS;
* DoubleSide RMS;
* final generated Side RMS;
* final output Side/Mid ratio;
* safety reductions.

If Double suddenly becomes clearly active, that proves the broadband centre gate is the dominant cause.

Then reintroduce content-awareness through a better routing strategy rather than simply restoring the attenuation.

---

# 12. ADT source limitation on stereo input

Current ADT generation is:

```cpp
voiceA = adtVoiceA.process(focusedMid, ...);
voiceB = adtVoiceB.process(focusedMid, ...);
```

So the double is generated entirely from:

```text
focusedMid = (L + R) / sqrt(2)
```

This works very well for:

* mono material;
* VX-generated stereo that still contains a strong common Mid.

It can work poorly for genuine stereo recordings whose useful vocal/instrument information is distributed differently across L/R.

Do not immediately switch to:

```text
ADT(L)
ADT(R)
```

That could destroy stereo geometry and create four-performer/chorus behaviour.

Instead investigate a stereo-aware ADT source strategy.

---

# 13. Stereo-aware Double source — preferred direction

Conceptually separate:

```text
common/centre information
+
stereo-specific information
```

The existing focused Mid remains the primary ADT source.

But when the input is genuinely stereo and Mid alone is insufficient, allow a restrained contribution derived from the stereo programme.

Possible strategies to prototype:

A. Mid + bounded Side-derived contribution

```text
adtSource =
    focusedMid
    +
    stereoContribution * focusedSide
```

where stereoContribution is small/content-aware.

B. Energy-normalised L/R common-content estimator

Derive a common source more robustly than simple L+R if the recording has channel imbalance or mic differences.

C. Centre extraction / correlation-weighted common component

Use L/R correlation and channel energy to estimate the coherent performance component without treating the entire Side as disposable ambience.

Do NOT implement a full source-separation system.

The goal is a lightweight stereo-aware ADT feed that makes a real stereo vocal respond to Double while preserving the original stereo recording.

---

# 14. Double should preserve original stereo geometry

Double must add another performance without re-panning or collapsing the existing recording.

Original stereo:

```text
L/R dry geometry
```

must remain intact.

Generated Double sits alongside it.

Conceptually:

```text
original stereo unchanged
        +
generated differentiated performance
```

The generated layer can be spatially placed according to Width.

Do not treat Double as another existing-Side multiplier.

---

# 15. Width and Double remain independent

Hard user-level contract:

## Width

Answers:

> How wide should the presentation be?

## Double

Answers:

> How much extra performance should there be?

Required cases:

### Width +100 / Double 0

Very wide existing image, no significant double-tracked character.

### Width 0 / Double 100

Strong double-tracked character while preserving roughly original spatial extent.

### Width +100 / Double 100

Maximum useful width plus maximum natural double-track character.

### Stereo source

Both controls must remain clearly active.

---

# 16. Do not weaken the safety systems globally

Retain:

* SideOrthogonalizer;
* ContentPredictabilityRestraint;
* MonoDownmixGuardrail;
* transient protection;
* low-frequency protection;
* Focus shaping;
* loudness compensation;
* Doppler limits;
* optional experimental micro-pitch.

However, trace exactly how much each layer removes.

The safety systems should protect pathological cases.

They must not routinely nullify a valid user request.

---

# 17. Instrument the complete Width path

Add debug/test telemetry for:

```text
estimatedInputWidth01
requestedOutputWidth01

inputMidRms
inputSideRms

stereoEvidence / monoEvidence

requestedExistingSideGain
actualExistingSideGain

regionBContributionRms

widthGapPercent
decorBlend

decorRawRms
decorPostSafetyRms

monoRiskRestraint
monoDownmixRestraint
predictabilityRestraint
orthogonalizerReductionDb

finalMidRms
finalSideRms
actualOutputWidth01
```

Do not infer what happened from one final metric.

We need visibility through the full chain.

---

# 18. Instrument the complete Double path

Add telemetry for:

```text
doubleRequest01

centerConfidence
oldDoubleCentreGate equivalent if retained for comparison

adtSourceMidRms
adtSourceStereoContributionRms

voiceARms
voiceBRms

doubleMidRawRms
doubleSideRawRms

requestedDoubleMidAmount
requestedDoubleSideAmount

doublePostOrthogonalizerRms
doublePostPredictabilityRms
doubleFinalContributionRms
```

This should make it immediately obvious if Double disappears:

* before generation;
* at routing;
* or in safety processing.

---

# 19. Use real stereo programme fixtures

Do not validate stereo behaviour primarily with synthetic or VX-generated stereo.

Those signals are useful regressions but biased toward the architecture.

Add representative genuine-stereo fixtures or equivalent deterministic stereo models representing:

* stereo vocal;
* stereo acoustic guitar;
* stereo piano;
* drum overhead-like material;
* stereo synth/pad;
* narrow stereo;
* moderate stereo;
* broad stereo;
* channel-imbalanced stereo;
* decorrelated room/ambience.

The genuine-stereo vocal behaviour is a primary acceptance case.

If private real recordings cannot live in the repository, build deterministic stereo fixtures that mimic:

* common vocal component;
* channel-specific room/reflection;
* small L/R gain/phase differences;
* modest decorrelated ambience.

---

# 20. Width acceptance tests

For a moderate genuine-stereo fixture:

```text
Width 0
Width 25
Width 50
Width 75
Width 100
```

must show monotonic actual width increase.

Hard requirement:

```text
actual(+100) must be materially greater than actual(+50)
```

Do not accept a technically positive but perceptually negligible difference.

Add a minimum separation threshold based on corpus evidence.

The exact threshold should be chosen from measured representative material, not invented blindly.

---

# 21. Double acceptance tests

For a genuine-stereo vocal-like fixture:

```text
Double 0
Double 25
Double 50
Double 75
Double 100
```

must produce monotonic generated-performance contribution.

Hard requirement:

```text
Double 100 must be clearly materially greater than Double 50
```

and cannot collapse merely because broadband centreConfidence is low.

---

# 22. Mono regression requirement

Run the current mono corpus unchanged.

Confirm:

* Width behaviour preserved;
* Double behaviour preserved;
* L/R imbalance still acceptable;
* generated-Side retention remains non-degenerate;
* mono fold-down remains safe;
* ADT sound path unchanged unless intentionally modified;
* full regression suite remains green.

If stereo correction breaks the mono sound, do not accept the implementation.

---

# 23. Do not tune toward meter numbers alone

The final judgement must include listening.

Required listening material:

* mono vocal;
* real/genuine stereo vocal;
* mono clean guitar;
* VX-rendered stereo guitar;
* naturally stereo guitar;
* piano;
* stereo pad;
* drums/overheads;
* full mix.

Listen specifically for:

Width:

* does every quarter-turn matter?
* does +100 clearly exceed +50?
* does the centre remain stable?
* does the mix become hollow or phasey?
* is widening smooth rather than suddenly changing character at Region C?

Double:

* is 25 subtle?
* is 50 obvious but natural?
* is 100 clearly maximum useful ADT?
* does real stereo finally respond?
* does it sound like a second performance rather than chorus/flange?

---



# 25. Deliverables

Implement Phase 1.2 and report:

1. The exact cause of the +50→+100 Width plateau confirmed by telemetry.
2. The replacement existing-stereo expansion law.
3. How mono behaviour was preserved.
4. Width measurements at:

   * 0
   * +25
   * +50
   * +75
   * +100
     for:
   * mono;
   * VX-generated stereo;
   * genuine/natural stereo fixtures.
5. Whether disabling Region C still allows monotonic positive Width on stereo.
6. The exact cause of stereo Double suppression confirmed by telemetry.
7. Results with `doubleCentreGate=1` diagnostic.
8. The new stereo-aware Double routing/source strategy if one was required.
9. Double contribution measurements at:

   * 0
   * 25
   * 50
   * 75
   * 100
     on mono and genuine stereo.
10. Orthogonalizer/predictability/mono-guardrail activity before and after.
11. Mono regressions.
12. Full test-suite results.
13. CPU/latency changes.
14. Any remaining perceptual limitations.

---

# 26. Definition of success

Do not declare this phase complete merely because tests pass.

It is successful when the original real-world complaint is no longer true:

> A genuinely stereo vocal responds clearly to both positive Width and Double.

Specifically:

* positive Width remains perceptually useful through the complete 0→100 range;
* +100 is clearly wider than +50;
* Double remains clearly audible on genuine stereo material;
* the existing excellent mono behaviour remains intact;
* safety systems still protect mono compatibility and imbalance;
* no UI trickery is used to conceal DSP saturation.

This is an engine correction, not a threshold patch.
