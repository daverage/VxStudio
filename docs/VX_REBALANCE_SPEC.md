# VX Rebalance Spec

## Recommendation

Yes: VX Suite can support this idea, but not as a drop-in use of the current two-knob shell.

`VX Rebalance` is one of the rare products where more exposed controls are the product, not accidental complexity. The user promise is direct mixed-track source balancing across five broad classes:

- `Vocals`
- `Drums`
- `Bass`
- `Guitar`
- `Other`

V1 should stay honest:

- no ML inference
- no stem export
- no isolated-source claims
- no “true separation” marketing

Instead, it should be framed as:

- a realtime mixed-track rebalance processor
- driven by STFT-domain heuristic ownership masks
- designed to push broad source families forward or back perceptually
- with bounded, artifact-aware control rather than surgical extraction

This fits VX Suite if we extend the shared framework cleanly for a slider-bank product instead of forking a one-off editor.

---

## Product Identity

- Product name: `VX Rebalance`
- Short tag: `RBL`
- Problem solved in one sentence: Rebalance the perceived level of the main source families inside a finished stereo mix from one place.
- Primary outcome: Push lead elements forward or tuck them back without opening a DAW remix workflow.
- Secondary outcome: Provide fast “make the vocal/drums/bass/guitar sit differently” control on imported stems-free material.
- Default mode: no `Vocal` / `General` mode in v1

Why no mode switch:

- the product job is already specific
- the five sliders are the user-facing mode system
- adding `Vocal` / `General` would blur the contract without giving a clearer decision surface

---

## UX

### Shipping contract

- Main controls: five vertical source sliders
- Secondary control: one global `Strength` control
- Shared `Listen` toggle needed?: no in v1
- Hidden advanced controls to avoid in v1: mask threshold menus, alternate detector presets, per-source sharpness, attack/release menus, stem solo buttons, spectral display, per-band debug meters
- Mode status text: `Intelligent mixed-track source rebalance`

### Main controls

- `Vocals`: push likely lead-vocal energy forward or back
- `Drums`: push likely transient/percussive energy forward or back
- `Bass`: push low-end pitched foundation forward or back
- `Guitar`: push broad harmonic midrange guitar-like content forward or back
- `Other`: push the remaining residual material forward or back
- `Strength`: globally scales all five slider deltas away from unity

### UI framework implication

The current shared editor only has first-class support for up to four rotary controls plus selector widgets. `VX Rebalance` therefore requires a shared framework extension:

- add a generic multi-control product layout to `Source/vxsuite/framework/`
- support at least five named continuous controls in a slider-bank presentation
- keep the product on the shared editor shell rather than building a product-specific editor fork
- allow vertical-slider presentation as a first-class style, not a hacked rotary replacement

This should be implemented as reusable framework capability because future VX products may also need grouped control banks.

---

## DSP Contract

- Inputs: stereo mixed programme audio
- Outputs: stereo mixed programme audio with source-family rebalance applied
- Latency: fixed algorithmic latency from the STFT path; target `1024` samples in v1 if the 1024/256 design is kept
- Mono/stereo policy: linked stereo ownership analysis, identical gain-mask decisions applied to both channels, channel-local phase preserved
- Proven lab/reference contract: increasing a source slider should, on average, increase the energy of that source family more than the others on labelled multitrack evaluation material
- Streaming reintegration rule: analysis and correction happen fully inside a streaming STFT overlap-add engine; no offline lookahead or whole-file context
- Failure-safe behavior: on uncertain, silent, or unstable frames the masks relax toward a neutral all-unity rebalance state
- Analysis-only helpers allowed: spectral flux, continuity counters, simple harmonic/formant priors, onset activity, band-energy ratios
- Exposed audible helpers allowed: bounded mask-weighted source-family gain, low-end protection, transient protection, slider smoothing
- Hidden audible helpers forbidden: static EQ curves sold as source rebalance, broadband loudness tricks presented as source ownership, offline-trained embedded source models in v1
- State that must never reset during playback: overlap-add buffers, mask smoothers, previous-frame magnitudes, continuity counters, slider smoothers
- State that must reset on transport/silence: startup priming, transient hold state after long idle, denormal-sensitive histories if needed

---

## Core Effect Rules

### 1. Analysis path

V1 uses a product-local STFT engine under `Source/vxsuite/products/rebalance/`:

- `FFT size`: `1024`
- `Hop size`: `256`
- `Window`: Hann
- `Bins`: `513`

Per frame:

1. collect enough samples for one hop
2. window and transform
3. compute magnitude and preserve phase
4. compute five raw ownership weights per bin
5. normalize the weights into soft masks
6. smooth the masks over time
7. apply user-weighted composite gain in the magnitude domain
8. preserve original phase
9. inverse transform and overlap-add

The masks are not stem estimates. They are ownership priors that drive a bounded rebalance.

### 2. Ownership masks

For every bin, compute raw per-source weights and normalize them so the five weights sum to `1.0`.

#### `Bass`

Primary prior:

- strong below about `80 Hz`
- taper from `80 Hz` to `250 Hz`
- zero or near-zero above that range

Extra support:

- add a pitch-continuity or sustained-foundation bonus when low bins remain active across several frames
- this should be bounded and should not dominate kick-heavy transients automatically

#### `Drums`

Primary prior:

- driven by transient activity and high-frequency flux
- kick support in the low band during transient-rich frames
- broadband ownership boost in snare/hat/cymbal bands during transient-rich frames

Extra support:

- low steady-state ownership when the frame is not transient-led
- stronger weighting during clear percussive onsets

#### `Vocals`

Primary prior:

- strongest in roughly `300 Hz - 3.5 kHz`
- weighted around common formant and presence regions

Extra support:

- reuse lightweight framework speech/voice evidence where appropriate, but do not depend on `Voice` mode policy
- if the shared voice-context layer improves this detector safely, treat it as analysis-only support, not a change in product identity

#### `Guitar`

Primary prior:

- broad harmonic midrange residual in roughly `250 Hz - 5 kHz`
- moderate default claim rather than aggressive first-principles identification

Intent:

- this remains a pragmatic residual midrange ownership lane after bass, drums, and vocals have made stronger claims

#### `Other`

Primary prior:

- flat residual catch-all across the spectrum

Intent:

- absorbs content not confidently explained by the other four families

### 3. Temporal smoothing

Apply first-order IIR mask smoothing per source and per bin to reduce flicker.

Suggested starting value:

- `alpha ≈ 0.85`

This must be tuned by ear and by selectivity measurement. The kept version should be the one that balances musical stability against control responsiveness.

### 4. Gain law

Each slider maps around unity, not from silence to boost-only.

Recommended user contract:

- centered default = neutral
- down = attenuate that source family
- up = boost that source family modestly

Recommended internal mapping:

- normalized parameter range in APVTS remains `0.0 - 1.0`
- map `0.5` to unity
- map below `0.5` to attenuation
- map above `0.5` to bounded boost

Suggested v1 practical range:

- attenuation floor: full mute is allowed internally, but very deep cuts should be artifact-gated in testing
- boost ceiling: cap effective boost around `+6 dB`

Per bin:

- compute a weighted composite gain from the five smoothed masks and the five effective slider gains
- clamp to a bounded safe range before synthesis

### 5. Strength control

`Strength` scales the delta from unity across all five family sliders.

Contract:

- `Strength = 0%` means the processor is effectively neutral regardless of slider positions
- `Strength = 100%` means the current slider positions apply fully

This is the right sixth control for v1 because it lets users set a rebalance shape, then globally ease it in or out.

### 6. Guardrails

#### Low-end protection

Below roughly `120 Hz`, blend the processed magnitude back toward the input to reduce low-end instability and phasey bass collapse.

#### Transient protection

On strongly transient frames:

- reduce non-drum gain deviations
- preserve the attack contour better than a naive full-frame rebalance would

#### Parameter smoothing

All six controls must be smoothed before they reach the gain engine.

Use:

- `juce::SmoothedValue<float>`
- ramp target around `50 ms` unless measurement/listening proves a better value

---

## Parameter Model

V1 should not use raw linear `0.0 - 2.0` user-facing values. It should follow the suite’s normalized parameter discipline and user-readable display mapping.

### Parameters

- `vocals`
- `drums`
- `bass`
- `guitar`
- `other`
- `strength`

### Recommended normalized mapping

For the five source-family controls:

- normalized `0.0` = strongest cut
- normalized `0.5` = neutral
- normalized `1.0` = strongest boost

Display recommendation:

- show them as centered percentage deltas or musically simple labels such as `-100% ... 0 ... +100%`
- do not expose raw normalized values

