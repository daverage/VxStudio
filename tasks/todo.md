# VX Spectrum scope review — 2026-03-17

## Problem
Assess whether VX Suite can support a standalone realtime analyzer that shows the dry spectrum, final wet spectrum, and per-plugin spectral influence for VX-family processors in one place, with optional waveform views and per-plugin visibility toggles, while keeping all analysis outside the critical audio path.

## Plan
- [x] Review VX Suite framework rules, research notes, and project lessons related to analysis, FFT ownership, and realtime/UI separation.
- [x] Inspect the current framework and processor architecture for shared FFT, stage-chain, and chain-visibility hooks.
- [x] Write a feasibility recommendation that distinguishes what is possible inside one plugin instance versus what requires explicit cross-plugin telemetry.

## Review
- The idea is technically possible, but not as a normal passive VST that can automatically "see" sibling VX plugins in an arbitrary DAW chain. This repo currently ships independent plugins, not a shared host graph, so a standalone analyzer would need an explicit telemetry channel from each participating VX plugin.
- The existing framework is a good base for this because FFT ownership is already centralized under `Source/vxsuite/framework/`, and the `StageChain` / processor-base pattern gives a clean place to add optional stage taps without turning each product into a one-off analyzer.
- To stay aligned with the VX Suite rules, this should not become a sprawling diagnostic panel inside every shipping product. The cleaner fit is a separate analyzer product or debug/companion product that consumes lightweight shared analysis snapshots.
- The safest architecture is: each VX-family plugin publishes downsampled, latency-aware analysis snapshots from lock-free preallocated buffers; the analyzer plugin subscribes and renders dry/wet/final overlays plus per-plugin traces and visibility toggles entirely on the UI/message side.
- Waveform overlays are also possible, but they should likely be a secondary tab or switchable layer. Trying to show dense multitrace spectrum plus waveform in one view would fight the suite's simplicity goals and reduce readability.

# VX Cleanup architecture review — 2026-03-17

## Problem
Review the current `VXCleanup` processor against the supplied classifier-focused design document, identify where the implementation is already strong, where event decisions are still coupled, and call out any realtime/smoothing/state concerns before refactoring.

## Plan
- [x] Read the current `VXCleanup` processor implementation and shared VX Suite constraints.
- [x] Map the current analysis/data flow from input to `polishChain.processCorrective()`.
- [x] Identify where breath, sibilance, plosive, and tonal decisions are currently coupled.
- [x] Check realtime-safety, parameter smoothing, and persistent-state handling in the current implementation.
- [x] Write a review summary with file/line references and note the most suitable refactor direction.

## Review
- The processor already follows a clean analysis-to-mapping-to-DSP flow: control smoothing, voice snapshot capture, tonal analysis, derived ratios/risk metrics, and a final `polish::Dsp::Params` handoff in one place.
- The strongest existing design choices are block-rate smoothing for the three user controls, reuse of shared voice-analysis context, and preserving a simple VX Suite front-end while keeping corrective mapping internal.
- The main architectural gap is classifier coupling: `lowTrouble`, `highTrouble`, and `cleanupDrive` currently act as shared upstream drivers for multiple downstream actions, so de-breath, de-ess, plosive control, and broad HF smoothing are not independently classified.
- The most important concrete coupling today is that `params.deEss`, `params.breath`, and `params.troubleSmooth` all derive significant authority from `highTrouble`, while `params.plosive` depends on `cleanupDrive`, which itself rises with either low- or high-band trouble.
- Realtime behavior looks disciplined in this file: no heap work in `processProduct`, `ScopedNoDenormals` is present, smoothing is block-based, and long-lived state is limited to the expected smoothed controls plus tonal-analysis state.
- The most meaningful next refactor would be to keep the current top-level processor shape but split Stage 2 into explicit per-event classifier signals (`breathness`, `sibilance`, `plosiveRisk`, `tonalMud`) before mapping them independently into `polish::Dsp::Params`.

# Shelf toggles + listen audit — 2026-03-16

## Problem
1. Polish shelf icons (low/high) are purely decorative — no click, no DSP effect.
2. Verify listen toggle is visible for all 4 products (denoiser, deverb, polish, proximity).

## Listen audit result
All 4 products set `listenParamId = "listen"` → framework shows button. ✓

## Plan — shelf toggles

### 1. `ProductIdentity` — add two optional param IDs
- `lowShelfParamId`  (empty = not used)
- `highShelfParamId` (empty = not used)

### 2. `createSimpleParameterLayout` — add bool params with default=true when set

### 3. `VxPolishProcessor.cpp`
- Set `identity.lowShelfParamId = "demud_on"`, `highShelfParamId = "deess_on"`
- In `processProduct()`: read flags; if `demud_on` false → `params.deMud = 0`; if `deess_on` false → `params.deEss = 0` (and zero `params.troubleSmooth` for high-shelf)

### 4. `EditorBase`
- In `paint()`: dim shelf icon (alpha 0.25) when its param is false
- Override `mouseDown()`: if click hits `lowShelfIconBounds` or `highShelfIconBounds`, toggle the parameter

---

# Legacy denoise review — 2026-03-17

## Problem
Review legacy denoise-related code such as `HandmadePrimary` and `DenoiseEngine`, decide whether it should be folded into VX Suite DSP or removed, and identify worthwhile denoise options for the main denoiser.

## Plan
- [x] Trace which legacy DSP files are still compiled or referenced.
- [x] Compare `Source/dsp/HandmadePrimary.*` against `Source/vxsuite/products/denoiser/dsp/VxDenoiserDsp.*`.
- [x] Check whether the legacy feature set includes useful capabilities missing from `VXDenoiser`.
- [x] Write a keep / migrate / remove recommendation with concrete next steps.

## Review
- `Source/dsp/DenoiseEngine.h`, `Source/dsp/DenoiseOptions.h`, `Source/dsp/ProcessOptions.h`, and `Source/dsp/AudioProcessStage.h` are effectively duplicate compatibility wrappers. They are not directly built by CMake and only exist to support `HandmadePrimary`, so they should not become shared VX Suite framework surface area.
- `Source/dsp/HandmadePrimary.cpp` is still live because `VXSubtract` compiles it directly. Removing the legacy DSP folder today would break `VXSubtract`.
- `HandmadePrimary` and `VxDenoiserDsp` share the same core blind denoise architecture: OM-LSA gain estimation, Martin minimum statistics, Bark transient protection, Bark masking floor, harmonic comb protection, LF temporal stability, and phase-coherent resynthesis.
- The genuinely unique legacy capability is learned-profile subtraction for `VXSubtract`: learn state, frozen profile persistence, confidence scoring, and subtract-specific gain shaping.
- Recommendation: do not merge all of `HandmadePrimary` into the main denoiser. Instead, move `HandmadePrimary` into `Source/vxsuite/products/subtract/dsp/` (or split out only reusable learned-profile helpers if a second product needs them) and retire the duplicate `Source/dsp/*` wrapper layer afterward.
- Recommendation: keep the shipping `VXDenoiser` focused on broadband denoise. Do not add product-specific subtract controls like profile learn, `subtract`, `sensitivity`, or `labRawMode` unless the denoiser product goal changes.
- Potential denoiser additions worth prototyping: optional fixed hum suppression for 50/60 Hz harmonics ahead of the denoiser sidechain, and an optional narrowband tonal/noise detector for whistle/whine cleanup. These are meaningfully different from the current broadband denoise path.
- Not recommended as separate denoiser options right now: "rumble" as a denoise feature, because that is better handled as simple high-pass or proximity/cleanup filtering; and "surgical denoise" as a UI promise, because the current two-knob VX Suite template does not support exposing detailed band editing honestly.

