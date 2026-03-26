#include "VxRebalanceDsp.h"

#include <algorithm>
#include <cmath>

namespace vxsuite::rebalance {

namespace {
constexpr float kEps = 1.0e-9f;
constexpr float kMaxBoostDb         = 12.0f;  // maximum boost applied to any source
constexpr float kMaxCutDb           = 18.0f;  // keep cuts near the UI contract instead of hard-nulling bins
constexpr int kStrengthIndex = 5;

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
        // vocals: core 500-1800, shoulders, upper-mid 1800-3500, sibilance
        makeProfile(makeRegion(500.0f, 1800.0f, 1.00f),
                    makeRegion(220.0f, 500.0f,  0.34f),
                    makeRegion(1800.0f, 3500.0f, 0.32f),
                    makeRegion(3500.0f, 6000.0f, 0.16f)),
        // drums: kick/snare body, transient crack, hi-hat
        makeProfile(makeRegion(60.0f, 180.0f,   1.00f),
                    makeRegion(180.0f, 280.0f,  0.54f),
                    makeRegion(280.0f, 400.0f,  0.10f)),
        // bass: low body, sub, harmonics
        makeProfile(makeRegion(40.0f, 120.0f,   0.95f),
                    makeRegion(4000.0f, 10000.0f, 1.00f),
                    makeRegion(120.0f, 350.0f,  0.42f),
                    makeRegion(2500.0f, 4000.0f, 0.18f)),
        // guitar: core mid, body, upper-mid, presence — proven profile
        makeProfile(makeRegion(350.0f, 1200.0f, 1.00f),
                    makeRegion(150.0f, 350.0f,  0.48f),
                    makeRegion(1200.0f, 2500.0f, 0.46f),
                    makeRegion(2500.0f, 4000.0f, 0.16f)),
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
                    makeRegion(3000.0f, 5000.0f, 0.14f)),
        makeProfile(makeRegion(55.0f, 170.0f,   1.00f),
                    makeRegion(170.0f, 260.0f,  0.50f),
                    makeRegion(260.0f, 350.0f,  0.10f)),
        makeProfile(makeRegion(45.0f, 130.0f,   0.95f),
                    makeRegion(4500.0f, 11000.0f, 1.00f),
                    makeRegion(130.0f, 400.0f,  0.44f),
                    makeRegion(2500.0f, 4500.0f, 0.18f)),
        makeProfile(makeRegion(300.0f, 1100.0f, 1.00f),
                    makeRegion(160.0f, 300.0f,  0.46f),
                    makeRegion(1100.0f, 2200.0f, 0.42f),
                    makeRegion(2200.0f, 3500.0f, 0.14f)),
        makeProfile(makeRegion(250.0f, 2000.0f, 0.48f),
                    makeRegion(2000.0f, 6000.0f, 0.24f),
                    makeRegion(20.0f, 20000.0f, 0.04f)),
        0.22f, 35.0f, 140.0f, 0.78f, 1.00f, 0.32f, 0.55f, 0.35f, 0.82f, 0.14f, 0.80f, 1.00f, 0.36f, 45.0f, 95.0f
    },
    // ---- Phone / Rough ----
    {
        makeProfile(makeRegion(350.0f, 1200.0f, 1.00f),
                    makeRegion(1200.0f, 2500.0f, 0.28f),
                    makeRegion(220.0f, 350.0f,  0.26f),
                    makeRegion(2500.0f, 4000.0f, 0.12f)),
        makeProfile(makeRegion(90.0f, 180.0f,   1.00f),
                    makeRegion(180.0f, 250.0f,  0.46f),
                    makeRegion(250.0f, 350.0f,  0.08f)),
        makeProfile(makeRegion(70.0f, 120.0f,   0.95f),
                    makeRegion(5000.0f, 10000.0f, 1.00f),
                    makeRegion(120.0f, 280.0f,  0.40f),
                    makeRegion(3000.0f, 5000.0f, 0.16f)),
        makeProfile(makeRegion(250.0f, 900.0f,  1.00f),
                    makeRegion(150.0f, 250.0f,  0.42f),
                    makeRegion(900.0f, 1800.0f,  0.40f),
                    makeRegion(1800.0f, 3000.0f, 0.12f)),
        makeProfile(makeRegion(300.0f, 3000.0f, 0.56f),
                    makeRegion(3000.0f, 7000.0f, 0.24f),
                    makeRegion(20.0f, 20000.0f, 0.05f)),
        0.32f, 50.0f, 220.0f, 0.62f, 0.88f, 0.40f, 0.25f, 0.40f, 0.64f, 0.08f, 0.65f, 0.95f, 0.52f, 55.0f, 110.0f
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
        controlSmoothers[static_cast<size_t>(i)].reset(sampleRateHz, 0.050);
        targetControlValues[static_cast<size_t>(i)].store(i == kStrengthIndex ? 1.0f : 0.5f, std::memory_order_relaxed);
    }

    reset();
}

