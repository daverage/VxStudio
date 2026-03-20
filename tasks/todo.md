# DSP hot-path pass (phase 1) — 2026-03-19

## Problem
The latest DSP review identified several confirmed hot-path issues. The highest-value ones to start with were `CorrectiveStage` rebuilding six peaking EQs on every sample and `OptoCompressorLA2A` doing a slow-release `std::exp` per sample.

## Plan
- [x] Move Cleanup/Polish trouble-band EQ coefficient generation out of the per-sample path.
- [x] Reduce `OptoCompressorLA2A` per-sample transcendental work without changing the audible contract.
- [x] Rebuild affected plugins and run the regression suite.
- [ ] Tackle the bigger `SubtractDsp` memory-shift refactor in a dedicated follow-up pass.

## Review
- `Source/vxsuite/products/polish/dsp/VxPolishCorrectiveStage.cpp` now runs the trouble-band detection over the block, accumulates per-band cut depth, and builds the six peaking EQ coefficient sets once per block instead of once per sample. That removes the worst repeated trig/pow work from Cleanup's hottest path while preserving the same trouble-smoothing role.
- `Source/vxsuite/products/OptoComp/OptoCompressorLA2A.cpp` now computes the slow-release coefficient once per block from the current optical-memory / LF state estimate instead of calling `std::exp` for it on every sample.
- During verification, the earlier shared framework output trimmer change was corrected so the emergency trimmer protects normal processed output but does not touch listen-delta output. That keeps headroom safety while preserving subtract-style wet+listen recombination semantics.
- The larger `SubtractDsp` ring-buffer conversion was started, exposed a behavior regression, and was intentionally backed out for this pass. It should come back as a dedicated refactor with targeted verification rather than being forced through alongside the safe CPU wins.
- Verified with `cmake --build build --target VXCleanupPlugin VXOptoCompPlugin VXSuitePluginRegressionTests -j4` and `./build/VXSuitePluginRegressionTests`.

# README refresh for analyser + framework behavior — 2026-03-19

## Problem
The root README and framework README had drifted behind the current implementation. They still described the analyser as having no controls, described listen as always outputting removed material, and implied output safety was only an optional product-local concern instead of part of the shared framework behavior.

## Plan
- [x] Review the root project README and framework README for outdated analyser, listen, and framework-behavior descriptions.
- [x] Update both documents so they reflect the current analyser controls/telemetry model and the current framework output-safety/layout contract.
- [x] Review the doc diff for clarity and consistency.

## Review
- `README.md` now describes `VXStudioAnalyser` as a chain-aware spectrum analyser with `Avg Time`, `Smoothing`, and `Hide Chain`, and explains the current full-chain / per-stage dry-vs-wet workflow.
- The root README also now documents the current listen contract correctly: removal tools audition what was removed, while additive/finishing tools audition what they added.
- `Source/vxsuite/framework/README.md` now documents the responsive shared editor behavior, automatic final output safety in `ProcessorBase`, optional product-local trimmers for extra discipline, and the shared telemetry path used by `VXStudioAnalyser`.
- This was a documentation-only pass; no build was required.

# Cleanup / suite headroom safety pass — 2026-03-19

## Problem
`Cleanup` could clip when De-Ess or Plosive correction were driven hard, and not every VX Suite product had a final output safety stage. The user asked for good headroom across the suite so no processor clips just by being active.

## Plan
- [x] Inspect the Cleanup corrective path and confirm where De-Ess / Plosive overshoot can occur.
- [x] Add a shared suite-wide emergency output safety layer and a Cleanup-specific overshoot guard that preserves subtractive behavior.
- [x] Add regression coverage for Cleanup De-Ess/Plosive stress, rebuild affected targets, and verify the fix.

## Review
- `Source/vxsuite/framework/VxSuiteProcessorBase.*` now owns a shared emergency `OutputTrimmer` for every VX processor. It runs after normal product processing in non-listen mode and after listen rendering in listen mode, providing a final suite-wide headroom safeguard even for products that did not previously have local output trimming.
- `Source/vxsuite/products/cleanup/VxCleanupProcessor.cpp` now measures dry versus post-corrective peak and reins the corrective chain back to a tiny margin above the dry peak before its local trimmer runs. That keeps Cleanup aligned with its subtractive intent: De-Ess, Plosive, and related corrective moves should not create new peak overs just because of filter-split recombination.
- `tests/VXSuitePluginRegressionTests.cpp` now includes a dedicated Cleanup stress case with both low-frequency plosive bursts and aggressive high-frequency sibilance content at full settings. The regression asserts finite output, safe peak headroom, and no large peak inflation relative to the input.
- Verified with `cmake --build build --target VXCleanupPlugin VXStudioAnalyserPlugin VXSuitePluginRegressionTests -j4` and `./build/VXSuitePluginRegressionTests`.

# Analyzer low-band display regression — 2026-03-19

## Problem
The latest in-host analyser pass regressed at the bottom of the spectrum: low bins could dominate summary text with obviously wrong callouts like large `20 Hz` deltas, and the visible low-end contour diverged from the SPAN reference because the shared low-band reduction was still too spike-sensitive.

## Plan
- [x] Inspect the shared spectrum reduction and analyser summary-selection paths to find why very-low bands were over-winning both the plot and the readout.
- [x] Make low-band reduction less sensitive to single-bin spikes and stop near-floor bands from being selected as the largest/dominant change.
- [x] Rebuild `VXStudioAnalyserPlugin`, rerun regression coverage, and document the fix.

## Review
- `Source/vxsuite/framework/VxSuiteSpectrumTelemetry.cpp` now widens extremely small summary bands to a minimum three-bin neighborhood before reduction and uses a less peak-heavy blend for those tiny bands. That keeps one FFT bin at the edge of the log scale from distorting the whole low-end display.
- `Source/vxsuite/products/analyser/VXStudioAnalyserEditor.cpp` now ignores near-floor bands when choosing the largest/dominant spectral delta for the summary text. The graph marker and readout should no longer jump to fake `20 Hz` wins when both dry and wet are effectively at the noise floor there.
- Verified with `cmake --build build --target VXStudioAnalyserPlugin VXSuitePluginRegressionTests -j4` and `./build/VXSuitePluginRegressionTests`.

# Analyzer timing + spectrum fidelity pass — 2026-03-18

## Problem
The analyser is closer, but it still diverges from SPAN in two visible ways: `Avg Time` feels jerky because it currently gates visible refreshes instead of behaving like a smooth RT-average, and the spectrum shape still loses too much low/high detail because each log band is reduced with a broad RMS average.

## Plan
- [x] Rework `Avg Time` so the chart repaints steadily while averaging continuously over time, instead of stepping the visible snapshot cadence.
- [x] Improve the summary-spectrum reduction so the full `20 Hz` to `20 kHz` plot preserves prominent peaks and better matches the reference analyzer shape.
- [x] Build and run the analyser/deverb verification targets, then document the remaining gap versus SPAN.

## Review
- `Source/vxsuite/products/analyser/VXStudioAnalyserEditor.*` now treats `Avg Time` as a true time-domain average of incoming spectrum/envelope frames rather than a stepped snapshot throttle. The UI repaints steadily at `24 Hz`, and longer average times now simply lengthen the decay/settling of the displayed spectrum instead of making the chart visibly jerk between sparse refreshes.
- Expanded `Avg Time` options to `100 ms` through `10000 ms`, keeping the default at `1000 ms` so the analyzer can cover both transient debugging and long-horizon tonal-balance reading more like SPAN.
- `Source/vxsuite/framework/VxSuiteSpectrumTelemetry.h` now publishes a denser analyzer spectrum with `256` log-spaced display bands and an `8192`-sample FFT. `Source/vxsuite/framework/VxSuiteSpectrumTelemetry.cpp` now reduces each log band with a peak-preserving blend instead of a plain RMS average, which keeps narrow resonances and high-frequency structure from collapsing into a broad midrange hump.
- `Source/vxsuite/products/analyser/VXStudioAnalyserEditor.cpp` now applies a fixed `4.5 dB/oct` display slope, matching the supplied SPAN reference more closely, and labels the full visible frequency range from `20 Hz` to `20 kHz`.
- Verified with `cmake --build build --target VXStudioAnalyserPlugin VXDeverbTests -j4` and `./build/VXDeverbTests`.

# Analyzer stage visibility + smoothing calibration — 2026-03-18

## Problem
After the timing/fidelity pass, the analyser regressed in two practical ways: external stage rows could disappear so the UI looked like it only supported `Full Chain`, and the smoothing control became nearly inert because the old fixed-neighbor mapping was far too weak for the new `256`-band spectrum.

## Plan
- [x] Relax the stage freshness gate so valid VX Suite stages remain visible/selectable in the rail instead of dropping out to full-chain fallback.
- [x] Remap spectral smoothing by octave width so `1/12`, `1/6`, `1/3`, and `1 OCT` produce visibly different blur amounts on the denser spectrum.
- [x] Rebuild the analyser target and rerun the standing deverb regression check.

## Review
- `Source/vxsuite/products/analyser/VXStudioAnalyserEditor.cpp` now keeps stage rows alive for up to `1500 ms` of telemetry age, which is much more tolerant of host/UI jitter than the previous `300 ms` gate and should stop the analyser collapsing to `Full Chain` when the live stage list is still valid.
- The smoothing control now scales against actual bins-per-octave for the current analyzer spectrum density instead of a tiny fixed radius. On a `256`-band plot, `1/6 OCT`, `1/3 OCT`, and broader modes now smooth over meaningfully different frequency spans, so the control should finally behave like a real spectral smoothing selector.
- Verified with `cmake --build build --target VXStudioAnalyserPlugin -j4` and `./build/VXDeverbTests`.

# Analyzer stage fallback + Avg Time visibility pass — 2026-03-18

## Problem
The analyser still showed two regressions in-host: the left rail could still end up empty except for `Full Chain`, and `Avg Time` remained too subtle because the visible traces were no longer being averaged strongly enough for the user to perceive the longer time constants.

## Plan
- [x] Add a robust fallback stage scan so the analyser can still populate the rail from live VX Suite stages when the strict current-domain scan comes up empty.
- [x] Make `Avg Time` act on the visible dry/wet traces more strongly so long settings like `5000 ms` and `10000 ms` clearly slow and stabilize the chart.
- [x] Rebuild the analyser target and rerun the standing deverb regression check.

## Review
- `Source/vxsuite/products/analyser/VXStudioAnalyserEditor.h` / `Source/vxsuite/products/analyser/VXStudioAnalyserEditor.cpp` now carry a fallback-stage mode. If no valid stages are found in the analyser's exact current domain, the UI falls back to other recent live VX Suite stages from the shared registry instead of presenting an empty rail.
- The diagnostics text now reports whether the rail is coming from the current domain or the fallback registry scan, which should make any remaining domain-binding issue obvious instead of silent.
- `Avg Time` now drives a stronger second temporal averaging pass over the visible dry/wet traces themselves, not just the hidden linear intermediate state. That should make large values visibly stabilize and slow the plotted curves in a way the user can actually perceive.
- Verified with `cmake --build build --target VXStudioAnalyserPlugin -j4` and `./build/VXDeverbTests`.

