#pragma once

#include "VxTuneSegmenter.h"
#include "VxTuneTargetEstimator.h"
#include "VxTuneTimeline.h"

#include <array>

namespace vxsuite::tune {

// Correction decision (architecture doc §5.8): a Bayesian target estimator
// (VxTuneTargetEstimator, build spec F3) decides which note the singer
// intends, evidence-weighted over time rather than re-decided from scratch
// every frame. A segmenter (VxTuneSegmenter, build spec F2) independently
// tracks note segments and a behaviour PROBABILITY DISTRIBUTION (sustain,
// vibrato, bend, slide, scoop, fall, passing-note, onset) over the same
// stream. This engine then decides whether/how hard to chase the
// estimated target: corrected only when the error clears a Natural-
// dependent dead zone, approached at a slew-limited rate so the correction
// trajectory can bend but never jump (rule 3), and scaled by how much the
// behaviour distribution currently reads as "passing note" rather than a
// held one - a continuous, evidence-based replacement for what used to be
// a fixed dwell-frame count (see targetEffectiveAmount below). Low
// decomposition confidence collapses the decision toward "do nothing"
// (rule 1).
//
// The intervention budget replaces the dead-zone/slew internals later
// behind this same frame-rate interface.
class CorrectionEngine {
public:
    struct Config {
        float minDecompConfidence = 0.4f;
        // Dead zone shrinks as Natural->Tight rises; slew speeds up.
        float deadZoneMaxCents = 30.0f;    // at full Natural
        float deadZoneMinCents = 3.0f;     // at full Tight
        float slewMinCentsPerSec = 100.0f; // at full Natural
        float slewMaxCentsPerSec = 600.0f; // at full Tight
        float releaseCentsPerSec = 200.0f; // decay rate toward zero correction
        // Low-pass on the note error so correction targets only the slow
        // component: residual vibrato leaking through the centre line must
        // not be chased back out of the signal.
        float errorCutoffHz = 1.0f;
        // A centre further than this from every allowed note is treated as
        // intentional (chromatic passing tone against the scale): do
        // nothing rather than yank it more than a semitone.
        float maxErrorCents = 120.0f;
        // If the (engaged) error has grown by more than this many cents
        // over the last kDivergenceWindowFrames despite active correction,
        // the target is stale - give up on it rather than keep chasing.
        // A net-over-a-window comparison (not a strict consecutive-frame
        // streak) so ordinary pitch jitter, which briefly interrupts an
        // otherwise real divergence, can't reset the count and let it
        // continue almost indefinitely. Measured directly: a real,
        // sustained upward glide left the estimator's locked target
        // behind; the error grew smoothly and correction actively
        // pitch-bent the real, correct singing in the wrong direction for
        // ~400ms before maxErrorCents would eventually have caught it.
        float maxErrorGrowthCents = 15.0f;
    };
    static constexpr int kDivergenceWindowFrames = 12;

    static constexpr std::uint16_t kChromaticMask = TargetEstimator::kChromaticMask;
    // Dead-zone hysteresis: once engaged, the error must fall back inside
    // this fraction of the dead zone before disengaging.
    static constexpr float kDeadZoneExitFactor = 0.5f;
    // Consecutive invalid (unvoiced/low-confidence) frames tolerated before
    // treating a dropout as real rather than a brief blip.
    static constexpr int kInvalidHoldFrames = 2;

    void prepare(const double frameRateHz, const Config& config) {
        cfg = config;
        frameRate = frameRateHz > 1.0 ? static_cast<float>(frameRateHz) : 187.5f;
        errorAlpha = 6.2831853f * cfg.errorCutoffHz / frameRate;
        if (errorAlpha > 1.0f)
            errorAlpha = 1.0f;
        estimator.prepare(frameRateHz, {});
        segmenter.prepare(frameRateHz, {});
        reset();
    }

    void reset() {
        correctionCents = 0.0f;
        smoothedError = 0.0f;
        haveError = false;
        engaged = false;
        invalidRun = 0;
        aeHistory.fill(0.0f);
        aeHistoryPos = 0;
        aeHistoryFull = false;
        estimator.reset();
        segmenter.reset();
    }

