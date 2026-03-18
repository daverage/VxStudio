#include "VxFinishDsp.h"

#include "../../../framework/VxSuiteBlockSmoothing.h"

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

    const float autoMakeupMaxDb = voiceMode ? 4.0f : 3.0f;
    const float autoMakeupTargetDb = autoMakeupMaxDb * std::pow(peakReduction, 0.85f);
    smoothedAutoMakeupDb += vxsuite::blockBlendAlpha(sr, numSamples, 0.18f)
        * (autoMakeupTargetDb - smoothedAutoMakeupDb);

    updateOptoParams(smoothedAutoMakeupDb + params.outputGainDb);
    opto.process(buffer);

    if (!finishStageEnabled) {
        limitEnv = 0.0f;
        limitGain = 1.0f;
        limiterActivity = 0.0f;
        return;
    }

    processLimiter(buffer);
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