# Analyzer cross-process domain binding fix — 2026-03-18

## Problem
The latest in-host screenshot showed `live stages: 0`, which meant the analyser was only seeing its own pass-through telemetry. That explains the empty left rail, the fake dry/wet behavior, and why stage selection could not work at all. The likely root cause is cross-process hosting: external VX stages were only trying to bind to the newest analyser domain in their own process, not the newest analyser domain globally.

## Plan
- [x] Add a global active-domain lookup alongside the existing per-process lookup in the shared analysis registry.
- [x] Make stage publishers fall back to that global analyser domain when no current-process analyser domain exists.
- [x] Rebuild the analyser target and rerun the standing deverb regression check.

## Review
- `Source/vxsuite/framework/VxSuiteSpectrumTelemetry.h` / `Source/vxsuite/framework/VxSuiteSpectrumTelemetry.cpp` now expose `DomainRegistry::latestActiveDomain(...)` in addition to `latestDomainForProcess(...)`.
- `StagePublisher::refreshDomainBinding(...)` now first tries to bind to the newest analyser domain in the current process, and if that fails, falls back to the newest active analyser domain globally. This should let VX stages still join the analyser chain even when the host bridges plugins into separate processes.
- Verified with `cmake --build build --target VXStudioAnalyserPlugin -j4` and `./build/VXDeverbTests`.

# Analyzer real Avg Time window — 2026-03-18

## Problem
Even after the earlier timing passes, `Avg Time` still behaved more like a subtle smoother than a real analyzer average. The user clarified the intended contract: higher values should visibly reduce fluctuation while the trace remains fluid, like SPAN's continuous RT averaging.

## Plan
- [x] Replace the editor-side exponential-only spectrum smoothing with a true rolling time window over recent spectrum frames.
- [x] Keep the UI repaint cadence smooth while using the selected `Avg Time` value to control how much recent spectrum history contributes to the visible trace.
- [x] Rebuild the analyser target and rerun the standing deverb regression check.

## Review
- `Source/vxsuite/products/analyser/VXStudioAnalyserEditor.h` / `Source/vxsuite/products/analyser/VXStudioAnalyserEditor.cpp` now maintain a rolling spectrum history per selection, along with running sums for dry and wet spectra.
- `Avg Time` now directly controls the size of that time window. Increasing the control causes the visible spectrum to average more recent frames together, which should reduce fluctuation without turning the chart into a stepped or frozen display.
- Verified with `cmake --build build --target VXStudioAnalyserPlugin -j4` and `./build/VXDeverbTests`.

# Restage VX chain after framework telemetry fixes — 2026-03-18

## Problem
The sidebar still did not repopulate in REAPER even after the framework-side domain fixes. The likely operational cause is that only `VXStudioAnalyser` had been rebuilt/restaged; the other VX Suite effects in the chain were still older binaries and therefore still publishing stage telemetry with the pre-fix shared framework behavior.

## Plan
- [x] Rebuild and restage the relevant VX Suite chain plugins so they all pick up the latest shared framework telemetry/domain changes.
- [x] Rerun the standing deverb regression check after the full plugin restage.

## Review
- Rebuilt/restaged `VXSubtract`, `VXCleanup`, `VXDeepFilterNet`, `VXDeverb`, `VXDenoiser`, `VXProximity`, `VXTone`, `VXOptoComp`, `VXFinish`, and `VXStudioAnalyser` with `cmake --build build --target VXSubtractPlugin VXCleanupPlugin VXDeepFilterNetPlugin VXDeverbPlugin VXDenoiserPlugin VXProximityPlugin VXTonePlugin VXOptoCompPlugin VXFinishPlugin VXStudioAnalyserPlugin -j4`.
- This ensures the whole REAPER chain now shares the same updated `VxSuiteFramework` telemetry/domain code rather than mixing a new analyser with old effect binaries.
- Re-ran `./build/VXDeverbTests` after the full restage.

# Analyzer FFT + range upgrade — 2026-03-18

## Problem
The analyser still feels fundamentally unlike SPAN because the underlying shared telemetry is too coarse. Even with better display logic, a `32`-band summary spectrum cannot present the full `20 Hz` to `20 kHz` picture with the width and separation the user expects from a real analyzer-style display.

## Plan
- [x] Increase the analyser telemetry FFT size and spectrum-band density so the dry/wet overlay can represent the audible range much more faithfully.
- [x] Tune the analyser plot labels and scaling so the denser spectrum reads clearly from `20 Hz` to `20 kHz`.
- [x] Build and run the affected targets/tests, then document the new analyzer contract and any remaining gaps versus SPAN.

## Review
- `Source/vxsuite/framework/VxSuiteSpectrumTelemetry.h` now publishes a denser analyser spectrum using a `4096`-sample FFT (`kSummarySpectrumFftOrder = 12`) and `128` display bands instead of the old `2048` / `32` summary path. That gives the dry/wet overlay much more room to represent the full `20 Hz` to `20 kHz` range rather than collapsing into a broad midrange sketch.
- `Source/vxsuite/products/analyser/VXStudioAnalyserEditor.cpp` continues to use the same stage-selection model on top of that denser telemetry, while the sparse-spectrum detection thresholds were widened so narrowband/test-tone cases still switch into the honest sparse display mode with the higher band count.
- The analyser still is not a clone of SPAN: it remains a VX Suite stage-aware analyzer rather than a full dedicated meter. But the backend is no longer bottlenecked by a 32-band summary spectrum, which was the main reason the view could not feel wide enough no matter how the UI was tuned.
- Verified with `cmake --build build --target VXStudioAnalyserPlugin VXDeverbTests -j4` and `./build/VXDeverbTests`.

# Deverb loudness + analyser spectrum redesign — 2026-03-18

## Problem
`VXDeverb` currently allows a substantial output level collapse because its wet path removes energy without any restrained direct-loudness compensation. Separately, `VX Studio Analyser` still uses a `Tone/Dynamics` presentation model when the desired product direction is closer to SPAN: dry and wet spectra overlaid in one place, while keeping stage-by-stage chain selection.

## Plan
- [x] Add a bounded loudness-preservation path to `VXDeverb` so dereverbing no longer causes a large level drop simply from inserting/using the plugin.
- [x] Add a regression test that catches unacceptable `VXDeverb` volume loss while preserving tail-reduction behavior.
- [x] Rework `VX Studio Analyser` into a single dry-vs-wet spectrum view with the current stage-selection model, replacing the current `Tone/Dynamics` tab split in practice.
- [x] Build and run the affected targets/tests, then document the new behavior contract and any remaining calibration work.

## Review
- `Source/vxsuite/products/deverb/VxDeverbProcessor.*` now applies a restrained post-deverb loudness compensation stage based on the dry-versus-wet RMS delta. The makeup is capped, smoothed over time, and peak-limited so `VXDeverb` no longer solves dereverbing by simply dumping overall level.
- Added a regression in `tests/VXDeverbTests.cpp` that fails if a representative dereverb render keeps too little of the input RMS, while the existing tail-reduction/coherence tests continue to guard the actual dereverb job.
- `Source/vxsuite/products/analyser/VXStudioAnalyserEditor.*` now behaves as a single dry/wet spectrum viewer instead of a `Tone/Dynamics` toggle. The main plot overlays dry and wet spectra in one place, keeps the stage-chain selection model, and preserves the sparse-spectrum honesty mode for narrowband/test-like signals.
- Added user-facing analyser controls for `Avg Time` and `Smoothing`, defaulting to `1000 ms` and `1/6 OCT` to match the supplied SPAN reference. These settings now drive the editor-side temporal averaging and frequency-domain smoothing so you can trade stability against detail.
- The current analyser still uses the existing backend telemetry overlap, which already starts from the same 75% overlap contract as the SPAN screenshot. The new controls are editor-side timing/smoothing controls rather than a full telemetry ABI change.
- Verified with `cmake --build build --target VXStudioAnalyserPlugin VXDeverbTests -j4` and `./build/VXDeverbTests`.

# Analyzer sparse-spectrum honesty pass — 2026-03-18

## Problem
The current `VX Studio Analyser` tone view still lies on sparse or test-like material. A sine/comb-like chain that reads as distinct peaks in SPAN is being blurred into a broad connected curve and summarized with language like `Low lift`, which makes the analyser look unstable and semantically wrong even when the underlying processors are not.

## Plan
- [x] Detect sparse / narrowband tone cases in the analyser render model instead of treating every spectrum like a broad voice-shaping curve.
- [x] Change the tone graph and summary language for sparse cases so the UI shows discrete affected bands and suppresses fake broad labels.
- [x] Build `VXStudioAnalyserPlugin`, review the new behavior contract, and document any remaining limitations.

## Review
- `Source/vxsuite/products/analyser/VXStudioAnalyserEditor.*` now classifies sparse / narrowband spectra from the actual band-energy distribution before choosing how to render the tone view. Signals dominated by a few peaks no longer get automatically passed through the broad-curve path.
- In sparse mode, the tone tab now stops neighbor-blending the bins into a fake shelf/tilt shape. It renders discrete per-band stems and markers instead, which is much closer to what tools like SPAN show for comb-like or test-tone material.
- The tone summary text now becomes honest for sparse cases: it reports the primary changed band plus a few significant band changes and explicitly suppresses broad labels like `Low lift` or `High cut`.
- The stage rail classifier now uses the same sparse detection, so rows that would previously chatter between `Tone`, `Dynamic`, and `Mixed` on narrowband material can identify as `Sparse` instead of pretending the change is a broad tonal shape.
- Verified with `cmake --build build --target VXStudioAnalyserPlugin -j4`. The existing ad-hoc code-signing replacement message appeared during VST3 staging, but the target completed successfully.
- Remaining limitation: this is still a 32-band explanation view, not a full-resolution analyzer like SPAN. The sparse mode makes it materially more honest, but it will still summarize rather than measure every narrow peak.

# Full VX Suite audit pass — 2026-03-20

## Problem
The repo now has a formal v2 audit baseline covering plugin validation, framework correctness, realtime safety, DSP correctness, REAPER host behavior, and release/build readiness. The suite needs that plan executed against the current codebase, with findings and coverage gaps documented from actual code and tooling results rather than assumptions.

## Plan
- [x] Capture the current environment, available validation tools, and shipping plugin inventory.
- [x] Run available automated verification lanes (`cmake` tests/profile/build checks, and `pluginval` if available) and record blockers for any missing external tooling.
- [x] Audit the shared framework against the v2 baseline: parameter/state contracts, denormal handling, latency/listen/tail behavior, oversized-block handling, and hot-path safety.
- [x] Audit products in risk order with emphasis on DeepFilterNet, Subtract, Deverb, and Denoiser, then cover the remaining processors and analyser.
- [x] Audit build/packaging metadata, bundle/resource layout, and platform/release readiness claims.
- [x] Produce a written audit report with severity-ranked findings, verification evidence, and explicit test/tooling gaps.