    // One call per analysis frame. amount/natural are the two user controls
    // (0..1, natural: 0 = fully natural, 1 = tight); scaleMask is a 12-bit
    // pitch-class filter (bit 0 = C) restricting correction targets.
    // Returns the correction to apply in cents, slew-limited across calls.
    float process(const PitchFrame& frame, const float amount, const float natural,
                  const std::uint16_t scaleMask = kChromaticMask) noexcept {
        // The estimator runs every frame regardless of validity: a
        // decompConfidence of exactly zero (true unvoiced) is how it knows
        // to forget between phrases; weak-but-nonzero confidence just
        // contributes weak evidence rather than forcing a reset.
        const auto target = estimator.update(frame.centreCents, frame.decompConfidence,
                                              scaleMask, natural);
        float desired = 0.0f;
        const bool validFrame = target.valid
            && frame.decompConfidence >= cfg.minDecompConfidence;
        // Segmenter runs alongside the estimator (independently - it must
        // not depend on which note the estimator favours, see file
        // header). unvoiced/near-unvoiced frames close the segment via the
        // same grace period as everything else below, not on every blip.
        const NoteSegment* segment = nullptr;
        if (target.valid)
            segment = &segmenter.process(frame);

        if (validFrame) {
            invalidRun = 0;
            const float error = target.errorCents;
            // Track only the slow error component; snap on discontinuities
            // (new note) instead of gliding across them.
            if (!haveError || absf(error - smoothedError) > 100.0f) {
                smoothedError = error;
                haveError = true;
            } else {
                smoothedError += errorAlpha * (error - smoothedError);
            }
            const float deadZone = cfg.deadZoneMaxCents
                + (cfg.deadZoneMinCents - cfg.deadZoneMaxCents) * natural;
            // Engage/disengage with hysteresis (exit well inside the entry
            // threshold): a single hard threshold flickers every frame the
            // error hovers near the boundary, which is audible as the
            // correction snapping on and off many times a second on real
            // (noisy) pitch estimates. Hysteresis plus the hold-through
            // below (invalidRun) are what make that boundary sticky.
            const float ae = absf(smoothedError);
            if (ae <= cfg.maxErrorCents) {
                if (engaged) {
                    if (ae < deadZone * kDeadZoneExitFactor)
                        engaged = false;
                } else if (ae > deadZone) {
                    engaged = true;
                }
            } else {
                engaged = false;
            }

            // Divergence guard: if the (engaged) error has grown net over
            // the last kDivergenceWindowFrames despite active correction,
            // the target is stale - a genuine glide has moved past
            // whatever note we locked onto, and continuing to chase it
            // means actively pitch-bending real, correct singing in the
            // wrong direction. haveError is also dropped so the next
            // valid frame re-evaluates fresh instead of gliding in from
            // the abandoned reference.
            if (engaged && aeHistoryFull
                && (ae - aeHistory[aeHistoryPos]) > cfg.maxErrorGrowthCents) {
                engaged = false;
                haveError = false;
            }
            aeHistory[aeHistoryPos] = ae;
            aeHistoryPos = (aeHistoryPos + 1) % kDivergenceWindowFrames;
            if (aeHistoryPos == 0)
                aeHistoryFull = true;

            if (engaged) {
                // Behaviour-weighted amount (F2, replaces the old fixed
                // dwell-frame count): the segmenter's passing-note
                // probability continuously scales how hard correction
                // commits, rather than a binary "proven yet?" gate. A
                // segment that just opened reads almost entirely as
                // passing-note (effective amount near zero); as it holds,
                // that probability decays and full amount phases back in.
                // Measured directly on real material: a fast vocal run/leap
                // that only grazes a chromatic note for a handful of frames
                // no longer gets chased as if it were a held target.
                const float passingNoteProb = segment != nullptr
                    ? segment->behaviourProb[static_cast<int>(Behaviour::passingNote)]
                    : 0.0f;
                const float effectiveAmount = amount * (1.0f - passingNoteProb);
                desired = -smoothedError * effectiveAmount;
            }
        } else {
            // A single low-confidence/unvoiced frame is normal mid-word
            // (plosives, brief detector noise) and must not reset the
            // decision every time - that reset was another on/off-flicker
            // source distinct from the dead-zone boundary above. Only give
            // up the held state after a short run of consecutive invalid
            // frames confirms it's a real gap, not a blip.
            if (++invalidRun > kInvalidHoldFrames) {
                haveError = false;
                engaged = false;
                segmenter.closeSegment();
            } else if (engaged) {
                desired = -smoothedError * amount;
            }
        }

        const float slewPerSec = desired != 0.0f
            ? cfg.slewMinCentsPerSec
                + (cfg.slewMaxCentsPerSec - cfg.slewMinCentsPerSec) * natural * natural
            : cfg.releaseCentsPerSec;
        const float maxStep = slewPerSec / frameRate;

        const float delta = desired - correctionCents;
        if (delta > maxStep)
            correctionCents += maxStep;
        else if (delta < -maxStep)
            correctionCents -= maxStep;
        else
            correctionCents = desired;
        return correctionCents;
    }

    float currentCorrectionCents() const noexcept { return correctionCents; }
    const NoteSegment& currentSegment() const noexcept { return segmenter.currentSegment(); }

private:
    static float absf(const float x) noexcept { return x < 0.0f ? -x : x; }

    Config cfg {};
    float frameRate = 187.5f;
    float errorAlpha = 0.03f;
    float correctionCents = 0.0f;
    float smoothedError = 0.0f;
    bool haveError = false;
    bool engaged = false;
    int invalidRun = 0;
    std::array<float, kDivergenceWindowFrames> aeHistory {};
    int aeHistoryPos = 0;
    bool aeHistoryFull = false;
    TargetEstimator estimator;
    Segmenter segmenter;
};

} // namespace vxsuite::tune
