# VxStudio

`VxStudio` is the standalone home for the VX Suite plugin line: a set of focused JUCE/VST3 audio effects built around a shared framework, minimal controls, and realtime-safe DSP.

Today the suite contains five effects:

- `VXDenoiser`
- `VXSubtract`
- `VXDeverb`
- `VXProximity`
- `VXPolish`

The goal of the project is not to ship giant channel strips. Each plugin is meant to solve one vocal or voice-adjacent problem quickly, with a small number of macro controls that map to deeper DSP internally.

The practical benefit is speed without losing discipline:

- faster decisions because each plugin has a clear job
- easier gain-staging and automation because controls stay compact
- more reliable realtime behavior because the suite is built around one framework contract
- better chain-building because each processor is meant to complement the others rather than duplicate them

## What This Repo Contains

- A shared VX Suite framework in `Source/vxsuite/framework/`
- Five product implementations in `Source/vxsuite/products/`
- CMake build targets for JUCE VST3 plugins
- A small set of focused tests, currently centered on `VXDeverb` and shared voice analysis

## Design Philosophy

VX Suite products follow a consistent contract:

- one main job per plugin
- one or two headline controls, with an optional third when it materially improves the result
- `Vocal` and `General` modes where the DSP genuinely benefits from different protection or aggressiveness
- optional `Listen` mode to audition what the plugin is removing
- no heap allocation or blocking work on the audio thread
- stable parameter IDs and deterministic latency behavior

The framework also provides shared voice-analysis evidence and a common editor/parameter model so products stay visually and behaviorally consistent.

## Recommended Order

VX Suite plugins are designed to work as focused building blocks. You do not need all of them on every track, but when a recording has multiple problems, the most effective order is usually:

1. `VXDenoiser` or `VXSubtract`
2. `VXDeverb`
3. `VXProximity`
4. `VXPolish`

Why this order works:

- Remove constant noise first so later processors do not enhance or react to it.
- Remove room tail before tone-shaping, otherwise proximity or polish moves can emphasize reverberant smear.
- Add closeness and tone after cleanup, once the signal is already more stable.
- Finish with `VXPolish`, which is the broad corrective/finalizing stage and works best on an already-cleaner source.

Two practical chain examples:

- Noisy voiceover in a reflective room:
  `VXDenoiser -> VXDeverb -> VXPolish`
- HVAC noise plus distant, thin vocal:
  `VXSubtract or VXDenoiser -> VXDeverb -> VXProximity -> VXPolish`

Rule of thumb:

- Choose `VXDenoiser` when the problem is general steady broadband noise.
- Choose `VXSubtract` when you can benefit from learning a representative noise print and want stronger profile-led removal.
- Use both only when necessary, and usually keep one of them moderate rather than pushing both hard.

## Plugins At A Glance

| Plugin | Main Job | Main Controls | Best Use |
| --- | --- | --- | --- |
| `VXDenoiser` | Broadband noise reduction | `Clean`, `Guard` | Constant room tone, hiss, HVAC, fan noise |
| `VXSubtract` | Learned/profile-guided subtractive denoise | `Subtract`, `Protect`, `Learn` | Repeating noise beds, hum, hiss, machine noise with a learnable profile |
| `VXDeverb` | Room-tail reduction | `Reduce`, `Blend` | Echoey or roomy spoken recordings |
| `VXProximity` | Close-mic tone shaping | `Closer`, `Air` | Making a source feel nearer, fuller, more present |
| `VXPolish` | Corrective finishing | `Polish`, `Body`, `Focus` | Smoothing mud, harshness, plosives, and edge in one pass |

## Shared Behavior Across The Suite

### `Vocal` vs `General`

Most products expose two modes:

- `Vocal` is the safer, speech-aware setting. It uses stronger source protection, more speech focus, and less aggressive cleanup.
- `General` allows broader or deeper processing across the full signal, with less protection bias.

That shared policy lives in `Source/vxsuite/framework/VxSuiteModePolicy.h`. In practice:

- `Vocal` protects formants, center image, and direct speech more strongly.
- `General` pushes harder on cleanup and full-range shaping.

### `Listen`

All five current products support `Listen`.

`Listen` outputs the removed material instead of the processed result. This is useful for checking whether you are removing mostly unwanted content or starting to carve into the source itself.

- For non-latency-sensitive paths, the framework uses `input - output`.
- For latency-bearing products like `VXDenoiser` and `VXDeverb`, the plugin aligns the dry reference first so the delta remains meaningful.

### Shared Voice Analysis

The framework computes block-rate signal evidence in `VxSuiteVoiceAnalysis` and exposes features such as:

- speech presence
- speech stability
- directness vs tail likelihood
- transient risk
- a composite voice-protection recommendation

Products like `VXPolish` use that evidence to steer multiple internal stages without exposing a large control surface.

## Effect Details

## `VXDenoiser`

`VXDenoiser` is the suite's broadband denoiser. It is designed for steady background noise rather than room reverb. Internally it is an STFT-based spectral denoiser with noise-floor tracking, Bark-band masking logic, smoothing, and stereo reconstruction that preserves side information.

### What It Does

At a high level, `VXDenoiser`:

- converts the signal into a short-time spectral representation
- estimates the noise floor over time
- calculates frequency-bin reduction based on the current signal, the tracked noise estimate, and a speech-aware protection policy
- smooths gains in time and frequency to avoid chattering and musical noise
- reconstructs the signal and reintroduces stereo side information in a controlled way

The current implementation also uses:

- OM-LSA style denoising behavior in `Vocal` mode messaging
- Bark/ERB-informed smoothing and masking
- a harmonic guard path to reduce damage to voiced content and transients
- latency-compensated listen output so the removed-noise audition is aligned

### Controls

`Clean`

- Main denoising amount.
- Higher values push the spectral reduction harder.
- Best thought of as "remove more background noise," not "make brighter" or "gate harder."

`Guard`

- Artifact protection control.
- Higher values preserve harmonics, transients, and source detail more aggressively.
- Useful when the noise reduction starts sounding phasey, lispy, watery, or overly stripped.

### Mode Behavior

`Vocal`

- biases the processor toward speech safety
- increases source protection and guard strictness
- is the better choice for voiceover, podcast, dialogue, and sung vocals

`General`

- behaves more like a broad spectral cleaner
- allows deeper cleanup across a wider range of material

### Best For

- air-conditioner noise
- computer fan noise
- preamp hiss
- steady room noise underneath spoken recordings

### Not Primarily For

- long room tails and slap echoes
- severe mouth clicks
- tonal EQ shaping

That work is better handled by `VXDeverb` or `VXPolish`.

## `VXSubtract`

`VXSubtract` is the suite's profile-guided subtractive denoiser. It is designed for cases where a learned noise print is useful, but the result still needs to be smarter and safer than a blunt static subtract mode.

Where a basic subtractive denoiser can easily tear into speech, `VXSubtract` adds protection heuristics and confidence-aware behavior so it can push harder on learned noise while still trying to preserve wanted audio.

### What It Does

At a high level, `VXSubtract`:

- captures a representative noise profile with `Learn`
- measures progress and confidence during capture
- freezes a learned spectral estimate when capture completes
- subtracts that profile with speech, tonal, and transient protection layered on top
- lets `Protect` trade raw removal strength for source preservation

The intended benefit is simple:

- stronger removal than a purely blind denoiser when the noise really is consistent
- less voice damage than a naive profile subtractor
- a more guided workflow than “draw a curve and hope”

### Controls

`Subtract`

- Main subtractive amount.
- Higher values remove more of the learned noise profile and adaptive background estimate.
- At maximum, the processor is allowed to drive well-matched steady noise much closer to zero than the gentler suite denoiser paths.

`Protect`

- Speech/detail protection control.
- Higher values preserve consonants, harmonics, and transient detail more aggressively.
- Use this when the cleanup is correct in principle but starts to sound hollow, lispy, chirpy, or over-scooped.

`Learn`

- Arms guided profile capture.
- Play the representative noise, let the meter build confidence, then stop playback.
- The processor locks the profile once it has heard enough valid material and the source drops quiet.

### Best For

- air-conditioner beds with a stable spectral fingerprint
- fan noise with a representative section available
- repeating machine noise
- hum or hiss that can be learned before the wanted speech starts

### Best Practice

- Learn the cleanest noise-only section you can.
- Use the shortest representative capture that still gives good confidence.
- Push `Subtract` harder only after the learned profile is trustworthy.
- If the source starts sounding “dug out,” increase `Protect` before backing off everything.

### Not Primarily For

- changing room size or echo
- broad tonal sweetening
- one-pass vocal finishing

That work belongs to `VXDeverb`, `VXProximity`, and `VXPolish`.

## `VXDeverb`

`VXDeverb` removes room tail and late reverberation. It is the suite's dedicated dereverberation processor, not just a tonal cleaner with a de-emphasis curve.

The core DSP is a latency-bearing spectral processor built around late-reverberant spectral variance (LRSV) suppression. In `Vocal` mode it can also apply a WPE-style stage to further reduce reverberant smear in a speech-oriented way.

