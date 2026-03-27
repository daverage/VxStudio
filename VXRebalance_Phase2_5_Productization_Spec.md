# VX Rebalance Phase 2.5 Productization Spec

## Goal

Move VX Rebalance from an analysis-rich spectral rebalance prototype into a product that **feels like source control**, not animated EQ.

The current engine now has most of the right structural pieces:
- harmonic peak detection
- harmonic clustering
- tracked clusters
- transient events
- object probability hooks
- foreground/background rendering entry point
- per-source controls plus strength
- low-end protection logic
- composite gain reconstruction

That is enough to stop thinking in terms of "what else do we add?" and start thinking in terms of:

- what actually owns the bins
- what actually drives rendering
- what the sliders should mean perceptually
- how to make the result sound like source movement rather than EQ

This phase is about making the object system **authoritative**.

---

## Core diagnosis

Right now the DSP still feels like ineffective EQ because the object system is mostly advisory.

The current shape is roughly:

1. raw per-bin source weights
2. semantic shaping
3. cluster influence
4. conditioned masks
5. smoothing
6. persistence
7. foreground/background rendering
8. composite gain

That means final output is still dominated by per-bin masks.

Result:
- spectral motion
- partial source suppression
- source-colored EQ
- weak foreground separation
- no strong sense that a vocal, guitar, or drum object is being moved

### Product principle

**Objects must own the bins. Not merely influence them.**

---

## Product behavior requirements

The product should behave like a source-focus processor, not a stem extractor and not an EQ.

When the user moves a source slider:
- the target source should come forward or backward as a coherent object
- neighboring competing sources should yield appropriately
- the movement should not sound like random tonal shifts
- attacks and sustains should stay linked
- source ownership should persist over time unless confidence collapses

---

## Slider contract

This is a hard requirement and should be treated as part of the product contract.

### Source sliders
The source sliders are:
- Vocals
- Drums
- Bass
- Guitar
- Other

Each source slider must represent:

- **-100%** = remove as much of that detected source contribution as the engine can safely remove
- **0%** = leave that source unchanged
- **+100%** = double the effective contribution of that detected source

### Strength slider
The Strength slider scales the effect of all source sliders.

At **Strength = 100%**:
- each source slider must operate at its full contract range
- `-100%` means the engine should target full attenuation of the owned source contribution for that object or region
- `+100%` means the engine should target 2x contribution of the owned source for that object or region

At lower Strength values:
- scale the effective source move proportionally
- example: if Vocals = +100% and Strength = 50%, the effective target is +50% contribution
- example: if Guitar = -100% and Strength = 25%, the effective target is -25% contribution

### Important clarification
This contract is **not** saying:
- globally null all bins in a broad frequency band
- globally add 6 dB to a frequency region
- apply a simple source-weighted EQ curve

It means:
- the engine should target source-owned energy
- source contribution should be scaled according to detected ownership and confidence
- stronger object confidence allows stronger enforcement of the slider contract
- low confidence should gracefully soften the move to avoid artifacts

### Internal mapping requirement
Do not think of slider values as directly mapping to dB first.

Think of them as mapping to **source contribution multipliers**.

Recommended internal meaning at Strength = 100%:
- `-100%` -> target multiplier of `0.0`
- `0%` -> target multiplier of `1.0`
- `+100%` -> target multiplier of `2.0`

Then:
- scale that multiplier target by object confidence
- scale that multiplier target by Strength
- apply rendering safeguards where the engine is uncertain

### Formula guidance
Let:
- `sliderNormalized` be in `[0, 1]`
- `sliderSigned = (sliderNormalized - 0.5) * 2.0` giving `[-1, 1]`
- `strength` be `[0, 1]`

Then the target source contribution multiplier should conceptually be:

```cpp
effectiveSignedMove = sliderSigned * strength;
targetMultiplier = 1.0f + effectiveSignedMove;
```

This yields:
- `-1.0` -> `0.0`
- `0.0` -> `1.0`
- `+1.0` -> `2.0`

This is the contract to preserve at the rendering layer.

