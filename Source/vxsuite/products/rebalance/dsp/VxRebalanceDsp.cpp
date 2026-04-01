#include "VxRebalanceDsp.h"

#include <algorithm>
#include <cmath>

namespace vxsuite::rebalance {

namespace {
constexpr float kEps = 1.0e-9f;
constexpr int kStrengthIndex = 5;
constexpr float kMaxBoostDb = 18.0f;
constexpr float kMaxCutDb = 18.0f;

using RecordingType = Dsp::RecordingType;
using BandRegion = Dsp::BandRegion;
using SourceBandProfile = Dsp::SourceBandProfile;
using RebalanceModeProfile = Dsp::RebalanceModeProfile;

constexpr BandRegion makeRegion(const float lo,
                                const float hi,
                                const float weight) noexcept {
    return { lo, hi, weight };
}

constexpr SourceBandProfile makeProfile(const BandRegion a = {},
                                        const BandRegion b = {},
                                        const BandRegion c = {},
                                        const BandRegion d = {},
                                        const BandRegion e = {},
                                        const BandRegion f = {}) noexcept {
    return { { a, b, c, d, e, f } };
}

constexpr std::array<RebalanceModeProfile, 3> kModeProfiles { {
    // ---- Studio ----
    {
        // vocals: core 500-1800, shoulders, upper-mid 1800-3500, sibilance/air
        makeProfile(makeRegion(500.0f, 1800.0f, 1.00f),
                    makeRegion(220.0f, 500.0f,  0.34f),
                    makeRegion(1800.0f, 3500.0f, 0.32f),
                    makeRegion(3500.0f, 9000.0f, 0.25f)),
        // drums: kick, snare body, snare crack, hi-hat/cymbal
        makeProfile(makeRegion(60.0f,   180.0f,  1.00f),
                    makeRegion(180.0f,  280.0f,  0.54f),
                    makeRegion(280.0f,  400.0f,  0.10f),
                    makeRegion(1500.0f, 6000.0f, 0.55f),
                    makeRegion(5500.0f, 18000.0f, 0.75f)),
        // bass: low body, sub, upper harmonics taper off above body region
        makeProfile(makeRegion(40.0f, 120.0f,   0.95f),
                    makeRegion(120.0f, 350.0f,  0.42f),
                    makeRegion(350.0f, 1000.0f, 0.10f),
                    makeRegion(1000.0f, 2500.0f, 0.04f)),
        // guitar: body, core mid, upper-mid, presence, shimmer
        makeProfile(makeRegion(150.0f, 700.0f,   1.00f),
                    makeRegion(700.0f, 2500.0f,  0.70f),
                    makeRegion(2500.0f, 5000.0f, 0.25f),
                    makeRegion(5000.0f, 10000.0f, 0.12f)),
        // other: broad residual, low weight
        makeProfile(makeRegion(250.0f, 1500.0f, 0.44f),
                    makeRegion(1500.0f, 5000.0f, 0.22f),
                    makeRegion(20.0f, 20000.0f, 0.03f)),
        0.12f, 20.0f, 90.0f, 1.00f, 0.82f, 0.20f, 0.85f, 0.20f, 1.00f, 0.22f, 1.00f, 0.90f, 0.22f, 35.0f, 80.0f
    },
    // ---- Live ----
    {
        makeProfile(makeRegion(450.0f, 1600.0f, 1.00f),
                    makeRegion(250.0f, 450.0f,  0.32f),
                    makeRegion(1600.0f, 3000.0f, 0.30f),
                    makeRegion(3000.0f, 9000.0f, 0.22f)),
        makeProfile(makeRegion(55.0f,   170.0f,  1.00f),
                    makeRegion(170.0f,  260.0f,  0.50f),
                    makeRegion(260.0f,  350.0f,  0.10f),
                    makeRegion(1400.0f, 5500.0f, 0.50f),
                    makeRegion(5000.0f, 18000.0f, 0.70f)),
        makeProfile(makeRegion(45.0f, 130.0f,   0.95f),
                    makeRegion(130.0f, 400.0f,  0.44f),
                    makeRegion(400.0f, 1100.0f, 0.10f),
                    makeRegion(1100.0f, 2500.0f, 0.04f)),
        makeProfile(makeRegion(160.0f,  700.0f,  1.00f),
                    makeRegion(700.0f,  2200.0f, 0.70f),
                    makeRegion(2200.0f, 4500.0f, 0.25f),
                    makeRegion(4500.0f, 10000.0f, 0.12f)),
        makeProfile(makeRegion(250.0f, 2000.0f, 0.48f),
                    makeRegion(2000.0f, 6000.0f, 0.24f),
                    makeRegion(20.0f, 20000.0f, 0.04f)),
        0.22f, 35.0f, 140.0f, 0.78f, 1.00f, 0.32f, 0.55f, 0.35f, 0.82f, 0.14f, 0.80f, 1.00f, 0.36f, 45.0f, 95.0f
    },
    // ---- Phone / Rough ----
    {
        makeProfile(makeRegion(350.0f, 1200.0f, 1.00f),
                    makeRegion(220.0f,  350.0f,  0.26f),
                    makeRegion(1200.0f, 2500.0f, 0.28f),
                    makeRegion(2500.0f, 9000.0f, 0.18f)),
        makeProfile(makeRegion(90.0f,   180.0f,  1.00f),
                    makeRegion(180.0f,  250.0f,  0.46f),
                    makeRegion(250.0f,  350.0f,  0.08f),
                    makeRegion(1600.0f, 5000.0f, 0.35f),
                    makeRegion(5000.0f, 12000.0f, 0.40f)),
        makeProfile(makeRegion(70.0f, 120.0f,   0.95f),
                    makeRegion(120.0f, 280.0f,  0.40f),
                    makeRegion(280.0f, 800.0f,  0.10f),
                    makeRegion(800.0f, 2000.0f, 0.04f)),
        makeProfile(makeRegion(150.0f,  600.0f,  1.00f),
                    makeRegion(600.0f,  1800.0f, 0.65f),
                    makeRegion(1800.0f, 4000.0f, 0.22f),
                    makeRegion(4000.0f, 8000.0f, 0.10f)),
        makeProfile(makeRegion(300.0f, 3000.0f, 0.56f),
                    makeRegion(3000.0f, 7000.0f, 0.24f),
                    makeRegion(20.0f, 20000.0f, 0.05f)),
        0.28f, 50.0f, 220.0f, 0.74f, 0.88f, 0.40f, 0.32f, 0.40f, 0.64f, 0.08f, 0.65f, 0.95f, 0.52f, 55.0f, 110.0f
    }
} };

inline float clamp01(const float value) noexcept {
    return juce::jlimit(0.0f, 1.0f, value);
}

inline float lerp(const float a, const float b, const float t) noexcept {
    return a + (b - a) * t;
}

inline float smoothingAlpha(const double sampleRate, const float milliseconds) noexcept {
    const float seconds = std::max(0.001f, milliseconds * 0.001f);
    const float frameSeconds = static_cast<float>(Dsp::kHopSize) / static_cast<float>(std::max(1000.0, sampleRate));
    return std::exp(-frameSeconds / seconds);
}

} // namespace

void Dsp::prepare(const double sampleRate, const int maxBlockSize, const int numChannels) {
    sampleRateHz = sampleRate > 1000.0 ? sampleRate : 48000.0;
    preparedChannels = juce::jlimit(1, 2, numChannels);
    maxBlockSizePrepared = std::max(1, maxBlockSize);
    fft.prepare(kFftOrder);
    vxsuite::spectral::prepareSqrtHannWindow(window, kFftSize);

    channels.assign(static_cast<size_t>(preparedChannels), {});
    for (auto& channel : channels) {
        channel.inputFifo.assign(static_cast<size_t>(kFftSize + maxBlockSizePrepared + kHopSize), 0.0f);
        channel.outputFifo.assign(static_cast<size_t>(kFftSize + maxBlockSizePrepared + kHopSize), 0.0f);
        channel.fftData.assign(static_cast<size_t>(kFftSize * 2), 0.0f);
        channel.ola.assign(static_cast<size_t>(kFftSize * 2), 0.0f);
    }
    for (int i = 0; i < kControlCount; ++i) {
        const double timeSeconds = i == kStrengthIndex ? 0.025 : 0.018;
        controlSmoothers[static_cast<size_t>(i)].reset(sampleRateHz, timeSeconds);
        targetControlValues[static_cast<size_t>(i)].store(i == kStrengthIndex ? 1.0f : 0.5f, std::memory_order_relaxed);
    }

    reset();
}

void Dsp::reset() {
    std::fill(prevAnalysisMag.begin(), prevAnalysisMag.end(), 0.0f);
    std::fill(bassContinuity.begin(), bassContinuity.end(), 0.0f);
    std::fill(compositeGain.begin(), compositeGain.end(), 1.0f);
    std::fill(prevCompositeGain.begin(), prevCompositeGain.end(), 1.0f);
    targetMonoScore.store(0.0f, std::memory_order_relaxed);
    targetCompressionScore.store(0.0f, std::memory_order_relaxed);
    targetTiltScore.store(0.0f, std::memory_order_relaxed);
    targetSeparationConfidence.store(1.0f, std::memory_order_relaxed);
    for (int i = 0; i < kControlCount; ++i) {
        const float defaultValue = i == kStrengthIndex ? 1.0f : 0.5f;
        currentControlValues[static_cast<size_t>(i)] = defaultValue;
        controlSmoothers[static_cast<size_t>(i)].setCurrentAndTargetValue(defaultValue);
    }
    for (auto& masks : smoothedMasks)
        std::fill(masks.begin(), masks.end(), 0.0f);
    masksPrimed = false;

    // Reset harmonic grouping and source persistence state
    std::fill(previousWinningSource.begin(), previousWinningSource.end(), otherSource);
    std::fill(previousWinningConfidence.begin(), previousWinningConfidence.end(), 0.0f);
    std::fill(sourcePersistence.begin(), sourcePersistence.end(), 0.0f);
    detectedPeakCount = 0;
    harmonicClusterCount = 0;
    for (auto& peak : detectedPeaks)
        peak = {};
    for (auto& cluster : harmonicClusters)
        cluster = {};
    std::fill(smoothedLogSpectrum.begin(), smoothedLogSpectrum.end(), 0.0f);
    std::fill(spectralEnvelope.begin(), spectralEnvelope.end(), 0.0f);

    // Reset Phase 2: Object tracking state
    for (auto& tracked : trackedClusters)
        tracked = {};
    nextTrackedClusterId = 1;
    for (auto& event : transientEvents)
        event = {};
    nextTransientEventId = 1;
    std::fill(binOwningCluster.begin(), binOwningCluster.end(), -1);
    std::fill(binOwnershipConfidence.begin(), binOwnershipConfidence.end(), 0.0f);
    std::fill(spectralFlux.begin(), spectralFlux.end(), 0.0f);
    currentFrameCount = 0;
    for (auto& value : debugDominantSources)
        value.store(otherSource, std::memory_order_relaxed);
    for (auto& value : debugConfidence)
        value.store(0.0f, std::memory_order_relaxed);
    for (auto& value : debugDominantMasks)
        value.store(0.0f, std::memory_order_relaxed);
    for (auto& value : debugOtherMasks)
        value.store(0.0f, std::memory_order_relaxed);
    for (auto& value : debugDominantCoverage)
        value.store(0.0f, std::memory_order_relaxed);
    debugOverallConfidence.store(0.0f, std::memory_order_relaxed);
    debugFrameCounter.store(0, std::memory_order_relaxed);

    for (auto& channel : channels) {
        std::fill(channel.inputFifo.begin(), channel.inputFifo.end(), 0.0f);
        std::fill(channel.outputFifo.begin(), channel.outputFifo.end(), 0.0f);
        std::fill(channel.fftData.begin(), channel.fftData.end(), 0.0f);
        std::fill(channel.ola.begin(), channel.ola.end(), 0.0f);
        channel.inputCount = 0;
        channel.inputWritePos = 0;
        channel.outputCount = kFftSize;
        channel.outputReadPos = 0;
        channel.outputWritePos = 0;
        channel.olaWritePos = 0;
        std::fill(channel.outputFifo.begin(), channel.outputFifo.end(), 0.0f);
        const int initialZeros = std::min(kFftSize, static_cast<int>(channel.outputFifo.size()));
        channel.outputCount = initialZeros;
        channel.outputWritePos = initialZeros % static_cast<int>(channel.outputFifo.size());
    }
}

void Dsp::setControlTargets(const std::array<float, kControlCount>& normalizedValues) {
    for (int i = 0; i < kControlCount; ++i)
        targetControlValues[static_cast<size_t>(i)].store(normalizedValues[static_cast<size_t>(i)], std::memory_order_relaxed);
}

void Dsp::setAnalysisContext(const AnalysisContext& newContext) noexcept {
    targetVocalDominance.store(newContext.vocalDominance, std::memory_order_relaxed);
    targetIntelligibility.store(newContext.intelligibility, std::memory_order_relaxed);
    targetSpeechPresence.store(newContext.speechPresence, std::memory_order_relaxed);
    targetTransientRisk.store(newContext.transientRisk, std::memory_order_relaxed);
}

void Dsp::setSignalQuality(const vxsuite::SignalQualitySnapshot& newQuality) noexcept {
    targetMonoScore.store(newQuality.monoScore, std::memory_order_relaxed);
    targetCompressionScore.store(newQuality.compressionScore, std::memory_order_relaxed);
    targetTiltScore.store(newQuality.tiltScore, std::memory_order_relaxed);
    targetSeparationConfidence.store(newQuality.separationConfidence, std::memory_order_relaxed);
}

void Dsp::setRecordingType(const RecordingType newType) noexcept {
    targetRecordingType.store(static_cast<int>(newType), std::memory_order_relaxed);
}

Dsp::DebugSnapshot Dsp::getDebugSnapshot() const noexcept {
    DebugSnapshot snapshot;
    for (int i = 0; i < kDebugBins; ++i) {
        snapshot.dominantSources[static_cast<size_t>(i)] =
            debugDominantSources[static_cast<size_t>(i)].load(std::memory_order_relaxed);
        snapshot.confidence[static_cast<size_t>(i)] =
            debugConfidence[static_cast<size_t>(i)].load(std::memory_order_relaxed);
        snapshot.dominantMasks[static_cast<size_t>(i)] =
            debugDominantMasks[static_cast<size_t>(i)].load(std::memory_order_relaxed);
        snapshot.otherMasks[static_cast<size_t>(i)] =
            debugOtherMasks[static_cast<size_t>(i)].load(std::memory_order_relaxed);
    }

    for (int s = 0; s < kSourceCount; ++s) {
        snapshot.dominantCoverage[static_cast<size_t>(s)] =
            debugDominantCoverage[static_cast<size_t>(s)].load(std::memory_order_relaxed);
    }

    snapshot.overallConfidence = debugOverallConfidence.load(std::memory_order_relaxed);
    snapshot.frameCounter = debugFrameCounter.load(std::memory_order_relaxed);
    return snapshot;
}

void Dsp::process(juce::AudioBuffer<float>& buffer) {
    const int numSamples = buffer.getNumSamples();
    const int numChannels = std::min(buffer.getNumChannels(), preparedChannels);
    if (numSamples <= 0 || numChannels <= 0 || channels.empty())
        return;

    for (int sample = 0; sample < numSamples; ++sample) {
        for (int i = 0; i < kControlCount; ++i) {
            controlSmoothers[static_cast<size_t>(i)].setTargetValue(
                targetControlValues[static_cast<size_t>(i)].load(std::memory_order_relaxed));
            currentControlValues[static_cast<size_t>(i)] = controlSmoothers[static_cast<size_t>(i)].getNextValue();
        }

        for (int ch = 0; ch < numChannels; ++ch) {
            auto& state = channels[static_cast<size_t>(ch)];
            state.inputFifo[static_cast<size_t>(state.inputWritePos)] = buffer.getReadPointer(ch)[sample];
            state.inputWritePos = (state.inputWritePos + 1) % static_cast<int>(state.inputFifo.size());
            state.inputCount = std::min(state.inputCount + 1, static_cast<int>(state.inputFifo.size()));
        }

        while (channels.front().inputCount >= kFftSize)
            processFrame();

        for (int ch = 0; ch < numChannels; ++ch) {
            auto& state = channels[static_cast<size_t>(ch)];
            float value = 0.0f;
            if (state.outputCount > 0) {
                value = state.outputFifo[static_cast<size_t>(state.outputReadPos)];
                state.outputReadPos = (state.outputReadPos + 1) % static_cast<int>(state.outputFifo.size());
                --state.outputCount;
            }
            buffer.getWritePointer(ch)[sample] = value;
        }
    }
}

void Dsp::processFrame() {
    std::array<float, kBins> analysisMag {};
    std::array<float, kBins> centerWeight {};
    std::array<float, kBins> sideWeight {};

    for (int ch = 0; ch < preparedChannels; ++ch) {
        auto& state = channels[static_cast<size_t>(ch)];
        std::fill(state.fftData.begin(), state.fftData.end(), 0.0f);
        const int fifoSize = static_cast<int>(state.inputFifo.size());
        const int frameStart = (state.inputWritePos - state.inputCount + fifoSize) % fifoSize;
        for (int i = 0; i < kFftSize; ++i) {
            const int srcIndex = (frameStart + i) % fifoSize;
            state.fftData[static_cast<size_t>(i)] = state.inputFifo[static_cast<size_t>(srcIndex)] * window[static_cast<size_t>(i)];
        }
        fft.performForward(state.fftData.data());

        for (int k = 0; k < kBins; ++k) {
            const float re = state.fftData[static_cast<size_t>(2 * k)];
            const float im = (k == 0 || k == kBins - 1) ? 0.0f : state.fftData[static_cast<size_t>(2 * k + 1)];
            analysisMag[static_cast<size_t>(k)] += std::sqrt(std::max(kEps, re * re + im * im));
        }
    }

    for (float& mag : analysisMag)
        mag /= static_cast<float>(preparedChannels);

    if (preparedChannels >= 2) {
        auto& left = channels[0];
        auto& right = channels[1];
        for (int k = 0; k < kBins; ++k) {
            const float reL = left.fftData[static_cast<size_t>(2 * k)];
            const float imL = (k == 0 || k == kBins - 1) ? 0.0f : left.fftData[static_cast<size_t>(2 * k + 1)];
            const float reR = right.fftData[static_cast<size_t>(2 * k)];
            const float imR = (k == 0 || k == kBins - 1) ? 0.0f : right.fftData[static_cast<size_t>(2 * k + 1)];
            const float midRe = 0.5f * (reL + reR);
            const float midIm = 0.5f * (imL + imR);
            const float sideRe = 0.5f * (reL - reR);
            const float sideIm = 0.5f * (imL - imR);
            const float midMag = std::sqrt(std::max(kEps, midRe * midRe + midIm * midIm));
            const float sideMag = std::sqrt(std::max(kEps, sideRe * sideRe + sideIm * sideIm));
            const float total = midMag + sideMag + kEps;
            centerWeight[static_cast<size_t>(k)] = juce::jlimit(0.0f, 1.0f, midMag / total);
            sideWeight[static_cast<size_t>(k)] = juce::jlimit(0.0f, 1.0f, sideMag / total);
        }
    } else {
        for (int k = 0; k < kBins; ++k) {
            centerWeight[static_cast<size_t>(k)] = 1.0f;
            sideWeight[static_cast<size_t>(k)] = 0.0f;
        }
    }

    computeMasks(analysisMag, centerWeight, sideWeight);

    const auto& modeProfile = currentModeProfile();

    for (int ch = 0; ch < preparedChannels; ++ch) {
        auto& state = channels[static_cast<size_t>(ch)];
        for (int k = 0; k < kBins; ++k) {
            const float re = state.fftData[static_cast<size_t>(2 * k)];
            const float im = (k == 0 || k == kBins - 1) ? 0.0f : state.fftData[static_cast<size_t>(2 * k + 1)];
            const float gain = juce::jlimit(0.0f, 8.0f, compositeGain[static_cast<size_t>(k)]);
            const float ratio = gain;
            state.fftData[static_cast<size_t>(2 * k)] *= ratio;
            if (k != 0 && k != kBins - 1)
                state.fftData[static_cast<size_t>(2 * k + 1)] *= ratio;
        }

        fft.performInverse(state.fftData.data());
        const int olaSize = static_cast<int>(state.ola.size());
        for (int i = 0; i < kFftSize; ++i) {
            const int olaIndex = (state.olaWritePos + i) % olaSize;
            state.ola[static_cast<size_t>(olaIndex)] += state.fftData[static_cast<size_t>(i)] * window[static_cast<size_t>(i)];
        }

        for (int i = 0; i < kHopSize; ++i) {
            if (state.outputCount < static_cast<int>(state.outputFifo.size()))
            {
                const int olaIndex = (state.olaWritePos + i) % olaSize;
                state.outputFifo[static_cast<size_t>(state.outputWritePos)] = state.ola[static_cast<size_t>(olaIndex)] * 0.5f;
                state.ola[static_cast<size_t>(olaIndex)] = 0.0f;
                state.outputWritePos = (state.outputWritePos + 1) % static_cast<int>(state.outputFifo.size());
                ++state.outputCount;
            }
        }

        state.olaWritePos = (state.olaWritePos + kHopSize) % olaSize;
        state.inputCount -= kHopSize;
    }
}

void Dsp::computeMasks(const std::array<float, kBins>& analysisMag,
                       const std::array<float, kBins>& centerWeight,
                       const std::array<float, kBins>& sideWeight) {
    std::array<std::array<float, kBins>, kSourceCount> rawWeights {};
    const auto& modeProfile = currentModeProfile();
    const AnalysisContext analysisContext {
        targetVocalDominance.load(std::memory_order_relaxed),
        targetIntelligibility.load(std::memory_order_relaxed),
        targetSpeechPresence.load(std::memory_order_relaxed),
        targetTransientRisk.load(std::memory_order_relaxed)
    };
    const vxsuite::SignalQualitySnapshot signalQuality {
        targetMonoScore.load(std::memory_order_relaxed),
        targetCompressionScore.load(std::memory_order_relaxed),
        targetTiltScore.load(std::memory_order_relaxed),
        targetSeparationConfidence.load(std::memory_order_relaxed)
    };
    float meanMag = 0.0f;
    for (const float mag : analysisMag)
        meanMag += mag;
    meanMag /= static_cast<float>(kBins);

    // Build spectral envelope for vocal/guitar arbitration
    buildSpectralEnvelope(analysisMag);

    float fluxAccum = 0.0f;
    int fluxCount = 0;
    for (int k = 0; k < kBins; ++k) {
        const float hz = binToHz(k);
        if (hz > 4000.0f) {
            fluxAccum += std::abs(analysisMag[static_cast<size_t>(k)] - prevAnalysisMag[static_cast<size_t>(k)]);
            ++fluxCount;
        }
    }
    const float highFreqFlux = fluxAccum / std::max(1, fluxCount) / std::max(kEps, meanMag);
    const float transientThreshold = lerp(0.80f, 0.34f, signalQuality.compressionScore) / std::max(0.45f, modeProfile.transientTrust);
    const float transientPrior = clamp01(highFreqFlux / transientThreshold) * modeProfile.transientTrust;
    const bool hardTransient = highFreqFlux > transientThreshold * 1.45f;
    const float bassThreshold = meanMag * 0.30f;
    const float monoVocalPenalty = lerp(0.0f, 0.30f, signalQuality.monoScore);
    const float voiceBias = juce::jlimit(0.45f, 1.75f,
                                         0.55f
                                       + 0.65f * analysisContext.vocalDominance
                                       + 0.32f * analysisContext.intelligibility
                                       + 0.18f * analysisContext.speechPresence
                                       - monoVocalPenalty);
    const float drumBias = juce::jlimit(0.35f, 1.70f,
                                        0.50f
                                      + 0.65f * transientPrior * modeProfile.drumTransientEmphasis
                                      + 0.20f * analysisContext.transientRisk);
    const float steadyPriorScale = lerp(1.0f, 0.40f, signalQuality.compressionScore) * modeProfile.harmonicTrust;
    const float vocalCentredCoeff = modeProfile.vocalCenterBias * lerp(1.0f, 0.45f, signalQuality.monoScore * (1.0f - modeProfile.stereoWidthTrust));
    const float bassContBonus = 1.0f + 0.18f * modeProfile.harmonicContinuityWeight * lerp(1.0f, 0.30f, signalQuality.tiltScore);
    const float attackAlpha = smoothingAlpha(sampleRateHz, modeProfile.attackMs);
    const float releaseAlpha = smoothingAlpha(sampleRateHz, modeProfile.releaseMs);
    const float confidenceRange = std::max(0.05f, 1.0f - modeProfile.confidenceFloor);
    const float usableConfidence = clamp01((signalQuality.separationConfidence - modeProfile.confidenceFloor) / confidenceRange);
    const auto recordingType = static_cast<RecordingType>(juce::jlimit(0, static_cast<int>(kModeProfiles.size()) - 1,
                                                                       targetRecordingType.load(std::memory_order_relaxed)));

    // Detect spectral peaks and build harmonic clusters
    detectSpectralPeaks(analysisMag);
    buildHarmonicClusters(analysisMag);
    analyseClusterSources(analysisMag, centerWeight, sideWeight, transientPrior, steadyPriorScale);

    // Phase 2: Object-based tracking
    ++currentFrameCount;
    updateTrackedClusters(analysisMag, centerWeight, sideWeight);
    detectTransientEvents(analysisMag, centerWeight, sideWeight, transientPrior);
    updateTransientEvents();
    updateObjectSourceProbabilities(analysisMag, centerWeight, sideWeight, transientPrior, steadyPriorScale);
    writeObjectOwnershipToBins();

    for (int k = 0; k < kBins; ++k) {
        const float hz = binToHz(k);
        const float prevMag = prevAnalysisMag[static_cast<size_t>(k)];
        const float deltaNorm = std::abs(analysisMag[static_cast<size_t>(k)] - prevMag)
            / std::max({ kEps, analysisMag[static_cast<size_t>(k)], prevMag, meanMag });
        const float localTransient = clamp01(deltaNorm * 4.0f);
        const float rawSteadyPrior = 1.0f - clamp01(deltaNorm * 2.5f);
        const float steadyPrior = rawSteadyPrior * steadyPriorScale;
        const float centered = lerp(1.0f - 0.5f * sideWeight[static_cast<size_t>(k)],
                                    centerWeight[static_cast<size_t>(k)],
                                    modeProfile.stereoWidthTrust);
        const float wide = lerp(0.0f, sideWeight[static_cast<size_t>(k)], modeProfile.stereoWidthTrust);
        const float kickWindow = std::exp(-0.5f * std::pow((hz - 72.0f) / 24.0f, 2.0f));
        const float bassWindow = modeAwareBandWeight(hz, modeProfile.bass);
        const float vocalWindow = modeAwareBandWeight(hz, modeProfile.vocals);
        const float drumWindow = modeAwareBandWeight(hz, modeProfile.drums);
        const float guitarWindow = modeAwareBandWeight(hz, modeProfile.guitars);
        const float guitarBodyWindow = smoothBand(hz, 150.0f, 900.0f);
        const float guitarUpperWindow = smoothBand(hz, 900.0f, 2500.0f);
        const float otherWindow = modeAwareBandWeight(hz, modeProfile.other);
        const bool phoneMode = recordingType == RecordingType::phoneRough;
        const bool liveMode = recordingType == RecordingType::live;

        float bass = 0.0f;
        if (bassWindow > 0.0f)
            bass = bassWindow * (0.08f + 0.82f * centered + 0.30f * steadyPrior * modeProfile.harmonicContinuityWeight);
        if (analysisMag[static_cast<size_t>(k)] > bassThreshold)
            bassContinuity[static_cast<size_t>(k)] = std::min(8.0f, bassContinuity[static_cast<size_t>(k)] + 1.0f);
        else
            bassContinuity[static_cast<size_t>(k)] = std::max(0.0f, bassContinuity[static_cast<size_t>(k)] - 1.0f);
        if (bassContinuity[static_cast<size_t>(k)] >= 6.0f)
            bass *= bassContBonus;
        bass *= lerp(1.0f, phoneMode ? 0.42f : (liveMode ? 0.30f : 0.35f), transientPrior * kickWindow);
        if (phoneMode && hz < 85.0f)
            bass *= 0.55f;
        bass *= lerp(1.0f, 0.55f, wide);
        bass *= lerp(1.0f, 0.28f, transientPrior * drumWindow);
        rawWeights[bassSource][static_cast<size_t>(k)] = bass;

        const float drumFloor = lerp(0.010f, 0.06f, modeProfile.residualFallback);
        float drums = drumFloor + 0.04f * drumWindow;
        if (hz >= 35.0f && hz <= 120.0f)
            drums += drumBias * kickWindow * drumWindow * (0.20f + 0.10f * centered) * (0.20f + 0.95f * transientPrior);
        if (hz >= 150.0f && hz <= 340.0f)
            drums += drumBias * drumWindow * gaussianPeak(hz, 210.0f, 85.0f) * (0.16f + 0.18f * transientPrior);
        if (hz >= 1400.0f && hz <= 6500.0f)
            drums += drumBias * drumWindow * gaussianPeak(hz, 3000.0f, 1300.0f)
                * (0.10f + 0.55f * transientPrior + 0.75f * localTransient);
        if (hz >= 5000.0f) {
            const float hihatWide = lerp(wide, 0.0f, signalQuality.monoScore);
            drums += drumBias * drumWindow * (0.06f + 0.16f * hihatWide + 0.42f * transientPrior + 0.72f * localTransient);
        }
        rawWeights[drumsSource][static_cast<size_t>(k)] = drums;

        float vocals = 0.01f;
        if (vocalWindow > 0.0f) {
            vocals = voiceBias * vocalWindow * (0.10f + vocalCentredCoeff * centered + 0.24f * steadyPrior);
            if (phoneMode && hz > 2000.0f)
                vocals *= 0.55f;
            if (phoneMode && hz < 200.0f)
                vocals *= 0.65f;
            vocals *= lerp(1.0f, 0.62f, transientPrior * clamp01((hz - 1800.0f) / 2200.0f));
            vocals *= lerp(1.0f, 0.82f, wide);
            if (hz >= 250.0f && hz <= 600.0f)
                vocals *= 1.15f;
        }
        rawWeights[vocalsSource][static_cast<size_t>(k)] = vocals;

        float guitar = 0.028f;
        if (guitarWindow > 0.0f) {
            const float guitarBodyEvidence = clamp01(
                0.55f * steadyPrior + 0.20f * wide + 0.18f * (1.0f - centered) + 0.35f * guitarBodyWindow);
            guitar = guitarWindow * (0.08f + 0.55f * steadyPrior * modeProfile.harmonicTrust
                + 0.22f * wide + 0.14f * (1.0f - centered));
            guitar *= lerp(0.90f, 1.18f, guitarBodyWindow);
            if (guitarUpperWindow > 0.0f)
                guitar *= lerp(0.68f, 1.0f, guitarBodyEvidence);
            if (steadyPrior > 0.45f)
                guitar *= lerp(1.0f, liveMode ? 1.28f : 1.12f, steadyPrior);
            guitar *= lerp(1.0f, phoneMode ? 0.90f : 0.82f, centered);
            guitar *= lerp(1.0f, phoneMode ? 0.84f : 0.76f, transientPrior);
            if (hz > 1800.0f)
                guitar *= liveMode ? 0.82f : 0.74f;
            if (phoneMode && hz > 2000.0f)
                guitar *= 0.55f;
            if (phoneMode && hz < 200.0f)
                guitar *= 0.65f;
        }
        rawWeights[guitarSource][static_cast<size_t>(k)] = guitar;

        const float occupied = clamp01(0.48f * bass + 0.62f * drums + 0.78f * vocals + 0.48f * guitar);
        float other = 0.003f + 0.24f * modeProfile.residualFallbackStrength * (1.0f - occupied) + 0.05f * wide;
        other += otherWindow * (0.03f + 0.42f * modeProfile.residualFallback * (0.35f + 0.65f * wide));
        if (phoneMode && hz >= 400.0f && hz <= 1800.0f)
            other *= 1.02f;
        if (hz < modeProfile.lowEndUnityBlendEndHz)
            other *= 0.12f;
        rawWeights[otherSource][static_cast<size_t>(k)] = other;

        if (hz < 120.0f) {
            rawWeights[guitarSource][static_cast<size_t>(k)] *= 0.4f;
            rawWeights[vocalsSource][static_cast<size_t>(k)] *= 0.3f;
        }

        if (phoneMode && hz >= 400.0f && hz <= 1800.0f) {
            rawWeights[vocalsSource][static_cast<size_t>(k)] *= 0.92f;
            rawWeights[guitarSource][static_cast<size_t>(k)] *= 0.96f;
            rawWeights[otherSource][static_cast<size_t>(k)] *= 0.88f;
        } else if (liveMode && hz >= 300.0f && hz <= 1200.0f) {
            rawWeights[guitarSource][static_cast<size_t>(k)] *= 1.34f;
            rawWeights[otherSource][static_cast<size_t>(k)] *= 0.68f;
        }

        if (liveMode && hz >= 1200.0f && hz <= 2400.0f) {
            rawWeights[guitarSource][static_cast<size_t>(k)] *= lerp(1.0f, 1.18f, steadyPrior);
            rawWeights[otherSource][static_cast<size_t>(k)] *= lerp(1.0f, 0.72f, steadyPrior);
        }

        if ((transientPrior > 0.45f || localTransient > 0.20f) && hz >= 1400.0f && hz <= 12000.0f) {
            const float transientDrive = std::max(transientPrior, localTransient);
            rawWeights[drumsSource][static_cast<size_t>(k)] *= lerp(1.0f, 1.85f, transientDrive);
            rawWeights[guitarSource][static_cast<size_t>(k)] *= lerp(1.0f, 0.68f, transientDrive);
            rawWeights[otherSource][static_cast<size_t>(k)] *= lerp(1.0f, 0.42f, transientDrive);
        }

        // Apply harmonic cluster influence to raw weights
        for (int c = 0; c < harmonicClusterCount; ++c) {
            const auto& cluster = harmonicClusters[static_cast<size_t>(c)];
            if (!cluster.active || cluster.confidence < 0.15f)
                continue;
            
            // Check if bin k is within range of any cluster member
            float maxFalloff = 0.0f;
            for (int m = 0; m < cluster.memberCount; ++m) {
                const int distance = std::abs(cluster.memberBins[static_cast<size_t>(m)] - k);
                if (distance <= 2) {
                    const float falloff = 1.0f - (distance / 2.0f);
                    maxFalloff = std::max(maxFalloff, falloff);
                }
            }
            
            if (maxFalloff > 0.0f) {
                const float conf = cluster.confidence * maxFalloff;
                
                // Normalize source scores as proper probability
                float sourceScoreSum = 0.0f;
                for (int i = 0; i < kSourceCount; ++i)
                    sourceScoreSum += cluster.sourceScores[static_cast<size_t>(i)];
                
                for (int s = 0; s < kSourceCount; ++s) {
                    const float scoreNorm =
                        cluster.sourceScores[static_cast<size_t>(s)] / (sourceScoreSum + kEps);
                    rawWeights[static_cast<size_t>(s)][static_cast<size_t>(k)] *=
                        lerp(1.0f, 0.5f + scoreNorm * 1.5f, conf);
                }
                // Extra boost to dominant source
                rawWeights[static_cast<size_t>(cluster.dominantSource)][static_cast<size_t>(k)] *=
                    lerp(1.0f, 1.3f, conf);
            }
        }

        float total = 0.0f;
        for (int source = 0; source < kSourceCount; ++source)
            total += rawWeights[source][static_cast<size_t>(k)];
        total = std::max(kEps, total);

        float semanticSupport[kSourceCount] {};
        if (hz < 220.0f) { // Low end support
            const float bassVsDrums = clamp01(0.48f
                + 0.34f * steadyPrior
                + 0.12f * centered
                - 0.42f * transientPrior);
            semanticSupport[bassSource] *= lerp(0.92f, 1.60f, bassVsDrums);
            semanticSupport[drumsSource] *= lerp(1.18f, 0.74f, bassVsDrums);
            semanticSupport[otherSource] *= 0.42f;
        }

        if (hz >= 280.0f && hz <= 3200.0f) { // Midrange vocal vs guitar arbitration
            const float vocalScore =
                vocalWindow * (0.6f + 0.4f * centered + 0.3f * analysisContext.speechPresence);
            const float guitarScore =
                guitarWindow * (0.6f + 0.3f * steadyPrior + 0.3f * wide);

            const float totalScore = vocalScore + guitarScore + kEps;

            const float vocalDominance = vocalScore / totalScore;
            const float guitarDominance = guitarScore / totalScore;

            rawWeights[vocalsSource][static_cast<size_t>(k)] *= (0.7f + 0.6f * vocalDominance);
            rawWeights[guitarSource][static_cast<size_t>(k)] *= (0.7f + 0.6f * guitarDominance);

            // Kill ambiguity
            rawWeights[otherSource][static_cast<size_t>(k)] *= 0.6f;

            if (liveMode && hz >= 350.0f && hz <= 2200.0f) {
                rawWeights[guitarSource][static_cast<size_t>(k)] *= (1.14f + 0.88f * guitarDominance);
                rawWeights[vocalsSource][static_cast<size_t>(k)] *= lerp(1.0f, 0.90f, guitarDominance);
                rawWeights[otherSource][static_cast<size_t>(k)] *= 0.40f;
            } else if (phoneMode && hz >= 350.0f && hz <= 1800.0f) {
                rawWeights[guitarSource][static_cast<size_t>(k)] *= (1.00f + 0.42f * guitarDominance);
                rawWeights[otherSource][static_cast<size_t>(k)] *= 0.58f;
            }
        }

        // Heuristic semantic support (DSP-only, no ML)
        // Add envelope-aware support for vocals and guitar
        const float envelopeMag = std::exp(spectralEnvelope[static_cast<size_t>(k)]);
        const float vocalEnv = vocalFormantSupport(hz, analysisMag[static_cast<size_t>(k)], envelopeMag);
        const float guitarEnv = guitarTonalSupport(hz, analysisMag[static_cast<size_t>(k)], envelopeMag, centered, wide, steadyPrior);

        semanticSupport[vocalsSource] = clamp01(0.02f + 0.98f * vocalWindow + 0.16f * centered - 0.10f * wide);
        semanticSupport[drumsSource]  = clamp01(0.03f + 0.94f * drumWindow + 0.16f * transientPrior + 0.10f * wide);
        semanticSupport[bassSource]   = clamp01(0.01f + 1.04f * bassWindow + 0.10f * centered + 0.10f * steadyPrior);
        semanticSupport[guitarSource] = clamp01(0.02f + 0.98f * guitarWindow + 0.16f * wide + 0.10f * (1.0f - centered) + 0.12f * steadyPrior);
        semanticSupport[otherSource]  = clamp01(0.05f + 0.72f * otherWindow + 0.18f * wide + 0.10f * (1.0f - centered));

        // Blend in envelope-aware support
        // Stronger influence in critical midrange (300-3000Hz)
        const float midrangeWeight = (hz >= 300.0f && hz <= 3000.0f) ? 0.45f : 0.2f;
        semanticSupport[vocalsSource] = lerp(semanticSupport[vocalsSource], 
                                             clamp01(semanticSupport[vocalsSource] + 0.5f * vocalEnv), 
                                             midrangeWeight);
        semanticSupport[guitarSource] = lerp(semanticSupport[guitarSource], 
                                             clamp01(semanticSupport[guitarSource] + 0.5f * guitarEnv), 
                                             midrangeWeight);

        if (hz < 140.0f)
            semanticSupport[vocalsSource] *= 0.04f;
        if (hz > 9000.0f)
            semanticSupport[vocalsSource] *= 0.18f;

        // Tighten low-end protection by source type
        if (hz < 110.0f) {
            semanticSupport[guitarSource] *= 0.5f;
            semanticSupport[otherSource] *= 0.5f;
        }
        // Drums retain moderate influence in kick region (60-120 Hz)
        if (hz >= 60.0f && hz <= 120.0f)
            semanticSupport[drumsSource] *= 0.85f;
        if ((transientPrior > 0.45f || localTransient > 0.20f) && hz >= 1400.0f && hz <= 12000.0f) {
            const float transientDrive = std::max(transientPrior, localTransient);
            semanticSupport[drumsSource] *= lerp(1.0f, 1.42f, transientDrive);
            semanticSupport[guitarSource] *= lerp(1.0f, 0.82f, transientDrive);
            semanticSupport[otherSource] *= lerp(1.0f, 0.70f, transientDrive);
        }
        // Bass has strong influence in bass region
        if (hz >= 40.0f && hz <= 150.0f)
            semanticSupport[bassSource] *= 1.15f;

        if (hz > 380.0f)
            semanticSupport[bassSource] *= 0.18f;
        if (hz > 700.0f)
            semanticSupport[bassSource] *= 0.08f;

        if (hz < 110.0f)
            semanticSupport[guitarSource] *= 0.18f;
        if (hz > 6500.0f)
            semanticSupport[guitarSource] *= 0.12f;

        if (hz < 110.0f)
            semanticSupport[otherSource] *= 0.10f;

        float conditionedMasks[kSourceCount] {};
        float conditionedTotal = 0.0f;

        for (int source = 0; source < kSourceCount; ++source) {
            const float baseMask = rawWeights[source][static_cast<size_t>(k)] / total;
            const float sourceSupport = 0.035f + (1.0f - 0.035f) * semanticSupport[static_cast<size_t>(source)];
            conditionedMasks[source] = std::pow(clamp01(baseMask), 1.08f) * sourceSupport;
        }

        if (phoneMode && hz >= 400.0f && hz <= 1800.0f) {
            conditionedMasks[vocalsSource] *= 0.92f;
            conditionedMasks[guitarSource] *= 0.98f;
            conditionedMasks[otherSource] *= 0.82f;
        } else if (liveMode && hz >= 300.0f && hz <= 1600.0f) {
            conditionedMasks[guitarSource] *= 1.48f;
            conditionedMasks[otherSource] *= 0.50f;
        }

        if (liveMode && hz >= 1600.0f && hz <= 2400.0f) {
            conditionedMasks[guitarSource] *= 1.18f;
            conditionedMasks[otherSource] *= 0.72f;
        }

        if ((transientPrior > 0.45f || localTransient > 0.20f) && hz >= 1400.0f && hz <= 12000.0f) {
            const float transientDrive = std::max(transientPrior, localTransient);
            conditionedMasks[drumsSource] *= lerp(1.0f, 1.38f, transientDrive);
            conditionedMasks[guitarSource] *= lerp(1.0f, 0.90f, transientDrive);
            conditionedMasks[otherSource] *= lerp(1.0f, 0.72f, transientDrive);
        }

        if ((phoneMode || liveMode) && hz >= 250.0f && hz <= 2200.0f) {
            const float guitarClaim = clamp01(
                0.50f * semanticSupport[guitarSource]
                + 0.25f * steadyPrior
                + 0.15f * wide
                + 0.10f * (1.0f - centered));
            conditionedMasks[guitarSource] *= lerp(1.0f, phoneMode ? 1.14f : 1.20f, guitarClaim);
            conditionedMasks[otherSource] *= lerp(1.0f, phoneMode ? 0.72f : 0.62f, guitarClaim);
        }

        // Reduce 'other' as fallback sink when top two named sources dominate
        float sortedScores[kSourceCount] {};
        for (int s = 0; s < kSourceCount; ++s)
            sortedScores[s] = conditionedMasks[static_cast<size_t>(s)];
        std::sort(sortedScores, sortedScores + kSourceCount, std::greater<float>());
        const float top1 = sortedScores[0];
        const float top2 = sortedScores[1];
        if ((top1 + top2) > 0.72f && otherWindow < 0.35f) {
            conditionedMasks[static_cast<size_t>(otherSource)] *= 0.65f;
        }

        conditionedTotal = 0.0f;
        for (int source = 0; source < kSourceCount; ++source)
            conditionedTotal += conditionedMasks[source];
        conditionedTotal = std::max(kEps, conditionedTotal);

        for (int source = 0; source < kSourceCount; ++source) {
            const float nextMask = conditionedMasks[source] / conditionedTotal;
            if (!masksPrimed) {
                smoothedMasks[source][static_cast<size_t>(k)] = nextMask;
            } else {
                const float previous = smoothedMasks[source][static_cast<size_t>(k)];
                const float alpha = nextMask > previous ? attackAlpha : releaseAlpha;
                smoothedMasks[source][static_cast<size_t>(k)] =
                    alpha * previous + (1.0f - alpha) * nextMask;
            }
        }
    }

    // Apply source persistence after mask smoothing
    applySourcePersistence(smoothedMasks, transientPrior);

    // Phase 2: Object ownership override - make objects authoritative
    applyObjectOwnershipToMasks(smoothedMasks);

    // Phase 2: Foreground/background rendering for object-based separation
    buildForegroundBackgroundRender();

    for (int k = 0; k < kBins; ++k)
        prevAnalysisMag[static_cast<size_t>(k)] = analysisMag[static_cast<size_t>(k)];

    masksPrimed = true;
}

const RebalanceModeProfile& Dsp::currentModeProfile() const noexcept {
    const int rawIndex = targetRecordingType.load(std::memory_order_relaxed);
    const int index = juce::jlimit(0, static_cast<int>(kModeProfiles.size()) - 1, rawIndex);
    return kModeProfiles[static_cast<size_t>(index)];
}

float Dsp::modeAwareBandWeight(const float hz, const SourceBandProfile& profile) const noexcept {
    float weight = 0.0f;
    for (const auto& region : profile.regions) {
        if (region.weight <= 0.0f || region.hi <= region.lo)
            continue;
        weight += region.weight * smoothBand(hz, region.lo, region.hi);
    }
    return weight;
}

float Dsp::smoothBand(const float hz, const float lo, const float hi) const noexcept {
    if (hi <= lo)
        return 0.0f;

    const float edge = std::max(20.0f, (hi - lo) * 0.25f);
    const float rise = clamp01((hz - (lo - edge)) / edge);
    const float fall = clamp01(((hi + edge) - hz) / edge);
    return std::min(rise, fall);
}

float Dsp::binToHz(const int bin) const noexcept {
    return static_cast<float>(bin) * static_cast<float>(sampleRateHz) / static_cast<float>(kFftSize);
}

float Dsp::gaussianPeak(const float x, const float centre, const float sigma) const noexcept {
    const float d = (x - centre) / std::max(1.0f, sigma);
    return std::exp(-0.5f * d * d);
}

void Dsp::buildSpectralEnvelope(const std::array<float, kBins>& analysisMag) {
    const int envelopeRadius = 8;
    for (int k = 0; k < kBins; ++k) {
        smoothedLogSpectrum[static_cast<size_t>(k)] = std::log(std::max(kEps, analysisMag[static_cast<size_t>(k)]));
    }
    for (int k = 0; k < kBins; ++k) {
        float sum = 0.0f;
        int count = 0;
        for (int r = -envelopeRadius; r <= envelopeRadius; ++r) {
            const int idx = k + r;
            if (idx >= 0 && idx < kBins) {
                sum += smoothedLogSpectrum[static_cast<size_t>(idx)];
                ++count;
            }
        }
        spectralEnvelope[static_cast<size_t>(k)] = sum / std::max(1, count);
    }
}

void Dsp::detectSpectralPeaks(const std::array<float, kBins>& analysisMag) {
    detectedPeakCount = 0;
    const float minHz = 80.0f;
    const float maxHz = 5000.0f;
    const float localThreshold = 1.3f;
    const float globalThreshold = 0.15f;

    struct Candidate {
        int bin;
        float hz;
        float mag;
    };
    std::array<Candidate, 48> candidates {};
    int candidateCount = 0;

    for (int k = 1; k < kBins - 1; ++k) {
        const float hz = binToHz(k);
        if (hz < minHz || hz > maxHz)
            continue;

        const float mag = analysisMag[static_cast<size_t>(k)];
        const float leftMag = analysisMag[static_cast<size_t>(k - 1)];
        const float rightMag = analysisMag[static_cast<size_t>(k + 1)];

        if (mag <= leftMag || mag <= rightMag)
            continue;

        float localSum = 0.0f;
        int localCount = 0;
        for (int r = -2; r <= 2; ++r) {
            const int idx = k + r;
            if (idx >= 0 && idx < kBins) {
                localSum += analysisMag[static_cast<size_t>(idx)];
                ++localCount;
            }
        }
        const float localAvg = localSum / std::max(1, localCount);

        if (mag > localAvg * localThreshold && mag > globalThreshold) {
            if (candidateCount < static_cast<int>(candidates.size())) {
                candidates[static_cast<size_t>(candidateCount)] = { k, hz, mag };
                ++candidateCount;
            }
        }
    }

    std::sort(candidates.begin(), candidates.begin() + static_cast<size_t>(candidateCount),
              [](const Candidate& a, const Candidate& b) { return a.mag > b.mag; });

    const int maxPeaks = std::min(candidateCount, kMaxPeaks);
    for (int i = 0; i < maxPeaks; ++i) {
        detectedPeaks[static_cast<size_t>(i)] = {
            candidates[static_cast<size_t>(i)].bin,
            candidates[static_cast<size_t>(i)].hz,
            candidates[static_cast<size_t>(i)].mag
        };
    }
    detectedPeakCount = maxPeaks;
}

void Dsp::buildHarmonicClusters(const std::array<float, kBins>& analysisMag) {
    harmonicClusterCount = 0;
    std::array<bool, kMaxPeaks> peakUsed {};

    for (int i = 0; i < detectedPeakCount && harmonicClusterCount < kMaxClusters; ++i) {
        if (peakUsed[static_cast<size_t>(i)])
            continue;

        const int rootBin = detectedPeaks[static_cast<size_t>(i)].bin;
        const float rootHz = detectedPeaks[static_cast<size_t>(i)].hz;
        if (rootHz < 100.0f)
            continue;

        HarmonicCluster cluster {};
        cluster.active = true;
        cluster.rootBin = rootBin;
        cluster.rootHz = rootHz;
        cluster.memberBins[0] = rootBin;
        cluster.memberCount = 1;
        cluster.strength = detectedPeaks[static_cast<size_t>(i)].magnitude;

        for (int h = 2; h <= kClusterHarmonics && cluster.memberCount < kClusterHarmonics; ++h) {
            const float targetHz = rootHz * static_cast<float>(h);
            if (targetHz > 8000.0f)
                break;

            const float tolerance = std::max(50.0f, targetHz * 0.08f);
            int bestBin = -1;
            float bestMag = 0.0f;

            for (int k = 0; k < kBins; ++k) {
                const float hz = binToHz(k);
                if (std::abs(hz - targetHz) < tolerance) {
                    const float mag = analysisMag[static_cast<size_t>(k)];
                    if (mag > bestMag && mag > detectedPeaks[static_cast<size_t>(i)].magnitude * 0.3f) {
                        bestMag = mag;
                        bestBin = k;
                    }
                }
            }

            if (bestBin >= 0) {
                cluster.memberBins[static_cast<size_t>(cluster.memberCount)] = bestBin;
                cluster.strength += bestMag;
                ++cluster.memberCount;
            }
        }

        if (cluster.memberCount >= 2) {
            harmonicClusters[static_cast<size_t>(harmonicClusterCount)] = cluster;
            ++harmonicClusterCount;
        }
    }
}

void Dsp::analyseClusterSources(const std::array<float, kBins>& analysisMag,
                                const std::array<float, kBins>& centerWeight,
                                const std::array<float, kBins>& sideWeight,
                                const float transientPrior,
                                const float steadyPriorScale) {
    const auto& modeProfile = currentModeProfile();

    for (int c = 0; c < harmonicClusterCount; ++c) {
        auto& cluster = harmonicClusters[static_cast<size_t>(c)];
        std::fill(cluster.sourceScores.begin(), cluster.sourceScores.end(), 0.0f);

        for (int m = 0; m < cluster.memberCount; ++m) {
            const int k = cluster.memberBins[static_cast<size_t>(m)];
            const float hz = binToHz(k);
            const float centered = lerp(1.0f - 0.5f * sideWeight[static_cast<size_t>(k)],
                                        centerWeight[static_cast<size_t>(k)],
                                        modeProfile.stereoWidthTrust);
            const float wide = lerp(0.0f, sideWeight[static_cast<size_t>(k)], modeProfile.stereoWidthTrust);
            const float deltaNorm = std::abs(analysisMag[static_cast<size_t>(k)] - prevAnalysisMag[static_cast<size_t>(k)])
                / std::max({ kEps, analysisMag[static_cast<size_t>(k)], prevAnalysisMag[static_cast<size_t>(k)] });
            const float steadyPrior = (1.0f - clamp01(deltaNorm * 2.5f)) * steadyPriorScale;

            const float vocalWindow = modeAwareBandWeight(hz, modeProfile.vocals);
            const float drumWindow = modeAwareBandWeight(hz, modeProfile.drums);
            const float bassWindow = modeAwareBandWeight(hz, modeProfile.bass);
            const float guitarWindow = modeAwareBandWeight(hz, modeProfile.guitars);

            cluster.sourceScores[static_cast<size_t>(vocalsSource)] +=
                vocalWindow * (0.1f + 0.8f * centered + 0.3f * steadyPrior);
            cluster.sourceScores[static_cast<size_t>(drumsSource)] +=
                drumWindow * (0.1f + 0.7f * transientPrior + 0.2f * wide);
            cluster.sourceScores[static_cast<size_t>(bassSource)] +=
                bassWindow * (0.1f + 0.8f * centered + 0.4f * steadyPrior);
            cluster.sourceScores[static_cast<size_t>(guitarSource)] +=
                guitarWindow * (0.1f + 0.6f * steadyPrior + 0.3f * wide + 0.2f * (1.0f - centered));
            cluster.sourceScores[static_cast<size_t>(otherSource)] += 0.05f;
        }

        // Normalize source scores properly - sum to 1.0 for stable probability
        float scoreSum = kEps;
        for (int s = 0; s < kSourceCount; ++s)
            scoreSum += cluster.sourceScores[static_cast<size_t>(s)];
        
        for (int s = 0; s < kSourceCount; ++s)
            cluster.sourceScores[static_cast<size_t>(s)] /= scoreSum;

        cluster.strength /= static_cast<float>(cluster.memberCount);

        // Find dominant and second for confidence calculation
        float maxScore = 0.0f, secondScore = 0.0f;
        int dominant = otherSource;
        for (int s = 0; s < kSourceCount; ++s) {
            const float score = cluster.sourceScores[static_cast<size_t>(s)];
            if (score > maxScore) {
                secondScore = maxScore;
                maxScore = score;
                dominant = s;
            } else if (score > secondScore) {
                secondScore = score;
            }
        }
        cluster.dominantSource = dominant;
        cluster.confidence = maxScore > 0.0f ? (maxScore - secondScore) / maxScore : 0.0f;
        cluster.confidence = clamp01(cluster.confidence * 1.5f);
    }
}

float Dsp::vocalFormantSupport(float hz, float localMag, float envelopeMag) const noexcept {
    if (hz < 200.0f || hz > 4000.0f)
        return 0.0f;
    const float ratio = localMag / std::max(kEps, envelopeMag);
    if (hz >= 400.0f && hz <= 2500.0f) {
        if (ratio >= 0.9f && ratio <= 1.4f)
            return 0.8f + 0.2f * (1.0f - std::abs(1.15f - ratio) * 2.0f);
    }
    return 0.3f * (1.0f - std::min(1.0f, std::abs(ratio - 1.0f)));
}

float Dsp::guitarTonalSupport(float hz, float localMag, float envelopeMag,
                              float centered, float wide, float steadyPrior) const noexcept {
    if (hz < 80.0f || hz > 6000.0f)
        return 0.0f;
    const float ratio = localMag / std::max(kEps, envelopeMag);
    float support = 0.0f;
    if (ratio > 1.08f)
        support += 0.55f * std::min(1.0f, (ratio - 1.08f) * 2.4f);
    support += 0.3f * wide;
    support += 0.2f * (1.0f - centered);
    support += 0.3f * steadyPrior;
    if (hz >= 150.0f && hz <= 900.0f)
        support += 0.12f;
    return clamp01(support);
}

void Dsp::applySourcePersistence(std::array<std::array<float, kBins>, kSourceCount>& conditionedMasks, 
                                 const float transientPrior) noexcept {
    for (int k = 0; k < kBins; ++k) {
        float best = 0.0f, second = 0.0f;
        int winner = otherSource, runnerUp = otherSource;

        for (int s = 0; s < kSourceCount; ++s) {
            const float val = conditionedMasks[static_cast<size_t>(s)][static_cast<size_t>(k)];
            if (val > best) {
                second = best;
                runnerUp = winner;
                best = val;
                winner = s;
            } else if (val > second) {
                second = val;
                runnerUp = s;
            }
        }

        const int prevWinner = previousWinningSource[static_cast<size_t>(k)];
        const float prevConf = previousWinningConfidence[static_cast<size_t>(k)];
        const float margin = best - second;
        // Dynamic threshold: lower in steady regions, higher in transient regions
        const float switchThreshold = lerp(0.05f, 0.12f, transientPrior);

        if (winner == prevWinner) {
            sourcePersistence[static_cast<size_t>(k)] = std::min(1.0f, sourcePersistence[static_cast<size_t>(k)] + 0.08f);
        } else {
            const float decay =
                (prevWinner == guitarSource && transientPrior < 0.55f) ? 0.08f : 0.14f;
            sourcePersistence[static_cast<size_t>(k)] =
                std::max(0.0f, sourcePersistence[static_cast<size_t>(k)] - decay);
            if (margin < switchThreshold && prevConf > 0.3f) {
                const float persistenceStrength =
                    prevWinner == guitarSource ? 0.26f : 0.18f;
                conditionedMasks[static_cast<size_t>(prevWinner)][static_cast<size_t>(k)] *=
                    1.0f + persistenceStrength * sourcePersistence[static_cast<size_t>(k)];
                conditionedMasks[static_cast<size_t>(winner)][static_cast<size_t>(k)] *=
                    1.0f - 0.10f * sourcePersistence[static_cast<size_t>(k)];
            }
        }

        previousWinningSource[static_cast<size_t>(k)] = winner;
        previousWinningConfidence[static_cast<size_t>(k)] = clamp01(margin * 3.0f);
    }
}

// ============================================================================
// Phase 2: Object-Based DSP Separation Implementation
// ============================================================================

void Dsp::updateTrackedClusters(const std::array<float, kBins>& analysisMag,
                                 const std::array<float, kBins>& centerWeight,
                                 const std::array<float, kBins>& sideWeight) {
    const auto& modeProfile = currentModeProfile();

    // Age existing tracked clusters
    ageTrackedClusters();

    // Match each harmonic cluster to a tracked cluster
    for (int c = 0; c < harmonicClusterCount; ++c) {
        const auto& cluster = harmonicClusters[static_cast<size_t>(c)];
        if (!cluster.active || cluster.confidence < 0.15f)
            continue;

        const int matchIdx = findBestTrackedClusterMatch(cluster);

        if (matchIdx >= 0) {
            // Update existing tracked cluster
            auto& tracked = trackedClusters[static_cast<size_t>(matchIdx)];
            tracked.active = true;
            tracked.inactiveFrames = 0.0f;
            tracked.ageFrames += 1.0f;

            // Update lifecycle state based on age and continuity
            if (tracked.ageFrames < 3.0f)
            {
                tracked.lifecycleState = TrackedCluster::LifecycleState::newborn;
            }
            else if (tracked.inactiveFrames < 2.0f)
            {
                tracked.lifecycleState = TrackedCluster::LifecycleState::stable;
            }
            else
            {
                tracked.lifecycleState = TrackedCluster::LifecycleState::decaying;
            }

            // Smooth F0 estimate
            const float newF0 = estimateClusterF0Hz(cluster);
            const float f0Alpha = 0.3f;
            tracked.estimatedF0Hz = lerp(tracked.estimatedF0Hz, newF0, f0Alpha);

            // Update mean properties
            float meanMagAccum = 0.0f;
            float meanHzAccum = 0.0f;
            float centerAccum = 0.0f;
            float widthAccum = 0.0f;

            for (int m = 0; m < cluster.memberCount; ++m) {
                const int bin = cluster.memberBins[static_cast<size_t>(m)];
                const float hz = binToHz(bin);
                const float mag = analysisMag[static_cast<size_t>(bin)];
                const float centered = centerWeight[static_cast<size_t>(bin)];
                const float wide = sideWeight[static_cast<size_t>(bin)];

                meanMagAccum += mag;
                meanHzAccum += hz;
                centerAccum += centered;
                widthAccum += wide;
            }

            const float memberScale = 1.0f / static_cast<float>(cluster.memberCount);
            tracked.meanMagnitude = meanMagAccum * memberScale;
            tracked.meanHz = meanHzAccum * memberScale;
            tracked.stereoCenter = centerAccum * memberScale;
            tracked.stereoWidth = widthAccum * memberScale;

            // Update member bins
            tracked.lastMemberCount = cluster.memberCount;
            for (int m = 0; m < cluster.memberCount; ++m) {
                tracked.lastMemberBins[static_cast<size_t>(m)] = cluster.memberBins[static_cast<size_t>(m)];
            }

            // Smooth source probabilities
            for (int s = 0; s < kSourceCount; ++s) {
                const float targetProb = cluster.sourceScores[static_cast<size_t>(s)] /
                                         (cluster.strength + kEps);
                tracked.sourceProbabilities[static_cast<size_t>(s)] =
                    lerp(tracked.sourceProbabilities[static_cast<size_t>(s)],
                         clamp01(targetProb), 0.4f);
            }

            // Update dominant source
            float maxProb = 0.0f;
            int dominant = otherSource;
            for (int s = 0; s < kSourceCount; ++s) {
                if (tracked.sourceProbabilities[static_cast<size_t>(s)] > maxProb) {
                    maxProb = tracked.sourceProbabilities[static_cast<size_t>(s)];
                    dominant = s;
                }
            }
            tracked.dominantSource = dominant;

            // Update confidence
            tracked.confidence = clamp01(0.7f * cluster.confidence + 0.3f * tracked.confidence);

            // Onset-to-sustain decay: if we had an onset, gradually transition to sustain
            if (tracked.onsetStrength > 0.1f) {
                tracked.onsetStrength *= 0.85f; // Decay onset
                tracked.sustainStrength = std::min(1.0f, tracked.sustainStrength + 0.15f); // Build sustain
            } else {
                // Natural sustain decay
                tracked.sustainStrength *= 0.95f;
            }

        } else {
            // Create new tracked cluster
            for (int t = 0; t < kMaxTrackedClusters; ++t) {
                auto& tracked = trackedClusters[static_cast<size_t>(t)];
                if (!tracked.active || tracked.inactiveFrames > 10.0f) {
                    tracked.active = true;
                    tracked.id = nextTrackedClusterId++;
                    tracked.inactiveFrames = 0.0f;
                    tracked.ageFrames = 1.0f;
                    tracked.transientBoost = 0.0f;
                    tracked.transientBoostFrames = 0;
                    tracked.lifecycleState = TrackedCluster::LifecycleState::newborn;
                    tracked.estimatedF0Hz = estimateClusterF0Hz(cluster);
                    tracked.meanHz = cluster.rootHz;
                    tracked.meanMagnitude = cluster.strength;
                    tracked.stereoCenter = 0.5f;
                    tracked.stereoWidth = 0.3f;
                    tracked.onsetStrength = 0.0f;
                    tracked.sustainStrength = 1.0f;
                    tracked.dominantSource = cluster.dominantSource;

                    for (int s = 0; s < kSourceCount; ++s) {
                        tracked.sourceProbabilities[static_cast<size_t>(s)] =
                            clamp01(cluster.sourceScores[static_cast<size_t>(s)] /
                                   (cluster.strength + kEps));
                    }

                    tracked.confidence = cluster.confidence * 0.7f;
                    tracked.lastMemberCount = cluster.memberCount;
                    for (int m = 0; m < cluster.memberCount; ++m) {
                        tracked.lastMemberBins[static_cast<size_t>(m)] = cluster.memberBins[static_cast<size_t>(m)];
                    }
                    break;
                }
            }
        }
    }
    
    // Also check for transient events that should influence clusters without harmonic matches
    // This ensures drum transients and other non-harmonic events get tracked
    for (int e = 0; e < kMaxTransientEvents; ++e) {
        const auto& event = transientEvents[static_cast<size_t>(e)];
        if (!event.active || event.magnitude < 0.05f)
            continue;
        
        // Find or create a tracked cluster for this transient
        int bestMatch = -1;
        float bestScore = 0.0f;
        
        for (int t = 0; t < kMaxTrackedClusters; ++t) {
            const auto& tracked = trackedClusters[static_cast<size_t>(t)];
            if (!tracked.active)
                continue;
            
            const float hzDiff = std::abs(event.peakHz - tracked.meanHz);
            const float hzScore = 1.0f / (1.0f + hzDiff / 300.0f);
            
            if (hzScore > bestScore && hzScore > 0.5f) {
                bestScore = hzScore;
                bestMatch = t;
            }
        }
        
        if (bestMatch >= 0) {
            auto& tracked = trackedClusters[static_cast<size_t>(bestMatch)];
            // Inject transient energy
            tracked.onsetStrength = std::min(1.0f, tracked.onsetStrength + event.magnitude * 0.5f);
        }
    }
}

float Dsp::estimateClusterF0Hz(const HarmonicCluster& cluster) const noexcept {
    if (cluster.memberCount < 2 || cluster.rootHz < 50.0f)
        return cluster.rootHz;
    
    // Simple f0 estimation based on harmonic spacing
    // Assume harmonics are roughly at integer multiples of f0
    float f0Estimates[kClusterHarmonics] {};
    int estimateCount = 0;
    
    for (int h = 1; h < cluster.memberCount && h < kClusterHarmonics; ++h) {
        const float harmonicHz = binToHz(cluster.memberBins[static_cast<size_t>(h)]);
        const float rootHz = cluster.rootHz;
        
        // Estimate f0 from harmonic ratio
        const float f0FromHarmonic = harmonicHz / static_cast<float>(h + 1);
        if (f0FromHarmonic > 50.0f && f0FromHarmonic < 2000.0f) {
            f0Estimates[estimateCount++] = f0FromHarmonic;
        }
    }
    
    if (estimateCount == 0)
        return cluster.rootHz;
    
    // Average the estimates
    float sum = 0.0f;
    for (int i = 0; i < estimateCount; ++i)
        sum += f0Estimates[static_cast<size_t>(i)];
    
    return sum / static_cast<float>(estimateCount);
}

int Dsp::findBestTrackedClusterMatch(const HarmonicCluster& cluster) const noexcept {
    int bestMatch = -1;
    float bestScore = 0.0f;
    
    const float f0Tolerance = 0.15f; // 15% F0 tolerance
    const float binTolerance = 3; // bins
    const float magTolerance = 0.5f; // 50% magnitude ratio
    
    for (int t = 0; t < kMaxTrackedClusters; ++t) {
        const auto& tracked = trackedClusters[static_cast<size_t>(t)];
        if (!tracked.active || tracked.inactiveFrames > 5.0f)
            continue;
        
        float score = 0.0f;
        
        // F0 similarity
        if (tracked.estimatedF0Hz > 0.0f && cluster.rootHz > 0.0f) {
            const float f0Ratio = std::min(tracked.estimatedF0Hz, cluster.rootHz) / 
                                  std::max(tracked.estimatedF0Hz, cluster.rootHz);
            if (f0Ratio > (1.0f - f0Tolerance)) {
                score += 0.4f * f0Ratio;
            }
        }
        
        // Mean Hz similarity
        const float hzRatio = std::min(tracked.meanHz, cluster.rootHz) / 
                              std::max(tracked.meanHz, cluster.rootHz);
        if (hzRatio > 0.7f) {
            score += 0.3f * hzRatio;
        }
        
        // Stereo similarity
        const float stereoDiff = std::abs(tracked.stereoCenter - 0.5f) + 
                                 std::abs(tracked.stereoWidth - 0.3f);
        score += 0.15f * (1.0f - stereoDiff);
        
        // Magnitude similarity
        const float magRatio = std::min(tracked.meanMagnitude, cluster.strength) / 
                               std::max(tracked.meanMagnitude, cluster.strength);
        if (magRatio > (1.0f - magTolerance)) {
            score += 0.15f * magRatio;
        }
        
        // Member bin overlap
        int overlapCount = 0;
        for (int m = 0; m < cluster.memberCount; ++m) {
            const int bin = cluster.memberBins[static_cast<size_t>(m)];
            for (int lm = 0; lm < tracked.lastMemberCount; ++lm) {
                if (std::abs(bin - tracked.lastMemberBins[static_cast<size_t>(lm)]) <= binTolerance) {
                    ++overlapCount;
                    break;
                }
            }
        }
        score += 0.2f * static_cast<float>(overlapCount) / 
                 std::max(1, std::max(cluster.memberCount, tracked.lastMemberCount));
        
        if (score > bestScore && score > 0.35f) {
            bestScore = score;
            bestMatch = t;
        }
    }
    
    return bestMatch;
}

void Dsp::ageTrackedClusters() noexcept {
    for (int t = 0; t < kMaxTrackedClusters; ++t) {
        auto& tracked = trackedClusters[static_cast<size_t>(t)];
        if (tracked.active) {
            tracked.inactiveFrames += 1.0f;
            
            // Decay confidence when inactive
            if (tracked.inactiveFrames > 3.0f) {
                tracked.confidence *= 0.85f;
            }
            
            // Deactivate if inactive too long
            if (tracked.inactiveFrames > 15.0f || tracked.confidence < 0.05f) {
                tracked.active = false;
                tracked.inactiveFrames = 0.0f;
                tracked.ageFrames = 0.0f;
            }
        }
    }
}

void Dsp::detectTransientEvents(const std::array<float, kBins>& analysisMag,
                                 const std::array<float, kBins>& centerWeight,
                                 const std::array<float, kBins>& sideWeight,
                                 const float transientPrior) {
    // Compute spectral flux
    float fluxAccum = 0.0f;
    for (int k = 0; k < kBins; ++k) {
        const float delta = analysisMag[static_cast<size_t>(k)] - prevAnalysisMag[static_cast<size_t>(k)];
        spectralFlux[static_cast<size_t>(k)] = std::max(0.0f, delta);
        fluxAccum += spectralFlux[static_cast<size_t>(k)];
    }

    // Detect transient events based on flux peaks
    const float fluxThreshold = transientPrior * 0.3f;

    if (transientPrior > 0.4f) {
        // Look for new transient events
        for (int t = 0; t < kMaxTransientEvents; ++t) {
            auto& event = transientEvents[static_cast<size_t>(t)];
            if (!event.active) {
                // Find peak flux bin
                int peakBin = 0;
                float peakFlux = 0.0f;
                for (int k = 0; k < kBins; ++k) {
                    if (spectralFlux[static_cast<size_t>(k)] > peakFlux) {
                        peakFlux = spectralFlux[static_cast<size_t>(k)];
                        peakBin = k;
                    }
                }

                if (peakFlux > fluxThreshold) {
                    event.active = true;
                    event.id = nextTransientEventId++;
                    event.linkedClusterTrackId = -1;
                    event.birthFrame = currentFrameCount;
                    event.peakHz = binToHz(peakBin);
                    event.magnitude = analysisMag[static_cast<size_t>(peakBin)];
                    event.stereoWidth = sideWeight[static_cast<size_t>(peakBin)];

                    // Compute bandwidth
                    float bandwidthAccum = 0.0f;
                    int bandwidthCount = 0;
                    for (int k = 0; k < kBins; ++k) {
                        if (spectralFlux[static_cast<size_t>(k)] > fluxThreshold * 0.5f) {
                            bandwidthAccum += std::abs(binToHz(k) - event.peakHz);
                            ++bandwidthCount;
                        }
                    }
                    event.bandwidthHz = bandwidthCount > 0 ?
                        bandwidthAccum / static_cast<float>(bandwidthCount) : 500.0f;

                    // Compute source probabilities
                    const float centered = centerWeight[static_cast<size_t>(peakBin)];
                    const float wide = sideWeight[static_cast<size_t>(peakBin)];

                    // Drum probability: broadband, transient-heavy
                    event.drumProbability = clamp01(0.5f + 0.3f * transientPrior +
                                                    0.2f * wide - 0.1f * centered);

                    // Guitar probability: pick attack
                    event.guitarProbability = clamp01(0.3f + 0.2f * (1.0f - centered) +
                                                      0.1f * wide);

                    // Vocal probability: consonant edge
                    event.vocalProbability = clamp01(0.2f + 0.3f * centered -
                                                     0.2f * wide);

                    // Link transient to nearest tracked cluster for attack-sustain continuity
                    // This is critical for making objects feel coherent
                    int bestCluster = -1;
                    float bestScore = 0.0f;
                    
                    for (int tc = 0; tc < kMaxTrackedClusters; ++tc) {
                        const auto& tracked = trackedClusters[static_cast<size_t>(tc)];
                        if (!tracked.active || tracked.inactiveFrames > 3.0f)
                            continue;
                        
                        // Frequency proximity
                        const float hzDiff = std::abs(event.peakHz - tracked.meanHz);
                        const float hzScore = 1.0f / (1.0f + hzDiff / 200.0f);
                        
                        // Source compatibility
                        float sourceCompat = 0.0f;
                        if (event.drumProbability > 0.5f && tracked.dominantSource == drumsSource)
                            sourceCompat = 0.8f;
                        else if (event.guitarProbability > 0.4f && tracked.dominantSource == guitarSource)
                            sourceCompat = 0.7f;
                        else if (event.vocalProbability > 0.4f && tracked.dominantSource == vocalsSource)
                            sourceCompat = 0.7f;
                        
                        const float score = hzScore * (0.6f + 0.4f * sourceCompat);
                        
                        if (score > bestScore) {
                            bestScore = score;
                            bestCluster = tc;
                        }
                    }
                    
                    // Inject onset evidence into matched cluster
                    if (bestCluster >= 0 && bestScore > 0.4f) {
                        auto& matched = trackedClusters[static_cast<size_t>(bestCluster)];
                        event.linkedClusterTrackId = matched.id;
                        matched.onsetStrength = std::min(1.0f, matched.onsetStrength + 0.8f * bestScore);
                        matched.sustainStrength = 0.3f; // Reset sustain, will rebuild
                        matched.confidence = std::min(1.0f, matched.confidence + 0.15f * bestScore);
                        matched.transientBoost = 1.0f;
                        matched.transientBoostFrames = 4;
                    }
                    
                    break;
                }
            }
        }
    }
}

void Dsp::updateTransientEvents() noexcept {
    const int maxLifetimeFrames = 6; // ~25ms at 48kHz/256 hop
    
    for (int t = 0; t < kMaxTransientEvents; ++t) {
        auto& event = transientEvents[static_cast<size_t>(t)];
        if (event.active) {
            const int age = currentFrameCount - event.birthFrame;

            if (event.linkedClusterTrackId >= 0 && event.magnitude > 0.02f) {
                for (int tc = 0; tc < kMaxTrackedClusters; ++tc) {
                    auto& tracked = trackedClusters[static_cast<size_t>(tc)];
                    if (!tracked.active || tracked.id != event.linkedClusterTrackId)
                        continue;

                    tracked.onsetStrength = std::min(1.0f, tracked.onsetStrength + event.magnitude * 0.12f);
                    tracked.transientBoost = std::max(tracked.transientBoost, event.magnitude * 0.6f);
                    tracked.transientBoostFrames = std::max(tracked.transientBoostFrames, 1);
                    break;
                }
            }
            
            // Decay magnitude
            event.magnitude *= 0.7f;
            
            // Deactivate if too old
            if (age > maxLifetimeFrames || event.magnitude < 0.01f) {
                event.active = false;
                event.linkedClusterTrackId = -1;
            }
        }
    }
}

void Dsp::updateObjectSourceProbabilities(const std::array<float, kBins>& analysisMag,
                                           const std::array<float, kBins>& centerWeight,
                                           const std::array<float, kBins>& sideWeight,
                                           const float transientPrior,
                                           const float steadyPriorScale) {
    const auto& modeProfile = currentModeProfile();
    const auto recordingType = static_cast<RecordingType>(juce::jlimit(
        0,
        static_cast<int>(kModeProfiles.size()) - 1,
        targetRecordingType.load(std::memory_order_relaxed)));
    
    // Update tracked cluster source probabilities using object-level logic
    for (int t = 0; t < kMaxTrackedClusters; ++t) {
        auto& tracked = trackedClusters[static_cast<size_t>(t)];
        if (!tracked.active)
            continue;
        
        // Vocal probability: center bias, formant support, moderate sustain
        const float vocalCenterBias = modeProfile.vocalCenterBias * tracked.stereoCenter;
        const float vocalSustain = tracked.sustainStrength * 0.3f;
        tracked.sourceProbabilities[static_cast<size_t>(vocalsSource)] = 
            clamp01(0.3f + vocalCenterBias + vocalSustain - 0.2f * tracked.stereoWidth);
        
        // Guitar probability: harmonic body, pick attack history, reduced center lock
        const float guitarWidth = tracked.stereoWidth * 0.45f;
        const float guitarReducedCenter = (1.0f - tracked.stereoCenter) * 0.18f;
        const float guitarSustain = tracked.sustainStrength * 0.30f;
        tracked.sourceProbabilities[static_cast<size_t>(guitarSource)] = 
            clamp01(0.22f + guitarWidth + guitarReducedCenter + guitarSustain + tracked.onsetStrength * 0.15f);
        if (recordingType == RecordingType::live) {
            tracked.sourceProbabilities[static_cast<size_t>(guitarSource)] = clamp01(
                tracked.sourceProbabilities[static_cast<size_t>(guitarSource)]
                + tracked.sustainStrength * 0.12f
                + tracked.stereoWidth * 0.08f);
        }
        
        // Bass probability: low f0, stable sustain, low width
        const float bassLowF0 = tracked.estimatedF0Hz < 150.0f ? 
            (1.0f - tracked.estimatedF0Hz / 150.0f) : 0.0f;
        const float bassStable = tracked.sustainStrength * 0.3f;
        tracked.sourceProbabilities[static_cast<size_t>(bassSource)] = 
            clamp01(0.2f + bassLowF0 * 0.5f + bassStable - tracked.stereoWidth * 0.3f);
        
        // Drum probability: strong onset, broadband, poor harmonic continuity
        const float drumOnset = tracked.onsetStrength * 0.9f;
        const float drumBroadband = tracked.stereoWidth * 0.25f;
        tracked.sourceProbabilities[static_cast<size_t>(drumsSource)] = 
            clamp01(0.18f + drumOnset + drumBroadband - tracked.sustainStrength * 0.12f);
        if (transientPrior > 0.45f) {
            tracked.sourceProbabilities[static_cast<size_t>(drumsSource)] = clamp01(
                tracked.sourceProbabilities[static_cast<size_t>(drumsSource)] + 0.12f * transientPrior);
        }
        
        // Other: residual when no source ownership is strong
        float maxSourceProb = 0.0f;
        for (int s = 0; s < kSourceCount - 1; ++s) {
            maxSourceProb = std::max(maxSourceProb, 
                                     tracked.sourceProbabilities[static_cast<size_t>(s)]);
        }
        tracked.sourceProbabilities[static_cast<size_t>(otherSource)] = 
            clamp01(0.1f + (1.0f - maxSourceProb) * 0.3f);
        
        // Update dominant source
        float maxProb = 0.0f;
        int dominant = otherSource;
        for (int s = 0; s < kSourceCount; ++s) {
            if (tracked.sourceProbabilities[static_cast<size_t>(s)] > maxProb) {
                maxProb = tracked.sourceProbabilities[static_cast<size_t>(s)];
                dominant = s;
            }
        }
        tracked.dominantSource = dominant;

        if (tracked.transientBoostFrames > 0) {
            const float boost = tracked.transientBoost;
            tracked.onsetStrength = clamp01(tracked.onsetStrength + boost * 0.5f);
            tracked.sourceProbabilities[static_cast<size_t>(tracked.dominantSource)] *= (1.0f + 0.5f * boost);
            tracked.sourceProbabilities[static_cast<size_t>(drumsSource)] *= (1.0f + 0.7f * boost);
            tracked.transientBoost *= 0.6f;
            --tracked.transientBoostFrames;
        }
    }
}

void Dsp::writeObjectOwnershipToBins() noexcept {
    // Initialize ownership arrays
    std::fill(binOwningCluster.begin(), binOwningCluster.end(), -1);
    std::fill(binOwnershipConfidence.begin(), binOwnershipConfidence.end(), 0.0f);

    // Write tracked cluster ownership to bins
    for (int t = 0; t < kMaxTrackedClusters; ++t) {
        const auto& tracked = trackedClusters[static_cast<size_t>(t)];
        if (!tracked.active || tracked.confidence < 0.2f)
            continue;

        for (int m = 0; m < tracked.lastMemberCount; ++m) {
            const int bin = tracked.lastMemberBins[static_cast<size_t>(m)];
            if (bin >= 0 && bin < kBins) {
                const int existingCluster = binOwningCluster[static_cast<size_t>(bin)];

                // Take ownership if higher confidence
                if (existingCluster < 0 ||
                    tracked.confidence > binOwnershipConfidence[static_cast<size_t>(bin)]) {
                    binOwningCluster[static_cast<size_t>(bin)] = t;
                    binOwnershipConfidence[static_cast<size_t>(bin)] = tracked.confidence;
                }
            }
        }
    }
}

void Dsp::applyObjectOwnershipToMasks(
    std::array<std::array<float, kBins>, kSourceCount>& masks) noexcept
{
    for (int k = 0; k < kBins; ++k)
    {
        const int clusterId = binOwningCluster[static_cast<size_t>(k)];
        const float confidence = binOwnershipConfidence[static_cast<size_t>(k)];

        if (clusterId < 0 || clusterId >= kMaxTrackedClusters)
            continue;

        if (confidence < 0.30f)
            continue;

        const auto& tracked = trackedClusters[static_cast<size_t>(clusterId)];
        if (!tracked.active)
            continue;

        const int dominant = tracked.dominantSource;
        const float agedConfidence =
            confidence * (1.0f - std::exp(-tracked.ageFrames * 0.1f));

        float lifecycleScale = 1.0f;
        switch (tracked.lifecycleState)
        {
            case TrackedCluster::LifecycleState::newborn:
                lifecycleScale = 0.6f;
                break;
            case TrackedCluster::LifecycleState::stable:
                lifecycleScale = 1.0f;
                break;
            case TrackedCluster::LifecycleState::decaying:
                lifecycleScale = 0.5f;
                break;
        }

        const float push = lifecycleScale * 0.8f * agedConfidence;

        for (int s = 0; s < kSourceCount; ++s)
        {
            if (s == dominant)
            {
                masks[static_cast<size_t>(s)][static_cast<size_t>(k)] =
                    lerp(masks[static_cast<size_t>(s)][static_cast<size_t>(k)], 1.0f, push);
            }
            else
            {
                masks[static_cast<size_t>(s)][static_cast<size_t>(k)] *= (1.0f - push);
            }
        }

        if (dominant != otherSource && agedConfidence > 0.6f)
        {
            masks[static_cast<size_t>(otherSource)][static_cast<size_t>(k)] *= 0.3f;
        }
    }
}

float Dsp::computeSourceContributionMultiplier(
    int source,
    float sliderNormalized,
    float strength) const noexcept
{
    const float sliderSigned = (sliderNormalized - 0.5f) * 2.0f;
    const float curved =
        std::copysign(std::pow(std::abs(sliderSigned), 1.8f), sliderSigned);
    const float maxContribution = source == otherSource ? 1.25f : 2.0f;
    return juce::jlimit(0.0f, maxContribution, 1.0f + curved * strength);
}


void Dsp::buildForegroundBackgroundRender() noexcept
{
    const float strength = currentControlValues[static_cast<size_t>(kStrengthIndex)];
    const auto mode = static_cast<RecordingType>(
        juce::jlimit(0, static_cast<int>(kModeProfiles.size()) - 1,
                     targetRecordingType.load(std::memory_order_relaxed)));
    int activeSources = 0;
    int activeSourceIndex = -1;
    int suppressedSources = 0;

    for (int s = 0; s < kSourceCount; ++s) {
        const float sliderSigned =
            (currentControlValues[static_cast<size_t>(s)] - 0.5f) * 2.0f;
        if (sliderSigned > 0.10f) {
            ++activeSources;
            activeSourceIndex = s;
        }
        if (sliderSigned < -0.80f)
            ++suppressedSources;
    }

    const bool nearIsolation = activeSources <= 1 && suppressedSources >= 3;
    std::array<int, kDebugBins> debugDominant {};
    std::array<float, kDebugBins> debugBestConfidence {};
    std::array<float, kDebugBins> debugDominantMask {};
    std::array<float, kDebugBins> debugOtherMask {};
    std::array<float, kSourceCount> debugCoverage {};
    float debugConfidenceSum = 0.0f;
    for (auto& value : debugDominant)
        value = otherSource;

    for (int k = 0; k < kBins; ++k)
    {
        std::array<float, kSourceCount> separationForces {};
        std::array<float, kSourceCount> ownership {};
        std::array<float, kSourceCount> contributions {};
        const int clusterId = binOwningCluster[static_cast<size_t>(k)];
        const auto* tracked = (clusterId >= 0 && clusterId < kMaxTrackedClusters)
            ? &trackedClusters[static_cast<size_t>(clusterId)]
            : nullptr;

        for (int s = 0; s < kSourceCount; ++s) {
            const float slider = currentControlValues[static_cast<size_t>(s)];
            const float sliderSigned = (slider - 0.5f) * 2.0f;
            const float curved =
                std::copysign(std::pow(std::abs(sliderSigned), 1.8f), sliderSigned);
            const float separation = std::abs(curved) * strength;
            const float contribution = computeSourceContributionMultiplier(
                s,
                slider,
                strength);

            separationForces[static_cast<size_t>(s)] = separation;
            contributions[static_cast<size_t>(s)] = contribution;

            float ownershipBias = 1.0f;
            if (sliderSigned > 0.0f)
                ownershipBias += 1.20f * separation;
            else if (sliderSigned < 0.0f)
                ownershipBias *= std::max(0.80f, 1.0f - 0.20f * separation);

            ownershipBias *= std::sqrt(std::max(0.04f, contribution));

            if (s == otherSource)
                ownershipBias = juce::jlimit(0.10f, 1.12f, ownershipBias);
            else
                ownershipBias = juce::jlimit(0.18f, 2.0f, ownershipBias);

            ownership[static_cast<size_t>(s)] =
                smoothedMasks[static_cast<size_t>(s)][static_cast<size_t>(k)] * ownershipBias;

            if (tracked != nullptr && tracked->active) {
                ownership[static_cast<size_t>(s)] *= lerp(
                    0.35f,
                    1.35f,
                    clamp01(tracked->sourceProbabilities[static_cast<size_t>(s)]));
                if (s == tracked->dominantSource)
                    ownership[static_cast<size_t>(s)] *=
                        lerp(1.0f, 1.25f, binOwnershipConfidence[static_cast<size_t>(k)]);
            }

            if (mode == RecordingType::live) {
                const float hz = binToHz(k);
                if (s == guitarSource && hz >= 300.0f && hz <= 2400.0f)
                    ownership[static_cast<size_t>(s)] *= (hz <= 1800.0f ? 1.38f : 1.20f);
                else if (s == otherSource && hz >= 300.0f && hz <= 2400.0f)
                    ownership[static_cast<size_t>(s)] *= (hz <= 1800.0f ? 0.52f : 0.70f);
            }
            if (s == drumsSource && tracked != nullptr && tracked->active) {
                const float hz = binToHz(k);
                if (hz >= 1400.0f && hz <= 12000.0f)
                    ownership[static_cast<size_t>(s)] *= lerp(1.0f, 1.22f, clamp01(tracked->onsetStrength));
            }
        }

        float best = 0.0f;
        float second = 0.0f;
        int dominant = otherSource;
        int runnerUp = otherSource;

        for (int s = 0; s < kSourceCount; ++s)
        {
            const float score = ownership[static_cast<size_t>(s)];
            if (score > best)
            {
                second = best;
                runnerUp = dominant;
                best = score;
                dominant = s;
            }
            else if (score > second)
            {
                second = score;
                runnerUp = s;
            }
        }

        const float fastConfidence =
            juce::jlimit(0.0f, 1.0f, (best - second) * 2.5f);

        float slowConfidence = 0.0f;
        if (tracked != nullptr)
        {
            if (tracked->active)
            {
                const float ageFactor =
                    juce::jlimit(0.0f, 1.0f, tracked->ageFrames * 0.08f);
                slowConfidence =
                    binOwnershipConfidence[static_cast<size_t>(k)] * ageFactor;
            }
        }

        const float confidence = juce::jlimit(0.0f, 1.0f,
            0.70f * fastConfidence + 0.30f * slowConfidence);

        std::array<float, kSourceCount> renderWeights = ownership;
        float renderTotal = 0.0f;
        for (float value : renderWeights)
            renderTotal += value;
        renderTotal = std::max(kEps, renderTotal);
        for (float& value : renderWeights)
            value /= renderTotal;

        const float dominantSeparation = separationForces[static_cast<size_t>(dominant)];
        if (nearIsolation && confidence > 0.5f)
        {
            for (int s = 0; s < kSourceCount; ++s) {
                if (s == dominant)
                    renderWeights[static_cast<size_t>(s)] = 1.0f;
                else
                    renderWeights[static_cast<size_t>(s)] *= 0.02f;
            }
        }
        else if (confidence >= 0.6f)
        {
            for (int s = 0; s < kSourceCount; ++s) {
                if (s == dominant)
                    renderWeights[static_cast<size_t>(s)] *= (1.0f + 1.35f * dominantSeparation);
                else if (s == runnerUp)
                    renderWeights[static_cast<size_t>(s)] *=
                        std::max(0.01f, 1.0f - dominantSeparation * 1.10f);
                else
                    renderWeights[static_cast<size_t>(s)] *= 0.01f;
            }

            if (confidence >= 0.82f) {
                for (int s = 0; s < kSourceCount; ++s)
                    renderWeights[static_cast<size_t>(s)] = s == dominant ? 1.0f : 0.005f;
            }
        }
        else if (confidence >= 0.3f)
        {
            for (int s = 0; s < kSourceCount; ++s) {
                if (s == dominant)
                    renderWeights[static_cast<size_t>(s)] *=
                        (1.0f + 1.05f * dominantSeparation);
                else if (s == runnerUp)
                    renderWeights[static_cast<size_t>(s)] *=
                        std::max(0.06f, 1.0f - 0.75f * dominantSeparation);
                else
                    renderWeights[static_cast<size_t>(s)] *=
                        std::max(0.06f, 1.0f - 0.95f * dominantSeparation);
            }
        }

        for (int s = 0; s < kSourceCount; ++s) {
            const float sliderSigned =
                (currentControlValues[static_cast<size_t>(s)] - 0.5f) * 2.0f;
            if (s == otherSource) {
                if (confidence > 0.6f)
                    renderWeights[static_cast<size_t>(s)] *= 0.25f;
                else if (confidence < 0.3f)
                    renderWeights[static_cast<size_t>(s)] *= 1.1f;
            }

            if (sliderSigned < -0.10f && s == dominant && confidence >= 0.3f)
                renderWeights[static_cast<size_t>(s)] *=
                    (1.0f + 0.45f * separationForces[static_cast<size_t>(s)]);

            if (nearIsolation && activeSourceIndex >= 0 && s != activeSourceIndex)
                renderWeights[static_cast<size_t>(s)] *= 0.02f;
        }

        renderTotal = 0.0f;
        for (float value : renderWeights)
            renderTotal += value;
        renderTotal = std::max(kEps, renderTotal);

        const int renderDominant =
            (nearIsolation && activeSourceIndex >= 0 && confidence > 0.5f)
                ? activeSourceIndex
                : dominant;

        float irmGain = 0.0f;
        for (int s = 0; s < kSourceCount; ++s) {
            const float renderMask = renderWeights[static_cast<size_t>(s)] / renderTotal;
            irmGain += renderMask * contributions[static_cast<size_t>(s)];
        }

        const float dominantMask =
            renderWeights[static_cast<size_t>(renderDominant)] / renderTotal;
        const float otherMask =
            renderWeights[static_cast<size_t>(otherSource)] / renderTotal;
        const int debugIndex =
            juce::jlimit(0, kDebugBins - 1, (k * kDebugBins) / kBins);
        if (confidence >= debugBestConfidence[static_cast<size_t>(debugIndex)]) {
            debugBestConfidence[static_cast<size_t>(debugIndex)] = confidence;
            debugDominant[static_cast<size_t>(debugIndex)] = renderDominant;
            debugDominantMask[static_cast<size_t>(debugIndex)] = dominantMask;
            debugOtherMask[static_cast<size_t>(debugIndex)] = otherMask;
        }
        debugCoverage[static_cast<size_t>(renderDominant)] += confidence;
        debugConfidenceSum += confidence;

        float residualEnergy = 0.0f;
        for (int s = 0; s < kSourceCount; ++s) {
            if (s == renderDominant)
                continue;

            const float renderMask = renderWeights[static_cast<size_t>(s)] / renderTotal;
            residualEnergy += renderMask * std::min(contributions[static_cast<size_t>(s)], 1.0f);
        }

        const float residualScale = nearIsolation
            ? 0.01f
            : (confidence >= 0.6f ? 0.02f : 0.08f);
        const float ibmGain =
            contributions[static_cast<size_t>(renderDominant)] + residualEnergy * residualScale;

        float total = irmGain;
        if (confidence < 0.3f) {
            total = irmGain;
        } else if (confidence < 0.6f) {
            const float hybrid = (confidence - 0.3f) / 0.3f;
            total = lerp(irmGain, ibmGain, hybrid);
        } else {
            total = ibmGain;
        }

        const float prev = prevCompositeGain[static_cast<size_t>(k)];
        compositeGain[static_cast<size_t>(k)] =
            lerp(prev, juce::jlimit(0.0f, 8.0f, total), 0.7f);
        prevCompositeGain[static_cast<size_t>(k)] = compositeGain[static_cast<size_t>(k)];
    }

    const float coverageSum = std::max(kEps,
        debugCoverage[0] + debugCoverage[1] + debugCoverage[2] + debugCoverage[3] + debugCoverage[4]);
    for (int i = 0; i < kDebugBins; ++i) {
        debugDominantSources[static_cast<size_t>(i)].store(
            debugDominant[static_cast<size_t>(i)], std::memory_order_relaxed);
        debugConfidence[static_cast<size_t>(i)].store(
            debugBestConfidence[static_cast<size_t>(i)], std::memory_order_relaxed);
        debugDominantMasks[static_cast<size_t>(i)].store(
            debugDominantMask[static_cast<size_t>(i)], std::memory_order_relaxed);
        debugOtherMasks[static_cast<size_t>(i)].store(
            debugOtherMask[static_cast<size_t>(i)], std::memory_order_relaxed);
    }
    for (int s = 0; s < kSourceCount; ++s) {
        debugDominantCoverage[static_cast<size_t>(s)].store(
            debugCoverage[static_cast<size_t>(s)] / coverageSum, std::memory_order_relaxed);
    }
    debugOverallConfidence.store(debugConfidenceSum / static_cast<float>(kBins), std::memory_order_relaxed);
    debugFrameCounter.store(currentFrameCount, std::memory_order_relaxed);
}

} // namespace vxsuite::rebalance
