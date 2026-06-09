# VX Studio

Focused, realtime-safe audio processors for voice, vocal production, and fast stereo-mix cleanup.

VX Studio is an open-source collection of JUCE/VST3 audio effects built around a shared C++ framework, compact control surfaces, and product-specific DSP. Each plugin is meant to do one job clearly instead of acting like a broad channel strip.

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
| VXSpeechClarity | Focused speech-artifact cleanup | `Sibilance`, `Plosive`, `Breath`, `Click` | De-essing, pop control, breath cleanup, click repair |
| VXProximity | Directional proximity model with adaptive filtering | `Closer`, `Air`, `Mud` | Intimacy, warmth, fullness, boom control after cleanup |
| VXProximityClassic | Simplified two-control proximity simulator | `Closer`, `Air` | Warmth and intimacy without the full three-dial model |
| VXTone | Bass, mid, and treble shaping | `Bass`, `Treble`, `Mid` | Warmth, presence, brightness, tonal balance |
| VXToneRefine | Guided tonal correction | `Mud`, `Harshness`, `Smooth` | Boxiness, brittleness, transparent smoothing |
| VXFinish | Final polish and level control | `Finish`, `Body`, `Gain` | Compression, recovery lift, controlled loudness |
| VXOptoComp | Professional LA2A-style opto levelling | `Peak Red.`, `Body`, `Gain`, `Pro`, `Behavior`, `Stereo Link` | Smooth riding, gentle limiting, opto character, engineer-facing control |
| VXLeveler | Adaptive riding and programme levelling | `Level`, `Control` | Speech riding, long-form consistency |
| VXRebalance | Confidence-driven source-family rebalance | `Vocals`, `Drums`, `Bass`, `Guitar`, `Other`, `Strength` | Broad mix moves without stems |
| VXRepair | All-in-one guided voice repair | `Noise`, `Speech Clarity`, `Clicks`, `Reverb` | Automatic problem detection and one-step correction |
| VXStudioAnalyser | Chain-aware dry-vs-wet analyser | `Avg Time`, `Smoothing` | Inspecting stage impact and whole-chain tone |

---

## Versions

Framework and plugin DSP versions are tracked independently.

| Component | Version |
|---|---|
| VX Studio Framework | `0.2.1` |
| VXDeepFilterNet | `0.2.1` |
| VXDenoiser | `0.2.1` |
| VXSubtract | `0.2.1` |
| VXDeverb | `0.2.1` |
| VXSpeechClarity | `1.0.0` |
| VXProximity | `0.2.1` |
| VXProximityClassic | `0.1.0` |
| VXTone | `0.2.0` |
| VXToneRefine | `1.0.0` |
| VXFinish | `0.3.0` |
| VXOptoComp | `0.3.0` |
| VXLeveler | `0.2.0` |
| VXRebalance | `0.2.2` |
| VXRepair | `0.1.1` |
| VXStudioAnalyser | `0.2.1` |

---

## Current status

- `15` focused plugins are implemented and shipping in the shared VX Studio shell.
- VXSpeechClarity and VXToneRefine (formerly VXClarity and VXRefine) are live  -  Speech Clarity DSP is active across all four bands.
- VXProximityClassic is a new two-control simplified proximity model.
- VXRepair is a new all-in-one guided repair assistant combining noise, clicks, clarity, and deverb in a single analysed workflow.
- VXCleanup has been archived  -  replaced by VXSpeechClarity (speech artifacts) and VXToneRefine (tonal refinement).
- GR meters are now active on VXDenoiser and VXDeverb.
- VXDeepFilterNet now has an active Guard control with artifact-aware blending.
- VXSubtract now warns when a learned noise profile may be stale (> 20 min old).
- The full regression harness currently passes end-to-end.

Latest verification:

```text
cmake --build build --target VXStudioPluginRegressionTests --parallel
./build/VXStudioPluginRegressionTests
```

On the latest clean run, `VXStudioPluginRegressionTests` completed with exit code `0`.

---

## Recommended signal chain

VX Studio is designed around composability. When a recording has multiple problems, this order is usually the best starting point:

```text
VXDeepFilterNet / VXDenoiser / VXSubtract -> VXDeverb -> VXSpeechClarity -> VXProximity -> VXTone / VXToneRefine -> VXFinish / VXOptoComp -> VXStudioAnalyser
```

