# VX Rebalance V2 Spec

## Recommendation

`VX Rebalance` v2 should pivot from heuristic-only source ownership to **ML-guided soft-mask rebalance**.

This is the right architectural step because v1 has already taught us the important boundary:

- pure heuristic STFT ownership can create movement
- it cannot reliably separate `Vocals`, `Drums`, `Bass`, `Guitar`, and `Other` on dense mastered stereo music
- users expect semantic control, not broad tonal shifts pretending to be source control

The correct v2 product is **not**:

- a live offline-quality stem separator
- a stem-export tool
- a multi-GB Demucs-style product inside the main insert path

The correct v2 product **is**:

- a **near-realtime** rebalance processor first
- using a compact ML model to estimate **soft source ownership masks**
- with the current DSP engine still responsible for:
  - bounded gain application
  - smoothing
  - low-end protection
  - transient protection
  - recording-type behaviour

So the product promise remains:

- “rebalance the main source families inside a mix”

not:

- “separate the song into perfect stems”

---

## Product Identity

- Product name: `VX Rebalance`
- Short tag: `RBL`
- Product class: shared-framework VX Suite product
- Product type: semantic mixed-track rebalance
- Main source lanes:
  - `Vocals`
  - `Drums`
  - `Bass`
  - `Guitar`
  - `Other`
- Global control:
  - `Strength`
- Recording-type selector:
  - `Studio`
  - `Live`
  - `Phone / Rough`

### One-sentence contract

Use lightweight ML-guided source ownership to push the perceived level of the main musical families inside a stereo mix without pretending to export true stems.

### Shipping-quality bias

V2 should optimize for **credible source behaviour first**, not minimum latency at any cost.

That means the first serious shipping target should bias toward:

- near-realtime / higher-latency quality

rather than:

- weakest-possible realtime quality just to claim “live ML”

### V2.0 scope decision

The first realtime shipping build should be **v2.0**:

- a lightweight **4-head** ML ownership model
  - `Vocals`
  - `Drums`
  - `Bass`
  - `Other`
- exported for a CPU-first runtime path
- with `Guitar` derived in DSP from:
  - model `Other`
  - harmonic midrange prior
  - negative space after stronger sources claim ownership

This keeps the runtime lighter than a true 5-head first ship while preserving the user-facing five-slider contract.

---

## V2 Product Rules

### What v2 must do

- Make the five source sliders behave more like semantic controls than broad EQ curves.
- Keep latency, CPU, and memory within VX Suite’s product expectations.
- Preserve the current suite framing: rebalance, not separation-for-export.
- Keep the plugin safe on poor recordings by backing off when the model or signal quality is uncertain.

### What v2 must not become

- A giant offline demixer shipped as an insert effect.
- A product that requires several-GB models.
- A UX that exposes ML complexity instead of a clean rebalance contract.
- A design that bypasses the framework and invents a product-specific processor/editor model.

---

## Core Architecture

```text
Stereo input
  -> STFT analysis
  -> lightweight ML ownership model
  -> 5 soft ownership masks:
       vocals / drums / bass / guitar / other
  -> mode-aware confidence shaping
  -> bounded source-family gain law
  -> low-end + transient guardrails
  -> ISTFT reconstruction
  -> stereo output
```

### Key principle

ML estimates **ownership**, not final audio.

The model should output soft masks or ownership priors. The audible rebalance still happens in DSP.

This keeps:

- the product explainable
- the audible guardrails local and tunable
- the ML model smaller than full demixing models where possible

### Important realism note

Five-source quality is constrained more by:

- training data
- labeling quality
- latency budget

than by architectural cleanliness alone.

---

## Training Data

Training data is the biggest implementation risk in v2.

The product wants these user-facing lanes:

- `Vocals`
- `Drums`
- `Bass`
- `Guitar`
- `Other`

The hard part is that many standard music-separation datasets do **not** provide `Guitar` as a clean independent target.

### Practical consequence

A credible 5-source model needs multitrack data where `Guitar` is labeled separately from:

- piano
- synths
- strings
- general accompaniment

Without that, the model will learn a smarter version of the same failure we already saw in heuristics:

- “midrange accompaniment” pretending to be guitar

### Required data strategy

Before committing to a 5-source shipping model, explicitly secure a training/evaluation set from one or more of:

- `MoisesDB`
- `Slakh2100`
- custom-licensed multitrack material
- any additional licensed or internal multitrack data with guitar isolated as its own semantic lane

### Shipping gate

If v2 does **not** have enough clean guitar-labeled data to prove that `Guitar` is meaningfully distinct from generic accompaniment, then the first ML shipping plan must be:

- a **4-strong-lane model**:
  - `Vocals`
  - `Drums`
  - `Bass`
  - `Other`
- plus **heuristic-shaped guitar behaviour layered on top of `Other`**

That is preferable to shipping a misleading `Guitar` slider that still mostly means “harmonic mids”.

---

## Source Model

### Required output heads

The v2 model should predict five soft ownership heads:

- `Vocals`
- `Drums`
- `Bass`
- `Guitar`
- `Other`

Each head should produce either:

- per-bin mask logits over the current STFT frame(s), or
- a compact prior that is expanded into a mask in the DSP layer

The preferred first version is direct per-bin soft-mask prediction because it aligns best with the current DSP engine.

### Practical fallback

Architecturally, the code path should support both:

- a 5-head model
- a 4-head model with derived guitar behaviour

This is important because the dataset risk may force the first trainable model to ship as `vocals / drums / bass / other`, even if the product UI still aims toward five controls later.

### Normalization

Per bin, the five source scores must normalize to sum to `1.0`.

This is important because `VX Rebalance` is fundamentally a **competition between sources**, not five independent EQ bands.

---

## Guitar Strategy

`Guitar` is the hardest source lane and must not be treated as “all leftover mids”.

The v2 design should treat `Guitar` as:

- **direct guitar evidence**
- plus **negative space** left after stronger lanes claim confident ownership

### Why

`Vocals`, `Drums`, and `Bass` are usually easier to identify.

`Guitar` overlaps heavily with:

- piano
- synths
- strings
- general accompaniment

So the model should not define guitar as:

```text
guitar = 1 - vocals - drums - bass
```

That would turn guitar into “everything harmonic in the midrange”.

### Correct v2 guitar rule

Conceptually:

```text
confident_core = vocals + drums + bass

guitar_mask =
    direct_guitar_evidence * A
  + residual_opportunity_after_confident_core * B
  + mode/context_prior * C
```

Where:

- `A` is the strongest term
- `B` helps guitar occupy plausible leftover space
- `C` is a smaller behavioural prior from `Studio / Live / Phone / Rough`

### `Other`

`Other` should then be:

- the unresolved residual after the other four lanes
- plus a small stabilizing floor

This keeps `Other` honest as uncertainty absorption, not a second “broad accompaniment” lane.

### Hard shipping rule

If the trained model cannot demonstrate materially better-than-residual `Guitar` behaviour on labelled evaluation data, then `Guitar` must be downgraded in the shipping plan to:

- heuristic-shaped residual behaviour

and documented honestly in the implementation plan.

---

## Recording-Type Modes

The model does **not** replace the existing recording-type concept.

`Studio`, `Live`, and `Phone / Rough` should remain in v2, but their role changes:

- they become behavioural priors and safety shaping around the model
- they do not need to be separate models in v2 unless validation proves that necessary

### Mode effects

#### `Studio`

- trust harmonic ownership more
- allow stronger moves
- trust stereo cues more
- use lighter fallback to `Other`

#### `Live`

- trust transient evidence more
- reduce certainty in harmonic lanes
- increase fallback behaviour
- protect against bleed and room smear

#### `Phone / Rough`

- reduce aggression
- trust stereo least
- trust damaged harmonic cues less
- lean more on `Other` and confidence gating
- preserve low end more aggressively

### Important rule

Poor recordings must make the product **less confident**, not more.

---

## ML Model Constraints

