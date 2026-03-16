#include "VxPolishDsp.h"

#include <algorithm>
#include <cmath>

namespace vxsuite::polish {
namespace {

inline float onePoleCoeff(const double sr, const float hz) {
    if (sr <= 0.0 || hz <= 0.0f)
        return 0.0f;
    const float a = std::exp(-2.0f * juce::MathConstants<float>::pi * hz / static_cast<float>(sr));
    return juce::jlimit(0.0f, 0.99999f, a);
}

struct BiquadCoeffs {
    float b0 = 1.0f;
    float b1 = 0.0f;
    float b2 = 0.0f;
    float a1 = 0.0f;
    float a2 = 0.0f;
};

inline BiquadCoeffs makeBandpass(const double sr, const float centerHz, const float q) {
    BiquadCoeffs c{};
    if (sr <= 0.0 || centerHz <= 0.0f || q <= 0.0f)
        return c;
    const float w0 = 2.0f * juce::MathConstants<float>::pi * centerHz / static_cast<float>(sr);
    const float cw = std::cos(w0);
    const float sw = std::sin(w0);
    const float alpha = sw / (2.0f * q);
    const float a0 = 1.0f + alpha;
    const float invA0 = 1.0f / std::max(1.0e-12f, a0);
    c.b0 = alpha * invA0;
    c.b1 = 0.0f;
    c.b2 = -alpha * invA0;
    c.a1 = (-2.0f * cw) * invA0;
    c.a2 = (1.0f - alpha) * invA0;
    return c;
}

inline BiquadCoeffs makePeakingEq(const double sr, const float centerHz, const float q, const float gainDb) {
    BiquadCoeffs c{};
    if (sr <= 0.0 || centerHz <= 0.0f || q <= 0.0f)
        return c;
    const float a = std::pow(10.0f, gainDb / 40.0f);
    const float w0 = 2.0f * juce::MathConstants<float>::pi * centerHz / static_cast<float>(sr);
    const float cw = std::cos(w0);
    const float sw = std::sin(w0);
    const float alpha = sw / (2.0f * q);
    const float a0 = 1.0f + alpha / a;
    const float invA0 = 1.0f / std::max(1.0e-12f, a0);
    c.b0 = (1.0f + alpha * a) * invA0;
    c.b1 = (-2.0f * cw) * invA0;
    c.b2 = (1.0f - alpha * a) * invA0;
    c.a1 = (-2.0f * cw) * invA0;
    c.a2 = (1.0f - alpha / a) * invA0;
    return c;
}

inline float processBiquadDf2(const float x, const BiquadCoeffs& c, float& z1, float& z2) {
    const float y = c.b0 * x + z1;
    z1 = c.b1 * x - c.a1 * y + z2;
    z2 = c.b2 * x - c.a2 * y;
    return y;
}

inline float processBiquadDf2(const float x, const float b0, const float b1, const float b2,
                              const float a1, const float a2, float& z1, float& z2) {
    const float y = b0 * x + z1;
    z1 = b1 * x - a1 * y + z2;
    z2 = b2 * x - a2 * y;
    return y;
}

} // namespace

void Dsp::prepare(double sampleRate, int, int numChannels) {
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    channels = std::max(1, numChannels);

    const float fsr = static_cast<float>(sr);
    cA180 = onePoleCoeff(sr, 180.0f);
    cRmsA = std::exp(-1.0f / (0.008f * fsr));
    cDeMudAtk = std::exp(-1.0f / (0.050f * fsr));
    cDeMudRel = std::exp(-1.0f / (0.200f * fsr));
    cAEssVoice = onePoleCoeff(sr, 5800.0f);
    cAEssGeneral = onePoleCoeff(sr, 6200.0f);
    cDeEssAtk = std::exp(-1.0f / (0.004f * fsr));
    cDeEssRel = std::exp(-1.0f / (0.075f * fsr));
    cPlosiveFastA = std::exp(-1.0f / (0.003f * fsr));
    cPlosiveSlowA = std::exp(-1.0f / (0.080f * fsr));
    cPlosiveAtkA = std::exp(-1.0f / (0.002f * fsr));
    cPlosiveRelA = std::exp(-1.0f / (0.100f * fsr));
    cPlosiveGainSmooth = std::exp(-1.0f / (0.010f * fsr));
    cCompGainSmooth = std::exp(-1.0f / (0.010f * fsr));
    cDetectorA = std::exp(-1.0f / (0.030f * fsr));
    cLowA_voice = onePoleCoeff(sr, 380.0f);
    cLowA_general = onePoleCoeff(sr, 320.0f);
    cLowMidA_voice = onePoleCoeff(sr, 820.0f);
    cLowMidA_general = onePoleCoeff(sr, 700.0f);
    cPresenceLoA = onePoleCoeff(sr, 1800.0f);
    cPresenceHiA = onePoleCoeff(sr, 5200.0f);
    cAirLoA = onePoleCoeff(sr, 6400.0f);
    cLimiterGainSmooth = std::exp(-1.0f / (0.0020f * fsr));
    {
        const BiquadCoeffs mudBpf = makeBandpass(sr, 300.0f, 1.2f);
        mudActBpfB0 = mudBpf.b0;
        mudActBpfA1 = mudBpf.a1;
        mudActBpfA2 = mudBpf.a2;
    }
    deMudEqZ1.assign(static_cast<size_t>(channels), 0.0f);
    deMudEqZ2.assign(static_cast<size_t>(channels), 0.0f);
    deEssLpCh.assign(static_cast<size_t>(channels), 0.0f);
    for (size_t b = 0; b < troubleEqZ1.size(); ++b) {
        troubleEqZ1[b].assign(static_cast<size_t>(channels), 0.0f);
        troubleEqZ2[b].assign(static_cast<size_t>(channels), 0.0f);
    }
    plosiveLpCh.assign(static_cast<size_t>(channels), 0.0f);
    plosiveGainCh.assign(static_cast<size_t>(channels), 1.0f);
    recoveryLowCh.assign(static_cast<size_t>(channels), 0.0f);
    recoveryLowMidCh.assign(static_cast<size_t>(channels), 0.0f);
    recoveryPresenceLoCh.assign(static_cast<size_t>(channels), 0.0f);
    recoveryPresenceHiCh.assign(static_cast<size_t>(channels), 0.0f);
    recoveryAirLoCh.assign(static_cast<size_t>(channels), 0.0f);
    reset();
}

void Dsp::setParams(const Params& newParams) {
    params = newParams;
}

void Dsp::reset() {
    deMudEnv = 0.0f;
    deMudDetMudZ1 = 0.0f;
    deMudDetMudZ2 = 0.0f;
    deMudDetRefZ1 = 0.0f;
    deMudDetRefZ2 = 0.0f;
    deMudMudRms = 0.0f;
    deMudRefRms = 0.0f;
    std::fill(deMudEqZ1.begin(), deMudEqZ1.end(), 0.0f);
    std::fill(deMudEqZ2.begin(), deMudEqZ2.end(), 0.0f);
    for (size_t b = 0; b < troubleEqZ1.size(); ++b) {
        std::fill(troubleEqZ1[b].begin(), troubleEqZ1[b].end(), 0.0f);
        std::fill(troubleEqZ2[b].begin(), troubleEqZ2[b].end(), 0.0f);
    }

    deEssEnv = 0.0f;
    deEssMonoLp = 0.0f;
    std::fill(deEssLpCh.begin(), deEssLpCh.end(), 0.0f);

    plosiveEnv = 0.0f;
    plosiveFast = 0.0f;
    plosiveSlow = 0.0f;
    plosiveMonoLp = 0.0f;
    std::fill(plosiveLpCh.begin(), plosiveLpCh.end(), 0.0f);
    std::fill(plosiveGainCh.begin(), plosiveGainCh.end(), 1.0f);
    std::fill(recoveryLowCh.begin(), recoveryLowCh.end(), 0.0f);
    std::fill(recoveryLowMidCh.begin(), recoveryLowMidCh.end(), 0.0f);
    std::fill(recoveryPresenceLoCh.begin(), recoveryPresenceLoCh.end(), 0.0f);
    std::fill(recoveryPresenceHiCh.begin(), recoveryPresenceHiCh.end(), 0.0f);
    std::fill(recoveryAirLoCh.begin(), recoveryAirLoCh.end(), 0.0f);
    recoveryMonoLowLp = 0.0f;
    recoveryMonoLowMidLp = 0.0f;
    recoveryMonoPresenceLoLp = 0.0f;
    recoveryMonoPresenceHiLp = 0.0f;
    recoveryMonoAirLp = 0.0f;
    recoveryInputEnv = 0.0f;
    recoveryBodyEnv = 0.0f;
    recoveryPresenceEnv = 0.0f;
    recoveryAirEnv = 0.0f;
    compEnv = 0.0f;
    compGain = 1.0f;
    limitEnv = 0.0f;
    limitGain = 1.0f;
    activeThisBlock = false;
    correctiveReductionDb = 0.0f;
    recoveryLiftDb = 0.0f;
    limiterReductionDb = 0.0f;
    deMudActivity = 0.0f;
    deEssActivity = 0.0f;
    plosiveActivity = 0.0f;
}

void Dsp::process(juce::AudioBuffer<float>& buffer) {
    processCorrective(buffer);
    processRecovery(buffer);
    processLimiter(buffer);
}

void Dsp::processCorrective(juce::AudioBuffer<float>& buffer) {
    const int numChannels = std::min(channels, buffer.getNumChannels());
    const int numSamples = buffer.getNumSamples();
    if (numChannels <= 0 || numSamples <= 0)
        return;

    const float deMudAmt = juce::jlimit(0.0f, 1.0f, params.deMud);
    const float deEssAmt = juce::jlimit(0.0f, 1.0f, params.deEss);
    const float plosiveAmt = juce::jlimit(0.0f, 1.0f, params.plosive);
    const float compAmt = juce::jlimit(0.0f, 1.0f, params.compress);
    const float troubleAmt = juce::jlimit(0.0f, 1.0f, params.troubleSmooth);
    const float proxCtx = juce::jlimit(0.0f, 1.0f, params.proximityContext);
    const float speechPresence = juce::jlimit(0.0f, 1.0f, params.speechPresence);
    const float loudNorm = juce::jlimit(0.0f, 1.0f, (params.speechLoudnessDb + 48.0f) / 42.0f);
    const bool voiceMode = params.contentMode == 0;

    activeThisBlock = false;
    correctiveReductionDb = 0.0f;
    deMudActivity = 0.0f;
    deEssActivity = 0.0f;
    plosiveActivity = 0.0f;
    if (deMudAmt <= 1.0e-6f && deEssAmt <= 1.0e-6f && plosiveAmt <= 1.0e-6f
        && compAmt <= 1.0e-6f && troubleAmt <= 1.0e-6f)
        return;
    activeThisBlock = true;
    std::array<BiquadCoeffs, 6> troubleCoeffs{};
    if (troubleAmt > 1.0e-6f) {
        const std::array<float, 6> centers { 1400.0f, 2400.0f, 3600.0f, 5200.0f, 7600.0f, 10500.0f };
        const std::array<float, 6> qVals { 1.10f, 1.20f, 1.20f, 1.15f, 1.05f, 0.90f };
        const std::array<float, 6> maxCutDb { 3.0f, 4.5f, 5.5f, 6.0f, 5.5f, 4.5f };
        const float smoothCtx = juce::jlimit(0.90f, 1.35f, 1.00f + 0.30f * params.artifactRisk - 0.03f * speechPresence);
        for (size_t b = 0; b < troubleCoeffs.size(); ++b) {
            const float cutDb = -maxCutDb[b] * troubleAmt * smoothCtx;
            troubleCoeffs[b] = makePeakingEq(sr, centers[b], qVals[b], cutDb);
        }
    }

    const float a180 = cA180;
    const BiquadCoeffs mudDetBpf = makeBandpass(sr, 300.0f, 0.8f);
    const BiquadCoeffs refDetBpf = makeBandpass(sr, 2000.0f, 0.7f);
    const float rmsA = cRmsA;
    const float aEss = voiceMode ? cAEssVoice : cAEssGeneral;

    const float deMudAtk = cDeMudAtk;
    const float deMudRel = cDeMudRel;
    const float deMudThresholdDb = (voiceMode ? 1.5f : 2.5f) + 2.0f * loudNorm - 1.2f * proxCtx;
    const float deMudRangeDb = 10.0f;
    const float deMudMaxCutDb = (voiceMode ? 12.0f : 9.0f) * deMudAmt;
    const float maxMudLinGain = (deMudAmt > 1.0e-6f)
        ? juce::Decibels::decibelsToGain(-deMudMaxCutDb)
        : 1.0f;

    const float deEssAtk = cDeEssAtk;
    const float deEssRel = cDeEssRel;
    const float deEssThreshold = juce::jlimit(0.16f, 0.42f, (voiceMode ? 0.22f : 0.28f) + 0.08f * loudNorm - 0.06f * proxCtx);
    const float deEssMaxCutDb = (voiceMode ? 10.0f : 7.5f) * deEssAmt;

    const float plosiveFastLocal = cPlosiveFastA;
    const float plosiveSlowLocal = cPlosiveSlowA;
    const float plosiveAtk = cPlosiveAtkA;
    const float plosiveRel = cPlosiveRelA;
    const float plosiveMaxCutDb = (voiceMode ? 8.0f : 6.0f) * plosiveAmt * (0.85f + 0.35f * proxCtx);

    const float compAtk = std::exp(-1.0f / ((0.030f - 0.014f * compAmt) * static_cast<float>(sr)));
    const float compRel = std::exp(-1.0f / ((0.320f - 0.140f * compAmt) * static_cast<float>(sr)));
    const float compThresholdDb = -14.0f - 6.0f * compAmt;
    const float compRatio = 1.0f + 2.2f * compAmt;
    const float compMakeupDb = 0.7f * compAmt;
    const float compGainSmooth = cCompGainSmooth;
    const float compSidechainBoost = juce::Decibels::decibelsToGain(juce::jlimit(0.0f, 18.0f, params.compSidechainBoostDb));

    float reductionAcc = 0.0f;
    float deMudAcc = 0.0f;
    float deEssAcc = 0.0f;
    float plosiveAcc = 0.0f;
    for (int i = 0; i < numSamples; ++i) {
        float monoLinear = 0.0f;
        float monoAbs = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch) {
            const float s = buffer.getReadPointer(ch)[i];
            monoLinear += s;
            monoAbs += std::abs(s);
        }
        monoLinear /= static_cast<float>(numChannels);
        monoAbs /= static_cast<float>(numChannels);

        plosiveMonoLp = a180 * plosiveMonoLp + (1.0f - a180) * monoAbs;
        const float lowAbs = std::abs(plosiveMonoLp);
        plosiveFast = plosiveFastLocal * plosiveFast + (1.0f - plosiveFastLocal) * lowAbs;
        plosiveSlow = plosiveSlowLocal * plosiveSlow + (1.0f - plosiveSlowLocal) * lowAbs;
        const float burstGate = 1.12f + 0.22f * loudNorm - 0.12f * proxCtx;
        const float burst = std::max(0.0f, (plosiveFast - burstGate * plosiveSlow) / (plosiveSlow + 1.0e-6f));
        const float plosiveTarget = juce::jlimit(0.0f, 1.0f, burst) * plosiveAmt;
        const float plosiveA = plosiveTarget > plosiveEnv ? plosiveAtk : plosiveRel;
        plosiveEnv = plosiveA * plosiveEnv + (1.0f - plosiveA) * plosiveTarget;
        const float plosiveGain = juce::Decibels::decibelsToGain(-plosiveMaxCutDb * plosiveEnv);

        const float mudDet = processBiquadDf2(monoLinear, mudDetBpf, deMudDetMudZ1, deMudDetMudZ2);
        const float refDet = processBiquadDf2(monoLinear, refDetBpf, deMudDetRefZ1, deMudDetRefZ2);
        deMudMudRms = rmsA * deMudMudRms + (1.0f - rmsA) * (mudDet * mudDet);
        deMudRefRms = rmsA * deMudRefRms + (1.0f - rmsA) * (refDet * refDet);
        const float ratio = (deMudMudRms + 1.0e-12f) / (deMudRefRms + 1.0e-12f);
        const float ratioDb = 10.0f * std::log10(std::max(1.0e-12f, ratio));
        const float excessDb = std::max(0.0f, ratioDb - deMudThresholdDb);
        const float deMudTarget = juce::jlimit(0.0f, 1.0f, excessDb / deMudRangeDb) * deMudAmt;
        const float deMudA = deMudTarget > deMudEnv ? deMudAtk : deMudRel;
        deMudEnv = deMudA * deMudEnv + (1.0f - deMudA) * deMudTarget;
        const float deMudGain = juce::Decibels::decibelsToGain(-deMudMaxCutDb * deMudEnv);

        deEssMonoLp = aEss * deEssMonoLp + (1.0f - aEss) * monoAbs;
        const float monoHf = monoAbs - deEssMonoLp;
        const float hfRatio = std::abs(monoHf) / (std::abs(monoAbs) + 1.0e-6f);
        const float deEssCeil = juce::jlimit(0.48f, 0.72f, 0.55f + 0.10f * (1.0f - speechPresence));
        const float deEssNorm = (hfRatio - deEssThreshold) / (deEssCeil - deEssThreshold);
        const float deEssTarget = juce::jlimit(0.0f, 1.0f, deEssNorm) * deEssAmt;
        const float deEssA = deEssTarget > deEssEnv ? deEssAtk : deEssRel;
        deEssEnv = deEssA * deEssEnv + (1.0f - deEssA) * deEssTarget;
        const float deEssGain = juce::Decibels::decibelsToGain(-deEssMaxCutDb * deEssEnv);
        deMudAcc += deMudEnv;
        deEssAcc += deEssEnv;
        plosiveAcc += plosiveEnv;

        const float compIn = std::abs(monoAbs) * compSidechainBoost;
        const float compA = compIn > compEnv ? compAtk : compRel;
        compEnv = compA * compEnv + (1.0f - compA) * compIn;
        const float compEnvDb = 20.0f * std::log10(compEnv + 1.0e-6f);
        float gainReductionDb = 0.0f;
        if (compEnvDb > compThresholdDb)
            gainReductionDb = (1.0f - 1.0f / compRatio) * (compEnvDb - compThresholdDb);
        const float compTargetGain = juce::Decibels::decibelsToGain(-gainReductionDb + compMakeupDb);
        compGain = compGainSmooth * compGain + (1.0f - compGainSmooth) * compTargetGain;
        const float deMudDb = std::max(0.0f, -juce::Decibels::gainToDecibels(std::max(deMudGain, 1.0e-6f), -120.0f));
        const float deEssDb = std::max(0.0f, -juce::Decibels::gainToDecibels(std::max(deEssGain, 1.0e-6f), -120.0f));
        const float plosiveDb = std::max(0.0f, -juce::Decibels::gainToDecibels(std::max(plosiveGain, 1.0e-6f), -120.0f));
        const float compDb = std::max(0.0f, -juce::Decibels::gainToDecibels(std::max(compGain, 1.0e-6f), -120.0f));
        const float troubleDb = 2.25f * troubleAmt;
        reductionAcc += 0.20f * (deMudDb + deEssDb + plosiveDb + compDb + troubleDb);

        for (int ch = 0; ch < numChannels; ++ch) {
            auto* d = buffer.getWritePointer(ch);
            float x = d[i];

            float plLp = plosiveLpCh[static_cast<size_t>(ch)];
            plLp = a180 * plLp + (1.0f - a180) * x;
            plosiveLpCh[static_cast<size_t>(ch)] = plLp;
            float gain = plosiveGainCh[static_cast<size_t>(ch)];
            gain = cPlosiveGainSmooth * gain + (1.0f - cPlosiveGainSmooth) * plosiveGain;
            gain = juce::jlimit(0.25f, 1.0f, gain);
            plosiveGainCh[static_cast<size_t>(ch)] = gain;
            x -= plLp * (1.0f - gain);

            {
                const float mudBand = processBiquadDf2(x, mudActBpfB0, 0.0f, -mudActBpfB0,
                                                       mudActBpfA1, mudActBpfA2,
                                                       deMudEqZ1[static_cast<size_t>(ch)],
                                                       deMudEqZ2[static_cast<size_t>(ch)]);
                const float mudLinGain = 1.0f + deMudEnv * (maxMudLinGain - 1.0f);
                x -= mudBand * (1.0f - mudLinGain);
            }

            float essLp = deEssLpCh[static_cast<size_t>(ch)];
            essLp = aEss * essLp + (1.0f - aEss) * x;
            deEssLpCh[static_cast<size_t>(ch)] = essLp;
            const float highBand = x - essLp;
            x = essLp + highBand * deEssGain;

            x *= compGain;
            if (troubleAmt > 1.0e-6f) {
                for (size_t b = 0; b < troubleCoeffs.size(); ++b) {
                    x = processBiquadDf2(x, troubleCoeffs[b],
                                         troubleEqZ1[b][static_cast<size_t>(ch)],
                                         troubleEqZ2[b][static_cast<size_t>(ch)]);
                }
            }

            d[i] = x;
        }
    }
    correctiveReductionDb = reductionAcc / static_cast<float>(numSamples);
    deMudActivity = deMudAcc / static_cast<float>(numSamples);
    deEssActivity = deEssAcc / static_cast<float>(numSamples);
    plosiveActivity = plosiveAcc / static_cast<float>(numSamples);
}

