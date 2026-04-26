# DSP strength pass — all effects to near-over-the-top at 100% — 2026-04-24

## Goal
Make every effect clearly audible at moderate settings and deliberately dramatic at 100%.
Prior audit: Denoiser 0.998 (nearly bypassed), Proximity 0.373, Tone 0.272, all too polite.

## Plan
- [x] Proximity: raise gain ceiling (vocal 6→10 dB, general 5.2→8 dB), linearise mapping, air 3→6 dB
- [x] Tone: raise kVocalMaxGainDb 5→9, kGeneralMaxGainDb 6→12; vocalPriority attenuation 0.18→0.08
- [x] Cleanup: remove cleanupStrength dead zone (cleanupStrength = cleanupDrive, no threshold)
- [x] Denoiser: cap speechPreserveBlend 0.85→0.55, residualTrimDepth voice keeps 0.56, general 0.54→0.72
- [x] Deverb: overSubtract multiplier 1.5→3.5
- [x] Finish: exponent 0.72/0.78→0.45/0.50, autoMakeupMaxDb voice 11.5→16, general 9.5→13; recovery ceiling raised
- [x] Build full suite — clean, only pre-existing Subtract wet/listen failure remains
- [x] Subtract wet/listen: fixed by resetting outputSafetyTrimmer at learnStopEdge; all regression tests pass

---

# Full regression tuning fix - 2026-04-24

## Goal
Fix the remaining known regression failures: `VXSubtract` listen/recombine and chain stability, `VXDenoiser` speech/noise tuning, and `VXCleanup` strength without voiced-material damage.

## Plan

- [x] Reproduce the current failing regression output and map each failure to the responsible processor path
- [x] Fix `VXSubtract` so wet/listen recombine is complementary and chain/block-size behavior is stable
- [x] Fix `VXDenoiser` so strong vocal denoise preserves speech while reducing noise-only input
- [x] Re-check `VXCleanup` strong settings against voiced-material guardrails and max-effect audit
- [x] Run focused audits plus the full regression suite
- [x] Rebuild and install refreshed VST3 bundles
- [x] Record final results and remaining risks

## Review

- `VXSubtract` now resets its streaming, control, voice-analysis, voice-context, and signal-quality state when Learn is stopped and a profile is finalized. This makes wet and listen renders complementary again and prevents learned noise context from contaminating the first processing pass.
- Replaced the blunt silent-block clear with a latency-tail-aware guard so true idle silence stays silent while render/host latency tails can still flush correctly.
- Added a protect-weighted vocal dry preserve blend for Subtract only when stereo profile coverage is complete, avoiding the right-only learned-profile regression while stabilizing the full chain.
- `VXDenoiser` now blends protected vocal speech back in at high Guard/Clean settings and applies faster/deeper residual trim when the input evidence is non-speech, fixing the previous "damages speech but barely reduces noise" split.
- `VXCleanup` stayed at the guarded strength from the audit pass; final regression confirms the stronger settings no longer trip voiced-material damage tests.
- Verification passed: `cmake --build build --target VXStudioPluginRegressionTests -j4`, `./build/VXStudioPluginRegressionTests`, max-effect audit on `data/voice_corpus/wav_clip`, `cmake --build build --target VXSuite_VST3 -j4`, install to `/Library/Audio/Plug-Ins/VST3/`, and `codesign --verify --deep --strict` on installed `VX*.vst3` bundles.
- Current max-effect report: `tasks/reports/max-effect-audit-clip-2026-04-24.md`.

# Full-suite maximum effect audit - 2026-04-24

## Goal
Audit every VX effect at maximum meaningful controls and make sure 100% settings produce a clear, measurable audio change rather than polite or near-neutral output.

## Plan

- [x] Build a focused strength-audit harness that renders representative audio through each effect at strong settings
- [x] Measure per-effect delta, RMS change, and any relevant target reduction/boost
- [x] Identify weak effects and inspect their processor/DSP laws
- [x] Strengthen weak 100% paths without breaking neutral identity or realtime safety
- [x] Re-run the audit and regression/build targets
- [x] Rebuild and install refreshed VST3 bundles
- [x] Record measured results and residual risks

## Review

- Added `VXStudioBatchAudioCheck` coverage for maximum-strength settings across the suite, including `VXRebalance`, and fixed the report summary delta-RMS accumulator so averages match the per-file rows.
- Wrote the current max-strength report to `tasks/reports/max-effect-audit-clip-2026-04-24.md`. On the clip corpus the measured residual ratios are: Leveler `0.206`, Cleanup `0.071`, Denoiser `0.998`, Deverb `0.366`, Finish `0.181`, OptoComp `0.181`, Tone `0.272`, Proximity `0.373`, Subtract `0.162`, Rebalance `0.127`.
- Strengthened `VXCleanup` high-strength intent, but backed off the extra assertive contour to stay inside voiced-material guardrails. Cleanup remains the least dramatic effect by design/tests; pushing it to ~`0.15` residual caused voiced articulation regressions.
- Added a `VXSubtract` max-strength anti-mute rescue that only engages at very high subtract or low protect, plus a live-input silence guard so blind/learned processing cannot generate output on truly silent input blocks.
- Rebuilt the full VST3 suite with `cmake --build build --target VXSuite_VST3 -j4`, installed all staged `VX*.vst3` bundles to `/Library/Audio/Plug-Ins/VST3/`, and verified installed bundle signatures with `codesign --verify --deep --strict`.
- Regression run after the audit/build still reports residual known tuning failures: Subtract wet/listen recombination (`diff=0.061174`), combined-chain coherence/block-size instability, and Denoiser speech/noise tuning. Cleanup no longer reports after the final backoff.

# VXRebalance track-isolation fix - 2026-04-24

## Goal
Fix `VXRebalance` so each plugin instance only processes the host track it is inserted on, and cannot pick up audio/state from other tracks or other VX effects.

