# VX Width

## Commercial Product and DSP Implementation Specification

**Document status:** Implementation specification
**Product family:** VX Studio
**Working product name:** VX Width
**Primary formats:** VST3, AU, standalone test host
**Framework:** JUCE
**Processing model:** Real-time, non-AI, stereo output
**Primary use cases:** Vocals, instruments, dialogue, buses and full mixes
**Product principle:** Maximum spatial control with four musical controls and no requirement for users to understand M/S processing, phase correlation, delay, detuning or decorrelation.

---

# 1. Product definition

VX Width is a stereo image and doubling processor with four controls:

1. **Width**
2. **Double**
3. **Tightness**
4. **Focus**

It must support four distinct tasks:

* Narrow an existing stereo signal.
* Collapse stereo cleanly to mono.
* Increase the width of existing stereo material.
* Convert mono or near-mono material into a believable stereo double.

The plugin must not behave like a basic Side-gain control, static Haas delay, hidden chorus or simple micro-pitch effect.

The processor must use different internal algorithms for:

* narrowing
* moderate stereo expansion
* synthetic spatial widening
* ADT-style doubling

The user should experience these as one coherent tool.

---

# 2. Product goals

## 2.1 Primary goals

The plugin must:

* produce useful results quickly
* remain understandable without documentation
* preserve the original centre signal
* maintain strong mono compatibility
* protect transients and low-frequency focus
* avoid obvious periodic chorus movement
* adapt to mono, near-mono and stereo material
* avoid large changes in perceived loudness
* remain stable under automation
* support real-time tracking and mixing
* produce commercially competitive output on exposed vocals

## 2.2 Secondary goals

The plugin should:

* work well on guitars, synths, backing vocals and dialogue
* provide more sophisticated width than simple M/S gain
* provide more natural doubling than static detune
* avoid requiring source-specific modes
* produce predictable output across headphones and speakers
* share reusable components with future VX spatial processors

## 2.3 Non-goals

Version one must not attempt:

* surround, Atmos or binaural output
* automatic source separation
* neural performance generation
* reverb-based widening
* manual crossover controls
* manual delay or cents controls
* detailed multiband editing
* independent left and right channel controls
* phase correction of poorly recorded stereo material
* restoration of missing stereo information

---

# 3. User experience

## 3.1 Main interface

The interface contains four primary dials:

```text
WIDTH     DOUBLE     TIGHTNESS     FOCUS
```

A small visual display may show the current image, but it must not become a technical analyser that competes with the controls.

Recommended secondary interface elements:

* Input stereo-state indicator
* Output width visualisation
* Mono compatibility indicator
* Input/output bypass
* Preset menu
* Undo/redo
* Output level trim only if testing proves it necessary

Do not expose:

* correlation thresholds
* delay times
* detune values
* crossover frequencies
* decorrelator type
* modulation rate
* transient sensitivity

## 3.2 Control hierarchy

The four controls must remain independent in intent:

* **Width** controls the size of the stereo image.
* **Double** controls how much generated performance is introduced.
* **Tightness** controls how close or loose the generated performance feels.
* **Focus** controls where in the frequency spectrum the spatial effect is concentrated.

The controls may influence multiple DSP parameters internally, but moving one control must not appear to unpredictably change the purpose of another.

VX Suite framework note: the shipping UI ceiling is four headline knobs (`docs/VX_SUITE_FRAMEWORK.md`), and each of the four here maps to a distinct, non-overlapping user decision (space, performance, relationship, spectral placement), so no control should be cut or merged to fit a smaller default. There is no single hero control; `ProductIdentity`, `ModePolicy`, and the shared `Listen` toggle should still be wired per the framework template even though this product does not need `Vocal`/`General` modes or removed-content audition.

---

# 4. Control specifications

# 4.1 Width

## User-facing range

```text
-100 to +100
```

Display landmarks:

* `-100`: Mono
* `0`: Original
* `+100`: Maximum managed width

Default:

```text
0
```

## Behaviour

The Width control is divided internally into processing regions.

### Region A: -100 to 0

Reduce the existing Side component.

At `-100`:

```text
SideOut = 0
```

The output must be true dual mono, derived from the Mid signal.

This region must not introduce artificial width, modulation or delay.

### Region B: 0 to approximately +35

Apply conservative expansion of the existing stereo image.

Use:

* bounded Side gain
* frequency-dependent weighting
* mono-risk monitoring
* loudness compensation

This region should remain highly transparent.

### Region C: approximately +35 to +100

Do not continue increasing original Side gain without limit.

