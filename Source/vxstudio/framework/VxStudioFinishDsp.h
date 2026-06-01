#pragma once

#include "VxStudioBlockSmoothing.h"
#include "VxStudioOptoCompressorLA2A.h"
#include "VxStudioProcessOptions.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace vxsuite::finish {

class Dsp final {
public:
    struct Params final {
        int contentMode = 0;
        float peakReduction = 0.0f;
        float outputGainDb = 0.0f;
        float body = 0.5f;
        bool stereoLink = true;
    };

    void prepare(double sampleRate, int maxBlockSize, int numChannels);
    void setParams(const Params& p);
    void reset();
    void process(juce::AudioBuffer<float>& buffer, const ProcessOptions& options = {});

    float getCompActivity() const noexcept { return opto.getActivity01(); }
    float getGainReductionDb() const noexcept { return opto.getGainReductionDb(); }
    float getEnvelopeDb() const noexcept { return opto.getEnvelopeDb(); }
    float getLimiterActivity() const noexcept { return limiterActivity; }

private:
    void updateOptoParams(float outputGainDb);
    void processLimiter(juce::AudioBuffer<float>& buffer);

    Params params {};
    double sr = 44100.0;
    int channels = 0;
    float smoothedAutoMakeupDb = 0.0f;
    float smoothedRecoveryDb = 0.0f;
    float limitEnv = 0.0f;
    float limitGain = 1.0f;
    float limiterActivity = 0.0f;
    OptoCompressorLA2A opto;
};

} // namespace vxsuite::finish