### What It Does

At a high level, `VXDeverb`:

- estimates room decay behavior using a shared RT60 tracker
- performs STFT-domain late-tail suppression using delayed spectral history
- scales over-subtraction with the `Reduce` amount
- optionally applies a voice-oriented WPE stage inside the spectral path
- reconstructs the processed signal with host-reported latency
- can restore some low-end weight after dereverberation using `Blend`

This is important because de-reverb often makes speech clearer while also making it feel thinner. `VXDeverb` explicitly separates those two jobs:

- `Reduce` handles reverberation removal
- `Blend` restores some low-frequency body after the fact

### Controls

`Reduce`

- Wet authority for the dereverb path.
- At lower values, the plugin mixes between latency-aligned dry and fully processed wet output.
- It also scales the processor's over-subtraction so low settings stay gentler and high settings push harder.

`Blend`

- Low-body restoration after dereverberation.
- Implemented as a low-frequency reinjection toward the aligned dry signal rather than a broad dry/wet cheat.
- Useful when the room cleanup makes a voice feel lean or over-dried.

### Mode Behavior

`Vocal`

- uses the safer voice-oriented behavior
- preserves direct speech more aggressively
- includes optional WPE-style cleanup in the spectral path
- aims to keep intelligibility and natural tone intact while reducing room wash

`General`

- allows deeper tail reduction across the full range
- is less speech-protective
- fits general sources and more aggressive cleanup goals

### Listen Behavior

`Listen` outputs the removed room contribution, using a latency-aligned dry reference. That makes it useful for hearing how much tail and wash are being taken out without dry/wet timing errors.

### Best For

- untreated room recordings
- distant narration or dialogue
- podcast tracks captured in reflective spaces
- speech with obvious late-room smear

### Not Primarily For

- constant broadband hiss
- close-mic tonal enhancement

That is the domain of `VXDenoiser` and `VXProximity`.

## `VXProximity`

`VXProximity` is a tone-shaping effect that simulates the feel of moving a microphone closer to the source. It is deliberately simple and lightweight compared to the other processors.

Internally it is implemented with shelf filters rather than convolution or physical modeling. That makes it cheap, stable, and predictable for insert use.

### What It Does

`VXProximity` shapes two broad perceptual cues of close-mic recording:

- more low-end body and warmth
- more upper presence or air

The DSP uses:

- a low shelf whose frequency and gain change with `Closer`
- a high shelf whose gain changes with `Air`
- different shelf tuning for `Vocal` and `General`

In the current implementation:

- `Closer` increases low-shelf gain and shifts the shelf region upward as the effect intensifies
- `Air` adds a high shelf above a mode-dependent turnover frequency

### Controls

`Closer`

- Main proximity-effect control.
- Adds bass body and warmth, making the source feel physically nearer to the mic.
- In `Vocal` mode it is tuned for a speech-style low shelf.

`Air`

- Adds upper clarity and openness.
- Useful to keep the result from becoming too thick or overly chesty as `Closer` increases.

### Mode Behavior

`Vocal`

- low shelf tuned roughly in the 80 to 200 Hz region
- high shelf centered for presence/clarity rather than extreme top end
- best for narration, singing, and spoken content

`General`

- low shelf shifts higher and behaves more broadly
- high shelf reaches further into full-range material

### Best For

- making a voice feel more intimate
- restoring some closeness after a distant recording
- sweetening a source before or after corrective cleanup

### Not Primarily For

- removing noise
- removing reverb
- dynamic corrective finishing

## `VXPolish`

`VXPolish` is the suite's finishing processor. It is the most composite effect in the repo: a single macro-style front end that drives several corrective and recovery stages behind the scenes.

Rather than doing one type of processing extremely deeply, `VXPolish` smooths multiple common voice problems in one pass:

- mud and boxiness
- harshness and fizz
- excessive sibilance
- plosive bursts
- light dynamic roughness

It then restores useful body and applies output control so the result feels finished rather than merely reduced.

### What It Does

`VXPolish` performs block-rate tonal analysis plus shared voice-analysis reading, then steers a DSP chain that currently includes:

- low-mid mud detection and reduction
- de-essing
- plosive taming
- gentle compression
- multi-band "trouble" smoothing with several peaking cuts
- body recovery
- limiter/output control

The processor measures tonal balance in several regions, including:

- low
- low-mid
- presence
- air

It combines that with speech-confidence and artifact-risk signals from the shared framework, then maps the user's three controls into a richer parameter set for the internal DSP chain.

### Controls

`Polish`

