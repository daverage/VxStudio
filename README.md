# VX Suite

> **Focused, realtime-safe audio processors for voice and vocal production.**
> Twelve plugins. One shared framework. One job each.

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
| **VXTone** | Bass and treble tone shaping | `Bass` · `Treble` | Warmth, brightness, tonal balance after cleanup |
| **VXFinish** | Smart finish and level control | `Finish` · `Body` · `Gain` | Compression, recovery lift, controlled loudness, final polish |
| **VXOptoComp** | LA2A-style opto compression | `Peak Red.` · `Body` · `Gain` | Natural levelling, limiting, smooth dynamic control |
| **VXLeveler** | Adaptive riding and programme levelling | `Level` · `Control` | Speech riding, long-form consistency, fader-style support |
| **VXRebalance** | Heuristic source-family rebalance | `Vocals` · `Drums` · `Bass` · `Guitar` · `Other` · `Strength` | Broad mix balance moves without stems |
| **VXStudioAnalyser** | Chain-aware spectrum analyser | `Avg Time` · `Smoothing` | Comparing dry vs wet, inspecting per-stage spectral changes, and checking the final VX chain |

---

## 🔢 Versions

Framework and DSPs now use independent semantic versions. A help/doc-only update can bump a DSP without requiring a framework release, and framework UI/layout work can move independently of product DSP releases.

| Component | Version |
|---|---|
| **VX Suite Framework** | `0.2.0` |
| **VXDeepFilterNet** | `0.2.0` |
| **VXDenoiser** | `0.2.0` |
| **VXSubtract** | `0.2.0` |
| **VXDeverb** | `0.2.0` |
| **VXProximity** | `0.2.0` |
| **VXCleanup** | `0.2.0` |
| **VXTone** | `0.2.0` |
| **VXFinish** | `0.3.0` |
| **VXOptoComp** | `0.3.0` |
| **VXLeveler** | `0.2.0` |
| **VXRebalance** | `0.2.0` |
| **VXStudioAnalyser** | `0.2.0` |

The plugin Help popup and this README are a shared documentation contract. When a DSP's UI, behaviour, or recommended usage changes, update both together.

---

## 🛠 Recommended Signal Chain

VX Suite is designed around composability. Each plugin complements the others without duplicating them. When a recording has multiple problems, this order works best:

```
VXDeepFilterNet / VXDenoiser / VXSubtract  →  VXDeverb  →  VXCleanup  →  VXProximity  →  VXTone  →  VXFinish / VXOptoComp  [→  VXStudioAnalyser]
```

**Why this order:**

1. **Remove noise first** — later processors should not react to or enhance a dirty noise floor.
2. **Remove room tail before tone-shaping** — proximity or polish moves can emphasize reverberant smear if applied to an untreated room recording.
3. **Clean up tone before enhancement** — subtractive cleanup prevents later processors from lifting mud, breaths, or harshness back up.
4. **Add closeness after cleanup** — proximity-style shaping works better on a stable, already-cleaner source.
5. **Shape tone after proximity** — VXTone shelves work best on a signal that already has the right spatial character.
6. **Compress and finish last** — VXFinish or VXOptoComp are safest once the signal is already clean and shaped. Use VXFinish for smart recovery and gain, VXOptoComp when you want opto-style levelling or limiting character.
7. **VXStudioAnalyser at the very end** — insert it last in the chain to inspect dry input, matched per-plugin traces, and the final wet result simultaneously. It is pass-through and adds no colouration.

### Practical Examples

```
Heavy street noise, reflective room:
  VXDeepFilterNet → VXDeverb → VXCleanup → VXFinish → VXStudioAnalyser

HVAC noise, thin and distant vocal:
  VXSubtract → VXDeverb → VXCleanup → VXProximity → VXTone → VXFinish

Levelling and polish after a clean recording:
  VXCleanup → VXTone → VXOptoComp
```

### Denoiser Choice

| Situation | Recommended |
|---|---|
| Heavy, non-stationary, or complex noise | **VXDeepFilterNet** |
| General steady broadband noise (hiss, fans) | **VXDenoiser** |
| Noise with a learnable spectral fingerprint | **VXSubtract** |
| Both present | Use ML isolation first, then target remaining steady beds |

### REAPER Preset Pack

The repo now includes a REAPER-facing preset pack under [`assets/reaper/`](assets/reaper/):