---

# Denoiser hum + narrowband — 2026-03-17

## Problem
Add useful mains hum and narrowband tonal cleanup to `VXDenoiser` without growing dead legacy code. Extract reusable spectral helpers instead of copying more `HandmadePrimary` logic into the suite path.

## Plan
- [x] Add or update task documentation and capture the user correction in `tasks/lessons.md`.
- [x] Extract shared spectral helper code from the duplicated denoise implementations.
- [x] Add hum and narrowband suppression to `VxDenoiserDsp` while preserving the two-knob product shape.
- [x] Reuse the extracted helper from `HandmadePrimary` so the legacy path sheds duplicated logic.
- [x] Build a focused target to verify the denoiser still compiles cleanly.

## Review
- Added shared spectral math in `Source/vxsuite/framework/VxSuiteSpectralHelpers.h` and reused it from both `VxDenoiserDsp` and `HandmadePrimary`, so the new denoiser work did not introduce another copy of Bark conversion / tonalness / phase-wrap logic.
- Added two internal cleanup stages to `VxDenoiserDsp`: mains-hum suppression that auto-tracks the stronger 50 Hz or 60 Hz harmonic family, and narrowband tonal suppression for steady whistle/whine-style peaks.
- Kept the existing two-knob denoiser product surface intact. The new stages are internal and respect the existing `Clean`, `Guard`, and mode shaping rather than adding more front-panel controls.
- Verified by building `VXDenoiser` and `VXSubtract` successfully with `cmake --build build --target VXDenoiser VXSubtract -j4`.

---

# VxDeepFilterNet — 2026-03-17

## Problem
Create a new VX Suite plugin called `VxDeepFilterNet` that uses the existing realtime DeepFilterNet work as a voice-only denoiser, keeps the standard two-knob UX, replaces the Vocal/General dropdown with `DeepFilterNet 2` / `DeepFilterNet 3`, and stays as realtime-safe and efficient as possible.

## Plan
- [x] Inspect the reusable realtime DeepFilter code in `VxCleaner` and map the minimal subset needed in `VxStudio`.
- [x] Generalize the framework's existing two-choice selector so products can supply custom labels instead of only Vocal/General.
- [x] Add a new `VxDeepFilterNet` product with a product-local realtime DeepFilter engine wrapper, two main knobs, listen mode, and DFN2/DFN3 selection.
- [x] Add CMake and dependency wiring for ONNX Runtime in this repo, with a safe compiled fallback when ORT or model assets are unavailable.
- [x] Build the new target and fix integration issues until it compiles cleanly.

## Review
- Added a new VX Suite product at `Source/vxsuite/products/deepfilternet/` with the standard two-knob / listen UX, but with a custom `Model` selector that exposes `DeepFilterNet 3` and `DeepFilterNet 2` instead of Vocal/General.
- Generalized the shared framework selector surface so products can provide custom labels and choice text without forking the editor or parameter layout.
- Imported the realtime DeepFilter service into this repo as product-local code and renamed it into a VX Suite-owned namespace so the new product no longer links back to sibling-repo code.
- Added local DeepFilter model assets under `assets/deepfilternet/models/` and bundled them into the VST3 resources, so the plugin owns its runtime assets inside `VxStudio`.
- Added ONNX Runtime detection and conditional build wiring in `CMakeLists.txt`, with a compiled fallback that still builds the plugin when ORT is unavailable.
- Verified by running `cmake -S . -B build`, `cmake --build build --target VXDeepFilterNet -j4`, `cmake --build build --target VXDeepFilterNet_VST3 -j4`, and `cmake --build build --target VXDenoiser VXSubtract -j4`.
- Current limitation: the realtime engine can run the bundled monolithic ONNX path today, which makes `DeepFilterNet 3` the active realtime option on this machine. The bundled `dfn2/` and `dfn3/` official model directories are packaged locally, but a dedicated multi-graph realtime backend is still needed before the selector can drive those bundle variants directly.

## Follow-up Plan — realtime DFN2/DFN3 backend
- [ ] Inspect the bundled `dfn2/` and `dfn3/` graph I/O so the runtime can wire encoder and decoder sessions correctly.
- [ ] Add a shared prepared realtime bundle backend that keeps persistent ONNX sessions and fixed-shape state buffers for `enc.onnx`, `erb_dec.onnx`, and `df_dec.onnx`.
- [ ] Route the `Model` selector to true realtime `DFN2` and `DFN3` execution instead of the current bundle-only status fallback.
- [ ] Reduce hot-path overhead by moving tensor metadata discovery and allocations into `prepareRealtime()`.
- [ ] Rebuild `VXDeepFilterNet` and `VXDeepFilterNet_VST3`, then confirm the default repo build still succeeds.

---

# VxDeepFilterNet runtime cleanup — 2026-03-17

## Problem
Replace the experimental ONNX/bundle backend with a fully local, production-shaped DeepFilter runtime path inspired by Alt-Denoiser, while keeping the implementation owned inside `VxStudio`, realtime-safe, and free from dead legacy layers.

## Plan
- [x] Re-pin the vendored `libDF` crate to a known-good upstream dependency graph and remove unneeded default-model embedding.
- [x] Clean up the `VxDeepFilterNet` C++ bridge so it targets the local runtime only, with fixed 48 kHz processing and local model extraction.
- [x] Verify `VXDeepFilterNet` builds cleanly, then run the normal repo build to ensure the product is part of the global path.
- [x] Add review notes covering the final architecture, remaining runtime limits, and follow-up work.

## Review
- `VxDeepFilterNet` now uses a fully local vendored DeepFilter runtime path: the plugin shell stays in VX Suite, the realtime engine runs fixed-rate 48 kHz processing through vendored `libDF`, and model assets remain owned by `VxStudio` under `assets/deepfilternet/models/`.
- The vendored Rust crate is pinned to the compatible `tract 0.21.4` stack and built with `--locked --no-default-features`, which avoids the broken dependency drift and removes the upstream default-model embed that was conflicting with our local asset ownership.
- Cargo output for the vendored runtime now goes into the normal CMake build tree instead of `ThirdParty/deep_filter/libDF/target/`, so the imported source stays source-only and the repo no longer accumulates generated runtime artifacts.
- The C++ service no longer depends on the old custom ONNX bundle path. It now extracts the selected model tarball locally, instantiates per-channel `libDF` state, resamples to 48 kHz with the vendored `Resampler`, and returns to host rate in the standard VX Suite processor flow.
- Verified with `cargo build --release --locked --no-default-features --manifest-path ThirdParty/deep_filter/libDF/Cargo.toml --features capi`, `cmake --build build --target VXDeepFilterNet -j4`, `cmake --build build --target VXDeepFilterNet_VST3 -j4`, and `cmake --build build -j4`.
- Remaining product caveat: the current runtime bridge is optimized around the official monophonic C API path, so `VxDeepFilterNet` is now cleanly realtime and locally owned, but deeper DFN2/DFN3 model-specific optimization work is still future tuning rather than unfinished architecture.

---

# VxDeepFilterNet DFN2 compatibility — 2026-03-17

## Problem
The modern vendored `libDF` runtime runs `DFN3`, but `DFN2` still crashes during model initialization even with the official `v0.3.1` model tarball. The product needs a real `DFN2` mode instead of a broken selector entry.

