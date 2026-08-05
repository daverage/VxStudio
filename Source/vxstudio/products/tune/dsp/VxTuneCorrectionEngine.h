#pragma once

#include "VxTuneSegmenter.h"
#include "VxTuneTargetEstimator.h"
#include "VxTuneTimeline.h"

#include <array>
#include <cmath>

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
        float staleTargetReleaseCentsPerSec = 200.0f;
        // Low-pass on the note error so correction targets only the slow
        // component: residual vibrato leaking through the centre line must
        // not be chased back out of the signal.
        float errorCutoffHz = 1.0f;
        // Musical authority: correction is allowed to become firm only
        // when the frame looks reliable and settled. These are internal
        // guardrails, not user-facing controls.
        float confidenceFullAuthority = 0.82f;
        float confidenceAuthorityFloorNatural = 0.20f;
        float confidenceAuthorityFloorTight = 0.45f;
        float onsetProtectNatural = 0.95f;
        float onsetProtectTight = 0.70f;
        float transitionProtectNatural = 0.75f;
        float transitionProtectTight = 0.35f;
        float onsetReasonAuthorityNatural = 0.25f;
        float onsetReasonAuthorityTight = 0.55f;
        float ambiguousTargetMarginLog = 0.25f;
        float ambiguousTargetAuthorityNatural = 0.45f;
        float ambiguousTargetAuthorityTight = 0.75f;
        float invalidHoldAuthorityNatural = 0.15f;
        float invalidHoldAuthorityTight = 0.45f;
        float stableBoostMarginLog = 0.65f;
        float stableBoostMinErrorCents = 22.0f;
        float stableBoostResidualCents = 35.0f;
        float stableBoostConfidence = 0.75f;
        float stableBoostFloorNatural = 0.60f;
        float stableBoostFloorTight = 0.85f;
        float nearCorrectVetoCents = 18.0f;
        float nearCorrectVetoMarginLog = 1.20f;
        float nearCorrectVetoMaxAuthority = 0.35f;
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
    struct AuthoritySnapshot {
        float total = 0.0f;
        float confidence = 0.0f;
        float passing = 0.0f;
        float onset = 0.0f;
        float transition = 0.0f;
        float target = 0.0f;
        float stableBoost = 0.0f;
        float nearCorrect = 1.0f;
    };
    struct TargetSnapshot {
        bool valid = false;
        int midiNote = -1;
        int runnerUpMidiNote = -1;
        float errorCents = 0.0f;
        float marginLog = 0.0f;
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
        lastMusicalAuthority = 0.0f;
        lastTargetMarginLog = 0.0f;
        lastAuthority = {};
        lastTarget = {};
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
                  const std::uint16_t scaleMask = kChromaticMask,
                  const float speed = 0.5f, const float focus = 0.5f) noexcept {
        // The estimator runs every frame regardless of validity: a
        // decompConfidence of exactly zero (true unvoiced) is how it knows
        // to forget between phrases; weak-but-nonzero confidence just
        // contributes weak evidence rather than forcing a reset.
        const auto target = estimator.update(frame.centreCents, frame.decompConfidence,
                                              scaleMask, natural);
        lastTarget = {
            target.valid,
            target.midiNote,
            target.runnerUpMidiNote,
            target.errorCents,
            target.marginLog,
        };
        lastTargetMarginLog = target.valid ? target.marginLog : 0.0f;
        lastMusicalAuthority = 0.0f;
        lastAuthority = {};
        float desired = 0.0f;
        float releaseCentsPerSec = cfg.releaseCentsPerSec;
        const float amountScale = 2.0f * clamp01(amount);
        const float focus01 = clamp01(focus);
        const float speed01 = clamp01(speed);
        const float hardTune01 = focus01 > speed01 ? focus01 : speed01;
        const float focusAboveMid = focus01 > 0.5f ? (focus01 - 0.5f) * 2.0f : 0.0f;
        const float focusAssertive = std::sqrt(focusAboveMid);
        const float hardAboveNatural = hardTune01 > 0.35f ? (hardTune01 - 0.35f) / 0.65f : 0.0f;
        const float minConfidence = cfg.minDecompConfidence
            + (0.18f - cfg.minDecompConfidence) * focusAssertive;
        const float maxErrorCents = cfg.maxErrorCents
            + (300.0f - cfg.maxErrorCents) * focusAssertive;
        const float maxErrorGrowthCents = cfg.maxErrorGrowthCents
            + (90.0f - cfg.maxErrorGrowthCents) * focusAssertive;
        const bool validFrame = target.valid
            && frame.decompConfidence >= minConfidence;
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
                const float directness = std::sqrt(clamp01(hardAboveNatural));
                const float effectiveErrorAlpha = errorAlpha
                    + (1.0f - errorAlpha) * directness * directness;
                smoothedError += effectiveErrorAlpha * (error - smoothedError);
            }
            float deadZone = cfg.deadZoneMaxCents
                + (cfg.deadZoneMinCents - cfg.deadZoneMaxCents) * natural;
            if (focus01 > 0.75f)
                deadZone *= 1.0f - 0.85f * ((focus01 - 0.75f) / 0.25f);
            // Engage/disengage with hysteresis (exit well inside the entry
            // threshold): a single hard threshold flickers every frame the
            // error hovers near the boundary, which is audible as the
            // correction snapping on and off many times a second on real
            // (noisy) pitch estimates. Hysteresis plus the hold-through
            // below (invalidRun) are what make that boundary sticky.
            const float ae = absf(smoothedError);
            if (ae <= maxErrorCents) {
                if (engaged) {
                    if (ae < deadZone * kDeadZoneExitFactor)
                        engaged = false;
                } else if (ae > deadZone) {
                    engaged = true;
                }
            } else {
                engaged = false;
                haveError = false;
                releaseCentsPerSec = cfg.staleTargetReleaseCentsPerSec;
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
                && (ae - aeHistory[aeHistoryPos]) > maxErrorGrowthCents) {
                engaged = false;
                haveError = false;
                releaseCentsPerSec = cfg.staleTargetReleaseCentsPerSec;
            }
            aeHistory[aeHistoryPos] = ae;
            aeHistoryPos = (aeHistoryPos + 1) % kDivergenceWindowFrames;
            if (aeHistoryPos == 0)
                aeHistoryFull = true;

            if (engaged) {
                // Musical authority (Phase 2): a target can be numerically
                // nearest yet still musically unsafe to chase. The
                // correction therefore becomes strong only when evidence is
                // confident, settled, and unambiguous.
                lastAuthority = musicalAuthority(frame, target, segment, natural);
                if (focus01 < 0.5f) {
                    lastAuthority.total *= 0.5f + focus01;
                } else if (focus01 > 0.5f) {
                    const float lift = focusAssertive;
                    lastAuthority.total = clamp01(lastAuthority.total
                        + (1.0f - lastAuthority.total) * lift);
                    if (focus01 >= 0.92f && absf(target.errorCents) >= 6.0f)
                        lastAuthority.total = 1.0f;
                }
                lastMusicalAuthority = lastAuthority.total;
                const float effectiveAmount = amountScale * lastMusicalAuthority;
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
                const float tight = clamp01(natural);
                const float invalidAuthority = cfg.invalidHoldAuthorityNatural
                    + (cfg.invalidHoldAuthorityTight - cfg.invalidHoldAuthorityNatural) * tight;
                lastMusicalAuthority = invalidAuthority;
                lastAuthority.total = invalidAuthority;
                lastAuthority.confidence = invalidAuthority;
                desired = -smoothedError * amountScale * invalidAuthority;
            }
        }

        const float speedScale = speed01 <= 0.5f
            ? 0.35f + 1.30f * speed01
            : 1.0f + 4.0f * std::sqrt((speed01 - 0.5f) * 2.0f);
        const float slewPerSec = (desired != 0.0f
            ? cfg.slewMinCentsPerSec
                + (cfg.slewMaxCentsPerSec - cfg.slewMinCentsPerSec) * natural * natural
            : releaseCentsPerSec) * speedScale;
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
    float currentMusicalAuthority() const noexcept { return lastMusicalAuthority; }
    float currentTargetMarginLog() const noexcept { return lastTargetMarginLog; }
    AuthoritySnapshot currentAuthoritySnapshot() const noexcept { return lastAuthority; }
    TargetSnapshot currentTargetSnapshot() const noexcept { return lastTarget; }

