# VX Tune — Phase 2: Stable Renderer And Measured Quality

Companion to `VXTUNE_VISION_ARCHITECTURE.md`, `VXTUNE_BUILD_SPEC.md`, and
`VXTUNE_TESTING.md`.

Status as of 2026-08-04: the obsolete time-domain renderer has been removed
from the production direction. Real LOAP renders showed persistent popping and
unstable wet artifacts even after several targeted fixes. Replacing the
production renderer with Signalsmith Stretch removed the popping. Phase 2 is
therefore the measured quality phase on top of a stable spectral renderer.

## Phase 2 Goal

Make VX Tune sound like a good automated vocal tuner, not merely a non-popping
pitch shifter.

Success means:

- stable audio with no clicks, pops, corrupted startup, or reset artifacts;
- useful correction that improves real singers without over-tuning them;
- preserved attacks, vibrato, transitions, consonants, and vocal identity;
- explicit latency and renderer diagnostics;
- repeatable benchmark evidence for every change.

## Non-Goals

- Do not declare Signalsmith final merely because it is stable.
- Do not enable formant processing by default until plain pitch shifting is
  measured.
- Do not vendor Rubber Band into the shipped plugin unless licensing is
  explicitly resolved.
- Do not optimise against "perfect pitch" at the expense of natural singing.

## Architecture Direction

### Production Renderer

Signalsmith Stretch is the production renderer baseline for Phase 2.

The production path must compile around:

- `VxTunePitchDetector`
- `VxTuneDecomposition`
- `VxTuneCorrectionEngine`
- `VxTunePitchRenderer` / `SignalsmithPitchRenderer`
- `VxTuneProcessor`

Obsolete renderer code must not be compiled into the production plugin path.

### Renderer Cleanup

Dead weight removed:

- the removed renderer is gone from the abstraction and factory;
- old shifter sources are no longer compiled into production, plugin,
  regression, profile, or analysis targets;
- renderer quality tests are backend-neutral through `VxTuneRendererTests`;
- removed shifter sources were deleted from source, not archived in-tree.

### Rubber Band

Rubber Band is a development comparator only until licensing is decided. It may
be added behind an explicit benchmark-only CMake option, but it must not become
a shipped backend through package-manager discovery or accidental linkage.

## Renderer Acceptance Tests

Create a dedicated `VxTuneRendererTests` target. It should feed fixed correction
envelopes into the renderer, independent of live pitch detection:

- `0c`
- `+25c`
- `-25c`
- `0 -> +50c`
- `+50 -> -50c`
- vibrato plus centre shift
- silence to voiced onset
- voiced to silence
- transport reset mid-note

Metrics:

- click/max-derivative score;
- pitch tracking error;
- envelope modulation/wateriness;
- startup validity;
- reset behavior;
- latency alignment;
- CPU rough cost;
- zero-correction damage/null behavior.

The tests should write optional WAV artifacts into `/private/tmp` when requested
so subjective review is tied to deterministic inputs.

## Signalsmith Quality Work

Implemented:

- `VxTunePitchRenderer::QualityProfile::Studio`
- `VxTunePitchRenderer::QualityProfile::Live`
- renderer tests proving the profiles use distinct Signalsmith configurations;
- debug snapshots reporting profile, latency, block size, interval size, and
  split-computation state.

Continue evaluating:

- `presetDefault`
- `presetCheaper`
- manual block/interval sizes
- split-computation on/off

Current profiles:

- **Studio:** Signalsmith default preset with split computation, 7200 samples
  latency at 48 kHz.
- **Live:** Signalsmith cheaper preset with split computation, 6720 samples
  latency at 48 kHz.

Every profile must report exact latency. Dry/wet alignment must use that
latency exactly.

## Diagnostics

The debug CSV must expose:

- requested correction cents;
- renderer-applied cents;
- backend name;
- renderer latency;
- pitch confidence;
- voicing;
- correction gate state;
- detected pitch and corrected pitch.