- [`assets/reaper/RPL Files/`](assets/reaper/RPL%20Files) contains one `.RPL` library per VX effect.
- [`assets/reaper/FX Chains/`](assets/reaper/FX%20Chains) contains full `.RfxChain` starting chains for the shared scenarios below.
- [`tools/reaper/generate_vx_reaper_presets.lua`](tools/reaper/generate_vx_reaper_presets.lua) regenerates both from the current VX Suite plug-ins inside REAPER.

Each effect library uses the same four preset names so a REAPER user can pick the same scenario title in every tool when building a chain manually:

| Preset | Use Case | Recommended Chain |
|---|---|---|
| `Camera Review - Far Phone` | Slightly noisy review-to-camera audio from a phone a few meters from the presenter. This is the strongest full-stack voice-repair example. | `VXSubtract → VXCleanup → VXDenoiser → VXDeepFilterNet → VXDeverb → VXProximity → VXTone → VXOptoComp → VXFinish` |
| `Live Music - Front Of Room` | Single-point live music or rehearsal capture where preserving the whole mix matters more than voice isolation. | `VXCleanup → VXTone → VXOptoComp → VXFinish` |
| `Podcast Finishing - Clean Voice` | Already-decent spoken-word capture that mainly needs polish, density, and tonal finishing. | `VXCleanup → VXProximity → VXTone → VXOptoComp → VXFinish` |
| `Mixed Audio - Voice + Guitar` | One track containing both voice and live instrument at the same time, where aggressive voice-specific denoise would damage the instrument. | `VXCleanup → VXTone → VXOptoComp → VXFinish` |

These scenario names are intentionally shared across all `.RPL` files, but not every processor is meant to be equally active in every scenario. For example, the `Live Music` and `Mixed Audio` presets keep `VXSubtract`, `VXDenoiser`, `VXDeepFilterNet`, and `VXDeverb` very conservative or effectively neutral because those source types do not tolerate speech-only cleanup well.

---

## 💎 Design Philosophy

Every VX Suite plugin follows the same contract:

- **One main job.** Each processor is designed around a single outcome. Scope boundaries are intentional.
- **Minimal controls.** One or two headline controls, with an optional third only when it materially improves the result.
- **Vocal and General modes.** Where the DSP genuinely benefits from different tuning, both modes are provided. Mode differences are always substantive, not cosmetic.
- **Listen mode.** Removal-style plugins audition what was removed; additive/finishing plugins audition what they added. The listen contract follows the product role rather than forcing one listen behavior on every DSP type.
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

All processing plugins support `Listen`, but the audition signal depends on the product role:

- **Removal / corrective tools** such as `VXDenoiser`, `VXSubtract`, `VXDeverb`, and `VXCleanup` output the removed material so you can hear what is being taken away.
- **Additive / finishing tools** such as `VXProximity`, `VXTone`, `VXFinish`, and `VXOptoComp` output the added delta so you can hear exactly what the processor is contributing.

This is useful for:

- Verifying that processing is targeting the right content.
- Checking where a processor starts to become too aggressive.
- Understanding what each stage is actually doing before switching back to the normal wet output.

### Shared Voice Analysis

The framework runs block-rate signal evidence in `VxSuiteVoiceAnalysis` and exposes:

- Speech presence and stability.
- Directness vs. late-tail likelihood.
- Transient and artifact risk.
- A composite voice-protection recommendation.

---

## 🔍 Plugin Details

### VXDeepFilterNet

ML-powered voice isolation for heavy or complex background noise. It is the strongest noise-removal tool in the suite when classic denoisers cannot separate the voice cleanly enough.

**How to use it**

- Start with `Clean` around `55%` to `70%` and raise it until the noise falls back clearly.
- Use `Guard` to restore natural speech detail if the result starts to sound over-processed.
- Choose the model that behaves best on the material. `DeepFilterNet 3` is usually the first choice.

**Example settings**

- Street or traffic noise: `Clean 75%`, `Guard 65%`.
- Busy cafe or moving background: `Clean 65%`, `Guard 75%`.
- Gentler isolation before other cleanup: `Clean 50%`, `Guard 80%`.

**Practical scenarios**

- Phone or camera speech recorded in public spaces.
- Dialogue with mixed non-stationary interference.
- First stage before deverb, cleanup, and finishing processors.

---

### VXDenoiser

Broadband spectral denoiser for steady noise such as hiss, fans, HVAC, and room tone. It is designed to clean the bed without turning into a voice-isolation tool.

**How to use it**

