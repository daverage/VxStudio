# VX Rebalance DSP-Only Update Spec

## Purpose

This document defines the full implementation update for **VX Rebalance** as a **DSP-only realtime spectral rebalance tool**.

The goal is not to create true isolated stems. The goal is to deliver the strongest practical DSP-only engine for realtime control of:

- Vocals
- Drums
- Bass
- Guitar
- Other

This update keeps the current STFT + soft-mask architecture, removes ML-path assumptions, and adds the missing structural pieces needed to push the DSP engine closer to product quality.

---

## Product target

The updated engine should:

- remain low-latency and realtime-capable on modest machines
- behave like a **source rebalance processor**, not a fake stem renderer
- improve stability and coherence when boosting or attenuating vocals and guitar
- reduce mask flicker, fizz, and residual mush
- work predictably across Studio, Live, and Phone / Rough material

The current design already applies a spectral gain field to the mix rather than attempting independent source reconstruction, which is the correct design direction for a realtime DSP-only product. fileciteturn0file0L1-L20

---

## Current system summary

The current engine already contains a solid base:

- 1024-point STFT with 256-sample hop
- sqrt Hann analysis / synthesis windowing
- overlap-add reconstruction
- multi-source soft masks
- stereo cues using mid/side weighting
- transient and steady-state heuristics
- analysis-context input for vocal dominance, intelligibility, speech presence, and transient risk
- per-source control smoothing
- recording-mode profiles for Studio, Live, and Phone / Rough

That is enough to justify evolving the current design rather than replacing it. fileciteturn0file0L1-L20 fileciteturn0file1L1-L40

---

## Core diagnosis

The current engine is strong for a DSP-only design, but it is still limited by four structural issues.

### 1. Bin-local decisions

Source inference still resolves mainly per bin. The engine uses good heuristics, but harmonically related bins are not treated as one object. That is the biggest reason vocals and guitar still blur together in the midrange. fileciteturn0file0L220-L380

### 2. No source identity memory

Masks are smoothed over time with attack and release, but there is no explicit short-term source persistence state. That means bins can still switch identity too easily when evidence is ambiguous. fileciteturn0file0L380-L520

### 3. Weak vocal vs guitar discrimination

The current arbitration between vocals and guitar is good, but it still relies on band windows, stereo placement, and local heuristics. It lacks a stronger harmonic-envelope distinction. fileciteturn0file0L320-L430

### 4. Other acts as an ambiguity sink

The residual `other` source is useful, but too much ambiguous content can fall into it instead of being redistributed intelligently between the strongest competing sources. fileciteturn0file0L330-L430

### 5. Processor still contains ML-path assumptions

The processor still exposes DSP / Demucs / UMX4 engine selection, model package creation, model download UI logic, and model-runner flow, which is no longer appropriate if the product is now DSP-only. fileciteturn0file2L1-L30 fileciteturn0file3L1-L220

---

## Required architecture changes

There are four mandatory DSP upgrades and one mandatory product cleanup.

### Upgrade A: Harmonic grouping

Add a lightweight harmonic-cluster stage.

Instead of treating each bin independently, detect strong spectral peaks and group their harmonics. Source inference should happen partly at the cluster level, then be projected back onto member bins.

This is the highest-value DSP-only improvement.

### Upgrade B: Source memory

Track recent source identity per bin and resist source switching unless the new evidence is clearly stronger.

This is separate from attack / release smoothing. It is identity smoothing rather than only value smoothing.

### Upgrade C: Vocal-formant-aware arbitration

Add a cheap spectral-envelope discriminator so vocals and guitar are not separated only by band windows and stereo width.

This should not be heavy pitch analysis. A smoothed log-spectrum envelope stage is enough.

### Upgrade D: Residual redistribution

Reduce the role of `other` as a generic uncertainty sink. When ambiguity is between two strong named sources, bias redistribution toward them unless the region genuinely looks like broad residual content.

### Upgrade E: Remove ML-path processor logic

Remove model downloads, model packaging, model-runner usage, multi-engine selectors, and ML-mask calls. Reframe the product as DSP-only.

---

## File-by-file implementation

# 1. `VxRebalanceDsp.h`

## Goal

Extend the DSP state so the engine can support:

- harmonic peak tracking
- harmonic clusters
- short-term source identity memory
- spectral-envelope analysis

