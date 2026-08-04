#pragma once

#include "VxTuneTimeline.h"

#include <cmath>

namespace vxsuite::tune {

// Bayesian target-note estimator (build spec F3, reduced scope).
//
// v0's CorrectionEngine picked the literal nearest chromatic note fresh
// every frame, then (later) held it with a fixed-cents lock margin. Both are
// single-frame decisions dressed up to look stable. Real vocal pitch during
// vibrato/portamento routinely straddles a semitone boundary, and a
// single-frame decision — locked or not — has no way to tell "the singer
// is between two notes right now" from "the singer moved to a new note":
// that is exactly what read as the correction "not knowing where it wants
// to go" (round 6).
//
// This estimator instead treats the intended note as evidence accumulated
// over time: every candidate note within +/-searchSemitones of the current
// pitch gets a confidence-weighted log-likelihood contribution each frame,
// and all candidates' running scores decay with a Natural-dependent time
// constant. A target that has been reinforced for many frames beats a
// candidate that only appeared this instant, which gives target persistence
// for free — no arbitrary fixed-cents "lock margin" — while a genuine new
// note still wins once its own evidence accumulates past whatever is
// currently held. Natural<->Tight governs both the per-frame tolerance
// (sigma: how far off pitch still counts as evidence for a note) and the
// memory (how long evidence persists): Natural is forgiving and
// long-memory (reluctant to abandon a target, so expressive movement reads
// as movement around a note rather than a chain of tiny note changes);
// Tight is narrow and short-memory (decides fast, follows genuine changes
// eagerly).
//
// Realtime-safe: fixed-size candidate pool, no allocation after prepare().
class TargetEstimator {
public:
    struct Config {
        float sigmaMaxCents = 50.0f;     // evidence tolerance at full Natural
        float sigmaMinCents = 18.0f;     // evidence tolerance at full Tight
        float memoryMaxSeconds = 1.2f;   // evidence half-life at full Natural
        float memoryMinSeconds = 0.25f;  // evidence half-life at full Tight
        int searchSemitones = 6;         // +/- range scanned each frame
    };

    static constexpr std::uint16_t kChromaticMask = 0x0FFF;
    static constexpr int kMaxCandidates = 24;

    void prepare(const double frameRateHz, const Config& config) noexcept {
        cfg = config;
        frameDur = frameRateHz > 1.0 ? static_cast<float>(1.0 / frameRateHz) : 1.0f / 187.5f;
        reset();
    }

    void reset() noexcept {
        count = 0;
    }

    struct Result {
        bool valid = false;
        int midiNote = -1;
        float errorCents = 0.0f;      // centreCents - target, signed
        int runnerUpMidiNote = -1;
        float marginLog = 0.0f;       // best score - runner-up score
    };

    // One call per analysis frame, always (even low-confidence/unvoiced —
    // that is how the estimator knows to forget between phrases).
    // decompConfidence == 0 means "nothing to go on": the pool resets so the
    // next phrase starts fresh instead of gliding from a stale target.
    Result update(const float centreCents, const float decompConfidence,
                  const std::uint16_t scaleMask, const float natural) noexcept {
        if (decompConfidence <= 0.0f) {
            reset();
            return {};
        }

        const std::uint16_t mask =
            (scaleMask & kChromaticMask) != 0 ? scaleMask : kChromaticMask;
        const float sigma = cfg.sigmaMaxCents
            + (cfg.sigmaMinCents - cfg.sigmaMaxCents) * natural;
        const float memory = cfg.memoryMaxSeconds
            + (cfg.memoryMinSeconds - cfg.memoryMaxSeconds) * natural;
        const float decay = std::exp(-frameDur / (memory > 0.01f ? memory : 0.01f));

        for (int i = 0; i < count; ++i)
            candidates[i].score *= decay;

        const float nearestChromatic = std::round(centreCents / 100.0f);
        for (int offset = -cfg.searchSemitones; offset <= cfg.searchSemitones; ++offset) {
            const int midi = 69 + static_cast<int>(nearestChromatic) + offset;
            const int pitchClass = ((midi % 12) + 12) % 12;
            if ((mask & (1u << pitchClass)) == 0)
                continue;
            const float error = centreCents - 100.0f * static_cast<float>(midi - 69);
            const float z = error / sigma;
            addEvidence(midi, -0.5f * z * z * decompConfidence);
        }

        if (count == 0)
            return {};

        int bestIdx = 0;
        for (int i = 1; i < count; ++i)
            if (candidates[i].score > candidates[bestIdx].score)
                bestIdx = i;
        int runnerIdx = -1;
        for (int i = 0; i < count; ++i) {
            if (i == bestIdx)
                continue;
            if (runnerIdx < 0 || candidates[i].score > candidates[runnerIdx].score)
                runnerIdx = i;
        }

        Result r;
        r.valid = true;
        r.midiNote = candidates[bestIdx].midi;
        r.errorCents = centreCents - 100.0f * static_cast<float>(candidates[bestIdx].midi - 69);
        r.runnerUpMidiNote = runnerIdx >= 0 ? candidates[runnerIdx].midi : -1;
        r.marginLog = runnerIdx >= 0
            ? candidates[bestIdx].score - candidates[runnerIdx].score
            : candidates[bestIdx].score;

        return r;
    }

private:
    struct Candidate {
        int midi = -1000000;
        float score = -1.0e9f;
    };

    void addEvidence(const int midi, const float logLikelihood) noexcept {
        for (int i = 0; i < count; ++i) {
            if (candidates[i].midi == midi) {
                candidates[i].score += logLikelihood;
                return;
            }
        }
        if (count < kMaxCandidates) {
            candidates[count++] = { midi, logLikelihood };
            return;
        }
        int weakest = 0;
        for (int i = 1; i < count; ++i)
            if (candidates[i].score < candidates[weakest].score)
                weakest = i;
        candidates[weakest] = { midi, logLikelihood };
    }

    Config cfg {};
    float frameDur = 1.0f / 187.5f;
    Candidate candidates[kMaxCandidates] {};
    int count = 0;
};

} // namespace vxsuite::tune
