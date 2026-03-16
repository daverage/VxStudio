#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <vector>

namespace vxsuite::polish {

class Dsp {
public:
    struct Params {
        float deMud = 0.0f;
        float deEss = 0.0f;
        float plosive = 0.0f;
        float compress = 0.0f;
        float troubleSmooth = 0.0f;
        float limit = 0.0f;
        float recovery = 0.0f;
        float voicePreserve = 0.75f;
        float denoiseAmount = 0.0f;
        float artifactRisk = 0.0f;
        float compSidechainBoostDb = 0.0f;
        int contentMode = 0;
        int sourcePreset = 0;
        float speechLoudnessDb = -30.0f;
        float proximityContext = 0.0f;
        float speechPresence = 0.5f;
        float noiseFloorDb = -80.0f;
    };

    void prepare(double sampleRate, int maxBlockSize, int numChannels);
    void setParams(const Params& newParams);
    void reset();
    void process(juce::AudioBuffer<float>& buffer);
    void processCorrective(juce::AudioBuffer<float>& buffer);
    void processRecovery(juce::AudioBuffer<float>& buffer);
    void processLimiter(juce::AudioBuffer<float>& buffer);
    bool isActive() const noexcept { return activeThisBlock; }
    float getCorrectiveReductionDb() const noexcept { return correctiveReductionDb; }
    float getRecoveryLiftDb() const noexcept { return recoveryLiftDb; }
    float getLimiterReductionDb() const noexcept { return limiterReductionDb; }
    float getTotalReductionDb() const noexcept { return correctiveReductionDb + limiterReductionDb - recoveryLiftDb; }
    float getDeMudActivity() const noexcept { return deMudActivity; }
    float getDeEssActivity() const noexcept { return deEssActivity; }
    float getPlosiveActivity() const noexcept { return plosiveActivity; }

private:
    Params params{};
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
    float cPlosiveFastA = 0.0f;
    float cPlosiveSlowA = 0.0f;
    float cPlosiveAtkA = 0.0f;
    float cPlosiveRelA = 0.0f;
    float cPlosiveGainSmooth = 0.0f;
    float cCompGainSmooth = 0.0f;
    float cDetectorA = 0.0f;
    float cLowA_voice = 0.0f;
    float cLowA_general = 0.0f;
    float cLowMidA_voice = 0.0f;
    float cLowMidA_general = 0.0f;
    float cPresenceLoA = 0.0f;
    float cPresenceHiA = 0.0f;
    float cAirLoA = 0.0f;
    float cLimiterGainSmooth = 0.0f;

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
    std::vector<float> deEssLpCh;
    std::array<std::vector<float>, 6> troubleEqZ1;
    std::array<std::vector<float>, 6> troubleEqZ2;

    float plosiveEnv = 0.0f;
    float plosiveFast = 0.0f;
    float plosiveSlow = 0.0f;
    float plosiveMonoLp = 0.0f;
    std::vector<float> plosiveLpCh;
    std::vector<float> plosiveGainCh;
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

    float compEnv = 0.0f;
    float compGain = 1.0f;
    float limitEnv = 0.0f;
    float limitGain = 1.0f;

    bool activeThisBlock = false;
    float correctiveReductionDb = 0.0f;
    float recoveryLiftDb = 0.0f;
    float limiterReductionDb = 0.0f;
    float deMudActivity = 0.0f;
    float deEssActivity = 0.0f;
    float plosiveActivity = 0.0f;
};

} // namespace vxsuite::polish
