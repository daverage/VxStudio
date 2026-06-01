# VX Suite

Focused, realtime-safe audio processors for voice, vocal production, and fast stereo-mix cleanup.

VX Suite is an open-source collection of JUCE/VST3 audio effects built around a shared C++ framework, compact control surfaces, and product-specific DSP. Each plugin is meant to do one job clearly instead of acting like a broad channel strip.

The current suite pass is complete: the core products, shared finishing framework, and analyser have all been retuned and reverified together. The most recent work focused on making top-of-range controls less polite, tightening source-local analyser behavior, removing pop-prone adaptive retuning in `VXProximity`, and keeping the full regression harness green.

The shared framework lives in `Source/vxstudio/framework/`. It handles parameter registration, the default editor shell, smoothing, status/help UI, listen-mode plumbing, and output safety so each product can stay focused on its DSP contract. See `Source/vxstudio/framework/README.md` for framework-level guidance.

This README and the in-plugin Help popup are a shared documentation contract. When a plugin's UI, selector behavior, DSP contract, or recommended usage changes, update both together.

---

## Plugins at a glance

| Plugin | Job | Main controls | Best for |
|---|---|---|---|
| VXDeepFilterNet | ML-powered voice isolation | `Clean`, `Guard` | Heavy noise, traffic, complex non-steady interference |
| VXDenoiser | Broadband denoise | `Clean`, `Guard` | Hiss, fan noise, HVAC, room tone |
| VXSubtract | Profile-guided subtractive denoise | `Subtract`, `Protect`, `Learn` | Learnable noise beds, hum, machines |
| VXDeverb | LRSV dereverberation with RT60 tracking | `Reduce`, `Blend` | Echoey rooms, distant speech, reverberant dialogue |
| VXSpeechClarity | Focused speech-artifact cleanup | `Sibilance`, `Plosive`, `Breath` | De-essing, pop control, breath cleanup |
| VXProximity | Directional proximity model with adaptive filtering | `Closer`, `Air`, `Mud` | Intimacy, warmth, fullness, boom control after cleanup |
| VXCleanup | Corrective voice cleanup | `Cleanup`, `Body`, `Focus` | Mud, harshness, breaths, plosives, sibilance |
| VXTone | Bass, mid, and treble shaping | `Bass`, `Treble`, `Mid` | Warmth, presence, brightness, tonal balance |
| VXToneRefine | Guided tonal correction | `Mud`, `Harshness`, `Smooth` | Boxiness, brittleness, transparent smoothing |
| VXFinish | Final polish and level control | `Finish`, `Body`, `Gain` | Compression, recovery lift, controlled loudness |
| VXOptoComp | Professional LA2A-style opto levelling | `Peak Red.`, `Body`, `Gain`, `Pro`, `Behavior`, `Stereo Link` | Smooth riding, gentle limiting, opto character, engineer-facing control |
| VXLeveler | Adaptive riding and programme levelling | `Level`, `Control` | Speech riding, long-form consistency |
| VXRebalance | Confidence-driven source-family rebalance | `Vocals`, `Drums`, `Bass`, `Guitar`, `Other`, `Strength` | Broad mix moves without stems |
| VXStudioAnalyser | Chain-aware dry-vs-wet analyser | `Avg Time`, `Smoothing` | Inspecting stage impact and whole-chain tone |

---

## Versions

Framework and plugin DSP versions are tracked independently.

| Component | Version |
|---|---|
| VX Suite Framework | `0.2.0` |
| VXDeepFilterNet | `0.2.0` |
| VXDenoiser | `0.2.0` |
| VXSubtract | `0.2.0` |
| VXDeverb | `0.2.0` |
| VXProximity | `0.2.0` |
| VXCleanup | `0.2.0` |
| VXTone | `0.2.0` |
| VXFinish | `0.3.0` |
| VXOptoComp | `0.3.0` |
| VXLeveler | `0.2.0` |
| VXRebalance | `0.2.1` |
| VXStudioAnalyser | `0.2.0` |

---

## Current status

- `12` focused plugins are implemented and shipping in the shared VX Suite shell.
- The latest suite-wide upgrade pass is complete across `Rebalance`, `Leveler`, `Finish` / `OptoComp`, `Cleanup`, `Denoiser`, `Deverb`, `Subtract`, `Proximity`, `Tone`, `DeepFilterNet`, and `VXStudioAnalyser`.
- Strong-setting behavior has been retuned so extreme control travel is more decisive instead of being overly compensated back toward unity.
- The analyser is now explicitly track-local and ignores bypassed/non-live upstream stages.
- The full regression harness currently passes end-to-end.