Blend in additional decorrelated spatial information using:

* optimised decorrelation filters
* adaptive spectral weighting
* transient-aware reduction
* source-relative mono protection

The transition between regions must be continuous and inaudible.

## Mapping

Use a nonlinear mapping.

The majority of the control travel should cover practical settings:

* `0 to +50`: subtle to clearly wider
* `+50 to +80`: large but usable
* `+80 to +100`: intentionally exaggerated but managed

Do not allow maximum settings to become unstable merely to sound dramatic.

---

# 4.2 Double

## User-facing range

```text
0 to 100
```

Default:

```text
0
```

## Behaviour

Double introduces synthetic performance variation.

At zero:

* no ADT voice contribution
* no additional latency beyond the base processor
* no hidden widening from the doubling engine

As Double increases, jointly increase:

* generated voice level
* left/right timing separation
* pitch differentiation
* spectral differentiation
* level variation
* stochastic movement
* DoubleMid thickness
* DoubleSide width

The Double control must not simply function as a wet/dry control.

At low values:

* barely perceptible thickening
* stable centre
* minimal timing displacement
* no obvious effect tail

At medium values:

* believable double-take impression
* clearly increased stereo dimension
* original remains dominant

At high values:

* obvious ADT or studio-doubling effect
* wider timing and pitch variation
* still free from periodic chorus behaviour
* transients remain controlled

## Mono behaviour

The original signal must remain dominant in mono.

Generated Side content should disappear or reduce benignly when summed.

A limited DoubleMid component may remain, but it must not create:

* hollowing
* flanging
* major spectral notches
* excessive gain buildup

---

# 4.3 Tightness

## User-facing range

```text
0 to 100
```

Display landmarks:

* `0`: Tight
* `50`: Natural
* `100`: Loose

Default:

```text
40
```

## Behaviour

Tightness controls the relationship between the original and generated performances.

It must jointly alter:

* mean delay
* delay range
* modulation depth
* modulation speed
* pitch deviation
* pitch-drift range
* pitch-drift speed
* gain variation
* spectral differentiation
* transient protection
* DoubleMid contribution

### Tight settings

Use:

* shorter average delay
* smaller delay variance
* minimal pitch drift
* reduced random movement
* stronger transient locking
* lower DoubleMid level
* more modern and precise sound

### Natural settings

Use:

* modest performance offsets
* slow irregular timing drift
* subtle pitch movement
* mild spectral asymmetry
* moderate transient protection

### Loose settings

Use:

* longer average delays
* greater timing variance
* larger pitch differences
* more obvious non-periodic movement
* increased spectral differentiation
* weaker but still active transient locking
* increased perception of separate performances

Loose settings must not produce unbounded delay changes or obvious pitch dives.

## Important constraint

Tightness must not change the overall effect level significantly.

A user should be able to move between Tight and Loose without needing to readjust Double because of a major gain change.

---

# 4.4 Focus

## User-facing range

```text
0 to 100
```

Display landmarks:

* `0`: Body
* `50`: Full
* `100`: Air

Default:

```text
55
```

## Behaviour

Focus controls the spectral region receiving the strongest generated width and doubling.

It must not be implemented as a simple fixed crossover.

The Focus control sets the target spectral weighting, while source analysis adapts the actual processing.

### Body

Emphasise:

* low-mid body
* lower harmonics
* fundamental-adjacent energy

Still protect:

* sub-bass
* unstable low-frequency stereo information
* strong bass fundamentals

Useful for:

* thin vocals
* guitars
* synths
* backing vocals

### Full

Distribute width across the useful spectrum while preserving low-frequency stability.

This should be the most generally applicable setting.

### Air

Emphasise:

* upper harmonics
* breath
* ambience
* high-frequency detail
* stereo spaciousness

Reduce spatial alteration of:

* fundamentals
* low-mid vocal body
* bass energy
* kick and low percussion

Useful for:

* lead vocals
* dialogue
* full mixes
* preserving centre solidity

## Adaptive behaviour

Focus should interact with source analysis.

For example:

* A low male vocal must not receive strong stereo decorrelation at its fundamental merely because Focus is set towards Body.
* A bright acoustic guitar must not become harsh because Focus is set to Air.
* Full-mix material must retain low-frequency coherence at all settings.

---

# 5. Core signal architecture

# 5.1 Processing overview

