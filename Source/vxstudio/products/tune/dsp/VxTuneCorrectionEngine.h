#pragma once

#include "VxTuneTargetEstimator.h"
#include "VxTuneTimeline.h"

namespace vxsuite::tune {

// Correction decision (architecture doc §5.8): a Bayesian target estimator
// (VxTuneTargetEstimator, build spec F3) decides which note the singer
// intends, evidence-weighted over time rather than re-decided from scratch
// every frame. This engine then decides whether/how hard to chase that
// target: corrected only when the error clears a Natural-dependent dead
// zone, approached at a slew-limited rate so the correction trajectory can
// bend but never jump (rule 3). Low decomposition confidence collapses the
// decision toward "do nothing" (rule 1).
//
// Behaviour interpolation and the intervention budget replace the dead-zone/
// slew internals later behind this same frame-rate interface.
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
        // A candidate target must be the estimator's top pick for this many
        // CONSECUTIVE frames before correction is allowed to engage/switch
        // onto it. Without this, a fast run/leap/ad-lib that only passes
        // near a chromatic note for a handful of frames gets treated as a
        // held note worth correcting to - measured directly on real
        // material where the correction chased a genuine fast vocal swing
        // as if it were a target, producing audible mis-tracking. Shrinks
        // toward Tight like the dead zone (decide faster, more willing to
        // treat brief evidence as real).
        float targetHoldFramesMax = 10.0f;  // at full Natural
        float targetHoldFramesMin = 3.0f;   // at full Tight
    };

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
        reset();
    }

    void reset() {
        correctionCents = 0.0f;
        smoothedError = 0.0f;
        haveError = false;
        engaged = false;
        invalidRun = 0;
        lastTargetMidi = -1;
        targetHoldFrames = 0;
        estimator.reset();
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
        if (validFrame) {
            invalidRun = 0;

            if (target.midiNote != lastTargetMidi) {
                lastTargetMidi = target.midiNote;
                targetHoldFrames = 1;
            } else if (targetHoldFrames < 1000000) {
                ++targetHoldFrames;
            }
            const float requiredHold = cfg.targetHoldFramesMax
                + (cfg.targetHoldFramesMin - cfg.targetHoldFramesMax) * natural;
            const bool targetProven =
                static_cast<float>(targetHoldFrames) >= requiredHold;

            if (targetProven) {
                const float error = target.errorCents;
                // Track only the slow error component; snap on
                // discontinuities (new note) instead of gliding across
                // them.
                if (!haveError || absf(error - smoothedError) > 100.0f) {
                    smoothedError = error;
                    haveError = true;
                } else {
                    smoothedError += errorAlpha * (error - smoothedError);
                }
                const float deadZone = cfg.deadZoneMaxCents
                    + (cfg.deadZoneMinCents - cfg.deadZoneMaxCents) * natural;
                // Engage/disengage with hysteresis (exit well inside the
                // entry threshold): a single hard threshold flickers every
                // frame the error hovers near the boundary, which is
                // audible as the correction snapping on and off many times
                // a second on real (noisy) pitch estimates. Hysteresis plus
                // the hold-through below (invalidRun) are what make that
                // boundary sticky.
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
                if (engaged)
                    desired = -smoothedError * amount;
            } else {
                // Not yet proven: ride out whatever the last PROVEN target
                // was doing rather than reacting to a candidate that has
                // only just appeared. haveError is dropped so that once
                // this (or the next) target does clear the hold, it snaps
                // fresh from the settled pitch instead of gliding in from a
                // stale reference computed against a different note.
                haveError = false;
                if (engaged)
                    desired = -smoothedError * amount;
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
    int lastTargetMidi = -1;
    int targetHoldFrames = 0;
    TargetEstimator estimator;
};

} // namespace vxsuite::tune
