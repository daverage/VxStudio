# VX Leveler Spec

## Recommendation

Yes: the VX Suite framework can support this idea, but only if we frame it honestly as a **behaviour-aware mixed-track speech recovery balancer** rather than a source separator.

This should be a lightweight realtime dynamics product that:

- gently levels overall program energy
- lifts sustained speech-band content when intelligibility is falling behind
- tames bright/transient instrument moments when they momentarily dominate
- contains output safely without trying to reconstruct isolated stems

That fits the current framework well:

- one focused problem
- two-knob outcome-led UI
- shared block-rate analysis and smoothing
- product-local DSP core
- strict realtime-safe operation

It does **not** fit as:

- source separation
- hard switching between “voice mode” and “instrument mode”
- a heavy multiband mastering tool with many exposed controls

---

## Product Identity

- Product name: `Leveler`
- Short tag: `LVL`
- Problem solved in one sentence: Keep speech understandable on a single mixed performance track where the instrument is usually louder than the talker.
- Primary outcome: Recover consistent speech intelligibility from a hot instrument-to-camera recording.
- Secondary outcome: Better containment of bright instrument bite and transient jumps.
- Default mode: no mode switch in v1

Why no mode switch:

- the job is already narrow and specific
- adding `Vocal` / `General` would blur the contract
- the adaptive detector should already react to the current balance of sustained speech-like vs transient-bright material

---

## UX

- Knob 1: `Clarity`
- Knob 2: `Control`
- Knob 3: none in v1
- Shared `Listen` toggle needed?: no
- Hidden advanced controls to avoid in v1: detector thresholds, band crossover frequencies, ratio controls, attack/release menus, manual makeup gain
- Mode status text: `Adaptive speech recovery for instrument-to-camera performance`

Knob meanings:

- `Clarity`: increases sustained speech-band lift and the bias toward recovering intelligible midrange when speech is buried
- `Control`: increases instrument-spike containment, brightness restraint, and final containment firmness

Why no `Listen` in v1:

- this processor both attenuates and lifts material
- a delta monitor would not map cleanly to a user-trustworthy “removed content” story
- framework support exists, but the product contract is clearer without it

---

## DSP Contract

- Inputs: mono or stereo full-range mixed performance track
- Outputs: same channel count, dynamics-shaped for intelligibility and containment
- Latency: zero in v1
- Mono/stereo policy: linked stereo detection with identical gain application across channels
- Proven lab/reference contract: recover intelligibility-weighted speech-band audibility when speech is masked by a louder instrument, while reducing high-band/transient overs during instrument-dominant moments
- Streaming reintegration rule: fully in-place processor, no offline lookahead reconstruction
- Failure-safe behavior: detector smoothing collapses toward neutral and all stage gains return toward unity on uncertain or silent material
- Analysis-only helpers allowed: RMS trend, spectral centroid, speech-band energy ratio, high-band energy ratio, transient density, short-term variance
- Exposed audible helpers allowed: slow leveller, speech-band upward lift, high-band transient containment, final safety limiter
- Hidden audible helpers forbidden: static tonal re-voicing, broad EQ ducking sold as “intelligence”, source-isolation claims
- State that must never reset during playback: envelope followers, detector smoothing, hysteresis state, crossover filter memory, dynamics gain memory
- State that must reset on transport/silence: denormal-sensitive detector tails after a suitable idle timeout, safety limiter state, startup priming flags

---

## Core Effect Rules

### 1. Analysis

Per block, compute:

- short-term RMS
- RMS variance over a short trailing window
- spectral centroid
- speech-band energy ratio in roughly `150 Hz - 4 kHz`
- high-band energy ratio in roughly `2 kHz+`
- transient density / transient strength

Reuse and extend the framework direction already present in [`VxSuiteVoiceAnalysis.h`](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/framework/VxSuiteVoiceAnalysis.h).

The detector should produce two soft scores:

- `speechDominance`
- `instrumentDominance`

Heuristic intent:

- speech-like material: steadier RMS, stronger speech-band proportion, lower centroid drift, lower transient density
- instrument-dominant moments: brighter centroid, stronger short transients, higher block-to-block variance, stronger upper-band spikes

Compute a smoothed balance:

`dominance = smooth(speechDominance - instrumentDominance)`

Use hysteresis so the bias does not flap on every strum or consonant.

### 2. Stage A: slow transparent leveller