For `strength`:

- normalized `0.0 - 1.0`
- displayed as `0 - 100%`

### Framework implication

The current `createSimpleParameterLayout(...)` helper is not sufficient for this product because:

- it only creates up to four continuous controls
- its float-display helpers assume percent-from-zero semantics or a special-case `Gain`

`VX Rebalance` therefore needs either:

1. a new shared multi-control layout helper, or
2. a product-specific parameter layout builder that still respects framework rules

The better long-term move is a shared multi-control parameter helper plus centered-value display formatting support.

---

## Realtime / framework shape

Recommended implementation shape:

- product folder: `Source/vxsuite/products/rebalance/`
- processor subclass: `vxsuite::ProcessorBase`
- product-local DSP modules:
  - `VxRebalanceProcessor.*`
  - `dsp/VxRebalanceDsp.*`
  - `dsp/VxRebalanceStft.*`
  - `dsp/VxRebalanceMaskEngine.*`
  - `dsp/VxRebalanceGuardrails.*`
- shared framework extensions:
  - multi-slider product metadata support
  - multi-control parameter layout support
  - shared editor-bank layout for vertical sliders

Suggested `ProductIdentity` direction:

- no `modeParamId`
- no `listenParamId`
- no `learnParamId`
- no aux selector in v1
- use a new control-bank declaration rather than overloading `primary` through `quaternary`

This is one of the few cases where extending the framework first is more elegant than trying to squeeze the product through the current four-knob contract.

---

## CPU / latency targets

- target comfortable realtime insert use on a modest laptop CPU
- target under roughly `15%` single-core usage at `44.1 kHz` / `512`-sample host buffer
- no heap allocation in `processBlock`
- deterministic latency reporting from the active STFT path

Sample-rate expectations:

- support at least `44.1 kHz` and `48 kHz` in v1
- higher rates are allowed only if CPU and selectivity remain acceptable

---

## Verification

### Core correctness

- bypass or neutral-setting transparency
- neutral slider state should be near-identical to dry apart from bounded STFT round-trip error
- automation continuity on all five source sliders plus `Strength`
- prepare/reset/sample-rate stability
- large host block safety
- silence recovery and startup priming stability
- correct fixed latency report

### Outcome-specific tests

- labelled multitrack evaluation:
  - raising `Vocals` should raise vocal-stem energy more than the others on average
  - raising `Drums` should raise drum-stem energy more than the others on average
  - raising `Bass` should raise bass-stem energy more than the others on average
  - lowering a family should reduce that family more than the others on average
- transient-preservation checks:
  - drum attacks should not collapse when `Vocals`, `Guitar`, or `Other` are reduced
- low-end stability checks:
  - bass-heavy material should not develop obvious low-end warble or stereo instability
- musical listening checks:
  - dense commercial mixes
  - sparse acoustic mixes
  - vocal-forward pop
  - drum-forward rock

### Useful metrics

- input RMS / output RMS / delta RMS
- per-stem proxy energy change on labelled material
- spectral-difference concentration by target family bands
- transient retention score for drum-heavy clips
- stereo correlation drift below `120 Hz`
- CPU and latency measurements

### Must-fail conditions

- a slider mostly changes broadband loudness instead of its intended family
- neutral settings sound obviously processed
- fast slider moves click or zipper
- latency is reported incorrectly
- low-end protection still permits unstable bass pumping or image collapse

---

## What is out of scope for V1

- ML inference of any kind
- ONNX / RTNeural / external runtime integration
- stem export or solo
- source-confidence meters
- analyzers or large visualizations
- offline separation mode
- sidechain input
- oversampling
- preset browser work beyond standard host state recall

---

## Implementation order

1. Extend the shared framework for a generic slider-bank product shape.
2. Add centered-value parameter display/mapping support for rebalance-style controls.
3. Build the product-local STFT and mask engine with all buffers preallocated.
4. Add the gain/guardrail path and validate neutral transparency first.
5. Add labelled-material selectivity tests before tuning by ear alone.
6. Tune only within bounded heuristic space; do not let v1 drift into pseudo-separation claims.

---

## Product honesty rules

`VX Rebalance` must stay honest in wording and implementation:

- it rebalances likely source families in a mix
- it does not isolate stems cleanly
- it is allowed to be musically useful before it is academically pure
- it is not allowed to fake usefulness with hidden broadband tone shaping or loudness inflation