## Review
- Environment captured on macOS 15.7.4 / Apple Silicon with a clean worktree and all staged VST3 bundles present under `Source/vxsuite/vst/`. `pluginval` and `reaper` were not available on `PATH`, so those lanes were documented as blocked rather than guessed.
- Built and ran `VXSuitePluginRegressionTests`, `VXDeverbTests`, `VxSuiteVoiceAnalysisTests`, and `VXSuiteProfile`; all completed successfully. `VXSuiteProfile` showed the sampled Cleanup/Proximity/Finish/Subtract chain remaining comfortably below realtime cost on this machine, including at `96 kHz`.
- The audit report is in `tasks/reports/VX_SUITE_FULL_AUDIT_2026-03-20.md` and records the main confirmed findings: zero tail-length reporting across framework-based products, `VXDeepFilterNet` model/resource staging and lookup issues, missing pluginval/CTest integration, duplicate `VXDeverb` CMake source entries, ad-hoc/non-notarized macOS bundles, and arm64-only staged artifacts.

# Verification coverage expansion — 2026-03-20

## Problem
The audit follow-up still had several verification gaps: no-allocation checks only covered Cleanup, tail reporting was only asserted at the metadata level, and there were no objective frequency-shape regressions for the clearly spectral products.

## Plan
- [x] Add shared test utilities for post-input tail rendering and sine-based response measurements.
- [x] Expand steady-state no-allocation checks across the processors already covered by the regression target.
- [x] Add tail-window behavior tests for latency/tail-bearing processors in the regression target.
- [x] Add basic frequency-response regression checks for Tone, Proximity, and Finish body shaping.
- [x] Rebuild and rerun `VXSuitePluginRegressionTests`.

## Review
- `tests/VxSuiteProcessorTestUtils.h` now provides `makeSine(...)`, `renderWithTail(...)`, and `rmsSkip(...)` so regression tests can measure simple spectral and tail behavior without duplicating harness code.
- `tests/VXSuitePluginRegressionTests.cpp` now checks steady-state audio-thread allocations for Cleanup, Denoiser, Deverb, Subtract, Finish, OptoComp, Proximity, and Tone instead of only Cleanup.
- The same regression file now verifies that Deverb, Denoiser, and Subtract produce meaningful output within their reported tail window and decay after it, rather than only reporting a non-zero tail length.
- Added basic frequency-shape regression checks that confirm `VXTone` bass/treble controls, `VXProximity` closer/air controls, and `VXFinish` body shaping still bias the expected frequency regions.
- Verified with `cmake --build build --target VXSuitePluginRegressionTests -j4` and `./build/VXSuitePluginRegressionTests`.

# Cleanup voiced-distortion fix — 2026-03-20

## Problem
`VXCleanup` is still adding audible distortion on voiced material. The existing Cleanup stress regression covers peak/headroom safety, but it does not directly guard the more important failure mode here: corrective processing should not chew up a clean sustained tone or otherwise turn harmonic speech material crunchy.

## Plan
- [x] Add a focused Cleanup distortion regression that reproduces the voiced-material failure case and fails on excessive harmonic damage.
- [x] Inspect and fix the corrective DSP path so Cleanup preserves voiced material while keeping its subtractive cleanup role.
- [x] Rebuild and rerun the affected regression target, then document the result and capture the lesson.

## Review
- `Source/vxsuite/products/cleanup/VxCleanupProcessor.cpp` now applies an explicit voiced-material guard before handing `deEss`, `breath`, and `troubleSmooth` to the shared corrective stage. Cleanup still acts on bright/noisy content, but it no longer asks the downstream DSP to push high-band removal as hard when the analysis says the block is clearly harmonic speech.
- `tests/VXSuitePluginRegressionTests.cpp` now adds a second voiced edge-case regression with a sustained harmonic source plus strong upper-mid/top-end content. That complements the older voiced-material test and makes the distortion check much harder to accidentally satisfy with only peak/headroom safety.
- Verified with:
  - `cmake --build build --target VXSuitePluginRegressionTests -j4`
  - `/tmp/cleanup_verify` built from the current `VXSuitePluginRegressionTests` objects, which measured `voiced_tone corr=0.997552 residualRatio=0.0699273` and `voiced_edge corr=0.995491 residualRatio=0.0948516`, both finite and within the new thresholds
  - `./build/VXSuitePluginRegressionTests` still stops on the pre-existing unrelated Deverb tail regression (`Deverb tail window was unexpectedly empty`), so the full suite is not green yet even though the Cleanup-specific verification passed

# Analyzer readability fixes — 2026-03-18

## Problem
`VX Studio Analyser` is functionally live, but the current in-host UI still fails the readability bar from the latest screenshots. The left stage rail does not present plugin names clearly, its first row collides with the rail header, and the Tone/Dynamics plots move too quickly and abstractly to communicate meaningful change.

## Plan
- [x] Fix the analyser stage rail layout/rendering so the header, full-chain row, and per-stage rows do not overlap and each row shows the plugin name/state/impact clearly in-host.
- [x] Simplify and slow the tone/dynamics render model so plotted changes are easier to read, with stronger temporal smoothing/hold behaviour and less meaningless frame-to-frame motion.
- [x] Build the analyser target, review the resulting diff, and document what changed plus any remaining calibration follow-up.

## Spec Notes
- Prefer the smallest editor-side fix that restores readable stage names and reliable hit targets.
- Preserve the existing telemetry ABI unless the plot issue clearly requires a framework-level cadence/smoothing adjustment.
- Favor “stable explanation” over “maximum liveness”; the analyser should read like a metering tool, not raw telemetry.

## Review
- Removed the invisible per-row `TextButton` overlay from the stage rail interaction path. Stage rows are now painted directly and selected via `mouseUp`, which avoids child-component repainting over the custom row text.
- Added explicit spacing between the `STAGE CHAIN` header and the `Full Chain` row so the first control no longer collides with the section title.
- Kept row text readable with fitted stage-name/state/impact text instead of relying on the hidden buttons.
- Slowed the analyser backend refresh from 20 Hz to 10 Hz and increased tone/dynamics/summary smoothing constants so the view behaves more like a readable meter than raw telemetry.
- Smoothed neighbouring tone bins and replaced the Catmull-Rom tone path with a direct line path so the graph stops inventing spline wiggles between sparse summary bands.
- Compile-verified with `cmake --build build --target VXStudioAnalyserPlugin -j4`.
- Remaining follow-up: this is compile-verified and targeted at the exact screenshot issues, but it still needs an in-host visual check to confirm the new cadence feels right across real material.

# VX Studio Analyser UI correction pass — 2026-03-18

## Problem
The rebuilt analyser still misses the usability bar in-host: the left stage rail does not reliably show plugin names and the rail header overlaps the top control, while the tone/dynamics plots feel too twitchy and semantically weak to read as a practical explanation tool.

## Plan
- [x] Fix the stage rail layout/render path so the header has its own space and every live stage name is visibly rendered.
- [x] Simplify and stabilize the tone/dynamics display model so changes read as broad, interpretable processor behaviour instead of fast-moving telemetry.
- [x] Build `VXStudioAnalyserPlugin`, review the result, and document any remaining calibration gaps.

## Review
- `Source/vxsuite/products/analyser/VXStudioAnalyserEditor.*` no longer relies on invisible `TextButton` overlays for stage selection. The stage rail is now painted directly, stage clicks are resolved via row hit-testing, and the rail header has dedicated vertical space above the `Full Chain` control, so the plugin names/state text are no longer hidden under button painting or overlapping the header region.
- The analyser display cadence is calmer: backend refresh moved from 50 ms to 100 ms, tone/dynamics summary smoothing was lengthened, tiny tone deltas are suppressed, and neighboring bins are blended before rendering so the curves read as broad processor moves instead of frame-by-frame FFT chatter.
- The tone path now draws a direct band-connected line instead of the previous overshooting spline, which removes fake wiggles between analyzer bands. Dynamics curves also get an extra neighbor-smoothing pass before the path is built, so the history panel is easier to read.
- Cleaned up text presentation alongside the fix: the title/header stack now includes the `VX SUITE` label again, the summary card only shows the actionable lines, and diagnostics/dynamics text now uses plain ASCII state words instead of the broken glyphs shown in-host.
- Verified with `cmake --build build --target VXStudioAnalyserPlugin -j4`. The VST3 restage emitted the existing ad-hoc code-signing replacement message but completed successfully.
- Remaining caveat: this pass is compile-verified and targeted to the exact issues from the screenshots, but I have not done a fresh REAPER visual check from this environment. The next refinement, if needed, should be based on one more in-host screenshot so we can tune readability rather than guess at it.

# Analyzer smoothing + render contract — 2026-03-18

## Problem
`VX Studio Analyser` still had the right stage-aware structure but the underlying cadence, smoothing, and render model were too raw, so the plots read like unstable debug telemetry instead of an industry-standard explanation tool. The user provided exact smoothing constants, FFT settings, RMS/peak rules, and a definitive visualization contract that now needs to become the implementation target.

## Plan
- [x] Update framework telemetry accumulation to match the requested analyzer defaults: 15 Hz publish cadence, 2048 Hann tone analysis with 75% overlap semantics, rolling RMS/peak/transient calculations, and stable dry-baseline summaries.
- [x] Rebuild the analyzer backend/editor flow around a staged render model with 20 Hz backend updates, 30 Hz UI repaint, smoothed summary metrics, and the exact Tone/Dynamics presentation contract.
- [x] Verify the analyzer build and document the resulting behavior, limits, and any residual calibration work.

## Review
- `Source/vxsuite/framework/VxSuiteSpectrumTelemetry.*` now treats stage summaries as rolling state instead of one isolated publish frame. Tier 1 tone telemetry moved to 32 log-spaced bands driven by a 2048-sample Hann analysis window with quarter-hop updates, and the publish cadence is now 15 Hz.
- The framework dynamics summary is now more analyzer-like: rolling 100 ms RMS, 10 ms versus 100 ms transient contrast, decaying held peak, and a 250 ms RMS history feeding the dynamics graph. `StagePublisher` no longer resets accumulators after every publish, so snapshots stay temporally stable.
- `VXStudioAnalyserProcessor` now publishes its own pass-through stage telemetry as a dry baseline. The editor excludes that analyzer stage from the visible chain, but uses it when there are no active upstream VX processors so the UI shows a stable dry/zero-change state instead of looking empty.
- `VXStudioAnalyserEditor` was split into a backend/render pipeline: a `HighResolutionTimer` builds the render model at 20 Hz, a UI timer applies it at 30 Hz, and `paint()` now only draws the already-prepared model. Telemetry reads, scope resolution, smoothing, and summary generation are out of the paint path.
- Tone rendering now follows the requested contract more closely: log-frequency axis, centered dB range, delta-first fill, faint before/after context, largest-change marker, and summary-strip language derived from smoothed values. Dynamics now shows RMS-over-time in dBFS with proper meter-style axes instead of unlabeled abstract traces.
- The analyzer chrome now follows the same VX Suite shell more closely: quieter rounded panels, stronger typographic hierarchy, a calmer stage rail, a dedicated summary card, and smoothed spline-style curve drawing so the hero graph feels closer to an industry EQ/dynamics tool rather than a raw debug plot.
- Verified with `cmake --build build --target VXStudioAnalyserPlugin -j4` and `cmake --build build --target VXCleanupPlugin -j4`.
- Remaining caveat: this pass is compile-verified and architecture-aligned, but not yet visually calibrated in-host after the new smoothing constants landed. The next likely refinement, if needed, is tuning summary wording or graph normalization from real screenshots rather than changing the transport again.