## Plan

- [x] Trace `VXRebalance` processor/DSP routing and any shared framework/global state it consumes
- [x] Reproduce or isolate the cross-instance / cross-track leakage in a focused regression
- [x] Patch the smallest ownership boundary so Rebalance uses only its own input buffer and instance state
- [x] Verify the fix with targeted regression/build
- [x] Rebuild and stage/install `VXRebalance.vst3`
- [x] Record results and residual risk

## Review

- Root cause found in shared VX stage telemetry, not the `VXRebalance` DSP buffer path: `StagePublisher::refreshDomainBinding()` was inferring analyser/track membership from spectral similarity inside the same host process.
- Removed the spectral auto-binding fallback so a VX effect no longer attaches itself to an unrelated analyser domain merely because another track/effect has similar spectrum.
- Added `testRebalanceInstancesStayTrackLocal()` to the plugin regression suite. It runs an active loud Rebalance instance beside an active silent Rebalance instance and verifies the silent instance stays silent.
- Verified build: `cmake --build build --target VXStudioPluginRegressionTests -j4`
- Verification run: `./build/VXStudioPluginRegressionTests`; the new Rebalance isolation test passed. Remaining failures are the pre-existing Subtract wet/listen, chain coherence/block-size/silence, and Denoiser tuning failures.
- Because the telemetry fix is in the statically linked framework, rebuilt the full suite: `cmake --build build --target VXSuite_VST3 -j4`
- Installed all 12 refreshed staged VST3 bundles to `/Library/Audio/Plug-Ins/VST3/` and verified `codesign --verify` for each installed bundle.

# VXSubtract silent-learn runaway fix - 2026-04-24

## Goal
Fix `VXSubtract` learning so silence does not instantly show as a complete learned profile, trigger feedback-like behavior, or mute plugin output.

## Plan

- [x] Trace the Subtract learn/profile state machine and telemetry path
- [x] Reproduce the silent-learning failure with a focused regression or harness
- [x] Prevent silent/near-silent input from being accepted as valid profile progress or profile readiness
- [x] Verify normal non-silent learning still works
- [x] Rebuild and stage the affected VST3 bundle
- [x] Record result and residual risk

## Review

- `VXSubtract` now keeps Learn armed on silent input without feeding the DSP learner, so digital silence does not advance progress, confidence, observed seconds, or profile readiness.
- While Learn is armed, the processor analyzes a scratch copy and leaves the audible buffer dry, avoiding feedback/latency-path output during capture.
- Added a regression covering silent Learn: no generated output, no progress/confidence, no finalized profile, and no mute after stopping Learn.
- Hardened Subtract learn/processing math against non-finite confidence and presence values so a bad analysis metric cannot poison profile readiness or collapse the signal path.
- Verified build: `cmake --build build --target VXStudioPluginRegressionTests -j4`
- Verification run: `./build/VXStudioPluginRegressionTests` confirms the new silent-learn regression and normal non-silent learn lifecycle no longer fail.
- Residual regression output still includes existing/tuning failures in Subtract wet/listen recombination, chain coherence/block-size coverage, chain silence, and Denoiser strength. These are not silent-learn failures, but they should be handled in a separate tuning pass.
- Rebuilt and staged `VXSubtract.vst3`: `cmake --build build --target VXSubtractStage -j4`
- Installed refreshed `/Library/Audio/Plug-Ins/VST3/VXSubtract.vst3`; installed binary hash matches `Source/vxstudio/vst/VXSubtract.vst3`.

# REAPER VST effects silent-processing investigation - 2026-04-23

## Goal
Find and fix why the VXStudio VST effects appear to make no audible change in REAPER,
covering Denoise, Proximity, Deverb, Subtract, and the other suite effects.

## Plan

- [x] Verify which plugin binaries/bundles REAPER is likely loading and whether the latest build was installed to the active VST3 path
- [x] Trace the shared plugin processing path from host audio input through parameter state, bypass/wet mix, DSP execution, and output copy
- [x] Inspect representative products with different DSP paths: Denoiser, Proximity, Deverb, Subtract, plus one known-simple effect
- [x] Build or run a focused offline harness proving whether non-neutral settings alter audio outside the host
- [x] Implement the smallest root-cause fix, rebuild affected bundles, and reinstall/stage them for REAPER
- [x] Run regression or targeted audio-delta verification and record the result

## Review

- REAPER project inspection found many named instances saved bypassed, which explains the "nothing happens" symptom for those effects in the tested projects:
  `VXProximity`, `VXSubtract`, `VXTone`, `VXFinish`, and `VXOptoComp` were bypassed in the inspected `Untitled.RPP`;
  `VXCleanup`, `VXDeverb`, `VXFinish`, `VXProximity`, `VXSubtract`, `VXTone`, and `VXOptoComp` were bypassed in the inspected `gx.RPP`.
- REAPER's arm64 VST cache has scanned the `/Library/Audio/Plug-Ins/VST3/` VX Suite bundles, and the installed binaries match the staged repo bundles where checked.
- Offline regression confirmed the shared processor/parameter path does process audio; the broad "all plugins are dry" symptom is not reproduced in the direct processor harness.
- Denoiser had a real high-setting weakness on noise-only material, so `VXDenoiser` now applies a conservative residual trim when shared voice evidence says the input is mostly non-speech/noise.
- Rebuilt and staged `VXDenoiserStage`, then refreshed `/Library/Audio/Plug-Ins/VST3/VXDenoiser.vst3`; the installed binary verifies with `codesign` and matches the staged bundle hash.
- Verification: `cmake --build build --target VXStudioPluginRegressionTests -j4` succeeds.
- Verification: `./build/VXStudioPluginRegressionTests` now reports only the pre-existing Denoiser speech-coherence failure; the previous noise-only "barely reduced" failure no longer appears.