You may still convert to dB internally where useful, but the perceptual design target is contribution scaling, not EQ gain.

---

## Main implementation objective

Make object analysis authoritative in rendering.

The next phase should shift from:

- bin-first, object-assisted

to:

- object-first, bin-refined

---

## Required architecture changes

## 1. Promote object ownership to first-class authority

You already have bin ownership scaffolding:
- `binOwningCluster`
- `binOwnershipConfidence`

These must become decisive in the render path.

### New function
Add:

```cpp
void Dsp::applyObjectOwnershipToMasks(
    std::array<std::array<float, kBins>, kSourceCount>& masks) noexcept;
```

### When to call it
Call it **after**:
- conditioned mask creation
- mask smoothing
- source persistence

and **before**:
- foreground/background render build
- final composite gain build

### Logic
For each bin:
1. look up `binOwningCluster[k]`
2. if valid, read tracked cluster dominant source and source probabilities
3. read `binOwnershipConfidence[k]`
4. push the masks toward object ownership

### Required behavior
If ownership confidence is high:
- the dominant source must become clearly dominant
- competing sources must be suppressed

Do not merely multiply existing masks lightly.
Blend them toward object ownership.

Recommended shape:

```cpp
push = 0.6f * ownershipConfidence;

for each source s:
    if s == dominant:
        masks[s][k] = lerp(masks[s][k], 1.0f, push);
    else:
        masks[s][k] *= (1.0f - push * 0.7f);
```

### Why this matters
This is the moment where the processor stops hedging and starts committing.

Without this step, the engine still sounds like source-flavored EQ.

---

## 2. Make tracked clusters drive rendering, not just analysis

Tracked clusters are currently valuable but still too observational.

They need to drive:
- ownership
- source confidence
- foreground priority
- suppression bias
- contribution scaling

### Requirement
For every tracked cluster maintain:
- `dominantSource`
- `sourceProbabilities`
- `confidence`
- `estimatedF0Hz`
- `onsetStrength`
- `sustainStrength`
- cluster life state

### Requirement
For each bin associated with a tracked cluster:
- use the cluster as the primary authority
- use per-bin cues only as refinement

### Design rule
If object and per-bin analysis disagree:
- prefer object analysis when confidence is high
- prefer per-bin analysis when confidence is low

---

## 3. Strengthen transient-to-tonal linkage

Transient events exist, but they must influence the next tonal object strongly enough to be audible.

### Requirement
When a transient event is detected:
- find the nearest active tracked cluster in frequency and time
- inject onset evidence into that cluster
- bias source probabilities based on the transient type

### Example behavior
- a guitar pick transient should reinforce the following guitar sustain cluster
- a vocal consonant onset should reinforce the following vocal cluster
- a drum transient should remain primarily transient-owned unless strong tonal continuation suggests otherwise

### Why
This is what makes:
- attack and sustain feel connected
- guitar feel like a played object
- vocals feel coherent
- drums feel separated instead of merely dulled

---

## 4. Rebuild foreground/background rendering as a real authority stage

The foreground/background stage must stop being a small adjustment and become the perceptual engine.

### Add requirement
For each bin:
1. determine dominant source and confidence
2. determine whether the bin is in a foreground-owned object
3. compute:
   - source contribution multiplier
   - foreground priority
   - competitor suppression
   - residual preservation

### Required conceptual behavior
Foreground source:
- receives stronger, cleaner rendering
- preserves body and continuity
- is allowed to approach slider target multiplier

Background competitors:
- are attenuated more aggressively
- are not just broadly EQ cut
- retain a small floor to avoid holes

Residual or low-confidence material:
- should remain more conservative
- should avoid dramatic suppression

### Required implementation direction
Add or expand:

```cpp
void Dsp::buildForegroundBackgroundRender(
    const std::array<float, kBins>& analysisMag) noexcept;
```

This function should:
- derive final source contribution multipliers per bin
- convert object ownership + slider intent + confidence into final render behavior
- build `compositeGain` from source contribution logic, not only from raw masks

---

## 5. Replace simple source-gain thinking with source contribution scaling

