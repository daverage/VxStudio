#include "VxTunePitchDetector.h"

#include <algorithm>
#include <cmath>

namespace vxsuite::tune {

namespace {

constexpr float kA440 = 440.0f;

float hzToCents(const float hz) noexcept {
    return 1200.0f * std::log2(hz / kA440);
}

} // namespace

void PitchDetector::prepare(const double sampleRate, const Config& config) {
    cfg = config;
    sr = sampleRate > 1000.0 ? sampleRate : 48000.0;

    tauMin = std::max(2, static_cast<int>(sr / cfg.maxHz));
    tauMax = std::max(tauMin + 8, static_cast<int>(std::ceil(sr / cfg.minHz)));
    windowSamples = tauMax;                       // W = max lag (standard YIN)

    ringSize = windowSamples + tauMax + cfg.hopSamples;
    ring.assign(static_cast<size_t>(ringSize), 0.0f);
    frame.assign(static_cast<size_t>(windowSamples + tauMax), 0.0f);
    cmndf.assign(static_cast<size_t>(tauMax + 1), 1.0f);
    reset();
}

void PitchDetector::reset() {
    std::fill(ring.begin(), ring.end(), 0.0f);
    ringWrite = 0;
    samplesSinceHop = 0;
    totalSamples = 0;
    previousLevelDb = -120.0f;
    previousCents = 0.0f;
    hadPreviousPitch = false;
}

int PitchDetector::process(const float* input, const int numSamples,
                           PitchObservation* out, const int maxOut) noexcept {
    int produced = 0;
    for (int i = 0; i < numSamples; ++i) {
        ring[static_cast<size_t>(ringWrite)] = input[i];
        ringWrite = (ringWrite + 1) % ringSize;
        ++totalSamples;

        if (++samplesSinceHop >= cfg.hopSamples) {
            samplesSinceHop = 0;
            if (totalSamples >= static_cast<std::int64_t>(windowSamples + tauMax)
                && produced < maxOut) {
                PitchObservation obs;
                obs.timeSamples = totalSamples;
                analyseWindow(obs);
                out[produced++] = obs;
            }
        }
    }
    return produced;
}

void PitchDetector::analyseWindow(PitchObservation& obs) noexcept {
    const int needed = windowSamples + tauMax;
    // Linearise the newest `needed` samples so x[j] indexing is contiguous.
    int start = ringWrite - needed;
    if (start < 0)
        start += ringSize;
    for (int j = 0; j < needed; ++j)
        frame[static_cast<size_t>(j)] =
            ring[static_cast<size_t>((start + j) % ringSize)];

    // Window level over the analysis span.
    double energy = 0.0;
    for (int j = 0; j < needed; ++j) {
        const double v = frame[static_cast<size_t>(j)];
        energy += v * v;
    }
    const float rms = static_cast<float>(std::sqrt(energy / needed));
    const float levelDb = rms > 1.0e-9f ? 20.0f * std::log10(rms) : -120.0f;
    obs.levelDb = levelDb;

    const float levelJumpDb = levelDb - previousLevelDb;
    previousLevelDb = levelDb;

    if (levelDb < cfg.silenceFloorDb) {
        obs.f0Hz = { 0.0f, 0.0f, EstimateReason::unvoiced, 0.0f };
        obs.voicedProb = 0.0f;
        hadPreviousPitch = false;
        return;
    }

    // YIN difference function + cumulative-mean normalisation.
    // d(tau) = sum_j (x[j] - x[j+tau])^2 over the window.
    const float* x = frame.data();
    double runningSum = 0.0;
    cmndf[0] = 1.0f;
    for (int tau = 1; tau <= tauMax; ++tau) {
        double d = 0.0;
        const float* a = x;
        const float* b = x + tau;
        for (int j = 0; j < windowSamples; ++j) {
            const double diff = static_cast<double>(a[j]) - static_cast<double>(b[j]);
            d += diff * diff;
        }
        runningSum += d;
        cmndf[static_cast<size_t>(tau)] =
            runningSum > 0.0 ? static_cast<float>(d * tau / runningSum) : 1.0f;
    }

    // Absolute-threshold search: first local minimum below threshold wins;
    // otherwise fall back to the global minimum.
    int bestTau = -1;
    for (int tau = tauMin; tau < tauMax; ++tau) {
        if (cmndf[static_cast<size_t>(tau)] < cfg.voicedThreshold
            && cmndf[static_cast<size_t>(tau)] <= cmndf[static_cast<size_t>(tau + 1)]) {
            bestTau = tau;
            break;
        }
    }
    int globalTau = tauMin;
    for (int tau = tauMin; tau <= tauMax; ++tau)
        if (cmndf[static_cast<size_t>(tau)] < cmndf[static_cast<size_t>(globalTau)])
            globalTau = tau;
    const bool thresholdHit = bestTau > 0;
    if (!thresholdHit)
        bestTau = globalTau;

    const float dip = cmndf[static_cast<size_t>(bestTau)];

    // Octave-ambiguity check: a genuine competing dip at HALF the period
    // means the signal carries strong octave-up periodicity (or we locked
    // onto a subharmonic). Dips at 2x the period are expected for any
    // periodic signal and prove nothing. Flag only; never silently switch.
    bool octaveDoubt = false;
    const int half = bestTau / 2;
    if (half >= tauMin && cmndf[static_cast<size_t>(half)] < 0.3f)
        octaveDoubt = true;

    // Parabolic interpolation around the chosen lag for sub-sample period.
    float refinedTau = static_cast<float>(bestTau);
    if (bestTau > tauMin && bestTau < tauMax) {
        const float y0 = cmndf[static_cast<size_t>(bestTau - 1)];
        const float y1 = cmndf[static_cast<size_t>(bestTau)];
        const float y2 = cmndf[static_cast<size_t>(bestTau + 1)];
        const float denom = y0 - 2.0f * y1 + y2;
        if (std::abs(denom) > 1.0e-12f)
            refinedTau += 0.5f * (y0 - y2) / denom;
    }

    const float f0 = static_cast<float>(sr) / refinedTau;
    const float voicedConfidence = std::clamp(1.0f - dip, 0.0f, 1.0f);

    if (!thresholdHit && dip > 0.5f) {
        // No usable dip at speaking level: aperiodic input.
        obs.f0Hz = { 0.0f, 0.0f, EstimateReason::periodicityCollapse, 0.0f };
        obs.voicedProb = 0.0f;
        hadPreviousPitch = false;
        return;
    }

    // Stability from frame-to-frame movement of the pitch estimate.
    const float cents = hzToCents(f0);
    float stability = 1.0f;
    if (hadPreviousPitch) {
        const float move = std::abs(cents - previousCents);
        stability = std::clamp(1.0f - move / 100.0f, 0.0f, 1.0f);
    } else {
        stability = 0.0f;
    }
    previousCents = cents;
    hadPreviousPitch = true;

    EstimateReason reason = EstimateReason::nominal;
    float confidence = voicedConfidence;
    if (levelJumpDb > 8.0f) {
        reason = EstimateReason::onsetTransient;
        confidence *= 0.6f;
    } else if (octaveDoubt) {
        reason = EstimateReason::octaveAmbiguity;
        // Harsh: an octave error is not "slightly less sure of the same
        // note" like the other degradations here - it is a wrong note a
        // full 12 semitones away, and it reads as high-confidence/high-
        // stability once it locks (the frames after the initial ambiguous
        // one look internally consistent). This needs to fall hard enough
        // below decomposition's anchor-trust gate (confidence*(0.5+0.5*
        // stability) >= 0.6) that it can't found a re-anchor by itself.
        confidence *= 0.35f;
    } else if (!thresholdHit) {
        reason = EstimateReason::lowSnr;
        confidence *= 0.5f;
    }

    obs.f0Hz = { f0, confidence, reason, stability };
    obs.voicedProb = voicedConfidence;
}

} // namespace vxsuite::tune