- Main corrective amount.
- Drives the overall cleanup intensity.
- As it rises, the processor can increase mud reduction, de-essing, trouble smoothing, compression, and denoise-related cleanup inside the finishing chain.

`Body`

- Restores useful weight after corrective smoothing.
- Especially helpful when cleanup removes too much fullness from a voice.
- Works as a recovery stage, not just a fixed low boost.

`Focus`

- Steers where the correction aims.
- Lower settings lean more toward low-mid cleanup and warmth management.
- Higher settings lean more toward upper-mid and top-end smoothing, including harshness and fizz control.

### Mode Behavior

`Vocal`

- keeps the chain more speech-safe
- uses restrained lift and stronger source preservation
- is the intended mode for spoken voice and most vocals

`General`

- broadens the behavior for wider-band material
- allows a more full-range corrective feel

### Listen Behavior

`Listen` outputs the removed trouble rather than the polished result. This is useful for checking whether `VXPolish` is removing mud, spit, fizz, plosive energy, and roughness, or whether it has started to eat too much of the wanted signal.

### Best For

- final cleanup on podcast or voiceover tracks
- reducing small problems quickly without building a long manual chain
- smoothing overly edgy or muddy vocal recordings

### Not Primarily For

- heavy de-noising of constant background noise
- true dereverberation
- explicit character EQ design

## Choosing The Right First Plugin

If you are not sure where to start, use this decision rule:

- Constant noise underneath the whole recording: start with `VXDenoiser`
- Repeating learnable noise print: start with `VXSubtract`
- Room wash, echo, or distant reverberant speech: start with `VXDeverb`
- Recording is clean enough but feels far away or thin: start with `VXProximity`
- Recording mainly needs finishing and smoothing: start with `VXPolish`

In other words:

- `VXDenoiser` is the broad “make the background quieter” tool.
- `VXSubtract` is the more surgical learned-noise remover.
- `VXDeverb` is the room-cleanup tool.
- `VXProximity` is the closeness/tone enhancer.
- `VXPolish` is the final corrective finisher.

## Build

The project uses CMake and JUCE.

Build the whole suite:

```bash
cmake -S . -B build
cmake --build build --target VXSuite_VST3 -j4
```

Built VST3 bundles are staged into:

```text
Source/vxsuite/vst/
```

### Main Targets

- `VXSuite_VST3` builds and stages all current plugins
- `VXDeverbTests` builds the focused deverb test binary
- `VXDeverbMeasure` builds the deverb measurement harness
- `VxSuiteVoiceAnalysisTests` builds tests for shared voice-analysis behavior

## Repository Layout

```text
Source/
  vxsuite/
    framework/          Shared processor/editor/parameter/mode/analysis code
    products/
      deverb/           VXDeverb processor + DSP
      denoiser/         VXDenoiser processor + DSP
      subtract/         VXSubtract processor
      polish/           VXPolish processor + DSP
      proximity/        VXProximity processor + DSP
  dsp/                  Shared subtractive DSP support currently used by VXSubtract
tests/                  Focused measurement and behavior tests
cmake/                  Project-specific CMake helpers
docs/                   Framework and product guidance
```

## Current Notes

- The suite currently targets `VST3` builds in this repo.
- Testing is stronger for `VXDeverb` than for the other products today.
- The codebase is already organized to encourage reusable framework behavior rather than one-off plugin architectures.
- `VXSubtract` is now part of the suite aggregate build and stages into the same `Source/vxsuite/vst/` location as the other VX Suite plugins.

## Key Source Files

- `Source/vxsuite/framework/VxSuiteProcessorBase.*` - shared processor lifecycle, mode, and listen behavior
- `Source/vxsuite/framework/VxSuiteModePolicy.h` - shared `Vocal` / `General` tuning contract
- `Source/vxsuite/framework/VxSuiteVoiceAnalysis.*` - block-rate speech/voice evidence
- `Source/vxsuite/products/denoiser/` - denoising product
- `Source/vxsuite/products/subtract/` - learned subtractive denoise product
- `Source/vxsuite/products/deverb/` - dereverb product
- `Source/vxsuite/products/proximity/` - proximity shaper
- `Source/vxsuite/products/polish/` - finishing/corrective processor

## Status

This repository is the active standalone implementation home for the VX Suite line. The current code reflects a shared framework and five distinct effect concepts:

- noise reduction
- profile-guided subtractive denoise
- dereverberation
- proximity/tone shaping
- finishing correction

The strongest validation is still centered on the de-reverb path today, but the suite is structured so all five products build, stage, and present a consistent VX Suite contract.