```text
Input
  |
  +-- Input format and state analysis
  |
  +-- Mid/Side encoding
  |
  +-- Existing stereo width path
  |
  +-- Synthetic spatial decorrelation path
  |
  +-- ADT double-generation path
  |
  +-- Adaptive spectral weighting
  |
  +-- Transient protection
  |
  +-- Mono-risk management
  |
  +-- Mid/Side reconstruction
  |
  +-- Energy and loudness compensation
  |
  +-- Output safety stage
```

The three spatial paths must remain conceptually separate:

1. Original Mid/Side path
2. Generic decorrelated width path
3. ADT performance path

Do not use one algorithm for all three purposes.

---

# 5.2 Mid/Side transform

Use an orthonormal transform:

```text
Mid  = (Left + Right) / sqrt(2)
Side = (Left - Right) / sqrt(2)
```

Decode using:

```text
Left  = (Mid + Side) / sqrt(2)
Right = (Mid - Side) / sqrt(2)
```

This simplifies energy accounting and avoids inconsistent gain assumptions.

All signal paths must be tested for exact or near-exact reconstruction at neutral settings.

At:

```text
Width = 0
Double = 0
```

The output must null against the input within floating-point tolerance, after delay-compensating for the processor's reported latency (any lookahead or oversampling stage). Excluding safety-stage rounding only; latency itself must not be excluded by skipping alignment.

---

# 6. Input analysis

The analysis system must operate continuously and smoothly.

It must derive:

* mono/stereo confidence
* Side-to-Mid energy ratio
* broadband correlation
* frequency-dependent coherence
* mono downmix spectral difference
* short-term loudness
* transient likelihood
* tonal versus noise-like energy
* spectral centroid
* low-frequency energy concentration
* approximate fundamental region where useful
* source stability over time

Do not expose source classification to the user.

## 6.1 Mono confidence

Calculate mono confidence from:

* Side-to-Mid ratio
* left/right sample similarity
* band coherence
* channel level difference
* sustained measurement over time

Avoid binary switching.

Produce a smoothed value:

```text
monoConfidence = 0.0 to 1.0
```

Where:

* `1.0` indicates effectively mono
* `0.0` indicates clearly stereo

Use this value to crossfade between existing-stereo and generated-stereo behaviours.

## 6.2 Near-mono handling

Near-mono material is common and must not trigger unstable mode changes.

Examples:

* mono vocal with stereo reverb
* centred guitar with stereo room
* mono source recorded through two similar channels
* stereo file containing nearly identical channels

Use continuous weighting, not discrete mode changes.

---

# 7. Existing stereo width engine

# 7.1 Narrowing

For negative Width values:

```text
SideNarrowed = SideInput * sideScale
```

Where:

```text
sideScale = 1.0 at Width 0
sideScale = 0.0 at Width -100
```

Use a perceptually smooth curve rather than a raw linear mapping.

Do not alter Mid unless needed for energy compensation.

## 7.2 Moderate expansion

For low positive Width:

```text
SideExpanded = SideInput * boundedGain
```

The Side gain must be:

* frequency weighted
* input-state aware
* restrained by mono-risk analysis
* smoothed
* level compensated

Recommended maximum direct Side gain:

```text
approximately +3 dB to +6 dB
```

The exact limit should vary with input material.

Do not allow unrestricted Side gain at maximum Width.

## 7.3 High-width decorrelation

Above the moderate expansion region, introduce a synthetic spatial layer.

Recommended approach:

* optimised velvet-noise decorrelator
* optionally combined with a short ERB-distributed all-pass network
* independent but complementary decorrelator instances for left and right
* offline-selected coefficient sets
* low latency
* no time-varying coefficient discontinuities

The decorrelator must:

* reduce interchannel coherence
* avoid strong tonal colouration
* avoid metallic ringing
* avoid static comb-filter signatures
* remain stable under automation
* produce bounded group delay
* support sample rates from 44.1 kHz to 192 kHz

## 7.4 Decorrelated layer construction

Do not simply decorrelate the full left and right input independently.

Prefer deriving decorrelated material from:

* Mid
* selected Side components
* noise-like residual
* sustained harmonic content

The exact blend should depend on mono confidence and signal analysis.

A possible architecture:

```text
decorSource =
    midContribution
  + selectedSideContribution
  + residualContribution
```

Then generate a decorrelated Side layer:

```text
decorSide = decorrelatorA(decorSource) - decorrelatorB(decorSource)
```

Normalise the result before blending.

---

# 8. ADT doubling engine

# 8.1 Design objective

The doubling engine must simulate the perceptual cues of a second performance without attempting literal source regeneration.

It should create differences in:

* timing
* pitch
* level
* spectral envelope
* transient articulation
* noise and breath content

