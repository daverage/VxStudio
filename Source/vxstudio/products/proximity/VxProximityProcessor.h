#pragma once

#include "../../framework/VxStudioBlockSmoothedControl.h"
#include "../../framework/VxStudioBlockSmoothing.h"
#include "../../framework/VxStudioEditorBase.h"
#include "../../framework/VxStudioOutputTrimmer.h"
#include "../../framework/VxStudioProcessorBase.h"
#include "../deverb/dsp/VxDeverbSpectralProcessor.h"
#include "dsp/VxProximityDsp.h"

class VXProximityAudioProcessor final : public vxsuite::ProcessorBase {
public:
    VXProximityAudioProcessor();
    ~VXProximityAudioProcessor() override = default;

    juce::String getStatusText() const override;
    float getLocalOutputTrimMaxReductionDb() const noexcept { return outputTrimmer.getMaxObservedReductionDb(); }
    vxsuite::MeteringSnapshot getMeteringSnapshot() const noexcept override;
    void setLastProximityGainDb(float db) noexcept { lastProximityGainDb = db; }

protected:
    void prepareSuite(double sampleRate, int samplesPerBlock) override;
    void resetSuite() override;
    void processProduct(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void renderListenOutput(juce::AudioBuffer<float>& outputBuffer,
                            const juce::AudioBuffer<float>& inputBuffer) override;

private:
    static vxsuite::ProductIdentity makeIdentity();

    vxsuite::proximity::ProximityDsp proximityDsp;
    // Direct-to-reverberant "dryness" pre-stage: reduces room-tone/reverberant
    // energy on the raw input before the physics EQ, so "closer" simulates the
    // room pickup dropping off, not just a bass-boost EQ curve. Introduces the
    // stage's algorithmic latency (reported via setReportedLatencySamples;
    // ProcessorBase's processCoordinator PDC-aligns the listen dry buffer to it
    // automatically — see VxDeverbSpectralProcessor.h for the LRSV algorithm).
    vxsuite::deverb::SpectralProcessor drynessDsp;
    vxsuite::BlockSmoothedControlTriple controls;
    vxsuite::OutputTrimmer outputTrimmer;
    double currentSampleRateHz = 48000.0;
    float lastProximityGainDb = 0.0f;
};
