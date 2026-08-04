# VX Tune — Finishing Build Spec

Companion to `VXTUNE_VISION_ARCHITECTURE.md` (v2, the design authority).
This document is the execution contract: every remaining work item, split by
the model tier it needs. **F-items need a strong model (Fable/Opus): they
involve DSP debugging from numeric traces or design decisions with rewrite
cost. S-items are self-contained, fully specified here, and safe for Sonnet:
the test suite is the referee.**

State as of 2026-08-03: detector + decomposition + correction engine v0 +
dual-tap shifter + pitch trace UI + Listen all landed and tested
(VxTuneAnalysisTests 33/33, VxTunePluginTests 8/8). See tasks/todo.md.

Ground rules for every item:
- Realtime rules from `docs/VX_SUITE_FRAMEWORK.md` are non-negotiable.
- Never regress an existing VX Tune test; full suite must stay at its
  baseline (currently 76/77 regression, 1 known pre-existing Leveler fail).
- New modules go behind the existing interfaces (architecture rule 6);
  extending `VxTuneTimeline.h` types is additive-only.

---

## F-tier (Fable/Opus sessions)

### F1. Epoch-synchronous TD-PSOLA shifter  — DONE, hardened through F4 rounds 1-5 (2026-08-03/04)
Initial pass (2026-08-03) met synthetic acceptance criteria (ratio ≤1c,
formant drift 0.4%, park/unvoiced bit-exact, latency 600 @48k) but had
never been validated against real vocal material. Real-vocal listening
(F4) surfaced 5 rounds of genuine bugs the synthetic suite couldn't catch
— see F4 log below. Current epoch-marking method: **waveform-similarity
(normalized cross-correlation) against the previously confirmed cycle**,
not amplitude peak-picking — this is the technique that finally closed
the gap; see F4 round 5.
Replaces the dual-tap shifter as the product render path (dual-tap stays in
the repo for A/B tests).

- File: `products/tune/dsp/VxTunePsolaShifter.{h,cpp}`. Same interface as
  `PitchShifter` (`prepare/reset/process/latencySamples`) plus
  `setPeriodHint(float periodSamples, float voicedConfidence)` fed from the
  detector each hop.
- Analysis epochs: predicted spacing = period hint, aligned to the waveform
  peak of a ~1 kHz low-passed copy within ±T/5 of prediction.
- Synthesis: Hann windows of 2T centred on epochs, overlap-add; synthesis
  epoch spacing = T / ratio; each synthesis epoch reuses the nearest
  analysis epoch (2T Hann at hop T satisfies COLA, so epoch-reuse at
  ratio 1 reconstructs exactly).
- Formants: intrinsically preserved by TD-PSOLA (windows are re-spaced, not
  resampled). No separate envelope stage below ±1 semitone.
- Unvoiced/low-confidence input: latency-aligned passthrough, ~5 ms
  crossfade between modes. Zero shift: park on exact epoch reuse.
- Latency: fixed `ceil(sr / 80)` samples (12.5 ms @48k); periods longer
  than the budget get truncated windows (rare below 80 Hz).
- Acceptance (extend VxTuneAnalysisTests):
  - ratio accuracy: sine + 10-harmonic tones, shifts ±25/±50/±100c, output
    pitch within ±5c of input × ratio;
  - formant proof: vowel-like tone (fixed spectral-envelope peak), shift
    ±100c → envelope peak frequency moves <2% (dual-tap moves it ~5.9%;
    assert PSOLA beats it decisively);
  - transparency at zero shift (voiced) better than −40 dB delta;
  - unvoiced noise passes through at aligned latency;
  - correction-chain tests re-run green with PSOLA as the renderer.