## Plan
- [x] Reproduce the `DFN2` initialization failure directly outside the plugin shell.
- [x] Vendor a `DFN2`-compatible upstream runtime locally and expose a distinct C API so it can coexist with the modern `DFN3` runtime.
- [x] Route `VxDeepFilterNet` to the correct runtime per model variant and keep both runtime build outputs out of the source tree.
- [x] Verify direct `DFN2` initialization plus `VXDeepFilterNet` and full-repo builds.

## Review
- Confirmed that the current vendored runtime still panics on `DeepFilterNet2_onnx.tar.gz`, so this was a real runtime compatibility issue rather than a UI or model-packaging bug.
- Added a second local vendored runtime under `ThirdParty/deep_filter_dfn2/` based on upstream `v0.3.1`, with a small local C shim exposing `df2_*` symbols so it can link alongside the current `df_*` runtime cleanly.
- Updated `VxDeepFilterNetService` to choose the runtime by model variant: `DFN3` uses the modern vendored runtime, `DFN2` uses the compatibility runtime, and both continue to share the same VX Suite processor shell and local resampling path.
- Moved both Rust runtime builds into `build/cargo/` so neither vendored source tree accumulates generated `target/` artifacts.
- Verified direct `DFN2` initialization with a local probe against `build/cargo/libdf_dfn2/release/libdf.a`, which returned the expected `480` frame length for `assets/deepfilternet/models/DeepFilterNet2_onnx.tar.gz`.
- Verified with `cmake --build build --target VXDeepFilterNet -j4` and `cmake --build build -j4` after wiring both runtimes into the main build.

---

# VXPolish tuning + slope info — 2026-03-17

## Problem
Make `VXPolish` more audible in practice, especially the compressor and limiter, show a clear info-line message when the slope icons are clicked, add a vocal-mode breath remover, and keep plosive/de-ess/breath cleanup more restrained in general mode.

## Plan
- [x] Inspect `VXPolish` DSP and editor code to locate dynamic thresholds, mode shaping, and slope click handling.
- [x] Retune `VXPolish` compression and limiting so the dynamics stages engage more readily.
- [x] Add an internal breath cleanup stage that is strongest in vocal mode and reduced in general mode.
- [x] Surface slope click feedback in the editor info/status area and verify the repo still builds cleanly.

## Review
- Increased the effective compressor and limiter drive by raising the processor-side control mapping and tightening the DSP-side thresholds, ratio, attack/release, and limiter ceiling so `Comp` and `Limit` are noticeably easier to trigger.
- Added an internal breath cleanup stage in `VxPolishDsp` that targets low-level airy high-band events; it is intentionally stronger in vocal mode and much lighter in general mode.
- Rebalanced cleanup strength so vocal mode remains the more assertive speech-focused setting, while general mode now backs off plosive, de-ess, and breath reduction to preserve wider-band material more naturally.
- Updated the shared editor so clicking the low/high slope icons now writes a short explanatory message into the existing top-right info/status line instead of silently toggling.
- Verified with `cmake --build build -j4`, which rebuilt `VXPolish`, `VXPolish_VST3`, and the full suite successfully.

---

# VXPolish smart gain + DFN2 LL — 2026-03-17

## Problem
Add a fourth `VXPolish` dial for smarter mode-aware gain lifting, surface a de-breath activity light, make demud/smoothing more obvious, and reduce the `DFN2` echo issue without harming the `DFN3` path.

## Plan
- [x] Extend the shared framework so a product can expose an optional fourth dial cleanly.
- [x] Wire `VXPolish` to use that fourth dial as a smart gain control that pushes the selective auto-gain/recovery path rather than plain broadband gain.
- [x] Add the de-breath LED and strengthen audible demud/intelligent smoothing behavior.
- [x] Switch `DFN2` to the low-latency export and verify the suite still builds cleanly.

## Review
- Extended the shared VX Suite framework with an optional quaternary control, including parameter layout, attachments, and 4-knob editor layout support.
- `VXPolish` now exposes a fourth `Gain` dial that increases the selective mode-aware recovery/auto-gain path rather than simply boosting overall level; vocal mode leans more into presence/air lift, while general mode leans relatively more into body/presence recovery.
- Added a `De-breath` activity light to `VXPolish` and kept the existing `De-ess`, `Plosive`, `Comp`, and `Limit` indicators.
- Increased `deMud` and `troubleSmooth` drive in the processor mapping so mud cleanup and intelligent smoothing are more noticeable in practice.
- Switched `DFN2` model selection to prefer `DeepFilterNet2_onnx_ll.tar.gz`, which avoids the older lookahead-offset path that was producing the echo-like artifact; `DFN3` remains on the standard current runtime/model path.
- Verified with `cmake -S . -B build` and `cmake --build build -j4`.

---

# VXSubtract learn/remove overhaul — 2026-03-17

## Problem
`VXSubtract` does not behave like a clear Audacity-style "learn profile, then remove profile" tool. Learn is destructive and auto-stops on hidden silence detection, while the subtract path is conservative enough that users can complete a learn and still hear too little removal.

## Plan
- [x] Make Learn non-destructive and explicit: keep the previous learned profile active until a new learn is completed successfully, and remove the hidden silence auto-stop behavior.
- [x] Rebalance `Subtract` so the learned profile removal is more audible, with `General` mode removing more aggressively and `Vocal` mode preserving speech/harmonics more intentionally.
- [x] Update status text and related processor state so the learn/remove workflow is understandable from the UI.
- [x] Build `VXSubtract` and the full repo, then document the final behavior and verification result.

## Review
- `VXSubtract` now behaves like an explicit profile-capture tool: `Learn` starts capturing noise and only locks the replacement profile when the user turns `Learn` off. The old profile stays intact during capture instead of being destroyed at learn start.
- Removed the hidden silence-driven auto-stop behavior in the processor, so learn no longer ends unexpectedly on short gaps or room-tone dips.
- Reworded the product status text so the UI now explains the intended flow: capture noise, click again to lock, then use `Subtract` to remove the learned profile.
- Rebalanced the learned subtract path in `HandmadePrimary` so profile removal is audibly stronger overall, with lower subtract floors and higher subtraction authority than before.
- Split the mode behavior more intentionally: `Vocal` now keeps stronger protection and a slightly softer subtract curve, while `General` applies a more aggressive learned-profile removal with much lighter protection throttling.

---

# Learn / Deverb / Cleanup / Finish bug-fix pass — 2026-03-17

## Problem
- `VXSubtract` Learn button does not reliably start learning.
- `VXDeverb` can produce humming / buzzing at extreme `Blend`.
- `VXCleanup` can clip and its EQ-style cleanup moves feel too subtle.
- `VXFinish` can clip, and `Finish` / `Body` / `Gain` are too weak in practice.

## Plan
- [x] Fix the Learn toggle edge handling and add regression coverage.
- [x] Stabilize `VXDeverb` at extreme `Blend` / wet settings and add a focused regression test.
- [x] Make `VXCleanup` more audible while adding output headroom protection.
- [x] Make `VXFinish` more audible, separate loudness push from clipping, and strengthen limiting.
- [x] Build the affected targets, run the regression harness, and document the final result.

