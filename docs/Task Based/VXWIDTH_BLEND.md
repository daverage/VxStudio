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

**Requirement (VX Width 1.0 shipped — see §22).** The implemented Blend
gain law is algebraically exact at the centre: `blendGain(0.5) = 1`, so

```text
Blend = 50
```

reproduces the pre-Blend VX Width output exactly (not just in close sonic
continuity), satisfying the original bit-exact requirement now that a
public release and real sessions/presets exist to protect. This preserves:

* old presets and sessions (Blend defaults to 50 - see §27);
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

**Revised preferred flow.** The existing loudness compensator must not sit
downstream of Blend — see §25/§26 for why. Loudness compensation stays
where it already is, inside the normal VX path, and Blend is constructed
from the compensated result:

```text
Original input
      │
      ├──────────────────────────── Dry reference
      │
      ▼
Width / Double DSP
      │
      ▼
Spatial / phase safety
      │
      ▼
Existing VX loudness compensation
      │
      ├──────────────────────────── Normal VX reference (Blend 50)
      │
      ▼
Construct effect-forward endpoint
      │
      ▼
Blend
      │
      ▼
Output trim / hard output safety only
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

**Naming.** To keep this unambiguous through the rest of the spec, the three
internal references Blend interpolates between are:

```text
DRY
= untouched original input
= never receives VX compGain