The current header only has the state needed for the present mask engine. It needs more structural memory. fileciteturn0file1L1-L120

## Add new constants

Inside `class Dsp`, add:

```cpp
static constexpr int kMaxPeaks = 24;
static constexpr int kMaxClusters = 24;
static constexpr int kClusterHarmonics = 8;
```

Keep these conservative. This is a realtime product, not a transcription engine.

## Add new structs

Add these inside `class Dsp`:

```cpp
struct SpectralPeak {
    int bin = 0;
    float hz = 0.0f;
    float magnitude = 0.0f;
};

struct HarmonicCluster {
    bool active = false;
    int rootBin = 0;
    float rootHz = 0.0f;
    float strength = 0.0f;
    int memberCount = 0;
    std::array<int, kClusterHarmonics> memberBins {};
    std::array<float, kSourceCount> sourceScores {};
    int dominantSource = otherSource;
    float confidence = 0.0f;
};
```

## Add new private methods

Add declarations for:

```cpp
void detectSpectralPeaks(const std::array<float, kBins>& analysisMag);
void buildHarmonicClusters(const std::array<float, kBins>& analysisMag);
void analyseClusterSources(const std::array<float, kBins>& analysisMag,
                           const std::array<float, kBins>& centerWeight,
                           const std::array<float, kBins>& sideWeight,
                           float transientPrior,
                           float steadyPriorScale);
void buildSpectralEnvelope(const std::array<float, kBins>& analysisMag);
float vocalFormantSupport(float hz, float localMag, float envelopeMag) const noexcept;
float guitarTonalSupport(float hz, float localMag, float envelopeMag,
                         float centered, float wide, float steadyPrior) const noexcept;
void applyClusterInfluence(std::array<std::array<float, kBins>, kSourceCount>& rawWeights) noexcept;
void applySourcePersistence(std::array<std::array<float, kBins>, kSourceCount>& conditionedMasks) noexcept;
```

## Add new state fields

Add these members:

```cpp
std::array<int, kBins> previousWinningSource {};
std::array<float, kBins> previousWinningConfidence {};
std::array<float, kBins> sourcePersistence {};

std::array<SpectralPeak, kMaxPeaks> detectedPeaks {};
std::array<HarmonicCluster, kMaxClusters> harmonicClusters {};
int detectedPeakCount = 0;
int harmonicClusterCount = 0;

std::array<float, kBins> smoothedLogSpectrum {};
std::array<float, kBins> spectralEnvelope {};
```

## Update `reset()` expectations

When resetting:

- set `previousWinningSource` to `otherSource`
- set `previousWinningConfidence` to `0.0f`
- set `sourcePersistence` to `0.0f`
- clear peak and cluster state
- clear `smoothedLogSpectrum` and `spectralEnvelope`

This is necessary so transport jumps and prepare/reset operations do not carry stale classification memory. fileciteturn0file0L100-L170

---

# 2. `VxRebalanceDsp.cpp`

## 2.1 Fix the mode-profile field order

### Problem

The header defines `RebalanceModeProfile` in this order:

1. vocals
2. bass
3. drums
4. guitars
5. other

But the initializer in the cpp reads like:

1. vocals
2. drums
3. bass
4. guitar
5. other

Based on the shared code, bass and drums appear to be swapped in the profile initialization. That needs to be corrected first. fileciteturn0file0L20-L100 fileciteturn0file1L20-L50

### Required change

Rebuild each `kModeProfiles` initializer so the field order matches the header exactly.

Do this before any deeper DSP work.

---

## 2.2 Build a spectral envelope every frame

### Goal

Create a lightweight spectral-envelope representation for use in vocal vs guitar arbitration.

### Required call site

At the start of `computeMasks()`, after `meanMag` is computed, call:

```cpp
buildSpectralEnvelope(analysisMag);
```

### Implementation notes

Use a smoothed log-spectrum approach.

Pseudo:

```cpp
for each bin k:
    smoothedLogSpectrum[k] = log(max(kEps, analysisMag[k]));

for each bin k:
    spectralEnvelope[k] = average(smoothedLogSpectrum[k - r ... k + r]);
```

Suggested radius:

- 6 to 10 bins

Keep this cheap and stable.

### Why