## Review
- Fixed the `VXSubtract` Learn bug in `Source/vxsuite/products/subtract/VxSubtractProcessor.cpp` by latching the current Learn parameter state on reset instead of hard-forcing the toggle state, so the first press after prepare/reset now starts capture correctly.
- Hardened `VXDeverb` in `Source/vxsuite/products/deverb/VxDeverbProcessor.cpp` and `Source/vxsuite/products/deverb/dsp/VxDeverbSpectralProcessor.cpp` by reducing extreme oversubtraction/WPE drive, making body restore more conservative at high reduce settings, clamping unstable spectral values, and sanitizing the final output path so maxed settings no longer explode into buzzing or giant peaks.
- Retuned `VXCleanup` in `Source/vxsuite/products/cleanup/VxCleanupProcessor.cpp` so `deMud`, `deEss`, `breath`, `plosive`, and HF smoothing all have more audible authority, while a fast output trim guard now prevents the cleanup stage from clipping the result.
- Retuned `VXFinish` in `Source/vxsuite/products/finish/VxFinishProcessor.cpp` and `Source/vxsuite/products/finish/dsp/VxFinishDsp.cpp` so `Finish`, `Body`, and `Gain` move more clearly, target makeup is less reckless, the limiter reacts faster with an instantaneous peak guard, and the old hard-clipping behavior is replaced with a softer final safety path.
- Expanded the regression harness in `tests/VXSuitePluginRegressionTests.cpp` and `CMakeLists.txt` to cover first-press Learn startup, Deverb at extreme Blend settings, stronger Cleanup audibility without clipping, and stronger Finish control separation without clipping.
- Verified with `cmake --build build --target VXSuitePluginRegressionTests -j4`, `./build/VXSuitePluginRegressionTests`, and `cmake --build build -j4`.

### Learn UX follow-up
- Updated the shared Learn UI in `Source/vxsuite/framework/VxSuiteEditorBase.cpp` and the Subtract status copy in `Source/vxsuite/products/subtract/VxSubtractProcessor.cpp` to follow common capture UX patterns: explicit start/stop wording, guidance to capture pure background noise for roughly 1 to 2 seconds, and confidence text framed as noise-print quality rather than abstract certainty.
- Verified with `cmake --build build --target VXSubtract VXSubtract_VST3 VXSuitePluginRegressionTests -j4`.
- Increased the learn target window and reduced the variance padding applied when freezing the learned profile, making the result closer to an Audacity-style learned noise estimate instead of an overly padded conservative profile.
- Verified with `cmake --build build --target VXSubtract -j4` and `cmake --build build -j4`.

---

# Denoiser latency + DFN2 guard pass — 2026-03-17

## Problem
The standard denoiser feels echoey on the wet path, and `VXDeepFilterNet` `DFN2` becomes robotic when `Guard` is raised. Both issues point to protection/latency behavior that sounds wrong in realtime use.

## Plan
- [x] Inspect the standard denoiser latency path and reduce the perceived delay without breaking the realtime-safe design.
- [x] Rework `DFN2` guard handling so protection no longer relies on a strong dry reblend that causes combing/robotic tone.
- [x] Build the affected targets and the full repo.
- [x] Document the final behavior and verification result.

## Review
- Reduced the standard denoiser STFT size from `2048/512` to `1024/256`, cutting the algorithmic latency from about `32 ms` to about `16 ms` while keeping the same overlap/WOLA structure and realtime-safe processing model.
- This does not make the spectral denoiser zero-latency, but it materially reduces the delayed-wet feel that could present as an echo on monitored material.
- Reworked `VXDeepFilterNet` guard behavior so `DFN2` no longer uses post-model dry/wet recovery, which was causing comb-filter/robotic artifacts when protection was raised.

---

# Legacy DSP removal + migration — 2026-03-17

## Problem
Remove the remaining legacy `Source/dsp/` code by moving the still-live subtract implementation into the VX Suite subtract product, replacing wrapper-only legacy process option/stage headers with the shared framework equivalents, and deleting the obsolete legacy files.

## Plan
- [x] Read project lessons and VX Suite framework guidance relevant to cleanup/corrective products.
- [x] Audit remaining legacy DSP files and identify which ones are still live.
- [x] Confirm the wrapper-only legacy headers duplicate framework-owned concepts.
- [x] Move the live subtract DSP into `Source/vxsuite/products/subtract/` and update product wiring.
- [x] Delete obsolete legacy wrapper headers/files and verify the repo still builds.

## Review
- Moved the only still-live legacy DSP implementation out of `Source/dsp/` and into the subtract product itself:
  - `Source/vxsuite/products/subtract/dsp/VxSubtractDsp.h`
  - `Source/vxsuite/products/subtract/dsp/VxSubtractDsp.cpp`
- Updated `VXSubtract` to own that DSP locally instead of reaching back into a legacy folder. The processor now includes `dsp/VxSubtractDsp.h`, uses `vxsuite::subtract::SubtractDsp`, and passes the shared `vxsuite::ProcessOptions` directly.
- Removed the obsolete compatibility wrapper headers entirely:
  - `Source/dsp/AudioProcessStage.h`
  - `Source/dsp/ProcessOptions.h`
  - `Source/dsp/DenoiseEngine.h`
  - `Source/dsp/DenoiseOptions.h`
- The migration also eliminates the old `vxcleaner::dsp` namespace from active code. The subtract implementation now lives under the suite-owned `vxsuite::subtract` namespace and uses framework primitives (`VxSuiteAudioProcessStage`, `VxSuiteProcessOptions`, `VxSuiteFft`, `VxSuiteSpectralHelpers`) directly.
- Updated the build graph so `VXSubtract` now compiles `Source/vxsuite/products/subtract/dsp/VxSubtractDsp.cpp` instead of `Source/dsp/HandmadePrimary.cpp`.
- Verified with:
  - `cmake --build build --target VXSubtract -j4`
  - `cmake --build build -j4`

---

# VX Suite FFT centralization — 2026-03-17

## Problem
Active VX Suite spectral products still own `juce::dsp::FFT` locally, which duplicates a core realtime primitive across the framework-facing codebase. Centralize FFT ownership in `Source/vxsuite/framework/` and migrate active users to the shared abstraction, while noting any still-live legacy path that should follow the same contract.

## Plan
- [x] Audit active and still-live FFT ownership across the repo.
- [x] Capture the correction in `tasks/lessons.md`.
- [x] Add a shared framework FFT wrapper that centralizes JUCE FFT ownership and transform calls.
- [x] Migrate active VX Suite FFT owners to the shared wrapper, plus the still-live legacy `HandmadePrimary` path if it fits cleanly.
- [x] Build the affected targets and document the result.

## Review
- Centralized FFT ownership in the framework wrapper at `Source/vxsuite/framework/VxSuiteFft.h`, keeping the abstraction intentionally small: `prepare(order)`, `isReady()`, `size()`, `bins()`, `performForward()`, and `performInverse()`.
- Confirmed the active VX Suite FFT owners are the denoiser and deverb spectral paths, and migrated both to the shared wrapper:
  - `Source/vxsuite/products/denoiser/dsp/VxDenoiserDsp.h`
  - `Source/vxsuite/products/denoiser/dsp/VxDenoiserDsp.cpp`
  - `Source/vxsuite/products/deverb/dsp/VxDeverbSpectralProcessor.h`
  - `Source/vxsuite/products/deverb/dsp/VxDeverbSpectralProcessor.cpp`
- Also aligned the still-live legacy FFT owner behind `VXSubtract`:
  - `Source/dsp/HandmadePrimary.h`
  - `Source/dsp/HandmadePrimary.cpp`
- Kept the change scoped to FFT ownership and transform dispatch only. Window generation, overlap-add logic, frame history, and per-bin DSP state remain product-local, so the spectral algorithms themselves were not rewritten.
- Verified with `cmake --build build --target VXDeverb VXDenoiser VXSubtract -j4` and `cmake --build build -j4`.