Latest verification:

```text
cmake --build build --target VXStudioPluginRegressionTests -j4
./build/VXStudioPluginRegressionTests
```

On the latest clean run, `VXStudioPluginRegressionTests` completed with exit code `0`.

---

## Recommended signal chain

VX Suite is designed around composability. When a recording has multiple problems, this order is usually the best starting point:

```text
VXDeepFilterNet / VXDenoiser / VXSubtract -> VXDeverb -> VXCleanup -> VXProximity -> VXTone -> VXFinish / VXOptoComp -> VXStudioAnalyser
```

Why this order:

1. Remove noise first so later stages do not react to or enhance the noise floor.
2. Remove room tail before enhancement so proximity and tone moves do not lift reverberant smear.
3. Do corrective cleanup before additive shaping.
4. Add closeness after cleanup.
5. Shape tone after proximity.
6. Finish or compress last.
7. Put VXStudioAnalyser at the end when you want to inspect the whole chain or an individual VX stage.

Example chains:

```text
Heavy street noise, reflective room:
  VXDeepFilterNet -> VXDeverb -> VXCleanup -> VXFinish -> VXStudioAnalyser

HVAC noise, thin and distant vocal:
  VXSubtract -> VXDeverb -> VXCleanup -> VXProximity -> VXTone -> VXFinish

Levelling and polish after a clean recording:
  VXCleanup -> VXTone -> VXOptoComp
```

Denoiser choice:

| Situation | Recommended |
|---|---|
| Heavy or non-stationary noise | VXDeepFilterNet |
| Steady broadband noise | VXDenoiser |
| Noise with a learnable fingerprint | VXSubtract |
| Both present | Use ML isolation first, then target the remaining steady bed |

---

## Shared behaviours

### Selector behaviour

Most VX Suite products use the shared `Vocal / General` selector when the DSP truly benefits from different tuning.

- `Vocal` is speech-aware and more conservative around intelligibility.
- `General` allows broader full-range cleanup or shaping.

Important exceptions:

- `VXDeepFilterNet` uses the main selector as a model selector: `DeepFilterNet 3` or `DeepFilterNet 2`.
- `VXLeveler` uses `Vocal Rider` and `Mix Leveler`, plus an `Analysis` selector with `Realtime`, `Smart Realtime`, and `Offline`.
- `VXRebalance` does not use the shared Vocal/General selector. It uses `Recording Type` with `Studio`, `Live`, and `Phone / Rough`.
- `VXStudioAnalyser` is a custom analyser UI rather than a standard processing shell.

### Listen

All processing plugins support `Listen`, but the audition signal depends on the product role.

- Removal and corrective tools audition what they remove.
- Additive and finishing tools audition what they add.

This is useful for checking whether a processor is targeting the right material and whether a setting has become too aggressive.

### Shared voice analysis

The framework runs block-rate analysis and exposes:

- speech presence and stability
- directness versus late-tail likelihood
- transient and artifact risk
- a composite voice-protection recommendation

---

## Plugin details

### VXDeepFilterNet

ML-powered voice isolation for heavy or complex background noise. It is the strongest noise-removal tool in the suite when classic denoisers cannot separate the voice cleanly enough.

How to use it:

- Start with `Clean` around `55%` to `70%`.
- Use `Guard` to recover natural speech detail if the result starts to sound over-processed.
- Choose the model that behaves best on the material. `DeepFilterNet 3` is usually the first choice.

Example settings:

- Street or traffic noise: `Clean 75%`, `Guard 65%`
- Busy cafe or moving background: `Clean 65%`, `Guard 75%`
- Gentler isolation before other cleanup: `Clean 50%`, `Guard 80%`

Practical scenarios:

- Phone or camera speech recorded in public spaces
- Dialogue with mixed non-stationary interference
- First stage before deverb, cleanup, and finishing processors

### VXDenoiser

Broadband spectral denoiser for steady noise such as hiss, fans, HVAC, and room tone. It is designed to clean the bed without turning into a voice-isolation tool.

How to use it:

- Raise `Clean` until the steady noise floor drops to a useful level.
- If the voice loses harmonics or consonants, increase `Guard`.
- Use it early in the chain, before deverb and finishing.

Example settings:

- Light hiss: `Clean 40%`, `Guard 75%`
- Fan or HVAC: `Clean 60%`, `Guard 70%`
- Safety-first spoken voice: `Clean 50%`, `Guard 85%`

