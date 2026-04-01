# VX Rebalance Phase 3 – Line-by-Line Implementation Guide

This document is a line-by-line implementation guide for the next stage of VX Rebalance, based on the current uploaded codebase.

It is written against these files:
- DSP core: turn8file2
- DSP header: turn8file3
- Processor/UI layer: turn8file0
- Processor header: turn8file1

The purpose is simple:

- remove the remaining EQ-like behavior
- make source ownership authoritative
- make the slider contract real
- make the renderer behave like a source controller

---

# 1. High-level objective

The engine already has:
- harmonic clustering
- tracked objects
- transient events
- source priors
- ownership mapping
- mask smoothing
- object override stage
- foreground/background render stage

The remaining problem is that the final audible result still behaves too much like spectral gain shaping.

That happens because:
- cluster source normalization is still wrong
- object ownership is still too polite
- foreground/background render is still not authoritative enough
- final compositeGain still behaves like a softened EQ scalar

This guide fixes that.

---

# 2. Files you will edit

## Main file
VxRebalanceDsp.cpp

## Supporting declarations
VxRebalanceDsp.h

## UI and wording check only
VxRebalanceProcessor.cpp

No meaningful structural work is needed in the processor file right now.

---

# 3. Step 1 – Fix cluster source normalization

## Why
Right now cluster source influence is normalized like this:

```cpp
cluster.sourceScores[s] / (cluster.strength + kEps)
```

That is not a proper probability.
It biases source ownership toward louder clusters rather than more strongly owned clusters.

This hurts:
- vocal vs guitar decisions
- tracked ownership stability
- cluster confidence behavior

## Where to edit
In VxRebalanceDsp.cpp, inside computeMasks(), find the block:

```cpp
// Apply harmonic cluster influence to raw weights
for (int c = 0; c < harmonicClusterCount; ++c) {
    ...
    if (maxFalloff > 0.0f) {
        const float conf = cluster.confidence * maxFalloff;
        for (int s = 0; s < kSourceCount; ++s) {
            const float scoreNorm = cluster.sourceScores[static_cast<size_t>(s)] /
                                   (cluster.strength + kEps);
            rawWeights[static_cast<size_t>(s)][static_cast<size_t>(k)] *=
                lerp(1.0f, 0.5f + scoreNorm * 1.5f, conf);
        }
```

## Replace that inner normalization with this

```cpp
float sourceScoreSum = 0.0f;
for (int i = 0; i < kSourceCount; ++i)
    sourceScoreSum += cluster.sourceScores[static_cast<size_t>(i)];

for (int s = 0; s < kSourceCount; ++s) {
    const float scoreNorm =
        cluster.sourceScores[static_cast<size_t>(s)] / (sourceScoreSum + kEps);

    rawWeights[static_cast<size_t>(s)][static_cast<size_t>(k)] *=
        lerp(1.0f, 0.5f + scoreNorm * 1.5f, conf);
}
```

## Result
This makes cluster ownership probabilistic rather than magnitude-biased.

Expected audible effect:
- less unstable midrange source swapping
- cleaner vocal/guitar separation
- better consistency in sustained objects

---

# 4. Step 2 – Make object ownership override stronger

## Why
You already call:

```cpp
applyObjectOwnershipToMasks(smoothedMasks);
```

That is correct. But the whole system is still too hesitant.
When object ownership confidence is high, the engine must commit.

Right now the ownership stage still behaves like advice.

It needs to become authority.

## Where to edit
In VxRebalanceDsp.cpp, find the implementation of:

```cpp
void Dsp::applyObjectOwnershipToMasks(std::array<std::array<float, kBins>, kSourceCount>& masks) noexcept
```

If it does not exist yet in a strong form, implement or replace it with the version below.

## Replace implementation with this