---

# VXPolish split into Cleanup + Finish — 2026-03-17

## Problem
`VXPolish` had grown into two different jobs: corrective cleanup and finish/loudness shaping. The product now needs to be split into two clear plugins, with every remaining stage doing meaningful work, preserving the existing vocal/general behavior, and keeping any gain/recovery path noise-aware.

## Plan
- [x] Define the split clearly: `Cleanup` owns corrective removal, `Finish` owns recovery/dynamics/final level.
- [x] Add shared tonal-analysis helpers plus new `VXCleanup` and `VXFinish` processors wired to the existing DSP stages.
- [x] Replace `VXPolish` in the build with the two new plugin targets and stage the new bundles.
- [x] Tighten the de-breath detector so it follows real breath-like events instead of generic bright content, then build and verify the repo.

## Review
- Added `VXCleanup`, a corrective-only plugin built around the existing polish DSP's subtractive stage. It owns `HPF`, `De-mud`, `Trouble smoothing`, `De-ess`, `De-breath`, and `Plosive` control.
- Added `VXFinish`, a finishing plugin built around the same DSP's dynamics/recovery stages. It owns smart recovery/body lift, compression, intelligent gain makeup, and limiting.
- Extracted shared tonal tracking into `Source/vxsuite/products/polish/VxPolishTonalAnalysis.h` so both new plugins follow the same vocal/general analysis instead of diverging.
- Kept vocal/general behavior aligned across both products: vocal remains more speech-protective, while general allows broader correction or finish reach.
- Kept all gain-adding behavior noise-aware in `VXFinish` by preserving the existing noise-floor gating and only applying smart makeup / recovery when the signal sits sufficiently above the estimated floor.
- Tightened the de-breath detector in `VxPolishDsp` so it is gated away from stronger transient and sibilant moments, making it follow breath-like airy events more specifically.
- Replaced `VXPolish` in the build graph with `VXCleanup` and `VXFinish`, and verified with `cmake -S . -B build`, `cmake --build build --target VXCleanup VXFinish -j4`, and `cmake --build build -j4`.

---

# VXCleanup classifier refactor — 2026-03-17

## Problem
`VXCleanup` still maps multiple corrective stages from shared `lowTrouble`, `highTrouble`, and `cleanupDrive` signals. This couples de-breath, de-ess, plosive, and tonal cleanup in ways that make the plugin feel less intelligent than its analysis-first architecture deserves.

## Plan
- [x] Re-read the current `VXCleanup` processor and confirm the active coupled mapping.
- [x] Add preallocated spectral feature extraction to `VXCleanup` using the shared framework FFT wrapper.
- [x] Compute explicit smoothed classifiers for breathness, sibilance, plosive risk, tonal mud, and harshness.
- [x] Remap `polish::Dsp::Params` from those classifier signals instead of shared trouble drives, with strict param clamps before `setParams()`.
- [x] Build `VXCleanup` and the full repo, then document the verification result.

## Review
- Added a preallocated spectral-analysis path directly in `VXCleanupAudioProcessor`, using the shared `VxSuiteFft` wrapper plus a ring buffer, Hann window, and FFT workspace allocated during `prepareSuite()`. No new heap work was added to `processProduct()`.
- `VXCleanup` now extracts explicit block features before mapping DSP params: spectral flatness, harmonicity (peak-dominance proxy), high-frequency ratio, high-band energy, and low-burst ratio. These are combined with the existing tonal ratios and voice-analysis snapshot instead of replacing them.
- Replaced the old coupled `lowTrouble` / `highTrouble` / `cleanupDrive` mapping with explicit classifier-style envelopes:
  - `breathEnv`
  - `sibilanceEnv`
  - `plosiveEnv`
  - `tonalMudEnv`
  - `harshnessEnv`
- Remapped the corrective stages from those classifiers rather than from a shared high-trouble signal:
  - `deMud` now follows `tonalMudWeight`
  - `deEss` now follows `sibilanceWeight`
  - `breath` now follows `breathWeight`
  - `plosive` now follows `plosiveWeight`
  - `troubleSmooth` now follows `harshWeight`
- The new breath/sibilance split is intentional:
  - breath classification favors noise-like, broadband, high-frequency content with low harmonicity and is gated by speech confidence/directness
  - sibilance classification favors bright, high-band, spectrally peaky content instead of sharing the same upstream drive as de-breath
- Added strict param clamps in the cleanup processor before `polishChain.setParams(params)` so the product enforces its own bounds even though the downstream DSP also guards inputs.
- Verified with:
  - `cmake --build build --target VXCleanup -j4`
  - `cmake --build build -j4`

---

# VX Suite shared-component review — 2026-03-17

## Problem
Review the VX Suite products for duplicate or near-duplicate processor/DSP/helper code that should be pulled into the framework or shared product components, and identify the strongest refactor opportunities without changing code yet.

## Plan
- [x] Inventory the current product modules and existing framework/shared helpers.
- [x] Compare processors, DSP shells, and helper patterns across products for duplication.
- [x] Identify the strongest 3-6 shared-component opportunities with file references and rationale.
- [x] Record the review result in this task log.

## Review
- [P1] Latency-aligned dry/listen scratch handling is duplicated across four products and should become a shared framework utility. `VXDenoiser`, `VXDeepFilterNet`, `VXDeverb`, and `VXSubtract` each own nearly the same `dryScratch` / `alignedDryScratch` / delay-line allocation, fill, and `renderListenOutput()` logic. See `Source/vxsuite/products/denoiser/VxDenoiserProcessor.cpp`, `Source/vxsuite/products/deepfilternet/VxDeepFilterNetProcessor.cpp`, `Source/vxsuite/products/deverb/VxDeverbProcessor.cpp`, and `Source/vxsuite/products/subtract/VxSubtractProcessor.cpp`. This is the strongest shared-component candidate because the repetition is structural, latency-bearing, and easy to drift out of sync.
- [P1] The spectral denoiser core is still split across two very similar product-local DSP implementations and should be factored into a shared spectral-cleanup core. `Source/vxsuite/products/denoiser/dsp/VxDenoiserDsp.*` and `Source/vxsuite/products/subtract/dsp/VxSubtractDsp.*` both carry overlapping STFT/min-stats/OM-LSA/bark-transient/harmonic-floor/phase-continuity machinery. `VXSubtract` adds learned-profile behavior on top, but the underlying blind denoise/scaffold is still clearly shared. This is a high-value extraction target, but it should be done carefully as a common spectral engine plus product-specific wrappers, not by forcing the products back together.
- [P2] Control smoothing helpers are duplicated in multiple processors and should be centralized in the framework. `blockBlendAlpha(...)` or equivalent inline exponential smoothing math appears in `VXCleanup`, `VXFinish`, `VXProximity`, `VXDenoiser`, `VXDeepFilterNet`, `VXSubtract`, and `VXDeverb`. See `Source/vxsuite/products/cleanup/VxCleanupProcessor.cpp`, `Source/vxsuite/products/finish/VxFinishProcessor.cpp`, `Source/vxsuite/products/proximity/VxProximityProcessor.cpp`, `Source/vxsuite/products/denoiser/VxDenoiserProcessor.cpp`, `Source/vxsuite/products/deepfilternet/VxDeepFilterNetProcessor.cpp`, `Source/vxsuite/products/subtract/VxSubtractProcessor.cpp`, and `Source/vxsuite/products/deverb/VxDeverbProcessor.cpp`. A small shared smoother/helper would remove repeated `exp()` math and keep time constants expressed consistently.
- [P2] The “Polish-derived analysis context” used by `VXCleanup` and `VXFinish` should probably be shared as a product-family helper rather than rebuilt ad hoc in each processor. Both compute tonal ratios from `VxPolishTonalAnalysis`, derive `speechConfidence` / `artifactRisk`, and then map those into `polish::Dsp::Params`. See `Source/vxsuite/products/cleanup/VxCleanupProcessor.cpp` and `Source/vxsuite/products/finish/VxFinishProcessor.cpp`. This should likely live under `Source/vxsuite/products/polish/` or a framework analysis helper used by the cleanup/finish family, not necessarily the global framework unless a third product needs the same contract.
- [P3] Simple parameter-layout construction is still reimplemented in a few processors even though the framework already has `createSimpleParameterLayout(...)`. `VXDenoiser` and `VXProximity` manually build what is effectively the standard two-float + mode + listen layout, and `VXDeverb` only diverges by control defaults/labels. See `Source/vxsuite/products/denoiser/VxDenoiserProcessor.cpp`, `Source/vxsuite/products/proximity/VxProximityProcessor.cpp`, `Source/vxsuite/products/deverb/VxDeverbProcessor.cpp`, and `Source/vxsuite/framework/VxSuiteParameters.h`. This is worth centralizing by making the framework helper support per-control defaults, but it is lower priority than the latency/listen and spectral-core duplication above.

