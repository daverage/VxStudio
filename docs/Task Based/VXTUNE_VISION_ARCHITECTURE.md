# VX Tune — Vision & Architecture

Standing reference for the VX Suite pitch product (working name **VX Tune**).
Status: **v2 — design locked 2026-08-03** after architecture review
(uncertainty objects, probabilistic behaviour, singer model + phrase layer as
modules, Bayesian target estimation, perceptual-cost decision engine,
intervention budget, performance decomposition elevated to core).
Implementation not started.

---

## 1. Vision

**A missed correction sounds like the singer; a wrong correction sounds like
a malfunction.** That asymmetry is the entire philosophy — every design
decision below follows from it.

We are not building another pitch corrector. We are building a system that
understands singing well enough to know when **not** to correct. The engine
never asks *"what note should I tune this to?"* It asks *"what is the singer
trying to do?"* — and only then decides whether correction is needed at all.

Success is not "the vocal is perfectly in tune." Success is **"it sounds like
the singer simply performed better."** Listeners should not notice
correction; they should only hear a more confident performance.

---

## 2. The defining idea — performance decomposition

Every observed pitch trajectory is a sum of layers:

```
Observed pitch = Intended pitch  (centre line)
              + Expression       (vibrato, bends, scoops, micro-drift — the residual)
              + Error            (performance error)
              + Noise            (measurement noise)
```

Current tuners estimate the note first and try to preserve vibrato
afterwards. VX Tune does the reverse:

1. **Separate** the performance into intent and expression.
2. **Correct** only the intent.
3. **Reapply** the untouched expression.

If the *Performance Decomposition* module consistently separates expressive
movement from genuine pitch error across singers and styles, everything
downstream becomes simpler — and the product is genuinely differentiated
rather than a well-executed clone. This module is the core intellectual
property of VX Tune. It gets the deepest investment, the richest test
corpus coverage, and the most protective interface.

---

## 3. Non-negotiable rules

Mechanisms, not aspirations. Every module inherits them.

1. **Asymmetric cost — when unsure, do nothing.** Formally: the decision
   engine minimises perceptual cost
   `C = C_error (listener notices bad singing) + C_tuning (listener notices
   tuning)`, and the weights are unequal — `C_tuning` dominates. *Tiny pitch
   error × zero tuning* frequently wins. Low confidence anywhere collapses
   the decision to no intervention.
2. **Correct the centre line, preserve the residual.** Correction applies
   only to the decomposed intent layer; the expression residual is re-added
   untouched. Vibrato preservation is structural, not classifier-dependent.
3. **Commit softly, revise smoothly, never jump.** The correction signal is
   a continuous trajectory with a permitted convergence rate; it may bend
   toward a revised target but may never step. A step is the "wobble"
   artifact and is forbidden by construction.
4. **Uncertainty is a first-class object, not a float.** Every estimate
   crosses a module boundary as `Estimate<T>` — value, confidence, *reason*,
   stability (§7.1). When the pitch detector degrades, downstream stages
   know *why* (periodicity collapsed vs. octave ambiguity vs. onset), and
   every decision the engine makes is explainable after the fact.
5. **Distributions, not verdicts.** Classification outputs are probability
   distributions (e.g. `45% vibrato / 40% sustain / 15% bend`), and
   downstream strategy interpolates across them. Hard mode-switching between
   behaviours is forbidden for the same reason steps in the correction curve
   are.
6. **Every module is replaceable behind its interface.** Hand-engineered
   today, tiny ONNX model tomorrow — detector, decomposition, behaviour
   analysis, singer model, key inference — with zero architecture change.
   The modularity test for any PR: swapping one module's implementation
   touches no other module.

---

## 4. Locked decisions (2026-08-03)

| # | Decision | Resolution |
|---|----------|-----------|
| 1 | Latency posture | **Real-time self-monitoring is the floor.** The core engine must be viable at tracking latency. A higher-quality deferred mode may use more lookahead later, but real-time is not a reduced afterthought. |
| 2 | Musical context | **Both.** Manual key/scale is always available and authoritative. Chromatic operation with note-intent inference covers unknown/changing/modal keys and borrowed notes. Auto key detection assists but is never the sole source of truth in v1. |
| 3 | Deferred / ARA mode | **Architect for it, don't ship it in v1.** Analysis, note/phrase representation, correction decisions, and rendering stay separated so an offline editor or ARA integration can be added without rewriting the DSP core. V1 is a focused real-time plugin. |
| 4 | Controls | **Three:** Amount, Natural↔Tight, Key/Scale (incl. chromatic + auto-assist). No separate transparency control. Everything else automatic unless testing proves users repeatedly need an override. |