This gives the engine one more dimension for discrimination:

- vocals tend to show shaped midrange envelope behaviour
- guitar tends to show more harmonic peak prominence relative to the envelope

---

## 2.3 Add spectral peak detection

### Goal

Detect strong peaks that can seed harmonic clusters.

### New call sites

Before the main per-bin raw-weight loop inside `computeMasks()`, call:

```cpp
detectSpectralPeaks(analysisMag);
buildHarmonicClusters(analysisMag);
analyseClusterSources(analysisMag, centerWeight, sideWeight,
                      transientPrior, steadyPriorScale);
```

### Peak detection rules

Only search in roughly:

- 80 Hz to 5000 Hz

A bin is a valid peak if:

- it is greater than its immediate neighbors
- it exceeds a local average over a small window
- it exceeds a threshold relative to `meanMag`

Keep only the strongest peaks up to `kMaxPeaks`.

### Suggested behaviour

Use a modest bias toward meaningful musical peaks. Ignore tiny peaks that will only create noisy clusters.

---

## 2.4 Build harmonic clusters

### Goal

Group related harmonic bins so the engine can reason about coherent source objects, not just isolated bins.

### Implementation

For each detected peak:

- treat it as a candidate root
- search for approximate harmonic bins near `2f`, `3f`, `4f`, up to `8f`
- allow a slightly wider tolerance at higher frequencies
- build a `HarmonicCluster`
- sum member magnitudes to estimate cluster strength

### Constraints

Do not over-engineer this.

This is not a precise F0 tracker. It is a coherence aid.

### Output

Each cluster should contain:

- root bin / frequency
- list of member bins
- aggregate strength
- per-source source scores
- dominant source
- confidence

---

## 2.5 Analyse source support per cluster

### Goal

Assign source support at the cluster level before applying influence back to bins.

### Inputs

Each cluster should be scored using:

- band-profile support
- center / side weighting
- steady prior
- transient prior
- spectral-envelope relationship

### Vocal bias

Boost vocals when the cluster is:

- in vocal band windows
- center-heavy
- not strongly transient
- shaped by vocal-like envelope behaviour

### Guitar bias

Boost guitar when the cluster is:

- in guitar band windows
- less centered or wider
- tonally stable
- relatively peak-dominant against the envelope

### Output

For each cluster, determine:

- `sourceScores[source]`
- `dominantSource`
- `confidence`

---

## 2.6 Inject cluster influence into raw weights

### Goal

Bias per-bin raw weights using the higher-confidence cluster decision.

### Required stage

After the existing raw per-bin weights are computed, call:

```cpp
applyClusterInfluence(rawWeights);
```

### Behaviour

For every member bin of a cluster:

- boost the dominant source weight
- lightly suppress competing sources
- reduce `other` unless the cluster itself looks residual
- scale influence by cluster confidence

### Example biasing

```cpp
rawWeights[dominant][k] *= lerp(1.0f, 1.45f, confidence);
rawWeights[runnerUp][k] *= lerp(1.0f, 0.92f, confidence);
rawWeights[otherSource][k] *= lerp(1.0f, 0.75f, confidence);
```

Do not hard-assign sources. This must stay soft.

---

## 2.7 Replace the weakest vocal/guitar semantic support with envelope-aware support

### Goal

Upgrade midrange source arbitration without using ML.

### Current problem

The current semantic support block is still mostly based on local windows, stereo placement, and steady/transient logic. That is helpful, but it is still too shallow in the hardest region. fileciteturn0file0L390-L470

### Required change

For vocals and guitar, derive extra support from the spectral envelope.

For each bin:

```cpp
const float envelopeMag = std::exp(spectralEnvelope[k]);
const float vocalEnv = vocalFormantSupport(hz, analysisMag[k], envelopeMag);
const float guitarEnv = guitarTonalSupport(hz, analysisMag[k], envelopeMag,
                                           centered, wide, steadyPrior);
```

Blend those into the `semanticSupport` calculation.

### Vocal-formant support heuristic

Cheap approach:

- vocals get extra support when the surrounding envelope looks like shaped midrange formant structure
- vocals lose support when the local bin behaves like an isolated tonal peak with little envelope support

### Guitar-tonal support heuristic

Cheap approach:

- guitar gets extra support when local harmonic peaks dominate relative to a flatter local envelope
- guitar is helped by width or reduced centering
- guitar is helped by steady harmonic continuity

This does not need true cepstral analysis or expensive pitch tracking.

---

## 2.8 Add source persistence after conditioned mask normalization

### Goal

Stabilize source identity across frames.

### Required stage

After `conditionedMasks[source]` are normalized, but before the attack / release mask smoothing is applied, call:

```cpp
applySourcePersistence(conditionedMasks);
```

### Required behaviour

For each bin:

1. find the strongest source and second-strongest source
2. compare the new winner to `previousWinningSource[k]`
3. if the new winner differs but only wins by a narrow margin, bias toward the previous winner
4. if the winner stays the same, increase persistence
5. if confidence falls or the source changes decisively, reduce persistence

### Suggested logic

```cpp
if (newWinner == previousWinner) {
    sourcePersistence[k] = min(1.0f, sourcePersistence[k] + 0.08f);
} else {
    sourcePersistence[k] = max(0.0f, sourcePersistence[k] - 0.14f);
    if (winnerMargin < switchThreshold) {
        conditionedMasks[previousWinner][k] *= 1.0f + 0.18f * sourcePersistence[k];
        conditionedMasks[newWinner][k] *= 1.0f - 0.10f * sourcePersistence[k];
    }
}
```

Then update:

- `previousWinningSource[k]`
- `previousWinningConfidence[k]`

### Why

This reduces:

- vocal/guitar chatter
- transient-led source flipping
- nervous mask behaviour in ambiguous bins

---

## 2.9 Reduce `other` as a fallback sink

### Goal

Make the engine commit more often when ambiguity is really between two named sources.

### Current problem

`other` is partly used as a residual fallback bucket. That is valid, but it can absorb too much uncertainty. fileciteturn0file0L340-L430

### Required behaviour

If the top two named sources already dominate a bin, and `otherWindow` is not especially strong, reduce the `other` mask.

### Suggested rule

After conditioned masks are computed:

```cpp
if ((top1 + top2) > 0.72f && otherWindow < 0.35f)
    conditionedMasks[otherSource][k] *= 0.65f;
```

Then renormalize.

### When to allow strong `other`

Only allow strong `other` when at least one of the following is true:

- broad residual region
- high stereo width with weak named-source evidence
- very low confidence across all named sources
- outside the core bands of vocals, bass, drums, and guitar

---

## 2.10 Confirm and preserve final `compositeGain` generation

### Important note

The current `processFrame()` applies `compositeGain[k]` directly during resynthesis. The shared snippet shows `compositeGain` being initialized and applied, but the final build stage for `compositeGain` is not visible in the excerpt. fileciteturn0file0L170-L230

### Required action

Confirm that the current code still computes `compositeGain` from:

- smoothed source masks
- user source gains from `mappedSourceGainDb()`

If not present, add it explicitly.

### Intended logic

For each bin:

```cpp
compositeGain[k] = 0.0f;
for each source:
    const float sourceDb = mappedSourceGainDb(source);
    const float sourceLin = juce::Decibels::decibelsToGain(sourceDb);
    compositeGain[k] += smoothedMasks[source][k] * sourceLin;

compositeGain[k] = juce::jlimit(minCutGain, maxBoostGain, compositeGain[k]);
```

This is the correct place for user-controlled source rebalance.

---

## 2.11 Tighten low-end protection

### Goal

Prevent low-end chaos without over-protecting the entire low-frequency region.

### Current behaviour

The current code blends toward unity below the low-end protection boundary, which is a sensible safety measure. fileciteturn0file0L200-L220

### Required change

Refine the behaviour so that:

- bass and kick remain controllable below the protection region
- vocals, guitar, and other are strongly de-emphasized at very low frequencies
- drums retain some low-end action around kick regions

### Practical rule

Instead of globally protecting everything below the boundary, reduce source influence below the boundary by source type:

- vocals: very low influence below ~140 Hz
- guitar: very low influence below ~110 Hz
- other: very low influence below ~110 Hz
- drums: moderate influence in kick region
- bass: strong influence in bass region

That should produce tighter low-end behaviour without making the rebalance feel inert down low.

---

# 3. `VxRebalanceProcessor.h`

## Goal

Simplify the processor to reflect a DSP-only product.