void Dsp::processRecovery(juce::AudioBuffer<float>& buffer) {
    const int numChannels = std::min(channels, buffer.getNumChannels());
    const int numSamples = buffer.getNumSamples();
    if (numChannels <= 0 || numSamples <= 0)
        return;

    const float recoveryAmt = juce::jlimit(0.0f, 1.0f, params.recovery);
    if (recoveryAmt <= 1.0e-5f) {
        recoveryLiftDb = 0.0f;
        return;
    }

    const bool voiceMode = params.contentMode == 0;
    const int sourcePreset = juce::jlimit(0, 5, params.sourcePreset);
    const float voicePreserve = juce::jlimit(0.0f, 1.0f, params.voicePreserve);
    const float denoiseAmt = juce::jlimit(0.0f, 1.0f, params.denoiseAmount);
    const float artifactRisk = juce::jlimit(0.0f, 1.0f, params.artifactRisk);
    const float speechPresence = juce::jlimit(0.0f, 1.0f, params.speechPresence);
    const float loudNorm = juce::jlimit(0.0f, 1.0f, (params.speechLoudnessDb + 48.0f) / 42.0f);

    const float strength = juce::jlimit(0.0f, 1.0f,
        recoveryAmt * (0.35f + 0.65f * voicePreserve) * (0.45f + 0.55f * denoiseAmt) * (1.0f - 0.60f * artifactRisk));
    if (strength <= 1.0e-5f) {
        recoveryLiftDb = 0.0f;
        return;
    }

    const float detectorA = cDetectorA;
    const float lowA = voiceMode ? cLowA_voice : cLowA_general;
    const float lowMidA = voiceMode ? cLowMidA_voice : cLowMidA_general;
    const float presenceLoA = cPresenceLoA;
    const float presenceHiA = cPresenceHiA;
    const float airLoA = cAirLoA;

    float targetBodyRatio = voiceMode ? 0.24f : 0.18f;
    float targetPresenceRatio = voiceMode ? 0.18f : 0.14f;
    float targetAirRatio = voiceMode ? 0.10f : 0.08f;
    float bodyMaxDbBase = voiceMode ? 6.2f : 4.5f;
    float presenceMaxDbBase = voiceMode ? 5.4f : 3.8f;
    float airMaxDbBase = voiceMode ? 3.2f : 2.4f;
    if (sourcePreset == 1) {
        targetBodyRatio += 0.03f;
        targetPresenceRatio += 0.02f;
        bodyMaxDbBase += 1.0f;
        presenceMaxDbBase += 0.8f;
        airMaxDbBase += 0.4f;
    } else if (sourcePreset == 2) {
        targetBodyRatio += 0.01f;
        targetPresenceRatio += 0.02f;
        bodyMaxDbBase += 0.5f;
        presenceMaxDbBase += 0.6f;
        airMaxDbBase += 0.2f;
    } else if (sourcePreset == 3) {
        targetBodyRatio += 0.015f;
        targetPresenceRatio += 0.015f;
        bodyMaxDbBase += 0.6f;
        presenceMaxDbBase += 0.5f;
        airMaxDbBase += 0.2f;
    } else if (sourcePreset == 4) {
        targetBodyRatio += 0.02f;
        targetPresenceRatio += 0.025f;
        bodyMaxDbBase += 0.8f;
        presenceMaxDbBase += 1.1f;
        airMaxDbBase += 0.5f;
    } else if (sourcePreset == 5) {
        targetBodyRatio += 0.015f;
        targetPresenceRatio += 0.020f;
        bodyMaxDbBase += 0.6f;
        presenceMaxDbBase += 0.9f;
        airMaxDbBase -= 0.2f;
    }
    const float targetBodyRatioClamped = juce::jlimit(0.14f, 0.34f, targetBodyRatio);
    const float targetPresenceRatioClamped = juce::jlimit(0.10f, 0.28f, targetPresenceRatio);
    const float targetAirRatioClamped = juce::jlimit(0.05f, 0.16f, targetAirRatio);
    const float bodyMaxDb = bodyMaxDbBase * strength;
    const float presenceMaxDb = presenceMaxDbBase * strength;
    const float airMaxDb = airMaxDbBase * strength * (0.65f + 0.35f * speechPresence);

    float liftAccDb = 0.0f;
    for (int i = 0; i < numSamples; ++i) {
        float mono = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            mono += buffer.getReadPointer(ch)[i];
        mono *= 1.0f / static_cast<float>(numChannels);

        const float monoAbs = std::abs(mono);
        recoveryMonoLowLp = lowA * recoveryMonoLowLp + (1.0f - lowA) * mono;
        recoveryMonoLowMidLp = lowMidA * recoveryMonoLowMidLp + (1.0f - lowMidA) * mono;
        const float monoLow = recoveryMonoLowLp;
        const float monoLowMid = recoveryMonoLowMidLp;
        const float lowMidBand = monoLowMid - monoLow;

        recoveryMonoPresenceLoLp = presenceLoA * recoveryMonoPresenceLoLp + (1.0f - presenceLoA) * mono;
        recoveryMonoPresenceHiLp = presenceHiA * recoveryMonoPresenceHiLp + (1.0f - presenceHiA) * mono;
        const float monoPresLo = recoveryMonoPresenceLoLp;
        const float monoPresHi = recoveryMonoPresenceHiLp;
        const float presenceBand = monoPresHi - monoPresLo;
        recoveryMonoAirLp = airLoA * recoveryMonoAirLp + (1.0f - airLoA) * mono;
        const float monoAir = mono - recoveryMonoAirLp;

        recoveryInputEnv = detectorA * recoveryInputEnv + (1.0f - detectorA) * monoAbs;
        recoveryBodyEnv = detectorA * recoveryBodyEnv + (1.0f - detectorA) * std::abs(lowMidBand);
        recoveryPresenceEnv = detectorA * recoveryPresenceEnv + (1.0f - detectorA) * std::abs(presenceBand);
        recoveryAirEnv = detectorA * recoveryAirEnv + (1.0f - detectorA) * std::abs(monoAir);

        const float inEnv = std::max(1.0e-6f, recoveryInputEnv);
        const float inDb = 20.0f * std::log10(inEnv);
        const float noiseGuard = juce::jlimit(0.0f, 1.0f, (inDb - (params.noiseFloorDb + 2.0f)) / 12.0f);

        const float bodyRatio = recoveryBodyEnv / inEnv;
        const float presenceRatio = recoveryPresenceEnv / inEnv;
        const float airRatio = recoveryAirEnv / inEnv;

        const float bodyNeed = juce::jlimit(0.0f, 1.0f, (targetBodyRatioClamped - bodyRatio) / targetBodyRatioClamped);
        const float presenceNeed = juce::jlimit(0.0f, 1.0f, (targetPresenceRatioClamped - presenceRatio) / targetPresenceRatioClamped);
        const float airNeed = juce::jlimit(0.0f, 1.0f, (targetAirRatioClamped - airRatio) / targetAirRatioClamped);

        const float loudGuard = juce::jlimit(0.35f, 1.0f, 1.0f - 0.55f * loudNorm);
        const float bodyGain = juce::Decibels::decibelsToGain(bodyMaxDb * bodyNeed * loudGuard * noiseGuard);
        const float presenceGain = juce::Decibels::decibelsToGain(presenceMaxDb * presenceNeed * (0.7f + 0.3f * speechPresence) * noiseGuard);
        const float airGain = juce::Decibels::decibelsToGain(airMaxDb * airNeed * (0.8f + 0.2f * speechPresence) * loudGuard * noiseGuard);
        liftAccDb += (bodyMaxDb * bodyNeed + presenceMaxDb * presenceNeed + airMaxDb * airNeed) * 0.333f * noiseGuard;

        for (int ch = 0; ch < numChannels; ++ch) {
            auto* d = buffer.getWritePointer(ch);
            const float x = d[i];

            float low = recoveryLowCh[static_cast<size_t>(ch)];
            low = lowA * low + (1.0f - lowA) * x;
            recoveryLowCh[static_cast<size_t>(ch)] = low;

            float lowMid = recoveryLowMidCh[static_cast<size_t>(ch)];
            lowMid = lowMidA * lowMid + (1.0f - lowMidA) * x;
            recoveryLowMidCh[static_cast<size_t>(ch)] = lowMid;
            const float bodyBand = lowMid - low;

            float pLo = recoveryPresenceLoCh[static_cast<size_t>(ch)];
            pLo = presenceLoA * pLo + (1.0f - presenceLoA) * x;
            recoveryPresenceLoCh[static_cast<size_t>(ch)] = pLo;
            float pHi = recoveryPresenceHiCh[static_cast<size_t>(ch)];
            pHi = presenceHiA * pHi + (1.0f - presenceHiA) * x;
            recoveryPresenceHiCh[static_cast<size_t>(ch)] = pHi;
            const float presenceBandCh = pHi - pLo;

            float airLo = recoveryAirLoCh[static_cast<size_t>(ch)];
            airLo = airLoA * airLo + (1.0f - airLoA) * x;
            recoveryAirLoCh[static_cast<size_t>(ch)] = airLo;
            const float airBand = x - airLo;

            const float y = x
                          + bodyBand * (bodyGain - 1.0f)
                          + presenceBandCh * (presenceGain - 1.0f)
                          + airBand * (airGain - 1.0f);

            d[i] = juce::jlimit(-1.0f, 1.0f, y);
        }
    }
    recoveryLiftDb = liftAccDb / static_cast<float>(numSamples);
}

