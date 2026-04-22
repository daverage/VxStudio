#include "VxStudioFinishDsp.h"

#include "VxStudioBlockSmoothing.h"

#include <algorithm>
#include <cmath>

namespace vxsuite::finish {

void Dsp::prepare(const double sampleRate, const int maxBlockSize, const int numChannels) {
    sr = sampleRate > 1000.0 ? sampleRate : 48000.0;
    channels = std::max(0, numChannels);
    opto.prepare(sr, maxBlockSize, channels);
    reset();
}

void Dsp::setParams(const Params& p) {
    params = p;
}

void Dsp::reset() {
    opto.reset();
    smoothedAutoMakeupDb = 0.0f;
    smoothedRecoveryDb = 0.0f;
    limitEnv = 0.0f;
    limitGain = 1.0f;
    limiterActivity = 0.0f;
}

void Dsp::process(juce::AudioBuffer<float>& buffer) {
    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0)
        return;

    const bool voiceMode = params.contentMode == 0;
    const float peakReduction = juce::jlimit(0.0f, 1.0f, params.peakReduction);
    const bool finishStageEnabled = peakReduction > 1.0e-4f;
    const int numChannels = std::min(channels, buffer.getNumChannels());

    double dryRmsSq = 0.0;
    float dryPeak = 0.0f;
    if (numChannels > 0) {
        for (int ch = 0; ch < numChannels; ++ch) {
            dryPeak = std::max(dryPeak, buffer.getMagnitude(ch, 0, numSamples));
            const auto* data = buffer.getReadPointer(ch);
            for (int i = 0; i < numSamples; ++i)
                dryRmsSq += static_cast<double>(data[i]) * data[i];
        }
    }
    const int dryCount = std::max(1, numChannels * numSamples);
    const float dryRms = static_cast<float>(std::sqrt(dryRmsSq / static_cast<double>(dryCount)));

    const float autoMakeupMaxDb = voiceMode ? 11.5f : 9.5f;
    const float autoMakeupFromKnobDb = autoMakeupMaxDb * std::pow(peakReduction, voiceMode ? 0.72f : 0.78f);
    const float autoMakeupTargetDb = autoMakeupFromKnobDb;
    smoothedAutoMakeupDb += vxsuite::blockBlendAlpha(sr, numSamples, 0.18f)
        * (autoMakeupTargetDb - smoothedAutoMakeupDb);

    updateOptoParams(smoothedAutoMakeupDb + params.outputGainDb);
    opto.process(buffer);

    if (!finishStageEnabled) {
        smoothedRecoveryDb = 0.0f;
        limitEnv = 0.0f;
        limitGain = 1.0f;
        limiterActivity = 0.0f;
        return;
    }

    processLimiter(buffer);

    double wetRmsSq = 0.0;
    float wetPeak = 0.0f;
    if (numChannels > 0) {
        for (int ch = 0; ch < numChannels; ++ch) {
            wetPeak = std::max(wetPeak, buffer.getMagnitude(ch, 0, numSamples));
            const auto* data = buffer.getReadPointer(ch);
            for (int i = 0; i < numSamples; ++i)
                wetRmsSq += static_cast<double>(data[i]) * data[i];
        }
    }
    const float wetRms = static_cast<float>(std::sqrt(wetRmsSq / static_cast<double>(dryCount)));
    const float dryDb = juce::Decibels::gainToDecibels(std::max(dryRms, 1.0e-6f), -120.0f);
    const float wetDb = juce::Decibels::gainToDecibels(std::max(wetRms, 1.0e-6f), -120.0f);
    const float measuredLossDb = std::max(0.0f, dryDb - wetDb - (voiceMode ? 0.75f : 0.95f));
    const float peakCeiling = juce::Decibels::decibelsToGain(voiceMode ? -1.0f : -1.2f);
    const float headroomDb = juce::Decibels::gainToDecibels(std::max(peakCeiling / std::max(wetPeak, 1.0e-6f), 1.0e-6f), 0.0f);
    const float grRecoveryDb = std::max(0.0f, opto.getGainReductionDb() - (voiceMode ? 0.8f : 1.1f))
        * (voiceMode ? 0.58f : 0.44f);
    const float limiterRecoveryDb = limiterActivity * (voiceMode ? 2.1f : 1.6f);
    const float desiredRecoveryDb = std::max(grRecoveryDb + limiterRecoveryDb, measuredLossDb * 0.55f);
    const float recoveryMaxDb = (voiceMode ? 4.0f : 3.2f) + (voiceMode ? 4.2f : 2.8f) * peakReduction;
    const float recoveryTargetDb = juce::jlimit(0.0f,
                                                std::max(0.0f, headroomDb),
                                                std::min(recoveryMaxDb, desiredRecoveryDb));
    smoothedRecoveryDb += vxsuite::blockBlendAlpha(sr, numSamples, recoveryTargetDb > smoothedRecoveryDb ? 0.22f : 0.34f)
        * (recoveryTargetDb - smoothedRecoveryDb);
    const float recoveryGain = juce::Decibels::decibelsToGain(smoothedRecoveryDb);
    if (std::abs(recoveryGain - 1.0f) > 1.0e-4f)
        buffer.applyGain(recoveryGain);
}

void Dsp::updateOptoParams(const float outputGainDb) {
    OptoCompressorLA2A::Params op {};
    op.peakReduction = juce::jlimit(0.0f, 1.0f, params.peakReduction);
    op.outputGainDb = outputGainDb;
    op.body = juce::jlimit(0.0f, 1.0f, params.body);
    op.stereoLink = true;
    op.mode = params.contentMode == 0 ? OptoCompressorLA2A::Mode::compress
                                      : OptoCompressorLA2A::Mode::limit;
    opto.setParams(op);
}

void Dsp::processLimiter(juce::AudioBuffer<float>& buffer) {
    const int numChannels = std::min(channels, buffer.getNumChannels());
    const int numSamples = buffer.getNumSamples();
    if (numChannels <= 0 || numSamples <= 0)
        return;

    const bool voiceMode = params.contentMode == 0;
    const float attackA = std::exp(-1.0f / (0.00025f * static_cast<float>(sr)));
    const float releaseA = std::exp(-1.0f / (0.050f * static_cast<float>(sr)));
    const float ceiling = juce::Decibels::decibelsToGain(voiceMode ? -1.5f : -1.8f);

    float limiterAccDb = 0.0f;
    for (int i = 0; i < numSamples; ++i) {
        float samplePeak = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            samplePeak = std::max(samplePeak, std::abs(buffer.getReadPointer(ch)[i]));

        const float envA = samplePeak > limitEnv ? attackA : releaseA;
        limitEnv = envA * limitEnv + (1.0f - envA) * samplePeak;

        float targetGain = 1.0f;
        if (limitEnv > ceiling)
            targetGain = ceiling / std::max(limitEnv, 1.0e-6f);
        if (samplePeak > ceiling)
            targetGain = std::min(targetGain, ceiling / std::max(samplePeak, 1.0e-6f));

        if (targetGain < limitGain) {
            limitGain = targetGain;
        } else {
            limitGain = releaseA * limitGain + (1.0f - releaseA) * targetGain;
        }

        limiterAccDb += std::max(0.0f, -juce::Decibels::gainToDecibels(std::max(limitGain, 1.0e-6f), -120.0f));

        for (int ch = 0; ch < numChannels; ++ch)
            buffer.getWritePointer(ch)[i] *= limitGain;
    }

    limiterActivity = juce::jlimit(0.0f, 1.0f, (limiterAccDb / static_cast<float>(numSamples)) / 6.0f);
}

} // namespace vxsuite::finish
