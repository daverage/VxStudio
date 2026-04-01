#pragma once

#include "../../OptoComp/OptoCompressorLA2A.h"
#include <juce_audio_basics/juce_audio_basics.h>

namespace vxsuite::finish {

class Dsp final {
public:
    struct Params final {
        int contentMode = 0;         // 0 = vocal, 1 = general
        float peakReduction = 0.0f;  // 0..1
        float outputGainDb = 0.0f;   // dB
        float body = 0.5f;           // 0..1
    };

    void prepare(double sampleRate, int maxBlockSize, int numChannels);
    void setParams(const Params& p);
    void reset();
    void process(juce::AudioBuffer<float>& buffer);

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
    float limitEnv = 0.0f;
    float limitGain = 1.0f;
    float limiterActivity = 0.0f;

    OptoCompressorLA2A opto;
};

} // namespace vxsuite::finish