Practical scenarios:

- Podcast or narration with constant background air noise
- Camera audio with a steady room bed
- Follow-up cleanup after a stronger ML pass leaves residual steady noise

### VXSubtract

Profile-guided subtractive denoiser for noises with a learnable fingerprint. It goes further than a blind denoiser when you can capture representative noise safely.

How to use it:

- Enable `Learn` and play the noise by itself for about one to two seconds.
- Turn `Learn` off to lock the profile.
- Raise `Subtract` for more removal and raise `Protect` if the source becomes hollow or over-scooped.

Example settings:

- Machine or room noise with a clean profile: `Subtract 65%`, `Protect 80%`
- More aggressive learned subtraction: `Subtract 80%`, `Protect 70%`
- Delicate speech preservation: `Subtract 55%`, `Protect 88%`

Practical scenarios:

- Air conditioner, projector, or other repeatable tonal or broadband beds
- Noise-only intro or pause available for learning
- Pre-clean stage before deverb and tonal shaping

### VXDeverb

Research-grade dereverberation using LRSV (Late-Reverberant Spectral Variance) with RT60 room-decay estimation and optional WPE (Weighted Prediction Error) enhancement for voice. Reduces reverberant tail and ambience while preserving direct sound clarity.

**Advanced features:**
- Per-bin RT60-adaptive Wiener filtering (Habets et al. 2009)
- Frame-online WPE in voice mode for speech-specific enhancement
- Temporal smoothing to suppress musical artifacts
- Adaptive body restoration (prevents over-thinning)

How to use it:

- Increase `Reduce` until the room tail pulls back without making the source papery.
- Use `Blend` to restore low-body weight if the dereverb pass gets too lean.
- Place it before proximity, tone shaping, and final dynamics.

Example settings:

- Small reflective room: `Reduce 50%`, `Blend 40%`
- Distant voice in a live room: `Reduce 70%`, `Blend 35%`
- General ambience tidy-up: `Reduce 35%`, `Blend 50%`

Practical scenarios:

- Phone or camera speech recorded far from the source
- Dialogue in an untreated room
- Recovering clarity before cleanup and finishing

### VXSpeechClarity

Targeted speech-artifact cleanup for sibilance, plosives, and breath noise. It is the focused corrective stage for speech mechanics rather than broad tonal shaping.

How to use it:

- Raise `Sibilance` to soften harsh `/s/` and `/z/` bursts without dulling the whole take.
- Raise `Plosive` to reduce low-frequency consonant thumps from close mics.
- Raise `Breath` to pull back obvious inhalations and wind-like noise between phrases.

Example settings:

- Light de-essing only: `Sibilance 35%`, `Plosive 0%`, `Breath 0%`
- Close-mic spoken voice cleanup: `Sibilance 30%`, `Plosive 40%`, `Breath 25%`
- Podcast artifact control: `Sibilance 45%`, `Plosive 35%`, `Breath 30%`

Practical scenarios:

- Speech tracks with sharp consonants and close-mic pops
- Dialogue cleanup before tone shaping and final compression
- Mechanical voice cleanup when `VXTone` or `VXFinish` would be too broad

### VXProximity

Directional microphone proximity model with real-time spectral analysis and adaptive 4-stage cascaded filtering. Simulates the tonal character of moving a microphone closer to a source—adding weight, intimacy, and presence—without altering spatial location or introducing artifacts.

**Advanced features:**
- Per-band energy tracking with adaptive filter parameter selection
- Context-aware modulation from voice analysis (transient risk, intelligibility)
- Physically plausible cascaded biquad approach (models real microphone impedance behavior)
- Non-linear control curves for natural proximity sensation
- Zero-latency pure IIR filtering
- Boom compensation control for different source characters

How to use it:

- Raise `Closer` to add weight and intimacy.
- Use `Air` to stop the sound becoming overly thick or shut in.
- Use `Mud` to balance bass depth versus low-mid boom, based on the source character.
- Apply it after noise and room problems are already under control.

Example settings:

- Thin distant voice: `Closer 65%`, `Air 45%`, `Mud 50%`
- Warm spoken-word polish: `Closer 55%`, `Air 40%`, `Mud 55%`
- Subtle intimacy lift: `Closer 45%`, `Air 50%`, `Mud 48%`

Practical scenarios:

- Phone or room mics that feel too far away
- Voice tracks that need warmth after cleanup
- Pre-tone-shaping enhancement before `VXTone`