---

# Cross-plugin shared-component review — 2026-03-17

## Problem
Review all current VX Suite plugins for duplicate or near-duplicate code and identify where multiple products should share a framework component instead of keeping separate local implementations.

## Plan
- [x] Inventory product modules and current framework/shared helpers.
- [x] Compare processors and DSP shells for repeated patterns that should become shared components.
- [x] Capture the strongest refactor opportunities with code references and risk notes.

## Review
- The strongest shared-component opportunity is latency-aligned listen support for latency-bearing products. `VXDenoiser`, `VXDeepFilterNet`, `VXSubtract`, and `VXDeverb` each keep their own dry scratch buffers, per-channel delay lines, aligned-dry builders, and custom `renderListenOutput()` paths even though they are all implementing the same core contract: aligned dry minus wet for removed-content audition. This should move into `ProcessorBase` as an optional latency-aligned listen helper rather than staying duplicated per product.
- Control smoothing is duplicated across most processors in slightly different local forms. `VxCleanupProcessor.cpp`, `VxFinishProcessor.cpp`, `VxDenoiserProcessor.cpp`, `VxDeepFilterNetProcessor.cpp`, `VxSubtractProcessor.cpp`, `VxProximityProcessor.cpp`, and `VxDeverbProcessor.cpp` all do some version of “prime on first block, then exponential block smoothing per control.” The exact time constants differ, but the state machine is mostly the same and should be a shared block-smoothing helper.
- `VxDenoiserDsp` and `VxSubtractDsp` still share a large spectral-core shape: min-stats noise tracking, OM-LSA presence probability, Bark transient hold, tonalness-driven protection, harmonic comb protection, frequency smoothing, and phase-continuity resynthesis. They should not be fully merged, but they are now close enough that a shared spectral engine/state/helper layer would reduce drift and make bug fixes land once.
- `VxCleanupProcessor.cpp` and `VxFinishProcessor.cpp` both rebuild the same high-level analysis fusion from `VxPolishTonalAnalysis` + `VoiceAnalysisSnapshot` before mapping into `polish::Dsp::Params`. This is product-specific enough that the final mapping should stay local, but the common “analysis evidence pack” wants a shared helper struct/function so the two products do not drift.
- Several products still duplicate custom parameter layouts that are very close to `createSimpleParameterLayout(...)`. `VxDenoiserProcessor.cpp`, `VxDeverbProcessor.cpp`, and `VxProximityProcessor.cpp` each hand-roll near-template layouts with the same mode/listen wiring and mostly standard float params. A more configurable shared layout builder would reduce repetition without forcing every product into the exact same defaults.

---

# Cross-plugin DRY refactor — 2026-03-17

## Problem
Reduce duplicated framework-shaped code that does not need to be product-local, especially where duplicated audio-path plumbing risks correctness drift across plugins.

## Plan
- [x] Add a shared framework helper for latency-aligned listen buffering/delta rendering.
- [x] Add a shared framework helper for block-rate smoothing/clamp math.
- [x] Migrate the duplicated latency-bearing processors to the shared helpers.
- [x] Migrate the simpler control-smoothing sites to the shared helper where it fits cleanly.
- [x] Build affected targets and the full repo, then document the result.
- [x] Extract the remaining shared Polish-family analysis evidence for Cleanup/Finish.
- [x] Expand the shared parameter-layout helper and migrate the near-template products.
- [x] Extract the denoiser/subtract spectral-core helpers that are still duplicated, then rebuild.

## Review
- Added `Source/vxsuite/framework/VxSuiteLatencyAlignedListen.h`, a shared helper that owns dry capture, aligned-dry buffering, per-channel delay lines, and removed-delta listen rendering for latency-bearing processors.
- Added `Source/vxsuite/framework/VxSuiteBlockSmoothing.h`, which centralizes `blockBlendAlpha`, block-rate one-time smoothing, attack/release smoothing, and shared `clamp01`.
- Extended `Source/vxsuite/framework/VxSuiteParameters.h` + `Source/vxsuite/framework/VxSuiteProduct.h` so products can declare per-control default values in the shared layout builder instead of hand-rolling near-template parameter layouts.
- Added `Source/vxsuite/products/polish/VxPolishAnalysisEvidence.h`, which centralizes the shared `TonalAnalysisState + VoiceAnalysisSnapshot` evidence pack used by both `VXCleanup` and `VXFinish`.
- Expanded `Source/vxsuite/framework/VxSuiteSpectralHelpers.h` with shared spectral setup helpers and a reusable Martin minimum-statistics state/update path used by both `VxDenoiserDsp` and `VxSubtractDsp`.
- Migrated the duplicated latency-aligned listen plumbing in:
  - `Source/vxsuite/products/denoiser/VxDenoiserProcessor.*`
  - `Source/vxsuite/products/deepfilternet/VxDeepFilterNetProcessor.*`
  - `Source/vxsuite/products/subtract/VxSubtractProcessor.*`
  - `Source/vxsuite/products/deverb/VxDeverbProcessor.*`
- Migrated the repeated local smoothing math in:
  - `Source/vxsuite/products/cleanup/VxCleanupProcessor.cpp`
  - `Source/vxsuite/products/finish/VxFinishProcessor.cpp`
  - `Source/vxsuite/products/proximity/VxProximityProcessor.cpp`
  - plus the latency-bearing processors above
- Removed the remaining near-template parameter-layout duplication from:
  - `Source/vxsuite/products/denoiser/VxDenoiserProcessor.*`
  - `Source/vxsuite/products/deverb/VxDeverbProcessor.*`
  - `Source/vxsuite/products/proximity/VxProximityProcessor.*`
- Migrated the shared Polish-family evidence derivation in:
  - `Source/vxsuite/products/cleanup/VxCleanupProcessor.*`
  - `Source/vxsuite/products/finish/VxFinishProcessor.*`
- Migrated shared spectral-core setup/min-stats helpers in:
  - `Source/vxsuite/products/denoiser/dsp/VxDenoiserDsp.*`
  - `Source/vxsuite/products/subtract/dsp/VxSubtractDsp.*`
