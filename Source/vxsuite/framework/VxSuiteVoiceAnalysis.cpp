#include "VxSuiteVoiceAnalysis.h"

#include <cmath>

namespace vxsuite {

float VoiceAnalysisState::onePoleAlpha(const double sampleRate, const float cutoffHz) noexcept {
    if (sampleRate <= 0.0 || cutoffHz <= 0.0f)
        return 0.0f;
    return std::exp(-2.0f * juce::MathConstants<float>::pi * cutoffHz / static_cast<float>(sampleRate));
}

void VoiceAnalysisState::prepare(const double sampleRate, const int /*maxSamplesPerBlock*/) {
    const double sr = sampleRate > 1000.0 ? sampleRate : 48000.0;
    alphaHp    = onePoleAlpha(sr, 120.0f);   // HP corner: removes DC/sub
    alphaBp    = onePoleAlpha(sr, 4200.0f);  // LP applied to HP output → bandpass
    alphaFast  = onePoleAlpha(sr, 22.0f);    // fast envelope: ~7 ms
    alphaSlow  = onePoleAlpha(sr, 3.3f);     // slow envelope: ~50 ms
    alphaVslow = onePoleAlpha(sr, 0.33f);    // noise floor: ~480 ms
    alphaStab  = onePoleAlpha(sr, 6.0f);     // stability smoother: ~26 ms
    alphaCenter = 0.84f;                     // block-rate center confidence smoothing
    alphaDir   = 0.88f;                      // block-rate directness smoothing
    reset();
}

void VoiceAnalysisState::reset() {
    hpState.fill(0.0f);
    bpState.fill(0.0f);
    envFast   = 0.0f;
    envSlow   = 0.0f;
    envVslow  = 1.0e-6f;
    stability  = 0.0f;
    centerCorr = 0.0f;
    directness = 0.0f;
    current    = {};
}

void VoiceAnalysisState::update(const juce::AudioBuffer<float>& input, const int numSamples) {
    if (numSamples <= 0)
        return;

    const int channels = std::min(input.getNumChannels(), 2);
    if (channels <= 0)
        return;

    const auto* chL = input.getReadPointer(0);
    const auto* chR = (channels > 1) ? input.getReadPointer(1) : chL;

    float hpL = hpState[0], bpL = bpState[0];
    float hpR = hpState[1], bpR = bpState[1];
    float eF = envFast, eS = envSlow, eVS = envVslow;
    float stab = stability, ctr = centerCorr, dir = directness;

    double dotLR = 0.0, energyL = 0.0, energyR = 0.0;

    for (int i = 0; i < numSamples; ++i) {
        // Speech-band bandpass: HP at 120 Hz, then LP at 4200 Hz
        hpL = alphaHp * hpL + (1.0f - alphaHp) * chL[i];
        hpR = alphaHp * hpR + (1.0f - alphaHp) * chR[i];
        bpL = alphaBp * bpL + (1.0f - alphaBp) * (chL[i] - hpL);
        bpR = alphaBp * bpR + (1.0f - alphaBp) * (chR[i] - hpR);

        const float monoSpeech = (bpL + bpR) * 0.5f;
        const float absm = std::abs(monoSpeech);

        // Envelope trackers
        eF  = alphaFast * eF  + (1.0f - alphaFast) * absm;
        eS  = alphaSlow * eS  + (1.0f - alphaSlow) * absm;
        // Noise floor: minimum-statistics tracker — only follows signal downward.
        // This lets it stay near zero during speech while converging quickly to
        // the true noise floor during quiet passages.
        if (absm < eVS)
            eVS = alphaVslow * eVS + (1.0f - alphaVslow) * absm;

        // Per-sample stability: 1 - transient risk
        const float tRisk = juce::jlimit(0.0f, 1.0f, eF / (eS + 1.0e-8f) - 1.0f);
        stab = alphaStab * stab + (1.0f - alphaStab) * (1.0f - tRisk);

        // Stereo correlation accumulation
        dotLR   += static_cast<double>(bpL) * bpR;
        energyL += static_cast<double>(bpL) * bpL;
        energyR += static_cast<double>(bpR) * bpR;
    }

    // Save filter states
    hpState[0] = hpL; bpState[0] = bpL;
    hpState[1] = hpR; bpState[1] = bpR;
    envFast    = eF;
    envSlow    = eS;
    envVslow   = eVS;
    stability  = stab;

    // Block-rate center confidence
    const float cBlock = static_cast<float>(dotLR / std::sqrt(std::max(1.0e-12, energyL * energyR)));
    ctr = alphaCenter * ctr + (1.0f - alphaCenter) * juce::jlimit(0.0f, 1.0f, cBlock);
    centerCorr = ctr;

    // Directness: fast envelope relative to slow (high when attack > decay)
    const float dBlock = juce::jlimit(0.0f, 1.0f, eF / (eS + 1.0e-8f));
    dir = alphaDir * dir + (1.0f - alphaDir) * dBlock;
    directness = dir;

    // Snapshot fields
    const float noiseRef = eVS * 4.0f + 1.0e-7f;
    const float presence = juce::jlimit(0.0f, 1.0f, (eF - noiseRef) / (noiseRef * 8.0f + 1.0e-7f));
    const float tailLik  = juce::jlimit(0.0f, 1.0f, (eS - eF) / (eS + 1.0e-8f));
    const float tRiskFinal = juce::jlimit(0.0f, 1.0f, eF / (eS + 1.0e-8f) - 1.0f);
    const float rawEnergy = juce::jlimit(0.0f, 1.0f, eF / 0.5f);
    const float protect = juce::jlimit(0.0f, 1.0f,
        presence * (0.4f * stab + 0.35f * ctr + 0.25f * (1.0f - tailLik)));

    current.speechPresence   = presence;
    current.speechStability  = stab;
    current.speechBandEnergy = rawEnergy;
    current.directness       = dir;
    current.tailLikelihood   = tailLik;
    current.centerConfidence = ctr;
    current.transientRisk    = tRiskFinal;
    current.protectVoice     = protect;
}

} // namespace vxsuite