### VXCleanup

Dual-stage spectral corrective processor with real-time multi-feature detection (8+ acoustic properties per frame). Targets mud, harshness, breaths, plosives, sibilance with problem-specific algorithms rather than one-size-fits-all spectral subtraction. Subtractive repair before enhancement.

**Advanced features:**
- Real-time spectral feature detection: flatness, harmonicity, high-frequency ratio, breath/sibilance/plosive envelopes
- Dual correction pipeline: aggressive corrective chain + broader persistent clarity stage
- Noise-floor integration from framework signal quality (voiceContext, separationConfidence)
- Voice-mode intelligibility protection with OutputTrimmer safety net

How to use it:

- Raise `Cleanup` until the distracting problems start to fall away.
- Increase `Body` if the result becomes too thin.
- Use `Focus` to steer the correction toward low-mid cleanup or more presence and air control.

Example settings:

- Muddy spoken voice: `Cleanup 55%`, `Body 55%`, `Focus 45%`
- Harsh, breathy voice: `Cleanup 60%`, `Body 50%`, `Focus 70%`
- Light corrective tidy-up: `Cleanup 35%`, `Body 55%`, `Focus 55%`

Practical scenarios:

- Dialogue that needs cleanup before any enhancement
- Speech with boxiness, spit, or low-end bumps
- Preparation stage before proximity, tone, or final compression

### VXTone

Bass, midrange, and treble shaping with mode-aware shelf and peak placement. It is the fast tonal balance stage after corrective cleanup.

How to use it:

- Start from the centre position and make small moves.
- Use `Bass` for weight and warmth, `Treble` for brightness and openness.
- Use `Mid` to shape the presence region: in Vocal mode at 2 kHz for speech intelligibility; in General mode at 1 kHz for warmth.
- Prefer subtle shaping after cleanup and proximity, not before.

Example settings:

- Need a little warmth: `Bass 58%`, `Treble 50%`, `Mid 50%`
- Dull voice lift: `Bass 50%`, `Treble 60%`, `Mid 55%`
- Balanced polish: `Bass 55%`, `Treble 56%`, `Mid 50%`

Practical scenarios:

- Final tonal balance after cleanup
- Correcting a track that feels thin, dull, or lacks presence
- Subtle pre-finish shaping before `VXFinish` or `VXOptoComp`

### VXToneRefine

Automatic tonal correction for mud, harshness, and general roughness. It is for targeted subtractive refinement when a track needs cleanup in specific problem regions without manual EQ moves.

How to use it:

- Raise `Mud` to reduce boxy low-mid buildup.
- Raise `Harshness` to soften brittle presence-region peaks.
- Raise `Smooth` to apply transparent broad tonal calming after the main problems are under control.

Example settings:

- Boxy room voice: `Mud 45%`, `Harshness 20%`, `Smooth 10%`
- Brittle spoken voice: `Mud 15%`, `Harshness 45%`, `Smooth 20%`
- General refinement pass: `Mud 25%`, `Harshness 30%`, `Smooth 25%`

Practical scenarios:

- Speech that feels muddy, brittle, or rough after basic cleanup
- Corrective refinement before finishing compression
- Faster guided tonal repair when manual EQ is too slow or fussy

### VXFinish

Final polish and level control after cleanup and tone work. It combines finish compression, bounded body recovery, makeup, and limiting for a more produced result.

How to use it:

- Raise `Finish` to increase compression, polish, and level control.
- Use `Body` to recover useful weight after cleanup.
- `Gain` is unity-centered: left is `50%`, centre is `100%`, right is `150%`.

Example settings:

- Light vocal polish: `Finish 35%`, `Body 55%`, `Gain 100%`
- Produced spoken voice: `Finish 60%`, `Body 58%`, `Gain 110%`
- Conservative final control after heavy cleanup: `Finish 45%`, `Body 52%`, `Gain 100%`

Practical scenarios:

- Last stage on cleaned speech
- Recovery and polish after corrective processing
- Fast final level shaping when you want more than a plain compressor

### VXOptoComp

Professional LA2A-style opto levelling and limiting with a visible `Pro` switch and slower, smoother programme-dependent gain reduction than `VXFinish`. It is for natural dynamic control with opto character when you want standalone engineer-facing control instead of guided finishing.

How to use it:

- Raise `Peak Red.` to drive more opto gain reduction.
- Use `Body` for light post-compressor weight shaping.
- `Gain` is unity-centered: left is `50%`, centre is `100%`, right is `150%`.
- Leave `Pro` off for the simple guided view: `Auto` behaviour and linked stereo tracking stay engaged.
- Turn `Pro` on to reveal `Behavior` and `Stereo Link`.
- `Behavior` on `Auto` follows the programme role. Use `Compress` for classic levelling and `Limit` for firmer containment.
- Keep `Stereo Link` high for classic linked stereo tracking, or lower it for dual-mono style response.

Example settings:

- Simple vocal levelling: `Peak Red. 35%`, `Body 52%`, `Gain 100%`, `Pro Off`
- Firm vocal compression: `Peak Red. 55%`, `Body 54%`, `Gain 108%`, `Pro On`, `Behavior Compress`, `Stereo Link 100%`
- Limiter-style bus control: `Peak Red. 65%`, `Body 50%`, `Gain 100%`, `Pro On`, `Behavior Limit`, `Stereo Link 100%`
- Asymmetric stereo material: `Peak Red. 45%`, `Body 50%`, `Gain 100%`, `Pro On`, `Behavior Compress`, `Stereo Link 0-25%`

Practical scenarios:

- Natural spoken-word levelling
- Opto-style smoothing after cleanup and tone shaping
- General dynamic control when `VXFinish` feels too guided or produced

### VXLeveler

Adaptive level control with two distinct behaviours: `Vocal Rider` for speech-focused riding and `Mix Leveler` for broader programme smoothing. It is meant to feel more like automatic fader support than static compression.

How to use it:

- Choose `Vocal Rider` when speech intelligibility is the priority.
- Choose `Mix Leveler` when you want gentler overall programme control.
- Use `Level` for how far the processor should even things out and `Control` for how assertively it reacts.

Example settings:

- `Vocal Rider` for uneven dialogue: `Level 65%`, `Control 60%`
- `Mix Leveler` for broad programme smoothing: `Level 50%`, `Control 45%`
- Heavier rider action: `Level 75%`, `Control 70%`

Practical scenarios:

- Speech riding in mixed or inconsistent recordings
- Programme smoothing before final finish or limiting
- Long-form content where sections vary in level too much

### VXRebalance

Confidence-driven source-family rebalance for full mixes. It estimates source ownership in time-frequency regions and lets you push or pull vocals, drums, bass, guitar, and residual content without stems.

How to use it:

- Choose the `Recording Type` that best matches the source: `Studio`, `Live`, or `Phone / Rough`.
- Start with small moves on the source lane you want to rebalance.
- Use `Strength` to scale the overall impact of all five source moves together.
- Treat it as perceptual source rebalance, not perfect stem extraction.

Example settings:

- Bring vocals forward slightly: `Vocals 60%`, `Strength 70%`
- Tuck a boomy rhythm section: `Bass 42%`, `Drums 45%`, `Strength 75%`
- Open a busy rehearsal mix: `Vocals 58%`, `Guitar 47%`, `Other 46%`, `Strength 65%`

Practical scenarios:

- Quick rebalance of a rough stereo mix
- Making speech or lead lines feel more present without remixing stems
- Light source-family shaping before final tone and dynamics

Notes:

- Current tuning work is aimed at making the render behave more like confident source allocation than EQ or weighted remix.
- Current debug builds can show a diagnostics panel for dominant-bin ownership, confidence, and `Other` leakage while tuning the DSP.

### VXStudioAnalyser

Chain-aware dry-vs-wet spectrum analyser for VX Suite. Insert it last to inspect either the whole chain or one specific VX stage at a time.

How to use it:

- Put the analyser at the end of the VX chain.
- Select `Full Chain` to compare chain input against final output.
- Click a stage in the left rail to inspect only that processor's dry-vs-wet spectrum.

Example settings:

- General readability: `Avg Time 500 ms`, `Smoothing 1/3 OCT`
- Fast transient inspection: `Avg Time 125 ms`, `Smoothing 1/12 OCT`
- Broad tonal overview: `Avg Time 1000 ms`, `Smoothing 1 OCT`

Practical scenarios:

- Checking what one plugin in the chain is really changing
- Comparing whole-chain tone before and after processing
- Debugging over-bright, over-thin, or over-damped processing decisions

---

## REAPER preset pack

The repo includes a REAPER-facing preset pack under `assets/reaper/`.

- `assets/reaper/RPL Files/` contains one `.RPL` library per VX effect.
- `assets/reaper/FX Chains/` contains full `.RfxChain` starting chains for shared scenarios.
- `tools/reaper/generate_vx_reaper_presets.lua` regenerates both from the current VX Suite plugins inside REAPER.

