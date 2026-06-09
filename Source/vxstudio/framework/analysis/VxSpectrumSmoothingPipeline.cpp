#include "VxSpectrumSmoothingPipeline.h"

#include "../VxStudioBlockSmoothing.h"

#include <algorithm>
#include <cmath>

namespace vxanalyser {

namespace {
constexpr float kSpectrumMinDb          = -78.0f;
constexpr float kSpectrumMaxDb          = -18.0f;
constexpr float kDisplaySlopeDbPerOct   = 4.5f;
constexpr float kDisplaySlopeRefHz      = 1000.0f;
constexpr float kMeaningfulBandFloorDb  = -72.0f;
} // namespace

// --- static helpers ---

float SpectrumSmoothingPipeline::bandCenterHz(const int band) noexcept {
    constexpr float kMin = 20.0f;
    constexpr float kMax = 20000.0f;
    const float norm = (static_cast<float>(band) + 0.5f)
                     / static_cast<float>(vxsuite::analysis::kSummarySpectrumBins);
    return kMin * std::pow(kMax / kMin, norm);
}

float SpectrumSmoothingPipeline::applyDisplaySlope(const float valueDb, const float hz) noexcept {
    const float oct = std::log2(std::max(20.0f, hz) / kDisplaySlopeRefHz);
    return valueDb + kDisplaySlopeDbPerOct * oct;
}

float SpectrumSmoothingPipeline::toDb(const float linear, const float floorDb) noexcept {
    return juce::Decibels::gainToDecibels(std::max(1.0e-6f, linear), floorDb);
}

bool SpectrumSmoothingPipeline::hasMeaningfulEnergy(const float bLin, const float aLin) noexcept {
    const float maxDb = std::max(toDb(bLin, -120.0f), toDb(aLin, -120.0f));
    return maxDb > kMeaningfulBandFloorDb;
}

float SpectrumSmoothingPipeline::smoothScalar(const float current,
                                               const float target,
                                               const float timeSeconds) noexcept {
    return vxsuite::smoothBlockValue(current, target,
                                     static_cast<double>(kUiRefreshHz), 1, timeSeconds);
}

std::array<float, vxsuite::analysis::kSummarySpectrumBins>
SpectrumSmoothingPipeline::smoothNeighbourBins(
    const std::array<float, vxsuite::analysis::kSummarySpectrumBins>& values,
    const int radius) noexcept {
    if (radius <= 0) return values;
    std::array<float, vxsuite::analysis::kSummarySpectrumBins> out {};
    const int n = static_cast<int>(values.size());
    for (int i = 0; i < n; ++i) {
        float weightedSum = 0.0f;
        float weightTotal = 0.0f;
        for (int off = -radius; off <= radius; ++off) {
            const int idx = juce::jlimit(0, n - 1, i + off);
            const float w = static_cast<float>(radius + 1 - std::abs(off));
            weightedSum += values[static_cast<std::size_t>(idx)] * w;
            weightTotal += w;
        }
        out[static_cast<std::size_t>(i)] = weightedSum / std::max(1.0f, weightTotal);
    }
    return out;
}

SpectrumSmoothingPipeline::SparseToneInfo SpectrumSmoothingPipeline::classifySparseTone(
    const std::array<float, vxsuite::analysis::kSummarySpectrumBins>& beforeLinear,
    const std::array<float, vxsuite::analysis::kSummarySpectrumBins>& afterLinear,
    const std::array<float, vxsuite::analysis::kSummarySpectrumBins>& deltaDb) noexcept {
    SparseToneInfo out;

    float maxEnergy = 0.0f;
    std::array<float, vxsuite::analysis::kSummarySpectrumBins> bandEnergy {};
    std::array<float, vxsuite::analysis::kSummarySpectrumBins> absDelta  {};
    for (int i = 0; i < vxsuite::analysis::kSummarySpectrumBins; ++i) {
        const auto si = static_cast<std::size_t>(i);
        const float e = std::max(beforeLinear[si], afterLinear[si]);
        bandEnergy[si] = e;
        absDelta[si]   = std::abs(deltaDb[si]);
        maxEnergy      = std::max(maxEnergy, e);
    }

    if (maxEnergy <= 1.0e-6f)
        return out;

    double totalEnergy = 0.0;
    std::array<float, 4> top4 { 0.0f, 0.0f, 0.0f, 0.0f };
    for (int i = 0; i < vxsuite::analysis::kSummarySpectrumBins; ++i) {
        const auto si = static_cast<std::size_t>(i);
        const float e = bandEnergy[si];
        totalEnergy += e;
        if (e >= maxEnergy * 0.15f)
            ++out.activeBands;
        for (int slot = 0; slot < 4; ++slot) {
            if (e > top4[static_cast<std::size_t>(slot)]) {
                for (int mv = 3; mv > slot; --mv)
                    top4[static_cast<std::size_t>(mv)] = top4[static_cast<std::size_t>(mv - 1)];
                top4[static_cast<std::size_t>(slot)] = e;
                break;
            }
        }
    }

    const double top4Sum = static_cast<double>(top4[0] + top4[1] + top4[2] + top4[3]);
    const float  peakDom = static_cast<float>(top4[0] / std::max(1.0e-12, totalEnergy));
    const float  top4Dom = static_cast<float>(top4Sum  / std::max(1.0,    totalEnergy));

    for (int i = 0; i < vxsuite::analysis::kSummarySpectrumBins; ++i) {
        const auto si = static_cast<std::size_t>(i);
        if (bandEnergy[si] >= maxEnergy * 0.18f || absDelta[si] >= 1.0f)
            out.significantBands.push_back(i);
    }

    out.sparse = (out.activeBands <= 18 || peakDom >= 0.22f || top4Dom >= 0.60f);
    if (!out.sparse)
        out.significantBands.clear();
    return out;
}

// --- process ---

void SpectrumSmoothingPipeline::reset() noexcept {
    history.clear();
    beforeLinearSum.fill(0.0f);
    afterLinearSum.fill(0.0f);
    deltaToneDb.fill(0.0f);
    displayDeltaDb.fill(0.0f);
    largestToneDeltaDb = 0.0f;
    initialized = false;
    lastKey.clear();
}

SpectrumSmoothingPipeline::Output SpectrumSmoothingPipeline::process(const Input& input) noexcept {
    Output out;

    if (!input.dry || !input.wet)
        return out;

    const bool keyChanged = (!initialized || lastKey != input.selectionKey);
    lastKey      = input.selectionKey;
    initialized  = true;

    if (keyChanged) {
        history.clear();
        beforeLinearSum.fill(0.0f);
        afterLinearSum.fill(0.0f);
        deltaToneDb.fill(0.0f);
        displayDeltaDb.fill(0.0f);
        largestToneDeltaDb = 0.0f;
    }

    const float averageSeconds      = std::max(0.1f, input.averageSeconds);
    const float deltaDisplaySeconds = std::max(0.10f, averageSeconds * 0.75f);
    const float summarySmoothing    = std::max(0.12f, averageSeconds * 0.60f);
    const auto  windowMs            = static_cast<std::uint64_t>(averageSeconds * 1000.0f);

    // Push new frame.
    HistoryFrame frame;
    frame.timestampMs = input.nowMs;
    for (std::size_t i = 0; i < vxsuite::analysis::kSummarySpectrumBins; ++i) {
        frame.beforeLinear[i] = std::max(1.0e-6f, input.dry->spectrum[i]);
        frame.afterLinear[i]  = std::max(1.0e-6f, input.wet->spectrum[i]);
        beforeLinearSum[i]   += frame.beforeLinear[i];
        afterLinearSum[i]    += frame.afterLinear[i];
    }

    while (static_cast<int>(history.size()) >= kMaxHistoryFrames) {
        for (std::size_t i = 0; i < vxsuite::analysis::kSummarySpectrumBins; ++i) {
            beforeLinearSum[i] -= history.front().beforeLinear[i];
            afterLinearSum[i]  -= history.front().afterLinear[i];
        }
        history.pop_front();
    }
    history.push_back(frame);

    while (history.size() > 1
           && (input.nowMs - history.front().timestampMs) > windowMs) {
        for (std::size_t i = 0; i < vxsuite::analysis::kSummarySpectrumBins; ++i) {
            beforeLinearSum[i] -= history.front().beforeLinear[i];
            afterLinearSum[i]  -= history.front().afterLinear[i];
        }
        history.pop_front();
    }

    const float histScale = 1.0f / static_cast<float>(std::max<std::size_t>(1, history.size()));

    // Build per-band display values.
    auto smoothedBefore = out.beforeToneDb;
    auto smoothedAfter  = out.afterToneDb;
    auto smoothedDelta  = out.deltaToneDb;
    float largestAcc = 0.0f;
    int   largestBand = 0;

    for (int i = 0; i < vxsuite::analysis::kSummarySpectrumBins; ++i) {
        const auto si = static_cast<std::size_t>(i);
        const float hz   = bandCenterHz(i);
        const float bLin = std::max(1.0e-6f, beforeLinearSum[si] * histScale);
        const float aLin = std::max(1.0e-6f, afterLinearSum[si]  * histScale);

        out.beforeLinear[si] = bLin;
        out.afterLinear[si]  = aLin;

        const float bDb = juce::jlimit(kSpectrumMinDb, kSpectrumMaxDb,
                                       applyDisplaySlope(toDb(bLin, -120.0f), hz));
        const float aDb = juce::jlimit(kSpectrumMinDb, kSpectrumMaxDb,
                                       applyDisplaySlope(toDb(aLin, -120.0f), hz));
        const float dTarget = juce::jlimit(-24.0f, 24.0f, toDb(aLin) - toDb(bLin));

        smoothedBefore[si] = bDb;
        smoothedAfter[si]  = aDb;

        if (keyChanged)
            deltaToneDb[si] = dTarget;
        else
            deltaToneDb[si] = smoothScalar(deltaToneDb[si], dTarget, deltaDisplaySeconds);

        const float displayTarget = std::abs(deltaToneDb[si]) < 1.0f ? 0.0f : deltaToneDb[si];

        if (keyChanged)
            displayDeltaDb[si] = displayTarget;
        else
            displayDeltaDb[si] = smoothScalar(displayDeltaDb[si], displayTarget, averageSeconds);

        smoothedDelta[si] = displayDeltaDb[si];

        if (hasMeaningfulEnergy(bLin, aLin)
            && std::abs(smoothedDelta[si]) > std::abs(largestAcc)) {
            largestAcc  = smoothedDelta[si];
            largestBand = i;
        }
    }

    const auto sparse = classifySparseTone(out.beforeLinear, out.afterLinear, smoothedDelta);
    out.sparseTone      = sparse.sparse;
    out.sparseToneBands = sparse.significantBands;

    if (sparse.sparse) {
        out.beforeToneDb = smoothedBefore;
        out.afterToneDb  = smoothedAfter;
        out.deltaToneDb  = smoothedDelta;
    } else {
        out.beforeToneDb = smoothNeighbourBins(smoothedBefore, input.smoothingRadius);
        out.afterToneDb  = smoothNeighbourBins(smoothedAfter,  input.smoothingRadius);
        out.deltaToneDb  = smoothNeighbourBins(smoothedDelta,  input.smoothingRadius);
    }

    out.largestToneBand = largestBand;
    largestToneDeltaDb = keyChanged
        ? largestAcc
        : smoothScalar(largestToneDeltaDb, largestAcc, summarySmoothing);
    out.largestToneDeltaDb = largestToneDeltaDb;

    return out;
}

} // namespace vxanalyser
