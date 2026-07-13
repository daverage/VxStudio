#pragma once

#include <array>
#include <cstdint>

namespace vxsuite::rebalance {

enum class RecordingType : int {
    studio = 0,
    live = 1,
    phoneRough = 2
};

enum SourceIndex {
    vocalsSource = 0,
    drumsSource = 1,
    bassSource = 2,
    guitarSource = 3,
    otherSource = 4
};

static constexpr int kSourceCount = 5;
static constexpr int kControlCount = 6;
static constexpr int kFftOrder = 11;
static constexpr int kFftSize = 1 << kFftOrder;
static constexpr int kHopSize = kFftSize / 4;
static constexpr int kBins = kFftSize / 2 + 1;
static constexpr int kDebugBins = 96;
static constexpr int kStemFrameSamples = 512;
static constexpr int kStemFrameChannels = 2;

struct DebugSnapshot {
    std::array<int, kDebugBins> dominantSources {};
    std::array<float, kDebugBins> confidence {};
    std::array<float, kDebugBins> dominantMasks {};
    std::array<float, kDebugBins> otherMasks {};
    std::array<float, kSourceCount> dominantCoverage {};
    float overallConfidence = 0.0f;
    int frameCounter = 0;
};

struct AiMaskFrame {
    bool available = false;
    float confidence = 0.0f;
    std::array<std::array<float, kBins>, kSourceCount> masks {};
};

struct StemFrame {
    bool available = false;
    float confidence = 0.0f;
    std::uint64_t sequenceNumber = 0;
    std::array<std::array<float, kStemFrameSamples>, kStemFrameChannels> mixture {};
    std::array<std::array<std::array<float, kStemFrameSamples>, kStemFrameChannels>, kSourceCount> stems {};
};

} // namespace vxsuite::rebalance
