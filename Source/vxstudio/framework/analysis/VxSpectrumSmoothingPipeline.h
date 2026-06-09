#pragma once

// Stateful spectrum smoothing: history frames, time-averaging, octave smoothing,
// display slope, delta calculation, sparse/broadband classification.
// No UI code. No registry reads. No selection logic.

#include "../../products/analyser/VXStudioAnalyserModels.h"

#include <array>
#include <deque>
#include <vector>
#include <cstdint>

namespace vxanalyser {

class SpectrumSmoothingPipeline {
public:
    struct Input {
        const vxsuite::analysis::AnalysisSummary* dry = nullptr;
        const vxsuite::analysis::AnalysisSummary* wet = nullptr;
        juce::String selectionKey;
        float averageSeconds  = 1.0f;
        int   smoothingRadius = 0;
        std::uint64_t nowMs   = 0;
    };

    struct Output {
        std::array<float, vxsuite::analysis::kSummarySpectrumBins> beforeToneDb {};
        std::array<float, vxsuite::analysis::kSummarySpectrumBins> afterToneDb  {};
        std::array<float, vxsuite::analysis::kSummarySpectrumBins> deltaToneDb  {};
        // Linear averages used by tone-summary text.
        std::array<float, vxsuite::analysis::kSummarySpectrumBins> beforeLinear {};
        std::array<float, vxsuite::analysis::kSummarySpectrumBins> afterLinear  {};
        int   largestToneBand    = 0;
        float largestToneDeltaDb = 0.0f;
        bool  sparseTone         = false;
        std::vector<int> sparseToneBands;
    };

    Output process(const Input& input) noexcept;
    void   reset() noexcept;

private:
    struct HistoryFrame {
        std::uint64_t timestampMs = 0;
        std::array<float, vxsuite::analysis::kSummarySpectrumBins> beforeLinear {};
        std::array<float, vxsuite::analysis::kSummarySpectrumBins> afterLinear  {};
    };

    static constexpr int kMaxHistoryFrames = 300;
    static constexpr int kUiRefreshHz      = 24;

    juce::String lastKey;
    bool         initialized = false;

    std::deque<HistoryFrame> history;
    std::array<float, vxsuite::analysis::kSummarySpectrumBins> beforeLinearSum {};
    std::array<float, vxsuite::analysis::kSummarySpectrumBins> afterLinearSum  {};
    std::array<float, vxsuite::analysis::kSummarySpectrumBins> deltaToneDb     {};
    std::array<float, vxsuite::analysis::kSummarySpectrumBins> displayDeltaDb  {};
    float largestToneDeltaDb = 0.0f;

    static float bandCenterHz(int band) noexcept;
    static float applyDisplaySlope(float valueDb, float hz) noexcept;
    static float toDb(float linear, float floorDb = -100.0f) noexcept;
    static bool  hasMeaningfulEnergy(float beforeLinear, float afterLinear) noexcept;
    static float smoothScalar(float current, float target, float timeSeconds) noexcept;

    static std::array<float, vxsuite::analysis::kSummarySpectrumBins>
        smoothNeighbourBins(const std::array<float, vxsuite::analysis::kSummarySpectrumBins>& values,
                            int radius) noexcept;

    struct SparseToneInfo {
        bool sparse      = false;
        int  activeBands = 0;
        std::vector<int> significantBands;
    };
    static SparseToneInfo classifySparseTone(
        const std::array<float, vxsuite::analysis::kSummarySpectrumBins>& beforeLinear,
        const std::array<float, vxsuite::analysis::kSummarySpectrumBins>& afterLinear,
        const std::array<float, vxsuite::analysis::kSummarySpectrumBins>& deltaDb) noexcept;
};

} // namespace vxanalyser
