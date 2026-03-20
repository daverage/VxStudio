# VX Suite Full Audit Report

Date: 2026-03-20
Baseline: `VX_Suite_Audit_Plan_v2.md`
Repo: `VxStudio`
Host environment used for this pass: macOS 15.7.4, Apple Silicon (`arm64`)

## Executive Summary

The current codebase is in decent shape from a local build-and-regression perspective: the main regression suite, deverb tests, and voice-analysis tests all passed locally, and `VXSuiteProfile` showed comfortable sub-realtime CPU ratios for the profiled chain on this machine.

The highest-signal confirmed issues from this pass are release and host-integration problems rather than immediate compile/runtime breakage:

1. All framework-based products report a tail length of `0.0`, including latency-bearing or tail-bearing processors.
2. `VXDeepFilterNet` does not resolve models from the staged VST3 bundle `Contents/Resources` path and the staged bundle currently does not include the model archives.
3. The staged macOS VST3 bundles are ad-hoc signed and not notarized/stapled.

Those three items are the most important blockers against the v2 audit baseline.

## Automated Verification Results

### Local build/test results

Commands run:

```bash
cmake --build build --target VXSuitePluginRegressionTests VxSuiteVoiceAnalysisTests VXDeverbTests VXSuiteProfile -j4
./build/VXSuitePluginRegressionTests
./build/VXDeverbTests
./build/VxSuiteVoiceAnalysisTests
./build/VXSuiteProfile
```

Observed result:

- `VXSuitePluginRegressionTests`: passed
- `VXDeverbTests`: passed
- `VxSuiteVoiceAnalysisTests`: passed
- `VXSuiteProfile`: passed

`VXSuiteProfile` summary:

- `44.1 kHz`, full chain: `x0.127-0.178 realtime`
- `48 kHz`, full chain: `x0.137-0.179 realtime`
- `96 kHz`, full chain: `x0.250-0.348 realtime`

Interpretation:

- The currently profiled Cleanup/Proximity/Finish/Subtract chain looks comfortably realtime-safe on this Apple Silicon machine.
- This profile target does not currently cover `VXDeepFilterNet`, `VXDenoiser`, `VXDeverb`, `VXTone`, or `VXStudioAnalyser`, so it is not a full suite performance sign-off.

### External validation / host-tool blockers

Not available in the local environment:

- `pluginval`: not found on `PATH`
- `reaper`: not found on `PATH`

Consequences:

- Pluginval strictness `10` was not run in this pass.
- Steinberg VST3 validation via pluginval strictness `>= 5` was not run in this pass.
- REAPER host/PDC/offline-render validation was not run in this pass.

These are audit blockers, not inferred passes.

## Findings

### [P0] macOS staged bundles are not release-signed or notarized

Evidence:

- `codesign -dv --verbose=2 Source/vxsuite/vst/VXCleanup.vst3` reports `Signature=adhoc` and `TeamIdentifier=not set`
- `xcrun stapler validate Source/vxsuite/vst/VXCleanup.vst3` reports `does not have a ticket stapled to it`

Impact:

- These bundles do not meet the plan's macOS shipping bar.
- Gatekeeper/notarization expectations for an end-user install are not satisfied.

Recommended fix:

- Add Developer ID signing and notarization/stapling to the release pipeline.
- Treat notarization as a release gate for macOS artifacts.

### [P1] All framework-based products currently report zero tail length

Evidence:

- [Source/vxsuite/framework/VxSuiteProcessorBase.h](../../Source/vxsuite/framework/VxSuiteProcessorBase.h) sets `getTailLengthSeconds()` to `0.0` at line `46`
- no non-analyser product override was found in the current source tree

Impact:

- Offline render in hosts such as REAPER can legally stop feeding silence immediately after input ends.
- Tail-bearing or latency-bearing processors such as `VXDeverb`, `VXDenoiser`, `VXSubtract`, and `VXDeepFilterNet` are at risk of truncated tails or prematurely cut processing during offline render/export.

Recommended fix:

- Add product-specific `getTailLengthSeconds()` overrides for any processor with non-trivial residual output or latency/tail semantics.
- Add automated tail-length tests and REAPER offline-render verification.

### [P1] `VXDeepFilterNet` does not load models from the VST3 bundle resource path, and staged bundles currently omit the model files

Evidence:

- [Source/vxsuite/products/deepfilternet/dsp/VxDeepFilterNetService.cpp](../../Source/vxsuite/products/deepfilternet/dsp/VxDeepFilterNetService.cpp) lines `212-238` search relative to the current working directory
- the same file lines `264-275` fall back to temp-directory extraction rather than bundle-local resources
- staged bundle contents under `Source/vxsuite/vst/VXDeepFilterNet.vst3/Contents/Resources/` currently contain `moduleinfo.json` only; no model archive files were staged