- Raise `Clean` until the steady noise floor drops to a useful level.
- If the voice loses harmonics or consonants, increase `Guard`.
- Use it early in the chain, before deverb and finishing.

**Example settings**

- Light hiss: `Clean 40%`, `Guard 75%`.
- Fan or HVAC: `Clean 60%`, `Guard 70%`.
- Safety-first spoken voice: `Clean 50%`, `Guard 85%`.

**Practical scenarios**

- Podcast or narration with constant background air noise.
- Camera audio with a steady room bed.
- Follow-up cleanup after a stronger ML pass leaves residual steady noise.

---

### VXSubtract

Profile-guided subtractive denoiser for noises with a learnable fingerprint. It goes further than a blind denoiser when you can capture representative noise safely.

**How to use it**

- Enable `Learn` and play the noise by itself for about one to two seconds.
- Turn `Learn` off to lock the profile.
- Raise `Subtract` for more removal and raise `Protect` if the source becomes hollow or over-scooped.

**Example settings**

- Machine or room noise with a clean profile: `Subtract 65%`, `Protect 80%`.
- More aggressive learned subtraction: `Subtract 80%`, `Protect 70%`.
- Delicate speech preservation: `Subtract 55%`, `Protect 88%`.

**Practical scenarios**

- Air conditioner, projector, or other repeatable tonal or broadband beds.
- Noise-only intro or pause available for learning.
- Pre-clean stage before deverb and tonal shaping.

---

### VXDeverb

Room-tail and reverb reduction for speech and general programme material. It reduces smeared ambience while keeping direct sound usable.

**How to use it**

- Increase `Reduce` until the room tail pulls back without making the source papery.
- Use `Blend` to restore low-body weight if the dereverb pass gets too lean.
- Place it before proximity, tone shaping, and final dynamics.

**Example settings**

- Small reflective room: `Reduce 50%`, `Blend 40%`.
- Distant voice in a live room: `Reduce 70%`, `Blend 35%`.
- General ambience tidy-up: `Reduce 35%`, `Blend 50%`.

**Practical scenarios**

- Phone or camera speech recorded far from the source.
- Dialogue in an untreated room.
- Recovering clarity before cleanup and finishing.

---

### VXProximity

Close-mic tone shaping that adds a fuller, nearer vocal perspective after cleanup. It is a tone-and-space shaper, not a corrective denoiser.

**How to use it**

- Raise `Closer` to add weight and intimacy.
- Use `Air` to stop the sound becoming overly thick or shut in.
- Apply it after noise and room problems are already under control.

**Example settings**

- Thin distant voice: `Closer 65%`, `Air 45%`.
- Warm spoken-word polish: `Closer 55%`, `Air 40%`.
- Subtle intimacy lift: `Closer 45%`, `Air 50%`.

**Practical scenarios**

- Phone or room mics that feel too far away.
- Voice tracks that need warmth after cleanup.
- Pre-tone-shaping enhancement before `VXTone`.

---

### VXCleanup

Corrective voice cleanup for mud, harshness, breaths, plosives, sibilance, and general tonal trouble. It is subtractive repair before enhancement.

**How to use it**

- Raise `Cleanup` until the distracting problems start to fall away.
- Increase `Body` if the result becomes too thin.
- Use `Focus` to steer the correction toward low-mid cleanup or more presence and air control.

**Example settings**

- Muddy spoken voice: `Cleanup 55%`, `Body 55%`, `Focus 45%`.
- Harsh, breathy voice: `Cleanup 60%`, `Body 50%`, `Focus 70%`.
- Light corrective tidy-up: `Cleanup 35%`, `Body 55%`, `Focus 55%`.

**Practical scenarios**

- Dialogue that needs cleanup before any enhancement.
- Speech with boxiness, spit, or low-end bumps.
- Preparation stage before proximity, tone, or final compression.

---

### VXTone

Simple bass and treble shaping with mode-aware shelf placement. It is the fast tonal balance stage after corrective cleanup.

**How to use it**

- Start from the centre position and make small moves.
- Use `Bass` for weight and warmth, `Treble` for brightness and openness.
- Prefer subtle shaping after cleanup and proximity, not before.

**Example settings**

- Need a little warmth: `Bass 58%`, `Treble 50%`.
- Dull voice lift: `Bass 50%`, `Treble 60%`.
- Balanced polish: `Bass 55%`, `Treble 56%`.

**Practical scenarios**

- Final tonal balance after cleanup.
- Correcting a track that feels thin or dull.
- Subtle pre-finish shaping before `VXFinish` or `VXOptoComp`.