private:
    static float absf(const float x) noexcept { return x < 0.0f ? -x : x; }
    static float clamp01(const float x) noexcept {
        if (x <= 0.0f)
            return 0.0f;
        if (x >= 1.0f)
            return 1.0f;
        return x;
    }
    static float prob(const NoteSegment* segment, const Behaviour behaviour) noexcept {
        return segment != nullptr
            ? segment->behaviourProb[static_cast<int>(behaviour)]
            : 0.0f;
    }

    AuthoritySnapshot musicalAuthority(const PitchFrame& frame,
                                       const TargetEstimator::Result& target,
                                       const NoteSegment* segment,
                                       const float natural) const noexcept {
        AuthoritySnapshot snapshot;
        const float tight = clamp01(natural);
        const float confidenceRange = cfg.confidenceFullAuthority - cfg.minDecompConfidence;
        const float confidenceRaw = confidenceRange > 0.01f
            ? (frame.decompConfidence - cfg.minDecompConfidence) / confidenceRange
            : 1.0f;
        const float confidenceFloor = cfg.confidenceAuthorityFloorNatural
            + (cfg.confidenceAuthorityFloorTight - cfg.confidenceAuthorityFloorNatural) * tight;
        snapshot.confidence = confidenceFloor
            + (1.0f - confidenceFloor) * clamp01(confidenceRaw);

        const float onsetProtect = cfg.onsetProtectNatural
            + (cfg.onsetProtectTight - cfg.onsetProtectNatural) * tight;
        const float transitionProtect = cfg.transitionProtectNatural
            + (cfg.transitionProtectTight - cfg.transitionProtectNatural) * tight;
        const float transitionProb = clamp01(prob(segment, Behaviour::slide)
            + prob(segment, Behaviour::scoop)
            + prob(segment, Behaviour::fall)
            + 0.5f * prob(segment, Behaviour::bend));

        snapshot.passing = 1.0f - prob(segment, Behaviour::passingNote);
        snapshot.onset = 1.0f - onsetProtect * prob(segment, Behaviour::onset);
        const float onsetReasonAuthority = frame.f0Hz.reason == EstimateReason::onsetTransient
            ? cfg.onsetReasonAuthorityNatural
                + (cfg.onsetReasonAuthorityTight - cfg.onsetReasonAuthorityNatural) * tight
            : 1.0f;
        snapshot.onset *= onsetReasonAuthority;
        snapshot.transition = 1.0f - transitionProtect * transitionProb;

        const float ambiguousAuthority = cfg.ambiguousTargetAuthorityNatural
            + (cfg.ambiguousTargetAuthorityTight - cfg.ambiguousTargetAuthorityNatural) * tight;
        snapshot.target = target.marginLog < cfg.ambiguousTargetMarginLog
            ? ambiguousAuthority
            : 1.0f;

        const float base = clamp01(snapshot.passing * snapshot.onset * snapshot.transition
                                   * snapshot.confidence * snapshot.target);

        const float stableProb = clamp01(prob(segment, Behaviour::sustain)
            + 0.65f * prob(segment, Behaviour::vibrato));
        const bool stableFrame =
            frame.f0Hz.reason == EstimateReason::nominal
            && frame.decompConfidence >= cfg.stableBoostConfidence
            && absf(target.errorCents) >= cfg.stableBoostMinErrorCents
            && absf(frame.residualCents) <= cfg.stableBoostResidualCents
            && target.marginLog >= cfg.stableBoostMarginLog;

        if (stableFrame && stableProb > 0.35f) {
            const float stableFloor = cfg.stableBoostFloorNatural
                + (cfg.stableBoostFloorTight - cfg.stableBoostFloorNatural) * tight;
            snapshot.stableBoost = stableFloor * stableProb;
        }

        snapshot.nearCorrect = 1.0f;
        if (absf(target.errorCents) <= cfg.nearCorrectVetoCents
            && target.marginLog < cfg.nearCorrectVetoMarginLog) {
            snapshot.nearCorrect = cfg.nearCorrectVetoMaxAuthority;
        }

        snapshot.total = clamp01(base + (1.0f - base) * snapshot.stableBoost);
        if (snapshot.total > snapshot.nearCorrect)
            snapshot.total = snapshot.nearCorrect;
        return snapshot;
    }

    Config cfg {};
    float frameRate = 187.5f;
    float errorAlpha = 0.03f;
    float correctionCents = 0.0f;
    float lastMusicalAuthority = 0.0f;
    float lastTargetMarginLog = 0.0f;
    AuthoritySnapshot lastAuthority {};
    TargetSnapshot lastTarget {};
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