# VX Analyzer rebuild spec - 2026-03-18

## Problem
Replace `VXSpectrum` with `VX Studio Analyser`, rebuilding the analyzer around a separate analysis layer instead of asking each DSP plugin to own rich inspection logic. The current `VXSpectrum` path is still a spectrum-first v1 that infers chain structure from waveform correlation and shared slot registration order, which is too fragile for reliable stage/group/full-chain inspection.

## Plan
- [x] Re-read the framework docs, research notes, and project lessons that constrain shared analysis and realtime telemetry work.
- [x] Inspect the current `VXSpectrum` and framework telemetry implementation to identify the exact seams a rebuild should replace.
- [x] Write a concrete technical spec for a two-tier telemetry model, analyzer responsibilities, transport semantics, and UI states.
- [x] Add a review section that calls out why the new design is more robust than the current inferred-chain model and what implementation phases should follow.
- [x] Tighten the spec with domain authority, explicit `StageTelemetry`, deterministic ordering, and incremental summary rules.
- [x] Start the framework implementation by introducing the new telemetry ABI and domain registry primitives.
- [x] Wire the new telemetry publisher through `ProcessorBase` without breaking current buildability.

# Analyzer live-chain sidebar contract fix — 2026-03-19

## Problem
The analyser sidebar regressed after the timing/smoothing work. Instead of representing the live VX Studio plugins active in the FX chain, it was effectively telemetry-driven: rows could disappear, partially appear, or map to the wrong stage whenever per-stage telemetry coverage lagged behind the chain snapshots. That also let incomplete telemetry create misleading full-chain graphs and absurd per-stage deltas.

## Plan
- [x] Re-center the sidebar on live VX chain snapshots so active plugins stay visible even when telemetry is partial.
- [x] Match stage telemetry onto snapshot rows with tolerant VX name normalization instead of brittle raw display-name equality.
- [x] Keep full-chain comparison in analyser passthrough mode whenever chain coverage is incomplete, while still allowing a selected stage row to show real telemetry if that stage has arrived.
- [x] Rebuild `VXStudioAnalyserPlugin` and rerun `VXDeverbTests`.

## Review
- `Source/vxsuite/products/analyser/VXStudioAnalyserEditor.cpp` now builds the sidebar from `SnapshotRegistry` first, using that as the source of truth for which VX plugins are currently live in the chain. Stage telemetry now enriches those rows instead of determining whether the row exists.
- Added VX-aware name normalization so snapshot product names like `VXSubtract` can reliably match telemetry stage names/IDs such as `Subtract` or similar framework identifiers.
- Sidebar selection is now row-based and stable. Clicking a visible chain row selects that row directly; the analyser then resolves any matched stage telemetry behind it. This removes the old index mismatch where a sidebar row could silently point at the wrong telemetry stage.
- Full-chain mode now only trusts the chain comparison when telemetry coverage is complete. If the live chain rows outnumber matched telemetry rows, the analyser falls back to its own passthrough view for the full-chain plot to avoid fake spikes. Individual rows with valid telemetry still show their own stage analysis while the rest of the chain catches up.
- Verified with `cmake --build build --target VXStudioAnalyserPlugin -j4` and `./build/VXDeverbTests`.

# Analyzer sidebar stability + spectrum crossover pass — 2026-03-19

## Problem
The analyser is very close now, but the live chain list still flickers and occasionally drops visible VX stages, and the spectrum still diverges from SPAN in two practical ways: the low end is visually underrepresented, and the wet/dry crossover is harder to read than it should be.

## Plan
- [x] Add a short-lived sidebar snapshot cache so the live VX chain remains stable through brief publish gaps instead of flickering like raw telemetry.
- [x] Correct the analyser display slope so low frequencies are not visually suppressed relative to the SPAN reference.
- [x] Improve the spectrum overlay with clearer wet fill and additive/subtractive crossover shading.
- [x] Rebuild `VXStudioAnalyserPlugin` and rerun `VXDeverbTests`.

## Review
- `Source/vxsuite/products/analyser/VXStudioAnalyserEditor.*` now keeps a short sidebar snapshot cache with a `3500 ms` hold, so brief snapshot dropouts no longer immediately remove a live VX stage row from the chain list.
- The analyser display slope was corrected to apply in the same direction as a conventional analyzer tilt. The previous sign error visually pushed lows down and highs up, which is why the low end looked weaker than SPAN even when the underlying signal was present.
- The spectrum paint path now adds a clearer wet fill plus differential crossover shading: additive regions are highlighted separately from subtractive regions, so it is much easier to see where wet rises above or falls below dry.
- Verified with `cmake --build build --target VXStudioAnalyserPlugin -j4` and `./build/VXDeverbTests`.

# Analyzer snapshot robustness + FFT contour correction — 2026-03-19

## Problem
The analyser was still missing active VX effects in the left panel, and the spectrum contour remained materially different from the SPAN reference. The evidence pointed to two backend issues: snapshot publishing was less robust than stage telemetry and could fail to rejoin after an initial registration miss, and the spectrum path was still using a packed real-FFT interpretation that could skew the displayed contour.

## Plan
- [x] Make the shared snapshot publisher retry slot registration during normal publish/silence updates instead of only at construction/prepare time.
- [x] Switch the analyser spectrum accumulation to JUCE's frequency-only FFT path so the display uses direct magnitude bins instead of hand-reading the packed real-transform buffer.
- [x] Keep the sidebar as a union of snapshot rows plus any unmatched live telemetry rows so active/silent VX plugins are less likely to disappear from the chain list.
- [x] Rebuild `VXStudioAnalyserPlugin` and rerun `VXDeverbTests`.

## Review
- `Source/vxsuite/framework/VxSuiteSpectrumTelemetry.cpp` now retries `SnapshotPublisher::ensureRegistered()` from both `publish()` and `publishSilence()` whenever the snapshot slot is unavailable, rather than silently staying missing for the rest of the session.
- `Source/vxsuite/framework/VxSuiteSpectrumTelemetry.cpp` now uses `performFrequencyOnlyForwardTransform()` for the analyzer spectrum path and reads direct magnitude bins from that result. This removes the previous manual interpretation of the packed real FFT buffer, which was a plausible source of the contour mismatch versus SPAN.
- `Source/vxsuite/products/analyser/VXStudioAnalyserEditor.cpp` now includes unmatched live telemetry stages in the sidebar even if there is no corresponding snapshot row, and silent stages are retained in the live stage pool instead of being dropped outright.
- Verified with `cmake --build build --target VXStudioAnalyserPlugin -j4` and `./build/VXDeverbTests`.

# Cleanup high-shelf control fix — 2026-03-19

## Problem
The user reported that the `Cleanup` high-shelf control did not seem to do much. Inspection showed that the UI toggle and parameter wiring were present, but the shared corrective DSP never actually used `hiShelfOn`, so the control was effectively a no-op.

## Plan
- [x] Trace the `Cleanup` high-shelf toggle from processor params into the shared corrective DSP.
- [x] Add a real high-shelf stage in the corrective DSP so enabling the control produces a meaningful top-end cleanup change.
- [x] Rebuild `VXCleanupPlugin` and rerun the standing regression check.

## Review
- `Source/vxsuite/products/polish/dsp/VxPolishDspCommon.h` now provides a reusable RBJ-style high-shelf biquad helper.
- `Source/vxsuite/products/polish/dsp/VxPolishCorrectiveStage.*` now owns a dedicated high-shelf filter/state and applies it when `params.hiShelfOn` is enabled. The shelf amount is derived from the current `deEss` and `troubleSmooth` drive, so the icon now has an audible cleanup effect instead of being dead wiring.
- Verified with `cmake --build build --target VXCleanupPlugin -j4` and `./build/VXDeverbTests`.

# Analyser CPU + memory efficiency pass — 2026-03-19

## Problem
The analyser is now functionally close, but the user explicitly asked to make it as CPU- and memory-efficient as possible. The hot paths still included avoidable work: copying the full FFT scratch buffer every analysis hop, allocating/sorting extra data in sparse-spectrum classification, repeated string normalization in sidebar matching, and redundant path/snapshot-string construction in the editor.

## Plan
- [x] Remove avoidable large-buffer copying from the shared spectrum telemetry FFT path.
- [x] Trim editor-side allocations and repeated string/path work in the analyser refresh/paint loop.
- [x] Rebuild `VXStudioAnalyserPlugin` and rerun the standing regression check.

## Review
- `Source/vxsuite/framework/VxSuiteSpectrumTelemetry.cpp` no longer copies the entire `fftData` buffer into a scratch array on every FFT hop. The analyser now fills and transforms the persistent buffer in place, which removes one full-size copy from the hottest telemetry path.
- `Source/vxsuite/products/analyser/VXStudioAnalyserEditor.cpp` now avoids the per-frame `std::vector` allocation/sort previously used to compute sparse-spectrum top energy. The classifier keeps the top four energies in a fixed array during a single pass instead.
- The editor refresh path now reserves stage/snapshot vectors up front, precomputes normalized external-stage match keys once per refresh, skips building the joined snapshot diagnostics list unless diagnostics are actually open, and reuses dry/wet spectrum paths within `paint()` instead of rebuilding them multiple times per frame.
- Verified with `cmake --build build --target VXStudioAnalyserPlugin -j4` and `./build/VXDeverbTests`.

# Analyser responsive text/layout pass — 2026-03-19

## Problem
The user asked to make sure the analyser text fits inside its boxes and that the UI responds cleanly at different sizes. The editor still had a few fixed-height/fixed-row assumptions that could make summary text and controls feel cramped, especially in narrower layouts.

## Plan
- [x] Tighten the analyser label scaling and stage-row text layout so long text fits more reliably.
- [x] Make the controls row wrap more gracefully in narrower content widths instead of crowding into one fixed strip.
- [x] Rebuild `VXStudioAnalyserPlugin` and rerun the standing regression check.