It must avoid sounding like:

* a fixed delay
* a conventional chorus
* a symmetric detuner
* a flanger
* a stereo reverb
* a static phase rotator

---

# 8.2 Voice architecture

Create two generated voices:

```text
Voice A
Voice B
```

Each voice should contain:

* fractional delay
* slow stochastic delay movement
* micro-pitch bias
* slow stochastic pitch drift
* independent gain movement
* independent spectral tilt
* subtle decorrelation
* transient-dependent processing reduction

Then derive:

```text
DoubleMid  = 0.5 * (VoiceA + VoiceB)
DoubleSide = 0.5 * (VoiceA - VoiceB)
```

Output contribution:

```text
MidOut  += doubleMidAmount  * DoubleMid
SideOut += doubleSideAmount * DoubleSide
```

The original Mid must remain the dominant signal.

---

# 8.3 Fractional delay system

Use high-quality fractional delay lines.

Acceptable interpolation approaches:

* cubic interpolation
* high-order Lagrange interpolation
* windowed-sinc interpolation
* Thiran all-pass fractional delay where appropriate

The chosen implementation must be tested for:

* frequency response
* modulation noise
* aliasing
* zipper noise
* CPU cost
* transient behaviour

Recommended initial delay ranges:

### Tight

```text
approximately 5 to 15 ms
```

### Natural

```text
approximately 10 to 28 ms
```

### Loose

```text
approximately 18 to 45 ms
```

These are starting ranges, not fixed user-facing values.

Avoid delay differences that create obvious discrete echoes.

---

# 8.4 Delay movement

Do not use a simple sine-wave LFO as the default modulation source.

Use band-limited stochastic trajectories such as:

* smoothed random walk
* interpolated random targets
* filtered noise
* multi-timescale random modulation

Each voice must use an independent random stream.

Movement must:

* remain continuous
* have bounded slope
* avoid repeated cycles
* avoid abrupt direction changes
* avoid long-term drift outside the permitted range

For larger target changes, use crossfaded delay taps rather than forcing one delay line to jump.

---

# 8.5 Pitch variation

Pitch movement may be produced by:

* the Doppler effect of delay modulation
* a dedicated micro-pitch stage
* a hybrid of both

The version-one baseline should use fractional-delay modulation as the primary method.

A supplementary granular or spectral micro-pitch stage may be added if listening tests show that delay modulation cannot provide sufficient independent control.

Recommended effective pitch deviation:

### Tight

```text
approximately ±1 to ±3 cents
```

### Natural

```text
approximately ±2 to ±7 cents
```

### Loose

```text
approximately ±4 to ±12 cents
```

Avoid permanently symmetric offsets such as:

```text
Voice A = +7 cents
Voice B = -7 cents
```

Static bias may be used as one component, but movement must be asymmetric and non-periodic.

---

# 8.6 Spectral differentiation

Each generated voice should receive subtle independent spectral changes.

Possible mechanisms:

* broad low/high tilt
* gentle formant-region emphasis changes
* high-frequency damping differences
* low-order dynamic filtering
* small independent saturation differences
* harmonic/residual-specific weighting

Typical changes should remain subtle:

```text
approximately 0.2 to 1.0 dB across broad regions
```

Avoid narrow resonant EQ changes.

Spectral differences must move slowly and should not create obvious timbral wobble.

---

# 8.7 Gain variation

Apply slow independent gain variation to each voice.

Recommended range:

```text
approximately ±0.2 to ±1.0 dB
```

Map the range to Tightness and Double.

Gain variation must be:

* smooth
* non-periodic
* independent between voices
* reduced during strong transients

---

# 8.8 Harmonic, residual and transient decomposition

For the advanced commercial implementation, introduce a lightweight three-component analysis:

1. Harmonic or tonal component
2. Residual or noise-like component
3. Transient component

This does not require source separation or machine learning.

Possible methods:

* harmonic-percussive spectral masking
* sinusoidal confidence estimates
* spectral flatness
* onset detection
* short-time phase stability
* transient extraction using prediction error

Processing strategy:

### Harmonic component

* subtle delay and pitch movement
* limited decorrelation
* protect fundamentals
* maintain stable centre pitch

### Residual component

* stronger decorrelation
* greater left/right spectral differentiation
* more width
* less need for pitch shifting

### Transient component

* preserve timing
* reduce delay spread
* reduce pitch drift
* reduce Side generation
* retain centre localisation

This stage is a major differentiator and should be prioritised after the base ADT engine is stable.

---

# 9. Transient protection

# 9.1 Detection

