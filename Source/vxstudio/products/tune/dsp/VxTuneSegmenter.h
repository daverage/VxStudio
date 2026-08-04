#pragma once

#include "VxTuneTimeline.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace vxsuite::tune {

// Note segmentation + behaviour analysis (build spec F2, architecture doc
// §5.3/§5.5, v1/hand-built scope). Groups the decomposed pitch stream into
// note-level segments and, per segment, estimates a PROBABILITY
// DISTRIBUTION over what kind of movement is happening (sustain, vibrato,
// bend, slide, scoop, fall, passing-note, onset) - never a hard label
// (architecture rule 5). This is what lets the correction engine react
// differently to "held note slightly flat" versus "brief fast run passing
// near a chromatic note" versus "deliberate vibrato" instead of guessing
// from duration alone.
//
// Deliberately independent of target estimation: per the pipeline order
// (§5), segmentation runs BEFORE target estimation, so boundaries are
// driven by the raw/centre pitch's own continuity, never by which note the
// target estimator currently favours.
//
// v1 features are hand-built and interpretable (each behaviour score
// traces back to one or two signals), matching the doc's stated v1 scope:
// "deterministic and debuggable... a learned classifier is a later drop-in"
// (rule 6). Realtime-safe: fixed-size state, no allocation after prepare().
class Segmenter {
public:
    struct Config {
        // A segment boundary opens when the raw centre stays this far from
        // the open segment's own running mean for this many consecutive
        // frames - a real note change, not a single noisy frame.
        float boundaryDeviationCents = 60.0f;
        int boundaryHoldFrames = 3;

        // Rolling window (frames) for vibrato rate/extent - vibrato is a
        // property of the RECENT residual, not the whole segment (a long
        // sustain may only develop vibrato partway through).
        static constexpr int kVibratoWindow = 40;   // ~460 ms @ 87 Hz
        float vibratoMinHz = 4.0f;
        float vibratoMaxHz = 7.5f;
        float vibratoMinExtentCents = 12.0f;

        // Passing-note evidence decays with segment age (half-life): a
        // segment that has only just opened looks like it might be a brief
        // pass-through; the longer it holds, the more that evidence fades.
        float passingNoteHalfLifeMs = 90.0f;

        // Bend/slide split by total excursion: a big, sustained-direction
        // move reads as a slide between notes; a small one as an
        // in-note bend/scoop.
        float slideMinExtentCents = 150.0f;
        float driftMonotonicCentsPerSec = 200.0f;   // "clearly gliding" floor
        float scoopFallWindowMs = 150.0f;           // near segment start/full

        // A boundary only opens once movement has SETTLED (drift below
        // this), not merely while pitch is far from the running mean.
        // Without this, a continuous glide/portamento re-triggers the
        // boundary every few frames as the slow mean tries to catch up
        // and falls behind again - it never gets to see itself as one
        // continuous slide, only a chain of onsets.
        float settleDriftCentsPerSec = 150.0f;

        // Behaviour output is smoothed frame-to-frame (not reset at
        // segment boundaries) so a genuine onset still reads as
        // increasingly onset-like rather than snapping instantly - a hard
        // per-frame jump is itself indistinguishable from classifier noise
        // (rule 5: distributions, not verdicts, applies to how the
        // distribution MOVES too, not just its shape at any one instant).
        float behaviourSmoothing = 0.5f;

        int onsetFrames = 2;
    };

    void prepare(const double frameRateHz, const Config& config) noexcept {
        cfg = config;
        frameDur = frameRateHz > 1.0 ? static_cast<float>(1.0 / frameRateHz) : 1.0f / 187.5f;
        reset();
    }

    void reset() noexcept {
        haveSegment = false;
        current = NoteSegment {};
        boundaryRun = 0;
        segFrames = 0;
        segMeanCentre = 0.0f;
        sumT = sumC = sumTC = sumTT = 0.0;
        onsetCounter = 0;
        ringPos = 0;
        ringCount = 0;
        residualRing.fill(0.0f);
        smoothedProb.fill(0.0f);
        smoothedProb[static_cast<size_t>(Behaviour::unvoiced)] = 1.0f;
    }