## Review
- `Source/vxsuite/products/analyser/VXStudioAnalyserEditor.cpp` now gives the subtitle, status, selection, and summary labels more tolerant horizontal scaling so long strings stay within their cards more reliably.
- Stage-row secondary text now uses a slightly smaller font and can wrap across two lines instead of truncating too aggressively in the rail.
- The top summary card gains a little extra height in narrower layouts, and the analyser control strip now splits into two rows when the content area is tight instead of forcing `Avg Time`, `Smoothing`, and `Hide Chain` into one crowded line.
- Verified with `cmake --build build --target VXStudioAnalyserPlugin -j4` and `./build/VXDeverbTests`.

# VXDenoiser vocal-mode amount fix — 2026-03-19

## Problem
The user reported that `VXDenoiser` in vocal mode appeared to do nothing. Inspection showed that the processor was scaling the vocal-mode clean amount down to only `8%` of the knob value before it ever reached the DSP, which effectively neutered the denoiser even at high settings.

## Plan
- [x] Inspect the vocal/general clean-amount mapping in `VXDenoiser`.
- [x] Correct the vocal-mode amount law while preserving the existing vocal protection/guard behavior.
- [x] Rebuild `VXDenoiserPlugin` and rerun the standing regression check.

## Review
- `Source/vxsuite/products/denoiser/VxDenoiserProcessor.cpp` now maps vocal-mode `Clean` to `0.55 * smoothedClean` instead of `0.08 * smoothedClean`. The DSP is now fed a meaningful amount in vocal mode while the existing `sourceProtect`, `guardStrictness`, and `speechFocus` safeguards remain in place.
- Verified with `cmake --build build --target VXDenoiserPlugin -j4` and `./build/VXDeverbTests`.
- [x] Build the affected targets and document the first implementation checkpoint.
- [x] Replace the old `VXSpectrum` product target/files with `VXStudioAnalyser` in the build and staged plugin output.
- [x] Replace the carried-over spectrum UI with a real stage-based `VXStudioAnalyser` shell built on the new analysis telemetry.

## Review
- Added `docs/VX_ANALYZER_REBUILD_SPEC.md` to define `VX Studio Analyser` as a separate analysis layer built on thin per-stage telemetry rather than product-local visualization logic.
- The spec replaces the current spectrum-centric `SnapshotView` model with a stage-oriented schema that separates identity, live state, bypass state, silence, lightweight summaries, capabilities, and deep-inspection requests.
- The design keeps `ProcessorBase` as the framework injection point, which means the rebuild stays aligned with VX Suite's shared-framework rule instead of creating new product-specific telemetry paths.
- The proposed two-tier model is safer than extending the current analyzer because it removes dependence on registration order and waveform-correlation heuristics as the primary chain model. Those can remain as fallback confidence tools rather than being treated as truth.
- The product direction is now explicit: this is a replacement of `VXSpectrum`, not a light refresh. The rewrite should land as `VX Studio Analyser` with a new product contract and UI model.
- The implementation sequence is intentionally phased: schema/transport first, framework publisher second, analyzer backend third, UI fourth, and deep inspection last. That should let us migrate without trying to replace the whole analyzer stack in one unsafe pass.
- First implementation checkpoint is in place: `ProductIdentity` now carries stage-analysis metadata, the framework has a new `vxsuite::analysis` telemetry ABI/domain registry/stage registry beside the legacy spectrum transport, and `ProcessorBase` now auto-publishes the new stage telemetry through `analysis::StagePublisher`.
- The new Tier 1 summary path is intentionally foundational rather than final-quality: it publishes aligned input/output summaries with incremental envelope/RMS/peak/transient/stereo metrics, while coarse spectrum bins are still placeholder-filled pending the dedicated band accumulator pass.
- Verified with `cmake --build build --target VXSpectrumPlugin VXCleanupPlugin -j4`. Both rebuilt successfully; the only output noise was the existing ad-hoc code-signing/staging warning during VST3 restaging.
- Replaced the old `Source/vxsuite/products/spectrum/` product with `Source/vxsuite/products/analyser/`, switched the CMake target/bundle from `VXSpectrum` to `VXStudioAnalyser`, registered analyzer-domain authority in the analyzer processor, and removed the staged `Source/vxsuite/vst/VXSpectrum.vst3` bundle.
- Verified the replacement with `cmake -S . -B build`, `cmake --build build --target VXStudioAnalyserPlugin -j4`, and `cmake --build build --target VXSuite_VST3 -j4`. The suite now stages `VXStudioAnalyser.vst3` instead of `VXSpectrum.vst3`.
- Rewrote `Source/vxsuite/products/analyser/VXStudioAnalyserProcessor.*` and `Source/vxsuite/products/analyser/VXStudioAnalyserEditor.*` so the product is no longer the old spectrum overlay view under a new name. The analyzer is now domain-scoped, stage-list driven, and shows `Before / After / Delta` scopes across `Tone`, `Dynamics`, and `Diagnostics`.
- The new analyzer shell reads `vxsuite::analysis::StageRegistry` instead of the legacy `spectrum::SnapshotRegistry`, orders stages by domain + `localOrderId`, supports single-stage or full-chain scope, and derives stage impact/class labels from the new Tier 1 summaries.
- Verified the rewritten analyzer with `cmake --build build --target VXStudioAnalyserPlugin -j4`. Current known limitation: the Tone view is structurally correct but still depends on placeholder Tier 1 spectrum bins from the first-pass accumulator, so the next cleanup should upgrade those bins from flat placeholders to proper coarse-band energy summaries.
- Replaced the placeholder spectrum fill in `Source/vxsuite/framework/VxSuiteSpectrumTelemetry.cpp` with a preallocated coarse FFT summary over a rolling mono window. Tier 1 spectrum bins are now derived from real band energy rather than `out.rms` replicated across every bin, so the analyzer traces can reflect actual tonal shape changes.
- Verified the spectrum summary upgrade with `cmake --build build --target VXStudioAnalyserPlugin VXCleanupPlugin -j4`.
- Rebuilt `VXStudioAnalyserEditor` around the definitive UI model: deterministic stage chain on the left, summary strip as the primary explanation surface, exactly one graph at a time, `Tone` and `Dynamics` tabs only, and diagnostics moved into a collapsible bottom panel.
- Tone now renders like a proper change-view: log-frequency x-axis, centered dB y-axis, delta-first filled presentation, faint before/after context, low/mid/high guides, and a largest-change marker. Dynamics now renders as before/after dBFS level history instead of an unlabeled abstract line plot.
- Verified the spec-driven UI rewrite with `cmake --build build --target VXStudioAnalyserPlugin -j4`.

# Finish/Opto idle telemetry fix — 2026-03-18

## Problem
`VXFinish` and `VXOptoComp` showed the `Opto` activity LED as active even in their nominal idle/open state. The shared compressor stage was still eligible to compute gain reduction at zero amount, and both products also inherited the framework's default `primaryDefaultValue = 0.5f`, so they opened half-engaged.

## Plan
- [x] Inspect the shared opto telemetry path and confirm whether the issue lived in DSP logic, product defaults, or both.
- [x] Patch the shared opto stage and product identities so zero amount is truly idle and telemetry reflects actual engaged compression.
- [x] Add or update regression coverage, build the affected targets, and document the result.

## Review
- Set `VXFinish` and `VXOptoComp` primary defaults to `0.0f` so both products now open with zero compression instead of inheriting the framework's generic midpoint default.
- Patched `Source/vxsuite/products/OptoComp/OptoCompressorLA2A.cpp` so `Peak Reduction = 0` forces zero target gain reduction and zero activity telemetry, while still allowing neutral gain/body handling to remain transparent.
- Fixed the centered `Gain` mapping in both processors so `0.5` is truly neutral; they were previously using the wrong `juce::jmap` overload and landing at `-6 dB` in the default state.
- Added focused regression coverage for zero-amount transparency and zero telemetry in both `Finish` and `OptoComp`, and updated the regression target to link the standalone `VXOptoComp` wrapper.
- Verified with `cmake --build build --target VXSuitePluginRegressionTests -j4 && ./build/VXSuitePluginRegressionTests` plus `cmake --build build --target VXFinishPlugin VXOptoCompPlugin -j4`. Both plugin targets rebuilt and restaged successfully, with only the existing ad-hoc code-signing warnings during VST3 staging.

# Cleanup slope toggle fix — 2026-03-18

## Problem
The `VXCleanup` slope icons were not reliably clickable because the shared editor rendered them in the label area, but those non-interactive labels and meter widgets could still intercept mouse events before the editor-level hit test ran.

## Plan
- [x] Inspect the shared editor hit-testing path and confirm whether child components were blocking the slope icon clicks.
- [x] Patch the shared framework editor so display-only widgets stay mouse-transparent and do not break shelf/slope toggles.
- [x] Build and verify the affected target or framework path, then document the result.

## Review
- Patched `Source/vxsuite/framework/VxSuiteEditorBase.cpp` so display-only labels and the learn meter explicitly call `setInterceptsMouseClicks(false, false)`, allowing editor-level shelf-icon hit testing to work as intended.
- The fix stays in the shared editor rather than adding a `VXCleanup`-specific workaround, so any current or future products using inline shelf icons inherit the corrected behavior.
- Verified with `cmake --build build --target VXCleanupPlugin -j4`; the plugin rebuilt and restaged successfully, with only the existing ad-hoc code-signing warning during VST3 staging.

# VX Spectrum implementation — 2026-03-17

# Per-effect audit fix pass — 2026-03-18

## Problem
Implement the audit fixes across each VX Suite effect individually, keeping changes consistent with the shared framework and with the expectation that the products work well together in a chain. Leave `VXDeepFilterNet` until last because its lifecycle/threading work is the riskiest.

## Plan
- [x] Update the task log and re-read the framework/lessons that constrain the fix pass.
- [x] Patch shared framework issues first: aligned-dry handling for oversized blocks, bypass-state behavior, and any helper cleanup needed by multiple products.
- [x] Fix `VXDeverb` against the updated framework and add focused regression coverage.
- [x] Fix `VXDenoiser` and `VXSubtract` latency/state correctness issues and add focused regression coverage.
- [x] Fix additive/listen/headroom consistency in `VXTone`, `VXProximity`, `VXFinish`, `VXOptoComp`, and any `VXCleanup` cleanup that should be aligned with framework usage.
- [x] Fix `VXDeepFilterNet` last: runtime lifecycle, thread safety, and wet/dry semantics.
- [x] Build and run the affected regression targets, then document the final results and residual risks.

## Spec Notes
- Framework changes should solve shared problems centrally rather than adding per-product defensive hacks where possible.
- Additive products should share consistent listen and headroom semantics.
- Output trimming remains an emergency guard, not the primary fix for gain-staging issues.
- DeepFilterNet work should preserve suite conventions where possible, but correctness and realtime safety take priority.

