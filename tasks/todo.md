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