### Control semantics (exact)

- **Amount** — how far correction moves: a single scalar on the final
  centre-line offset. Decides *how much of the identified error is removed*.
- **Natural ↔ Tight** — how aggressively movement is treated as error rather
  than expression. Internally it is the master scale on the **intervention
  budget** (§5.8) and the `C_tuning` weight. Decides *what counts as error*.
- **Key/Scale** — manual key/scale, chromatic, or auto-assist (§5.6).

These two axes never fight: at full Amount with a Natural setting, vibrato
and scoops still survive because they were never classified as error.

UI adds **feedback, not control**: a small live display of the detected note
and whether the engine is intervening. Trust in an "intelligent" plugin
comes from seeing it decide.

---

## 5. Architecture

```
audio
  ─► 5.1 Pitch Detection
  ─► 5.2 Performance Decomposition        ◄── the core
  ─► 5.3 Segmentation (phrase ▸ note ▸ frame)
  ─► 5.4 Singer Model
  ─► 5.5 Behaviour Analysis
  ─► 5.6 Musical Context
  ─► 5.7 Target Estimation (Bayesian fusion)
  ─► 5.8 Decision Engine (perceptual cost + intervention budget)
  ─► 5.9 Pitch Shifter (+ formant preservation)
  ─► 5.10 Quality Analysis
  ─► out
```

Each stage publishes `Estimate<T>` objects into the shared data model (§7)
and reads only from that model — no module reaches around its neighbours.

### 5.1 Pitch detection
- Primary: **pYIN-class probabilistic detector** — cheap, natively emits a
  pitch posterior.
- Secondary cross-check detector (spectral / SWIPE′-class) for octave-error
  vetoing.
- Emits `Estimate<f0>` whose *reason* field carries the degradation cause:
  `periodicity-collapse`, `low-snr`, `onset`, `octave-ambiguity`,
  `unvoiced`.
- Upgrade path: CREPE-class neural detector via the existing ONNX inference
  infrastructure (see DeepFilterNet product) — rule 6 applies.
- Analysis window (~25 ms for an 80 Hz low male fundamental) lags the write
  head and does **not** count against plugin latency; the cost is slightly
  stale estimates at onsets — one more reason onsets default to hands-off.

### 5.2 Performance decomposition — *the core*
Splits the raw pitch track into **centre line** (intended contour) and
**expression residual** (vibrato, jitter, drift, gesture micro-shape), each
with its own confidence and stability. Everything downstream consumes this
split; nothing downstream re-derives it. Solving this better than anyone
else — consistently, across singers and styles — is the product bet, so:

- it gets its own validation track in the corpus (human-labelled
  intent/expression splits, §10);
- its interface is frozen earliest and defended hardest;
- learned implementations (rule 6) are expected here first.

### 5.3 Segmentation — phrase ▸ note ▸ frame
Humans sing phrases, not notes. The timeline is hierarchical:

- **Frame** — hop-rate pitch/decomposition data.
- **Note segment** — fixed-lag HMM/Viterbi over the pitch posterior;
  segments are provisional while open and finalise as the lag passes (the
  soft-commit mechanism of rule 3).
- **Phrase segment** — phrase start/end, breath, climax, resolution.
  Bootstrapped from the framework's `VoiceContextSnapshot`
  (phraseActivity/phraseStart/phraseEnd) and refined with pitch-domain
  evidence. Phrase position feeds the decision engine: end-of-phrase falls
  are expression, first-note-after-breath intonation settles slower, the
  climax note tolerates more spread.

### 5.4 Singer model
Its own module, not a detail of target estimation. A slowly-evolving profile
learned over the song, published as `Estimate<SingerProfile>`:

preferred intonation offset · vibrato rate · average vibrato depth · pitch
drift tendency · typical scoop depth/length · typical settle time onto a
note ("correction latency") · comfortable range.

Good singers are internally consistent — this becomes one of the strongest
priors in §5.7, and it personalises behaviour analysis (e.g. *this* singer's
vibrato is 5.5 Hz ± 40 cents, so 6 Hz movement is vibrato, not instability).
Updates run off the audio thread (`DeferredAnalysis`); state persists across
transport stops within a session.

