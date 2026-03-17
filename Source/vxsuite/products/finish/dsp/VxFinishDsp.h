#pragma once

#include "../../polish/dsp/VxPolishCorrectiveStage.h"

#include <vector>

namespace vxsuite::finish {

class Dsp {
public:
    using Params = vxsuite::polish::SharedParams;

    void prepare(double sampleRate, int maxBlockSize, int numChannels);
    void setParams(const Params& params);
    void reset();
    void processCorrective(juce::AudioBuffer<float>& buffer);
    void processRecovery(juce::AudioBuffer<float>& buffer);
    void processLimiter(juce::AudioBuffer<float>& buffer);

    float getRecoveryActivity() const noexcept { return recoveryActivity; }
    float getCompActivity() const noexcept { return corrective.getCompActivity(); }
    float getLimiterActivity() const noexcept { return limiterActivity; }

private:
    Params params {};
    double sr = 44100.0;
    int channels = 0;

    float cDetectorA = 0.0f;
    float cLowA_voice = 0.0f;
    float cLowA_general = 0.0f;
    float cLowMidA_voice = 0.0f;
    float cLowMidA_general = 0.0f;
    float cPresenceLoA = 0.0f;
    float cPresenceHiA = 0.0f;
    float cAirLoA = 0.0f;

    float hiShelfB0 = 1.0f, hiShelfB1 = 0.0f, hiShelfA1 = 0.0f;
    std::vector<float> hiShelfZ1;

    std::vector<float> recoveryLowCh;
    std::vector<float> recoveryLowMidCh;
    std::vector<float> recoveryPresenceLoCh;
    std::vector<float> recoveryPresenceHiCh;
    std::vector<float> recoveryAirLoCh;
    float recoveryMonoLowLp = 0.0f;
    float recoveryMonoLowMidLp = 0.0f;
    float recoveryMonoPresenceLoLp = 0.0f;
    float recoveryMonoPresenceHiLp = 0.0f;
    float recoveryMonoAirLp = 0.0f;
    float recoveryInputEnv = 0.0f;
    float recoveryBodyEnv = 0.0f;
    float recoveryPresenceEnv = 0.0f;
    float recoveryAirEnv = 0.0f;

    float limitEnv = 0.0f;
    float limitGain = 1.0f;
    float recoveryActivity = 0.0f;
    float limiterActivity = 0.0f;

    vxsuite::polish::CorrectiveStage corrective;
};

} // namespace vxsuite::finish