CorrectionEngine remains the authority over musical correction. The renderer may
interpolate for numerical continuity, but it must not hide large smoothing that
makes the audible result diverge from diagnostics.

## MAST Benchmark Loop

Use `VXTUNE_TESTING.md` as the benchmark plan:

1. Freeze a representative 40-60 item MAST subset.
2. Render original, VX natural/default, and VX tight.
3. Save pitch CSVs, correction CSVs, and contour plots.
4. Label failures by category.
5. Run small blind listening tests asking separately:
   - more in tune?
   - more natural?
6. Tune hidden correction rules against the frozen development subset.
7. Keep validation/test material separate.

Local MAST data:

- Source: Zenodo record `8007358`, `MASTmelody_dataset.zip`.
- Downloaded archive:
  `data/vxtune/mast/MASTmelody_dataset.zip`.
- Extracted audio:
  `data/vxtune/mast/audioFiles/MAST_melody_audio/`.
- Extracted CREPE F0:
  `data/vxtune/mast/f0data_crepe/MAST_melody_f0/`.
- Extracted annotations:
  `data/vxtune/mast/annotations/annotations.csv`.

Frozen VX Tune development subset:

- Manifest:
  `data/vxtune/mast/vxtune_mast_frozen_dev_manifest.csv`.
- Selection report:
  `data/vxtune/mast/vxtune_mast_frozen_dev_selection.txt`.
- Size: 50 student performances, 100 VX Tune renders.
- Balance: 10 score-4, 15 score-3, 15 score-2, 10 score-1.
- Selection rule: student performances only (`*_per*`), majority-score
  balance, full-agreement examples prioritised where available.

First batch output:

- Output directory:
  `data/vxtune/mast/vxtune_batch_frozen_dev/`.
- Manifest:
  `data/vxtune/mast/vxtune_batch_frozen_dev/vxtune_batch_manifest.csv`.
- Metrics:
  `data/vxtune/mast/vxtune_batch_frozen_dev/vxtune_batch_metrics.csv`.
- Status: 100/100 renders succeeded.

Initial metric readout:

| Score | Preset | Mean median improvement | Mean output error | Mean correction |
| ----- | ------ | ----------------------- | ----------------- | --------------- |
| 1 | natural | +2.98c | 23.91c | 5.63c |
| 1 | tight | +0.99c | 25.90c | 6.03c |
| 2 | natural | +2.91c | 21.57c | 6.54c |
| 2 | tight | +1.76c | 22.71c | 7.01c |
| 3 | natural | +2.78c | 19.37c | 5.04c |
| 3 | tight | -0.20c | 22.35c | 5.46c |
| 4 | natural | +2.42c | 18.35c | 5.85c |
| 4 | tight | +0.91c | 19.86c | 6.66c |

Interpretation: the current settings are conservative and only mildly improve
median stable-region pitch error. Tight correction is not reliably better, and
already harms some score-3/4 cases. The next tuning work should therefore focus
on target interpretation, attack/transition protection, and avoiding correction
when the performance is already good or ambiguous.

### Batch Harness

`VxTuneBatchHarness` is the Phase 2 batch-render scaffold.

Usage:

```sh
./build/VxTuneBatchHarness <manifest.csv|audio-directory> <output-directory>
```

Manifest columns:

```csv
id,audio_path,score,group
```

Only `audio_path` is required. Directory mode scans `.wav`, `.aif`, `.aiff`,
`.flac`, and `.m4a` files non-recursively.

For each case the harness writes:

- latency-compensated `natural` and `tight` wet WAVs;
- one VX Tune debug CSV per render;
- `vxtune_batch_manifest.csv` containing input metadata, preset values,
  renderer latency, wet path, debug path, and status.
- `vxtune_batch_metrics.csv` containing first-pass objective render metrics.

Current presets:

- `natural`: amount `0.325`, natural `0.25`, speed `0.50`, focus `0.50`;
- `balanced`: amount `0.50`, natural `0.65`, speed `0.30`,
  focus `0.60`;
- `tight`: amount `0.50`, natural `0.85`, speed `0.50`, focus `0.50`;
- `hard_tune`: amount `1.0`, natural `1.0`, speed `1.00`, focus `1.00`.

The VX Tune UI exposes matching test presets:

- `Natural`: amount `0.325`, natural `0.25`, speed `0.50`,
  focus `0.50`, key `Auto`, scale `Auto`;
- `Balanced`: amount `0.50`, natural `0.65`, speed `0.30`,
  focus `0.60`, key `Auto`, scale `Auto`;
- `Tight`: amount `0.50`, natural `0.85`, speed `0.50`,
  focus `0.50`, key `Auto`, scale `Auto`.
- `Hard Tune`: amount `1.00`, natural `1.00`, speed `1.00`,
  focus `1.00`, key `Auto`, scale `Auto`.

These presets are one-shot UI helpers: choosing one writes the real Amount,
Natural, Speed, Focus, Key, and Scale parameters so manual DAW tests can match
the batch harness.

Amount is intentionally scaled so `50%` is the old `100%` correction strength
and `100%` requests roughly double the previous ceiling.

### Key And Scale Direction

Current implementation: VX Tune exposes separate `Key` and `Scale` selectors.
`Key` offers `Auto` plus C..B. `Scale` offers `Auto`, `Chromatic`, `Major`,
`Natural Minor`, `Harmonic Minor`, and `Melodic Minor`.

Auto key/scale is section-based, not first-note based. It accumulates confident
voiced pitch classes over time, scores major and natural-minor candidates, and
falls back to chromatic correction until enough evidence is available. Manual
Key/Scale choices override the auto detector immediately.

The useful scale set should start small:

- `Chromatic`
- major
- natural minor
- harmonic minor
- melodic minor

Later, add custom allowed-note masks if users need modal, blues, or non-diatonic
material. Wrong key detection is worse than chromatic tuning, so the safe
default remains `Chromatic` until the key confidence is high.

Current summary metrics:

- debug, voiced, and stable frame counts;
- voiced coverage;
- mean pitch confidence;
- median stable-region input pitch error vs nearest chromatic note;
- median stable-region output pitch error vs nearest chromatic note;
- median stable-region improvement;
- mean and maximum absolute correction;
- mean musical authority, where `0` means the correction engine deliberately
  held back and `1` means full trust in the correction target;
- mean correction during low-confidence frames;
- input and output residual/vibrato extent;
- vibrato extent ratio.

The next harness layer should add contour plotting and richer timing metrics:
transition duration, attack intervention, correction lag, and per-note failure
labels.

### Batch Report

`tools/vxtune_batch_report.py` reads a `VxTuneBatchHarness` output directory
and writes:

- `vxtune_batch_summary.md`;
- contour SVGs under `contours/most_harmed/`;
- contour SVGs under `contours/worst_output_error/`.

Usage:

```sh
python3 tools/vxtune_batch_report.py data/vxtune/mast/vxtune_batch_frozen_dev
```

The first contour view plots input pitch error, output pitch error, correction,
and confidence over time, using nearest-chromatic pitch error as the provisional
target until reference-melody alignment is added.

### MAST Musicality Alignment

`tools/vxtune_mast_musicality.py` adds an offline musical-context layer using
MAST's supplied chroma arrays.

Usage:

```sh
python3 tools/vxtune_mast_musicality.py data/vxtune/mast/vxtune_batch_frozen_dev
```

It performs a small DTW alignment between each student performance and the best
matching reference take for the same melody pattern, then compares VX Tune's
detected and corrected pitch classes against the aligned reference pitch class.

Outputs:

- `vxtune_musicality_metrics.csv`
- `vxtune_musicality_summary.md`

