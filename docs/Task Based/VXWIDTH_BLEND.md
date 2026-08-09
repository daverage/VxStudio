Absolutely. Here’s the full spec with the Blend behaviour made explicit and treated as a hard product contract.

# VX Width — Fifth Control: Blend

## Objective

Add a fifth user-facing **Blend** control to VX Width.

This is an intentional exception to the usual four-control VX Studio philosophy because Blend represents a genuinely independent creative decision that the existing four controls do not cover.

The five controls become:

```text
Width      = how wide the result is

Double     = how much additional performance is generated

Blend      = how much original source versus VX-processed result is heard

Tightness  = how independently the generated performance behaves

Focus      = where the generated performance sits spectrally
```

The required UI layout is an inverted pyramid:

```text
        WIDTH     DOUBLE     BLEND

           TIGHTNESS     FOCUS
```

The top row contains the primary sound decisions.

The bottom row contains refinement controls for the generated Double.

---

# 1. Blend product contract

The Blend control has a deliberately non-standard mapping.

This mapping is **explicit and must not be changed**:

```text
Blend 0
= untouched original signal

Blend 50
= current/default VX Width sound

Blend 100
= maximum useful effect-emphasised result
```

This is not a conventional Dry/Wet control.

It is intentionally centre-referenced.

The centre position represents the VX-designed normal sound that already exists today.

Moving left progressively backs VX processing away toward the original.

Moving right deliberately pushes beyond the normal VX balance toward a more exposed/effect-forward result.

This behaviour is foundational.

---

# 2. Why Blend 50 is the neutral/default position

Current VX Width processing already retains the original source and adds/modifies spatial and Double content around it.

Therefore the existing sound is not equivalent to:

```text
50% dry
+
50% wet
```

It is its own designed balance.

Blend must therefore preserve that existing behaviour at its centre position.

Requirements:

```text
Blend = 50
```

must reproduce the pre-Blend version of VX Width exactly or within normal floating-point tolerance.

This preserves:

* old presets;
* old sessions;
* automation compatibility;
* the currently approved sound;
* the existing Width/Double balance.

Do not reinterpret Blend 50 as a textbook equal-power wet/dry midpoint.

---

# 3. Parameter definition

Add:

```text
Parameter ID: blend
Display name: Blend
Range: 0–100
Default: 50
Centre detent: 50
```

Old presets or sessions that do not contain the parameter must load with:

```text
Blend = 50
```

No existing parameter IDs may change.

Do not migrate or alter:

* Width;
* Double;
* Tightness;
* Focus.

---

# 4. User-facing interpretation

Suggested semantics:

```text
0      Original
50     Normal
100    Effect
```

Suggested tooltip/help:

> Blend controls the balance between the original performance and VX's processed spatial/doubled result. 50% is the normal VX sound; move left for more original signal or right for a more effect-forward result.

Avoid using “Dry/Wet” as the primary wording because Blend does not behave like a conventional 0–100 wet/dry crossfade.

---

# 5. Control ownership

Blend must have one responsibility only:

> Control how much of the original source remains relative to VX processing.

Blend must NOT alter the actual DSP decisions made by:

```text
Width
Double
Tightness
Focus
```

Specifically, Blend must not feed or modify:

```text
targetWidth01
adtSeparationForWidth()
sideGain
decorBlend
doubleAmount
tightness01
focusTiltDb
input-width analysis
stereoEvidence
phase-risk decisions
ADT timing
ADT pitch movement
```

The processor should generate the same underlying Width/Double result regardless of Blend position.

Blend acts on how that result is presented relative to the original.

---

# 6. Signal architecture

Preserve an untouched reference to the original input:

```text
dryL
dryR
```

or equivalently:

```text
dryMid
drySide
```

before VX processing modifies the signal.

Preferred conceptual flow:

```text
Original input
      │
      ├─────────────── Dry reference
      │
      ▼
Width + Double DSP
      │
      ▼
VX processed signal
      │
      ▼
Blend stage
      │
      ▼
Loudness management
      │
      ▼
Output trim
```

The Blend stage must not force a redesign of the existing M/S architecture.

Prefer minimal code changes.

---

# 7. Important distinction: processed signal versus effect contribution

The current VX output already contains the original source.

Therefore do not blindly do:

```cpp
out = dry * dryGain + processedVX * wetGain;
```

because `processedVX` already contains dry content.

That would double-count the original source across much of the control range.

Instead define:

```text
dry
=
untouched original signal

processed
=
normal VX result before Blend

delta
=
processed - dry
```

This decomposition is exact.

Then the left half of Blend can cleanly control the amount of VX delta around the original.

---

# 8. Blend 0 → 50 behaviour

For Blend values from 0 to 50:

```text
Blend 0
= original only

Blend 25
= half of the normal VX change

Blend 50
= full normal VX result
```

