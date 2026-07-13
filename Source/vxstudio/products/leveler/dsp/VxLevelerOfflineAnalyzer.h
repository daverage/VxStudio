#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <cstdint>
#include <vector>

namespace vxsuite::leveler {

struct OfflineAnalysisResult final {
    double sampleRate = 48000.0;
    int blockSize = 256;
    float globalMedianDb = -30.0f;
    float globalUpperDb = -24.0f;
    float globalDynamicRangeDb = 6.0f;
    // Median over blocks with actual signal (silence-gated). For sparse
    // material such as a vocal take, the plain median lands in the gaps;
    // this is the level the material actually plays at.
    float activeMedianDb = -30.0f;
    // Timeline position (in samples) of the first captured analysis block;
    // -1 when unknown (no playhead, or state saved by an older version).
    std::int64_t startSample = -1;
    std::vector<float> targetCurveDb;

    [[nodiscard]] bool isValid() const noexcept { return !targetCurveDb.empty(); }
};

class OfflineAnalyzer final {
public:
    static OfflineAnalysisResult analyse(const juce::AudioBuffer<float>& buffer,
                                         double sampleRate,
                                         int blockSize);
    static OfflineAnalysisResult analyse(const float* blockDb,
                                         size_t blockCount,
                                         double sampleRate,
                                         int blockSize);
};

} // namespace vxsuite::leveler