void Dsp::processLimiter(juce::AudioBuffer<float>& buffer) {
    const int numChannels = std::min(channels, buffer.getNumChannels());
    const int numSamples = buffer.getNumSamples();
    if (numChannels <= 0 || numSamples <= 0)
        return;

    const float limitAmt = juce::jlimit(0.0f, 1.0f, params.limit);
    limiterReductionDb = 0.0f;
    if (limitAmt <= 1.0e-6f)
        return;

    const bool voiceMode = params.contentMode == 0;
    const float limiterAttackA = std::exp(-1.0f / ((0.0012f - 0.0008f * limitAmt) * static_cast<float>(sr)));
    const float limiterReleaseA = std::exp(-1.0f / ((0.120f - 0.070f * limitAmt) * static_cast<float>(sr)));
    const float limiterGainSmoothA = cLimiterGainSmooth;
    const float limiterCeil = juce::Decibels::decibelsToGain(-(voiceMode ? 0.6f : 0.9f) - 5.4f * limitAmt);

    float limiterAcc = 0.0f;
    for (int i = 0; i < numSamples; ++i) {
        float samplePeak = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            samplePeak = std::max(samplePeak, std::abs(buffer.getReadPointer(ch)[i]));

        const float limA = samplePeak > limitEnv ? limiterAttackA : limiterReleaseA;
        limitEnv = limA * limitEnv + (1.0f - limA) * samplePeak;
        float limitTargetGain = 1.0f;
        if (limitEnv > limiterCeil)
            limitTargetGain = limiterCeil / (limitEnv + 1.0e-6f);
        limitGain = limiterGainSmoothA * limitGain + (1.0f - limiterGainSmoothA) * limitTargetGain;
        limiterAcc += std::max(0.0f, -juce::Decibels::gainToDecibels(std::max(limitGain, 1.0e-6f), -120.0f));

        for (int ch = 0; ch < numChannels; ++ch) {
            auto* d = buffer.getWritePointer(ch);
            d[i] = juce::jlimit(-1.0f, 1.0f, d[i] * limitGain);
        }
    }
    limiterReductionDb = limiterAcc / static_cast<float>(numSamples);
}

} // namespace vxsuite::polish
