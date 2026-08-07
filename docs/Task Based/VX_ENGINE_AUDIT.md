# VX Width — Enforce Strict Control Ownership

## Objective

Cleanly enforce this product rule throughout the current VX Width DSP:

> **Width is the only user control allowed to directly alter spatial width.**

The four controls have distinct responsibilities:

```text
Width
    = spatial extent / geometry

Double
    = amount of additional synthetic performance

Tightness
    = how closely the generated performance follows the source

Focus
    = where in the spectrum the generated performance is concentrated
```

The current codebase has already started moving in this direction. Do not blindly redesign it.

Audit the actual current signal path and finish the separation so that the invariant is structurally true and regression-tested.

---

# 1. Required product semantics

## Width

Width is the sole owner of:

* existing Side expansion;
* narrowing toward mono;
* generated/decorrelated widening;
* spatial placement/separation of generated Double voices;
* target output width.

If actual stereo extent changes because of a deliberate user control, that control must be Width.

## Double

Double controls only:

> How much additional performance is present?

Increasing Double may make the sound perceptually richer or denser, but it must not directly request a wider stereo image.

## Tightness

Tightness controls only characteristics of the generated Double, such as:

* delay range;
* timing independence;
* delay movement;
* Doppler budget;
* gain variation;
* micro-performance variation.

It must not alter the Width engine.

## Focus

Focus controls only the spectral character of the generated Double:

```text
0   = Body
50  = Full
100 = Air
```

It must not alter:

* direct Side gain;
* Width target;
* decorrelator amount;
* decorrelator source spectrum;
* generated Width amount;
* Width safety calculation.

---

# 2. Important perceptual distinction

Do not confuse:

```text
perceived spaciousness
```

with:

```text
Width algorithm control
```

A looser Double may naturally sound more spacious because the second performance is less correlated.

An Air-focused Double may feel more separated because high frequencies localise strongly.

That is acceptable.

What is not acceptable is for Tightness or Focus to feed any parameter or processing stage whose intended job is to determine Width.

The DSP ownership must remain clean even where psychoacoustic perception overlaps.

---

# 3. Audit the current implementation before changing anything

Trace the complete current `VxWidthProcessor.cpp` signal path.

Produce a dependency map showing which controls influence:

```text
direct existing-Side expansion
target-seeking Width solver
Region C decorrelation
Width orthogonalisation
Double source
Voice A
Voice B
DoubleMid
DoubleSide
Double orthogonalisation
predictability restraint
mono guardrail
final Mid
final Side
```

Specifically search for every use, direct or indirect, of:

```text
width
double
tightness
focus
```

Do not assume comments are correct.

Verify actual data flow.

---

# 4. Current architecture that should be preserved where correct

The current header already indicates that Focus/Tightness separation work exists:

* only `focusTiltMid` remains;
* there is no `focusTiltSide`;
* Region C is intended to use untilted Mid/Side;
* separate `sideOrthogonalizerWidth` and `sideOrthogonalizerDouble` instances exist because the former shared adaptive state allowed Tightness/ADT delays to influence Width.

Preserve these changes if the implementation matches the comments.

Do not collapse the two orthogonaliser paths back together.

---

# 5. Focus must be Double-only

The current `OnePoleTilt` implementation was previously changed to be approximately energy-neutral because the old Focus path could change generated Side energy dramatically.

That is useful for Double quality, but energy neutrality alone is not sufficient.

The stronger invariant is:

> Focus must not be anywhere in the Width-generation path.

Therefore ensure the Width decorrelator receives a source derived only from un-Focus-shaped programme material.

Conceptually:

```cpp
widthDecorSource =
    functionOf(originalMid, originalSide);
```

NOT:

```cpp
widthDecorSource =
    functionOf(focusedMid, focusedSide);
```

The ADT path may use:

```cpp
focusedDoubleSource = focusTiltMid.process(...);
```

because Focus belongs to Double.

---

# 6. Tightness must be Double-only

`AdtVoice::process()` currently accepts `tightness01` and uses it to control delay ranges, Doppler limits and gain variation. That is exactly where Tightness belongs.

Ensure Tightness has no route into:

* target width;
* estimated width;
* direct Side gain;
* Region C amount;
* decorrelator state;
* Width orthogonaliser state;
* Width safety calculations.

Particularly inspect adaptive/shared state.

A parameter does not need to be explicitly passed to a Width function to affect Width indirectly.

For example, the previous shared orthogonaliser problem meant ADT delay changes altered adaptive coefficients which then altered the Width-generated Side.

Prevent these hidden dependencies structurally.

---

# 7. Width-generated and Double-generated Side must remain separate until necessary

Conceptually maintain:

```text
WIDTH SIDE
    direct existing Side
    +
    decorrelated Width contribution

DOUBLE SIDE
    generated Voice A/B difference
```

Do not combine them early and then process them through adaptive systems whose state depends on both.

Preferred structure:

```text
widthGeneratedSide
    ↓
Width-specific safety/orthogonalisation
    ↓

doubleGeneratedSide
    ↓
Double-specific safety/orthogonalisation
    ↓

combine only at the final Side construction stage
```

This is important because otherwise Double, Tightness or Focus can indirectly alter the Width path through shared adaptive state.

---

# 8. Width must control placement of the Double

There is one deliberate interaction between Width and Double:

> Width determines where the generated performances sit spatially.

That is allowed because Width owns geometry.

Double determines how much generated performance exists.

Therefore the relationship should conceptually be:

```text
Double
    ↓
generate performance A/B
    ↓
amount/density

Width
    ↓
determine spatial placement/separation
```

Do not let Double itself determine how far apart the performances are.

If the current implementation represents Voice A/B separation through `DoubleSide`, review whether that implicitly makes increased Double synonymous with increased width.

If so, separate:

```text
generated performance amount
```

from:

```text
generated performance placement
```

where practical.

Do not over-engineer this if the existing implementation already behaves correctly, but document the relationship.

---

# 9. Required behavioural invariants

## Invariant A — Width independence

With:

```text
Double = 0
```

hold Width fixed.

Sweep:

```text
Tightness:
0 / 25 / 50 / 75 / 100

Focus:
0 / 25 / 50 / 75 / 100
```

Measured Width output must not materially change.

Ideally, when Double=0:

> Tightness and Focus should be DSP-irrelevant to the audio output.

If those controls only belong to Double, there is no reason for them to alter sound when Double is zero.

This is an especially useful regression test.

---

# 10. Stronger Double=0 invariant

Test this explicitly:

```text
Width = fixed
Double = 0
```

Render identical source material with:

```text
Tightness = 0, Focus = 0
Tightness = 100, Focus = 0
Tightness = 0, Focus = 100
Tightness = 100, Focus = 100
```

The outputs should null exactly or within floating-point/test tolerance.

If they do not, trace the dependency and remove it.

This is the cleanest proof that Tightness and Focus really are Double-only controls.

---

# 11. Focus independence test with Double active

When Double > 0, Focus is allowed to change the sound.

However it should not directly move the Width engine's own output.

Instrument separately:

```text
widthPathSideRms
doublePathSideRms
finalSideRms
```

At fixed Width and Double:

```text
Focus 0
Focus 50
Focus 100
```

Expected:

```text
widthPathSideRms:
    essentially unchanged

doublePath spectral distribution:
    changes

final perceptual character:
    changes
```

Broadband final width may move slightly as an unavoidable psychoacoustic/resultant consequence of changing the Double spectrum.

Do not demand mathematically identical final Side/Mid ratio with Double active.

Instead demand that the dedicated Width path remains invariant.

---

# 12. Tightness independence test with Double active

Likewise:

```text
Width fixed
Double fixed
Tightness 0 / 50 / 100
```

Expected:

```text
Width direct Side gain:
    unchanged

Width Region C amount:
    unchanged

Width decorrelator input/output level:
    unchanged apart from deterministic numerical tolerance

Double voice timing/movement:
    changes

Double character:
    changes
```

Again, final measured stereo correlation may naturally vary because the Double itself changes.

That is acceptable.

What must not happen is Tightness changing the Width engine's requested or generated width.

---

# 13. The visualiser must primarily represent Width

The main spatial-width visualisation should not respond significantly to Focus or Tightness because they are not Width controls.

If the main Width rays currently visibly open/close when Focus or Tightness moves, determine whether:

A. the DSP Width path is actually changing; or

B. the visualiser is measuring total final output including Double characteristics.

Do not fix a DSP problem in the UI.

First enforce control ownership in DSP.

For the eventual UI:

```text
main rays
    = Width geometry

secondary/ghost information
    = Double presence/independence
```

Focus and Tightness should affect the Double representation, if represented at all, not the primary Width geometry.

---

# 14. Be careful with actualOutputWidth telemetry

If `actualOutputWidth01` is calculated from the final complete output:

```text
original
+ Width
+ Double
```

then it may legitimately change slightly when Tightness or Focus alter the generated Double.

That does not necessarily mean the Width algorithm is coupled.

For testing this control-ownership invariant, add or use separate Width-path telemetry.

For example:

```text
actualWidthPathSideRms
actualWidthPathMidRms
widthPathOutputRatio
```

or an equivalent measure before Double is added.

Do not use a final-output width meter alone to decide whether the internal Width path is independent.

---

# 15. Safety processing ownership

Review:

* SideOrthogonalizer;
* ContentPredictabilityRestraint;
* MonoDownmixGuardrail;
* sub-bass protection;
* loudness compensation.

Determine whether any shared safety state can create cross-control coupling.

The current split Width/Double orthogonaliser architecture is specifically intended to prevent this. Preserve that separation.

Where a safety mechanism genuinely must inspect final combined output, that is acceptable.

But it must not silently turn Focus or Tightness into inputs to the Width strength law.

---

# 16. Focus wording/code comments

Update misleading comments that still describe Focus as affecting:

```text
"width and doubling"
```

if that is no longer the intended product.

The product contract is now:

> Focus controls where the generated doubled performance is concentrated spectrally.

Do not describe Focus as steering the Width effect.

Likewise update help text/preset documentation where necessary.

Do not change user-facing control names.

---

# 17. Parameter responsibilities

Final contract:

```text
WIDTH
- negative narrowing
- positive target-seeking stereo expansion
- generated Width/decorrelation
- spatial placement/separation
- only owner of requested spatial extent

DOUBLE
- generated-performance amount

TIGHTNESS
- generated-performance timing/microvariation relationship

FOCUS
- generated-performance spectral emphasis
```

No other hidden interpretation.

---

# 18. Do not change the successful Width engine while doing this

The current target-seeking Width work should remain intact.

Do not retune:

* target width mapping;
* direct Side gain solver;
* direct Side ceiling;
* stereoEvidence;
* Region C residual calculation;

unless a control-separation bug specifically requires it.

This task is about **dependency ownership**, not Width tuning.

---

# 19. Do not retune Double unnecessarily

Likewise, do not change:

* Double centre-gate floor;
* ADT delay bands;
* Doppler budgets;
* micro-pitch;
* Voice A/B seeds;

unless required to remove an actual cross-control dependency.

Preserve current sonic behaviour as much as possible.

---

# 20. Regression tests to add

Add explicit tests with representative mono and stereo fixtures.

### Test 1 — Tightness does nothing with Double=0

```text
Width fixed
Double 0
Focus fixed

Tightness 0 vs 100
```

Expected:

```text
null / effectively identical output
```

### Test 2 — Focus does nothing with Double=0

```text
Width fixed
Double 0
Tightness fixed

Focus 0 vs 100
```

Expected:

```text
null / effectively identical output
```

### Test 3 — Width path invariant to Tightness

```text
Width +75
Double >0

Tightness 0 vs 100
```

Width-path telemetry unchanged.

Double-path telemetry changes.

### Test 4 — Width path invariant to Focus

```text
Width +75
Double >0

Focus 0 vs 100
```

Width-path telemetry unchanged.

Double spectral distribution changes.

### Test 5 — Width remains effective

Verify:

```text
Width 0 / 25 / 50 / 75 / 100
```

still produces the expected monotonic Width behaviour.

### Test 6 — Double remains effective

Verify:

```text
Double 0 / 25 / 50 / 75 / 100
```

still produces increasing generated-performance contribution.

---

# 21. Preserve all foundational invariants

Must remain true:

* Width=0 exact passthrough when Double=0;
* negative Width still narrows correctly to mono;
* positive Width still widens mono and stereo correctly;
* mono path remains unchanged where previously guaranteed;
* Double remains audible on genuine stereo;
* no reintroduction of L/R imbalance;
* mono fold-down safety remains intact;
* no audio-thread allocation/locking;
* deterministic offline renders;
* zero reported latency remains unchanged.

---

# 22. Report back

Before declaring this complete, report:

1. The dependency map found in the current code.
2. Every place Focus currently affects the signal.
3. Every place Tightness currently affects the signal.
4. Any indirect/shared-state coupling found.
5. Whether the existing separate Width/Double orthogonalisers fully solve the previous Tightness leakage.
6. Whether Region C receives completely Focus-independent source material.
7. Whether Tightness and Focus null when Double=0.
8. Width-path measurements across Focus 0/50/100.
9. Width-path measurements across Tightness 0/50/100.
10. Confirmation that Double still responds correctly.
11. Confirmation that Width still responds correctly.
12. VXWidth regression-suite result.
13. Broader cross-plugin regression-suite result.

---

# Definition of done

The implementation is complete when the following statement is structurally and measurably true:

> **Width is the only control that tells VX Width how wide the signal should be.**

Double adds another performance.

Tightness changes how independently that performance behaves.

Focus changes where that performance sits spectrally.

Neither Tightness nor Focus participates in the Width-generation algorithm, directly or indirectly.

When Double=0, changing Tightness or Focus should produce no audible/DSP output change.

When Double>0, Tightness and Focus may alter the perceived stereo result as a natural consequence of changing the generated performance, but the dedicated Width path itself must remain unchanged.
