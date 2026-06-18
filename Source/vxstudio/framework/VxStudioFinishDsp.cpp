#include "VxStudioFinishDsp.h"

#include "VxStudioBlockSmoothing.h"

#include <algorithm>
#include <cmath>

namespace vxsuite::finish {

namespace {

inline float clamp01(const float x) noexcept {
    return juce::jlimit(0.0f, 1.0f, x);
}

inline float catmullRomSample(const float p0,
                              const float p1,
                              const float p2,
                              const float p3,
                              const float t) noexcept {
    const float t2 = t * t;
    const float t3 = t2 * t;
    return 0.5f * ((2.0f * p1)
                   + (-p0 + p2) * t
                   + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2
                   + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}

float estimateTruePeak(const juce::AudioBuffer<float>& buffer, const int numChannels) noexcept {
    const int samples = buffer.getNumSamples();
    if (numChannels <= 0 || samples <= 0)
        return 0.0f;

    float peak = 0.0f;
    constexpr std::array<float, 3> kProbePoints { 0.25f, 0.5f, 0.75f };

    for (int ch = 0; ch < numChannels; ++ch) {
        const auto* data = buffer.getReadPointer(ch);
        for (int i = 0; i < samples; ++i) {
            const float p1 = data[i];
            peak = std::max(peak, std::abs(p1));

            if (i >= samples - 1)
                continue;

            const float p0 = data[i > 0 ? i - 1 : i];
            const float p2 = data[i + 1];
            const float p3 = data[i + 2 < samples ? i + 2 : i + 1];
            for (const float t : kProbePoints)
                peak = std::max(peak, std::abs(catmullRomSample(p0, p1, p2, p3, t)));
        }
    }

    return peak;
}

} // namespace

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
    primed = false;
    dryPeakL = dryPeakR = wetPeakL = wetPeakR = 0.0f;
}

void Dsp::process(juce::AudioBuffer<float>& buffer, const ProcessOptions& options) {
    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0)
        return;

    const bool voiceMode = options.isVoiceMode;
    const float peakReduction = juce::jlimit(0.0f, 1.0f, params.peakReduction);
    const bool finishStageEnabled = peakReduction > 1.0e-4f;
    const int numChannels = std::min(channels, buffer.getNumChannels());

    double dryRmsSq = 0.0;
    if (numChannels > 0) {
        for (int ch = 0; ch < numChannels; ++ch) {
            const auto* data = buffer.getReadPointer(ch);
            for (int i = 0; i < numSamples; ++i)
                dryRmsSq += static_cast<double>(data[i]) * data[i];
        }
    }
    const int dryCount = std::max(1, numChannels * numSamples);
    const float dryRms = static_cast<float>(std::sqrt(dryRmsSq / static_cast<double>(dryCount)));
    const float dryTruePeak = estimateTruePeak(buffer, numChannels);

    // Park makeup at zero when signal is below −80 dBFS so that when audio resumes
    // after silence (y1 decayed to zero → no compression on first sample), the makeup
    // starts low rather than at its steady-state value. The 10ms blend rate on the
    // first non-silent block ramps it to full before the opto attack establishes GR,
    // preventing the limiter overload that caused a pop in the original snap behaviour.
    if (dryTruePeak < 1.0e-4f) {
        smoothedAutoMakeupDb = 0.0f;
        primed = false;
    }

    const float autoMakeupMaxDb = 18.0f;
    const float autoMakeupFromKnobDb = autoMakeupMaxDb * std::pow(peakReduction, voiceMode ? 0.40f : 0.43f);
    if (dryTruePeak < 1.0e-4f || !finishStageEnabled) {
        smoothedAutoMakeupDb = 0.0f;
    } else {
        const float blendRate = !primed ? 0.010f : 0.18f;
        smoothedAutoMakeupDb += vxsuite::blockBlendAlpha(sr, numSamples, blendRate)
            * (autoMakeupFromKnobDb - smoothedAutoMakeupDb);
        primed = true;
    }

    // Capture per-channel dry peak before any processing.
    {
        const auto* ch0 = numChannels > 0 ? buffer.getReadPointer(0) : nullptr;
        const auto* ch1 = numChannels > 1 ? buffer.getReadPointer(1) : nullptr;
        float peakL = 0.0f, peakR = 0.0f;
        for (int i = 0; i < numSamples; ++i) {
            if (ch0) peakL = std::max(peakL, std::abs(ch0[i]));
            if (ch1) peakR = std::max(peakR, std::abs(ch1[i]));
        }
        dryPeakL = peakL;
        dryPeakR = numChannels > 1 ? peakR : peakL;
    }

    updateOptoParams(smoothedAutoMakeupDb + params.outputGainDb);
    opto.process(buffer);

    if (!finishStageEnabled) {
        smoothedRecoveryDb = 0.0f;
        limitEnv = 0.0f;
        limitGain = 1.0f;
        limiterActivity = 0.0f;
        wetPeakL = dryPeakL;
        wetPeakR = dryPeakR;
        return;
    }