claude# Stronger high-end effect pass - 2026-04-22

## Goal
Fix the issues found in the effect-strength audit so high values stop feeling overly polite, compression recovers level more convincingly, and the main realtime-safety bug is removed.

## Plan

- [x] Remove the steady-state audio-thread allocation in `VXSubtract`
- [x] Strengthen shared `Finish` / `OptoComp` compression so high settings clamp harder and recover output level more confidently
- [x] Increase `VXLeveler` general-mode upward authority so mix mode feels like real levelling instead of mostly attenuation
- [x] Make `VXCleanup` audibly stronger at high settings without regressing voiced-material guardrails
- [x] Rebuild the affected plugins and reinstall the updated VST3 bundles
- [x] Run the regression suite and record residual risk

## Review

- `VXFinish` / `VXOptoComp` now drive the shared opto compressor harder near the top of the knob and apply measured recovery after compression/limiting, so loud passages do not stay dipped as easily after gain reduction.
- `VXLeveler` general mode now has meaningfully more upward lift available, which should make mix-mode levelling read as active instead of overly cautious.
- `VXCleanup` now includes the merged persistent cleanup stage, a later-ramping high-end strength curve, and looser makeup recovery so high settings read more clearly while still staying inside the voiced-material Cleanup regressions.
- `VXSubtract` no longer resizes its stereo scratch buffers every block during normal processing; it only grows them on oversize input blocks.
- Verified build: `cmake --build build --target VXCleanupStage VXFinishStage VXOptoCompStage VXSubtractStage VXLevelerStage VXStudioPluginRegressionTests -j4`
- Installed refreshed bundles to `/Library/Audio/Plug-Ins/VST3/`: `VXCleanup`, `VXFinish`, `VXOptoComp`, `VXSubtract`, `VXLeveler`
- Verified run: `./build/VXStudioPluginRegressionTests`
- Remaining regression output is now:
  - pre-existing denoiser tuning failures
  - no new Cleanup / Finish / Leveler / Subtract regressions from this pass
- Follow-up investigation on the former full-chain block-size failure showed the problem was in the regression helper, not the shipped subtract DSP: the test learned a subtract profile with hardcoded `256`-sample blocks even when the plugin instance had been prepared for `64` or `512`, which made the learned profile differ before the actual comparison run.
- Fixed the test helper so `primeSubtractLearn(...)` now learns with the same block size the subtract instance was prepared/rendered with; after that change, `./build/VXStudioPluginRegressionTests` falls back to only the long-standing denoiser failures.

claude# Cleanup readability-guard upgrade - 2026-04-10

## Goal
Upgrade the existing Cleanup plugin so it preserves readability, body, and articulation more intelligently while still behaving like Cleanup, not Clarity.

## Plan

- [x] Audit the current Cleanup evidence flow, corrective stage, and test coverage for the right guard insertion points.
- [x] Add persistent density awareness, self-masking metrics, readability-preservation logic, cumulative harm guards, and tonal drift protection to the Cleanup corrective path.
- [x] Keep the existing Cleanup identity and controls intact while refining Vocal mode and Focus behavior.
- [x] Extend regression coverage for over-cleaning, readability preservation, and cooperative stage behavior.
- [x] Build the affected targets and run targeted regression checks.

## Review

- Added a framework-level readability guard resolver and persistent density tracking for Cleanup so the DSP can distinguish persistent mud from short corrective events before it commits to cuts.
- Cleanup now threads focus, density persistence, articulation risk, body-loss risk, cumulative correction risk, and tonal drift risk into the shared corrective stage.
- Focused Cleanup regression coverage now checks that high Focus preserves voiced articulation better than low Focus on edge-case voiced material.
- Verified build: `cmake --build build --target VXCleanup VXStudioPluginRegressionTests -j4`
- Verified run: `./build/VXStudioPluginRegressionTests`
- Result: the new Cleanup focus regression passed, the Cleanup strong-setting audibility check now clears, and the plosive false-trigger guard stays within range on voiced material.
- Remaining regression output is limited to the unrelated denoiser baseline failures already present in the tree.

# Finish/OptoComp shared-core extraction - 2026-04-10

## Goal
Move the shared LA-2A-style opto/finish DSP into `Source/vxstudio/framework/` as the single source of truth, then make `VXFinish` and `VXOptoComp` consume that framework implementation directly.

## Plan

- [x] Copy the shared opto/finish DSP into framework-owned sources
- [x] Repoint `VXFinish` and `VXOptoComp` to the framework-owned DSP
- [x] Remove the obsolete product-local Finish/OptoComp DSP sources from the tree
- [x] Build the affected targets and run regression verification

## Review

- The shared opto/finish DSP now lives in `Source/vxstudio/framework/` and is linked from both products
- The old product-local `VxFinishDsp` and `OptoCompressorLA2A` sources were removed so the code only exists in one place
- The shared `Body` control now reaches the DSP instead of being smoothed and discarded, and it stays neutral at center while still working when `Finish` is bypassed
- Verified build: `cmake --build build --target VXCleanup VXFinish VXOptoComp VXStudioPluginRegressionTests -j4`
- Verified run: `./build/VXStudioPluginRegressionTests`
- Result: the Finish body-control regression cleared; the remaining warnings are the pre-existing Cleanup and Denoiser baseline failures plus the cleanup allocation warning

# Cleanup self-containment fix - 2026-04-10

## Goal
Make `VXCleanup` self-contained by moving the shared corrective DSP and tonal-analysis helpers out of the `polish` product tree and into the VX framework.

## Plan

- [x] Move the shared corrective DSP and analysis helpers into `Source/vxstudio/framework/`
- [x] Repoint `VXCleanup` to the framework-owned helpers and remove `polish` includes
- [x] Update the framework CMake wiring and stop compiling the `polish` DSP file into Cleanup targets
- [x] Build the affected targets and verify the Cleanup product still compiles