- This removes the repeated per-product implementations of:
  - dry/aligned scratch allocation
  - per-channel latency delay lines
  - aligned dry reconstruction
  - removed-content listen delta rendering
  - duplicated block-rate smoothing formulas
- It also removes duplicated:
  - near-template mode/listen/float parameter layouts
  - Cleanup/Finish analysis-evidence formulas
  - denoiser/subtract sqrt-Hann prep, Bark-bin setup, and Martin minimum-statistics update/reset logic
- Verified with:
  - `cmake --build build --target VXDenoiser VXSubtract VXCleanup VXFinish VXDeverb VXProximity -j4`
  - `cmake --build build -j4`

---

# Legacy file removal pass — 2026-03-17

## Problem
Remove any remaining legacy or obsolete files left in the repo after the framework and DSP migrations, while keeping the active build tree intact.

## Plan
- [x] Audit the repo for remaining legacy/obsolete files and confirm whether they are referenced.
- [x] Delete unreferenced legacy files that are no longer part of the active suite.
- [x] Build the full repo and document the final state.

## Review
- Confirmed there are no remaining live `Source/dsp/`-style legacy code paths or backup/reject files in the repo.
- Confirmed the only remaining obsolete file under `Source/` was `Source/vxsuite/products/deverb/vx_deverb_phases_1_3_agent_guide.md`, an unreferenced agent-planning markdown artifact rather than active source.
- Deleted that stale deverb guide so `Source/` now contains active code/assets only.
- Verified with `cmake --build build -j4`.

---

# Polish DSP split — 2026-03-17

## Problem
`products/polish/dsp/VxPolishDsp.*` is still a monolithic leftover from when Polish behaved like a broader single product. Cleanup and Finish now own different jobs, so split the DSP by product responsibility and remove the old file.

## Plan
- [x] Identify the safe split boundary between shared corrective code and Finish-only recovery/limiter code.
- [x] Move Cleanup and Finish onto product-owned DSP entrypoints and delete the old `VxPolishDsp.*` file.
- [x] Build affected targets and the full repo, then document the result.

## Review
- Split the old monolithic `Source/vxsuite/products/polish/dsp/VxPolishDsp.*` into product-owned DSP entrypoints:
  - `Source/vxsuite/products/cleanup/dsp/VxCleanupDsp.*`
  - `Source/vxsuite/products/finish/dsp/VxFinishDsp.*`
- Kept only genuinely shared corrective-stage code in:
  - `Source/vxsuite/products/polish/dsp/VxPolishCorrectiveStage.*`
  - `Source/vxsuite/products/polish/dsp/VxPolishDspCommon.h`
  - `Source/vxsuite/products/polish/dsp/VxPolishSharedParams.h`
- Updated `VXCleanup` and `VXFinish` processors and `CMakeLists.txt` to compile against their own DSP files instead of the old shared monolith.
- Removed the obsolete `Source/vxsuite/products/polish/dsp/VxPolishDsp.h` and `Source/vxsuite/products/polish/dsp/VxPolishDsp.cpp` files entirely.
- This also eliminated the stale `sourcePreset` branch that was always pinned to `0` in both wrappers.
- Verified with:
  - `cmake --build build --target VXCleanup VXFinish -j4`
  - `cmake --build build -j4`

---

# Plugin regression harness — 2026-03-17

## Problem
Add a processor-level regression harness that checks the most failure-prone suite behaviors after the recent refactors: Subtract learn state/progress, listen-mode delta behavior, neutral cleanup behavior, and multi-plugin chain stability.

## Plan
- [x] Inspect the existing processor test pattern and choose the right harness structure.
- [x] Add shared processor test utilities for fixture generation, block rendering, and latency-aware trimming.
- [x] Implement an initial regression executable covering Cleanup identity, Subtract learn/listen behavior, and Subtract→Cleanup→Finish chain stability.
- [x] Configure/build the new test target, run it, and document the result.

## Review
- Added a new standalone regression executable at `tests/VXSuitePluginRegressionTests.cpp` plus shared helpers in `tests/VxSuiteProcessorTestUtils.h`, following the same "real processor instance" pattern as the existing suite test targets instead of introducing a separate unit-test framework.
- The first pass covers five high-value behaviors: `VXCleanup` identity at zero cleanup, `VXSubtract` learn lifecycle/progress monotonicity, `VXSubtract` listen-mode removed-content output, `VXSubtract -> VXCleanup -> VXFinish` chain stability, and silence preservation through the combined chain.
- The render helper is latency-aware, so processors are exercised with their reported plugin latency rather than with unrealistic zero-latency assumptions.
- The Subtract listen assertion was tuned to check steady-state recombination with a realistic tolerance after the initial alignment transient, which makes it useful as a regression guard without overfitting to startup buffer state.
- Verified with `cmake --build build --target VXSuitePluginRegressionTests -j4 && ./build/VXSuitePluginRegressionTests`, which completed successfully.

# Framework listen + Finish role pass — 2026-03-17

## Problem
Implement the next high-priority suite cleanup pass: centralize latency-aware listen and latency reporting in the shared framework, add a small coordination path for stage latency, reduce block-size dependence with explicit regression tests, and tighten `VXFinish` so it behaves like a finish stage rather than cleanup.

## Plan
- [x] Centralize latency-aware listen capture/alignment/rendering in the shared processor framework and remove duplicated product-local listen plumbing where it is no longer needed.
- [x] Add a small shared latency-coordination helper so processors can report summed stage latency consistently instead of open-coding `setLatencySamples(...)`.
- [x] Refactor `VXFinish` mapping and DSP flow so it no longer performs corrective cleanup work and its gain path is target-driven rather than RMS-reactive.
- [x] Extend the regression harness with host block-size invariance checks and an explicit full-chain test.
- [x] Build the affected targets, run the regression harness, and document the result.

## Review
- Added a shared coordinator at `Source/vxsuite/framework/VxSuiteProcessCoordinator.h` and wired it through `Source/vxsuite/framework/VxSuiteProcessorBase.h` / `Source/vxsuite/framework/VxSuiteProcessorBase.cpp`, so latency-aware removed-content listen now lives in the framework instead of four separate products carrying their own aligned-dry plumbing.
- `VXDenoiser`, `VXSubtract`, `VXDeverb`, and `VXDeepFilterNet` now report stage latency through the shared framework helpers and use the base listen path; `VXDeverb` still accesses aligned dry during processing for body restore, but it now does so through the base coordinator instead of a product-local listen buffer.
- `VXFinish` now behaves more like a finishing stage: its control mapping no longer rises from cleanup-style trouble metrics, recovery is driven by tonal deficit plus speech clarity, and makeup is target-driven from intended output loudness instead of being a direct reaction to pre/post RMS reduction inside the block.
- Expanded the regression harness to cover `Subtract -> Cleanup -> Proximity -> Finish` as an explicit suite chain and added block-size invariance checks so the current release bar now includes host-buffer consistency, not just single-block correctness.
- Verified with `cmake --build build --target VXSuitePluginRegressionTests -j4 && ./build/VXSuitePluginRegressionTests` and `cmake --build build -j4`.

# Suite master checklist — 2026-03-17

## Framework
- [x] Move latency-aligned Listen into `ProcessorBase` so every plugin uses the same removed-signal path.
- [x] Add a small stage-chain manager to own process order, total latency, and stage reset/prepare.
- [x] Report total latency to the host consistently from the framework.
- [x] Standardise on shared helpers like `VxSuiteBlockSmoothing.h` and remove duplicate local smoothing/clamp code.
- [x] Keep raw voice analysis separate from derived protection logic for easier tuning and debugging.

