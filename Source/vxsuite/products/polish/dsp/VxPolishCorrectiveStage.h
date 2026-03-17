#pragma once

#include "VxPolishSharedParams.h"

#include <array>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>

namespace vxsuite::polish {

class CorrectiveStage {
public:
    void prepare(double sampleRate, int numChannels);
    void setParams(const SharedParams& newParams);
    void reset();
    void process(juce::AudioBuffer<float>& buffer);

    float getDeMudActivity() const noexcept { return deMudActivity; }
    float getDeEssActivity() const noexcept { return deEssActivity; }
    float getBreathActivity() const noexcept { return breathActivity; }
    float getPlosiveActivity() const noexcept { return plosiveActivity; }
    float getCompActivity() const noexcept { return compActivity; }
    float getTroubleActivity() const noexcept { return troubleActivity; }

private:
    SharedParams params {};
    double sr = 44100.0;
    int channels = 0;

    float cA180 = 0.0f;
    float cRmsA = 0.0f;
    float cDeMudAtk = 0.0f;
    float cDeMudRel = 0.0f;
    float cAEssVoice = 0.0f;
    float cAEssGeneral = 0.0f;
    float cDeEssAtk = 0.0f;
    float cDeEssRel = 0.0f;
    float cABreathVoice = 0.0f;
    float cABreathGeneral = 0.0f;
    float cBreathAtk = 0.0f;
    float cBreathRel = 0.0f;
    float cPlosiveFastA = 0.0f;
    float cPlosiveSlowA = 0.0f;
    float cPlosiveAtkA = 0.0f;
    float cPlosiveRelA = 0.0f;
    float cPlosiveGainSmooth = 0.0f;
    float cCompGainSmooth = 0.0f;
    float cTroubleRefA = 0.0f;
    float cTroubleAtk = 0.0f;
    float cTroubleRel = 0.0f;

    float hpfB0 = 1.0f, hpfB1 = 0.0f, hpfB2 = 0.0f, hpfA1 = 0.0f, hpfA2 = 0.0f;
    std::vector<float> hpfZ1, hpfZ2;

    std::array<float, 6> troubleDetBpfB0 {};
    std::array<float, 6> troubleDetBpfA1 {};
    std::array<float, 6> troubleDetBpfA2 {};
    float troubleRefLp = 0.0f;
    float troubleRefRms = 0.0f;
    std::array<float, 6> troubleBandZ1 {};
    std::array<float, 6> troubleBandZ2 {};
    std::array<float, 6> troubleBandRms {};

    float deMudEnv = 0.0f;
    float deMudDetMudZ1 = 0.0f;
    float deMudDetMudZ2 = 0.0f;
    float deMudDetRefZ1 = 0.0f;
    float deMudDetRefZ2 = 0.0f;
    float deMudMudRms = 0.0f;
    float deMudRefRms = 0.0f;
    std::vector<float> deMudEqZ1;
    std::vector<float> deMudEqZ2;
    float mudActBpfB0 = 0.0f;
    float mudActBpfA1 = 0.0f;
    float mudActBpfA2 = 0.0f;

    float deEssEnv = 0.0f;
    float deEssMonoLp = 0.0f;
    float breathEnv = 0.0f;
    float breathMonoLp = 0.0f;
    std::vector<float> deEssLpCh;
    std::vector<float> breathLpCh;
    std::array<std::vector<float>, 6> troubleEqZ1;
    std::array<std::vector<float>, 6> troubleEqZ2;

    float plosiveEnv = 0.0f;
    float plosiveFast = 0.0f;
    float plosiveSlow = 0.0f;
    float plosiveMonoLp = 0.0f;
    float compEnv = 0.0f;
    std::vector<float> plosiveLpCh;
    std::vector<float> plosiveGainCh;

    float deMudActivity = 0.0f;
    float deEssActivity = 0.0f;
    float breathActivity = 0.0f;
    float plosiveActivity = 0.0f;
    float compActivity = 0.0f;
    float troubleActivity = 0.0f;
};

} // namespace vxsuite::polish