void Dsp::reset() {
    std::fill(prevAnalysisMag.begin(), prevAnalysisMag.end(), 0.0f);
    std::fill(bassContinuity.begin(), bassContinuity.end(), 0.0f);
    std::fill(compositeGain.begin(), compositeGain.end(), 1.0f);
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
            const float mag = std::sqrt(std::max(kEps, re * re + im * im));
            float gain = compositeGain[static_cast<size_t>(k)];
            const float hz = binToHz(k);

            if (hz < modeProfile.lowEndUnityBlendEndHz) {
                const float blend = clamp01((hz - modeProfile.lowEndUnityBlendStartHz)
                    / std::max(5.0f, modeProfile.lowEndUnityBlendEndHz - modeProfile.lowEndUnityBlendStartHz));
                const float protectedBlend = lerp(1.0f, blend, modeProfile.lowEndProtection);
                gain = lerp(1.0f, gain, protectedBlend);
            }

            const float scaledMag = mag * juce::jlimit(0.0f,
                                                       juce::Decibels::decibelsToGain(kMaxBoostDb),
                                                       gain);
            const float ratio = scaledMag / std::max(kEps, mag);
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

    for (int k = 0; k < kBins; ++k) {
        const float hz = binToHz(k);
        const float prevMag = prevAnalysisMag[static_cast<size_t>(k)];
        const float deltaNorm = std::abs(analysisMag[static_cast<size_t>(k)] - prevMag)
            / std::max({ kEps, analysisMag[static_cast<size_t>(k)], prevMag, meanMag });
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
            drums += drumBias * drumWindow * gaussianPeak(hz, 3000.0f, 1300.0f) * (0.12f + 0.72f * transientPrior);
        if (hz >= 5000.0f) {
            const float hihatWide = lerp(wide, 0.0f, signalQuality.monoScore);
            drums += drumBias * drumWindow * (0.08f + 0.18f * hihatWide + 0.58f * transientPrior);
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
            const float residualSpace = 1.0f - clamp01(0.96f * vocals + 0.82f * drums + 0.50f * bass);
            const float guitarBody = guitarWindow * (0.10f + 0.28f * steadyPrior * modeProfile.harmonicTrust
                + 0.16f * wide + 0.16f * (1.0f - centered));
            guitar += residualSpace * guitarBody;
            guitar *= lerp(1.0f, phoneMode ? 0.90f : 0.82f, centered);
            guitar *= lerp(1.0f, phoneMode ? 0.88f : 0.80f, vocals * (0.7f + 0.3f * analysisContext.speechPresence));
            guitar *= lerp(1.0f, phoneMode ? 0.90f : 0.84f, transientPrior);
            if (phoneMode && hz > 2000.0f)
                guitar *= 0.55f;
            if (phoneMode && hz < 200.0f)
                guitar *= 0.65f;
            if (hz > 1800.0f)
                guitar *= liveMode ? 0.82f : 0.74f;
        }
        rawWeights[guitarSource][static_cast<size_t>(k)] = guitar;

        const float occupied = clamp01(0.48f * bass + 0.62f * drums + 0.78f * vocals + 0.48f * guitar);
        float other = 0.003f + 0.24f * modeProfile.residualFallbackStrength * (1.0f - occupied) + 0.05f * wide;
        other += otherWindow * (0.03f + 0.42f * modeProfile.residualFallback * (0.35f + 0.65f * wide));
        if (phoneMode && hz >= 400.0f && hz <= 1800.0f)
            other *= 1.08f;
        if (hz < modeProfile.lowEndUnityBlendEndHz)
            other *= 0.12f;
        other *= 1.0f - 0.55f * clamp01(vocals);
        other *= 1.0f - 0.35f * clamp01(guitar);
        rawWeights[otherSource][static_cast<size_t>(k)] = other;

        if (phoneMode && hz >= 400.0f && hz <= 1800.0f) {
            rawWeights[vocalsSource][static_cast<size_t>(k)] *= 0.88f;
            rawWeights[guitarSource][static_cast<size_t>(k)] *= 0.82f;
        } else if (liveMode && hz >= 300.0f && hz <= 1200.0f) {
            rawWeights[guitarSource][static_cast<size_t>(k)] *= 0.90f;
            rawWeights[otherSource][static_cast<size_t>(k)] *= 1.08f;
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
        }

        // Heuristic semantic support (DSP-only, no ML)
        semanticSupport[vocalsSource] = clamp01(0.02f + 0.98f * vocalWindow + 0.16f * centered - 0.10f * wide);
        semanticSupport[drumsSource]  = clamp01(0.03f + 0.94f * drumWindow + 0.16f * transientPrior + 0.10f * wide);
        semanticSupport[bassSource]   = clamp01(0.01f + 1.04f * bassWindow + 0.10f * centered + 0.10f * steadyPrior);
        semanticSupport[guitarSource] = clamp01(0.02f + 0.98f * guitarWindow + 0.16f * wide + 0.10f * (1.0f - centered) + 0.12f * steadyPrior);
        semanticSupport[otherSource]  = clamp01(0.05f + 0.72f * otherWindow + 0.18f * wide + 0.10f * (1.0f - centered));

        if (hz < 140.0f)
            semanticSupport[vocalsSource] *= 0.08f;
        if (hz > 9000.0f)
            semanticSupport[vocalsSource] *= 0.18f;

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
            conditionedMasks[vocalsSource] *= 0.88f;
            conditionedMasks[guitarSource] *= 0.82f;
        } else if (liveMode && hz >= 300.0f && hz <= 1200.0f) {
            conditionedMasks[guitarSource] *= 0.90f;
            conditionedMasks[otherSource] *= 1.08f;
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

float Dsp::mappedSourceGainDb(const int sourceIndex) const noexcept {
    const auto& modeProfile = currentModeProfile();
    const float strength = currentControlValues[static_cast<size_t>(kStrengthIndex)];
    const float centered = (currentControlValues[static_cast<size_t>(sourceIndex)] - 0.5f) * 2.0f;
    // Asymmetric: boosts use kMaxBoostDb, cuts use kMaxCutDb so param=0 reaches silence.
    const float db = centered >= 0.0f
        ? centered * kMaxBoostDb
        : centered * kMaxCutDb;
    return lerp(0.0f,
                juce::jlimit(-kMaxCutDb, kMaxBoostDb, db) * modeProfile.globalStrength,
                strength);
}

} // namespace vxsuite::rebalance