---

### VXFinish

Final polish and level control after cleanup and tone work. It combines finish compression, bounded body recovery, makeup, and limiting for a more produced result.

**How to use it**

- Raise `Finish` to increase compression, polish, and level control.
- Use `Body` to recover useful weight after cleanup.
- `Gain` is unity-centered: left is `50%`, centre is `100%`, right is `150%`.

**Example settings**

- Light vocal polish: `Finish 35%`, `Body 55%`, `Gain 100%`.
- Produced spoken voice: `Finish 60%`, `Body 58%`, `Gain 110%`.
- Conservative final control after heavy cleanup: `Finish 45%`, `Body 52%`, `Gain 100%`.

**Practical scenarios**

- Last stage on cleaned speech.
- Recovery and polish after corrective processing.
- Fast final level shaping when you want more than a plain compressor.

---

### VXOptoComp

LA2A-style opto levelling and limiting with slower, smoother program-dependent gain reduction than `VXFinish`. It is for natural dynamic control with opto character.

**How to use it**

- Raise `Peak Red.` to drive more opto gain reduction.
- Use `Body` for light post-compressor weight shaping.
- `Gain` is unity-centered: left is `50%`, centre is `100%`, right is `150%`.

**Example settings**

- Gentle levelling: `Peak Red. 35%`, `Body 52%`, `Gain 100%`.
- Firm voice levelling: `Peak Red. 55%`, `Body 54%`, `Gain 108%`.
- Limiter-style general control: `Peak Red. 65%`, `Body 50%`, `Gain 100%`.

**Practical scenarios**

- Natural spoken-word levelling.
- Opto-style smoothing after cleanup and tone shaping.
- General dynamic control when `VXFinish` feels too produced.

---

### VXLeveler

Adaptive level control with two distinct behaviours: `Vocal Rider` for speech-focused riding and `Mix Leveler` for broader programme smoothing. It is meant to feel more like automatic fader support than static compression.

**How to use it**

- Choose `Vocal Rider` when speech intelligibility is the priority.
- Choose `Mix Leveler` when you want gentler overall programme control.
- Use `Level` for how far the processor should even things out and `Control` for how assertively it reacts.

**Example settings**

- `Vocal Rider` for uneven dialogue: `Level 65%`, `Control 60%`.
- `Mix Leveler` for broad programme smoothing: `Level 50%`, `Control 45%`.
- Heavier rider action: `Level 75%`, `Control 70%`.

**Practical scenarios**

- Speech riding in mixed or inconsistent recordings.
- Programme smoothing before final finish or limiting.
- Long-form content where sections vary in level too much.

---

### VXRebalance

Heuristic source-family rebalance for full mixes. It lets you gently lift or tuck vocals, drums, bass, guitar, and residual content without running a heavyweight stem-separation model.

**How to use it**

- Start with small moves on the source lane you want to rebalance.
- Use `Strength` to scale the overall impact of all five moves together.
- Treat it like broad corrective balance, not surgical stem extraction.

**Example settings**

- Bring vocals forward slightly: `Vocals 60%`, `Strength 70%`.
- Tuck a boomy rhythm section: `Bass 42%`, `Drums 45%`, `Strength 75%`.
- Open a busy rehearsal mix: `Vocals 58%`, `Guitar 47%`, `Other 46%`, `Strength 65%`.

**Practical scenarios**

- Quick rebalance of a rough stereo mix.
- Making speech or lead lines feel more present without remixing stems.
- Light source-family shaping before final tone and dynamics.

---

### VXStudioAnalyser

Chain-aware dry-vs-wet spectrum analyser for VX Suite. Insert it last to inspect either the whole chain or one specific VX stage at a time.

**How to use it**

- Put the analyser at the end of the VX chain.
- Select `Full Chain` to compare chain input against final output.
- Click a stage in the left rail to inspect only that processor's dry-vs-wet spectrum.

**Example settings**

- General readability: `Avg Time 500 ms`, `Smoothing 1/3 OCT`.
- Fast transient inspection: `Avg Time 125 ms`, `Smoothing 1/12 OCT`.
- Broad tonal overview: `Avg Time 1000 ms`, `Smoothing 1 OCT`.

**Practical scenarios**

- Checking what one plugin in the chain is really changing.
- Comparing whole-chain tone before and after processing.
- Debugging over-bright, over-thin, or over-damped processing decisions.

---