NORMAL_VX
= corrected pre-Blend processor output (§34's PRE-BLEND BASELINE)
= includes the existing Width/Double processing and its baseline loudness
  compensation
= exactly what Blend 50 reproduces
= this document's "dry"/"processed" pair above maps to DRY/NORMAL_VX

EFFECT_ENDPOINT
= chosen musically useful effect-forward interpretation (§10-12)
= derived from the same underlying processing
= may reuse NORMAL_VX's baseline compGain
= must NOT compute its own Blend-dependent loudness normalisation
```

Blend is interpolating between three intentional sonic states, not feeding
a wet/dry crossfade into an adaptive normaliser:

```text
0 ─────────────── 50 ─────────────── 100

DRY              NORMAL_VX          EFFECT_ENDPOINT
```

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

**Implementation note (built without listening access - confirm by ear before ship).**
Neither Candidate A nor Candidate B as literally specified survives the
hard §20 invariant: both are built entirely from "what changed" (pure
delta, or an explicit effect-only bus with the original component
excluded), so both collapse to silence at Blend 100 whenever
Width=0/Double=0 (delta=0) - directly violating "Blend 0/25/50/75/100
should all produce the original input" when there is no VX effect to
expose. A `[§20]` regression test (tests/VXWidthShellCheck.cpp) caught
this against a literal Candidate-A implementation during the first
build pass.

The endpoint actually implemented is a third, simple option per this
section's own escape hatch: **extrapolated delta**, not substituted delta.

```text
output = dry + blendGain * (processed - dry)

blendGain(0)   = 0     (Blend 0 = dry, §8)
blendGain(0.5) = 1     (Blend 50 = NORMAL_VX, exact - §2/§22)
blendGain(1.0) = 2.0   (Blend 100 = twice as far past dry as Blend 50 -
                        as far again in the same direction the effect
                        already moved the signal, not a separately-
                        constructed "effect-only" signal)
```

This keeps §7's delta decomposition and §9's "more effect-forward than
Blend 50" intent, but fixes the §20 collapse structurally: when
`processed == dry` (delta = 0), `output = dry` at *every* Blend position,
not only at Blend 0. It also makes Blend 50 algebraically exact (not just
close - stronger than §22 currently requires) and keeps §23 monotonicity
automatic (RMS(output-dry) scales linearly with blendGain).

The `2.0` ceiling is a reasoned default, not a listening-confirmed one -
this section's requirement stands: **run the listening pass in this
section against the actual corpus, and tune or replace kBlendMaxGain
(VxWidthProcessor.cpp) based on the result**, same as either named
candidate would have needed. Candidate A/B's originally-specified
"effect-only" character can still be recovered by extrapolating a
constructed effect-bus signal instead of the delta above, if listening
shows the plain extrapolation isn't distinct enough from Width/Double's
own knobs - that would be a small, local change to what "processed" and
"delta" are built from, not to the Blend architecture itself.

**Tuning history: a single global gain could not satisfy both "audible" and
"safe."** `1.5` was tried first and found too subtle (extrapolation only
adds `(K-1)*delta` beyond NORMAL_VX, so at `1.5` the 50->100 half added only
*half* as much change as 0->50 already did - 4-12% Side-energy increase on
moderate Width-only settings, genuinely inaudible). `2.0` fixed that
(symmetric halves, clean 2.0x Side-RMS lift) but `2.5`/`3.0` measurably
clipped (peak up to 1.05-1.11) on the Width=100/Double=100 and
Width=-100/Double=100 corners specifically.

**Root cause and fix: one gain was doing two unrelated jobs.** Splitting the
per-sample blend into three components (Mid/existing-Side/generated-Side -
already required for the polarity fix below) exposed that a single ceiling
was simultaneously (a) amplifying newly GENERATED effect content, which is
headroom-cheap (starts at dry=0, only grows), and (b) extrapolating the
EXISTING-signal modification, which is headroom-expensive (scaling an
already-present level). The production architecture is now:

```text
existingBlendGain  (kExistingContentMaxGain = 2.0, fixed)
    -> Width's existing-Side term (sideOriginal*sideScale*sideGain) and
       existing-Mid's own compGain-driven coefficient
    -> conservative: this is also what the polarity-inversion fix (below)
       clamps at 0, so it must stay proven clip-free on its own

generatedBlendGain (requested up to generatedBlendMaxGainState = 3.0)
    -> Double's generated Mid contribution, Width's decorrelated Side,
       Double's ADT Side - all content with no dry component to invert
    -> REQUESTED ceiling only - the gain actually applied is this value
       further limited by a per-block headroom solve, not used directly
```

**Headroom-limited generated gain (VxWidthProcessor.cpp, processProduct()).**
Rather than picking one fixed generatedBlendMaxGainState value that's
"safe everywhere" (which the data showed throws away real audible range on
the ~95% of settings that have ample headroom), the actual applied gain is
solved exactly, per block, from the real decomposition already computed:

```text
output = base + g * generated   (per Mid/Side component, per §7's dry+gain*delta)

solve for the largest g in [0, requestedGain] such that
  |base + g*generated| <= kOutputHeadroomCeiling (0.98, matching
                                                   OutputTrimmer's own ceiling)
  holds for every sample in the block, both channels
```

This is a closed-form per-sample linear solve (`g <= (ceiling - base) /
generated`, sign-aware), not a peak estimate or a fraction of NORMAL_VX's
own peak - the two doubly-summed components can reinforce or partially
cancel per sample depending on relative sign, and the exact solve accounts
for that. The block-wide minimum g becomes `rawSafeGeneratedGain`; it's
applied with an **instant snap when it drops** (this block's own solve is
already exact, so anything less than a full, immediate snap would mean
knowingly using a gain looser than what this block's own data proves is
safe - the same reactive-lag flaw the OutputTrimmer's within-block ramp
already has, and worth avoiding here specifically since it's a
*predictable, reproducible* overshoot rather than a rare transient) and a
**slow, smoothed recovery when headroom returns** (0.30s, purely a taste
choice to avoid audible pumping on transient material - has no correctness
requirement). The final `actualGeneratedGain = min(requestedGain,
smoothedSafeGeneratedGain)`.

Result: moderate/realistic settings (Width or Double alone at 25/50%, or
both at 50%) get the FULL requested 3.0x gain with peak comfortably under
0.90 - genuinely audible, ~2-3x Side-RMS lift from Blend 50->100. The two
extreme corners (Width=100/Double=100, and Width=-100/Double=100 where
100% of the Side signal is generated content) are automatically capped to
whatever headroom allows - measured peak pins exactly at 0.9800 (the
ceiling) regardless of the requested 2.0/2.5/3.0, with **zero measured
overshoot**, confirmed by both the `[Blend split-gain measurements]`
diagnostic and the gated `[Review fix: headroom-limited Blend gain never
exceeds output ceiling]` regression in tests/VXWidthShellCheck.cpp.

**Precise scope of the guarantee (review follow-up - do not overstate
this).** The solve ensures generated-content Blend extrapolation is never
*itself* the cause of exceeding the ceiling, given the non-generated/base
contribution is already within it. It is not a claim that "Blend
guarantees output never exceeds 0.98" in general - a sample where
`|base| > ceiling` on its own (a pre-existing overload independent of
Blend, e.g. from another control's own headroom use) cannot be rescued by
any value of the generated gain; that case degrades gracefully to
`g -> 0` (never makes the pre-existing overload worse) but does not fix
it, and remains OutputTrimmer's responsibility exactly as before Blend
existed. A design rule follows directly from this: **the headroom limiter
should normally be INERT for Blend<=50** (NORMAL_VX, the "current default
VX sound" contract in §2/§22, should virtually never need the generated
gain reduced below its own requested <=1.0 value to stay under 0.98 - if
it did, Blend would be silently altering the Blend-50 reference point
itself rather than leaving it to the final OutputTrimmer like everything
else). Confirmed by the gated `[Review fix: headroom limiter inert at
Blend<=50 across representative material]` regression (mono/stereo,
positive/negative Width, Double 0/100, Blend 0/10/25/40/50): applied
generated gain equals requested in every case tested.

This is deliberately a **hard technical headroom constraint only** - it
never evaluates correlation, mono-safety, or "does this sound tasteful"
(those stay exactly where §25/§26 put them, evaluated on NORMAL_VX before
Blend exists), and it never touches Width/Double/Tightness/Focus/ADT
geometry - only how far Blend's own presentation-layer extrapolation is
allowed to go. It does not violate §25's "don't feed Blend back into DSP
decisions," because nothing here is a DSP decision; it's Blend limiting
itself against a fixed technical number it shares with the OutputTrimmer.

**Review fix (found before listening, not by ear): existing-Side polarity
inversion.** A single uniform gain applied to the whole L/R delta has a real
bug, not just a tuning question: Width's *existing*-Side term
(`sideOriginal * sideScale * sideGain` - the direct narrowing/widening of
the original image, as distinct from newly generated content) can reach
exactly 0 at NORMAL_VX (Width=-100 fully collapses it to mono). Extrapolating
that already-zero destination further in the same direction inverts its
sign by Blend 100 - L/R polarity swap on narrowed material, not "more
effect". Fixed by decomposing the per-sample blend into Mid/existing-Side/
generated-Side components and clamping only the existing-Side coefficient
at 0 (`juce::jmax(1.0f + existingBlendGain*(sideScale*sideGain*compGain -
1.0f), 0.0f)`), leaving generated Width/Double content free to extrapolate
(it has no dry component to invert - starts at 0, only grows). A
`[Review fix]` regression test (Width=-100, Double=0, Blend 0..100,
checking input/output Side cross-correlation never goes negative) covers
this in tests/VXWidthShellCheck.cpp. Confirmed algebraically: the
unclamped coefficient at Width=-100 is `1.0, 0.5, 0.0, -0.25, -0.5` for
Blend `0/25/50/75/100` - exactly the inversion predicted, now clamped to
`1.0, 0.5, 0.0, 0.0, 0.0`.

**Still outstanding before shipping generatedBlendMaxGainState=3.0:** the
correlation/mono-downmix/peak measurements run so far are still synthetic-
corpus, non-gated diagnostics (`tests/VXWidthShellCheck.cpp`'s `[Blend
split-gain measurements]`), not the full §31 corpus (12 material types) or
a human listening pass. The headroom solve guarantees no clipping, but
does NOT evaluate whether the resulting sound is musically good at the
corners where it engages heavily (e.g. Width=-100/Double=100 at Blend 100
runs at a much-reduced actual gain purely to avoid clipping - whether that
reduced-emphasis result still sounds intentional, or instead sounds like
the knob "gave up," is exactly the kind of judgment §11 defers to a human).
Run the full corpus and listen before shipping; re-check with the
diagnostic if `kExistingContentMaxGain` or `generatedBlendMaxGainState` are
tuned again.

---

# 12. Blend 100 does not have to mean literal zero dry

This is explicit.

Blend 100 means:

> maximum useful effect emphasis.

It does **not** necessarily mean:

> mathematically remove every sample of the original source.

If literal zero-dry produces a result that is obviously musically inferior, do not force it.

The product meaning is perceptual and creative, not dogmatic.

So the contract is (post-release — see §2/§22):

```text
Blend 0   = exact original

Blend 50  = recommended/default VX balance, exactly the pre-Blend sound

Blend 100 = maximum useful VX-forward sound
```

Both 0 and 50 are hard algebraic endpoints. 50 is also a hard **product**
endpoint (the centre-referenced default) — see §2/§22 for how the
implemented gain law makes this exact by construction, not just close.

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

# 21. Blend 0 null testing

This splits into two separate tests — a static endpoint invariant and a
separate automation-behaviour check. Do not conflate them.

## Static endpoint test

Hold `Blend = 0` from processor reset/priming through the complete test
render. After any unavoidable DSP warm-up required by the existing
architecture:

```text
output == untouched dry
```

within floating-point tolerance.

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

This is the hard acceptance criterion. `Blend = 0` is an exact DSP endpoint.

## Automation test

For transitions `50 → 0` or `100 → 0`, do **not** require instantaneous null
while smoothing is still settling. Instead verify:

* the transition is click-free;
* output moves monotonically toward dry;
* no overshoot;
* no stale generated content lingers past the transition;
* once the smoothed Blend parameter reaches/settles at 0, output nulls
  against dry.

Moving away from 0 should ramp normally rather than jump. Parameter
automation approaches the Blend 0 endpoint through the normal smoothing
law — it is not required to be discontinuously exact mid-ramp.

---

# 22. Blend 50 exact-reproduction check

**Hard pass/fail requirement, matching §21's Blend-0 treatment.** VX Width
1.0 has shipped, so Blend 50 must reproduce the approved **PRE-BLEND
BASELINE** commit (§34) exactly (or within normal floating-point
tolerance) — not merely close in intent and character. §34's corrected
ADT placement, coherence guardrail, and loudness-compensator fixes must
already be in that baseline.

By construction, `blendGain(0.5) = 1` and `existingBlendGain`/
`existingSideBlendGain`/`midOwnBlendGain` all reduce to their pre-Blend
values at that gain, so `midOut`/`side` (and therefore `left[i]`/`right[i]`)
at Blend 50 are algebraically unchanged from the pre-Blend build - the
gain law satisfies this requirement structurally, not just empirically.

Compare with the processor at:

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

Record the PRE-BLEND BASELINE commit hash and the reference-render fixture
ID alongside the Blend regression notes. A deviation here is a regression,
not a listening judgement call - it means the gain law's Blend-50 identity
has been broken and must be fixed, not re-justified by ear.

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

Superseded by §6: loudness management no longer sits after Blend. Preferred
order is now:

```text
dry input
   ↓
Width/Double DSP
   ↓
spatial/phase safety
   ↓
loudness management  (Blend-independent — see §26)
   ↓
Blend
   ↓
output trim / hard output safety only
```

Phase-risk and mono guardrail restraints (monoRiskRestraint,
phaseRiskRestraintWidth/Double, etc.) continue to be measured from the
widthOnly/doubleOnly hypothetical outputs — i.e. from NORMAL_VX's own DSP
geometry, never from the live Blended output. This is unchanged by Blend
and must not be broken when Blend is added: Blend must not alter what these
guardrails see or feed back into.

However, high Blend values may expose effect-only content that creates additional audible risk.

Therefore also test the **final Blend output** for:

* severe negative correlation;
* mono collapse;
* excessive LF Side;
* output overload.

Do not feed normal Blend changes back into the Width target-seeking solver unless there is a genuine technical reason.

---

# 26. Loudness management

**This section is now a hard architectural rule, not gentle guidance,**
following the §6/§25 revision:

> The loudness compensator must calculate its gain from the normal
> Width/Double processing path exactly as if Blend were fixed at 50. Blend
> must never feed its live output RMS back into the loudness compensator.

`compGain` remains a property of NORMAL_VX (the underlying Width/Double
processing), not of the Blend setting. Concretely:

```text
Blend 0
= untouched dry
= no compGain applied to dry

Blend 50
= NORMAL_VX, including its existing baseline loudness compensation

Blend 100
= EFFECT_ENDPOINT, built on the same underlying compGain basis
  rather than being independently renormalised toward input RMS
```

For EFFECT_ENDPOINT (§10-12), reuse the same `compGain` derived from
NORMAL_VX where applicable. Do not compute a second, Blend-100-specific
adaptive compensation value — a second normaliser would make Candidate A
and Candidate B converge perceptually for the wrong reason, defeating the
listening comparison in §11.

The reasoning: the compensator's feedback loop drives toward
`compGain = 1/effectRms` (a fixed point relative to whatever it measures).
If it measured Blend's own live output, pushing Blend toward 100 to expose
more effect would cause the compensator to partially renormalise that
increase away — directly undermining the control it's supposed to serve.
Keeping the compensator's input Blend-independent removes that feedback
path entirely.

The output trimmer downstream of Blend may still prevent genuine overload,
but it must remain a hard-safety mechanism, not a loudness normaliser. It
must not:

* flatten the Blend control;
* make Blend 100 sound as conservative as 50;
* introduce false mono/phase restraint;
* cause Width/Double safety cross-coupling.

Blend should remain clearly audible.

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

so they land on the default/recommended VX balance (§2).

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

**Review fix: an exceptional-path allocation was found and removed.** The
headroom-solve scratch buffers (§9-12's implementation) were initially
sized in `prepareSuite()` to the host's declared block size, with a
defensive `.assign()` fallback in `processProduct()` in case a host ever
sent a larger block than declared. That fallback was itself a possible
audio-thread allocation, "no allocations in normal operation" is not the
same guarantee as "no allocations" - removed per review. The VST3/AU/AAX
contract (enforced by the JUCE wrapper layer, not just convention) is that
a host must call `prepareToPlay()` again before sending a larger block
than previously declared, so the scratch size from `prepareSuite()` is a
true upper bound, not a normal-case assumption; a debug-only `jassert`
replaces the runtime resize.

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

The current pending architecture work still has priority, and must be
landed as a named, stable baseline before Blend implementation starts —
not merely "in progress on the same branch":

```text
Step 1
Land/commit corrected ADT M/S spatial placement.

Step 2
Land/commit processed-output coherence guardrail.

Step 3
Revalidate and choose direct-Side ceiling (12/15/18 dB).

Step 4
Land/commit corrected loudness-compensator/safety coupling.

Step 5
Run full regressions/listening and declare that commit:
PRE-BLEND BASELINE.

Step 6
Generate reference renders from that exact commit (§22).

Step 7
Begin Blend implementation.
```

Do not begin Blend implementation until steps 1-5 have landed as a stable
baseline. If Blend work is performed in the same branch regardless, do not
use Blend results to validate the old ADT geometry or old coherence
measurement.

Blend must be evaluated against, and its Blend-50 contract (§22) defined
against, the PRE-BLEND BASELINE commit specifically — not an older VX
Width build, and not a moving target.

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

And these Blend landmarks hold, both 0 and 50 algebraic endpoints post-release (§2/§12/§22):

```text
0
= exact untouched original

50
= recommended/default VX balance, exactly the pre-Blend sound
  (byte-exact/floating-point-tolerance - see §22)

100
= strongest musically useful VX-forward result
```

The top half of Blend is deliberately **not defined as literal zero-dry** unless listening proves that this is genuinely the best endpoint.

The guiding principle is:

> **Blend 0 is mathematically dry. Blend 50 is exactly the pre-Blend VX result. Blend 100 is perceptually the maximum useful effect.**

That is the shipped product contract (§22).
