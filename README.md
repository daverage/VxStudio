# VX Suite

> **Focused, realtime-safe audio processors for voice and vocal production.**
> Seven plugins. One shared framework. One job each.

VX Suite is an open-source collection of JUCE/VST3 audio effects built around a shared C++ framework, minimal control surfaces, and performance-first DSP. Each plugin solves one problem cleanly rather than trying to be a general-purpose channel strip.

The underlying framework — `Source/vxsuite/framework/` — is designed to be reusable. If you want to build your own JUCE-based VST3 plugin, you can use it as a foundation: it handles parameter registration, UI layout, editor creation, block smoothing, and output safety so you only have to write the DSP. See [`Source/vxsuite/framework/README.md`](Source/vxsuite/framework/README.md) for a step-by-step guide and a worked example.

I wanted simple to use, smart plugins that would all work well together to enable content creators to produce high-quality audio with minimal effort. My initial use case was to take audio from a video that was recorded on my phone, at a distance, then clean it up to the sound quality of a studio recording.
This repo contains one of the few implementations of de-reverb or deverb / reverberation removal on Github as well!
 
---

## 🚀 Plugins at a Glance

| Plugin | Job | Controls | Best For |
|---|---|---|---|
| **VXDeepFilterNet** | ML-powered voice isolation | `Clean` · `Guard` | Heavy noise, street sounds, non-steady interference |
| **VXDenoiser** | Broadband noise reduction | `Clean` · `Guard` | Hiss, fan noise, room tone, HVAC |
| **VXSubtract** | Profile-guided subtractive denoise | `Subtract` · `Protect` · `Learn` | Learnable noise beds, hum, machine noise |
| **VXDeverb** | Room tail and reverb removal | `Reduce` · `Blend` | Echoey rooms, distant speech, reverberant dialogue |
| **VXProximity** | Close-mic tone shaping | `Closer` · `Air` | Intimacy, warmth, fullness after cleanup |
| **VXCleanup** | Corrective voice cleanup | `Cleanup` · `Body` · `Focus` | Mud, harshness, breaths, plosives, sibilance |
| **VXFinish** | Smart finish and level control | `Finish` · `Body` · `Gain` | Compression, recovery lift, controlled loudness, final polish |

---

## 🛠 Recommended Signal Chain

VX Suite is designed around composability. Each plugin complements the others without duplicating them. When a recording has multiple problems, this order works best:

```
VXDeepFilterNet / VXDenoiser / VXSubtract  →  VXDeverb  →  VXCleanup  →  VXProximity  →  VXFinish
```

**Why this order:**

1. **Remove noise first** — later processors should not react to or enhance a dirty noise floor.
2. **Remove room tail before tone-shaping** — proximity or polish moves can emphasize reverberant smear if applied to an untreated room recording.
3. **Clean up tone before enhancement** — subtractive cleanup prevents later processors from lifting mud, breaths, or harshness back up.
4. **Add closeness after cleanup** — proximity-style shaping works better on a stable, already-cleaner source.
5. **Finish last** — compression, recovery, and smart gain are safest once the signal is already controlled.

### Practical Examples

```
Heavy street noise, reflective room:
  VXDeepFilterNet → VXDeverb → VXCleanup → VXFinish

HVAC noise, thin and distant vocal:
  VXSubtract → VXDeverb → VXCleanup → VXProximity → VXFinish
```

### Denoiser Choice

| Situation | Recommended |
|---|---|
| Heavy, non-stationary, or complex noise | **VXDeepFilterNet** |
| General steady broadband noise (hiss, fans) | **VXDenoiser** |
| Noise with a learnable spectral fingerprint | **VXSubtract** |
| Both present | Use ML isolation first, then target remaining steady beds |

---

## 💎 Design Philosophy

Every VX Suite plugin follows the same contract:

- **One main job.** Each processor is designed around a single outcome. Scope boundaries are intentional.
- **Minimal controls.** One or two headline controls, with an optional third only when it materially improves the result.
- **Vocal and General modes.** Where the DSP genuinely benefits from different tuning, both modes are provided. Mode differences are always substantive, not cosmetic.
- **Listen mode.** All plugins output the removed material rather than the processed result when Listen is engaged — useful for checking whether processing is targeting the right content.
- **Realtime-safe.** No heap allocation or blocking work on the audio thread.
- **Stable parameter contracts.** Parameter IDs and latency behaviour are designed to be deterministic and host-friendly.

---

## 🔌 Shared Behaviours

### Vocal vs General (or Model Select)

Most products expose a `Vocal / General` mode switch. This is not a simple preset selector — the DSP tuning changes in each product based on mode.

- **Vocal** is speech-aware. It uses more conservative source protection, formant-friendly behaviour, and less aggressive cleanup. It biases processing toward what a voice-aware engineer would do by hand.
- **General** is less constrained. It allows broader cleanup, deeper cuts, and full-range shaping without speech-protective bias.

**Note:** `VXDeepFilterNet` uses this selector to switch between **DeepFilterNet 3** and **DeepFilterNet 2** models instead of Vocal/General modes.

### Listen

All plugins support `Listen`. Enabling it routes the removed content — not the processed output — to your monitors or DAW track. This is useful for:

- Verifying that processing targets noise, reverb, mud, or harshness rather than the wanted source.
- Checking at what amount the plugin starts removing too much useful signal.
- Dialing in just enough processing before switching back to normal output.