### 5.5 Behaviour analysis
Emits a **probability distribution** over behaviours per note segment
(rule 5):

`sustain · vibrato · bend · slide · scoop · fall · passing-note · onset ·
unvoiced`

(No `uncertain` class — uncertainty is the distribution's entropy plus the
`Estimate` confidence, not a bucket.) v1 implementation: hand-built
interpretable features on the decomposed track — derivative statistics,
residual modulation rate/extent (autocorrelation at 4–7 Hz), segment
duration, distance-to-target, onset shape — normalised against the singer
model. Deterministic and debuggable: you can answer "why did it flatten my
bend" from the reason fields. A learned classifier is a later drop-in
(rule 6).

### 5.6 Musical context
Maintains the pitch-class prior from the Key/Scale control: manual key =
strong prior, chromatic = flat prior, auto-assist = key estimate learned off
the audio thread with capped weight (never sole source of truth in v1).

### 5.7 Target estimation — Bayesian fusion
The engine, stated explicitly:

```
P(target | everything) ∝ P(pitch evidence | target)      — what was sung
                       × P(target | key/scale)           — musical context prior
                       × P(target | singer model)        — this singer's tendencies
                       × P(target | phrase position)     — where in the phrase
                       × P(target | behaviour)           — what kind of gesture
```

Everything contributes evidence; nothing hardcodes rules. Borrowed and blue
notes fall out naturally — strong consistent evidence for an out-of-scale
target overrides the prior instead of being "wrong." Implementation note:
realised as **log-domain additive scores over 12 pitch classes × octave**,
updated per frame — a handful of adds and a max, fully realtime-safe. The
Bayesian statement is the design contract; the log-score table is the code.
Output: `Estimate<targetNote>` including the runner-up and margin (the
margin drives convergence caution in §5.8).

### 5.8 Decision engine — perceptual cost, one budget
Objective (rule 1): minimise `C_error + C_tuning`, weights set by
Natural↔Tight. The engine's outputs per segment: planned centre-line offset
(`correctionCents` at full Amount) and permitted `convergenceRate` —
strategy is *interpolated across the behaviour distribution*, never switched
(rule 5).

All adaptive behaviour spends from a single internal **intervention budget**
(denominated in perceptual-cost units, replenished per phrase) rather than
several adaptive systems fighting each other: correction depth, convergence
speed, formant adjustment, and revision frequency all draw from it; low
confidence anywhere raises the price of intervening. Natural↔Tight is the
budget's master scale. One arbiter, explainable spend log in dev builds.

### 5.9 Pitch shifter + formant preservation
TD-PSOLA-family monophonic shifter with formant preservation. Transparent
correction is mostly < 1 semitone; large creative shifts are explicitly not
this product's job. The shifter consumes a correction curve; it knows
nothing about notes, targets, or decisions.

### 5.10 Quality analysis
Post-shift artifact check (framework `ReadabilityGuard` /
`ArtifactDetectors` pattern): if the rendered correction is producing
artifacts, back off the blend — disclosed protection, per suite purity
rules. Feeds back as a cost term into the intervention budget.

---

## 6. Latency & real-time budget

- **Target plugin latency: ≤ ~12 ms @ 48 kHz** (dominated by the shifter's
  period buffering). Report from the active DSP path only, per framework
  latency rules.
- Decision lag is hidden by convergence (rule 3), not by buffer delay:
  targets may firm up over ~40–80 ms, but the audio path does not wait.
- The future **deferred mode** is the same engine with a longer decision
  horizon (fewer revisions), not a different algorithm. The decision horizon
  is a constructor parameter of segmentation + decision stages from day one.
- All processing realtime-safe per framework rules: no allocation, locks, or
  I/O in `processBlock`. Heavy one-shot work (auto key, singer-model
  updates) goes through `DeferredAnalysisRunner` + `RealtimeResultMailbox`
  (`VxStudioDeferredAnalysis.h`).

---

## 7. The data model — the real API

The structures between analysis and rendering are the seam that keeps ARA
addable without a rewrite: an offline editor is "a UI over the phrase/note
timeline plus a re-render." Treat these as stable contracts; extend by
adding fields.

### 7.1 `Estimate<T>` — the uncertainty object (rule 4)