## Review

- `VXCleanup` now depends on framework-owned corrective implementation instead of importing `../polish/...` directly from the product tree
- The shared corrective DSP, tonal analysis, and evidence helpers now live in `Source/vxstudio/framework/` as real framework code, and the old `polish` copies were removed
- Cleanup no longer needs any `polish` source files in its own target list
- Verified build: `cmake --build build --target VXCleanup VXStudioPluginRegressionTests -j4`
- Verified run: `./build/VXStudioPluginRegressionTests`
- Result: the regression binary still reports the existing denoiser and finish failures, plus the pre-existing cleanup allocation tracking warning; it also still flags the Cleanup strong-setting threshold, which should be treated as a follow-up tuning item rather than a dependency issue

# VXDenoiser crash investigation — 2026-04-07

## Goal
Investigate and fix the REAPER crash reported in `vxsuite::denoiser::DenoiserDsp::processFrame`
on the CoreAudio realtime thread, then verify the plugin still behaves correctly.

## Plan

- [x] Review the supplied crash report and identify the faulting product/function
- [x] Inspect `VxDenoiserDsp` / `VxDenoiserProcessor` / framework preparation paths for invalid-memory risks
- [x] Implement the smallest safe fix for the realtime crash
- [x] Build the affected targets and run targeted denoiser verification
- [x] Add findings and review notes below

## Review

- Hardened `VxDenoiserDsp::processInPlace()` with `hasValidProcessingState()` so the denoiser now
  fails dry instead of touching invalid or undersized prepared buffers/state on the realtime thread
- Added denoiser regression coverage for reset/reprepare stability and oversized host-block consistency
- Fixed a pre-existing build mismatch between `VXSTUDIO_DISABLE_PLUGIN_ENTRYPOINT` and
  `VXSUITE_DISABLE_PLUGIN_ENTRYPOINT` so `VXStudioPluginRegressionTests` can link
- Verified build: `cmake --build build --target VXStudioPluginRegressionTests -j4`
- Verified run: `./build/VXStudioPluginRegressionTests`
- Result: the denoiser-specific regression additions passed silently, but the full regression binary
  still reports unrelated existing failures in denoiser tuning, finish frequency response, and
  cleanup steady-state allocation tracking
- Follow-up: fixed VST3 staging helpers in `CMakeLists.txt` to use the active build config
  (`$<CONFIG>`) instead of hardcoded `Debug`, corrected the stale `tests/VXSuiteProfile.cpp`
  reference to `tests/VXStudioProfile.cpp`, rebuilt all plugin targets, and refreshed the single
  shared bundle folder at `Source/vxstudio/vst`

# Rebalance: htdemucs_6s integration + DSP guitar improvements — 2026-03-26

## Goal
Replace UMX4/Spleeter with htdemucs_6s (explicit guitar stem) and improve DSP heuristic
guitar separation across all three recording-quality modes.

## Plan

- [x] Write export script `assets/rebalance/export_demucs6_onnx.py`
      Contract: input [1,2,88200] waveform @ 44100 Hz → output [1,6,2,88200] stems
      Stems: 0=drums 1=bass 2=other 3=vocals 4=guitar 5=piano
- [ ] Run script to download model and produce `vx_rebalance_demucs6.onnx` + JSON
      → user needs to run: `pip install demucs torch torchaudio onnx onnxruntime`
        then `cd assets/rebalance && python export_demucs6_onnx.py`
        then place outputs in `assets/rebalance/models/demucs6/`
- [x] Create `VxRebalanceDemucsModel.h/.cpp` (waveform-in/out ONNX runner, same C-API pattern)
- [x] Extend `ModelRunner`: add `demucs6` ActiveModel + chunk accumulation path
- [x] Update `VxRebalanceProcessor.cpp`: makeDemucsPackage() + pass demucs file to runner
- [x] DSP guitar profiles: reverted to proven baseline (presence-zone boost regressed guitar corr
      from 0.677 to 0.294 — guitar improvement comes from Demucs, not heuristic DSP)
- [x] Add new sources to CMakeLists.txt
- [x] Build verified: VXRebalanceMeasure builds clean

## Notes
- demucs stem order: drums(0) bass(1) other(2) vocals(3) guitar(4) piano(5)
- VX lane mapping: vocals←3, drums←0, bass←1, guitar←4, other←(2+5 merged)
- chunk size 88200 = 2s @ 44100 Hz (good balance quality/mask refresh rate)
- keep existing UMX4 as fallback when demucs model file absent

---

# Rebalance review for listening tests — 2026-03-27

## Goal
Review the current ReBalance implementation against the DSP-only spec and decide whether it is ready for the stated listening tests:

- Studio vocal + guitar
- Live room band clip
- Phone speech + acoustic guitar
- Drum-heavy loop
- Bass-heavy mix

## Plan

- [x] Gather current ReBalance spec, processor, DSP implementation, and existing lessons
- [x] Verify the product is actually DSP-only end-to-end and not retaining stale ML/runtime assumptions
- [x] Inspect source mapping, mode-profile wiring, and neutral-path behavior against the spec
- [x] Inspect the DSP control laws against the listening-test expectations for vocals, guitar, drums, bass, and neutral transparency
- [x] Run any available targeted verification that can prove or disprove readiness
- [x] Add review findings and residual risks below

## Review

- Applied the requested final ReBalance DSP patch set in `VxRebalanceDsp.h/.cpp`
- Added transient inheritance state, lifecycle-aware ownership push, squared blended confidence, stronger render commitment, low-end contamination guards, and composite-gain flooring/smoothing
- Verified build: `cmake --build build --target VXRebalanceMeasure -j4`
- Remaining gate: the five listening-test clips still need to be auditioned in host or measured with real stem sets before calling the product fully ready for listening tests

---

# Rebalance listening-test implementation — 2026-03-27