Baseline label counts from `vxtune_batch_frozen_dev` before Phase 2 musical
authority:

- `attack_or_transition_intervention`: 53
- `low_confidence_intervention`: 48
- `many_harmed_stable_frames`: 29
- `likely_wrong_target_when_already_close`: 12
- `stable_pitch_harmed`: 1
- `review`: 22

Post-authority label counts from `vxtune_batch_frozen_dev_musical_authority`:

- `attack_or_transition_intervention`: 10
- `low_confidence_intervention`: 8
- `many_harmed_stable_frames`: 22
- `likely_wrong_target_when_already_close`: 9
- `review`: 64

Post-authority aggregate observations:

- worst harmed render improved from `-10.35c` to `-4.78c`;
- low-confidence mean correction dropped substantially across every score and
  preset group;
- mean musical authority on MAST is low (`0.12` to `0.24`), which confirms the
  engine is now deliberately conservative on uncertain educational-vocal
  material;
- some mean improvement values dipped because the engine is avoiding risky
  movement rather than forcing a correction.

Stable-note lift run: `vxtune_batch_frozen_dev_stable_lift`.

Changes relative to `vxtune_batch_frozen_dev_musical_authority`:

- total median-improvement delta across the 100 renders: `+12.08c`;
- mean correction rose modestly (`+9.72c` summed across all render rows);
- safety labels were unchanged:
  - `attack_or_transition_intervention`: 10
  - `low_confidence_intervention`: 8
  - `many_harmed_stable_frames`: 22
  - `likely_wrong_target_when_already_close`: 9
- largest positive deltas:
  - `510_mel1_per112958 natural`: `+2.31c`
  - `52_mel1_per146759 natural`: `+2.01c`
  - `55_mel2_per148759 natural`: `+1.97c`
- largest negative deltas:
  - `510_mel2_per174995 natural`: `-0.69c`
  - `510_mel1_per104558 tight`: `-0.54c`
  - `56_mel2_per163160 tight`: `-0.38c`

Guarded stable-note lift run: `vxtune_batch_frozen_dev_stable_lift_guarded`.

The guard requires the settled target error to be at least `22c` before stable
evidence can add extra authority. This preserves the lift for real intonation
errors while avoiding extra force on already-close notes.

Changes relative to `vxtune_batch_frozen_dev_musical_authority`:

- total median-improvement delta across the 100 renders: `+13.84c`;
- summed mean-correction delta: `+5.85c`;
- no per-render median-improvement regressions vs the authority-only run;
- safety labels were unchanged:
  - `attack_or_transition_intervention`: 10
  - `low_confidence_intervention`: 8
  - `many_harmed_stable_frames`: 22
  - `likely_wrong_target_when_already_close`: 9

Changes relative to the unguarded stable lift:

- fixes the largest unguarded regressions:
  - `510_mel2_per174995 natural`: `+0.74c`
  - `510_mel1_per104558 tight`: `+0.54c`
  - `56_mel2_per163160 tight`: `+0.38c`
- reduces summed mean correction by `3.88c`, so it is more selective.

Decision: keep the guarded stable lift as the current Phase 2 correction
authority baseline.

Target-selection diagnostic pass: `vxtune_batch_frozen_dev_near_correct_veto`.

Added debug CSV fields:

- `target_midi_note`
- `runner_up_midi_note`
- `target_error_cents`
- `target_margin_log`
- `authority_near_correct`

Added batch-report field:

- `mean_near_correct_authority`

Inspection of the nine `likely_wrong_target_when_already_close` cases showed
that most were not simple small ambiguous nudges. They often had large current
target errors, meaning correction was lingering after the estimator had already
moved or invalidated the previous target. The first near-correct veto was safe
but weak:

- `many_harmed_stable_frames`: `22 -> 21`
- `review`: `64 -> 65`
- `likely_wrong_target_when_already_close`: unchanged at `9`
- total median-improvement delta vs guarded stable lift: approximately neutral
  (`+0.03c`)