Use a combination of:

* broadband onset strength
* high-frequency energy rise
* spectral flux
* short-term crest factor
* prediction error
* optional multiband onset detection

Generate a continuous transient-confidence value.

## 9.2 Processing response

During strong transients:

* reduce generated Side
* reduce delay separation
* reduce modulation depth
* reduce DoubleMid
* reduce decorrelator contribution
* increase original Mid dominance

Do not bypass the entire effect abruptly.

Use attack and release smoothing.

Suggested response:

* fast attack
* medium release
* band-dependent behaviour

Consonants and picking transients should remain precise without causing audible pumping of the stereo image.

---

# 10. Spectral focus system

# 10.1 Frequency-domain implementation

The Focus system may use:

* minimum-phase crossovers
* linear-phase crossovers only if latency mode permits
* spectral weighting curves
* ERB-spaced analysis bands
* multiband IIR processing
* hybrid STFT analysis with time-domain rendering

Avoid exposing discrete bands to the user.

## 10.2 Weighting model

Create a smooth target width curve:

```text
widthWeight(f, Focus, sourceAnalysis)
```

Inputs include:

* Focus
* spectral centroid
* fundamental confidence
* low-frequency energy
* coherence by band
* transient content
* mono confidence
* source tonality

## 10.3 Low-frequency protection

There must be no universal hard-coded mono-below-X rule.

Instead:

* strongly restrain generated Side in sub-bass
* protect identified fundamentals
* reduce low-band widening when coherence is poor
* permit useful low-mid width when safe
* use smooth frequency transitions

For full mixes, low-frequency protection must be more conservative than for isolated instruments.

---

# 11. Mono and phase-risk management

# 11.1 Principles

The plugin must not claim to correct phase.

It must manage mono and phase risk by:

* preserving original Mid
* limiting unstable additional Side
* comparing processed and unprocessed downmixes
* restraining low-frequency decorrelation
* protecting transients
* avoiding fixed Haas-style delays as the main widening method

## 11.2 Required measurements

Continuously estimate:

* broadband correlation
* coherence by perceptual band
* downmix RMS difference
* downmix loudness difference
* downmix spectral deviation
* low-frequency cancellation
* centre-image displacement
* short-term negative-correlation duration

## 11.3 Input-relative guardrail

Do not use a single rule such as:

```text
correlation must remain above zero
```

Instead compare processed output against the unprocessed input.

The safety system should determine:

```text
How much additional damage is being introduced by this processing?
```

If requested processing causes excessive degradation:

* reduce new decorrelated width first
* reduce generated DoubleSide second
* reduce direct Side expansion third
* preserve original Mid at all times

## 11.4 Safety blending

Safety correction must be smooth.

Use:

* band-dependent gain reduction
* attack and release smoothing
* hysteresis
* no binary mode switching
* no abrupt stereo image collapse

---

# 12. Loudness and energy management

Spatial processing can alter perceived loudness.

Implement internal compensation based on:

* Mid/Side energy
* short-term loudness
* generated layer energy
* mono/stereo perception differences

Do not force strict sample-level or RMS equality.

The goal is:

* comparable perceived level
* no obvious gain jump when Width changes
* no major buildup when Double increases
* no output overs caused by reconstruction

Provide at least:

```text
6 dB internal headroom
```

Prefer more in floating-point processing.

Use a final safety gain stage, not a limiter.

The plugin must not add audible compression unless explicitly designed into the algorithm.

---

# 13. Latency strategy

## 13.1 Commercial requirement

The plugin must support low enough latency for real-time monitoring.

Target modes:

### Live mode

* minimal lookahead
* time-domain processing
* reduced harmonic/residual sophistication if necessary
* target reported latency: 0 to 64 samples where possible

### Quality mode

* optional short lookahead
* more accurate transient and spectral analysis
* higher-quality decomposition
* target reported latency: below approximately 10 ms at 48 kHz

The user-facing product may initially expose only one automatic mode if both paths can be switched based on host state.

If a mode selector is introduced, it must not become a fifth main control.

## 13.2 Latency reporting

All latency must be reported correctly to the host.

Changes that affect latency must:

* trigger correct host notification
* avoid audio discontinuities
* remain stable during playback

---

# 14. Sample-rate and block-size support

Required sample rates:

* 44.1 kHz
* 48 kHz
* 88.2 kHz
* 96 kHz
* 176.4 kHz
* 192 kHz

Required block sizes:

* 16 samples
* 32 samples
* 64 samples
* 128 samples
* 256 samples
* 512 samples
* 1024 samples
* 2048 samples