## Goal
Implement a repeatable listening-test workflow for ReBalance and source audio that fits:

- Studio vocal + guitar
- Live room band clip
- Phone speech + acoustic guitar
- Drum-heavy loop
- Bass-heavy mix

## Plan

- [x] Inspect the current `VXRebalanceMeasure` fixture contract
- [x] Choose reproducible source audio and derive the five cases
- [x] Implement fixture-generation tooling and documentation
- [x] Run the generator and produce local case folders
- [x] Run the measure harness and emit a report

## Review

- Added `tools/rebalance_listening_protocol.py` with `prepare` and `report` commands
- Added `tests/fixtures/rebalance/README.md` documenting the workflow
- Downloaded the official MUSDB 7-second preview dataset into `tests/fixtures/rebalance/musdb_preview/`
- Generated five case folders plus `tests/fixtures/rebalance/listening_cases/manifest.json`
- Wrote the first report to `tasks/reports/rebalance_listening_protocol_report.md`
- Important outcome: the protocol is working and already shows that some extreme cut checks still need listening and likely DSP follow-up, rather than just assuming readiness

### Follow-up

- Removed the bogus ReBalance mode selector so the product no longer shows `Vocal` / `General`
- Strengthened the source contribution law to use a steeper dB mapping and added an explicit all-sliders-down hard mute path
- Rebuilt `VXRebalanceMeasure` and regenerated `tasks/reports/rebalance_listening_protocol_report.md`

---

# Rebalance: slider coverage fix — 2026-03-27

## Goal
Drums slider does nothing above 400 Hz (hi-hats/snare crack dead).
Guitar slider is computed as a vocal residual so it barely works when vocals are present.
Vocals is the only slider with broad audible effect.

## Plan

- [x] A: Extend drum profiles (all 3 modes) to activate existing snare-crack (1400–6500 Hz)
      and hi-hat (5000–18000 Hz) mask logic that currently multiplied by zero drumWindow
- [x] B: Remove guitar residualSpace formula — compute guitar directly from guitarWindow
      same pattern as vocals/bass/drums; let normalization resolve competition
- [x] C: Extend vocal sibilance ceiling from 6000 → 9000 Hz; add guitar shimmer band 5000–10000 Hz
      Rebuild confirmed clean.

---

# Rebalance review: slider targeting + corpus status — 2026-03-27

## Goal
Review whether each ReBalance slider is wired to the correct source behavior in the current source,
and inventory the available corpus for tuning/training.

## Plan

- [x] Verify processor parameter order and DSP source index order
- [x] Inspect source-specific mask construction and render-stage contribution law
- [x] Check whether recording type materially changes DSP behavior
- [x] Inventory current listening/tuning corpus on disk
- [x] Add findings and residual risks below

## Review

- Processor parameter order matches DSP source order end-to-end: `vocals`, `drums`, `bass`, `guitar`, `other`, `strength`
- Recording Type is real and active in DSP; `Studio`, `Live`, and `Phone / Rough` branch mask behavior in several places
- No obvious source-lane swap exists in the current source; the main issue is separation authority, not parameter misrouting
- Residual risk remains in ambiguous bins because the final render sums weighted source gains, so a slider can still behave like a broad tonal move when masks are not decisive enough
- Local corpus exists for tuning: MUSDB preview plus generated listening fixtures and reports
- Corpus is still limited for product-grade tuning, especially for true phone captures and real live-room material; current phone fixture is synthetic/reconstructed rather than a broad real-device set

---

# Rebalance: render symmetry update — 2026-03-27

## Goal
Check all recording modes against the current render law and fix the push/pull asymmetry so
boosts and cuts behave like matched bipolar controls.

## Plan

- [x] Review Studio, Live, and Phone / Rough mode profiles against the current render path
- [x] Research relevant source-separation masking / uncertainty handling guidance
- [x] Patch render-stage symmetry so uncertain bins blend toward unity instead of protecting cuts only
- [x] Build and run local verification after the render change

## Review

- Confirmed all three modes were feeding the same asymmetric render stage; the mode differences were real, but the boost/cut mismatch came from the shared final composite-gain law
- Updated the render to use symmetric `±18 dB` source moves and a dB-domain weighted blend so negative and positive slider travel behave like matched bipolar controls
- Replaced cut-only protection with symmetric uncertainty handling that blends low-confidence bins back toward unity instead of flooring only the negative side
- Verified build: `cmake --build build --target VXRebalanceMeasure -j4`
- Regenerated `tasks/reports/rebalance_listening_protocol_report.md`
- Result: guitar cut now measures as an actual cut in Studio, Live, and Phone / Rough instead of flipping positive in the live/phone cases

---

# Rebalance: final ownership/exclusivity render spec — 2026-03-27

## Goal
Implement the final DSP separation spec:

- slider intent = contribution + ownership bias
- IRM-like low-confidence blend
- IBM-like high-confidence exclusivity
- near-isolation without brittle exact equality
- `other` reduced to residual ambience instead of a full catch-all source

## Plan

- [x] Compare current render path against the new final spec
- [x] Replace contribution mapping with the spec's perceptual curve
- [x] Add ownership bias, near-isolation, and confidence-gated exclusivity to the final render
- [x] Rebuild measure target and plugin target
- [x] Regenerate the listening report and note residual gaps

## Review