## Realtime / DAW safety
- [x] Test every plugin and the full chain at `64`, `128`, `256`, and `512` sample buffers.
- [x] Test at `44.1`, `48`, and `96` kHz.
- [~] Check transport stop/start, bypass, reset, state restore, and sample-rate changes in a DAW.
- [x] Verify no heap allocation, locks, or hidden vector growth happen on the audio thread.
- [x] Confirm behaviour stays consistent between mono and stereo paths.

## Plugin interaction
- [x] Tighten plugin role boundaries so each plugin does one job only.
- [x] Make sure later plugins never reintroduce problems earlier plugins removed.
- [x] Validate the full recommended chain, not just plugins in isolation.
- [x] Check that Listen mode behaves consistently across all plugins in the chain.

## VXCleanup
- [x] Keep pushing explicit event classification for breath, sibilance, plosive, and harshness.
- [x] Reduce dependence on current host block size for spectral/event detection.
- [x] Validate Cleanup across different DAW block sizes so detection feels stable.

## VXFinish
- [x] Remove or hard-cap any denoise/corrective behaviour.
- [x] Redefine `Gain` as a loudness target or finish bias, not reactive makeup.
- [x] Separate compressor role from limiter role more clearly.
- [x] Replace RMS-reactive makeup logic with a more stable target-driven approach.

## Consistency / product polish
- [x] Make shared controls like `Body`, `Gain`, and `Guard` feel consistent across plugins.
- [x] Keep UI and control behaviour coherent across the suite.
- [x] Add CPU profiling targets for single plugins and full-chain use.
- [x] Build a release checklist for host compatibility, latency, CPU, and chain behaviour.
- [x] Make the UI window larger than you think you need so all text is visible.
- [x] Ensure all text has enough space and is readable, with roughly `10–12 pt` minimum where possible.

## Notes
- `[x]` means completed in the current refactor pass.
- `[~]` means partially addressed in code or tests, but still needs deliberate follow-up or broader verification.
- The only remaining non-automated item is an actual interactive DAW host pass for transport/bypass/session-restore behaviour. The code now has automated coverage for lifecycle, sample-rate, block-size, mono/stereo, listen semantics, chain stability, and steady-state allocation safety, plus a profiling target and release checklist to drive the final manual host check.

## Final Review
- Added `Source/vxsuite/framework/VxSuiteStageChain.h` so stage latency / prepare / reset can be coordinated explicitly instead of by repeated convention in each processor shell.
- Expanded `tests/VXSuitePluginRegressionTests.cpp` into a broader lifecycle and safety harness: block-size invariance, multi-sample-rate coverage, mono/stereo consistency, Subtract state restore, listen-semantic checks, and a steady-state audio-thread allocation guard.
- Added `tests/VXSuiteProfile.cpp` as a profiling target for single-plugin and full-chain timing sweeps.
- Increased the shared editor’s default/minimum sizes and text/layout spacing in `Source/vxsuite/framework/VxSuiteEditorBase.cpp` and `Source/vxsuite/framework/VxSuiteLookAndFeel.cpp` so status text, hints, and control labels have more breathing room and stay readable.
- Added `docs/VX_SUITE_RELEASE_CHECKLIST.md` and `docs/VX_SUITE_CONTROL_SEMANTICS.md` so host validation and shared control meaning are now explicit project assets instead of only tribal knowledge.

---

# Deverb regression repair — 2026-03-17

## Problem
`VXDeverb` no longer behaves like the pre-refactor plugin. The dedicated tests show two hard regressions:
- `reduce=0` is not a true identity path
- speech correlation at meaningful `Reduce` settings collapses

The user explicitly wants to keep WPE, because the old plugin with WPE worked well.

## Plan
- [x] Compare the current Deverb wrapper and spectral path against the older working version to isolate the remaining regression.
- [x] Fix the underlying Deverb latency/processing mismatch without removing WPE or the good framework dry-capture fix.
- [x] Run `VXDeverbTests`, `VXDeverbMeasure`, and the broader regression harness.
- [x] Record the result and add a lesson if the root cause was introduced by the refactor.

## Review
- The biggest remaining regression was in the Deverb spectral stage timing: the STFT path was paying latency twice by both waiting for the causal analysis window and pre-advancing the OLA write cursor by the reported latency. Resetting `olaWritePos` to `0` in `Source/vxsuite/products/deverb/dsp/VxDeverbSpectralProcessor.cpp` restored true `reduce=0` identity and brought the processed output back into host-latency alignment.
- I kept the good framework change from `Source/vxsuite/framework/VxSuiteProcessorBase.cpp`: aligned dry is still captured every block so body-restore and listen paths can rely on it even when Listen mode is off.
- I compared the current Deverb files directly against commit `dc55d8c`, which confirmed that the wrapper was already close to the trusted behavior and that the real repair belonged in the spectral timing, not in removing WPE.
- `Source/vxsuite/products/deverb/VxDeverbProcessor.cpp` now gives `Body` a more meaningful low-band restoration path again by combining the original wet-to-dry low blend with a small bounded low-band support term.
- `tests/VXDeverbTests.cpp` was updated to match the corrected latency contract: full-wet Deverb is now expected to stay aligned after host latency compensation rather than line up better with an extra delayed dry signal.
- Verified with:
  - `cmake --build build --target VXDeverbTests VXSuitePluginRegressionTests VXDeverb -j4`
  - `./build/VXDeverbTests`
  - `./build/VXSuitePluginRegressionTests`
  - `./build/VXDeverbMeasure --reduce 0 --body 0 --render-seconds 0.9`
  - `cmake --build build -j4`

---

# Finish gain/compressor rebalance — 2026-03-17

## Problem
`VXFinish` still feels wrong in use:
- the compressor path can clip
- the `Gain` control is not meaningfully useful
- the middle of the `Gain` control should be neutral, with left reducing and right increasing

## Plan
- [x] Inspect the current Finish gain/compressor mapping and identify why the midpoint already adds level.
- [x] Rework the Finish gain law so `0.5` is neutral and the control behaves bipolarly around center.
- [x] Reduce compressor/makeup-driven clipping without making Finish feel lifeless.
- [x] Run focused Finish verification and the suite regression harness.
- [x] Record the result and any lesson from the fix.

## Review
- `Source/vxsuite/products/finish/VxFinishProcessor.cpp` no longer treats the `Gain` knob as a hidden driver for compression, recovery, limiter pressure, and target makeup. The midpoint is now truly neutral, with the knob interpreted as a bipolar final output trim around `0.5`.
- `Gain` now happens at the end of the Finish path as a smoothed final output gain stage after limiting, with the existing trim guard still catching overs so the plugin does not clip just because the user pushes the right side.
- Compression/makeup pressure was reduced at the processor mapping level so the compressor is less likely to overshoot into the limiter. `Finish` and `Body` still shape density/recovery, but `Gain` no longer quietly changes the compressor personality.
- Updated the `Gain` hint text to describe the intended behavior directly: middle neutral, left reduce, right increase.
- Added regression coverage in `tests/VXSuitePluginRegressionTests.cpp` so Finish now has an explicit test for bipolar `Gain` behavior around the centered midpoint.
- Verified with:
  - `cmake --build build --target VXSuitePluginRegressionTests VXFinish -j4`
  - `./build/VXSuitePluginRegressionTests`
  - `cmake --build build -j4`