### Size target

The first shipping target should be a **small or medium model**, not a giant separation checkpoint.

Practical goal:

- prefer tens to low hundreds of MB, not several-GB checkpoints

This keeps:

- bundle size manageable
- load time manageable
- iteration realistic

### Realism note

Do not assume a high-quality 5-source music model will fit neatly under `50 MB`.

The spec should assume:

- meaningful 5-source quality may require a model larger than the lightest 4-stem systems
- model size is acceptable if it still fits VX product constraints and does not drift into giant offline-demixer territory

### Runtime target

The first model should be judged as suitable only if it can support:

- insert-style usage
- predictable latency
- bounded CPU use on a modest modern laptop

If the first acceptable-quality model cannot meet that, v2 should ship with an explicit higher-latency or render-quality mode rather than pretending it is lightweight.

### Inference runtime

V2 should commit to its inference runtime early because it affects:

- platform support
- CPU budget
- packaging
- threading
- future optimization work

### Runtime choice

Recommended default runtime for v2:

- **ONNX Runtime** as the main cross-platform inference backend

Why:

- it keeps the first implementation portable across macOS and Windows
- it has a clearer C++ integration story for a VX product than introducing a larger training/runtime stack into the plugin

Optional optimization path later:

- `CoreML` acceleration on Apple Silicon if profiling proves ONNX CPU performance is not good enough

But the core spec should assume:

- one main portable runtime first
- platform-specific acceleration second

### Audio-thread rule

Inference must not block the audio thread.

The runtime/integration design must therefore use one of:

- chunked synchronous inference with guaranteed bounded cost proven safe for the chosen latency mode
- or a carefully designed background inference path with buffered handoff

If that guarantee cannot be made cleanly, the model is not yet suitable for the shipping insert path.

### Inference cadence

Do not require inference on every sample.

Prefer:

- frame-wise or chunk-wise inference
- reuse across several hops where safe
- mask smoothing in DSP after inference

### Model class

Recommended direction:

- compact spectrogram-domain mask predictor
- non-causal or limited-lookahead chunk model for the first high-quality shipping target

Avoid as the first shipping direction:

- full Demucs-style waveform separator as the insert core
- large offline-quality 4/5-stem demix models adapted into the live path

---

## DSP Responsibilities In V2

Even with ML, the DSP still owns:

- STFT framing and reconstruction
- mask normalization
- temporal smoothing
- bounded gain law
- transient protection
- low-end unity blending / protection
- parameter smoothing
- mode-specific safety scaling
- confidence-based backoff

### Why keep this in DSP

Because the model should answer:

- “who owns this bin?”

The DSP should answer:

- “how much should we safely move it?”

That separation is cleaner, safer, and more VX-consistent.

---

## Confidence Model

V2 should use both:

- shared framework `SignalQuality`
- model-derived confidence

### Framework confidence

Keep using:

- `monoScore`
- `compressionScore`
- `tiltScore`
- `separationConfidence`

### Model confidence

Add ML-side evidence such as:

- entropy of source probabilities
- peak-vs-flat ownership margin
- stability of the predicted source over adjacent frames

### Combined confidence use

Combined confidence should:

- reduce aggressive moves when uncertain
- increase fallback to `Other`
- reduce `Guitar` claims when direct evidence is weak
- preserve neutral-ish output on bad material rather than damage it

---

## Gain Law

The product still uses centered source sliders plus `Strength`.

### Slider contract

- user-facing source sliders remain centered around `0 dB`
- current wide range can remain if testing supports it
- effective move should still be mode-bounded and confidence-bounded

### Gain application

Per bin:

```text
mask ownership
  -> confidence shaping
  -> weighted source gain sum
  -> bounded composite gain
  -> low-end + transient protection
```

The model should not directly output final gain.

---

## Latency Contract

The latency story should be explicit, not hedged.

### V2 default

The first serious ML shipping target should be:

- **High Precision / Near-Realtime**
- with an accepted higher latency budget than v1 heuristics