- `computeSourceContributionMultiplier()` now follows the spec's perceptual slider curve and caps `other` boost below the main named sources
- `buildForegroundBackgroundRender()` now computes ownership-biased dominance, confidence-gated IRM/hybrid/IBM-style render states, and tolerant near-isolation logic instead of exact `-100%` checks
- High-confidence bins now suppress non-dominant sources much harder; near-isolation forces non-active sources toward near-zero rather than preserving a remix
- Rebuilt both `VXRebalanceMeasure` and `VXRebalancePlugin`, and synced the installed VST3 bundle in `/Library/Audio/Plug-Ins/VST3/VXRebalance.vst3`
- Regenerated `tasks/reports/rebalance_listening_protocol_report.md`
- Current residual gap: bass cut remains strong and vocals up remains strong, but drum and guitar cuts are still weaker than the final spec target on some corpus cases, so the ownership logic is now structurally correct but still needs tuning
- Follow-up tuning improved those weak cases by softening negative ownership collapse and strengthening live guitar / transient drum ownership:
  studio guitar `target_corr` moved to about `-0.70`, live guitar to about `-0.26`, phone guitar to about `-0.58`, and drum-heavy loop drums cut to about `-0.45`
- Additional live-only arbitration tuning improved live guitar further by reducing residual fallback and boosting guitar ownership in the 300 Hz to 2.4 kHz range:
  live guitar `target_corr` moved again to about `-0.33`

---

# Rebalance: final ownership render spec — 2026-03-27

## Goal
Align ReBalance to the final ownership-based separation spec so it behaves like source energy
allocation rather than EQ, tilt, or weighted remix.

## Plan

- [x] Review current render model, near-isolation logic, exclusivity, slider intent, and `other` handling against the new spec
- [x] Patch the render path to switch from weighted remix toward ownership-driven IRM/IBM hybrid behavior
- [x] Add near-isolation and slider-driven ownership bias per the final spec
- [x] Tighten `other` so it behaves like residual ambience instead of a full source
- [x] Build and run verification after the patch

## Review

- Replaced the previous weighted-remix style final render with an ownership-driven IRM/IBM hybrid in `VxRebalanceDsp.cpp`
- Kept the existing mask engine, but moved slider intent into both contribution and ownership bias at render time
- Preserved the curved bipolar contribution law, capped `other` boosts, and reinforced `other` suppression in confident bins
- Used the existing tracked-object probabilities to strengthen ownership instead of introducing a second unrelated ownership system
- Verified build: `cmake --build build --target VXRebalanceMeasure -j4`
- Regenerated `tasks/reports/rebalance_listening_protocol_report.md`
- Outcome: the structure now matches the final spec much better, with dominant-plus-residual reconstruction instead of weighted remix; however, some hard cases such as live guitar cut are still underpowered in the current heuristics and need further tuning rather than claiming the job is fully solved

---

# Rebalance: per-bin debug visualizer — 2026-03-28

## Goal
Add a lightweight ReBalance-only debug visualizer so we can inspect per-bin dominant source,
confidence, and mask leakage without changing the shared VX Suite editor for other products.

## Plan

- [x] Add a compact debug snapshot at the ReBalance DSP boundary
- [x] Expose the snapshot through the ReBalance processor and override `createEditor()`
- [x] Add a ReBalance-only wrapper editor with a small diagnostics panel
- [x] Build the plugin and verify the visualizer compiles into the installed VST3

## Review

- Added a compact atomic-backed `DebugSnapshot` to `VxRebalanceDsp` with downsampled dominant source, confidence, dominant-mask, `other` mask, and per-source dominant coverage
- Published the snapshot at the end of the final render so the visualizer reflects the actual ownership/exclusivity stage rather than an earlier heuristic mask
- Added a ReBalance-only `createEditor()` override and wrapper editor that embeds the existing shared VX Suite editor and adds a diagnostics panel below it
- Kept the shared `EditorBase` untouched so other VX Suite products are not affected
- Verified builds: `cmake --build build --target VXRebalanceMeasure -j4` and `cmake --build build --target VXRebalancePlugin -j4`
- Synced the installed bundle: `/Library/Audio/Plug-Ins/VST3/VXRebalance.vst3`
- Residual gap: this is a debug-facing visualizer, not yet a polished production UI; it is meant to expose dominant-bin ownership leakage and confidence patterns while tuning ReBalance

---

# README and help-popup refresh - 2026-03-28

## Goal
Bring the top-level README and the shared in-plugin help popup content up to date, and
remove non-ASCII characters that could render poorly in VST popup text.

## Plan

- [x] Audit the current README and shared help popup source
- [x] Rewrite the README with current plugin/version/behavior information
- [x] Update shared help content where product wording is stale
- [x] Remove non-ASCII characters from the popup rendering path and verify the affected files

---

# Rebalance: smaller default shell + stronger continuous slider response - 2026-03-29

## Goal
Make ReBalance feel smaller and more responsive by default, and make slider travel behave
like continuous source redistribution instead of a weak polite remix:

- smaller default plugin shell
- faster default control response
- `-100%` trends to effectively nothing without binary gating
- `+100%` trends toward a real 2x emergence on the same source lane
- prove the change with automated listening-protocol measurements

## Plan

- [x] Stop the stale background listening-protocol run and capture the current baseline
- [x] Tighten default editor sizing and authoritative smoothing values
- [x] Strengthen continuous ownership redistribution and residual suppression in the final render
- [x] Build `VXRebalanceMeasure` and `VXRebalancePlugin`
- [x] Run the automated listening protocol and compare isolated lane gains before/after
- [x] Sync the installed VST3 and add review notes below

## Review

- Reduced the default ReBalance wrapper scale from `0.88` to `0.82` in `VxRebalanceEditor.cpp`, keeping the diagnostics panel collapsed by default so the plugin opens smaller in host
- Tightened DSP control smoother times to `18 ms` for source sliders and `25 ms` for strength in `VxRebalanceDsp.cpp` so the plugin feels more immediate without removing smoothing entirely
- Reworked the final render so contribution stays continuous at `0x .. 2x` while ownership no longer collapses too early on negative moves; cuts now preserve dominant-bin claim longer and high-confidence cut bins suppress competitors more usefully
- Built `VXRebalanceMeasure` and `VXRebalancePlugin`, then synced `Source/vxsuite/vst/VXRebalance.vst3` into `/Library/Audio/Plug-Ins/VST3/VXRebalance.vst3`