The render stage should honor the slider contract exactly.

### Requirement
For each source:
- interpret slider position as contribution target, not immediate EQ gain
- translate that into object-aware render multipliers

Recommended conceptual mapping:
- `-100%` -> contribution target `0.0`
- `0%` -> contribution target `1.0`
- `+100%` -> contribution target `2.0`

Then scale by:
- Strength
- object confidence
- source ownership confidence
- render safety policy

### Example
If:
- vocals slider = `+100%`
- strength = `100%`
- cluster confidence = `0.9`

Then the vocal-owned object should approach 2x contribution strongly.

If:
- same settings, but confidence = `0.25`

Then the engine should move in that direction, but much more cautiously.

---

## 6. Stop using masks alone as the final audio truth

Masks are estimates. Objects are the truth candidate.

### Requirement
Final render should be based on:

1. object ownership
2. object confidence
3. source contribution contract
4. safety moderation

Masks should provide shape, but not final authority.

### Implementation note
This means `compositeGain` must be built from:
- source-owned mask weights
- object-driven contribution multipliers
- foreground/background decisions
- residual protection

Not only:
- `sum(mask * gain)`

---

## 7. Add per-object render traits

This is what makes the product feel intentional.

For each source object type, define render behavior.

### Vocals
When boosted:
- preserve formant core
- preserve intelligibility
- keep consonant edge connected
- do not over-lift noise tail

When reduced:
- suppress the owned formant body first
- keep a small consonant/air residue
- avoid robotic holes

### Guitar
When boosted:
- preserve pick-to-body continuity
- preserve harmonic body
- avoid only boosting bright upper mids

When reduced:
- suppress body and sustain together
- keep a little transient realism
- avoid hollowing the mix center

### Drums
When boosted:
- preserve attack shape
- preserve kick and cymbal separation
- avoid broad harshness inflation

When reduced:
- attenuate attack families coherently
- keep room residue and low-confidence ambience

### Bass
When boosted:
- favor stable low-end ownership
- avoid adding broad mud

When reduced:
- suppress owned bass body
- preserve kick distinction

### Other
When boosted:
- remain subtle and low confidence aware
- avoid becoming a garbage bucket

When reduced:
- only reduce when confidence that material is genuinely non-target residual is high

---

## 8. Suppress the "other" sink aggressively when ownership is strong

Other is still too available in most DSP separators, and that makes everything feel smeared.

### Requirement
If:
- tracked object ownership is high
- dominant source confidence is high
- top named sources dominate a region

Then:
- reduce `other` sharply

Tie this directly to object confidence, not just local mask ratios.

Recommended behavior:
- as object confidence rises, `other` should collapse toward a low residual floor

---

## 9. Add confidence-aware aggression policy

This must be global.

### High confidence
Allow:
- stronger object ownership overrides
- stronger slider contract enforcement
- stronger competitor suppression
- less other bleed

### Low confidence
Allow:
- softer moves
- more ambiguity tolerance
- more residual preservation
- less aggressive source removal

### Requirement
Use:
- tracked cluster confidence
- bin ownership confidence
- overall separation confidence
- transient confidence

to determine how close to the slider contract the engine should actually go

---

## 10. Add anti-EQ safeguards

These are mandatory.

### Safeguard A: no hard source move without coherent ownership
Do not apply strong moves to a source unless:
- cluster confidence is sufficient
- or bin ownership confidence is sufficient
- or transient+tonal linkage is strong

### Safeguard B: preserve object coherence
When a source is boosted or cut:
- move all owned harmonic members coherently
- do not let isolated bins swing wildly

### Safeguard C: preserve ambience floor
Maintain a low residual floor in uncertain regions.

### Safeguard D: preserve attack/body coupling
If an object has a transient and sustain relationship, do not let them diverge excessively.

These safeguards prevent the exact "ineffective EQ" sound.

---

## 11. Required processing order

Use this order for the product engine:

1. STFT analysis
2. stereo feature extraction
3. spectral envelope build
4. transient / flux analysis
5. peak detection
6. harmonic clustering
7. tracked cluster update
8. transient event update
9. object source probability update
10. object ownership write to bins
11. raw per-bin source estimate
12. semantic support shaping
13. harmonic cluster influence
14. conditioned mask generation
15. mask smoothing
16. source persistence
17. object ownership override
18. foreground/background render build
19. final contribution-scaled composite gain build
20. inverse STFT and overlap-add

The key change is:
- object ownership override and render stages must sit late in the chain
- they must not be diluted by later smoothing

---

## 12. New or expanded functions

### Ownership
```cpp
void Dsp::applyObjectOwnershipToMasks(
    std::array<std::array<float, kBins>, kSourceCount>& masks) noexcept;
```

### Rendering
```cpp
void Dsp::buildForegroundBackgroundRender(
    const std::array<float, kBins>& analysisMag) noexcept;
```

### Contribution scaling
```cpp
float Dsp::computeSourceContributionMultiplier(
    int source,
    float sliderNormalized,
    float strength,
    float confidence) const noexcept;
```

### Priority and suppression
```cpp
float Dsp::computeSourceForegroundPriority(
    int source,
    float userControl,
    float confidence) const noexcept;

float Dsp::computeSourceSuppressionBias(
    int source,
    float userControl,
    float confidence) const noexcept;
```

---

## 13. Render contract details

### Contribution multiplier mapping
At Strength = 100%:
- source slider `-100%` must map to target contribution `0.0`
- source slider `0%` must map to target contribution `1.0`
- source slider `+100%` must map to target contribution `2.0`

### Strength scaling
Strength scales the signed movement around unity.

Recommended conceptual formula:

```cpp
sliderSigned = (sliderNormalized - 0.5f) * 2.0f;   // [-1, 1]
effectiveSignedMove = sliderSigned * strength;     // scaled by strength
targetContribution = 1.0f + effectiveSignedMove;   // [0, 2]
```

### Confidence moderation
Then blend with confidence:

```cpp
effectiveContribution = lerp(1.0f, targetContribution, confidence);
```

This means:
- low confidence leaves the signal closer to unchanged
- high confidence moves strongly toward the slider contract

This is the correct behavior.

---

## 14. Processor-level instruction

Keep the UI simple.
Do not add more controls.

The product should remain:
- Vocals
- Drums
- Bass
- Guitar
- Other
- Strength
- Recording Type

But internally, each source control must drive:
- contribution target
- foreground priority
- competitor suppression
- ownership enforcement depth

The help text and product explanation should be updated to describe these as:
- source presence or source focus controls

Not literal stem controls.

---

## 15. Success criteria

This phase is successful when the processor no longer sounds like source-shaped EQ.

### Vocals up
Should sound like:
- the vocal comes forward
- not simply brighter upper mids

### Guitar down
Should sound like:
- the guitar body falls back
- not just a generic midrange cut

### Drums down
Should sound like:
- attack families recede
- not simply a duller mix

### Bass down
Should sound like:
- bass body reduces more than kick or low-mid ambience
- not just a low-shelf cut

### Neutral
Should sound identical except for latency compensation.

---

## 16. Implementation order

Do these in order.

### Step 1
Implement and wire `applyObjectOwnershipToMasks()`.

### Step 2
Make `buildForegroundBackgroundRender()` authoritative enough to drive final contribution scaling.

### Step 3
Implement exact source contribution multiplier logic so sliders obey:
- `-100% -> 0x`
- `0% -> 1x`
- `+100% -> 2x`
at Strength = 100%.

### Step 4
Link transient events more strongly into tracked clusters.

### Step 5
Tune confidence moderation and ambience floor.

### Step 6
Retune source-specific render traits by listening tests.

---

## 17. Final product principle

Do not ask:
- how do we make the masks more clever?

Ask:
- how do we make the listener believe the source moved?

That is the product question now.

The answer is:
- ownership
- contribution scaling
- foreground/background rendering
- confidence-aware commitment

Once objects truly own rendering, this stops sounding like ineffective EQ and starts sounding like source control.