```cpp
void Dsp::applyObjectOwnershipToMasks(
    std::array<std::array<float, kBins>, kSourceCount>& masks) noexcept
{
    for (int k = 0; k < kBins; ++k)
    {
        const int clusterId = binOwningCluster[static_cast<size_t>(k)];
        const float confidence = binOwnershipConfidence[static_cast<size_t>(k)];

        if (clusterId < 0 || clusterId >= kMaxTrackedClusters)
            continue;

        if (confidence < 0.30f)
            continue;

        const auto& tracked = trackedClusters[static_cast<size_t>(clusterId)];
        if (!tracked.active)
            continue;

        const int dominant = tracked.dominantSource;
        const float agedConfidence =
            confidence * (1.0f - std::exp(-tracked.ageFrames * 0.1f));

        const float push = 0.8f * juce::jlimit(0.0f, 1.0f, agedConfidence);

        for (int s = 0; s < kSourceCount; ++s)
        {
            if (s == dominant)
            {
                masks[static_cast<size_t>(s)][static_cast<size_t>(k)] =
                    lerp(masks[static_cast<size_t>(s)][static_cast<size_t>(k)], 1.0f, push);
            }
            else
            {
                masks[static_cast<size_t>(s)][static_cast<size_t>(k)] *= (1.0f - push);
            }
        }

        if (dominant != otherSource && agedConfidence > 0.6f)
        {
            masks[static_cast<size_t>(otherSource)][static_cast<size_t>(k)] *= 0.3f;
        }
    }
}
```

## Result
This is the point where source ownership actually becomes audible.

Expected audible effect:
- less smear
- less ambiguity
- clearer source dominance
- stronger sense that the vocal or guitar is a real object

---

# 5. Step 3 – Keep final gain in contribution space

## Why
The current code has already improved here.
In processFrame() the final stage now does:

```cpp
const float gain = juce::jlimit(0.0f, 2.0f, compositeGain[k]);
const float ratio = gain;
```

That is much better than the older dB-oriented version.

Do not undo this.

## What to keep
Leave this structure in place:

```cpp
const float gain = juce::jlimit(0.0f, 2.0f, compositeGain[...]);
const float ratio = gain;
```

## What not to reintroduce
Do not reintroduce:
- mappedSourceGainDb
- juce::Decibels::decibelsToGain(...)
- asymmetric source gain logic
- final-stage dB clamping

The product contract is now:
- -100% = 0x
- 0% = 1x
- +100% = 2x

That is contribution scaling, not EQ gain.

---

# 6. Step 4 – Implement computeSourceContributionMultiplier() exactly

## Why
This is the heart of the slider contract.

You already declared:

```cpp
float computeSourceContributionMultiplier(int source, float sliderNormalized, float strength, float confidence) const noexcept;
```

Now it must become the central mapping.

## Where to edit
In VxRebalanceDsp.cpp, add or replace the implementation of:

```cpp
float Dsp::computeSourceContributionMultiplier(...)
```

## Use this exact implementation

```cpp
float Dsp::computeSourceContributionMultiplier(
    int source,
    float sliderNormalized,
    float strength,
    float confidence) const noexcept
{
    juce::ignoreUnused(source);

    const float sliderSigned = (sliderNormalized - 0.5f) * 2.0f;
    const float effectiveMove = sliderSigned * strength;
    const float target = 1.0f + effectiveMove;

    return lerp(1.0f, target, juce::jlimit(0.0f, 1.0f, confidence));
}
```

## Result
This makes the UI contract mathematically explicit.

At Strength = 1:
- 0.0 slider -> 0.0 contribution
- 0.5 slider -> 1.0 contribution
- 1.0 slider -> 2.0 contribution

With lower confidence:
- the move softens toward 1.0

---

# 7. Step 5 – Rewrite buildForegroundBackgroundRender()

## Why
This is the most important change in the entire phase.

Right now the renderer still feels too blended.
You need it to:
- identify the dominant source
- identify the second source
- derive confidence from the gap
- apply stronger weighting to the dominant source
- suppress competitors
- then apply contribution multipliers

This is what changes the sound from EQ-ish to source movement.

## Where to edit
In VxRebalanceDsp.cpp, find:

```cpp
void Dsp::buildForegroundBackgroundRender(const std::array<float, kBins>& analysisMag) noexcept
```

Replace the body with the implementation below.

## Replace with this

