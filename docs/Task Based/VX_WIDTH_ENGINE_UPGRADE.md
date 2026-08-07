# VX Width — Dynamic Spatial Width Engine Redesign

## Objective

Replace the current Width behaviour with a geometry-driven, content-aware spatial-width engine.

This is an engine redesign, not a patch to the existing M/S Side-gain implementation.

The new engine must:

* understand whether the incoming signal is effectively mono or already stereo;
* estimate the current perceptual width of stereo material;
* make the Width control behave differently, but intuitively, for mono and stereo sources;
* provide a defined spatial endpoint at maximum Width;
* preserve the original stereo image at Width = 0 for stereo material;
* collapse stereo predictably towards mono for negative Width;
* create genuine spatial separation for mono material as Width increases;
* visibly represent the current and requested width in the plugin UI in real time;
* retain the existing safety architecture: transient protection, low-frequency protection, generated-Side orthogonalisation, mono guardrails, etc.

The user-facing concept is **spatial position**, not Side gain.

---

# 1. User model

Think of the horizontal stereo field as approximately:

```text
                    CENTRE
                      |
                      |
                      |
-90° L ---------------+--------------- +90° R
```

0° represents centre.

±90° represents the practical hard-left / hard-right limits.

Width controls how far apart the left/right spatial components are allowed to move.

The control must therefore have a meaningful endpoint:

```text
Maximum Width = maximum useful left/right separation
              ≈ hard-left / hard-right
              ≈ ±90°
```

This does NOT mean blindly hard-panning every component of an existing mix.

It means ±90° is the spatial target/boundary against which the engine expands the existing image.

---

# 2. Mono and stereo have different Width-control semantics

The plugin must dynamically determine whether the source is effectively mono or meaningfully stereo.

Do not use a single binary L==R check.

Use the existing analysis framework and derive a stable `inputWidthConfidence` / `inputStereoWidth` based on:

* L/R correlation;
* Side/Mid energy ratio;
* centre confidence;
* existing mono confidence;
* optionally short-term stability of those measurements.

The classification must be smoothed and hysteretic so normal programme material does not cause the UI or processing model to flick rapidly between mono and stereo modes.

---

# 3. Mono Width behaviour

For an effectively mono signal:

```text
Width control:

0%                                              100%
|------------------------------------------------|
centre                                      full spread
```

The knob should begin fully counter-clockwise.

Display:

```text
0                    50                    100
```

There is no meaningful negative Width because the source is already mono.

### Width = 0

Output remains centred.

No artificial stereo separation should be introduced.

### Width > 0

Generate complementary spatial content and progressively move the perceived pair away from centre.

Conceptually:

```text
0%

                 A/B
                  |
                  0°


25%

               A   B
                \ /
                 |


50%

            A         B
              \     /
               \   /


75%

        A                 B
          \             /


100%

A                                     B
-90°                               +90°
```

At 100%, generated A/B spatial components should behave equivalently to fully separated left/right voices.

For the ADT path this can correspond to true hard-left/hard-right generated voices where safe.

This gives Width = 100 a real and understandable meaning.

---

# 4. Stereo Width behaviour

For meaningfully stereo material, the existing image becomes the neutral/reference state.

The knob should therefore use a bipolar display:

```text
-100                   0                   +100
|----------------------|----------------------|
mono             original width           full width
```

The knob should initialise visually at the centre when Width = 0.

### Width = 0

The spatial geometry must remain the same as the incoming signal.

This is the neutral point.

It should not mean:

```text
some arbitrary M/S gain
```

It means:

```text
preserve estimated existing width
```

### Width = -100

Collapse the stereo image to mono/centre.

Conceptually:

```text
existing L/R positions
        ↓
       0°
```

### Width between -100 and 0

Progressively contract the current stereo image towards centre.

### Width between 0 and +100

Progressively expand the existing image towards the maximum useful spatial boundary.

Conceptually, if the engine estimates that the material currently occupies approximately ±35°:

```text
Width = 0

             L       R
             -35°   +35°
```

then:

```text
Width +25  → approximately ±49°
Width +50  → approximately ±63°
Width +75  → approximately ±76°
Width +100 → target approximately ±90°
```

The exact mapping may use a perceptually tuned curve rather than linear degrees.

The important requirement is monotonic behaviour:

> Increasing Width must always move eligible spatial content further outward.

---

# 5. Width must be relative to current width

This is central to the redesign.

Do NOT use:

```text
outputSide = inputSide * arbitraryGain
```

as the definition of Width.

Instead determine:

```text
estimatedInputWidth = W0
requestedWidthControl = C
targetWidth = f(W0, C)
```

For stereo material:

```text
C = -100  -> targetWidth = 0
C = 0     -> targetWidth = W0
C = +100  -> targetWidth = maximum useful width
```

Conceptually:

```text
if C < 0:
    targetWidth = lerp(W0, 0, abs(C)/100)

if C >= 0:
    targetWidth = lerp(W0, Wmax, C/100)
```

A perceptual interpolation curve may replace linear interpolation later.

For mono:

```text
W0 ≈ 0

control range becomes:

0 -> Wmax
```

---

# 6. Estimating current stereo width

Create an explicit width-analysis feature rather than deriving behaviour indirectly from `monoConfidence`.

Suggested conceptual feature:

```cpp
struct SpatialWidthSnapshot
{
    float monoConfidence;       // 0..1
    float stereoConfidence;     // 0..1
    float estimatedWidth01;     // 0=center, 1≈max practical stereo width
    float correlation;          // -1..1
    float sideToMidRatio;
    float centreConfidence;
};
```

`estimatedWidth01` should be continuous and smoothed.

It does not need to claim physically exact source angles.

It is a **perceptual stereo-width estimate**.

Its job is to answer:

> How wide does this signal currently appear, relative to centre and the practical stereo boundary?

This same value must drive both:

* processing;
* visualisation.

The UI and DSP must never disagree because they use separate definitions of Width.

---

# 7. Processing model

## Existing stereo content

Positive Width should primarily expand the existing stereo geometry.

M/S processing may still be used internally, but only as one mechanism.

Do not treat Side gain as the product behaviour.

The engine may combine:

* bounded existing-Side expansion;
* frequency-dependent spatial scaling;
* generated decorrelated information where additional separation is required;
* existing stereo preservation;
* centre-aware synthetic width.

The amount of synthetic width should depend on how much additional spatial movement is required.

Example:

```text
Already 85% wide + Width 100
    -> very little synthetic content required.

20% wide + Width 100
    -> significant additional spatial contribution required.
```

This is the opposite of simply saying:

```text
stereo input -> don't generate much width
```

The question becomes:

```text
How far is current width from requested width?
```

---

# 8. Mono processing

Mono has no existing Side to expand.

Therefore its Width behaviour must be generated.

Use the current complementary generated-Side/decorrelation architecture, but map its result to the requested spatial position.

For generated ADT voices:

```text
Width 0   -> both spatially centred
Width 50  -> partial symmetric pan
Width 100 -> Voice A hard left / Voice B hard right
```

Use a proper panning law rather than linear channel multiplication.

The dry/original centre should remain stable according to the intended product mix.

Width should control spatial placement; Double controls performance differentiation.

---

# 9. Width and Double must be independent

This distinction is now explicit.

## Width

Answers:

> Where are the performances located?

## Double

Answers:

> How different are the performances?

Examples:

### Width 100 / Double 0

Very wide spatial presentation, but generated components remain closely related.

### Width 100 / Double 100

Very wide AND clearly differentiated performance:

* independent timing;
* micro-pitch;
* gain variation;
* spectral variation.

### Width 0 / Double 100

Should still create audible thickening/performance variation, but remain predominantly centred.

Double must also remain effective on existing stereo material.

Low mono confidence must NOT effectively disable Double.

Centre confidence may determine which material is most appropriate to double, but the user turning Double up must produce a clearly perceptible result.

---

# 10. Stereo content must remain responsive

This is a hard acceptance requirement.

The current observed behaviour is that Width and Double can feel almost inactive on already-stereo material.

That is not acceptable.

Stereo awareness should change HOW processing happens, not WHETHER it happens.

For ordinary stereo material:

```text
Width 0 -> original width

Width +50 -> visibly and audibly wider

Width +100 -> approaches maximum useful stereo extent
```

Similarly:

```text
Double 0 -> no generated performance

Double +50 -> obvious but natural additional performance

Double +100 -> maximum intended doubling character
```

Existing width can reduce unnecessary synthetic processing, but must not suppress the requested result.

---

# 11. Real-time visualisation

The plugin UI should visualise Width geometrically.

This is part of the feature, not decorative metering.

Suggested display:

```text
                    centre
                       |
                 \     |     /
                   \   |   /
                     \ | /
L ---------------------+--------------------- R
```

Represent:

1. current/input width;
2. requested/processed width.

For example:

```text
Input width     = ±32°
Target width    = ±68°
```

The UI might show:

* faint/ghosted input-width rays;
* stronger processed/target-width rays.

Conceptually:

```text
        target L                 target R
             \                   /
              \   input input   /
               \    \   /      /
                \    \ /      /
                 \    |      /
                      |
                    centre
```

Do not display literal degree numbers unless testing shows they are useful.

The geometry is more important than pretending the angle estimate is physically exact.

---

# 12. Dynamic knob presentation

The Width knob presentation must adapt to source type.

## Mono source

```text
0 -> 100
```

0 is fully counter-clockwise.

There is no negative range shown.

## Stereo source

```text
-100 -> 0 -> +100
```

0 is visually centred.

-100 is fully counter-clockwise.

+100 is fully clockwise.

### Important UX requirement

Do NOT let a changing source cause the knob itself to jump violently between physical positions.

Input classification must be sufficiently stable before presentation mode changes.

Consider:

* hysteresis;
* minimum dwell time;
* smooth UI transition;
* preserving the user's semantic Width request through a classification transition.

For example, a stereo intro becoming briefly mono should not make the control suddenly leap across the interface.