```cpp
enum class EstimateReason : uint8_t {
    nominal, periodicityCollapse, lowSnr, onsetTransient,
    octaveAmbiguity, unvoiced, sparseEvidence, conflictingEvidence
};

template <typename T>
struct Estimate {
    T             value;
    float         confidence;   // 0..1
    EstimateReason reason;      // why confidence is what it is
    float         stability;    // 0..1, how fast this estimate is moving
};
```

Every module boundary speaks `Estimate<T>`. The reason field is what makes
decisions debuggable and, in dev builds, explainable in the overlay.

### 7.2 Frame / note / phrase timeline

```cpp
// Hop-rate frame data (every 64–128 samples)
struct PitchFrame {
    int64_t timeSamples;
    Estimate<float> f0Hz;        // reason carries detector degradation cause
    float   voicedProb;
    float   centreHz;            // decomposed intent layer
    float   residualCents;       // expression layer (preserved verbatim)
    float   decompConfidence;    // how cleanly the split held this frame
    float   levelDb;
};

enum class Behaviour : uint8_t {
    unvoiced, onset, sustain, vibrato, bend, slide, scoop, fall, passingNote,
    count
};

struct NoteSegment {
    int64_t startSample;
    int64_t endSample;                       // provisional while open
    bool    open;                            // inside the decision horizon

    // analysis
    float   medianCentreCents;               // vs A440 reference
    float   segmentConfidence;
    float   behaviourProb[(int) Behaviour::count]; // distribution, sums to 1
    float   behaviourEntropy;                // uncertainty of the distribution
    float   vibratoRateHz, vibratoExtentCents;
    float   driftCentsPerSec;

    // intent
    Estimate<int> targetMidiNote;            // value -1 = no target (ignore)
    int     runnerUpMidiNote;
    float   targetMarginLog;                 // posterior margin over runner-up

    // decision
    float   correctionCents;                 // centre-line offset at full Amount
    float   convergenceRate;                 // permitted approach speed
    float   budgetSpent;                     // perceptual-cost units consumed
};

struct PhraseSegment {
    int64_t startSample, endSample;
    bool    open;
    float   breathConfidence;                // boundary was a breath
    int     climaxNoteIndex;                 // -1 = none identified
    int     resolutionNoteIndex;             // -1 = none identified
    float   budgetRemaining;                 // intervention budget state
};

struct SingerProfile {                       // published as Estimate<SingerProfile>
    float intonationOffsetCents;             // systematic sharp/flat tendency
    float vibratoRateHz, vibratoDepthCents;
    float driftCentsPerSec;
    float scoopDepthCents, scoopLengthMs;
    float settleTimeMs;                      // typical latency onto a note
    float rangeLowMidi, rangeHighMidi;
    float observationSeconds;                // how much evidence backs this
};
```

Interface discipline: if a correction decision ever leaks into the shifter,
or the detector leaks into the decision engine, the ARA path becomes a
rewrite.

---

## 8. VX Suite product template

- **Product name:** VX Tune (working)
- **Short tag:** TUN
- **Problem solved in one sentence:** Fixes vocal pitch errors while leaving
  the performance — vibrato, bends, phrasing — untouched.
- **Primary outcome:** the singer sounds like they performed better.
- **Secondary outcome:** zero audible "tuned" artifact at default settings.
- **Default mode:** none — vocal-only product; no `Voice`/`General` split
  (the DSP has no meaningful general-material map).

### UX
- **Knob 1 — Amount** (default gentle-active per suite default-tuning
  learning, e.g. ~0.6; hint: "How much pitch error is removed")