## 🏗 Build

The project uses CMake and JUCE. A pre-configured JUCE submodule is included.

### macOS

**Prerequisites:**
- Xcode Command Line Tools (`xcode-select --install`)
- CMake 3.20+ (`brew install cmake`)
- Rust (`curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh`) — required only for VXDeepFilterNet

```bash
# Clone with submodules (JUCE is a submodule)
git clone --recurse-submodules <repo-url>
cd VxStudio

# Configure
cmake -S . -B build

# Build all plugins
cmake --build build -j$(nproc)

# Or build a single plugin
cmake --build build --target VXDenoiser_VST3 -j$(nproc)
```

Built `.vst3` bundles are staged automatically into `Source/vxsuite/vst/`.

VXDeepFilterNet additionally requires model files in `assets/deepfilternet/models/`. Without them the plugin builds but will report no model available at runtime. All other plugins have no extra dependencies.

### Windows

> **Windows support is theoretical at the moment — the build system is fully wired up for it, but it has not been tested end-to-end. If you try it, please open an issue with your results.**

The main unknown is whether the Rust `tract-onnx` crate inside libDF compiles cleanly against the MSVC toolchain. Everything else in the codebase is cross-platform through JUCE.

**Prerequisites:**
- Visual Studio 2022 with the "Desktop development with C++" workload (MSVC v143)
- CMake 3.20+
- Rust with the MSVC target: `rustup target add x86_64-pc-windows-msvc`

```bat
:: Configure (x64)
cmake -S . -B build -G "Visual Studio 17 2022" -A x64

:: Build all plugins
cmake --build build --config Release -- /m

:: Or build a single plugin
cmake --build build --config Release --target VXDenoiser_VST3
```

Staging copies `.vst3` bundles into `Source/vxsuite/vst/` the same as macOS.

### Installing Built Plugins

After building, the staged `.vst3` bundles in `Source/vxsuite/vst/` are the files you copy into your DAW's VST3 location.

### Install on macOS

- Copy the desired `.vst3` bundles into `/Library/Audio/Plug-Ins/VST3/` for all users, or `~/Library/Audio/Plug-Ins/VST3/` for the current user only.
- If you distribute unsigned or ad-hoc signed builds, macOS may block them the first time. In that case:
  - move the plugin into the VST3 folder
  - rescan in your DAW
  - if macOS blocks the plugin, approve it in `System Settings -> Privacy & Security`
- Expect a little more friction for unsigned builds than for notarized ones. That is normal for this project's current distribution model.

### Install on Windows

- Copy the desired `.vst3` bundles into `C:\Program Files\Common Files\VST3\`.
- Rescan plugins in your DAW after copying.
- If a plugin fails to load on a fresh machine, install the Microsoft Visual C++ 2015-2022 Redistributable before debugging further.
- If you distribute builds in a `.zip`, Windows may mark the downloaded archive as coming from the internet. If users hit odd load/open issues, tell them to unblock the zip in file properties before extracting.

### Targets

| Target | Description |
|---|---|
| `VXDeepFilterNet_VST3` | DeepFilterNet isolation plugin |
| `VXDenoiser_VST3` | Denoiser plugin |
| `VXSubtract_VST3` | Subtract plugin |
| `VXDeverb_VST3` | Deverb plugin |
| `VXProximity_VST3` | Proximity plugin |
| `VXCleanup_VST3` | Cleanup plugin |
| `VXTone_VST3` | Tone shaper plugin |
| `VXFinish_VST3` | Finish plugin |
| `VXOptoComp_VST3` | Opto compressor plugin |
| `VXStudioAnalyser_VST3` | Chain analyser |

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
      cleanup/        VXCleanup processor + DSP
      tone/           VXTone processor
      finish/         VXFinish processor + DSP
      OptoComp/       VXOptoComp processor (uses finish DSP)
      analyser/       VXStudioAnalyser (pass-through, telemetry UI)
      polish/         Shared corrective DSP (used internally)
  dsp/                Shared subtractive DSP used by VXSubtract
tests/                Measurement and behaviour tests
cmake/                Project CMake helpers
docs/                 Framework and product reference
```

---

## 🚥 Status

VST3 on macOS — confirmed working. Windows build system is fully wired up but still needs more end-to-end host validation; see the Windows build section for caveats. All ten plugins build and stage on macOS. VXDeepFilterNet requires Rust and model files in `assets/` — all other plugins have no extra dependencies beyond CMake and a C++20 compiler.