The header is already relatively light, but the cpp still assumes multiple engines and model state. fileciteturn0file2L1-L30

## Required changes

Keep only processor state required for:

- DSP processing
- control smoothing
- latency-aligned neutral path
- sample rate and block-size tracking

If the timer is only there for ML/model state, remove timer inheritance entirely.

If the timer is still needed for UI refresh for another reason, document that clearly. Otherwise remove it.

---

# 4. `VxRebalanceProcessor.cpp`

## 4.1 Remove multi-engine product setup

### Current problem

The current processor still advertises engine choices for:

- DSP
- Demucs
- UMX4

and still builds downloadable model packages. That does not belong in the DSP-only version. fileciteturn0file3L1-L110

### Required changes

In `makeIdentity()`:

- remove the engine selector parameter and labels
- keep the recording type selector
- keep the six control-bank entries: vocals, drums, bass, guitar, other, strength

In `makeParameterLayout()`:

- remove the engine `AudioParameterChoice`
- keep only:
  - recording type
  - vocals
  - drums
  - bass
  - guitar
  - other
  - strength

---

## 4.2 Remove model package functions

Delete:

- `makeDemucsPackage()`
- `makeUmx4Package()`

These are no longer relevant. fileciteturn0file3L30-L75

---

## 4.3 Simplify status text

Replace the current status logic with a DSP-only status message such as:

```text
Linked-stereo source rebalance  -  DSP heuristic engine  -  Studio  -  latency 1024 samples
```

No model state, no fallback wording, no download messaging.

---

## 4.4 Remove model download UI methods

Delete all methods that only exist for downloadable model support:

- `supportsModelDownloadUi()`
- `isModelReadyForUi()`
- `isModelDownloadInProgress()`
- `getModelDownloadProgress()`
- `shouldPromptForModelDownload()`
- `getModelDownloadButtonText()`
- `getModelDownloadPromptTitle()`
- `getModelDownloadPromptBody()`
- `requestModelDownload()`
- `declineModelDownloadPrompt()`

These are not valid in the DSP-only product. fileciteturn0file3L110-L210

---

## 4.5 Remove model-runner flow from prepare/reset/timer

### In `prepareSuite()`

Keep:

- sample-rate tracking
- block-size tracking
- `dsp.prepare(...)`
- dry-delay setup
- latency reporting

Remove:

- model-asset service logic
- model-file lookup
- `modelRunner.prepare(...)`
- model readiness state

### In `resetSuite()`

Keep:

- `dsp.reset()`
- control reset
- dry delay reset

Remove:

- `modelRunner.reset()`

### In `timerCallback()`

If it only exists to hot-reload models or update model-ready state, delete it entirely.

If the timer remains, it must have a new DSP-only responsibility.

---

## 4.6 Simplify `processProduct()`

### Desired flow

The DSP-only version of `processProduct()` should do only this:

1. read source controls and strength
2. read voice context snapshot
3. read signal quality snapshot
4. read recording type
5. update DSP analysis context
6. smooth controls
7. if effectively neutral, run dry latency path
8. otherwise set DSP control targets and process

### Target structure

```cpp
std::array<float, Dsp::kControlCount> targets = { ... };

const auto voiceContext = getVoiceContextSnapshot();
const auto signalQuality = getSignalQualitySnapshot();
const int recordingType = ...;

dsp.setAnalysisContext(...);
dsp.setSignalQuality(signalQuality);
dsp.setRecordingType(...);

smooth controls;

if (effectivelyNeutral) {
    processNeutralWithLatency(buffer);
    return;
}

dsp.setControlTargets(smoothedControls);
dsp.process(buffer);
```

### Explicit removals

Delete all of the following:

- `isMlModeSelected()` logic
- `isDemucs6ModeSelected()` logic
- `isUmx4ModeSelected()` logic
- `modelRunner.analyseBlock(...)`
- `dsp.setMlMaskSnapshot(...)`
- any ML fallback branching

That path should no longer exist. fileciteturn0file3L220-L380

---

## Recommended implementation order

Do the work in this sequence.

### Phase 1: Processor cleanup

Convert the product fully to DSP-only first.

Reason:

- removes stale branches
- reduces test confusion
- gives a stable base for the DSP work

### Phase 2: Fix mode-profile field order