    // One call per voiced analysis frame (decompConfidence > 0). Returns
    // the currently open segment's live, continuously-updated snapshot.
    const NoteSegment& process(const PitchFrame& frame) noexcept {
        if (!haveSegment)
            openSegment(frame);

        const float deviation = std::abs(frame.centreCents - segMeanCentre);
        const bool settled = std::abs(current.driftCentsPerSec) < cfg.settleDriftCentsPerSec;
        if (deviation > cfg.boundaryDeviationCents && settled) {
            if (++boundaryRun >= cfg.boundaryHoldFrames)
                openSegment(frame);
        } else {
            boundaryRun = 0;
        }

        updateFeatures(frame);
        computeBehaviour(frame);
        return current;
    }

    // Call on a confirmed voicing gap (mirrors decomposition's own anchor
    // drop) so the next voiced frame starts a clean segment rather than
    // gliding boundary logic across silence.
    void closeSegment() noexcept { haveSegment = false; }

    const NoteSegment& currentSegment() const noexcept { return current; }

private:
    void openSegment(const PitchFrame& frame) noexcept {
        current = NoteSegment {};
        current.startSample = frame.timeSamples;
        current.open = true;
        haveSegment = true;
        boundaryRun = 0;
        segFrames = 0;
        sumT = sumC = sumTC = sumTT = 0.0;
        segMeanCentre = frame.centreCents;
        onsetCounter = cfg.onsetFrames;
        ringPos = 0;
        ringCount = 0;
    }

    void updateFeatures(const PitchFrame& frame) noexcept {
        ++segFrames;
        current.endSample = frame.timeSamples;
        current.segmentConfidence = frame.decompConfidence;

        // Running mean centre: a cheap, realtime-safe stand-in for
        // "median" - good enough as a boundary reference and for display.
        const float a = 1.0f / static_cast<float>(segFrames < 32 ? segFrames : 32);
        segMeanCentre += a * (frame.centreCents - segMeanCentre);
        current.medianCentreCents = segMeanCentre;

        // Drift: linear-regression slope of centreCents vs elapsed time,
        // accumulated incrementally (no backward pass).
        const double t = static_cast<double>(segFrames) * frameDur;
        sumT += t;
        sumC += frame.centreCents;
        sumTC += t * frame.centreCents;
        sumTT += t * t;
        const double n = static_cast<double>(segFrames);
        const double denom = n * sumTT - sumT * sumT;
        current.driftCentsPerSec = denom > 1.0e-6
            ? static_cast<float>((n * sumTC - sumT * sumC) / denom)
            : 0.0f;

        // Rolling residual ring -> vibrato rate (zero-crossing count) and
        // extent (peak-to-peak / 2) over the recent window only.
        residualRing[static_cast<size_t>(ringPos)] = frame.residualCents;
        ringPos = (ringPos + 1) % Config::kVibratoWindow;
        if (ringCount < Config::kVibratoWindow)
            ++ringCount;

        float mn = 1.0e9f, mx = -1.0e9f;
        int crossings = 0;
        float prevSample = 0.0f;
        bool havePrev = false;
        for (int i = 0; i < ringCount; ++i) {
            const int idx = (ringPos - ringCount + i + Config::kVibratoWindow)
                % Config::kVibratoWindow;
            const float v = residualRing[static_cast<size_t>(idx)];
            mn = std::min(mn, v);
            mx = std::max(mx, v);
            if (havePrev && (prevSample < 0.0f) != (v < 0.0f)
                && std::abs(v - prevSample) > 1.0f)
                ++crossings;
            prevSample = v;
            havePrev = true;
        }
        const float windowSeconds = static_cast<float>(ringCount) * frameDur;
        current.vibratoExtentCents = ringCount > 2 ? 0.5f * (mx - mn) : 0.0f;
        current.vibratoRateHz = (ringCount > 4 && windowSeconds > 0.0f)
            ? static_cast<float>(crossings) / (2.0f * windowSeconds)
            : 0.0f;

        if (onsetCounter > 0)
            --onsetCounter;
    }