Why this order:

1. Remove noise first so later stages do not react to or enhance the noise floor.
2. Remove room tail before enhancement so proximity and tone moves do not lift reverberant smear.
3. Do corrective cleanup (sibilance, plosive, breath) before additive shaping.
4. Add closeness after cleanup.
5. Shape tone after proximity.
6. Finish or compress last.
7. Put VXStudioAnalyser at the end when you want to inspect the whole chain or an individual VX stage.

For a fully guided single-plugin workflow, use **VXRepair**  -  it analyses, selects, and applies noise, clarity, click, and reverb reduction automatically.

Example chains:

```text
Heavy street noise, reflective room:
  VXDeepFilterNet -> VXDeverb -> VXSpeechClarity -> VXFinish -> VXStudioAnalyser

HVAC noise, thin and distant vocal:
  VXSubtract -> VXDeverb -> VXSpeechClarity -> VXProximity -> VXTone -> VXFinish

Levelling and polish after a clean recording:
  VXToneRefine -> VXTone -> VXOptoComp

Quick single-plugin repair:
  VXRepair
```

Denoiser choice:

| Situation | Recommended |
|---|---|
| Heavy or non-stationary noise | VXDeepFilterNet |
| Steady broadband noise | VXDenoiser |
| Noise with a learnable fingerprint | VXSubtract |
| Both present | Use ML isolation first, then target the remaining steady bed |
| Full guided repair | VXRepair |

---

## Shared behaviours

### Selector behaviour

Most VX Studio products use the shared `Vocal / General` selector when the DSP truly benefits from different tuning.

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
- If the status bar shows a stale-profile warning, re-learn to re-capture the current noise floor.

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

Targeted speech-artifact cleanup for sibilance, plosives, breath noise, and clicks. It is the focused corrective stage for speech mechanics rather than broad tonal shaping. All four DSP bands are active.

How to use it:

- Raise `Sibilance` to soften harsh `/s/` and `/z/` bursts without dulling the whole take.
- Raise `Plosive` to reduce low-frequency consonant thumps from close mics.
- Raise `Breath` to pull back obvious inhalations and wind-like noise between phrases.
- Raise `Click` to catch mouth clicks, lip smacks, and other short impulse artifacts.

Example settings:

- Light de-essing only: `Sibilance 35%`, `Plosive 0%`, `Breath 0%`
- Close-mic spoken voice cleanup: `Sibilance 30%`, `Plosive 40%`, `Breath 25%`
- Podcast artifact control: `Sibilance 45%`, `Plosive 35%`, `Breath 30%`
- Impulse cleanup: `Click 30%`, with the other bands at `0%` unless needed

Practical scenarios:

- Speech tracks with sharp consonants, close-mic pops, and mouth noise
- Dialogue cleanup before tone shaping and final compression
- Mechanical voice cleanup when `VXTone` or `VXFinish` would be too broad

### VXProximity

Directional microphone proximity model with real-time spectral analysis and adaptive 4-stage cascaded filtering. Simulates the tonal character of moving a microphone closer to a source  -  adding weight, intimacy, and presence  -  without altering spatial location or introducing artifacts.

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

### VXProximityClassic

A simplified two-control proximity simulator. Adds low-end body and upper-presence shimmer without the full three-dial model. Use this when VXProximity's Mud control is more than the source needs.

How to use it:

- Raise `Closer` to add weight and intimacy.
- Use `Air` to prevent the sound becoming too thick or congested.
- Apply it after noise and room problems are under control.

Example settings:

- Thin distant voice: `Closer 65%`, `Air 45%`
- Subtle warmth lift: `Closer 40%`, `Air 35%`
- Spoken-word polish: `Closer 55%`, `Air 40%`

Practical scenarios:

- Phone or room mics that feel too far away
- Voice tracks that need warmth after cleanup
- Situations where the three-dial version is more than needed

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

### VXRepair

All-in-one guided voice repair assistant. Analyses the audio and automatically suggests which tools to enable and at what strength. Combines noise reduction, speech clarity cleanup, click repair, and dereverberation in a single workflow.

How to use it:

- Insert VXRepair and let it analyse for a few seconds. It detects problems and pre-sets each tool.
- Accept the suggestions, adjust each slider to taste, or toggle individual tools off.
- Use the `Noise`, `Speech Clarity`, `Clicks`, and `Reverb` sliders to scale each stage.

