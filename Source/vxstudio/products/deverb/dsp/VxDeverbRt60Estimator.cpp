#include "VxDeverbRt60Estimator.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace vxsuite::deverb {

LollmannRt60Estimator::LollmannRt60Estimator() {
    subFrameCount.fill(0);
    subFrameIdx.fill(0);
    for (auto& v : subFrameEnergy)
        v.assign(kSubFrames, 0.0f);
    histogram.assign(kHistBins, 0);
    historyBuffer.assign(kHistDepth, 0.0f);
}

// ── prepare ───────────────────────────────────────────────────────────────────

void LollmannRt60Estimator::prepare(const double hostSampleRate) {
    decimRatio = std::max(1, static_cast<int>(std::round(hostSampleRate / kInternalRate)));
    computeBandCoeffs();
    for (auto& v : subFrameEnergy)
        v.assign(kSubFrames, 0.0f);
    histogram.assign(kHistBins, 0);
    historyBuffer.assign(kHistDepth, 0.0f);
    reset();
}

// ── computeBandCoeffs ─────────────────────────────────────────────────────────
// 1/3-octave bandpass filters centred at 125, 160, 200, 250, 315, 400, 500,
// 630, 800 Hz designed at kInternalRate = 16000 Hz.
// Q = 2^(1/6) / (2^(1/3) - 1) ≈ 4.318 for 1/3-octave constant-Q bandpass.

void LollmannRt60Estimator::computeBandCoeffs() {
    static constexpr float kCentreFreqs[kNumBands] = {
        125.0f, 160.0f, 200.0f, 250.0f, 315.0f, 400.0f, 500.0f, 630.0f, 800.0f
    };
    // Q for 1/3-octave: fc / bw = 2^(1/6) / (2^(1/3) - 1) ≈ 4.318
    static constexpr float kQ = 4.318f;

    for (int b = 0; b < kNumBands; ++b) {
        const float fc = kCentreFreqs[b];
        const float w0 = 2.0f * 3.14159265f * fc / static_cast<float>(kInternalRate);
        const float alpha = std::sin(w0) / (2.0f * kQ);
        const float a0    = 1.0f + alpha;

        bandCoeffs[b].b0 =  alpha  / a0;
        bandCoeffs[b].b2 = -alpha  / a0;
        bandCoeffs[b].a1 = (-2.0f * std::cos(w0)) / a0;
        bandCoeffs[b].a2 = (1.0f - alpha) / a0;
    }
}

// ── reset ─────────────────────────────────────────────────────────────────────

void LollmannRt60Estimator::reset() {
    decimCounter = 0;
    decimAccum   = 0.0f;

    bandState.fill({});

    for (int b = 0; b < kNumBands; ++b) {
        subFrameEnergy[b].assign(kSubFrames, 0.0f);
        subFrameCount[b] = 0;
        subFrameIdx[b]   = 0;
    }

    std::fill(histogram.begin(),     histogram.end(),     0);
    std::fill(historyBuffer.begin(), historyBuffer.end(), 0.0f);
    historyWritePos = 0;
    historyCount    = 0;

    smoothedRt60 = kDefaultRt60;
    hasEstimate  = false;
    outputRt60   = kDefaultRt60;
}

// ── pushSamples ───────────────────────────────────────────────────────────────

void LollmannRt60Estimator::pushSamples(const float* samples, const int count) {
    if (useFixed) return; // not worth estimating if overridden

    for (int i = 0; i < count; ++i) {
        decimAccum += samples[i];
        ++decimCounter;

        if (decimCounter == decimRatio) {
            const float decimSample = decimAccum / static_cast<float>(decimRatio);
            decimAccum   = 0.0f;
            decimCounter = 0;

            for (int b = 0; b < kNumBands; ++b)
                processBandSample(b, decimSample);
        }
    }
}

// ── processBandSample ─────────────────────────────────────────────────────────
// Direct Form II biquad. b1 = 0 for all bands.

void LollmannRt60Estimator::processBandSample(const int band, const float sample) {
    const auto& c = bandCoeffs[band];
    auto&       s = bandState[band];

    const float w  = sample - c.a1 * s.x1 - c.a2 * s.x2;
    const float y  = c.b0 * w + c.b2 * s.x2;          // b1 = 0
    s.x2 = s.x1;
    s.x1 = w;

    subFrameEnergy[band][static_cast<size_t>(subFrameIdx[band])] += y * y;
    ++subFrameCount[band];

    if (subFrameCount[band] == kSubFrameSamples)
        onSubFrameComplete(band);
}

// ── onSubFrameComplete ────────────────────────────────────────────────────────

void LollmannRt60Estimator::onSubFrameComplete(const int band) {
    subFrameCount[band] = 0;
    ++subFrameIdx[band];

    if (subFrameIdx[band] == kSubFrames)
        onFrameComplete(band);

    // Zero out the energy accumulator for the new sub-frame slot (wrapping)
    const int nextSlot = subFrameIdx[band] % kSubFrames;
    subFrameEnergy[band][static_cast<size_t>(nextSlot)] = 0.0f;
}

// ── onFrameComplete ───────────────────────────────────────────────────────────