    void computeBehaviour(const PitchFrame& frame) noexcept {
        float scores[static_cast<int>(Behaviour::count)] = {};
        const float ageMs = static_cast<float>(segFrames) * frameDur * 1000.0f;

        scores[static_cast<int>(Behaviour::onset)] =
            (onsetCounter > 0 || frame.f0Hz.reason == EstimateReason::onsetTransient)
                ? 1.0f : 0.05f;

        // Exponential decay: strong evidence right after a boundary,
        // fading as the segment proves it's holding.
        const float passing = std::exp2(-ageMs / cfg.passingNoteHalfLifeMs);
        scores[static_cast<int>(Behaviour::passingNote)] = passing;

        const bool vibratoRateOk = current.vibratoRateHz >= cfg.vibratoMinHz
            && current.vibratoRateHz <= cfg.vibratoMaxHz;
        scores[static_cast<int>(Behaviour::vibrato)] =
            (vibratoRateOk && current.vibratoExtentCents >= cfg.vibratoMinExtentCents)
                ? std::min(1.0f, current.vibratoExtentCents / 40.0f)
                : 0.05f;

        const float absDrift = std::abs(current.driftCentsPerSec);
        const bool monotonic = absDrift > cfg.driftMonotonicCentsPerSec;
        const float excursion = std::abs(frame.centreCents - current.medianCentreCents);
        const bool nearEdge = ageMs < cfg.scoopFallWindowMs;

        scores[static_cast<int>(Behaviour::scoop)] =
            (monotonic && nearEdge && current.driftCentsPerSec > 0.0f)
                ? std::min(1.0f, absDrift / 1000.0f) : 0.02f;
        scores[static_cast<int>(Behaviour::fall)] =
            (monotonic && current.driftCentsPerSec < 0.0f)
                ? std::min(1.0f, absDrift / 1000.0f) : 0.02f;
        scores[static_cast<int>(Behaviour::slide)] =
            (monotonic && excursion > cfg.slideMinExtentCents)
                ? std::min(1.0f, absDrift / 1000.0f) : 0.02f;
        scores[static_cast<int>(Behaviour::bend)] =
            (monotonic && excursion <= cfg.slideMinExtentCents)
                ? std::min(1.0f, absDrift / 600.0f) : 0.02f;

        // Sustain is the default: strong when nothing else has claimed the
        // frame (settled, not vibrating hard, not gliding).
        scores[static_cast<int>(Behaviour::sustain)] =
            std::max(0.05f, 1.0f - passing)
            * (vibratoRateOk ? 0.3f : 1.0f)
            * (monotonic ? 0.2f : 1.0f);

        float total = 0.0f;
        for (const float s : scores)
            total += s;
        for (int i = 0; i < static_cast<int>(Behaviour::count); ++i) {
            const float p = total > 1.0e-6f ? scores[i] / total : 0.0f;
            // Smoothed across frames (deliberately NOT reset at segment
            // boundaries - see Config::behaviourSmoothing) so even a
            // genuine onset ramps in over a couple of frames instead of
            // snapping instantly.
            smoothedProb[static_cast<size_t>(i)] +=
                cfg.behaviourSmoothing * (p - smoothedProb[static_cast<size_t>(i)]);
        }
        float entropy = 0.0f;
        for (int i = 0; i < static_cast<int>(Behaviour::count); ++i) {
            const float p = smoothedProb[static_cast<size_t>(i)];
            current.behaviourProb[i] = p;
            if (p > 1.0e-6f)
                entropy -= p * std::log2(p);
        }
        current.behaviourEntropy = entropy;
    }

    Config cfg {};
    float frameDur = 1.0f / 187.5f;

    bool haveSegment = false;
    NoteSegment current {};
    int boundaryRun = 0;
    int segFrames = 0;
    float segMeanCentre = 0.0f;
    double sumT = 0.0, sumC = 0.0, sumTC = 0.0, sumTT = 0.0;
    int onsetCounter = 0;

    std::array<float, Config::kVibratoWindow> residualRing {};
    int ringPos = 0;
    int ringCount = 0;
    std::array<float, static_cast<size_t>(Behaviour::count)> smoothedProb {};
};

} // namespace vxsuite::tune