Example settings:

- Podcast with background hiss and slight room reverb: let Repair analyse and apply the relevant stages.
- Phone or camera speech with strong interference: Repair handles noise, clicks, clarity, and room reduction together.
- Mostly clean voice: Repair will leave inactive tools off and only apply what is needed.

Practical scenarios:

- Fast single-plugin repair when there is no time to build a chain manually
- First pass on unfamiliar material
- All-in-one correction for noisy remote recording

### VXStudioAnalyser

Chain-aware dry-vs-wet spectrum analyser for VX Studio. Insert it last to inspect either the whole chain or one specific VX stage at a time.

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
- `tools/reaper/generate_vx_reaper_presets.lua` regenerates both from the current VX Studio plugins inside REAPER.

Shared scenario names:

| Preset | Use case | Recommended chain |
|---|---|---|
| `Camera Review - Far Phone` | Slightly noisy review-to-camera audio from a phone a few meters from the presenter | `VXSubtract -> VXSpeechClarity -> VXDenoiser -> VXDeepFilterNet -> VXDeverb -> VXProximity -> VXTone -> VXOptoComp -> VXFinish` |
| `Live Music - Front Of Room` | Single-point live music or rehearsal capture where preserving the whole mix matters more than voice isolation | `VXToneRefine -> VXTone -> VXOptoComp -> VXFinish` |
| `Podcast Finishing - Clean Voice` | Already-decent spoken-word capture that mainly needs polish and density | `VXSpeechClarity -> VXProximity -> VXTone -> VXOptoComp -> VXFinish` |
| `Mixed Audio - Voice + Guitar` | One track containing both voice and live instrument, where aggressive speech-only denoise would damage the instrument | `VXToneRefine -> VXTone -> VXOptoComp -> VXFinish` |

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
cmake --build build --parallel
```

Build a single plugin:

```bash
cmake --build build --target VXRebalancePlugin --parallel
```

Built `.vst3` bundles are staged into `Source/vxstudio/vst/`.

VXDeepFilterNet also requires model files in `assets/deepfilternet/models/`. Without them the plugin still builds, but no model will be available at runtime.

### Windows

Windows support is wired up and the GitHub Actions release workflow now builds and publishes Windows assets on hosted runners, but broader host validation is still pending.

Prerequisites:

- Visual Studio 2022 with Desktop development with C++
- CMake 3.20+
- Rust with the MSVC target

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --parallel
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
| `VXProximityClassic_VST3` | ProximityClassic plugin |
| `VXClarity_VST3` | Speech Clarity plugin |
| `VXTone_VST3` | Tone plugin |
| `VXRefine_VST3` | ToneRefine plugin |
| `VXFinish_VST3` | Finish plugin |
| `VXOptoComp_VST3` | Opto compressor plugin |
| `VXLeveler_VST3` | Leveler plugin |
| `VXRebalance_VST3` | Rebalance plugin |
| `VXRepair_VST3` | Repair plugin |
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
      proximityClassic/ VXProximityClassic processor and DSP
      speech_clarity/ VXSpeechClarity processor and DSP (built as VXClarity)
      tone/           VXTone processor
      tone_refine/    VXToneRefine processor and DSP (built as VXRefine)
      finish/         VXFinish processor and DSP
      OptoComp/       VXOptoComp processor
      leveler/        VXLeveler processor and DSP
      rebalance/      VXRebalance processor, DSP, and diagnostics UI
      repair/         VXRepair processor (embeds denoiser/deverb/speech_clarity DSP)
      analyser/       VXStudioAnalyser processor and custom analyser UI
      cleanup/        VXCleanup  -  archived, sources kept for reference
tests/                Measurement and behaviour tests
tools/                Utility scripts and fixture builders
assets/               Models, REAPER presets, and related resources
docs/                 Framework and product reference
tasks/                Working plans, reports, and lessons
```

---

## Status

- macOS VST3 builds are confirmed and staged.
- Windows build generation is present and the release workflow can publish Windows assets without a local Windows machine, but broader end-to-end host validation is still pending.
- All 15 plugins build on macOS from the current tree.
- VXDeepFilterNet is the only plugin with extra runtime model dependencies.
- VXRepair embeds DSP from VXDenoiser, VXDeverb, and VXSpeechClarity  -  modifying those DSP files requires re-testing VXRepair.