Broadband downward leveller with a low ratio and slow timing.

Target v1 behaviour:

- attack around `30-80 ms`
- release around `300-900 ms`
- ratio around `1.4:1 - 2.0:1`

Role:

- reduce large program swings from the hotter instrument capture
- prepare the signal for the adaptive stages
- avoid pumping on normal speech movement

### 3. Stage B: speech-focused upward lift

Speech-band dynamics stage operating on a broad mid band, mixed back conservatively.

Suggested v1 band:

- approx. `150 Hz - 4 kHz`

Role:

- increase presence of sustained speech information when it is being masked
- act more when `dominance` favors speech or when speech-band energy is present but underrepresented
- back off when the detector says the block is mostly instrument-driven

Important:

- this is not a broadband parallel smash
- upward action should be bounded and speech-presence gated so noise or pick attack is not promoted

### 4. Stage C: high-band transient containment

Fast compressor/dynamic tamer focused on the bright attack region.

Suggested v1 band:

- approx. `2 kHz+`

Target behaviour:

- attack around `1-8 ms`
- release around `90-180 ms`
- moderate ratio, increasing with `Control`

Role:

- catch pick attack and other bright instrument spikes
- preserve sustain better than a broadband fast compressor would
- relax when the detector leans speech-dominant

### 5. Stage D: final containment

Gentle limiter or suite safety containment only catching overs.

Target:

- ceiling around `-1 dBFS`
- minimal normal activity

---

## Parameter Mapping

### `Clarity`

Maps to:

- stronger speech-band upward lift authority
- slightly stronger speech-presence protection in detector weighting
- modest increase in leveller makeup target

Must not map to:

- bright static EQ boost
- general loudness inflation

### `Control`

Maps to:

- stronger high-band transient containment
- slightly firmer leveller action
- slightly tighter final containment

Must not map to:

- obvious broadband squashing
- strong top-end dulling during sustained instrument passages

---

## Realtime / framework shape

Recommended implementation shape:

- product-local DSP under `Source/vxsuite/products/leveler/`
- processor subclass from `vxsuite::ProcessorBase`
- two normalized parameters only
- shared editor shell from `EditorBase`
- block-rate smoothing with `VxSuiteBlockSmoothing`
- no heap allocation in `processBlock`
- preallocated detector history buffers in `prepareSuite`

Suggested internal modules:

- `VxLevelerProcessor.*`
- `dsp/VxLevelerDsp.*`
- `dsp/VxLevelerDetector.*`

This product is a better fit for reusable framework helpers later:

- generic band-energy / centroid helpers
- generic adaptive dominance smoothing helper

But v1 should ship as a product-local implementation first so the framework does not get polluted with speculative abstractions.

---

## Verification

### Core correctness

- bypass transparency
- zero-setting near-unity behavior
- automation continuity for both knobs
- prepare/reset/sample-rate stability
- large host block safety
- silence to speech recovery

### Outcome-specific tests

- on speech+instrument synthetic mixes where the instrument is materially louder, `Clarity` should improve speech-band audibility during masked speech segments without large broadband gain inflation
- on transient-heavy instrument passages, `Control` should reduce high-band peak overs more than low-mid sustain
- on speech-dominant passages, high-band containment should relax relative to instrument-dominant passages
- on instrument-only material, speech-focused upward lift must stay modest and presence-gated
- on speech-only material, the processor must not read like a bright de-esser inverse or obvious tonal effect

### Useful metrics

- input/output RMS
- speech-band RMS delta
- high-band peak delta
- crest-factor change
- blockwise gain activity per stage
- limiter engagement percentage

---

## Honest product wording

Good wording:

- `Keeps spoken performance clear on one mixed track`
- `Lifts the words, reins in the spikes`
- `Adaptive levelling for voice + guitar performances`

Bad wording:

- `Separates guitar and voice`
- `AI source extraction`
- `Perfectly rebalances any mix`

---

## Bottom line

This is viable as a VX Suite plugin if we keep the product narrow:

- no source-separation promise
- no sprawling control surface
- adaptive behaviour, not hard classification
- simple zero-latency dynamics architecture with soft dominance steering

The elegant v1 is essentially:

1. slow leveller
2. speech-band upward lift
3. high-band transient tamer
4. smoothed behaviour detector that biases stages instead of switching them

That should get most of the benefit described in the concept while staying consistent with the VX framework and the suite's product philosophy.