### Automated verification

- Full listening-protocol rerun after the first render patch exposed a regression: the plugin felt smaller/faster, but source movement became much weaker than the previous baseline. Example regressions from `tasks/reports/rebalance_listening_protocol_report.md`:
  studio guitar cut `target_corr` fell to about `-0.245` with only about `-0.13 dB` isolated guitar change
  live guitar cut fell to about `-0.055` with only about `-0.02 dB` isolated guitar change
  drums cut fell to about `-0.192` with only about `-0.05 dB` isolated drum change
- I then corrected the render and ran targeted automated checks directly with `VXRebalanceMeasure` on the critical lanes:
  studio vocals `+24 dB`: `target_corr 0.769`, isolated vocals about `+3.59 dB`
  studio guitar `-24 dB`: `target_corr -0.531`, isolated guitar about `-0.60 dB`
  live guitar `-24 dB`: `target_corr -0.097`, isolated guitar about `-0.07 dB`
  phone guitar `-24 dB`: `target_corr -0.437`, isolated guitar about `-1.96 dB`
  drums `-24 dB`: `target_corr -0.312`, isolated drums about `-0.17 dB`
  bass `-24 dB`: `target_corr -0.939`, isolated bass about `-11.45 dB`

### Outcome

- This is a better build than the regressed intermediate one and it is smaller and more responsive by default
- Bass cut is now strong and phone guitar cut is materially stronger
- Studio guitar cut recovered into a believable direction, but it is still not as strong as the product contract wants
- Live guitar cut and drum cut are still too weak, so the plugin is improved but not yet at the "natural 0x to 2x source redistribution" goal across all lanes

### Follow-up pass in progress

- [x] Tighten live guitar vs `other` arbitration in raw, conditioned, and render ownership stages
- [x] Strengthen drum transient ownership in upper-band transient bins and object tracking
- [x] Rebuild and rerun targeted automated verification for live guitar and drums without breaking bass/vocals

### Follow-up pass results

- Live guitar improved from about `-0.07 dB` isolated attenuation to about `-0.40 dB`, with `target_corr` improving from about `-0.10` to about `-0.20`
- Drums improved from about `-0.17 dB` isolated attenuation to about `-0.20 dB`, but the move is still small and `target_corr` remains around `-0.29`
- Bass remained strong at about `-11.45 dB`, so the follow-up pass did not break the low-end lane that was already behaving well
- Current targeted verification is documented in `tasks/reports/rebalance_targeted_verification_2026-03-29.md`

### Guitar body-first pass results

- Added body-first guitar weighting, reduced phone-mode midband over-suppression, slowed guitar persistence decay through ambiguous frames, and reduced `other` reclamation in phone/live guitar bins
- Studio guitar improved again to about `-0.66 dB` isolated attenuation with `target_corr` about `-0.55`
- Live guitar held near the improved range at about `-0.42 dB` isolated attenuation with `target_corr` about `-0.21`
- Phone guitar improved again to about `-2.12 dB` isolated attenuation with `target_corr` about `-0.45`
- This pass helped guitar without breaking bass, but drums still remain the weakest underpowered lane

### Final structural cleanup in progress

- [x] Remove dead harmonic-cluster helper that is no longer used
- [x] Add explicit transient-to-tracked-cluster linkage for attack/sustain continuity
- [x] Relax the most conservative Phone guitar mode scalars and verify guitar lanes again

### Final structural cleanup results

- Removed the unused `applyClusterInfluence()` helper so the inline cluster-influence path in `computeMasks()` is now the only active implementation
- Added `TransientEvent.linkedClusterTrackId` and reused it during transient decay so attack/sustain continuity is no longer write-only
- Relaxed Phone mode scalars for guitar evidence:
  `confidenceFloor 0.32 -> 0.28`
  `harmonicTrust 0.62 -> 0.74`
  `stereoWidthTrust 0.25 -> 0.32`
- Guitar verification after this cleanup:
  studio guitar stayed strong at about `-0.66 dB` with `target_corr` about `-0.55`
  live guitar improved slightly in correlation to about `-0.216` and held about `-0.39 dB`
  phone guitar improved again to about `-2.25 dB` with `target_corr` about `-0.47`
- Applied the remaining cleanup fixes from the final patch prompt:
  equal-width legend slots in `VxRebalanceEditor.cpp`
  aligned `updateLayout()` sizing constants with `resized()`
  added `wasNeutral` handling in `VxRebalanceProcessor` so neutral -> active transitions reset the DSP once before processing
  verified builds: `cmake --build build --target VXRebalanceMeasure -j4` and `cmake --build build --target VXRebalancePlugin -j4`

### Final measured tuning pass in progress

- [x] Tighten live guitar midband ownership with minimal blast radius
- [x] Strengthen drum transient-bin authority without breaking bass
- [x] Rebuild and rerun targeted verification for live guitar, drums, and bass

### Final measured tuning pass results

- Kept the live-guitar arbitration improvement: live guitar moved to about `-0.416 dB` isolated attenuation with `target_corr` about `-0.225`
- Reverted the over-strong drum-specific ownership nudge when it failed to improve the target; final drums remain about `-0.195 dB` with `target_corr` about `-0.286`
- Bass remained safely strong at about `-11.29 dB`, so the final pass did not destabilise the best-performing lane
- Synced the final kept build to `/Library/Audio/Plug-Ins/VST3/VXRebalance.vst3`

## Review