    processLimiter(buffer);

    double wetRmsSq = 0.0;
    if (numChannels > 0) {
        for (int ch = 0; ch < numChannels; ++ch) {
            const auto* data = buffer.getReadPointer(ch);
            for (int i = 0; i < numSamples; ++i)
                wetRmsSq += static_cast<double>(data[i]) * data[i];
        }
    }
    const float wetRms = static_cast<float>(std::sqrt(wetRmsSq / static_cast<double>(dryCount)));
    const float wetTruePeak = estimateTruePeak(buffer, numChannels);
    const float dryDb = juce::Decibels::gainToDecibels(std::max(dryRms, 1.0e-6f), -120.0f);
    const float wetDb = juce::Decibels::gainToDecibels(std::max(wetRms, 1.0e-6f), -120.0f);
    const float measuredLossDb = std::max(0.0f, dryDb - wetDb - (voiceMode ? 0.55f : 0.70f));
    const float dryCrestDb = juce::Decibels::gainToDecibels(std::max(dryTruePeak / std::max(dryRms, 1.0e-6f), 1.0e-6f), 0.0f);
    const float wetCrestDb = juce::Decibels::gainToDecibels(std::max(wetTruePeak / std::max(wetRms, 1.0e-6f), 1.0e-6f), 0.0f);
    const float crestPenalty = clamp01((wetCrestDb - (voiceMode ? 11.0f : 9.5f)) / 10.0f);
    const float peakCeiling = juce::Decibels::decibelsToGain(voiceMode ? -1.2f : -1.1f);
    const float headroomDb = juce::Decibels::gainToDecibels(std::max(peakCeiling / std::max(wetTruePeak, 1.0e-6f), 1.0e-6f), 0.0f);
    const float grRecoveryDb = std::max(0.0f, opto.getGainReductionDb() - (voiceMode ? 0.4f : 0.5f))
        * (voiceMode ? 0.82f : 0.72f);
    const float limiterRecoveryDb = limiterActivity * (voiceMode ? 3.4f : 3.0f);
    const float measuredRecoveryWeight = (voiceMode ? 0.65f : 0.60f)
        * (1.0f - 0.30f * crestPenalty)
        * (1.0f - 0.12f * clamp01(std::max(0.0f, wetCrestDb - dryCrestDb) / 6.0f));
    const float desiredRecoveryDb = std::max(grRecoveryDb + limiterRecoveryDb,
                                             measuredLossDb * measuredRecoveryWeight);
    const float recoveryMaxDb = (voiceMode ? 7.0f : 6.0f) + (voiceMode ? 8.0f : 6.0f) * peakReduction;
    const float recoveryTargetDb = juce::jlimit(0.0f,
                                                std::max(0.0f, headroomDb),
                                                std::min(recoveryMaxDb, desiredRecoveryDb));
    smoothedRecoveryDb += vxsuite::blockBlendAlpha(sr, numSamples, recoveryTargetDb > smoothedRecoveryDb ? 0.22f : 0.34f)
        * (recoveryTargetDb - smoothedRecoveryDb);
    const float recoveryGain = juce::Decibels::decibelsToGain(smoothedRecoveryDb);
    if (std::abs(recoveryGain - 1.0f) > 1.0e-4f)
        buffer.applyGain(recoveryGain);

    // Clip to -0.5 dBFS so that true-peak estimation (Catmull-Rom overshoot ~0.5%)
    // stays safely inside 0 dBFS even after recovery gain on bright transient material.
    constexpr float kFinalCeiling = 0.944f;
    for (int ch = 0; ch < numChannels; ++ch) {
        float* data = buffer.getWritePointer(ch);
        for (int i = 0; i < numSamples; ++i)
            data[i] = juce::jlimit(-kFinalCeiling, kFinalCeiling, data[i]);
    }

    // Capture per-channel wet peak after all processing.
    {
        const auto* ch0 = numChannels > 0 ? buffer.getReadPointer(0) : nullptr;
        const auto* ch1 = numChannels > 1 ? buffer.getReadPointer(1) : nullptr;
        float peakL = 0.0f, peakR = 0.0f;
        for (int i = 0; i < numSamples; ++i) {
            if (ch0) peakL = std::max(peakL, std::abs(ch0[i]));
            if (ch1) peakR = std::max(peakR, std::abs(ch1[i]));
        }
        wetPeakL = peakL;
        wetPeakR = numChannels > 1 ? peakR : peakL;
    }
}

void Dsp::updateOptoParams(const float outputGainDb) {
    OptoCompressorLA2A::Params op {};
    op.peakReduction = juce::jlimit(0.0f, 1.0f, params.peakReduction);
    op.outputGainDb = outputGainDb;
    op.body = juce::jlimit(0.0f, 1.0f, params.body);
    op.stereoLink = params.stereoLink;
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
    const float ceiling = juce::Decibels::decibelsToGain(voiceMode ? -1.2f : -1.0f);

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
