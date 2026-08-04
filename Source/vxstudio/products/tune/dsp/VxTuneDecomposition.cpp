#include "VxTuneDecomposition.h"

#include <algorithm>
#include <cmath>

namespace vxsuite::tune {

namespace {

constexpr float kA440 = 440.0f;
constexpr float kTwoPi = 6.28318530717958647692f;
constexpr int kFastTrackSettleFrames = 6;

float hzToCents(const float hz) noexcept {
    return 1200.0f * std::log2(hz / kA440);
}

float centsToHz(const float cents) noexcept {
    return kA440 * std::exp2(cents / 1200.0f);
}

} // namespace

void PerformanceDecomposition::prepare(const double frameRateHz, const Config& config) {
    cfg = config;
    const double rate = frameRateHz > 1.0 ? frameRateHz : 187.5;
    alpha = 1.0f - static_cast<float>(std::exp(-kTwoPi * cfg.centreCutoffHz / rate));
    reset();
}

void PerformanceDecomposition::reset() {
    stage1Cents = 0.0f;
    centreCents = 0.0f;
    anchored = false;
    deviationRun = 0;
    settleRemaining = 0;
}

PitchFrame PerformanceDecomposition::process(const PitchObservation& observation) noexcept {
    PitchFrame frame;
    frame.timeSamples = observation.timeSamples;
    frame.f0Hz = observation.f0Hz;
    frame.voicedProb = observation.voicedProb;
    frame.levelDb = observation.levelDb;

    const bool voiced = observation.f0Hz.value > 0.0f && observation.f0Hz.confidence > 0.0f;
    if (!voiced) {
        // Hold the last centre value through unvoiced gaps for display
        // continuity, but drop the anchor so the next voiced frame snaps
        // fresh instead of gliding from a stale note.
        anchored = false;
        frame.centreCents = centreCents;
        frame.centreHz = 0.0f;
        frame.residualCents = 0.0f;
        frame.decompConfidence = 0.0f;
        return frame;
    }

    const float rawCents = hzToCents(observation.f0Hz.value);
    // Trustworthiness for anchoring/tracking decisions: confidence alone
    // isn't enough. A detector glitch (consonant, brief tracking loss) can
    // report a high per-frame confidence while the estimate itself is
    // jumping wildly frame to frame - exactly the case that used to slip
    // through and drag the centre line off with it. stability (frame-to-
    // frame consistency) catches that; this is the same blend already
    // reported outward as decompConfidence, just also used internally.
    const float trust = observation.f0Hz.confidence
        * (0.5f + 0.5f * observation.f0Hz.stability);
    const bool confident = trust >= cfg.anchorConfidence;

    if (!anchored && !confident) {
        // Nothing trustworthy to anchor to yet: report the observation but
        // leave the intent line unset rather than founding it on noise.
        frame.centreCents = centreCents;
        frame.centreHz = 0.0f;
        frame.residualCents = 0.0f;
        frame.decompConfidence = 0.0f;
        return frame;
    }

    const float deviation = std::abs(rawCents - centreCents);
    const bool wasSettling = settleRemaining > 0;
    if (anchored && confident && !wasSettling && deviation > cfg.persistentDeviationCents)
        ++deviationRun;
    else if (confident)
        deviationRun = 0;

    // Only a voicing onset or a single-frame jump too large to be
    // expression gets an instant snap (centre := raw). A SUSTAINED
    // deviation (deviationRun) is deliberately NOT snapped the same way
    // anymore: a real note reached through intermediate frames and a
    // continuous glide/portamento look identical at this point (deviation
    // built up gradually over a few frames either way), and portamento is
    // extremely common in real singing. Snapping produced a real,
    // measured bug: mid-glide, centre would jump ~100c in one frame while
    // the actual pitch had moved only ~10-15c, because centre (slow
    // low-pass) simply couldn't keep pace with a fast glide and tripped
    // the persistent-deviation threshold - reported as "pitch constantly
    // jumping". Fast-tracking (settle boost, no jump) converges just as
    // quickly for a genuine note change while tracking a glide smoothly.
    const bool hardReset = !anchored || (confident && deviation > cfg.snapJumpCents);
    const bool fastTrack = !hardReset && deviationRun >= cfg.persistentDeviationFrames;

    if (hardReset) {
        stage1Cents = rawCents;
        centreCents = rawCents;
        anchored = true;
        deviationRun = 0;
        settleRemaining = cfg.settleFrames;
    } else {
        if (fastTrack) {
            // Deliberately shorter than the post-hard-reset settle window:
            // at this alpha (~0.5/frame once boosted) the gap is ~95%
            // closed within a handful of frames, so a short window
            // catches up to a genuine glide/note-reach without staying
            // exposed to boosted-alpha tracking through whatever
            // low-confidence noise follows (that longer exposure was
            // itself a regression - see file header note).
            settleRemaining = kFastTrackSettleFrames;
            deviationRun = 0;
        }
        // Confidence-weighted smoothing: unreliable observations move the
        // intended contour less. During settling (post-snap, or now
        // post-fast-track) the centre converges faster so it catches up to
        // an extreme or a fast glide within about one cycle instead of
        // lagging indefinitely or flip-flopping.
        const bool settlingNow = wasSettling || fastTrack;
        const float boost = settlingNow ? cfg.settleBoost : 1.0f;
        const float a = std::clamp(alpha * boost * std::clamp(trust, 0.05f, 1.0f),
                                   0.0f, 0.5f);
        stage1Cents += a * (rawCents - stage1Cents);
        centreCents += a * (stage1Cents - centreCents);
        if (settlingNow)
            --settleRemaining;
    }

    frame.centreCents = centreCents;
    frame.centreHz = centsToHz(centreCents);
    frame.residualCents = rawCents - centreCents;   // identity by construction

    // The split is only as trustworthy as the pitch it was computed from,
    // and it needs a settled estimate to mean anything.
    frame.decompConfidence = trust;
    return frame;
}

} // namespace vxsuite::tune
