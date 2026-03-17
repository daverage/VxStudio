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
- Increased the learn target window and reduced the variance padding applied when freezing the learned profile, making the result closer to an Audacity-style learned noise estimate instead of an overly padded conservative profile.
- Verified with `cmake --build build --target VXSubtract -j4` and `cmake --build build -j4`.