### F2. Note segmentation + behaviour distributions — CORE DONE (2026-08-04)
- File: `products/tune/dsp/VxTuneSegmenter.h` (header-only, matches the
  other v0/F3 modules' style). `CorrectionEngine` now owns a `Segmenter`
  alongside its `TargetEstimator`, run independently (segmentation must not
  depend on which note the target estimator favours — matches the doc's
  pipeline order, §5.3 before §5.7).
- Segments open on a sustained deviation from the open segment's own
  running mean centre, but ONLY once movement has settled (drift below a
  threshold) - not merely while pitch is far from the mean. Real bug
  caught building this: without the settle requirement, a continuous
  glide/portamento re-triggered the boundary every few frames as the slow
  mean tried to catch up and fell behind again, so it never got to see
  itself as one continuous slide - only a chain of onsets. Caught by a
  synthetic glide test, not guessed.
- Per-segment features, all incremental/O(1) per frame (no allocation, no
  backward pass): running-mean centre, linear-regression drift (c/s),
  rolling-window (~460ms) residual zero-crossing rate + extent for vibrato
  rate/depth, onset flag (reuses the detector's own onsetTransient reason).
  Combined into `behaviourProb[9]` (sustain/vibrato/bend/slide/scoop/fall/
  passing-note/onset/unvoiced) via simple interpretable weighted scores,
  normalised to a distribution (never a hard label - rule 5).
- Behaviour output is itself SMOOTHED frame-to-frame (not reset at segment
  boundaries): a genuine onset ramps in over ~2 frames rather than
  snapping instantly - a hard per-frame jump in the distribution is itself
  a rule-5 violation, not just the underlying label choice. Caught by the
  distribution-continuity test (max L1 jump was 1.75/2.0 before this,
  1.0 after).
- Wired into `CorrectionEngine`: the old fixed dwell-frame gate (F4 round 6
  patch) is retired. `behaviourProb[passingNote]` now continuously scales
  the effective correction amount (`amount * (1 - passingNoteProb)`) -
  a segment that just opened reads as almost entirely passing-note
  (near-zero effective amount) and phases back to full amount as that
  probability decays with held duration. Strictly more principled than the
  old binary "proven yet?" gate it replaces, and subsumes its behaviour.
- Verified on real material (the LOAP track that originally exposed the
  dwell-gate gap): correction still stays near-silent through the fast
  register-leap passage, now for a legible reason (vibrato/bend/passing-
  note dominant there) rather than an opaque frame-count. No envelope/
  audio-quality regression on LOAP or the earlier nf3 track.
- Acceptance: synthetic gauntlet (`testBehaviourDistribution`) - sustain,
  vibrato (5.5Hz), a continuous glide (must read slide/bend, not sustain
  or onset), and a distribution-continuity bound (max L1 jump across
  adjacent frames < 1.5) reusing the existing brief-leap scenario. All
  pass. `testIgnoresBriefFastLeap` (round 6's original regression test)
  still passes unchanged, now exercised through the new behaviour-weighted
  path instead of the retired dwell gate.
- Explicitly NOT done (real remainder, deferred): singer-model
  normalisation of behaviour thresholds (needs the singer model, not
  built), phrase-level position weighting, bend/slide/scoop-aware
  convergence-rate softening (today only passing-note softens amount; a
  genuine in-progress scoop's shape still gets corrected uniformly rather
  than only-once-settled). Segmentation on genuinely dense/fast real
  material (LOAP) still over-fires "onset" more than ideal - functional
  outcome is correct (see above) but the reasoning surfaced isn't always
  the most musically apt label; a known rough edge, not chased further to
  avoid the whack-a-mole pattern from earlier PSOLA rounds.

### F3. Decision layer v1: Bayesian target estimation — CORE DONE (2026-08-04)
- File: `products/tune/dsp/VxTuneTargetEstimator.h` (header-only, matches
  CorrectionEngine's style). CorrectionEngine internals replaced (public
  interface unchanged) — it now owns a `TargetEstimator` and asks it for
  the target note/error each frame instead of picking the literal nearest
  chromatic note fresh every frame.
- Implemented: log-domain evidence accumulation per candidate MIDI note
  (±6 semitones of the current pitch each frame), confidence-weighted
  Gaussian likelihood, exponential decay with a Natural-dependent memory
  (1.2s at full Natural, 0.25s at full Tight) and Natural-dependent
  tolerance (sigma 50c at full Natural, 18c at full Tight). Target
  persistence (a well-evidenced note beats a candidate that just appeared)
  falls out of the accumulation itself — no arbitrary fixed-cents "lock
  margin" needed, which is what F4 round 6's note-lock patch had to use
  and which this replaces. Emits target MIDI note, signed error, runner-up,
  and log-domain margin (matches `NoteSegment::targetMidiNote` /
  `runnerUpMidiNote` / `targetMarginLog` in VxTuneTimeline.h for future F2
  reuse).
- NOT implemented (deferred, real F3 remainder): singer model, phrase
  position, behaviour-distribution weighting, per-phrase intervention
  budget. Those need F2 (segmentation) first per the doc's own sequencing;
  the estimator here is scoped to the part that was actually causing the
  reported failure (target instability), built to the same Estimate<T>/
  NoteSegment contract so it slots into the fuller F3 later without a
  rewrite.
- Real bug found+fixed during build: initial version pruned candidates
  below an absolute log-score floor (-20); accumulated steady-state scores
  for a genuinely converged target are unboundedly more negative than that
  (evidence/(1-decay), often -50 or lower), so the CORRECT target kept
  getting pruned and re-added from scratch — this produced exactly the
  "wanders/reverses direction on a held note" failure the estimator was
  built to fix. Caught by a probe harness before shipping (steady A3+30c
  input should converge and stay; it instead flipped to adjacent notes at
  arbitrary frames). Fixed by removing the absolute-floor prune entirely —
  the fixed-size candidate pool with weakest-eviction-on-overflow already
  bounds memory without it.
- Acceptance: all existing correction/vibrato/scale tests still pass; new
  regression test (`testCorrectsSharpNote`) asserts a converged correction
  stays converged (samples the correction at two well-separated
  post-convergence points, catches exactly the prune bug above). 58/58
  analysis, 8/8 plugin, regression 76/77 (baseline).
- Remaining acceptance criteria from the original spec (singer model,
  intervention budget, borrowed-note-overrides-scale-prior) still apply to
  the deferred remainder above, not to this pass.
- **2026-08-04 fix (score accumulation bug):** found via a real bounce
  ("pitch slides") that the log-score accumulator had no bound on how
  negative a candidate's history could get, so a candidate that becomes
  correct after a real note change could take ~200 frames (over a second)
  to overtake stale history from the PRIOR held note, even with perfect
  evidence every frame after landing. Fixed with a cumulative score floor
  (`kScoreFloor`) plus a constant in-range bonus (`kInRangeBonus`) so
  "currently plausible" is an active positive signal, not just "not
  penalised" - a plain Gaussian log-likelihood peaks at 0, so without the
  bonus, correct-but-freshly-evidenced candidates and stale-but-ignored
  ones decay identically and never differentiate. Recovery time dropped
  to 5-10 frames. `CorrectionEngine` also gained a "divergence guard"
  (give up on a target if its error grows net over a ~70ms window despite
  active correction) as a complementary safety net for whatever this
  doesn't cover.

### F4. Real-vocal debugging pass
After F1: run real vocal takes (REAPER + trace + Listen), fix what breaks.
Unscopeable by definition — detector/segmentation failures on breath, fry,
runs. Keep fixes as new regression clips in the corpus (S4 tooling).

---

## S-tier (Sonnet-safe, any order, each self-contained)

### S1. Key/Scale parameter plumbing
- Add `key` (12 choices C..B) and `scale` (Chromatic, Major, Minor,
  Auto-assist) AudioParameterChoice to VXTune layout; stable IDs; surface
  via `auxSelector`/control-bank per EditorBase capabilities (pick whichever
  the framework supports without new UI code).
- DSP side: expose the chosen prior as a 12-float pitch-class weight array
  on the processor (chromatic = flat). F3 consumes it; until F3 lands the
  engine may ignore it. Test: parameter round-trips through state save/load.

### S2. Profile + allocation coverage — DONE (2026-08-04)
- Added to `VXSuiteProfile` (tests/VXStudioProfile.cpp + CMakeLists.txt
  source list) at all SR/block combos, and to
  `testNoSteadyStateAllocationsOnAudioThread` in the regression suite
  (tests/VXStudioPluginRegressionTests.cpp + CMakeLists.txt source list).
  Both targets needed VXTune's DSP .cpp files added to their CMake source
  lists (weren't there before - VXTune was never in either target).
- Measured: 0.04-0.33x realtime across all SR/block combos, well under the
  CI x0.5 threshold at 48k/512; no steady-state allocations. Full
  `VXSuiteProfile` run exits 0, "All plugins within x0.5 realtime at
  48k/512" including Tune. Regression suite still 76/77 (baseline).

### S3. Pitch-trace polish
- Zoom selector for the pitch trace (reuse the level-trace zoom pattern);
  optional in-key gridline highlighting reading S1's parameter; target-note
  marker fed by a new `getPitchTraceTargetCents()` virtual (return NaN/
  sentinel when no target). Pure UI; no DSP changes.

### S4. Corpus tooling
- New CLI target `VxTuneAnalyze`: reads a WAV, runs detector +
  decomposition (+ segmenter when F2 lands), writes frames/segments as CSV
  (one row per hop: time, f0, conf, reason, centre, residual). Follows the
  VXLevelerMeasure target pattern (JUCE audio_formats for WAV). Used to
  build the annotated corpus; keep clips + labels under `tests/corpus/`.

### S5. Neural detector wiring
- ONNX detector service behind a `PitchDetector`-compatible facade,
  following the DeepFilterNet service template (deferred load, model in
  resources, VXSTUDIO_ flag-gated). Model choice (PESTO-class) is a
  decision gate — wire the scaffolding, leave the model slot documented,
  same pattern as SOTA_UPGRADE_SCOPING.md items 11-12.

### S6. MIDI guide input
- Optional MIDI input bus: held notes become a strong target prior
  (overrides key prior, not evidence). Blocked on F3's prior interface;
  spec: guide note = +large log-score on that pitch class ± octave window.

### S7. Docs/copy
- README section, help HTML refresh, web copy for VX Tune once F1 lands
  (align with `ProductIdentity.readmeSection` conventions).

### S8. Artifact-quality backoff (doc §5.10)
- Post-shift `ReadabilityGuard`/`ArtifactDetectors` check (framework
  pattern, see Denoiser): if artifact risk rises, scale correction blend
  down smoothly (never a jump). Disclosed in help text. Test: heavily
  artifacted synthetic case reduces blend; clean case leaves it at 1.0.

---

## Sequencing

F1 → F4 (listening) → F2 → F3 are ordered; S1/S2/S4/S7 can run any time;
S3 after S1; S5/S6/S8 after their noted dependencies. The product is
"cutting edge" per the vision doc when F1–F3 are in with F4's corpus
regression clips green, S1 shipped (Key/Scale is a v1 control), and S2
keeping it inside the suite performance bar.
