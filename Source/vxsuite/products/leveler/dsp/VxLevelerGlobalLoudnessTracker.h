#pragma once

#include <array>

namespace vxsuite::leveler {

class GlobalLoudnessTracker final {
public:
    void prepare(double sampleRate, int maxBlockSize);
    void reset();
    void update(float shortLoudnessDb, float momentaryLoudnessDb, bool validWindow, int blockSamples);

    [[nodiscard]] float getGlobalShortDb() const noexcept { return globalShortEstimateDb; }
    [[nodiscard]] float getGlobalBaselineDb() const noexcept { return globalMedianEstimateDb; }
    [[nodiscard]] float getGlobalUpperDb() const noexcept { return globalUpperEstimateDb; }
    [[nodiscard]] float getConfidence() const noexcept { return globalConfidence; }
    [[nodiscard]] float getDynamicRangeDb() const noexcept { return globalDynamicRangeEstimateDb; }

private:
    static constexpr int kBinCount = 193;
    static constexpr int kStartupBufferSize = 512;
    static constexpr float kMinDb = -90.0f;
    static constexpr float kMaxDb = 6.0f;
    static constexpr float kBinStepDb = (kMaxDb - kMinDb) / static_cast<float>(kBinCount - 1);

    [[nodiscard]] static int binIndexForDb(float valueDb) noexcept;
    [[nodiscard]] float quantileDb(float q) const noexcept;
    void recomputeStartupEstimates() noexcept;

    std::array<float, kBinCount> histogram {};
    std::array<float, kStartupBufferSize> startupShortDbBuffer {};
    double sampleRateHz = 48000.0;
    int preparedBlockSize = 256;
    float globalShortEstimateDb = -36.0f;
    float globalMedianEstimateDb = -36.0f;
    float globalUpperEstimateDb = -30.0f;
    float globalConfidence = 0.0f;
    float globalDynamicRangeEstimateDb = 6.0f;
    float observedSeconds = 0.0f;
    float totalWeight = 0.0f;
    int startupWriteIndex = 0;
    int startupCount = 0;
    int startupUpdateCounter = 0;
    float startupMedianEstimateDb = -36.0f;
    float startupUpperEstimateDb = -30.0f;
    float startupConfidence = 0.0f;
};

} // namespace vxsuite::leveler