The processor must:

* support variable block sizes
* avoid sample-rate-dependent modulation speed
* scale delay values correctly
* recalculate filters safely
* avoid allocations in the audio thread
* avoid locks in the audio thread
* handle offline rendering correctly

---

# 15. Channel configurations

Required:

* mono input to stereo output
* stereo input to stereo output

Optional:

* mono input to mono output should bypass stereo generation and return a compatible mono result
* stereo input to mono output should use the managed Mid signal

The plugin must not silently discard channels.

For mono-to-stereo operation:

* instantiate a true stereo output bus
* ensure the DAW recognises the output configuration
* test hosts that provide dual-mono buffers
* test hosts that do not automatically expand mono tracks to stereo

---

# 16. Parameter smoothing and automation

All four controls must support sample-accurate or block-ramped automation.

No control movement may cause:

* clicks
* zipper noise
* abrupt pitch jumps
* delay discontinuities
* stereo image flips
* sudden safety-system engagement

Use separate smoothing times appropriate to each destination.

Examples:

* gain parameters: short smoothing
* delay targets: longer bounded transitions
* filter weights: medium smoothing
* stochastic ranges: gradual remapping
* mode crossfades: long enough to avoid artefacts

Automation must be tested at audio-rate-like densities.

---

# 17. Randomness and repeatability

The doubling engine uses stochastic modulation.

Requirements:

* no shared global random generator
* deterministic per-instance seeds where appropriate
* independent streams for each voice and process
* reset-safe behaviour
* no identical modulation across duplicated plugin instances
* no denormal-related stalls
* no repeated short cycles

Offline renders should be repeatable by default unless the host or design explicitly requests new variation.

Recommended seed inputs:

* persistent instance identifier
* saved random seed
* voice index
* process index

The saved plugin state must preserve the seed.

---

# 18. Oversampling and alias management

Oversampling is not automatically required.

Assess aliasing from:

* saturation stages
* spectral pitch shifting
* nonlinear interpolation
* modulation
* any nonlinear safety processing

If nonlinear saturation is included:

* keep it subtle
* oversample only that stage if needed
* do not add unnecessary plugin-wide latency

Fractional delay interpolation must be tested for high-frequency artefacts at all sample rates.

---

# 19. CPU and memory targets

Commercial targets at 48 kHz, stereo, 128-sample blocks:

### Baseline mode

* average CPU below approximately 1% of one modern performance core per instance
* no audio-thread allocation
* bounded fixed memory
* no background thread dependency

### Advanced quality mode

* average CPU below approximately 2–3% of one modern performance core per instance
* peaks must remain controlled
* no large FFT setup work during playback

These values are engineering targets and should be tested on:

* Apple Silicon baseline machine
* recent Intel Mac
* mainstream Windows laptop
* high-core-count workstation

Optimise only after perceptual quality is validated.

---

# 20. Preset strategy

Presets must demonstrate use cases, not expose technical parameters.

Recommended factory presets:

* Mono Maker
* Slightly Narrower
* Natural Width
* Wide but Safe
* Vocal Double
* Tight Vocal Double
* Loose ADT
* Backing Vocal Spread
* Acoustic Guitar Width
* Electric Guitar Double
* Synth Expansion
* Mix Air
* Dialogue Space
* Extreme Width

Presets should move only the four controls and any approved non-user-facing mode state.

Avoid dozens of near-duplicate presets.

---

# 21. Visual feedback

The visualiser should communicate:

* original width
* processed width
* centre stability
* mono risk

It should not require knowledge of vectorscopes.

Recommended design:

* central vertical core representing Mid
* soft lateral field representing Side
* generated double shown as subtle moving outer energy
* warning state only when the safety system is actively restraining the requested effect

Do not display raw correlation as the main visual.

Optional labels:

* Mono
* Original
* Wide
* Protected

Avoid red warning behaviour during harmless brief correlation events.

---

# 22. State management

Save and restore:

* Width
* Double
* Tightness
* Focus
* quality or latency mode
* random seed
* internal version number
* future migration data

Preset and session recall must produce identical audible behaviour.

Implement state versioning from the first release.

Do not serialize transient analysis state.

---

# 23. Bypass behaviour

Bypass must:

* preserve host latency behaviour
* avoid clicks
* crossfade where necessary
* stop adding generated layers
* avoid resetting stochastic state unnecessarily
* resume without modulation discontinuity

Support both:

* plugin internal bypass
* host bypass where available

---

# 24. Failure and edge-case handling

The processor must remain stable with:

* silence
* denormals
* DC offset
* single-sample impulses
* clipped input
* out-of-phase stereo
* one silent channel
* left-only audio
* right-only audio
* dual mono
* negative correlation
* very low-frequency sine waves
* high-frequency sine waves
* white noise
* pink noise
* rapidly changing block size
* sample-rate changes
* transport start/stop
* loop boundaries
* offline bounce
* parameter automation during silence
* malformed state restoration

Silence must remain silence.

No generated noise may appear when input is silent unless an explicitly modelled tape-noise feature is added later.

---

# 25. Commercial quality requirements

The plugin must not ship until it passes:

* mono compatibility tests
* phase and polarity tests
* automation stress tests
* DAW compatibility tests
* long-duration stability tests
* null tests at neutral settings
* sample-rate consistency tests
* listening tests on exposed vocals
* listening tests on full mixes
* headphone and speaker tests
* CPU spike tests
* preset recall tests
* offline render repeatability tests

Supported host test matrix should include at minimum:

* REAPER
* Logic Pro
* Ableton Live
* Cubase
* Studio One
* Pro Tools if AAX is later supported
* at least one Windows VST3 host
* at least one macOS AU host

---

# 26. Objective test suite

## 26.1 Neutral null test

Settings:

```text
Width = 0
Double = 0
```

Expected:

* output nulls against input to floating-point tolerance once delay-compensated for reported latency
* reported latency matches the declared baseline for the active mode
* no spectral difference

## 26.2 Mono collapse test

Settings:

```text
Width = -100
Double = 0
```

Expected:

* left and right outputs identical
* output equals managed Mid
* no channel imbalance
* no delay between channels

## 26.3 Mono-input centre preservation

Input:

* mono vocal
* mono sine sweeps
* mono music

Settings:

* Double and Width varied

Expected:

* mono downmix retains the original as the dominant component
* no severe comb filtering
* no large spectral nulls
* level change remains bounded

## 26.4 Correlation stress test

Input:

* synthetic stereo signals with known phase relationships

Expected:

* safety system restrains additional risk
* no unstable gain pumping
* no abrupt collapse
* pre-existing negative correlation is not automatically erased

## 26.5 Transient test

Input:

* clicks
* drums
* consonant-heavy speech
* picked guitar

Expected:

* no obvious flam at normal settings
* no excessive transient smearing
* width reduction around onsets is smooth

## 26.6 Modulation periodicity test

Analyse generated voice trajectories.

Expected:

* no obvious short repeated cycle
* independent left/right behaviour
* bounded delay and pitch movement
* no discontinuities

## 26.7 Spectral-colouration test

Input:

* sine sweep
* pink noise
* multitone signal

Expected:

* neutral settings remain neutral
* decorrelation does not produce severe static notches
* Focus changes broad spatial weighting without narrow resonances

---

# 27. Listening test programme

Use blind and level-matched testing.

Reference comparison should include representative products from:

* conventional M/S imagers
* micro-pitch processors
* ADT processors
* chorus-based doublers
* modern decorrelation-based wideners

Evaluate:

* naturalness
* apparent width
* vocal intelligibility
* centre focus
* mono compatibility
* transient clarity
* tonal colouration
* headphone stability
* speaker translation
* fatigue over long listening
* usefulness of all four controls

The goal is not to copy a competitor.

The goal is to demonstrate that VX Width provides:

* faster control
* more reliable mono behaviour
* more natural doubling
* fewer unusable settings
* less technical decision-making

---

# 28. Build order

VX Width is built to its full target architecture (§30) in one pass, not shipped as a sequence of narrower interim products. There are no partial-feature "releases" between here and §32 Acceptance criteria — the list below is dependency ordering for implementation work, not a stage gate with its own exit bar or its own shippable state.

Rationale: every later subsystem (ADT, decorrelation, content-aware weighting, mono-risk management) reads from or writes into the same M/S core and the same parameter/state contract. Building a narrower v1 and layering the rest on top means rebuilding those seams twice — once for the interim shape, once for the real one. Build the seams for the end state from the start.

Implementation order (each step depends on the ones above it; build straight through):