### Shared Voice Analysis

The framework runs block-rate signal evidence in `VxSuiteVoiceAnalysis` and exposes:

- Speech presence and stability.
- Directness vs. late-tail likelihood.
- Transient and artifact risk.
- A composite voice-protection recommendation.

---

## 🔍 Plugin Details

### VXDeepFilterNet

State-of-the-art ML-powered voice isolation. Uses DeepFilterNet models (v2 and v3) to separate speech from complex background noise. Highly effective at removing non-stationary noise that traditional spectral denoisers cannot touch.

**Controls:**

`Clean` — ML denoise amount. Higher values push the DeepFilter model harder to isolate the voice.

`Guard` — Speech protection. Pulls back some dry detail if the model starts sounding too assertive or "processed."

**Model differences:**
- `DeepFilterNet 3`: Latest model, generally superior for complex noise and transients.
- `DeepFilterNet 2`: Earlier model, sometimes preferred for specific noise textures.

---

### VXDenoiser

STFT-based spectral denoiser for steady background noise. Internally uses noise-floor tracking, Bark-band masking, gain smoothing, and stereo reconstruction that preserves side information.

**Controls:**

`Clean` — Main denoising amount. Higher values push spectral reduction harder.

`Guard` — Artifact protection. Higher values preserve harmonics, transients, and source detail.

**Best for:** air conditioning, computer fans, preamp hiss, steady room noise under speech.

---

### VXSubtract

Profile-guided subtractive denoiser. Designed for cases where a learned noise print provides enough information to go further than a blind denoiser — while still applying speech and transient protection.

**Controls:**

`Subtract` — Main subtractive amount. Higher values remove more of the learned profile.

`Protect` — Speech and detail protection. Raise this when the cleanup sounds correct but the voice becomes hollow or over-scooped. Higher settings also make the subtract engine back off more, so it is both a preservation control and a subtraction-strength limiter.

`Learn` — Starts guided profile capture. Play the representative noise, then switch `Learn` off to lock the replacement profile.

---

### VXDeverb

Dereverberation processor built on spectral late-reverberant suppression (LRSV). In `Vocal` mode, it additionally runs a full per-bin online WPE (Weighted Prediction Error) dereverberation stage.

**Controls:**

`Reduce` — Dereverb authority. Controls the mix between dry-aligned and fully processed output.

`Blend` — Low-body restoration. Reintroduces low-end weight post-dereverberation.

---

### VXProximity

Lightweight close-mic tone shaper. Simulates the tonal effect of moving a microphone closer to the source using shelf filters.

**Controls:**

`Closer` — Main proximity control. Increases low-shelf gain and shifts the shelf region.

`Air` — Upper clarity and openness. Prevents the result from becoming too thick.

---

### VXCleanup

Corrective cleanup processor. Focused on subtractive voice repair rather than enhancement: mud reduction, de-essing, de-breath, plosive control, and intelligent trouble smoothing.

**Controls:**

`Cleanup` — Main corrective amount. Drives overall cleanup intensity.

`Body` — Preservation bias. Keeps useful low and low-mid weight while cleanup works.

`Focus` — Steers the correction target (low-mid cleanup vs. upper-mid / air cleanup).

---

### VXFinish

Final shaping and level processor. Handles the restorative and dynamic side after cleanup: smart recovery lift, compression, intelligent gain makeup, and limiting, with noise-aware gating to avoid reintroducing floor noise.

**Controls:**

`Finish` — Main finish amount. Drives compression, final control, and overall polish.

`Body` — Recovery amount. Restores useful weight and presence intelligently after cleanup.

`Gain` — Smart gain. Increases mode-aware lift and makeup only when the signal is clean enough to support it.

---

## 🏗 Build

The project uses CMake and JUCE. A pre-configured JUCE submodule is included.

```bash
# Configure
cmake -S . -B build

# Build everything
cmake --build build -j$(nproc)
```

### Targets

| Target | Description |
|---|---|
| `VXDeepFilterNet_VST3` | DeepFilterNet isolation plugin |
| `VXDenoiser_VST3` | Denoiser plugin |
| `VXSubtract_VST3` | Subtract plugin |
| `VXDeverb_VST3` | Deverb plugin |
| `VXProximity_VST3` | Proximity plugin |
| `VXCleanup_VST3` | Cleanup plugin |
| `VXFinish_VST3` | Finish plugin |

---

## 📂 Repository Layout

```
Source/
  vxsuite/
    framework/        Shared processor · editor · parameters · mode · voice analysis
    products/
      deepfilternet/  VXDeepFilterNet processor + ML service
      denoiser/       VXDenoiser processor + DSP
      subtract/       VXSubtract processor + DSP
      deverb/         VXDeverb processor + DSP
      proximity/      VXProximity processor + DSP
      cleanup/        VXCleanup processor
      finish/         VXFinish processor
      polish/         VXPolish processor + DSP
  dsp/                Shared subtractive DSP used by VXSubtract
tests/                Measurement and behaviour tests
cmake/                Project CMake helpers
docs/                 Framework and product reference
```

---

## 🚥 Status

VST3 on macOS. All seven plugins build, stage, and present a consistent VX Suite framework contract. VXDeepFilterNet requires ONNX Runtime and valid model files in the `assets/` directory.