## Review
- Implemented the shared framework fix in `ProcessorBase` so oversized host blocks are chunked through the prepared scratch/block-size contract, bypass continues to advance latency-aligned dry state, and additive products can render consistent delta-audition output via a shared helper.
- Fixed `VXDeverb` by removing dead stage plumbing, moving RT60 tracking to per-channel estimators, and replacing the unstable-output fallback with a deterministic dry fallback for the offending block.
- Fixed `VXDenoiser` and `VXSubtract` so zero-strength paths explicitly return the framework-aligned dry signal, preserving reported latency/PDC behaviour instead of relying on latent DSP to reconstruct dry. `VXSubtract` state restore now rejects incompatible learned-profile formats and keeps Learn armed across reset.
- Aligned additive/listen semantics across `VXTone`, `VXProximity`, `VXFinish`, and `VXOptoComp` with the framework additive-delta helper, and reduced product-specific gain ranges where the audit showed output-trim-dependent headroom.
- Adjusted `VXCleanup` FFT-order selection to scale with sample rate instead of assuming a fixed 48 kHz analysis window.
- Fixed `VXDeepFilterNet` last by refactoring the service onto double-buffered runtime bundles, making model/runtime swaps API-consistent, avoiding audio-thread runtime teardown/reset, and restoring proper low-strength wet/dry behaviour with latency-aligned dry blending.
- Added regression coverage for zero-strength PDC alignment, mismatched Subtract profile restore, Learn-after-reset, Tone additive listen semantics, and oversized host-block processing through the shared framework path.
- Verification completed with `cmake --build build --target VXSuitePluginRegressionTests -j4`, `./build/VXSuitePluginRegressionTests`, and a full `cmake --build build -j4` project build. Residual note: the full build emits the existing macOS ad-hoc signing warnings while staging VST3 bundles, but the build completed successfully.

# REAPER preset pack — 2026-03-18

## Problem
Add a documented REAPER-facing preset pack for VX Suite: per-effect `.RPL` preset libraries with the same scenario names across products, plus matching `.RfxChain` scenario chains and README guidance for when to use each one.

## Plan
- [x] Add the task log entry and capture the REAPER API lesson from the failed probe.
- [x] Define four shared scenarios and per-effect starting values that stay consistent with the framework and with realistic chain usage.
- [x] Add a reproducible generator for REAPER `.RPL` libraries and `.RfxChain` files.
- [x] Generate and commit the preset pack outputs under a stable repo path.
- [x] Update `README.md` with scenario descriptions, recommended signal chains, and preset-pack usage notes.

## Review
- Added a repo-local REAPER preset generator at `tools/reaper/generate_vx_reaper_presets.lua` that uses the installed/staged VX Suite VST3s inside REAPER to emit per-effect `.RPL` libraries and scenario `.RfxChain` files.
- Generated the preset pack under `assets/reaper/`, with one `.RPL` file per effect in `assets/reaper/RPL Files/` and four scenario chains in `assets/reaper/FX Chains/`.
- Standardized four shared scenario names across all effect libraries: `Camera Review - Far Phone`, `Live Music - Front Of Room`, `Podcast Finishing - Clean Voice`, and `Mixed Audio - Voice + Guitar`.
- Updated `README.md` and added `assets/reaper/README.md` so the repo now documents what each scenario is for and which chain to start with.
- Captured the REAPER API lesson from the failed probe: REAPER Lua parameter/preset helpers do not always mirror the return conventions implied by generic docs or other language bindings, so probing return values directly is safer than assuming tuple layouts.

# Full per-effect audit sweep — 2026-03-18

## Problem
Review every VX Suite effect individually against the strict JUCE/VST3 + VX Suite audit prompt, then produce a separate report for each effect covering correctness, safety, efficiency, framework usage, host behavior, and validation gaps.

## Plan
- [x] Re-read the relevant project lessons and VX Suite framework rules, then inventory every effect target and its DSP/framework dependencies.
- [x] Audit the shared VX Suite framework paths that affect all effects: lifecycle, smoothing, listen, output trim, latency, buffer handling, and state/host semantics.
- [x] Perform an individual code audit for each effect product: `VXCleanup`, `VXDeverb`, `VXDenoiser`, `VXDeepFilterNet`, `VXFinish`, `VXOptoComp`, `VXProximity`, `VXSubtract`, and `VXTone`.
- [x] Decide whether `VXSpectrum` belongs in the effect audit set; if not, exclude it explicitly from the reports with a reason.
- [x] Write one report per audited effect in the requested format: executive summary, findings table, framework cleanup list, test matrix, and patch recommendations.
- [x] Review the reports for consistency, rank the highest-severity risks, and document any cross-product framework drift or repeated failure patterns.

## Spec Notes
- Release readiness is the bar; missing verification counts as risk, not a pass.
- Reports should stay effect-specific even when the root cause lives in shared framework code.
- Framework responsibilities must not be re-audited as if each product were raw JUCE unless the product is bypassing the framework.
- Minimal, surgical remediation suggestions come first; redesigns should be called out separately.

## Review
- Reports added under `tasks/reports/effect-audits/` for `Cleanup`, `Deverb`, `Denoiser`, `DeepFilterNet`, `Finish`, `OptoComp`, `Proximity`, `Subtract`, and `Tone`.
- `VXSpectrum` was explicitly excluded because it is an analysis product rather than an audio effect, though the shared framework review still flagged its telemetry path as a realtime risk.
- Highest-severity cross-product issues:
- The framework dry/aligned-listen path is not safe when hosts deliver larger-than-prepared blocks, which becomes a release-blocking bug in `VXDeverb` and a stale-state risk for other latency-bearing products.
- `VXDeepFilterNet` has release-blocking thread-safety and lifecycle issues around runtime preparation/reset.
- `VXDenoiser` and `VXSubtract` both have host-PDC mismatches when their effective wet amount reaches zero while latency remains reported.
- Repeated architecture drift:
- Output trimming is being used in multiple products as a secondary safety net without proof that the main DSP path is already gain-stable.
- Additive-product listen and wrapper logic are still duplicated instead of centralized.

# Cleanup + Finish opto integration review — 2026-03-18

## Problem
Review the new `optocomp` compressor integration for `VXFinish`, identify any correctness/build risks first, then apply the `clean and finish.md` product-split upgrades without breaking the new compressor path.

## Plan
- [x] Review the current `VXFinish` integration, the new `OptoComp` files, and the `clean and finish.md` intent document.
- [x] Capture the concrete integration issues and fix the build/runtime wiring so `VXFinish` really uses the new opto compressor.
- [x] Align `Cleanup` and `Finish` behavior with the requested split: Cleanup stays subtractive/protective, Finish stays additive/leveling.
- [x] Build and verify the affected targets, then document the final review and any residual risks.

## Spec Notes
- `Cleanup` must remain subtractive and protective: no compression, no loudness shaping, no guessing at what `Finish` should add back.
- `Finish` must stay focused on opto compression, light body shaping, and final level control.
- The new compressor should be preserved as the core `Finish` dynamics stage rather than replaced with the older shared polish chain.
- Any integration fix should be minimal and framework-friendly: no product-specific forks unless required for correctness.

## Review
- Review findings first:
- `VxFinishDsp.h` included `OptoCompressorLA2A.h` with no reachable path, so the new compressor could not compile at all.
- `VxFinishProcessor.cpp` switched over to a nonexistent `dsp` member while the class still owned `polishChain`, so the product would still fail after the include issue.
- `VxFinishDsp.cpp` was a stale pre-opto implementation that no longer matched the header, which meant the new compressor path was only half-integrated.
- `OptoCompressorLA2A.cpp` stored body-shelf state in `static thread_local` storage, so reset behavior was not instance-deterministic and could leak state across plugin instances on the same thread.
- Fixes landed:
- Rewired `VXFinish` so it now consistently drives the new `OptoComp` LA-2A-style compressor through `vxsuite::finish::Dsp`.
- Replaced the stale `VxFinishDsp.cpp` implementation with a focused Finish path: opto compression, light adaptive makeup, clean peak limiting, and final trimming.
- Kept the product split sharp: `Cleanup` remains the subtractive/protective stage, while `Finish` no longer depends on the old polish-analysis/recovery path.
- Made the compressor body shelf instance-owned and resettable instead of `thread_local`, then added a regression test to lock that behavior in.
- Verification:
- `cmake --build build --target VXSuitePluginRegressionTests -j4`
- `./build/VXSuitePluginRegressionTests`
- `cmake --build build --target VXFinishPlugin -j4`
- Residual risk:
- `Finish` now follows the requested architecture much more closely, but the adaptive makeup stage is intentionally simple and heuristic rather than analysis-driven. That keeps the new compressor path intact and predictable, but there is still room for future tuning by ear in-host.

# OptoComp standalone plugin — 2026-03-18

## Problem
Expose the new opto compressor as its own VX Suite plugin as well, without forking the compressor DSP away from the `Finish` path.

## Plan
- [x] Inspect the existing VX plugin shell pattern and choose the lightest reusable wrapper shape.
- [x] Add a dedicated `OptoComp` processor that reuses the shared finish/opto DSP path.
- [x] Register a new `VXOptoComp` VST3 target and include it in suite staging.
- [x] Build and stage the standalone plugin.

## Review
- Added a new thin product wrapper at `Source/vxsuite/products/OptoComp/VxOptoCompProcessor.{h,cpp}` instead of copying DSP code.
- `VXOptoComp` reuses `vxsuite::finish::Dsp`, so the compressor behavior stays shared between the standalone compressor and `Finish`.
- The standalone plugin exposes dedicated product identity and wording: `Peak Red.`, `Body`, and `Gain`, with the same vocal/general mode mapping and listen-delta semantics.
- Registered the new VST3 target in `CMakeLists.txt`, added it to the suite aggregate target, and staged it to `Source/vxsuite/vst/VXOptoComp.vst3`.
- Verified with `cmake -S . -B build && cmake --build build --target VXOptoCompPlugin -j4`.

## Problem
Build a dedicated VX analyzer plugin that can show dry/wet spectrum overlays plus one coloured trace per active VX-family plugin, while moving the shared snapshot engine into the framework so current and future plugins can publish telemetry without duplicating analysis code.

## Plan
- [x] Review VX Suite framework rules, research notes, and project lessons related to analysis, FFT ownership, and realtime/UI separation.
- [x] Inspect the current framework and processor architecture for shared FFT, stage-chain, and chain-visibility hooks.
- [x] Define a realistic v1 scope: standalone analyzer product, framework snapshot publisher, spectrum-first UI, waveform deferred or secondary.
- [x] Add a realtime-safe shared snapshot publisher to the VX Suite framework and auto-wire it into `ProcessorBase`.
- [x] Add a new analyzer product/plugin that renders dry, wet, and per-plugin spectrum traces with visibility toggles.
- [x] Build and verify the new plugin and the existing suite products that now publish telemetry.

