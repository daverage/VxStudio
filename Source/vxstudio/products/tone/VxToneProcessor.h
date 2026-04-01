#pragma once

#include "../../framework/VxStudioProcessorBase.h"
#include "../../framework/VxStudioBlockSmoothedControl.h"

#include <array>
#include <vector>

class VXToneAudioProcessor final : public vxsuite::ProcessorBase {
public:
    VXToneAudioProcessor();
    juce::String getStatusText() const override;

protected:
    void prepareSuite(double sampleRate, int samplesPerBlock) override;
    void resetSuite() override;
    void processProduct(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void renderListenOutput(juce::AudioBuffer<float>& outputBuffer,
                            const juce::AudioBuffer<float>& inputBuffer) override;

private:
    static vxsuite::ProductIdentity makeIdentity();

    struct BiquadCoeffs {
        float b0 = 1.f, b1 = 0.f, b2 = 0.f, a1 = 0.f, a2 = 0.f;
    };

    struct BiquadState {
        float x1 = 0.f, x2 = 0.f, y1 = 0.f, y2 = 0.f;
    };

    static BiquadCoeffs lowShelfCoeffs (double sampleRate, float gainDb, float freqHz) noexcept;
    static BiquadCoeffs highShelfCoeffs(double sampleRate, float gainDb, float freqHz) noexcept;
    static void applyBiquad(float* samples, int numSamples,
                            const BiquadCoeffs& c, BiquadState& s) noexcept;

    double currentSampleRateHz = 48000.0;
    std::vector<BiquadState> bassState;
    std::vector<BiquadState> trebleState;
    vxsuite::BlockSmoothedControlPair controls;
};