This is the right default because causal low-latency quality on dense music is a much harder problem than “good enough rebalance with some lookahead”.

### Practical target

The spec should assume a meaningful quality mode may need something on the order of:

- roughly `100–200 ms` effective analysis/inference lookahead

unless profiling and listening prove a lower-latency model still meets product quality.

### Realtime mode

A lower-latency mode may exist later, but it should be treated as:

- a secondary mode
- with explicitly lower quality expectations

not as the main v2 promise.

### UI rule

If a compute/latency mode is exposed later, it must remain simple and user-safe. Do not expose:

- model names
- checkpoint choices
- runtime backend decisions

The recording-type selector remains about input conditions, not compute plumbing.

---

## Framework Integration

V2 must remain a normal VX Suite product:

- shared `ProcessorBase`
- shared editor shell
- shared parameter conventions
- shared `SignalQuality`
- product-local DSP + model runner under `Source/vxsuite/products/rebalance/`

### Shared framework opportunities

If model runtime plumbing becomes reusable, it should be added to the framework only as:

- generic model loading / lifecycle
- generic inference scheduling helpers
- generic confidence snapshot patterns

Do **not** move rebalance-specific source policy into the framework.

---

## Suggested Code Structure

```text
Source/vxsuite/products/rebalance/
  VxRebalanceProcessor.h/.cpp
  dsp/
    VxRebalanceDsp.h/.cpp
    VxRebalanceMasks.h/.cpp
    VxRebalanceGuardrails.h/.cpp
  ml/
    VxRebalanceModel.h/.cpp
    VxRebalanceModelRunner.h/.cpp
    VxRebalanceFeatureBuffer.h/.cpp
    VxRebalanceConfidence.h/.cpp
```

### Responsibilities

- `ModelRunner`: model I/O, scheduling, inference
- `FeatureBuffer`: STFT features / chunk assembly for inference
- `Confidence`: convert raw model outputs into confidence signals
- `Dsp`: mask integration, gain law, guardrails, reconstruction

---

## Verification

V2 cannot be considered successful without labelled-stem verification.

### Required measurement stages

#### 1. Source selectivity harness

For each lane:

- boost one source
- measure energy/correlation change against labelled stems
- verify target stem rises more than the others

This must be done on:

- phone capture splits
- pro release splits
- at least one non-rock/acoustic test set if available
- and specifically on any guitar-labeled dataset used to justify the `Guitar` lane

#### 2. Realtime behaviour

Verify:

- CPU
- latency
- inference scheduling behaviour
- no audio-thread stalls from runtime/model execution
- no clicks on automation
- no denorm / no allocation in audio thread
- correct neutral pass-through

#### 3. Product listening

Judge:

- does `Vocals` feel like vocals?
- does `Bass` feel like bass rather than kick + low guitar?
- does `Drums` feel percussive rather than “bright plus punchy”?
- does `Guitar` feel guitar-like enough to justify the label?

### Hard truth gate

If `Guitar` still behaves like generic harmonic accompaniment after ML integration, the product contract or lane naming may need adjustment.

If the 5-head model cannot demonstrate credible guitar specificity because the data/runtime/latency tradeoff is not good enough, the first ML shipping build must fall back to:

- 4-head ML ownership
- plus heuristic guitar shaping

instead of shipping a dishonest “guitar” model.

---

## Out Of Scope For V2

- stem export
- solo/mute isolated stem audition as a core promise
- giant offline-quality demixing path as the default insert mode
- broad model zoo support
- model-selection UI for end users
- unrelated framework redesign

---

## Recommendation Summary

`VX Rebalance` v2 should be built as:

- a **5-head ML-guided soft-mask rebalance engine**
- with `Vocals`, `Drums`, and `Bass` treated as the strongest direct lanes
- `Guitar` treated as **direct evidence plus weighted residual opportunity**
- `Other` treated as a true residual absorber
- current DSP kept in charge of safety, smoothing, and user-facing behaviour

This is the most credible path to giving users the control they actually want without turning the product into a heavyweight stem-separation tool.
