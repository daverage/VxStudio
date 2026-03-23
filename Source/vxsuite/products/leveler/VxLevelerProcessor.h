#pragma once

#include "../../framework/VxSuiteProcessorBase.h"
#include "dsp/VxLevelerDetector.h"
#include "dsp/VxLevelerDsp.h"

class VXLevelerAudioProcessor final : public vxsuite::ProcessorBase {
public:
    VXLevelerAudioProcessor();
    ~VXLevelerAudioProcessor() override = default;

    juce::String getStatusText() const override;
    int getActivityLightCount() const noexcept override;
    float getActivityLight(int index) const noexcept override;
    std::string_view getActivityLightLabel(int index) const noexcept override;
    void setDebugTuning(const vxsuite::leveler::Dsp::Tuning& tuning) noexcept;

protected:
    void prepareSuite(double sampleRate, int samplesPerBlock) override;
    void resetSuite() override;
    void processProduct(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

private:
    static vxsuite::ProductIdentity makeIdentity();

    vxsuite::leveler::Detector detector;
    vxsuite::leveler::Dsp dsp;
    double currentSampleRateHz = 48000.0;
    float smoothedLevel = 0.0f;
    float smoothedControl = 0.0f;
    bool controlsPrimed = false;
};
