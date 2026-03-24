#include "VxLevelerGlobalLoudnessTracker.h"

#include <algorithm>
#include <cmath>

namespace vxsuite::leveler {

namespace {

inline float clamp01(const float x) noexcept {
    return std::clamp(x, 0.0f, 1.0f);
}

} // namespace

void GlobalLoudnessTracker::prepare(const double sampleRate, const int maxBlockSize) {
    sampleRateHz = sampleRate > 1000.0 ? sampleRate : 48000.0;
    preparedBlockSize = std::max(1, maxBlockSize);
    reset();
}

void GlobalLoudnessTracker::reset() {
    histogram.fill(0.0f);
    startupShortDbBuffer.fill(-90.0f);
    globalShortEstimateDb = -36.0f;
    globalMedianEstimateDb = -36.0f;
    globalUpperEstimateDb = -30.0f;
    globalConfidence = 0.0f;
    globalDynamicRangeEstimateDb = 6.0f;
    observedSeconds = 0.0f;
    totalWeight = 0.0f;
    startupWriteIndex = 0;
    startupCount = 0;
    startupUpdateCounter = 0;
    startupMedianEstimateDb = -36.0f;
    startupUpperEstimateDb = -30.0f;
    startupConfidence = 0.0f;
}

void GlobalLoudnessTracker::update(const float shortLoudnessDb,
                                   const float /*momentaryLoudnessDb*/,
                                   const bool validWindow,
                                   const int blockSamples) {
    const float blockSeconds = static_cast<float>(std::max(1, blockSamples))
        / static_cast<float>(sampleRateHz > 1000.0 ? sampleRateHz : 48000.0);
    const float horizonSeconds = 30.0f;
    const float decay = std::exp(-blockSeconds / horizonSeconds);

    for (auto& bin : histogram)
        bin *= decay;
    totalWeight *= decay;

    if (validWindow) {
        histogram[static_cast<size_t>(binIndexForDb(shortLoudnessDb))] += 1.0f;
        totalWeight += 1.0f;
        observedSeconds += blockSeconds;
        const float emaCoeff = std::exp(-blockSeconds / 12.0f);
        globalShortEstimateDb = emaCoeff * globalShortEstimateDb + (1.0f - emaCoeff) * shortLoudnessDb;

        startupShortDbBuffer[static_cast<size_t>(startupWriteIndex)] = shortLoudnessDb;
        startupWriteIndex = (startupWriteIndex + 1) % kStartupBufferSize;
        startupCount = std::min(startupCount + 1, kStartupBufferSize);
        startupUpdateCounter = (startupUpdateCounter + 1) % 16;
        if (startupCount >= 24 && startupUpdateCounter == 0)
            recomputeStartupEstimates();
    }

    if (totalWeight > 1.0e-4f) {
        globalMedianEstimateDb = quantileDb(0.50f);
        globalUpperEstimateDb = quantileDb(0.82f);
        globalDynamicRangeEstimateDb = std::max(1.5f, globalUpperEstimateDb - globalMedianEstimateDb);
    }

    const float timeConfidence = clamp01(1.0f - std::exp(-observedSeconds / 12.0f));
    startupConfidence = clamp01((static_cast<float>(startupCount) / 192.0f)
                                * (0.60f + 0.40f * clamp01((startupUpperEstimateDb - startupMedianEstimateDb) / 6.0f)));
    if (startupCount >= 24 && observedSeconds < 8.0f) {
        const float bootstrapBlend = clamp01(observedSeconds / 8.0f);
        globalMedianEstimateDb = startupMedianEstimateDb + bootstrapBlend * (globalMedianEstimateDb - startupMedianEstimateDb);
        globalUpperEstimateDb = startupUpperEstimateDb + bootstrapBlend * (globalUpperEstimateDb - startupUpperEstimateDb);
        globalDynamicRangeEstimateDb = std::max(1.5f, globalUpperEstimateDb - globalMedianEstimateDb);
    }
    globalConfidence = std::max(timeConfidence, 0.45f * startupConfidence);
}

int GlobalLoudnessTracker::binIndexForDb(const float valueDb) noexcept {
    const float normalized = (std::clamp(valueDb, kMinDb, kMaxDb) - kMinDb) / kBinStepDb;
    return std::clamp(static_cast<int>(std::lround(normalized)), 0, kBinCount - 1);
}

float GlobalLoudnessTracker::quantileDb(const float q) const noexcept {
    if (totalWeight <= 1.0e-5f)
        return globalMedianEstimateDb;

    const float target = std::clamp(q, 0.0f, 1.0f) * totalWeight;
    float accum = 0.0f;
    for (int i = 0; i < kBinCount; ++i) {
        accum += histogram[static_cast<size_t>(i)];
        if (accum >= target)
            return kMinDb + static_cast<float>(i) * kBinStepDb;
    }
    return kMaxDb;
}

void GlobalLoudnessTracker::recomputeStartupEstimates() noexcept {
    std::array<float, kStartupBufferSize> sorted {};
    for (int i = 0; i < startupCount; ++i)
        sorted[static_cast<size_t>(i)] = startupShortDbBuffer[static_cast<size_t>(i)];
    std::sort(sorted.begin(), sorted.begin() + startupCount);

    const auto pick = [&](const float q) {
        const int idx = std::clamp(static_cast<int>(std::lround(q * static_cast<float>(startupCount - 1))),
                                   0,
                                   std::max(0, startupCount - 1));
        return sorted[static_cast<size_t>(idx)];
    };

    startupMedianEstimateDb = pick(0.50f);
    startupUpperEstimateDb = pick(0.82f);
}

} // namespace vxsuite::leveler
