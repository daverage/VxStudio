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