The underlying parameter/state representation therefore needs careful design.

---

# 13. Parameter model

Do not store the Width parameter purely as the current knob angle.

Store its semantic request.

Suggested internal representation:

```cpp
enum class WidthInputMode
{
    MonoLike,
    StereoLike
};

float widthRequest;
float estimatedInputWidth;
float requestedOutputWidth;
```

For stereo:

```text
widthRequest ∈ [-1, +1]
```

For mono UI:

```text
displayed request ∈ [0, +1]
```

Internally it may still use the common representation.

The DSP should operate on:

```text
requestedOutputWidth
```

rather than directly on knob position.

---

# 14. Input classification must not become a hard DSP switch

There should not be:

```cpp
if (mono)
    monoAlgorithm();
else
    stereoAlgorithm();
```

with abrupt transitions.

Use continuous weighting.

For example:

```text
monoWeight
stereoWeight
```

and crossfade/weight the relevant mechanisms.

The UI may present Mono or Stereo mode once confidence passes hysteresis thresholds, but the DSP should remain continuous.

---

# 15. Width safety architecture remains

Do not remove the existing safety systems.

Retain:

* transient protection;
* low-frequency generated-Side protection;
* SideOrthogonalizer;
* ContentPredictabilityRestraint;
* MonoDownmixGuardrail;
* loudness/energy management;
* centre-confidence analysis;
* content-aware modulation.

However, review their interaction with the new requested-width model.

Safety systems may restrain an unsafe request.

They must not routinely make the Width control perceptually inactive.

A useful conceptual separation is:

```text
requested width
      ↓
width engine
      ↓
candidate spatial output
      ↓
safety correction
      ↓
actual width
```

Telemetry should make it possible to inspect:

```text
requestedWidth
actualWidth
safetyReduction
```

---

# 16. Required regression tests

Add tests for the new spatial contract.

## Mono endpoint

Mono input:

```text
Width 0   -> effectively mono
Width 100 -> maximum generated spatial separation
```

Generated A/B voices at 100 must reach the equivalent of hard L/R placement.

---

## Stereo neutral

Stereo source:

```text
Width 0
```

must preserve the original spatial geometry within tolerance.

---

## Stereo collapse

```text
Width -100
```

must produce an effectively mono output.

---

## Stereo expansion

A deterministic stereo fixture with moderate initial width must have:

```text
estimatedOutputWidth(+25)
    >
estimatedOutputWidth(0)

estimatedOutputWidth(+50)
    >
estimatedOutputWidth(+25)

estimatedOutputWidth(+100)
    >
estimatedOutputWidth(+50)
```

Width must be monotonic.

---

## Already-wide material

A very-wide source at +100 must remain safe and must not be made narrower.

The engine may make only a small change if already close to the target.

---

## Stereo Double

For a stereo fixture:

```text
Double 0 -> baseline

Double > 0 -> measurable generated-performance contribution
```

The contribution must not collapse merely because monoConfidence is low.

---

## Dynamic classification

Test transitions:

```text
mono -> stereo
stereo -> mono
```

with no discontinuity, NaN, large gain jump, or parameter-state corruption.

---

## Existing safety tests

All existing:

* mono compatibility;
* imbalance;
* Side retention;
* allocation;
* null;
* sample-rate;
* block-size;
* Doppler;
* micro-pitch

tests must continue to pass or be intentionally replaced by tests representing the new contract.

Do not simply loosen thresholds to accommodate the redesign.

---

# 17. Debug telemetry

Expose enough internal information to prove the engine is doing what the UI says.

At minimum:

```text
monoConfidence
estimatedInputWidth01
requestedOutputWidth01
actualOutputWidth01
existingSideContribution
generatedWidthContribution
doubleContribution
safetyRestraintAmount
```

This is particularly important while tuning stereo behaviour.

---

# 18. Do not optimise against one fixture

Use the existing representative corpus.

Test:

* mono vocals;
* stereo vocals;
* guitar;
* piano;
* synth pad;
* drums;
* bass-heavy material;
* noise-like material;
* transient-rich material;
* narrow stereo;
* moderate stereo;
* already-wide stereo.

The important property is generalisation.

---

# 19. Implementation philosophy

Do not begin by tweaking existing Side-gain constants.

First implement the new abstraction:

```text
INPUT WIDTH
      ↓
REQUESTED WIDTH
      ↓
TARGET SPATIAL GEOMETRY
      ↓
PROCESSING REQUIRED TO REACH TARGET
```

Then determine how much of the existing M/S, decorrelation and generated-content machinery can be reused underneath it.

The existing DSP components are implementation tools.

They are no longer the definition of Width.

---

# 20. Definition of success

The redesign is successful when a user can predict what the control will do without understanding M/S processing.

For mono:

> 0 is centre. Turn Width clockwise and the sound opens outward until the generated pair reaches the left and right extremes.

For stereo:

> 0 is what I already have. Turn left and it collapses towards mono. Turn right and it expands towards the widest useful version of the existing image.

And the real-time display visibly follows that same spatial model.

That is the new Width engine.