- Rewrote `README.md` in plain ASCII with current plugin inventory, version table, selector behavior, ReBalance updates, build targets, and repository layout
- Updated the shared `VXRebalance` help popup copy in `Source/vxsuite/framework/VxSuiteHelpContent.h` so it now reflects Recording Type selection and the current confidence-driven rebalance wording
- Removed the unicode list bullet from `Source/vxsuite/framework/VxSuiteHelpView.cpp` so popup-rendered unordered lists stay ASCII-safe
- Verified ASCII-only content in `README.md`, `Source/vxsuite/framework/VxSuiteHelpContent.h`, and `Source/vxsuite/framework/VxSuiteHelpView.cpp`
- Verified full suite build after the shared help change: `cmake --build build --target VXSuite_VST3 -j4`
- Synced all staged VST3 bundles from `Source/vxsuite/vst/` into `/Library/Audio/Plug-Ins/VST3/`

---

# Rebalance review follow-up - 2026-03-28

## Goal
Address the latest ReBalance code-review findings without breaking the current ownership-based render design.

## Plan

- [x] Remove redundant processor-side control smoothing
- [x] Make contribution scaling actually use render confidence
- [x] Clean up the duplicate low-vocal semantic penalty and the unused render argument
- [x] Move debug UI to a shipping-safe collapsed default and verify build/install

## Review

- Removed the redundant processor-side `smoothBlockValue` layer so ReBalance now relies on the DSP's own `SmoothedValue` control path only
- Updated `computeSourceContributionMultiplier()` so per-source contribution is now moderated by render confidence instead of silently ignoring the `confidence` parameter
- Collapsed the duplicate `hz < 140 Hz` vocal semantic penalty into one explicit multiplier and removed the unused `analysisMag` argument from `buildForegroundBackgroundRender()`
- Stopped knocking `other` down directly from vocal/guitar raw weights at the first profile stage; `other` is still controlled later through arbitration and confident-bin render suppression
- Changed the diagnostics panel to start collapsed by default
- Verified builds: `cmake --build build --target VXRebalanceMeasure -j4` and `cmake --build build --target VXRebalancePlugin -j4`
- Synced the installed bundle: `/Library/Audio/Plug-Ins/VST3/VXRebalance.vst3`
# VxClarity build — 2026-04-10

## Goal
Build `VxClarity` as a standalone VX Suite product that follows the supplied brief instead of reusing `Cleanup`'s product identity.

## Plan

- [x] Map the brief against existing VX Suite products and confirm `VxClarity` should live alongside `Cleanup`
- [x] Extend the shared processor base so a product can declare an optional sidechain bus
- [x] Add `VxClarity` product files, help content, version wiring, and build registration
- [x] Build the new plugin target and verify the new product compiles cleanly

## Review

- Added a standalone `VXClarity` plugin target with its own identity, help content, version entry, and VST3 staging
- Extended `vxsuite::ProcessorBase` with an optional buses-properties constructor so `VxClarity` can expose a sidechain bus cleanly
- Implemented `VxClarity` as a two-knob clarity processor with `Clean` and `Focus`, shared `Voice` / `General` mode mapping, and optional sidechain-aware adaptive space-making
- Verified build: `cmake --build /Users/andrzejmarczewski/Documents/GitHub/VxStudio/build --target VXClarity_VST3 -j4`
- Verified compile-only regression target: `cmake --build /Users/andrzejmarczewski/Documents/GitHub/VxStudio/build --target VXStudioPluginRegressionTests -j4`
- Residual risk: the DSP is intentionally broad and zero-latency, but it still needs in-host listening and corpus tuning to confirm the brief's subjective "clearer / less cloudy / not pumpy" goal

# VxLeveler audio pickup investigation - 2026-04-22

## Goal
Verify why `VxLeveler` no longer seems to pick up input audio / process it, fix the root cause, and prove the signal path works again.

## Plan

- [x] Inspect the VxLeveler processor/editor/DSP signal path and recent framework interactions
- [x] Reproduce or isolate where audio stops being observed or processed
- [x] Implement the smallest correct fix
- [x] Build and run targeted verification
- [x] Add review notes with outcome and residual risk

## Review

- `VXLeveler`'s processor path is active in the local harness: `./build/VXLevelerMeasure tests/fixtures/rebalance/listening_cases/studio_vocal_guitar/_original.wav /tmp/vxleveler_out.wav general 1.0 1.0 smart 2` produced changing trace values and a rendered output file, so the plugin code is still receiving audio and processing it.
- The staged host-loaded bundle at `Source/vxstudio/vst/VXLeveler.vst3` was stale before this check and still dated `2026-04-07`; rebuilding `VXLevelerStage` refreshed the actual bundle your DAW loads to `2026-04-22`.
- Verified the staged bundle now matches the freshly built binary via identical `shasum` values for `build/VXLeveler_artefacts/Release/VST3/VXLeveler.vst3/Contents/MacOS/VXLeveler` and `Source/vxstudio/vst/VXLeveler.vst3/Contents/MacOS/VXLeveler`.
- `./build/VXStudioPluginRegressionTests` still fails, but only on the pre-existing denoiser regressions; no Leveler regression failed during this check.

# VST compile pass - 2026-04-24

## Goal
Compile the current VXStudio VST3 plugin suite and refresh the staged bundles.

## Plan

- [x] Confirm available VST3/staging targets in the configured build tree
- [x] Build the aggregate VST3 suite target
- [x] Verify the staged VST3 bundles exist after compilation
- [x] Record build result and any residual issues

## Review

- Verified configured build targets include the aggregate `VXSuite_VST3` target and individual plugin `*Stage` targets.
- Verified build: `cmake --build build --target VXSuite_VST3 -j4`
- Result: build completed successfully and refreshed staged VST3 bundles in `Source/vxstudio/vst/`.
- Verified staged bundles exist with executable payloads for: `VXCleanup`, `VXDeepFilterNet`, `VXDenoiser`, `VXDeverb`, `VXFinish`, `VXLeveler`, `VXOptoComp`, `VXProximity`, `VXRebalance`, `VXStudioAnalyser`, `VXSubtract`, and `VXTone`.
- Verified `codesign --verify` succeeds for all staged `.vst3` bundles.
- No source fixes were needed for this compile pass.