Recommended formulation:

```cpp
const float x = blend / 50.0f;

output =
    dry
    + x * (processed - dry);
```

So:

```text
Blend 0:
dry + 0 * delta
= dry

Blend 25:
dry + 0.5 * delta

Blend 50:
dry + 1.0 * delta
= processed
```

This section should be mathematically exact.

---

# 9. Blend 50 → 100 behaviour

This half of the control is different.

For Blend above 50, the user is explicitly asking for:

> more effect emphasis than the normal VX sound.

Do not simply extend the same delta multiplier indefinitely unless listening proves that is the best result.

Do not assume:

```text
Blend 100 = processed - dry
```

is musically correct just because it is algebraically neat.

At Blend 100, the result should be:

> the strongest useful VX-forward interpretation of the current Width/Double settings.

The endpoint must be chosen based on listening and safety.

---

# 10. Two required Blend>50 candidates

Implement and compare at least these two approaches.

## Candidate A — pure delta/effect-only

Define:

```text
effectDelta =
processed - dry
```

and transition from:

```text
Blend 50 = processed
```

toward:

```text
Blend 100 = effectDelta
```

This has excellent mathematical symmetry but may sound strange for stereo widening because the delta contains:

* added Side;
* removed Side;
* gain differences;
* phase-opposed components;
* the difference between processed and original geometry.

It must not be chosen simply because it is mathematically elegant.

---

## Candidate B — explicit VX effect bus

Construct a musically meaningful effect contribution consisting of the processing VX deliberately creates or modifies.

Potential components:

```text
generated Width Side

generated/decorrelated Width content

additional existing-Side expansion above original

Double Mid

Double Side
```

The untouched original component is excluded.

For narrowing, explicitly decide what constitutes a meaningful effect contribution rather than simply producing negative original-Side residue.

This candidate may produce a more useful Blend 100 endpoint.

---

# 11. Choose Blend 100 by listening

The final Blend 100 behaviour must be chosen from listening tests.

It must not be selected from synthetic metrics alone.

Use representative material:

* mono vocal;
* stereo vocal;
* mono guitar;
* stereo guitar;
* piano;
* drums;
* synth/pad;
* narrow stereo mix;
* wide stereo mix;
* transient-heavy source;
* tonal sustained source.

Compare Candidate A and Candidate B.

Choose the endpoint that best satisfies:

```text
more effect-forward than Blend 50

still musically useful

does not become hollow for no good reason

does not collapse catastrophically in mono

does not create excessive anti-phase behaviour

still clearly reflects Width and Double settings
```

If neither candidate is ideal, derive a third simple endpoint based on the evidence.

Do not introduce unnecessary new DSP merely to solve Blend 100.

---

# 12. Blend 100 does not have to mean literal zero dry

This is explicit.

Blend 100 means:

> maximum useful effect emphasis.

It does **not** necessarily mean:

> mathematically remove every sample of the original source.

If literal zero-dry produces a result that is obviously musically inferior, do not force it.

The product meaning is perceptual and creative, not dogmatic.

So the contract is:

```text
Blend 0   = exact original

Blend 50  = exact current VX

Blend 100 = maximum useful VX-forward sound
```

Only 0 and 50 are hard algebraic endpoints.

100 is a hard **product** endpoint whose DSP definition must be selected by listening.

---

# 13. Mono widening behaviour

With mono input and Double = 0:

```text
Blend 0
= untouched mono centre

Blend 25
= mono centre + reduced generated width

Blend 50
= current VX mono widening

Blend 75
= more exposed generated width / less centre dominance

Blend 100
= maximum useful stereoised result
```

The original source should not disappear unexpectedly before the top of the control.

The transition must remain smooth.

At Blend 100, avoid producing a result that is effectively only anti-phase Side unless listening proves that to be desirable, which is unlikely.

---

# 14. Stereo widening behaviour

With stereo input:

```text
Blend 0
= untouched stereo signal

Blend 50
= current VX widened/narrowed result

Blend 100
= maximum useful emphasis of the altered VX image
```

Width still determines the intended geometry.

Blend must not change:

```text
Width target
solver target
ADT separation
generated Width amount
```

Any measured final stereo-width difference caused by changing dry/effect balance is acceptable.

But Blend must not feed back into the Width engine.

---

# 15. Double behaviour

With Double active:

```text
Blend 0
= original performance only

Blend 50
= current VX Double balance

Blend 100
= maximum useful emphasis of the generated performance
```

Important:

```text
Double = 0
```

means no generated Double exists regardless of Blend.

Therefore:

```text
Double 0
Blend 100
```

must not create synthetic performances.

Similarly:

```text
Double 100
Blend 0
```

must still output only the original source.

The ADT engine may continue running internally to keep buffers/state live, but its output must be absent.

---

# 16. Width and Double remain independent

The full ownership model becomes:

```text
WIDTH
    -> spatial target
    -> existing stereo narrowing/expansion
    -> generated Width geometry
    -> ADT spatial separation

DOUBLE
    -> amount of generated performance

BLEND
    -> original versus VX presentation

TIGHTNESS
    -> timing / pitch / gain / independence of generated performance

FOCUS
    -> spectral weighting of generated performance
```

This ownership model must be testable.

---

# 17. UI layout

Use this layout:

```text
        WIDTH     DOUBLE     BLEND

           TIGHTNESS     FOCUS
```

The top row should have three evenly balanced controls.

The lower row should sit centred beneath them, forming an inverted pyramid.

Do not simply place five knobs in one row.

Do not shrink all controls excessively to force them into the existing four-control template.

Width should remain visually important.

Blend should be comparable in importance to Double.

Tightness and Focus may remain visually secondary.

---

# 18. UI centre behaviour

Blend should have a clear centre detent at 50.

This communicates:

```text
left of centre
= more original / less VX

centre
= normal VX sound

right of centre
= more VX / effect emphasis
```

This is conceptually closer to a bipolar intensity relationship than a normal wet/dry knob, even though its displayed range remains 0–100.

---

# 19. Parameter smoothing

Use the existing block/control smoothing infrastructure.

Blend automation must be:

* click-free;
* continuous;
* safe through 50;
* safe at 0 and 100.

There must be no special-case discontinuity at the centre detent.

The mathematical function may change shape at 50, but both value and preferably first-order perceived response should remain smooth.

---

# 20. Width=0 / Double=0 invariant

This remains a critical invariant.

When:

```text
Width = 0
Double = 0
```

there is no VX effect.

Therefore:

```text
Blend 0
Blend 25
Blend 50
Blend 75
Blend 100
```

should all produce the original input.

Blend should have nothing to expose when the DSP has generated no difference from dry.

This protects the conceptual model.

---

# 21. Blend 0 exact-null test

At:

```text
Blend = 0
```

the plugin must output the untouched original input.

Test across:

* mono;
* stereo;
* narrow stereo;
* wide stereo;
* positive Width;
* negative Width;
* Double 0;
* Double 100;
* all Tightness positions;
* all Focus positions.

The output should null against dry input within floating-point tolerance.

This is a hard acceptance criterion.

---

# 22. Blend 50 backwards-compatibility test

Create a reference render from the current pre-Blend processor.

Then compare with the new processor at:

```text
Blend = 50
```

using identical:

* Width;
* Double;
* Tightness;
* Focus;
* input;
* sample rate;
* block size.

The result must null or match within expected floating-point tolerance.

This is also a hard acceptance criterion.

---

# 23. Blend monotonicity

Measure distance-from-dry:

```text
RMS(newOutput - dry)
```

for:

```text
Blend 0
25
50
75
100
```

on representative Width processing.

Expected:

```text
0 < 25 < 50 < 75 < 100
```

or at minimum perceptually monotonic effect emphasis where algebraic RMS is not a useful metric above 50.

Repeat for Double processing.

Avoid top-half plateauing.

---

# 24. Blend should not become another master strength knob

A subtle but important distinction:

Blend changes:

> relationship between original and processed content.

It should not internally scale:

```text
Width request
Double request
ADT timing
Focus amount
```

For example, this is wrong:

```text
effectiveWidth = Width * Blend
```

This is also wrong:

```text
effectiveDouble = Double * Blend
```

Instead the full VX engine should render normally, then Blend controls presentation.

---

# 25. Safety positioning

Safety analysis should generally evaluate the actual spatial DSP before final loudness management.

Preferred order:

```text
dry input
   ↓
Width/Double DSP
   ↓
spatial/phase safety
   ↓
Blend
   ↓
loudness management
   ↓
output trim
```

However, high Blend values may expose effect-only content that creates additional audible risk.

Therefore also test the **final Blend output** for:

* severe negative correlation;
* mono collapse;
* excessive LF Side;
* output overload.

Do not feed normal Blend changes back into the Width target-seeking solver unless there is a genuine technical reason.

---

# 26. Loudness management

Blend may cause large perceived-level differences, particularly above 50.

Loudness compensation may gently manage gross level differences.

It must not:

* flatten the Blend control;
* make Blend 100 sound as conservative as 50;
* introduce false mono/phase restraint;
* cause Width/Double safety cross-coupling.

Blend should remain clearly audible.

Loudness management is level housekeeping, not effect-strength management.

---

# 27. Preset compatibility

Old presets:

```text
no Blend value present
```

must resolve to:

```text
Blend = 50
```

so they reproduce the existing sound.

New presets may intentionally store other Blend values.

No hidden migration should alter the other controls.

---

# 28. Preset philosophy

Blend also enables more useful preset differentiation.

Examples:

```text
Natural Width
Blend around 50

Subtle Width
Blend below 50

Wide FX
Blend above 50

Vocal Double
Blend around 40–60

Artificial Double
Blend high
```

Do not encode these taste decisions into hard DSP limits.

---

# 29. Help text

Public control descriptions should become:

**Width**
How wide the signal is.

**Double**
How much additional performance is generated.

**Blend**
How much original versus VX-processed sound you hear.

**Tightness**
How closely the generated performance follows the original.

**Focus**
Where the generated performance sits spectrally.

Blend-specific help:

> At 0 you hear the original signal only. At 50 you hear the normal VX Width sound. Above 50 the VX processing becomes increasingly exposed, reaching the strongest useful effect at 100.

---

# 30. Test matrix

At minimum run:

### No-effect test

```text
Width 0
Double 0
Blend 0/25/50/75/100
```

All should equal dry.

### Width-only

```text
Width 100
Double 0
Blend 0/25/50/75/100
```

### Double-only

```text
Width 0
Double 100
Blend 0/25/50/75/100
```

### Combined

```text
Width 100
Double 100
Blend 0/25/50/75/100
```

### Narrowing

```text
Width -100
Double 0
Blend 0/25/50/75/100
```

### Combined with character controls

At fixed Width/Double/Blend:

```text
Tightness 0/50/100
Focus 0/50/100
```

Verify ownership remains correct.

---

# 31. High-Blend safety corpus

Specifically test Blend 100 on:

* bit-exact mono;
* mono vocal;
* tonal electric guitar;
* acoustic guitar;
* piano;
* transient drums;
* noise/ambience;
* narrow stereo mix;
* moderate stereo mix;
* wide stereo mix;
* hard-panned content;
* intentionally anti-phase fixture.

Check:

```text
L/R balance
mono fold-down
processed correlation
LF behaviour
centre stability
peak level
RMS level
comb filtering
hollowness
generated-content dominance
```

Do not automatically fail Blend 100 because it sounds strongly processed.

That is intentional.

Fail it when it becomes technically or musically unusable.

---

# 32. CPU / realtime requirements

The Blend implementation must:

* add no audio-thread allocations;
* add no latency;
* remain deterministic;
* remain realtime-safe;
* avoid unnecessary duplicate DSP processing.

Prefer retaining one dry reference rather than reprocessing the entire engine twice.

---

# 33. Minimal-code principle

Follow CLAUDE.md:

> simplicity first / touch minimal code.

Do not:

* redesign the entire processor;
* introduce a universal VX dry/wet framework;
* alter unrelated plugins;
* create unnecessary parallel render pipelines;
* convert the processor out of M/S.

The smallest clean implementation local to VX Width is preferred.

---

# 34. Interaction with the pending ADT and guardrail fixes

The current pending architecture work still has priority:

1. Correct ADT spatial placement in M/S.
2. Add processed-output coherence to the phase-risk guardrail.
3. Revalidate direct-Side authority at 12/15/18 dB.
4. Correct loudness-compensator/safety coupling.
5. Add Blend against that stable topology.

If Blend work is performed in the same branch, do not use Blend results to validate the old ADT geometry or old coherence measurement.

Blend should be evaluated against the corrected spatial engine.

---

# 35. Reporting required

Report back with:

1. Exact Blend signal-flow.
2. Exact dry reference used.
3. Exact processed reference used.
4. Exact formula for Blend 0→50.
5. Candidate A implementation/result for Blend 50→100.
6. Candidate B implementation/result for Blend 50→100.
7. Chosen Blend 100 interpretation.
8. Why it won.
9. Blend 0 null-test result.
10. Blend 50 backwards-compatibility result.
11. Width=0/Double=0 invariant result.
12. Mono Blend sweep.
13. Stereo Blend sweep.
14. Double Blend sweep.
15. Width/Double/Tightness/Focus ownership tests.
16. High-Blend mono/phase results.
17. Loudness behaviour.
18. CPU impact.
19. Allocation check.
20. Full regression totals.
21. Anything that still requires subjective listening.

---

# Definition of done

The finished control set is:

```text
        WIDTH     DOUBLE     BLEND

           TIGHTNESS     FOCUS
```

with these exact meanings:

```text
Width
= how wide

Double
= how much additional performance

Blend
= how original or effect-forward

Tightness
= how independently the generated performance behaves

Focus
= where the generated performance sits spectrally
```

And these Blend landmarks are non-negotiable:

```text
0
= exact untouched original

50
= exact current/default VX sound

100
= strongest musically useful VX-forward result
```

The top half of Blend is deliberately **not defined as literal zero-dry** unless listening proves that this is genuinely the best endpoint.

The guiding principle is:

> **Blend 0 is mathematically dry. Blend 50 is mathematically the current VX result. Blend 100 is perceptually the maximum useful effect.**

That is the product contract.