Reason:

- possible immediate quality improvement
- foundational correctness issue

### Phase 3: Harmonic grouping

Reason:

- biggest perceptual improvement
- gives coherent source objects

### Phase 4: Source persistence

Reason:

- biggest stability improvement

### Phase 5: Envelope-aware vocal/guitar support

Reason:

- hardest classification problem
- biggest gain in the main use case

### Phase 6: Residual and low-end refinement

Reason:

- polish and cleanup

---

## Testing plan

Use fixed repeatable material.

### Required test material

- studio vocal + guitar
- full mix with centered lead vocal
- live room recording with bright cymbals
- phone recording with voice + acoustic guitar
- bass-heavy production clip
- drum-forward loop

### Required control tests

For each clip test:

- vocals up
- vocals down hard
- guitar up
- guitar down
- drums down
- bass down
- all controls neutral
- strength at 25%, 50%, 100%

### Listen for

- vocal fizz
- guitar chirpiness
- drum transient smearing
- cymbal pumping
- low-end phasey feel
- center collapse
- source identity flicker
- ambiguity sinking into `other`

### Success criteria

The update is successful if:

- vocals sound more coherent when boosted
- vocal reduction damages guitar less, and guitar reduction damages voice less
- masks feel more stable in the midrange
- Live and Phone modes sound less messy from roughly 400 Hz to 2 kHz
- drums down does not hollow out bass body
- low-end remains controlled rather than phasey or over-protected

---

## What not to waste time on

Do not spend cycles on more of the same heuristic tuning:

- more fixed regions
- more scalar weights
- more ad hoc source multipliers
- more residual fallback tricks

The current system already has plenty of weighting logic. The next gains come from structure:

- harmonic coherence
- source persistence
- envelope-aware arbitration

---

## Expected end result

After these updates, VX Rebalance will still not be a true stem separator.

That is fine.

It should become a stronger and more honest product:

- a DSP-only linked-stereo source rebalance processor
- better vocal control
- materially better guitar behaviour
- more stable masks
- less residual mush
- cleaner product story

That is the right outcome for this architecture.

---

## Agent execution checklist

Use this as the implementation order for the agent.

### Cleanup

- [ ] Remove model packages from `VxRebalanceProcessor.cpp`
- [ ] Remove engine selector and ML-specific parameters
- [ ] Remove model download UI methods
- [ ] Remove model-runner prepare/reset/process logic
- [ ] Simplify status text to DSP-only
- [ ] Remove timer if no longer needed

### DSP correctness

- [ ] Fix `RebalanceModeProfile` initializer field order
- [ ] Confirm `compositeGain` is still built correctly
- [ ] Tighten low-end protection by source type

### Harmonic structure

- [ ] Add `SpectralPeak` struct
- [ ] Add `HarmonicCluster` struct
- [ ] Add peak detection state
- [ ] Add cluster state
- [ ] Implement `detectSpectralPeaks(...)`
- [ ] Implement `buildHarmonicClusters(...)`
- [ ] Implement `analyseClusterSources(...)`
- [ ] Implement `applyClusterInfluence(...)`

### Source identity stability

- [ ] Add previous-winning-source state
- [ ] Add confidence and persistence state
- [ ] Implement `applySourcePersistence(...)`
- [ ] Update reset logic for all new state

### Vocal vs guitar discrimination

- [ ] Add spectral-envelope buffers
- [ ] Implement `buildSpectralEnvelope(...)`
- [ ] Implement `vocalFormantSupport(...)`
- [ ] Implement `guitarTonalSupport(...)`
- [ ] Blend envelope-aware support into `semanticSupport`

### QA

- [ ] Build repeatable listening set
- [ ] Compare before/after on fixed clips
- [ ] Validate neutral path latency behaviour
- [ ] Validate no obvious extra zippering or instability
- [ ] Validate Studio / Live / Phone modes separately

---

## Notes for the implementing agent

Important constraints:

- Do not convert this into a true source reconstruction engine.
- Do not add heavy ML-like analysis or offline assumptions.
- Do not add expensive global optimizers.
- Keep all new logic bounded and realtime-safe.
- Bias and stabilize. Do not hard-assign.
- Prefer soft influence over brittle classification.
- Maintain the current user-facing behaviour: source rebalance, not stem export.