- **Knob 2 — Natural ↔ Tight** (default toward Natural; hint: "When movement
  counts as error")
- **Control 3 — Key/Scale** (justified third control: key + scale selector
  with `Chromatic` and `Auto-assist` entries)
- **Listen toggle:** no. Raw input−output delta of a pitch shift is not a
  meaningful audition signal. Revisit only if a "hear corrections only"
  rendering proves useful.
- **Feedback display:** detected note + intervention indicator (small, part
  of the status area — not a meter wall).
- **Hidden in v1:** retune-speed, per-behaviour overrides, vibrato depth,
  formant controls, MIDI-guide input.

### DSP contract
- **Inputs:** mono vocal focus; stereo handled per framework mono-corrective
  rules (correct a mono/mid analysis path, document the exact stereo
  re-entry, latency-aligned).
- **Latency:** fixed, ≤ ~12 ms @ 48 kHz, reported from active path.
- **Failure-safe behaviour:** unvoiced/uncertain/silent input passes through
  bit-transparent apart from the constant latency.
- **State never reset during playback:** singer profile, auto-key prior,
  smoothed controls.
- **State reset on transport/silence:** open segments, shifter overlap
  state, convergence trajectories, per-phrase budget (use phrase boundaries
  from `VoiceContextSnapshot` for artifact-free resets, per the denoiser
  FIFO pattern).

### Framework hooks
`ProcessorBase` / `EditorBase` / `ProductIdentity`; product DSP in
`Source/vxstudio/products/tune/dsp/`; shared candidates for
`Source/vxstudio/framework/`: pitch tracker + pYIN posterior (plausible
future use by analyser/repair), `Estimate<T>`, the timeline types. Reuse:
`VoiceContext` (phrase bootstrap, speech presence gates detection),
`DeferredAnalysis` (auto-key, singer-model updates), `SpectrumTelemetry`
pattern for the Phase 1 overlay, `SilenceGuard`, `BlockSmoothedControl`.

---

## 9. Phasing

### Phase 1 — Analysis only (highest risk first, no shifting)
Pitch tracking, **performance decomposition**, phrase/note segmentation,
behaviour distributions, singer model, target estimation — rendered as a
visual overlay showing values *and* Estimate reasons (telemetry → editor,
dev build only; shipping UI keeps the suite's no-meter rule).

**Gate (primary):** on real vocal takes, the decomposition's
intent/expression split matches human-labelled splits — this is the core
bet and it is validated before anything is built on it.
**Gate (secondary):** the system's overall story about the performance
(segments, behaviours, targets) matches what a musician hears.

Phase 1 also starts the **annotated vocal corpus**: varied takes with
human-labelled behaviour distributions, target notes, and
intent/expression splits. This corpus is the project's most valuable asset.

### Phase 2 — Correction
Decision engine (perceptual cost + intervention budget, do-nothing
default), centre-line correction, TD-PSOLA shifter + formant preservation,
three-control UI, fixed-lag real-time mode. **Gate:** validation suite
below passes; A/B listening on corpus material.

### Phase 3 — Depth
Learned decomposition and behaviour models behind the frozen interfaces
(rule 6), singer-consistency refinement, deferred/offline high-quality mode
(longer horizon), neural detector swap-in, ARA/editor exploration over the
phrase/note timeline.

---

## 10. Verification (per suite effect-validation rules)

Proven separately: the plugin changes audio; the change moves in the
intended direction; **what should be preserved is preserved**; and the
decomposition itself is sound.

- **Decomposition metric (core):** on corpus takes with labelled
  intent/expression splits, centre-line error and residual-preservation
  error against the labels below threshold; reconstruction identity
  `centre + residual == observed` holds to numerical precision.
- **Outcome metric:** median |cents-to-target| on segments labelled
  *held wrong note* must decrease with Amount; test fails if effectively
  dry.
- **Preservation metrics:** on *vibrato*-dominant segments, output vibrato
  extent ≥ 90% of input at any Natural↔Tight ≤ neutral; on
  *bend/scoop/slide*-dominant segments, gesture-shape correlation against
  input above threshold.
- **Do-nothing proof:** on in-tune reference takes, output−input delta
  below audibility threshold at default settings.
- **No-jump proof:** correction curve first-difference bounded by the
  permitted convergence rate — never a step, including across target
  revisions **and across behaviour-distribution shifts** (strategy
  interpolation, rule 5).
- **Budget sanity:** total per-phrase budget spend is monotone in
  Natural↔Tight; zero-budget (full Natural, Amount 0) nulls against the
  do-nothing path.
- Standard suite battery: bypass transparency, automation continuity,
  prepare/reset stability, sample-rate coverage (44.1–192 k), large host
  blocks, silence→voice recovery, latency-aligned comparisons, profile +
  allocation tests like the other 15 plugins.

---

## 11. Objective, restated

This is a **musical decision engine**, not a pitch correction algorithm.
Pitch shifting is only the final stage. The value lies in the performance
decomposition — separating what the singer meant from how they expressed it
— and in deciding *whether*, *how much*, and *when* to correct while
preserving everything that makes a human performance feel human.
