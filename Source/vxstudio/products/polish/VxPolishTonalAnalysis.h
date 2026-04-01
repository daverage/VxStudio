#pragma once

#include <cmath>

#include <juce_audio_basics/juce_audio_basics.h>

namespace vxsuite::polish {

struct TonalAnalysisState {
    float lowLp = 0.0f;
    float lowMidLp = 0.0f;
    float presenceLoLp = 0.0f;
    float presenceHiLp = 0.0f;
    float airLp = 0.0f;
    float inputEnv = 0.0f;
    float lowMidEnv = 0.0f;
    float presenceEnv = 0.0f;
    float airEnv = 0.0f;
    float noiseFloorDb = -80.0f;

    void reset() noexcept {
        lowLp = 0.0f;
        lowMidLp = 0.0f;
        presenceLoLp = 0.0f;
        presenceHiLp = 0.0f;
        airLp = 0.0f;
        inputEnv = 0.0f;
        lowMidEnv = 0.0f;
        presenceEnv = 0.0f;
        airEnv = 0.0f;
        noiseFloorDb = -80.0f;
    }
};

inline float tonalOnePoleCoeff(const double sampleRate, const float cutoffHz) {
    if (sampleRate <= 0.0 || cutoffHz <= 0.0f)
        return 0.0f;
    return std::exp(-2.0f * juce::MathConstants<float>::pi * cutoffHz / static_cast<float>(sampleRate));
}

inline void updateTonalAnalysis(TonalAnalysisState& state,
                                const juce::AudioBuffer<float>& buffer,
                                const double sampleRate,
                                const int numSamples) {
    const int channels = buffer.getNumChannels();
    if (channels <= 0 || numSamples <= 0)
        return;

    const float lowA = tonalOnePoleCoeff(sampleRate, 180.0f);
    const float lowMidA = tonalOnePoleCoeff(sampleRate, 850.0f);
    const float presenceLoA = tonalOnePoleCoeff(sampleRate, 1800.0f);
    const float presenceHiA = tonalOnePoleCoeff(sampleRate, 5200.0f);
    const float airA = tonalOnePoleCoeff(sampleRate, 7200.0f);
    const float envA = std::exp(-1.0f / (0.040f * static_cast<float>(sampleRate)));
    const float noiseA = std::exp(-1.0f / (0.500f * static_cast<float>(sampleRate)));

    for (int i = 0; i < numSamples; ++i) {
        float mono = 0.0f;
        for (int ch = 0; ch < channels; ++ch)
            mono += buffer.getReadPointer(ch)[i];
        mono /= static_cast<float>(channels);

        state.lowLp = lowA * state.lowLp + (1.0f - lowA) * mono;
        state.lowMidLp = lowMidA * state.lowMidLp + (1.0f - lowMidA) * mono;
        state.presenceLoLp = presenceLoA * state.presenceLoLp + (1.0f - presenceLoA) * mono;
        state.presenceHiLp = presenceHiA * state.presenceHiLp + (1.0f - presenceHiA) * mono;
        state.airLp = airA * state.airLp + (1.0f - airA) * mono;

        const float lowMidBand = state.lowMidLp - state.lowLp;
        const float presenceBand = state.presenceHiLp - state.presenceLoLp;
        const float airBand = mono - state.airLp;
        const float monoAbs = std::abs(mono);

        state.inputEnv = envA * state.inputEnv + (1.0f - envA) * monoAbs;
        state.lowMidEnv = envA * state.lowMidEnv + (1.0f - envA) * std::abs(lowMidBand);
        state.presenceEnv = envA * state.presenceEnv + (1.0f - envA) * std::abs(presenceBand);
        state.airEnv = envA * state.airEnv + (1.0f - envA) * std::abs(airBand);

        const float envDb = juce::Decibels::gainToDecibels(state.inputEnv + 1.0e-6f, -120.0f);
        if (envDb < state.noiseFloorDb)
            state.noiseFloorDb = noiseA * state.noiseFloorDb + (1.0f - noiseA) * envDb;
        else
            state.noiseFloorDb = 0.9995f * state.noiseFloorDb + 0.0005f * envDb;
    }
}

} // namespace vxsuite::polish