- summed mean-correction delta vs guarded stable lift: `-0.91c`

Stale-target release experiment:

- `staleTargetReleaseCentsPerSec = 900c/s` improved labels:
  - `attack_or_transition_intervention`: `10 -> 8`
  - `likely_wrong_target_when_already_close`: `9 -> 7`
  - `low_confidence_intervention`: `8 -> 7`
  - `many_harmed_stable_frames`: `22 -> 20`
- but it cost too much aggregate tuning:
  - total median-improvement delta vs guarded stable lift: `-7.94c`
  - summed mean-correction delta: `-30.02c`

Balanced stale-target release experiment:

- `staleTargetReleaseCentsPerSec = 500c/s` kept the same label improvements as
  `900c/s` but still cost too much aggregate tuning:
  - total median-improvement delta vs guarded stable lift: `-8.20c`
  - summed mean-correction delta: `-24.76c`

Decision: keep target diagnostics and the near-correct authority cap, but do
not keep faster stale-target release. The remaining wrong-target labels need a
better target model, not a faster global correction release.

Interpretation: the renderer is no longer the main blocker, and the first
musical-authority pass made VX Tune much safer. The remaining quality gap is now
target confidence and correction assertiveness: the plugin sometimes needs to be
more decisive on stable wrong notes without reopening the attack/transition and
low-confidence failure modes.

### LOAP ReaTune Reference

The LOAP listening reference is `wet_reatune_reference.wav`, generated in
ReaTune with automatic chromatic correction, all notes enabled, no pitch range
limits, stereo correction enabled, `elastique 3.3.3 Soloist`, monophonic mode,
and `250 ms` attack time.

This matters because the reference is not using key-specific intelligence or a
hard robotic retune. It is a slow, chromatic, monophonic correction into a mature
spectral renderer. VX Tune should therefore be measured first against that
behaviour: stable note-centre movement over roughly the same time scale,
preserved attacks and transitions, and no audible pitch stepping. If VX Tune
moves more frames than ReaTune but still sounds closer to dry, the next change is
not simply "more Amount"; it is the correction trajectory, target-hold/release
logic, or renderer/formant character.

Two testing dials are now exposed beyond Amount and Natural:

- `Speed`: scales correction slew/release speed around the measured baseline.
  `50%` preserves the previous behaviour, lower values ease more slowly toward
  a ReaTune-like feel, and higher values move the note centre much faster.
- `Focus`: scales how strongly musical-authority gates correction. `50%`
  preserves the previous behaviour, lower values are more conservative, and
  higher values are more assertive on stable targets. At the top end it can
  override most authority gates so `Hard Tune` is a real ceiling test.

The exposed sweeps are intentionally centred rather than top-loaded: changes
above `50%` should be audible before the final few percent, while `100%`
remains the diagnostic hard-tune ceiling.

These controls intentionally sit above the individual hidden constants. Do not
expose the raw guardrail constants unless listening tests prove a broad Speed or
Focus sweep cannot isolate the problem.

## Immediate Implementation Order

1. DONE: Remove obsolete renderer adapter and production linkage.
2. DONE: Add renderer debug fields for backend name, latency,
   requested/applied cents, profile, block size, interval size, and split mode.
3. DONE: Add `VxTuneRendererTests`.
4. DONE: Add Signalsmith quality-profile experiments.
5. DONE: Build the MAST batch harness.
6. DONE: Add objective summary metrics.
7. DONE: Add contour plots and reference-aligned MAST musicality labels.
8. DONE: Add attack/transition and confidence-aware correction authority.
9. DONE: Improve stable-note assertiveness with guarded stable lift.
10. DONE: Add target-choice diagnostics and evaluate near-correct/stale-target
    vetoes.
11. DONE: Add UI test presets and expose Speed/Focus test controls.
12. NEXT: Improve the target model itself: target switching, stale-target
    invalidation, and musical context/key awareness.