Shared scenario names:

| Preset | Use case | Recommended chain |
|---|---|---|
| `Camera Review - Far Phone` | Slightly noisy review-to-camera audio from a phone a few meters from the presenter | `VXSubtract -> VXCleanup -> VXDenoiser -> VXDeepFilterNet -> VXDeverb -> VXProximity -> VXTone -> VXOptoComp -> VXFinish` |
| `Live Music - Front Of Room` | Single-point live music or rehearsal capture where preserving the whole mix matters more than voice isolation | `VXCleanup -> VXTone -> VXOptoComp -> VXFinish` |
| `Podcast Finishing - Clean Voice` | Already-decent spoken-word capture that mainly needs polish and density | `VXCleanup -> VXProximity -> VXTone -> VXOptoComp -> VXFinish` |
| `Mixed Audio - Voice + Guitar` | One track containing both voice and live instrument, where aggressive speech-only denoise would damage the instrument | `VXCleanup -> VXTone -> VXOptoComp -> VXFinish` |

---

## Build

The project uses CMake and JUCE. A JUCE submodule is included.

### macOS

Prerequisites:

- Xcode Command Line Tools
- CMake 3.20+
- Rust, required only for VXDeepFilterNet

```bash
git clone --recurse-submodules <repo-url>
cd VxStudio
cmake -S . -B build
cmake --build build -j4
```

Build a single plugin:

```bash
cmake --build build --target VXRebalancePlugin -j4
```

Built `.vst3` bundles are staged into `Source/vxstudio/vst/`.

VXDeepFilterNet also requires model files in `assets/deepfilternet/models/`. Without them the plugin still builds, but no model will be available at runtime.

### Windows

Windows support is wired up but still needs broader host validation.

Prerequisites:

- Visual Studio 2022 with Desktop development with C++
- CMake 3.20+
- Rust with the MSVC target

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build -j4
```

### Install built plugins

macOS:

- Copy the desired `.vst3` bundles into `/Library/Audio/Plug-Ins/VST3/` for all users, or `~/Library/Audio/Plug-Ins/VST3/` for the current user.
- Rescan plugins in the DAW after copying.

Windows:

- Copy the desired `.vst3` bundles into `C:\Program Files\Common Files\VST3\`.
- Rescan plugins in the DAW after copying.

Useful plugin targets:

| Target | Description |
|---|---|
| `VXDeepFilterNet_VST3` | DeepFilterNet isolation plugin |
| `VXDenoiser_VST3` | Denoiser plugin |
| `VXSubtract_VST3` | Subtract plugin |
| `VXDeverb_VST3` | Deverb plugin |
| `VXProximity_VST3` | Proximity plugin |
| `VXCleanup_VST3` | Cleanup plugin |
| `VXTone_VST3` | Tone plugin |
| `VXFinish_VST3` | Finish plugin |
| `VXOptoComp_VST3` | Opto compressor plugin |
| `VXLeveler_VST3` | Leveler plugin |
| `VXRebalance_VST3` | Rebalance plugin |
| `VXStudioAnalyser_VST3` | Studio analyser plugin |

---

## Repository layout

```text
Source/
  vxstudio/
    framework/        Shared processor, editor, parameters, help, analysis, safety
    products/
      deepfilternet/  VXDeepFilterNet processor and ML service
      denoiser/       VXDenoiser processor and DSP
      subtract/       VXSubtract processor and DSP
      deverb/         VXDeverb processor and DSP
      proximity/      VXProximity processor and DSP
      cleanup/        VXCleanup processor and DSP
      tone/           VXTone processor
      finish/         VXFinish processor and DSP
      OptoComp/       VXOptoComp processor
      leveler/        VXLeveler processor and DSP
      rebalance/      VXRebalance processor, DSP, and diagnostics UI
      analyser/       VXStudioAnalyser processor and custom analyser UI
tests/                Measurement and behaviour tests
tools/                Utility scripts and fixture builders
assets/               Models, REAPER presets, and related resources
docs/                 Framework and product reference
tasks/                Working plans, reports, and lessons
```

---

## Status

- macOS VST3 builds are confirmed and staged.
- Windows build generation is present but needs broader end-to-end host validation.
- All 12 plugins build on macOS from the current tree.
- VXDeepFilterNet is the only plugin with extra runtime model dependencies.
- VXRebalance is under active tuning and now includes a compact diagnostics panel in the current debug-oriented editor path.
