#pragma once

#include "VxTuneTimeline.h"

namespace vxsuite::tune {

// Performance decomposition — the core of VX Tune (architecture doc §2, §5.2).
// Splits each pitch observation into a centre line (the intended contour,
// which is all later correction may touch) and an expression residual
// (vibrato/jitter/drift, preserved verbatim).
//
// Invariant: for voiced frames, centreCents + residualCents equals the raw
// pitch in cents exactly — reconstruction identity holds by construction
// because the residual is computed as raw minus centre.
//
// v1 centre estimator: two cascaded confidence-weighted one-pole low-passes
// (~1.5 Hz at the analysis frame rate), snapped to the raw pitch on voicing
// onsets and on jumps too large to be expression. Vibrato at 4–7 Hz passes
// almost entirely into the residual; held-note drift moves the centre.
// Replaceable behind this interface (doc rule 6).
class PerformanceDecomposition {
public:
    struct Config {
        float centreCutoffHz = 1.5f;    // centre-line responsiveness
        float snapJumpCents = 200.0f;   // instant re-anchor (single-frame jump)
        // A raw pitch that stays this far from the centre for this many
        // consecutive frames is a new note, not expression: re-anchor.
        // (Vibrato residual stays well under this; detector transitions
        // through a note change produce sub-snapJump steps that this catches.)
        float persistentDeviationCents = 80.0f;
        int persistentDeviationFrames = 4;
        // Minimum pitch confidence to (re-)anchor the centre. Low-confidence
        // frames (onsets, transition artifacts) must never move the intent
        // line — when unsure, do nothing (architecture doc rule 1).
        float anchorConfidence = 0.6f;
        // After any snap the centre converges faster for a short settle
        // window, so an anchor that landed on an expression extreme (e.g. a
        // vibrato peak) slides to the true mean within about one cycle
        // instead of triggering re-anchor flip-flop between extremes.
        int settleFrames = 24;
        float settleBoost = 6.0f;
    };

    void prepare(double frameRateHz, const Config& config);
    void reset();

    PitchFrame process(const PitchObservation& observation) noexcept;

private:
    Config cfg {};
    float alpha = 0.05f;
    float stage1Cents = 0.0f;
    float centreCents = 0.0f;
    bool anchored = false;
    int deviationRun = 0;
    int settleRemaining = 0;
};

} // namespace vxsuite::tune