void LollmannRt60Estimator::onFrameComplete(const int band) {
    // Build log-energy sequence for this frame
    float logEnergy[kSubFrames];
    for (int i = 0; i < kSubFrames; ++i)
        logEnergy[i] = std::log(subFrameEnergy[band][static_cast<size_t>(i)] + 1.0e-20f);

    // Find longest monotonically decreasing run (length ≥ 3)
    int bestStart = -1;
    int bestLen   = 0;
    int runStart  = 0;
    int runLen    = 1;

    for (int i = 1; i < kSubFrames; ++i) {
        if (logEnergy[i] < logEnergy[i - 1]) {
            ++runLen;
        } else {
            if (runLen >= 3 && runLen > bestLen) {
                bestLen   = runLen;
                bestStart = runStart;
            }
            runStart = i;
            runLen   = 1;
        }
    }
    if (runLen >= 3 && runLen > bestLen) {
        bestLen   = runLen;
        bestStart = runStart;
    }

    if (bestStart >= 0 && bestLen >= 3) {
        const float slope = mlSlopeEstimate(logEnergy + bestStart, bestLen);
        const float rt60  = slopeToRt60(slope);
        if (rt60 >= kRt60Min && rt60 <= kRt60Max)
            addEstimate(rt60);
    }

    subFrameIdx[band] = 0;
}

// ── mlSlopeEstimate ───────────────────────────────────────────────────────────
// Closed-form linear regression slope (ML-optimal for Gaussian noise).
// Uses only N-element sums; O(N) arithmetic.

float LollmannRt60Estimator::mlSlopeEstimate(const float* logEnergy, const int N) {
    float sum_y  = 0.0f;
    float sum_xy = 0.0f;
    for (int i = 0; i < N; ++i) {
        sum_y  += logEnergy[i];
        sum_xy += static_cast<float>(i) * logEnergy[i];
    }
    // Closed-form sums: sum_x = N(N-1)/2, sum_x2 = N(N-1)(2N-1)/6
    const float fn     = static_cast<float>(N);
    const float sum_x  = fn * (fn - 1.0f) / 2.0f;
    const float sum_x2 = fn * (fn - 1.0f) * (2.0f * fn - 1.0f) / 6.0f;
    const float denom  = fn * sum_x2 - sum_x * sum_x;
    if (denom == 0.0f) return 0.0f;
    return (fn * sum_xy - sum_x * sum_y) / denom;
}

// ── slopeToRt60 ───────────────────────────────────────────────────────────────

float LollmannRt60Estimator::slopeToRt60(const float slopePerSubFrame) {
    // slope is in Nepers/subFrame computed from log(energy).
    // Since energy = amplitude², a 60 dB (amplitude) decay corresponds to
    // a log(energy) drop of 2·ln(1000) = ln(10^6) ≈ 13.816 Nepers.
    // RT60 = -13.816 / slopePerSec
    const float slopePerSec = slopePerSubFrame
        * (static_cast<float>(kInternalRate) / static_cast<float>(kSubFrameSamples));
    if (slopePerSec >= 0.0f) return 0.0f; // not a decay
    return -2.0f * std::log(1000.0f) / slopePerSec; // = -13.816 / slopePerSec
}

// ── addEstimate ───────────────────────────────────────────────────────────────

void LollmannRt60Estimator::addEstimate(const float rt60) {
    int bin = static_cast<int>((rt60 - kRt60Min) / (kRt60Max - kRt60Min) * static_cast<float>(kHistBins));
    bin = std::clamp(bin, 0, kHistBins - 1);

    if (historyCount == kHistDepth) {
        // Buffer full: remove oldest entry from histogram
        const float oldest   = historyBuffer[historyWritePos];
        int         oldBin   = static_cast<int>((oldest - kRt60Min) / (kRt60Max - kRt60Min) * static_cast<float>(kHistBins));
        oldBin = std::clamp(oldBin, 0, kHistBins - 1);
        if (histogram[oldBin] > 0)
            --histogram[oldBin];
    } else {
        ++historyCount;
    }

    historyBuffer[historyWritePos] = rt60;
    ++histogram[bin];
    historyWritePos = (historyWritePos + 1) % kHistDepth;

    const float peak = histogramPeak();
    smoothedRt60 = kSmoothGamma * smoothedRt60 + (1.0f - kSmoothGamma) * peak;
    outputRt60   = smoothedRt60;
    hasEstimate  = true;
}

// ── histogramPeak ─────────────────────────────────────────────────────────────

float LollmannRt60Estimator::histogramPeak() const {
    int maxCount = 0;
    int maxBin   = 0;
    for (int b = 0; b < kHistBins; ++b) {
        if (histogram[b] > maxCount) {
            maxCount = histogram[b];
            maxBin   = b;
        }
    }
    return kRt60Min + (static_cast<float>(maxBin) + 0.5f)
           * (kRt60Max - kRt60Min) / static_cast<float>(kHistBins);
}

// ── getEstimatedRt60 ──────────────────────────────────────────────────────────

float LollmannRt60Estimator::getEstimatedRt60() const {
    if (useFixed) return fixedRt60;
    return outputRt60;
}

// ── debug overrides ───────────────────────────────────────────────────────────

void LollmannRt60Estimator::setFixedRt60(const float seconds) {
    fixedRt60 = seconds;
    useFixed  = true;
}

void LollmannRt60Estimator::clearFixedRt60() {
    useFixed = false;
}

} // namespace vxsuite::deverb
