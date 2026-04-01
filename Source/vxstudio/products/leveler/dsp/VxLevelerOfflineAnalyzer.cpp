#include "VxLevelerOfflineAnalyzer.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace vxsuite::leveler {

namespace {

inline float gainToDbFloor(const float gain) noexcept {
    return juce::Decibels::gainToDecibels(std::max(gain, 1.0e-5f), -100.0f);
}

inline float clampf(const float x, const float lo, const float hi) noexcept {
    return std::clamp(x, lo, hi);
}

} // namespace

OfflineAnalysisResult analyseBlockDbVector(const float* blockDbData,
                                           const size_t blockCount,
                                           const double sampleRate,
                                           const int requestedBlockSize) {
    OfflineAnalysisResult result {};
    result.sampleRate = sampleRate > 1000.0 ? sampleRate : 48000.0;
    result.blockSize = std::max(1, requestedBlockSize);
    std::vector<float> blockDb(blockDbData, blockDbData + blockCount);
    std::vector<float> sorted = blockDb;
    std::sort(sorted.begin(), sorted.end());
    const auto quantileAt = [&](const float q) {
        if (sorted.empty())
            return -36.0f;
        const float index = std::clamp(q, 0.0f, 1.0f) * static_cast<float>(sorted.size() - 1);
        return sorted[static_cast<size_t>(std::lround(index))];
    };

    result.globalMedianDb = quantileAt(0.50f);
    result.globalUpperDb = quantileAt(0.82f);
    result.globalDynamicRangeDb = std::max(1.5f, result.globalUpperDb - result.globalMedianDb);

    const float blockSeconds = static_cast<float>(result.blockSize) / static_cast<float>(result.sampleRate);
    const float shortCoeff = std::exp(-blockSeconds / 3.0f);
    const float baselineRise = std::exp(-blockSeconds / 4.0f);
    const float baselineFall = std::exp(-blockSeconds / 8.0f);

    float shortDb = blockDb.empty() ? -36.0f : blockDb.front();
    float baselineDb = shortDb;
    result.targetCurveDb.reserve(blockDb.size());
    for (const float currentDb : blockDb) {
        shortDb = shortCoeff * shortDb + (1.0f - shortCoeff) * currentDb;
        const float baselineCoeff = shortDb > baselineDb ? baselineRise : baselineFall;
        baselineDb = baselineCoeff * baselineDb + (1.0f - baselineCoeff) * shortDb;

        const float localTarget = baselineDb + 0.18f * (shortDb - baselineDb);
        float globalTarget = result.globalMedianDb + 0.12f * (shortDb - result.globalMedianDb);
        if (shortDb > result.globalUpperDb)
            globalTarget = std::min(globalTarget,
                                    result.globalUpperDb - 0.15f * result.globalDynamicRangeDb);

        const float rangeNorm = clampf((result.globalDynamicRangeDb - 2.0f) / 10.0f, 0.0f, 1.0f);
        const float globalBlend = juce::jmap(rangeNorm, 0.78f, 0.58f);
        result.targetCurveDb.push_back(juce::jmap(globalBlend, localTarget, globalTarget));
    }

    return result;
}

OfflineAnalysisResult OfflineAnalyzer::analyse(const juce::AudioBuffer<float>& buffer,
                                               const double sampleRate,
                                               const int requestedBlockSize) {
    const int blockSize = std::max(1, requestedBlockSize);
    const int channels = std::max(1, buffer.getNumChannels());
    const int samples = buffer.getNumSamples();
    const int blockCount = std::max(1, (samples + blockSize - 1) / blockSize);
    std::vector<float> blockDb;
    blockDb.reserve(static_cast<size_t>(blockCount));

    for (int start = 0; start < samples; start += blockSize) {
        const int num = std::min(blockSize, samples - start);
        double energy = 0.0;
        int count = 0;
        for (int ch = 0; ch < channels; ++ch) {
            const float* data = buffer.getReadPointer(ch);
            for (int i = start; i < start + num; ++i) {
                energy += static_cast<double>(data[i]) * static_cast<double>(data[i]);
                ++count;
            }
        }
        const float blockRms = count > 0 ? static_cast<float>(std::sqrt(energy / static_cast<double>(count))) : 0.0f;
        blockDb.push_back(gainToDbFloor(blockRms));
    }

    return analyseBlockDbVector(blockDb.data(), blockDb.size(), sampleRate, blockSize);
}

OfflineAnalysisResult OfflineAnalyzer::analyse(const float* blockDb,
                                               const size_t blockCount,
                                               const double sampleRate,
                                               const int blockSize) {
    if (blockDb == nullptr || blockCount == 0)
        return {};
    return analyseBlockDbVector(blockDb, blockCount, sampleRate, blockSize);
}

} // namespace vxsuite::leveler