```cpp
void Dsp::buildForegroundBackgroundRender(
    const std::array<float, kBins>& analysisMag) noexcept
{
    juce::ignoreUnused(analysisMag);

    for (int k = 0; k < kBins; ++k)
    {
        float best = 0.0f;
        float second = 0.0f;
        int dominant = otherSource;

        for (int s = 0; s < kSourceCount; ++s)
        {
            const float m = smoothedMasks[static_cast<size_t>(s)][static_cast<size_t>(k)];

            if (m > best)
            {
                second = best;
                best = m;
                dominant = s;
            }
            else if (m > second)
            {
                second = m;
            }
        }

        const float localConfidence =
            juce::jlimit(0.0f, 1.0f, (best - second) * 2.0f);

        const float ownershipConfidence =
            binOwnershipConfidence[static_cast<size_t>(k)];

        const float confidence = std::max(localConfidence, ownershipConfidence);

        float total = 0.0f;
        const bool strongOwnership = ownershipConfidence > 0.6f;

        for (int s = 0; s < kSourceCount; ++s)
        {
            const float slider = currentControlValues[static_cast<size_t>(s)];
            const float strength = currentControlValues[static_cast<size_t>(kStrengthIndex)];

            const float contribution =
                computeSourceContributionMultiplier(s, slider, strength, confidence);

            float weight = smoothedMasks[static_cast<size_t>(s)][static_cast<size_t>(k)];

            if (strongOwnership)
            {
                if (s == dominant)
                    weight *= (1.0f + 0.6f * confidence);
                else
                    weight *= (1.0f - 0.8f * confidence);
            }
            else
            {
                if (s == dominant)
                    weight *= (1.0f + 0.4f * confidence);
                else
                    weight *= (1.0f - 0.5f * confidence);
            }

            if (s == otherSource && strongOwnership && dominant != otherSource)
            {
                weight *= 0.3f;
            }

            total += weight * contribution;
        }

        compositeGain[static_cast<size_t>(k)] =
            juce::jlimit(0.0f, 2.0f, total);
    }
}
```

## Result
This is the renderer finally trusting the analysis.

Expected audible effect:
- target source actually moves
- non-target sources yield
- boosts feel like emergence, not tone lift
- cuts feel like source reduction, not dullness

---

# 8. Step 6 – Processor layer check

## Current state
Your processor UI wording is already much better:

```cpp
"Vocal Presence. 0% = unchanged, -100% = reduced, +100% = enhanced."
```

That is correct and should stay.

## What to keep unchanged
Do not change:
- parameter count
- strength control structure
- latency compensation path
- smoothing in processor layer
- recording-type selector

The processor shell is already in the right place.

---

# 9. Step 7 – Listening test protocol

After implementing the changes above, test using fixed clips.

## Test set
Use:
- studio vocal + guitar
- live room band clip
- phone speech + acoustic guitar
- drum-heavy loop
- bass-heavy mix
- dense stereo music

## Test moves
For each clip test:
- vocals +100%
- vocals -100%
- guitar +100%
- guitar -100%
- drums -100%
- bass -100%
- all neutral
- strength at 25%, 50%, 100%

## What success sounds like

### Vocals +100%
Should sound like:
- voice moves forward
- not merely upper-mid lift

### Vocals -100%
Should sound like:
- voice reduces
- not just midrange scoop

### Guitar -100%
Should sound like:
- guitar body recedes
- not generic dulling

### Drums -100%
Should sound like:
- attacks reduce
- cymbals and snare soften
- not whole mix losing air

### Bass -100%
Should sound like:
- bass body reduces more than kick
- not broad low-shelf behaviour

### Neutral
Should sound effectively transparent apart from latency alignment

---

# 10. Step 8 – What not to touch yet

Do not add:
- more UI controls
- more source types
- new ML code
- more heuristic coefficients
- more recording modes

You are already past the point where more features will help.

Right now quality comes from:
- stronger authority
- cleaner normalization
- better rendering commitment

---

# 11. Final implementation order

Use this order exactly.

## 1
Fix cluster source normalization

## 2
Implement computeSourceContributionMultiplier() exactly

## 3
Replace buildForegroundBackgroundRender() fully

## 4
Strengthen applyObjectOwnershipToMasks()

## 5
Add late-stage other suppression

## 6
Run listening tests and only then adjust constants

---

# 12. Final expected outcome

After these changes, VX Rebalance should stop sounding like:
- spectral EQ with source hints

and start sounding like:
- a real source presence tool

Not perfect demixing.
Not clean stems.
But a real, convincing product.

That is the correct target for this DSP architecture.

---

# 13. Final note

The system is already smart enough.

The missing part is not intelligence.
It is commitment.

This guide makes the renderer finally trust:
- the clusterer
- the tracker
- the ownership map
- the slider contract

That is the last meaningful step before tuning.
