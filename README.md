# VX Suite

> **Focused, realtime-safe audio processors for voice and vocal production.**
> Five plugins. One shared framework. One job each.

VX Suite is a collection of JUCE/VST3 audio effects built around a shared C++ framework, minimal control surfaces, and performance-first DSP. Each plugin solves one problem cleanly rather than trying to be a general-purpose channel strip.

---

## Plugins at a Glance

| Plugin | Job | Controls | Best For |
|---|---|---|---|
| **VXDenoiser** | Broadband noise reduction | `Clean` · `Guard` | Hiss, fan noise, room tone, HVAC |
| **VXSubtract** | Profile-guided subtractive denoise | `Subtract` · `Protect` · `Learn` | Learnable noise beds, hum, machine noise |
| **VXDeverb** | Room tail and reverb removal | `Reduce` · `Blend` | Echoey rooms, distant speech, reverberant dialogue |
| **VXProximity** | Close-mic tone shaping | `Closer` · `Air` | Intimacy, warmth, fullness after cleanup |
| **VXPolish** | Corrective finishing | `Polish` · `Body` · `Focus` | Mud, harshness, sibilance, plosives, dynamic roughness |

---

## Recommended Signal Chain

VX Suite is designed around composability. Each plugin complements the others without duplicating them. When a recording has multiple problems, this order works best:

```
VXDenoiser / VXSubtract  →  VXDeverb  →  VXProximity  →  VXPolish
```

**Why this order:**

1. **Remove noise first** — later processors should not react to or enhance a dirty noise floor.
2. **Remove room tail before tone-shaping** — proximity or polish moves can emphasize reverberant smear if applied to an untreated room recording.
3. **Add closeness and tone after cleanup** — shaping works better on a stable, already-cleaner source.
4. **Finish with VXPolish** — broad corrective and recovery work is most effective as the final pass.

### Practical Examples

```
Noisy voiceover, reflective room:
  VXDenoiser → VXDeverb → VXPolish

HVAC noise, thin and distant vocal:
  VXSubtract → VXDeverb → VXProximity → VXPolish
```

### Denoiser Choice

| Situation | Recommended |
|---|---|
| General steady broadband noise | **VXDenoiser** |
| Noise with a learnable spectral fingerprint | **VXSubtract** |
| Both present | Both at moderate settings — avoid pushing both hard simultaneously |

---

## Design Philosophy

Every VX Suite plugin follows the same contract:

- **One main job.** Each processor is designed around a single outcome. Scope boundaries are intentional.
- **Minimal controls.** One or two headline controls, with an optional third only when it materially improves the result.
- **Vocal and General modes.** Where the DSP genuinely benefits from different tuning, both modes are provided. Mode differences are always substantive, not cosmetic.
- **Listen mode.** All five plugins output the removed material rather than the processed result when Listen is engaged — useful for checking whether processing is targeting the right content.
- **Realtime-safe.** No heap allocation or blocking work on the audio thread.
- **Stable parameter contracts.** Parameter IDs and latency behaviour are designed to be deterministic and host-friendly.

---

## Shared Behaviours

### Vocal vs General

Most products expose a `Vocal / General` mode switch. This is not a simple preset selector — the DSP tuning changes in each product based on mode:

- **Vocal** is speech-aware. It uses more conservative source protection, formant-friendly behaviour, and less aggressive cleanup. It biases processing toward what a voice-aware engineer would do by hand.
- **General** is less constrained. It allows broader cleanup, deeper cuts, and full-range shaping without speech-protective bias.

The shared mode policy lives in `Source/vxsuite/framework/VxSuiteModePolicy.h`.

### Listen

All five plugins support `Listen`. Enabling it routes the removed content — not the processed output — to your monitors or DAW track. This is useful for:

- Verifying that processing targets noise, reverb, mud, or harshness rather than the wanted source
- Checking at what amount the plugin starts removing too much useful signal
- Dialing in just enough processing before switching back to normal output

For latency-bearing products (`VXDenoiser`, `VXDeverb`), the framework aligns the dry reference internally so the Listen signal remains temporally meaningful.

### Shared Voice Analysis

The framework runs block-rate signal evidence in `VxSuiteVoiceAnalysis` and exposes:

- Speech presence and stability
- Directness vs. late-tail likelihood
- Transient and artifact risk
- A composite voice-protection recommendation

Products like `VXPolish` consume this evidence to steer multiple internal stages adaptively — without requiring the user to configure them separately.

---

## Plugin Details

### VXDenoiser

STFT-based spectral denoiser for steady background noise. Internally uses noise-floor tracking, Bark-band masking, gain smoothing, and stereo reconstruction that preserves side information.