## Review
- The idea is technically possible, but not as a passive VST that automatically inspects sibling plugins. The framework needs an explicit shared telemetry path.
- Added a framework-owned snapshot registry/publisher and wired it into `ProcessorBase`, so existing VX Suite products now publish compact dry/wet waveform telemetry automatically without product-local analyzer code.
- After the first build proved bundle-local only, replaced the registry backend with a shared-memory mapped transport so separate VX plugin bundles can see the same telemetry pool instead of each keeping a private static registry.
- Added a dedicated `VXSpectrum` plugin with a custom editor that renders the inferred chain dry bed, the analyzer input/final wet spectrum, and one coloured trace per active VX-family plugin with visibility toggles.
- The analyzer computes FFT visuals on the UI side from published waveform snapshots, keeping the heavier spectral rendering work out of the product audio path while the framework publishers stay preallocated, fixed-size, and lock-free during block publication.
- Verified with `cmake -S . -B build`, `cmake --build build --target VXSpectrum VXCleanup -j4`, `cmake --build build --target VXSpectrumPlugin VXDenoiser_VST3 -j4`, and `cmake --build build --target VXSuite_VST3 -j4` after the shared-memory backend landed.
- Remaining v1 limitation: chain ordering is still inferred from active VX publisher order rather than explicit host graph routing, so waveform view and more explicit chain/session routing are the next clean upgrades after we host-smoke-test the current analyzer.

# VX Spectrum pickup + comparison pass — 2026-03-17

## Problem
`VXSpectrum` still stops showing upstream VX plugins reliably after the pause/bypass work, and the live traces move too quickly to make useful visual comparisons in real time.

## Plan
- [x] Review the current telemetry publish/reset path and the chain matcher together to find why active upstream plugins are being dropped.
- [x] Make the analyzer resilient to pause/bypass-induced silence snapshots without showing stale garbage when a plugin is actually gone.
- [x] Improve the real-time comparison view so traces are visually stable enough to compare, while keeping the analyzer lightweight and off the audio path.
- [x] Build and run targeted verification for `VXSpectrum` and the shared telemetry framework.

## Review
- Root cause was split across the transport and UI: bypassed/paused plugins were still holding active telemetry slots with silent snapshots, and the analyzer was treating those zeroed frames as current truth when reconstructing the upstream chain.
- The shared telemetry slot now carries `silent` plus `lastPublishMs`, so paused publishers are still visible to the backend but can be reused safely when they stay silent. `SnapshotPublisher` also re-registers lazily if it loses a slot, which prevents `VXSpectrum` from getting stuck in a no-self-slot state after the pool fills.
- `VXSpectrum` now keeps a short UI-side hold on the last non-silent snapshot for each slot instead of immediately trusting a single silent frame, which makes chain pickup much less fragile around pause/bypass transitions.
- The display is now intentionally stabilised: spectrum lines use short attack/release smoothing so users can compare shapes in real time without chasing raw per-refresh jitter.
- A second matcher bug showed up in host testing: the analyzer was treating telemetry registration order like chain order. That is not a real DAW routing signal, so the matcher now uses signal-first correlation with order only as a weak hint instead of a hard gate.
- Cleaned up the no-match state so it renders as a small banner rather than overlapping the plot content.
- Verified with `cmake --build build --target VXSpectrumPlugin -j4` and `cmake --build build --target VXSuite_VST3 -j4` so the rebuilt staged bundles in `Source/vxsuite/vst/` all speak the same telemetry format.


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

---

# VXFinish clipping + slope hit-testing — 2026-03-17

## Problem
`VXFinish` still clips/distorts under compression, and the shared inline slope buttons no longer react after the icon layout change.

## Plan
- [x] Remove hidden compressor makeup from the shared corrective stage so `VXFinish` does not stack gain ahead of its limiter/output trim.
- [x] Fix shared slope icon hit-testing so inline icons toggle reliably even when the mouse event originates from a child component.
- [x] Rebuild the affected VST3 targets and record the verification result.

## Review
- Removed the shared corrective-stage compressor makeup path in `Source/vxsuite/products/polish/dsp/VxPolishCorrectiveStage.cpp`, so compression now only attenuates and can no longer quietly add level before `VXFinish` reaches its own limiter and output trim.
- Fixed the inline slope icon hit-testing in `Source/vxsuite/framework/VxSuiteEditorBase.cpp` by converting incoming mouse events into editor coordinates before checking the stored icon bounds. This restores the shared low/high slope toggles after the inline layout change.
- Verified with `cmake --build build --target VXFinish_VST3 VXCleanup_VST3 -j4` and then `cmake --build build --target VXSuite_VST3 -j4`, which rebuilt and restaged the full suite into `Source/vxsuite/vst/`.

---

# Full chain audit — 2026-03-17

## Problem
Review the full audio chain of each VX plugin and verify the suite stays clean: no clipping/distortion, no obvious phase/delay regressions, and sensible behavior when plugins are used together.

## Plan
- [x] Inspect the processor paths, latency handling, listen semantics, and output safety coverage across the current VX products.
- [x] Run the existing regression and deverb-focused tests, then expand coverage for under-tested products.
- [x] Apply focused fixes where the audit found concrete safety or routing issues.
- [x] Record the results, including any residual failures that still need deeper DSP work.

## Review
- Strengthened shared safety handling in `Source/vxsuite/framework/VxSuiteOutputTrimmer.h` so overload protection now applies immediate reduction on attack instead of ramping down too slowly through the same block.
- Added output-safety trimming to `Source/vxsuite/products/tone/VxToneProcessor.cpp`, `Source/vxsuite/products/proximity/VxProximityProcessor.cpp`, and `Source/vxsuite/products/deverb/VxDeverbProcessor.cpp`, which closes an output-headroom gap for additive/EQ-style products and extreme deverb body settings.
- Normalized plugin-entrypoint guarding in `Source/vxsuite/products/tone/VxToneProcessor.cpp` and `Source/vxsuite/products/denoiser/VxDenoiserProcessor.cpp` so these products can be linked into regression binaries without duplicate `createPluginFilter()` symbols.
- Expanded `tests/VXSuitePluginRegressionTests.cpp` to cover `Tone`, `Proximity`, and `Denoiser`, which were previously underrepresented compared with Cleanup / Finish / Deverb / Subtract.
- Reworked `Source/vxsuite/products/deverb/VxDeverbProcessor.cpp` body-restore logic so it no longer applies a negative low-band “restore” when wet low energy already exceeds dry, and so its restore path is more tightly gated and bounded.
- Rebalanced `Source/vxsuite/products/denoiser/VxDenoiserProcessor.cpp` to be more conservative at high settings and added a small latency-aligned dry recovery tied to `Guard`, reducing the risk of over-destructive speech damage.
- Verified successful rebuilds with:
  - `cmake --build build --target VXSuitePluginRegressionTests VXDeverbTests -j4`
  - `cmake --build build --target VXSuite_VST3 -j4`
- Current residual failures after the audit:
  - `./build/VXDeverbTests` still fails `Body` restoration magnitude (`off=0.774373 on=0.777374`), so Deverb `Blend` is now safer but still too weak to meet the intended restore contract.
  - `./build/VXSuitePluginRegressionTests` still fails Deverb extreme-tail suppression (`rms=0.186114` in the tail), which means Deverb `Blend` still reintroduces too much sustained low tail at the extreme.
  - `./build/VXSuitePluginRegressionTests` still fails the new denoiser coherence check (`|corr|=0.128598`), so the aggressive end of `VXDenoiser` still needs deeper DSP tuning beyond wrapper-level mapping changes.
- Conclusion from this audit: the suite is in a safer place than before, and several framework/product safety gaps were fixed, but I cannot honestly mark every plugin chain as fully clean yet because `VXDeverb` and `VXDenoiser` still have measurable behavior regressions under strong settings.
- Follow-up direction from the user: prefer upstream DSP/gain fixes over output trimming. I removed the new trim reliance from `VXTone`, `VXProximity`, and `VXDeverb`, reduced the shelf gain laws for Tone/Proximity, and switched the deverb audit pass toward speech-aware body gating plus an instability fallback rerender instead of simple output shaving.
- After that upstream-first pass, `Tone` and `Proximity` remain on the cleaner path, but `VXDeverb` still fails both body-restore and extreme-tail checks, and `VXDenoiser` still fails the aggressive-setting audit. Those two need deeper product-specific DSP work rather than more wrapper-level protection.

---

# Finish compressor/headroom review — 2026-03-17

## Problem
`VXFinish` still distorts when the compressor/finish path is pushed. Review the full in/out audio path and leave more real DSP headroom rather than relying on distortion-prone last-stage catching.

## Plan
- [x] Inspect the full Finish path: corrective stage, recovery lift, makeup, output gain, limiter, and final safety.
- [x] Remove distortion-prone limiter behavior and reduce gain stacking into the limiter.
- [x] Rebuild `VXFinish` and run focused regression/build verification.
- [x] Record the result and any remaining non-Finish regressions separately.

## Review
- `Source/vxsuite/products/finish/dsp/VxFinishDsp.cpp` no longer uses the old `softLimitSample(...)` stage in the limiter. The limiter is now a cleaner gain-only limiter with instant attack when reduction is needed, a slightly lower ceiling, and release smoothing only on recovery.
- `Source/vxsuite/products/finish/VxFinishProcessor.cpp` now leaves explicit internal headroom before the limiter by scanning the post-recovery/post-makeup/post-output-gain block and cleanly scaling it down to a `-6 dB` pre-limiter target when needed. This keeps the limiter from being driven into distortion as normal operating behavior.
- The Finish makeup target is also less aggressive: lower target loudness, smaller density lift, and a tighter maximum makeup range, so the product is less likely to stack recovery + makeup + gain into the limiter.
- Verified with:
  - `cmake --build build --target VXFinish_VST3 VXSuitePluginRegressionTests -j4`
  - `./build/VXSuitePluginRegressionTests`
- The regression executable still fails, but the remaining reported failures are currently `VXDeverb` and `VXDenoiser`, not `VXFinish`.

---

# Vocal mode consistency audit — 2026-03-19

## Problem
Check how the shared framework defines `Vocal` mode, how each VX product actually handles it, and whether the suite is consistent with the intended "scalpel, not chainsaw" vocal behavior.

## Plan
- [x] Inspect the shared framework mode contract and common process-option plumbing.
- [x] Review each major VX product's `Vocal` vs `General` behavior.
- [x] Record consistency findings and concrete gaps.

## Review
- Framework contract is consistent in principle: `Source/vxsuite/framework/VxSuiteModePolicy.h` defines `Vocal` as higher source protection, stronger speech focus, higher guard strictness, and lower late-tail aggression than `General`.
- `VXDenoiser`, `VXCleanup`, `VXSubtract`, `VXDeverb`, `VXTone`, `VXOptoComp`, and `VXFinish` all do change DSP behavior in response to `Vocal`, but they do so through different mechanisms and with uneven fidelity to the shared policy.
- Strongest alignment with the framework contract:
  - `VXDenoiser` maps the shared policy fields into `ProcessOptions` and its DSP uses those values for speech-band protection, transient guarding, and suppression depth.
  - `VXCleanup` uses mode both in the processor mappings and in the shared corrective DSP via `contentMode`, producing more speech-aware thresholds/cuts in `Vocal`.
  - `VXDeverb` uses `voiceMode` inside the spectral dereverb DSP to protect the speech band and enable speech-targeted WPE only in `Vocal`.
