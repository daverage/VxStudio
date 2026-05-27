#include "VxClarityDsp.h"

#include <algorithm>
#include <cmath>

namespace vxsuite::clarity {
namespace {

constexpr std::array<float, ClarityDsp::kBandCount> kBodyWeights {
    1.00f, 0.95f, 0.70f, 0.35f, 0.15f, 0.05f
};

constexpr std::array<float, ClarityDsp::kBandCount> kClarityWeights {
    0.20f, 0.60f, 1.00f, 1.00f, 0.70f, 0.35f
};

constexpr std::array<float, ClarityDsp::kBandCount> kBandCentersHz {
    70.0f, 220.0f, 650.0f, 1800.0f, 4200.0f, 10500.0f
};

constexpr std::array<float, ClarityDsp::kBandCount> kMaxCutDb {
    8.0f, 10.0f, 11.5f, 11.0f, 8.0f, 5.0f
};

constexpr std::array<float, ClarityDsp::kCrossCount> kCutoffHz {
    120.0f, 350.0f, 1000.0f, 2800.0f, 6500.0f
};

template <typename T>
T lerp(T a, T b, T t) {
    return a + (b - a) * t;
}

float safeFloat(const float value) {
    if (!std::isfinite(value))
        return 0.0f;
    if (std::fpclassify(value) == FP_SUBNORMAL)
        return 0.0f;
    return value;
}

} // namespace

void ClarityDsp::OnePoleLowpass::setCutoff(const double sampleRate, const float cutoffHz) noexcept {
    if (sampleRate <= 0.0 || cutoffHz <= 0.0f) {
        coeff = 0.0f;
        z = 0.0f;
        return;
    }
    coeff = std::exp(-2.0f * juce::MathConstants<float>::pi * cutoffHz / static_cast<float>(sampleRate));
}

float ClarityDsp::OnePoleLowpass::process(const float sample) noexcept {
    z = sample + coeff * (z - sample);
    return z;
}

void ClarityDsp::prepare(const double sampleRate, const int maxBlockSize, const int numChannels) {
    sr = sampleRate > 1000.0 ? sampleRate : 48000.0;
    channels.resize(static_cast<size_t>(std::max(1, numChannels)));
    configureFilters(sr);
    reset();
    juce::ignoreUnused(maxBlockSize);
}

void ClarityDsp::configureFilters(const double sampleRate) noexcept {
    for (auto& channel : channels) {
        for (size_t i = 0; i < kCrossCount; ++i)
            channel.audioSplits[i].setCutoff(sampleRate, kCutoffHz[i]);
    }
    for (size_t i = 0; i < kCrossCount; ++i) {
        analysis.mainSplits[i].setCutoff(sampleRate, kCutoffHz[i]);
        analysis.sideSplits[i].setCutoff(sampleRate, kCutoffHz[i]);
    }
}

void ClarityDsp::resetFilters() noexcept {
    for (auto& channel : channels) {
        for (auto& split : channel.audioSplits)
            split.reset();
    }
    for (auto& split : analysis.mainSplits)
        split.reset();
    for (auto& split : analysis.sideSplits)
        split.reset();
    analysis.previousMainSample = 0.0f;
    analysis.previousSideSample = 0.0f;
}

void ClarityDsp::reset() noexcept {
    resetFilters();
    longDensity.fill(0.0f);
    shortConflict.fill(0.0f);
    bandGainDb.fill(0.0f);
    targetGainDb.fill(0.0f);
    sidechainPresence = 0.0f;
    lastBlockRms = 0.0f;
    lastBlockVariance = 0.0f;
}

float ClarityDsp::clamp01(const float value) noexcept {
    return juce::jlimit(0.0f, 1.0f, safeFloat(value));
}

float ClarityDsp::safeRmsFromEnergy(const float energy, const int samples) noexcept {
    if (samples <= 0)
        return 0.0f;
    return static_cast<float>(std::sqrt(std::max(0.0f, energy / static_cast<float>(samples))));
}

void ClarityDsp::analyzeBlock(const juce::AudioBuffer<float>& buffer,
                              const juce::AudioBuffer<float>* sidechainBuffer,
                              const Params& params,
                              std::array<float, kBandCount>& outTargetsDb,
                              float& outSidechainPresence) noexcept {
    outTargetsDb.fill(0.0f);
    outSidechainPresence = 0.0f;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    if (numSamples <= 0 || numChannels <= 0)
        return;

    double mainEnergy = 0.0;
    double sideEnergy = 0.0;
    double diffEnergy = 0.0;
    std::array<double, kBandCount> mainBandEnergy {};
    std::array<double, kBandCount> sideBandEnergy {};

    const bool hasSidechain = sidechainBuffer != nullptr
        && sidechainBuffer->getNumChannels() > 0
        && sidechainBuffer->getNumSamples() > 0;
    const int sidechainChannels = hasSidechain ? sidechainBuffer->getNumChannels() : 0;

    for (int sample = 0; sample < numSamples; ++sample) {
        float mainMono = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            mainMono += buffer.getReadPointer(ch)[sample];
        mainMono /= static_cast<float>(std::max(1, numChannels));

        const float mainDiff = mainMono - analysis.previousMainSample;
        analysis.previousMainSample = mainMono;
        mainEnergy += static_cast<double>(mainMono) * mainMono;
        diffEnergy += static_cast<double>(mainDiff) * mainDiff;

        float mainPrev = 0.0f;
        float mainSplit = mainMono;
        for (size_t band = 0; band < kCrossCount; ++band) {
            mainSplit = analysis.mainSplits[band].process(mainMono);
            const float bandValue = mainSplit - mainPrev;
            mainBandEnergy[band] += static_cast<double>(bandValue) * bandValue;
            mainPrev = mainSplit;
        }
        const float topMainBand = mainMono - mainPrev;
        mainBandEnergy[kBandCount - 1] += static_cast<double>(topMainBand) * topMainBand;

        if (hasSidechain) {
            float sideMono = 0.0f;
            for (int ch = 0; ch < sidechainChannels; ++ch)
                sideMono += sidechainBuffer->getReadPointer(ch)[sample];
            sideMono /= static_cast<float>(std::max(1, sidechainChannels));

            const float sideDiff = sideMono - analysis.previousSideSample;
            analysis.previousSideSample = sideMono;
            sideEnergy += static_cast<double>(sideMono) * sideMono;
            float sidePrev = 0.0f;
            float sideSplit = sideMono;
            for (size_t band = 0; band < kCrossCount; ++band) {
                sideSplit = analysis.sideSplits[band].process(sideMono);
                const float bandValue = sideSplit - sidePrev;
                sideBandEnergy[band] += static_cast<double>(bandValue) * bandValue;
                sidePrev = sideSplit;
            }
            const float topSideBand = sideMono - sidePrev;
            sideBandEnergy[kBandCount - 1] += static_cast<double>(topSideBand) * topSideBand;
        }
    }

    const float mainRms = safeRmsFromEnergy(static_cast<float>(mainEnergy), numSamples);
    const float sideRms = hasSidechain ? safeRmsFromEnergy(static_cast<float>(sideEnergy), numSamples) : 0.0f;
    const float transientScore = juce::jlimit(0.0f, 1.0f,
        safeRmsFromEnergy(static_cast<float>(diffEnergy), numSamples) / std::max(0.02f, mainRms));
    const float blockVariance = juce::jlimit(0.0f, 1.0f,
        std::abs(mainRms - lastBlockRms) / std::max(0.02f, mainRms + lastBlockRms));
    lastBlockRms = mainRms;
    lastBlockVariance = blockVariance;

    double totalMainBand = 1.0e-12;
    double centroidNumerator = 0.0;
    for (size_t band = 0; band < kBandCount; ++band) {
        totalMainBand += mainBandEnergy[band];
        centroidNumerator += static_cast<double>(kBandCentersHz[band]) * mainBandEnergy[band];
    }
    const float centroid = static_cast<float>(centroidNumerator / totalMainBand);
    const float centroidScore = juce::jlimit(0.0f, 1.0f, (centroid - 250.0f) / 9000.0f);
    const float energyBalance = juce::jlimit(0.0f, 1.0f, mainRms / std::max(0.02f, 0.18f));
    const float coreDensity = juce::jlimit(0.0f, 1.0f,
        0.42f * energyBalance + 0.28f * centroidScore + 0.20f * transientScore + 0.10f * blockVariance);

    float sidePresence = 0.0f;
    if (hasSidechain) {
        sidePresence = juce::jlimit(0.0f, 1.0f, sideRms / std::max(0.02f, 0.18f));
        const float rawPresence = juce::jlimit(0.0f, 1.0f, 0.35f + 0.65f * sidePresence);
        outSidechainPresence = rawPresence;
    }

    const float trust = juce::jlimit(0.30f, 1.0f, 0.35f + 0.65f * params.separationConfidence);
    const float voiceProtect = params.voiceMode
        ? juce::jlimit(0.48f, 1.0f, params.sourceProtect)
        : 0.90f;
    const float focus = clamp01(params.focus);
    const float clean = clamp01(params.clean);

    for (size_t band = 0; band < kBandCount; ++band) {
        const float mainBandNorm = static_cast<float>(mainBandEnergy[band] / totalMainBand);
        const float sideBandNorm = hasSidechain
            ? static_cast<float>(sideBandEnergy[band] / (1.0e-12 + sideEnergy))
            : 0.0f;
        const float densityNeed = juce::jlimit(0.0f, 1.0f,
            0.50f * longDensity[band] + 0.30f * mainBandNorm + 0.20f * coreDensity);
        const float bodyWeight = lerp(kBodyWeights[band], kClarityWeights[band], focus);
        const float modeWeight = params.voiceMode
            ? (0.78f + 0.18f * params.speechFocus)
            : 1.08f;
        const float guardMod = juce::jlimit(0.68f, 1.0f, 1.0f - 0.26f * params.guardStrictness);
        const float bandBodyProtect = lerp(1.0f, 0.70f, focus) * voiceProtect * guardMod;
        const float persistentNeed = juce::jlimit(0.0f, 1.0f, densityNeed);
        const float adaptiveNeed = hasSidechain
            ? juce::jlimit(0.0f, 1.0f, 0.60f * sideBandNorm + 0.25f * outSidechainPresence + 0.15f * coreDensity)
            : juce::jlimit(0.0f, 1.0f, 0.40f * transientScore + 0.25f * blockVariance + 0.35f * coreDensity);
        const float combinedNeed = juce::jlimit(0.0f, 1.0f,
            0.60f * persistentNeed + 0.40f * adaptiveNeed);
        const float effectStrength = juce::jlimit(0.0f, 1.0f, 0.68f + 0.92f * clean);
        const float maxCutDb = kMaxCutDb[band] * clean * effectStrength * trust * modeWeight;
        const float shapedNeed = juce::jlimit(0.0f, 1.0f, combinedNeed * bodyWeight * bandBodyProtect);
        outTargetsDb[band] = -maxCutDb * shapedNeed;
    }

    const float cleanupLift = juce::jlimit(0.0f, 1.0f,
        clean * (params.voiceMode ? params.speechFocus : 0.75f));
    for (size_t band = 0; band < kBandCount; ++band) {
        longDensity[band] = juce::jlimit(0.0f, 1.0f,
            0.992f * longDensity[band] + 0.008f * static_cast<float>(mainBandEnergy[band] / totalMainBand));
        shortConflict[band] = juce::jlimit(0.0f, 1.0f,
            0.940f * shortConflict[band] + 0.060f * juce::jlimit(0.0f, 1.0f,
                hasSidechain
                    ? static_cast<float>(sideBandEnergy[band] / (mainBandEnergy[band] + sideBandEnergy[band] + 1.0e-12))
                    : juce::jlimit(0.0f, 1.0f, 0.55f * transientScore + 0.45f * blockVariance)));

        const float bias = params.voiceMode && band <= 2 ? 0.76f : 1.0f;
        targetGainDb[band] = outTargetsDb[band] * bias * (0.88f + 0.12f * cleanupLift);
    }
}

void ClarityDsp::processChannel(const size_t channelIndex,
                                float* data,
                                const int numSamples,
                                const std::array<float, kBandCount>& bandGains) noexcept {
    if (!data || numSamples <= 0)
        return;

    auto& channel = channels[channelIndex];
    for (int sample = 0; sample < numSamples; ++sample) {
        const float x = safeFloat(data[sample]);
        float lower = 0.0f;
        float y = 0.0f;
        for (size_t band = 0; band < kCrossCount; ++band) {
            const float lp = channel.audioSplits[band].process(x);
            const float bandSample = lp - lower;
            y += bandSample * juce::Decibels::decibelsToGain(bandGains[band]);
            lower = lp;
        }
        y += (x - lower) * juce::Decibels::decibelsToGain(bandGains[kBandCount - 1]);
        data[sample] = y;
    }
}

void ClarityDsp::process(juce::AudioBuffer<float>& buffer,
                         const juce::AudioBuffer<float>* sidechainBuffer,
                         const Params& params,
                         const ProcessOptions& options) noexcept {
    const int numSamples = buffer.getNumSamples();
    const int numChannels = std::min(buffer.getNumChannels(), static_cast<int>(channels.size()));
    if (numSamples <= 0 || numChannels <= 0)
        return;

    // Framework integration: ProcessOptions provides baseline protection scaling
    static_cast<void>(options);  // Use for future spectral floor scaling if needed

    std::array<float, kBandCount> targets {};
    float sidePresence = 0.0f;
    analyzeBlock(buffer, sidechainBuffer, params, targets, sidePresence);
    sidechainPresence = juce::jlimit(0.0f, 1.0f,
        0.90f * sidechainPresence + 0.10f * sidePresence);

    const bool firstActiveResponse = std::all_of(bandGainDb.begin(), bandGainDb.end(), [](float gain) {
        return std::abs(gain) < 1.0e-3f;
    });
    const float smoothing = firstActiveResponse
        ? juce::jlimit(0.20f, 0.55f, 0.35f + 0.15f * params.clean + 0.10f * params.focus)
        : juce::jlimit(0.10f, 0.32f, 0.16f + 0.10f * params.clean + 0.06f * params.focus);

    for (size_t band = 0; band < kBandCount; ++band)
        bandGainDb[band] = bandGainDb[band] + smoothing * (targets[band] - bandGainDb[band]);

    for (int ch = 0; ch < numChannels; ++ch)
        processChannel(static_cast<size_t>(ch), buffer.getWritePointer(ch), numSamples, bandGainDb);

    const float decay = juce::jlimit(0.0f, 1.0f, 0.995f + 0.002f * (1.0f - params.clean));
    if (params.clean <= 1.0e-4f) {
        for (size_t band = 0; band < kBandCount; ++band) {
            bandGainDb[band] *= 0.70f;
            targetGainDb[band] *= 0.70f;
        }
    } else if (params.sidechainPresent) {
        for (size_t band = 0; band < kBandCount; ++band)
            targetGainDb[band] *= decay;
    }
}

} // namespace vxsuite::clarity