**What it does:**
- Converts the signal to a short-time spectral representation
- Tracks and estimates the noise floor over time
- Applies frequency-bin gain reduction weighted by speech-protection policy
- Smooths gains in time and frequency to suppress musical noise and chattering
- Reconstructs the output and reintroduces side information in a controlled way

**Controls:**

`Clean` — Main denoising amount. Higher values push spectral reduction harder. Think of it as "remove more background," not "make it brighter."

`Guard` — Artifact protection. Higher values preserve harmonics, transients, and source detail. Raise this when the noise reduction sounds phasey, watery, or lispy.

**Mode differences:**
- `Vocal`: biases toward speech safety; increases harmonic guard strictness
- `General`: behaves as a broader spectral cleaner; allows deeper cleanup

**Best for:** air conditioning, computer fans, preamp hiss, steady room noise under speech
**Not for:** room tails, plosives, tonal EQ — use `VXDeverb` or `VXPolish`

---

### VXSubtract

Profile-guided subtractive denoiser. Designed for cases where a learned noise print provides enough information to go further than a blind denoiser — while still applying speech and transient protection that a naive subtract would miss.

**What it does:**
- Captures a representative noise profile via `Learn`
- Measures capture progress and confidence during the learning pass
- Freezes a learned spectral estimate when capture completes
- Subtracts that profile with layered protection for speech, tonality, and transients
- Uses `Protect` to trade removal aggressiveness for source preservation

**Controls:**

`Subtract` — Main subtractive amount. Higher values remove more of the learned profile and adaptive background estimate.

`Protect` — Speech and detail protection. Raise this when the cleanup sounds correct but the voice becomes hollow, chirpy, or over-scooped.

`Learn` — Arms guided profile capture. Play the representative noise, let the confidence meter build, then stop. The profile locks when sufficient valid material has been observed.

**Best for:** consistent learnable noise beds, HVAC with a stable fingerprint, machine hum, hiss that can be isolated before the wanted audio
**Not for:** room size changes, EQ, one-pass vocal finishing — use `VXDeverb`, `VXProximity`, `VXPolish`

---

### VXDeverb

Dereverberation processor built on spectral late-reverberant suppression (LRSV). Introduces latency; compensates internally and reports it to the host. In `Vocal` mode, optionally applies a WPE-style stage for speech-oriented smear reduction.

**What it does:**
- Estimates room decay characteristics using a shared RT60 tracker
- Performs STFT-domain late-tail suppression using delayed spectral history
- Scales over-subtraction with the `Reduce` amount
- Applies WPE-style cleanup in `Vocal` mode
- Reconstructs with reported host latency
- Restores low-frequency weight post-dereverberation via `Blend`

**Controls:**

`Reduce` — Dereverb authority. Controls the mix between dry-aligned and fully processed output, and scales internal over-subtraction. Low values are gentle; high values push hard.

`Blend` — Low-body restoration after dereverberation. Reintroduces low-end toward the dry-aligned reference. Useful when the room cleanup makes the voice feel lean or over-dried.

**Mode differences:**
- `Vocal`: preserves direct speech more aggressively; includes WPE-style cleanup
- `General`: allows deeper tail reduction across the full range

**Listen:** outputs the removed room contribution using a latency-aligned dry reference.

**Best for:** untreated room recordings, distant narration, podcast tracks in reflective spaces
**Not for:** constant broadband hiss, close-mic tonal enhancement — use `VXDenoiser`, `VXProximity`

---

### VXProximity

Lightweight close-mic tone shaper. Simulates the tonal effect of moving a microphone closer to the source using shelf filters — no convolution or physical modelling. Intentionally simple, cheap, and predictable.

**What it does:**
- Shapes low-end body and warmth with a mode-dependent low shelf
- Adds upper presence or air with a high shelf
- Different shelf frequencies and tuning between `Vocal` and `General`

**Controls:**

`Closer` — Main proximity control. Increases low-shelf gain and shifts the shelf region as the effect intensifies. Makes the source feel physically nearer to the mic.

`Air` — Upper clarity and openness. Prevents the result from becoming too thick or chesty as `Closer` increases.

**Mode differences:**
- `Vocal`: low shelf tuned ~80–200 Hz, high shelf for presence; best for narration and vocals
- `General`: low shelf sits higher and behaves more broadly; high shelf reaches further into full-range material

**Best for:** making a voice feel more intimate, restoring closeness in distant recordings, sweetening before `VXPolish`
**Not for:** noise, reverb, dynamic correction

---

### VXPolish