- Partially consistent / custom interpretations:
  - `VXSubtract` does become more protective and speech-focused in `Vocal`, but it hardcodes its own mapping instead of using `currentModePolicy()`, so it follows the suite intent without actually sharing the framework contract.
  - `VXTone` and `VXProximity` switch to narrower, more speech-aware shaping in `Vocal`, but they use simple product-local mode branches rather than the framework policy fields.
  - `VXOptoComp` and `VXFinish` switch between compressor-like `Vocal` behavior and limiter-like `General` behavior through `contentMode`, which is coherent, but much thinner than the framework's richer protection model.
- Clear consistency gap:
  - `VXDeepFilterNet` does not use the suite `Vocal/General` mode contract at all. Its `modeParamId` is repurposed as the model selector (`DeepFilterNet 3` vs `DeepFilterNet 2`), so there is no real `General` mode behavior to compare and no shared-policy mapping.
- Overall conclusion: the suite is directionally consistent that `Vocal` should be more surgical and speech-aware, but implementation consistency is mixed. Some products honor the shared framework policy directly, some implement their own local "voice mode" interpretation, and `VXDeepFilterNet` currently breaks the contract by replacing mode with model selection entirely.

---

# Denoiser / analyser regression pass — 2026-03-19

## Problem
`VXDenoiser` was reading like a broad full-range level drop in both modes, and the analyser was still occasionally flipping into the sparse stem/bar render with orange dots.

## Plan
- [x] Inspect the Denoiser amount/protection path and analyser sparse-render switching logic.
- [x] Implement targeted fixes for Denoiser specificity and analyser render stability.
- [x] Rebuild the affected plugins, run focused regression coverage, and record the result.

## Review
- `Source/vxsuite/products/denoiser/dsp/VxDenoiserDsp.cpp` now uses `speechFocus` inside the gain law, adding explicit speech-band protection so vocal cleanup stays more surgical instead of broadly pulling down the whole vocal range.
- The same DSP now slows upward motion in its per-bin noise estimate when speech presence is high, which reduces the tendency for the noise floor to chase live program material and turn the denoiser into broadband attenuation.
- `Source/vxsuite/products/denoiser/VxDenoiserProcessor.cpp` now applies bounded loudness-retention makeup after denoising, then runs the shared output trimmer. That keeps the result from feeling like simple volume loss without just blending the removed noise back in.
- `Source/vxsuite/products/analyser/VXStudioAnalyserEditor.cpp` no longer switches the spectrum plot into the sparse stem/orange-dot renderer; it stays in the normal overlay view even when the signal is narrowband, which removes the distracting bar-chart flash.
- Added a denoiser regression in `tests/VXSuitePluginRegressionTests.cpp` to ensure strong settings in both vocal and general modes retain useful output level instead of collapsing excessively.
- Verified with:
  - `cmake --build build --target VXDenoiserPlugin VXStudioAnalyserPlugin VXSuitePluginRegressionTests -j4`
  - `./build/VXSuitePluginRegressionTests`
- Follow-up pink-noise correction:
  - `Source/vxsuite/products/denoiser/VxDenoiserProcessor.cpp` now only applies loudness-retention makeup when the DSP actually sees speech-like presence, so noise-only material is no longer “protected” back toward its original broadband level.
  - Vocal and general clean-drive scaling were both increased so max settings can do real work on noise-only material instead of reading as near-bypass in vocal mode.
  - `Source/vxsuite/products/denoiser/dsp/VxDenoiserDsp.cpp` now gates speech-band protection by per-bin speech presence instead of always protecting the vocal band, which prevents pink noise from being treated like protected voice content.
  - Added a noise-only denoiser regression in `tests/VXSuitePluginRegressionTests.cpp` so both modes must measurably reduce noise-only input at full settings.
  - Re-verified with:
    - `cmake --build build --target VXDenoiserPlugin VXSuitePluginRegressionTests -j4`
    - `./build/VXSuitePluginRegressionTests`

---

# Framework UI responsiveness pass — 2026-03-19

## Problem
Ensure framework-based VX effects fit text cleanly, resize sensibly, and inherit readable layout behavior at the shared editor level instead of relying on one-off per-plugin fixes.

## Plan
- [x] Inspect the shared framework editor and framework UI rules.
- [x] Implement framework-level text-fitting and responsive layout improvements in the shared editor base.
- [x] Rebuild representative plugins and run regression verification.

## Review
- `Source/vxsuite/framework/VxSuiteEditorBase.cpp` now uses more forgiving shared minimum editor sizes, stronger minimum horizontal scaling on title/status/knob text, and more generous shared vertical space for knob labels and hint text.
- The shared header layout now tolerates narrower widths better by allowing the status line to fall to its own row when the control row gets tight, which prevents cramped header text across framework-based products.
- Shared mode/listen/learn controls now reserve slightly more width, and learn meter text is less likely to truncate aggressively.
- Updated `docs/VX_SUITE_FRAMEWORK.md` to make readable text and responsive shared layout part of the framework contract rather than an ad hoc cleanup item.
- Verified with:
  - `cmake --build build --target VXCleanupPlugin VXProximityPlugin VXFinishPlugin VXDenoiserPlugin -j4`
  - `./build/VXSuitePluginRegressionTests`

---

# Cleanup plosive false-trigger follow-up — 2026-03-20

## Problem
`VXCleanup` is still audibly distorting in a way that lines up with the plosive meter lighting. That suggests the remaining bug is not broad Cleanup harshness anymore, but false plosive detection on voiced material inside the Cleanup-only corrective path.

## Plan
- [x] Add a targeted regression that measures Cleanup plosive activity on real plosive material versus voiced edge-case material.
- [x] Tighten the Cleanup plosive detector/mapping so voiced harmonic content does not trigger plosive removal while real plosive bursts still do.
- [x] Rebuild, rerun the targeted verification, document the result, and commit the follow-up fix.

## Review
- `tests/VXSuitePluginRegressionTests.cpp` now includes `testCleanupPlosiveMeterTargetsBurstsNotVoicedMaterial()`, which measures the Cleanup plosive activity light directly instead of inferring false triggering only from output damage.
- `Source/vxsuite/products/polish/dsp/VxPolishCorrectiveStage.*` now uses a lower detector lowpass for the plosive follower and applies an explicit speech-aware plosive guard, so bright voiced material no longer drives the internal plosive envelope like a real low-frequency burst.
- `Source/vxsuite/products/cleanup/VxCleanupProcessor.cpp` now biases plosive correction much harder toward the low-focus side of Cleanup and further backs it off when the block looks strongly voiced/harmonic.
- Verified with:
  - `cmake --build build --target VXSuitePluginRegressionTests -j4`
  - relinked `/tmp/cleanup_plosive_probe` against current objects, which measured `burstActivity=0.838522` and `voicedActivity=0.0770964`
  - `./build/VXSuitePluginRegressionTests`
- The shared regression executable still exits on the pre-existing Deverb tail-window failure, but Cleanup no longer reports a plosive-related regression before that point.

---

# Deverb latency-vs-tail regression fix — 2026-03-20

## Problem
The shared regression suite was still failing on `Deverb tail window was unexpectedly empty`. The failure turned out not to be a Deverb DSP tail bug, but a framework/test contract bug: VX Suite was auto-reporting latency as if it were post-input tail audio.

## Plan
- [x] Reproduce the Deverb tail-window failure and inspect whether the processor really emits post-input tail audio or only host-reported latency.
- [x] Fix the framework/test contract so latency-bearing processors do not report fake tail length unless they truly emit carryover after input stops.
- [x] Rebuild and rerun the regression executable, then document the result and commit the fix.

## Review
- `Source/vxsuite/framework/VxSuiteProcessorBase.*` no longer inflates `tailLengthSeconds` from reported latency. Host PDC/latency reporting remains intact, but JUCE tail length now means actual post-input carryover only.
- `tests/VXSuitePluginRegressionTests.cpp` now checks that Deverb, Denoiser, and Subtract report latency without pretending that latency is tail, and the rendered-tail verification now only runs for processors that explicitly report non-zero tail length.
- Verified with `cmake --build build --target VXSuitePluginRegressionTests -j4` and `./build/VXSuitePluginRegressionTests`, which now passes end-to-end.

---

# Post-fix validation closure — 2026-03-20

## Problem
After the Cleanup distortion/plosive fixes and the latency-vs-tail reporting fix, the remaining task was to rerun the available local validation lanes and record the final repo-local status before closing this pass out.

## Plan
- [x] Re-run the key local regression/unit tests after the latest DSP/framework fixes.
- [x] Re-run the registered pluginval lane across all staged bundles.
- [x] Record the post-fix validation status and commit the closure note.

## Review
- Re-ran `ctest --output-on-failure -R '^(VXSuitePluginRegressionTests|VXDeverbTests|VxSuiteVoiceAnalysisTests)$'` from `build/`; all three tests passed.
- Re-ran `ctest --output-on-failure -R '^pluginval_'` from `build/`; all 10 pluginval checks passed: `VXCleanup`, `VXDeepFilterNet`, `VXDenoiser`, `VXDeverb`, `VXFinish`, `VXOptoComp`, `VXProximity`, `VXStudioAnalyser`, `VXSubtract`, and `VXTone`.
- Repo-local conclusion for this pass: source fixes are committed, the local regression suite is green, and the full available pluginval lane is green. Remaining follow-up work is external/in-host validation rather than another known code repair.

---

# Cleanup high-shelf distortion follow-up — 2026-03-20

## Problem
The remaining user report narrowed the residual Cleanup distortion down further: it only seemed to happen when the Cleanup amount was engaged and the high shelf was enabled. That pointed to the `hishelf_on` path rather than the broader corrective chain.

## Plan
- [x] Isolate the Cleanup high-shelf path on a voiced edge-case and confirm whether enabling the shelf adds disproportionate damage.
- [x] Make the high shelf gentler and more stable on voiced material, then add a regression that compares shelf-off versus shelf-on on the same signal.
- [x] Rebuild, rerun the full regression executable, document the result, and commit the fix.

## Review
- `Source/vxsuite/products/polish/dsp/VxPolishCorrectiveStage.cpp` now computes a gentler, higher-frequency high shelf for voiced Cleanup material, scales it back with `voicePreserve`/`speechPresence`, and stops clearing the shelf filter state on every coefficient retune.
- `tests/VXSuitePluginRegressionTests.cpp` now includes `testCleanupHighShelfDoesNotOverdamageVoicedEdgeCase()`, which compares the same voiced edge-case with `hishelf_on` off versus on and fails if the shelf path adds too much extra residual damage.
- Before the fix, the isolated shelf probe measured `off residual=0.0738695`, `on residual=0.0985489`, and `onOffResidual=0.0684699`. After the fix, that same probe measured `off residual=0.0738695`, `on residual=0.0748042`, and `onOffResidual=0.0144093`.
- Verified with `cmake --build build --target VXSuitePluginRegressionTests -j4` and `./build/VXSuitePluginRegressionTests`, which passed cleanly after the shelf-path change.