Impact:

- Fresh installed bundles are not portable in the way the v2 plan expects.
- Behavior depends on launch context and whether embedded model extraction succeeds.
- This is especially risky for host installs where the working directory is not the repo root.

Recommended fix:

- Resolve model assets from the plugin bundle location (`Contents/Resources`) first.
- Stage the model archives into the bundle resources during packaging, or make embedded-model extraction the explicit and fully tested shipping path.
- Add a bundle-integrity regression lane for `VXDeepFilterNet`.

### [P2] Plugin validation is not integrated into the build/test pipeline

Evidence:

- no `pluginval`, `CTest`, `enable_testing`, or `add_test` integration was found in `CMakeLists.txt`, `cmake/`, or `tests/`
- `pluginval` was not available locally on `PATH`

Impact:

- The plan's strictness `5` and `10` validation gates are currently absent.
- Host-conformance regressions can slip through until manual DAW testing.

Recommended fix:

- Add pluginval as an optional-but-supported CMake/CTest lane.
- Promote strictness `5` to PR gate and strictness `10` to release gate.

### [P2] `VXDeverb` target registers duplicate source files in CMake

Evidence:

- [CMakeLists.txt](../../CMakeLists.txt) lines `173-177` include `VxDeverbRt60Estimator.cpp` and `VxDeverbWpeStage.cpp` twice

Impact:

- The current build still succeeds, but the target definition is noisy and easier to accidentally break later.
- This is a build hygiene issue that should be cleaned up during the packaging/build audit.

Recommended fix:

- Remove the duplicate source entries from the `VXDeverb` target.

### [P2] Current staged macOS bundles are `arm64` only, not universal

Evidence:

- `file Source/vxsuite/vst/VXDeepFilterNet.vst3/Contents/MacOS/VXDeepFilterNet` reports `Mach-O 64-bit bundle arm64`
- `file Source/vxsuite/vst/VXCleanup.vst3/Contents/MacOS/VXCleanup` reports `Mach-O 64-bit bundle arm64`

Impact:

- Apple Silicon support is present.
- Intel macOS support is unproven from the staged artifacts in this repo snapshot.

Recommended fix:

- Either document `arm64`-only support explicitly, or produce universal binaries if Intel macOS remains in scope.

## Positive Checks

- Shared APVTS construction is static at processor construction time.
- No APVTS listener-registration usage was found in the shared code paths audited in this pass.
- Oversized host-block chunking exists in `ProcessorBase::processBlock(...)`.
- The major DSP processors audited in code (`Cleanup`, `Subtract`, `Deverb`, `Denoiser`, `DeepFilterNet`, `Finish`, `OptoComp`, `Tone`, `Proximity`) all use `juce::ScopedNoDenormals` in their processing path.
- The local regression suite already covers several high-value contracts:
  - listen semantics
  - state restore
  - block-size invariance
  - no steady-state allocation for Cleanup
  - headroom and boundedness cases

## Product/Subsystem Notes

| Area | Current status | Notes |
|---|---|---|
| Framework shell | Mostly sound, but incomplete for tail reporting and pluginval coverage | Chunking and latency-aligned listen are present |
| `VXDeepFilterNet` | Highest-risk product remains packaging/runtime path | Fixed-latency path exists; bundle resource path does not |
| `VXSubtract` | Core regression coverage is fairly strong | Learn/save/restore checks already exist |
| `VXDeverb` | DSP and tests build cleanly | CMake target has duplicate sources; tail reporting absent |
| `VXDenoiser` | Uses stage-chain latency and local denormal protection | Tail reporting absent |
| `VXStudioAnalyser` | Not deeply audited in-host this pass | REAPER and UI stress remain blocked by missing host/tooling |

## Coverage Gaps Still Open

- Pluginval strictness `10` on all products
- Steinberg VST3 validator pass
- REAPER PDC correlation tests
- REAPER offline-render tail verification
- REAPER loop/seek discontinuity verification
- Windows build/signing/CRT validation
- Universal-binary decision and verification for macOS
- Full-suite no-allocation coverage for every processor
- `VXDeepFilterNet` sample-rate/resampler regression coverage
- Frequency-response regression coverage for frequency-shaped products

## Suggested Remediation Order

1. Fix macOS signing/notarization and define the release pipeline.
2. Fix tail-length reporting for all relevant products and add tail tests.
3. Fix `VXDeepFilterNet` bundle resource staging and resource-path resolution.
4. Add pluginval/CTest integration.
5. Clean up CMake duplication and document architecture support.