The suite's finishing processor. A macro-style front end that drives a multi-stage corrective and recovery DSP chain internally. Rather than doing one thing extremely deeply, `VXPolish` smooths multiple common voice problems in a single pass.

**Corrective stages:**
- Low-mid mud detection and reduction (content-adaptive, ratio-based)
- De-essing (high-frequency ratio detection, mode-aware threshold)
- Plosive taming (burst detection with fast/slow envelope comparison)
- Gentle compression (auto-makeup, sidechain-aware)
- Multi-band dynamic trouble smoothing (six peaking bands, 1.4–10.5 kHz, detection-driven — only cuts where content genuinely exceeds reference)
- Body recovery (frequency-band lift toward target spectral ratios)
- Output limiter

**Slope filters (clickable icons in the UI):**
- Low-cut (HPF): 2nd-order Butterworth — 80 Hz in `Vocal`, 40 Hz in `General`
- High-shelf cut: 1st-order — 12 kHz / −4 dB in `Vocal`, 16 kHz / −3 dB in `General`

**Controls:**

`Polish` — Main corrective amount. Drives overall cleanup intensity across mud reduction, de-essing, trouble smoothing, compression, and finishing.

`Body` — Recovery amount. Restores useful spectral weight after corrective stages. Works as a content-aware recovery lift, not a static low boost.

`Focus` — Steers the correction target. Lower settings emphasise low-mid cleanup and warmth management; higher settings emphasise upper-mid and top-end smoothing.

**Mode differences** — Vocal and General tuning differs across every internal stage: thresholds, max cut depths, shelf frequencies, detection sensitivity.

**Listen:** outputs the removed content — mud, harshness, sibilance, plosive energy, and roughness — rather than the polished result.

**Best for:** final cleanup on voiceover and podcast, one-pass corrective finishing, reducing small spectral problems without a long manual chain
**Not for:** heavy broadband denoising, true dereverberation, intentional character EQ

---

## Decision Guide

Not sure where to start? Use this:

| Problem | Start Here |
|---|---|
| Constant noise under the whole recording | **VXDenoiser** |
| Repeating, learnable noise bed | **VXSubtract** |
| Room wash, echo, or reverberant speech | **VXDeverb** |
| Clean recording that feels distant or thin | **VXProximity** |
| Needs finishing, smoothing, corrective polish | **VXPolish** |

---

## Build

The project uses CMake and JUCE. A pre-configured JUCE submodule is included.

```bash
# Configure
cmake -S . -B build

# Build everything
cmake --build build -j$(nproc)
```

Built VST3 bundles are staged into:

```
Source/vxsuite/vst/
```

### Targets

| Target | Description |
|---|---|
| `VXPolish_VST3` | Polish plugin VST3 bundle |
| `VXDenoiser_VST3` | Denoiser plugin VST3 bundle |
| `VXSubtract_VST3` | Subtract plugin VST3 bundle |
| `VXDeverb_VST3` | Deverb plugin VST3 bundle |
| `VXProximity_VST3` | Proximity plugin VST3 bundle |
| `VXDeverbTests` | Focused deverb test binary |
| `VXDeverbMeasure` | Deverb measurement harness |
| `VxSuiteVoiceAnalysisTests` | Shared voice-analysis tests |

---

## Repository Layout

```
Source/
  vxsuite/
    framework/        Shared processor · editor · parameters · mode · voice analysis
    products/
      denoiser/       VXDenoiser processor + DSP
      subtract/       VXSubtract processor + DSP
      deverb/         VXDeverb processor + DSP
      proximity/      VXProximity processor + DSP
      polish/         VXPolish processor + DSP
  dsp/                Shared subtractive DSP used by VXSubtract
tests/                Measurement and behaviour tests
cmake/                Project CMake helpers
docs/                 Framework and product reference
```

### Key Source Files

| File | Purpose |
|---|---|
| `framework/VxSuiteProcessorBase.*` | Shared processor lifecycle, mode, and listen behaviour |
| `framework/VxSuiteEditorBase.*` | Shared editor layout, knobs, activity indicators, shelf icons |
| `framework/VxSuiteModePolicy.h` | `Vocal` / `General` tuning contract |
| `framework/VxSuiteProduct.h` | `ProductIdentity` struct — per-plugin configuration |
| `framework/VxSuiteVoiceAnalysis.*` | Block-rate speech and voice evidence |
| `framework/VxSuiteParameters.h` | Shared APVTS parameter layout helpers |
| `products/polish/dsp/VxPolishDsp.*` | Polish multi-stage corrective DSP |

---

## Status

VST3 on macOS. All five plugins build, stage, and present a consistent VX Suite framework contract. Test coverage is strongest for the dereverberation and voice analysis paths.