1. Plugin shell, bus configuration, `ProductIdentity`/`ModePolicy` wiring, orthonormal M/S codec, state management, parameter smoothing, test harness.
2. Input analysis (§6): mono confidence, coherence, loudness, transient likelihood, spectral centroid — every downstream subsystem consumes this, so it is not deferred.
3. Existing-stereo width engine (§7): narrowing, bounded expansion, decorrelated high-Width layer.
4. ADT doubling engine (§8), including harmonic/residual/transient decomposition (§8.8) — not deferred to a later pass, since Voice A/B and DoubleMid/DoubleSide are structural, not additive.
5. Transient protection (§9), spectral focus mapping (§10), mono/phase-risk management (§11), loudness compensation (§12).
6. Latency modes, sample-rate/block-size coverage, channel configuration handling (§13-15).
7. Full UI, visual feedback, preset system, debug telemetry (§21, §20, §31).
8. Commercial hardening: host validation matrix, performance optimisation, installer/signing, crash reporting, documentation (§25).

Validate continuously against §26 (objective tests) as each subsystem lands, rather than batching validation to the end of a stage. The only hard gate is §32 Acceptance criteria before release.

---

# 29. Agent implementation rules

The implementation agent must:

1. Preserve the four-control product model.
2. Keep all technical parameters internal.
3. Build each DSP subsystem behind a clean interface.
4. Provide unit tests for every mathematical transform.
5. Provide offline audio test utilities.
6. Avoid premature UI work before the core algorithms pass listening tests.
7. Maintain an A/B path for comparing algorithm revisions.
8. Avoid adding third-party dependencies unless licensing is compatible with commercial distribution.
9. Document all third-party code and licence obligations.
10. Avoid GPL dependencies in the distributable plugin unless the product licensing strategy explicitly permits them.
11. Use deterministic state and random-seed handling.
12. Avoid memory allocation and locks in the audio thread.
13. Make all processing sample-rate independent.
14. Add debug measurements that can be compiled out of production builds.
15. Keep legacy or experimental engines behind build flags.
16. Never hide a major audible compromise behind a safety label.
17. Prefer natural sound over impressive measurement scores.
18. Treat mono preservation as a design objective, not an absolute claim.
19. Test every high-level control across its full range.
20. Stop and redesign any subsystem that only sounds acceptable within a narrow parameter region.

---

# 30. Suggested class architecture

```text
VxWidthProcessor
  InputAnalyser
  StereoStateEstimator
  MidSideCodec
  ExistingWidthProcessor
  SpatialDecorrelator
  AdtDoubleProcessor
    FractionalDelayVoice A
    FractionalDelayVoice B
    StochasticTrajectoryGenerator
    MicroPitchProcessor
    SpectralVariationProcessor
  ComponentSeparator
  TransientProtector
  SpectralFocusMapper
  MonoRiskManager
  LoudnessCompensator
  OutputSafetyStage
  ParameterMapper
  StateManager
  DebugSnapshot
```

Each subsystem must be independently testable.

Recommended shared interface pattern:

```cpp
prepare(const ProcessSpec&);
reset();
process(AudioBlock<float>&);
setParameters(const SmoothedParameters&);
getLatencySamples() const;
getDebugSnapshot() const;
```

Avoid requiring analysis subsystems to allocate or resize during processing.

---

# 31. Debug and development telemetry

Development builds should expose:

* input mono confidence
* input Side/Mid ratio
* output Side/Mid ratio
* broadband correlation
* minimum band coherence
* generated DoubleMid level
* generated DoubleSide level
* decorrelator contribution
* transient-protection gain
* mono-risk reduction amount
* low-frequency protection amount
* current voice delays
* effective pitch offsets
* CPU time per subsystem
* peak output level

This information should be available through:

* debugger snapshot
* optional CSV logging
* hidden development panel
* automated test output

It must not appear in the commercial interface.

---

# 32. Acceptance criteria

The product is ready for commercial release only when:

* neutral settings are audibly and mathematically transparent
* mono collapse is predictable and stable
* mono sources can be widened without obvious comb filtering
* the Double control produces a credible double-take effect
* Tightness changes performance relationship rather than merely modulation rate
* Focus changes spectral placement without obvious EQ colouration
* maximum settings remain intentional rather than broken
* no main control has a large unusable section
* centre content remains stable at normal settings
* transients remain clear
* low-frequency content remains controlled
* automation is artefact-free
* CPU performance supports typical multi-instance sessions
* state recall is deterministic
* the plugin behaves consistently across major hosts
* blind listeners prefer or match VX against relevant commercial alternatives in its intended use cases

---

# 33. Final product principle

The internal architecture may be complex, but the user model must remain simple:

```text
Width changes the space.

Double creates another performance.

Tightness controls how closely it follows.

Focus decides where the width lives.
```

The plugin should not expose DSP engineering.

It should make the technically safe and musically useful choice automatically, while leaving enough range for deliberate creative effects.
