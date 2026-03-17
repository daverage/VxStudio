#include "VxPolishCorrectiveStage.h"

#include "VxPolishDspCommon.h"

namespace vxsuite::polish {

void CorrectiveStage::prepare(double sampleRate, int numChannels) {
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    channels = std::max(1, numChannels);

    const float fsr = static_cast<float>(sr);
    cA180 = detail::onePoleCoeff(sr, 180.0f);
    cRmsA = std::exp(-1.0f / (0.008f * fsr));
    cDeMudAtk = std::exp(-1.0f / (0.050f * fsr));
    cDeMudRel = std::exp(-1.0f / (0.200f * fsr));
    cAEssVoice = detail::onePoleCoeff(sr, 5800.0f);
    cAEssGeneral = detail::onePoleCoeff(sr, 6200.0f);
    cDeEssAtk = std::exp(-1.0f / (0.004f * fsr));
    cDeEssRel = std::exp(-1.0f / (0.075f * fsr));
    cABreathVoice = detail::onePoleCoeff(sr, 2800.0f);
    cABreathGeneral = detail::onePoleCoeff(sr, 3400.0f);
    cBreathAtk = std::exp(-1.0f / (0.012f * fsr));
    cBreathRel = std::exp(-1.0f / (0.120f * fsr));
    cPlosiveFastA = std::exp(-1.0f / (0.003f * fsr));
    cPlosiveSlowA = std::exp(-1.0f / (0.080f * fsr));
    cPlosiveAtkA = std::exp(-1.0f / (0.002f * fsr));
    cPlosiveRelA = std::exp(-1.0f / (0.100f * fsr));
    cPlosiveGainSmooth = std::exp(-1.0f / (0.010f * fsr));
    cCompGainSmooth = std::exp(-1.0f / (0.010f * fsr));
    cTroubleRefA = detail::onePoleCoeff(sr, 800.0f);
    cTroubleAtk = std::exp(-1.0f / (0.006f * fsr));
    cTroubleRel = std::exp(-1.0f / (0.080f * fsr));

    const std::array<float, 6> centers { 1400.0f, 2400.0f, 3600.0f, 5200.0f, 7600.0f, 10500.0f };
    const std::array<float, 6> qVals   { 1.10f,   1.20f,   1.20f,   1.15f,   1.05f,   0.90f   };
    for (size_t b = 0; b < 6; ++b) {
        const auto bpf = detail::makeBandpass(sr, centers[b], qVals[b]);
        troubleDetBpfB0[b] = bpf.b0;
        troubleDetBpfA1[b] = bpf.a1;
        troubleDetBpfA2[b] = bpf.a2;
    }

    hpfZ1.assign(static_cast<size_t>(channels), 0.0f);
    hpfZ2.assign(static_cast<size_t>(channels), 0.0f);
    const auto mudBpf = detail::makeBandpass(sr, 300.0f, 1.2f);
    mudActBpfB0 = mudBpf.b0;
    mudActBpfA1 = mudBpf.a1;
    mudActBpfA2 = mudBpf.a2;
    deMudEqZ1.assign(static_cast<size_t>(channels), 0.0f);
    deMudEqZ2.assign(static_cast<size_t>(channels), 0.0f);
    deEssLpCh.assign(static_cast<size_t>(channels), 0.0f);
    breathLpCh.assign(static_cast<size_t>(channels), 0.0f);
    for (size_t b = 0; b < troubleEqZ1.size(); ++b) {
        troubleEqZ1[b].assign(static_cast<size_t>(channels), 0.0f);
        troubleEqZ2[b].assign(static_cast<size_t>(channels), 0.0f);
    }
    plosiveLpCh.assign(static_cast<size_t>(channels), 0.0f);
    plosiveGainCh.assign(static_cast<size_t>(channels), 1.0f);
    reset();
}

void CorrectiveStage::setParams(const SharedParams& newParams) {
    if (newParams.hpfOn != params.hpfOn || newParams.contentMode != params.contentMode) {
        const float fc = (newParams.contentMode == 0) ? 80.0f : 40.0f;
        const float K = std::tan(juce::MathConstants<float>::pi * fc / static_cast<float>(sr));
        const float K2 = K * K;
        const float norm = 1.0f / (1.0f + juce::MathConstants<float>::sqrt2 * K + K2);
        hpfB0 = norm;
        hpfB1 = -2.0f * norm;
        hpfB2 = norm;
        hpfA1 = 2.0f * (K2 - 1.0f) * norm;
        hpfA2 = (1.0f - juce::MathConstants<float>::sqrt2 * K + K2) * norm;
        if (newParams.hpfOn && !params.hpfOn) {
            std::fill(hpfZ1.begin(), hpfZ1.end(), 0.0f);
            std::fill(hpfZ2.begin(), hpfZ2.end(), 0.0f);
        }
    }

    params = newParams;
}

void CorrectiveStage::reset() {
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
    breathEnv = 0.0f;
    breathMonoLp = 0.0f;
    std::fill(deEssLpCh.begin(), deEssLpCh.end(), 0.0f);
    std::fill(breathLpCh.begin(), breathLpCh.end(), 0.0f);

    plosiveEnv = 0.0f;
    plosiveFast = 0.0f;
    plosiveSlow = 0.0f;
    plosiveMonoLp = 0.0f;
    compEnv = 0.0f;
    std::fill(plosiveLpCh.begin(), plosiveLpCh.end(), 0.0f);
    std::fill(plosiveGainCh.begin(), plosiveGainCh.end(), 1.0f);

    deMudActivity = 0.0f;
    deEssActivity = 0.0f;
    breathActivity = 0.0f;
    plosiveActivity = 0.0f;
    compActivity = 0.0f;
    troubleActivity = 0.0f;
    std::fill(hpfZ1.begin(), hpfZ1.end(), 0.0f);
    std::fill(hpfZ2.begin(), hpfZ2.end(), 0.0f);
    troubleRefLp = 0.0f;
    troubleRefRms = 0.0f;
    troubleBandZ1.fill(0.0f);
    troubleBandZ2.fill(0.0f);
    troubleBandRms.fill(0.0f);
}

void CorrectiveStage::process(juce::AudioBuffer<float>& buffer) {
    const int numChannels = std::min(channels, buffer.getNumChannels());
    const int numSamples = buffer.getNumSamples();
    if (numChannels <= 0 || numSamples <= 0)
        return;

    const float deMudAmt = juce::jlimit(0.0f, 1.0f, params.deMud);
    const float deEssAmt = juce::jlimit(0.0f, 1.0f, params.deEss);
    const float breathAmt = juce::jlimit(0.0f, 1.0f, params.breath);
    const float plosiveAmt = juce::jlimit(0.0f, 1.0f, params.plosive);
    const float compAmt = juce::jlimit(0.0f, 1.0f, params.compress);
    const float troubleAmt = juce::jlimit(0.0f, 1.0f, params.troubleSmooth);
    const float proxCtx = juce::jlimit(0.0f, 1.0f, params.proximityContext);
    const float speechPresence = juce::jlimit(0.0f, 1.0f, params.speechPresence);
    const float loudNorm = juce::jlimit(0.0f, 1.0f, (params.speechLoudnessDb + 48.0f) / 42.0f);
    const bool voiceMode = params.contentMode == 0;

    if (params.hpfOn) {
        for (int ch = 0; ch < numChannels; ++ch) {
            auto* buf = buffer.getWritePointer(ch);
            for (int i = 0; i < numSamples; ++i) {
                buf[i] = detail::processBiquadDf2(buf[i], hpfB0, hpfB1, hpfB2, hpfA1, hpfA2,
                                                  hpfZ1[static_cast<size_t>(ch)],
                                                  hpfZ2[static_cast<size_t>(ch)]);
            }
        }
    }

    deMudActivity = 0.0f;
    deEssActivity = 0.0f;
    breathActivity = 0.0f;
    plosiveActivity = 0.0f;
    compActivity = 0.0f;
    troubleActivity = 0.0f;
    if (deMudAmt <= 1.0e-6f && deEssAmt <= 1.0e-6f && breathAmt <= 1.0e-6f && plosiveAmt <= 1.0e-6f
        && compAmt <= 1.0e-6f && troubleAmt <= 1.0e-6f) {
        return;
    }

    std::array<detail::BiquadCoeffs, 6> troubleCoeffs {};
    const std::array<float, 6> troubleCenters { 1400.0f, 2400.0f, 3600.0f, 5200.0f, 7600.0f, 10500.0f };
    const std::array<float, 6> troubleQVals   { 1.10f,   1.20f,   1.20f,   1.15f,   1.05f,   0.90f   };
    const std::array<float, 6> troubleMaxCut = voiceMode
        ? std::array<float, 6>{ 3.0f, 4.5f, 5.5f, 6.0f, 5.5f, 4.5f }
        : std::array<float, 6>{ 3.0f, 4.5f, 6.0f, 7.0f, 6.0f, 5.0f };
    const std::array<float, 6> troubleThresh = voiceMode
        ? std::array<float, 6>{ 2.0f, 1.5f, 1.0f, 0.5f, 0.0f, 1.5f }
        : std::array<float, 6>{ 2.0f, 1.5f, 0.5f, 0.0f, 0.0f, 0.5f };
    constexpr float troubleRange = 6.0f;
    const float smoothCtx = juce::jlimit(0.90f, 1.35f,
        1.00f + 0.30f * params.artifactRisk - 0.03f * speechPresence);

    const float a180 = cA180;
    const auto mudDetBpf = detail::makeBandpass(sr, 300.0f, 0.8f);
    const auto refDetBpf = detail::makeBandpass(sr, 2000.0f, 0.7f);
    const float rmsA = cRmsA;
    const float aEss = voiceMode ? cAEssVoice : cAEssGeneral;

    const float deMudThresholdDb = (voiceMode ? 1.5f : 2.5f) + 2.0f * loudNorm - 1.2f * proxCtx;
    const float deMudRangeDb = 10.0f;
    const float deMudMaxCutDb = (voiceMode ? 12.0f : 9.0f) * deMudAmt;
    const float maxMudLinGain = (deMudAmt > 1.0e-6f)
        ? juce::Decibels::decibelsToGain(-deMudMaxCutDb)
        : 1.0f;

    const float deEssThreshold = juce::jlimit(0.18f, 0.46f, (voiceMode ? 0.21f : 0.31f) + 0.08f * loudNorm - 0.05f * proxCtx);
    const float deEssMaxCutDb = (voiceMode ? 11.0f : 5.5f) * deEssAmt;
    const float aBreath = voiceMode ? cABreathVoice : cABreathGeneral;
    const float breathThreshold = juce::jlimit(0.22f, 0.55f, (voiceMode ? 0.30f : 0.40f) + 0.06f * loudNorm);
    const float breathCeil = juce::jlimit(0.42f, 0.75f, breathThreshold + (voiceMode ? 0.18f : 0.14f));
    const float breathLevelFloor = juce::Decibels::decibelsToGain(params.noiseFloorDb + (voiceMode ? 8.0f : 10.0f));
    const float breathLevelCeil = voiceMode ? 0.14f : 0.10f;
    const float breathMaxCutDb = (voiceMode ? 7.0f : 2.8f) * breathAmt * (0.85f + 0.15f * (1.0f - speechPresence));
    const float plosiveMaxCutDb = (voiceMode ? 8.5f : 4.4f) * plosiveAmt * (0.82f + 0.28f * proxCtx);

    const float compAtk = std::exp(-1.0f / ((0.022f - 0.010f * compAmt) * static_cast<float>(sr)));
    const float compRel = std::exp(-1.0f / ((0.220f - 0.090f * compAmt) * static_cast<float>(sr)));
    const float compThresholdDb = -20.0f - 8.0f * compAmt;
    const float compRatio = 1.6f + 4.0f * compAmt;
    const float compMakeupDb = 1.0f * compAmt;
    const float compSidechainBoost = juce::Decibels::decibelsToGain(juce::jlimit(0.0f, 18.0f, params.compSidechainBoostDb));

    float deMudAcc = 0.0f;
    float deEssAcc = 0.0f;
    float breathAcc = 0.0f;
    float plosiveAcc = 0.0f;
    float compAcc = 0.0f;
    float troubleAcc = 0.0f;
    float compGain = 1.0f;

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

        float troubleDbSample = 0.0f;
        if (troubleAmt > 1.0e-6f) {
            troubleRefLp = cTroubleRefA * troubleRefLp + (1.0f - cTroubleRefA) * monoLinear;
            troubleRefRms = cRmsA * troubleRefRms + (1.0f - cRmsA) * (troubleRefLp * troubleRefLp);
            const float refEnv = std::sqrt(troubleRefRms + 1.0e-12f);
            float bandCutSum = 0.0f;
            for (size_t b = 0; b < 6; ++b) {
                const float bs = detail::processBiquadDf2(monoLinear, troubleDetBpfB0[b], 0.0f, -troubleDetBpfB0[b],
                                                          troubleDetBpfA1[b], troubleDetBpfA2[b],
                                                          troubleBandZ1[b], troubleBandZ2[b]);
                const float bsq = bs * bs;
                const float tA = bsq > troubleBandRms[b] ? cTroubleAtk : cTroubleRel;
                troubleBandRms[b] = tA * troubleBandRms[b] + (1.0f - tA) * bsq;
                const float bandEnv = std::sqrt(troubleBandRms[b] + 1.0e-12f);
                const float ratioDb = 20.0f * std::log10(bandEnv / refEnv + 1.0e-12f);
                const float excessDb = std::max(0.0f, ratioDb - troubleThresh[b]);
                const float drive = juce::jlimit(0.0f, 1.0f, excessDb / troubleRange);
                const float cutDb = -troubleMaxCut[b] * troubleAmt * drive * smoothCtx;
                troubleCoeffs[b] = detail::makePeakingEq(sr, troubleCenters[b], troubleQVals[b], cutDb);
                bandCutSum += troubleMaxCut[b] * troubleAmt * drive;
            }
            troubleDbSample = bandCutSum / 6.0f;
            troubleAcc += troubleDbSample;
        }

        plosiveMonoLp = a180 * plosiveMonoLp + (1.0f - a180) * monoAbs;
        const float lowAbs = std::abs(plosiveMonoLp);
        plosiveFast = cPlosiveFastA * plosiveFast + (1.0f - cPlosiveFastA) * lowAbs;
        plosiveSlow = cPlosiveSlowA * plosiveSlow + (1.0f - cPlosiveSlowA) * lowAbs;
        const float burstGate = 1.12f + 0.22f * loudNorm - 0.12f * proxCtx;
        const float burst = std::max(0.0f, (plosiveFast - burstGate * plosiveSlow) / (plosiveSlow + 1.0e-6f));
        const float plosiveTarget = juce::jlimit(0.0f, 1.0f, burst) * plosiveAmt;
        const float plosiveA = plosiveTarget > plosiveEnv ? cPlosiveAtkA : cPlosiveRelA;
        plosiveEnv = plosiveA * plosiveEnv + (1.0f - plosiveA) * plosiveTarget;
        const float plosiveGain = juce::Decibels::decibelsToGain(-plosiveMaxCutDb * plosiveEnv);

        const float mudDet = detail::processBiquadDf2(monoLinear, mudDetBpf, deMudDetMudZ1, deMudDetMudZ2);
        const float refDet = detail::processBiquadDf2(monoLinear, refDetBpf, deMudDetRefZ1, deMudDetRefZ2);
        deMudMudRms = rmsA * deMudMudRms + (1.0f - rmsA) * (mudDet * mudDet);
        deMudRefRms = rmsA * deMudRefRms + (1.0f - rmsA) * (refDet * refDet);
        const float ratio = (deMudMudRms + 1.0e-12f) / (deMudRefRms + 1.0e-12f);
        const float ratioDb = 10.0f * std::log10(std::max(1.0e-12f, ratio));
        const float excessDb = std::max(0.0f, ratioDb - deMudThresholdDb);
        const float deMudTarget = juce::jlimit(0.0f, 1.0f, excessDb / deMudRangeDb) * deMudAmt;
        const float deMudA = deMudTarget > deMudEnv ? cDeMudAtk : cDeMudRel;
        deMudEnv = deMudA * deMudEnv + (1.0f - deMudA) * deMudTarget;
        const float deMudGain = juce::Decibels::decibelsToGain(-deMudMaxCutDb * deMudEnv);

        deEssMonoLp = aEss * deEssMonoLp + (1.0f - aEss) * monoAbs;
        const float monoHf = monoAbs - deEssMonoLp;
        const float hfRatio = std::abs(monoHf) / (std::abs(monoAbs) + 1.0e-6f);
        const float deEssCeil = juce::jlimit(0.48f, 0.72f, 0.55f + 0.10f * (1.0f - speechPresence));
        const float deEssNorm = (hfRatio - deEssThreshold) / (deEssCeil - deEssThreshold);
        const float deEssTarget = juce::jlimit(0.0f, 1.0f, deEssNorm) * deEssAmt;
        const float deEssA = deEssTarget > deEssEnv ? cDeEssAtk : cDeEssRel;
        deEssEnv = deEssA * deEssEnv + (1.0f - deEssA) * deEssTarget;
        const float deEssGain = juce::Decibels::decibelsToGain(-deEssMaxCutDb * deEssEnv);

        breathMonoLp = aBreath * breathMonoLp + (1.0f - aBreath) * monoAbs;
        const float monoBreathBand = monoAbs - breathMonoLp;
        const float breathRatio = std::abs(monoBreathBand) / (monoAbs + 1.0e-6f);
        const float breathNorm = (breathRatio - breathThreshold) / (breathCeil - breathThreshold);
        const float breathLevelNorm = juce::jlimit(0.0f, 1.0f,
            (breathLevelCeil - monoAbs) / (breathLevelCeil - breathLevelFloor + 1.0e-6f));
        const float breathSpeechGuard = juce::jlimit(0.15f, 1.0f, 1.0f - (voiceMode ? 0.45f : 0.65f) * speechPresence);
        const float breathTransientGuard = juce::jlimit(0.0f, 1.0f, 1.0f - 1.25f * burst);
        const float breathSibilantGuard = juce::jlimit(0.0f, 1.0f, 1.0f - 0.80f * deEssEnv);
        const float breathTarget = juce::jlimit(0.0f, 1.0f, breathNorm)
                                 * breathLevelNorm
                                 * breathSpeechGuard
                                 * breathTransientGuard
                                 * breathSibilantGuard
                                 * breathAmt;
        const float breathA = breathTarget > breathEnv ? cBreathAtk : cBreathRel;
        breathEnv = breathA * breathEnv + (1.0f - breathA) * breathTarget;
        const float breathGain = juce::Decibels::decibelsToGain(-breathMaxCutDb * breathEnv);

        deMudAcc += deMudEnv;
        deEssAcc += deEssEnv;
        breathAcc += breathEnv;
        plosiveAcc += plosiveEnv;

        const float compIn = std::abs(monoAbs) * compSidechainBoost;
        const float compA = compIn > compEnv ? compAtk : compRel;
        compEnv = compA * compEnv + (1.0f - compA) * compIn;
        const float compEnvDb = 20.0f * std::log10(compEnv + 1.0e-6f);
        float gainReductionDb = 0.0f;
        if (compEnvDb > compThresholdDb)
            gainReductionDb = (1.0f - 1.0f / compRatio) * (compEnvDb - compThresholdDb);
        const float compTargetGain = juce::Decibels::decibelsToGain(-gainReductionDb + compMakeupDb);
        compGain = cCompGainSmooth * compGain + (1.0f - cCompGainSmooth) * compTargetGain;
        const float compDb = std::max(0.0f, -juce::Decibels::gainToDecibels(std::max(compGain, 1.0e-6f), -120.0f));
        compAcc += juce::jlimit(0.0f, 1.0f, compDb / 8.0f);

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

            const float mudBand = detail::processBiquadDf2(x, mudActBpfB0, 0.0f, -mudActBpfB0,
                                                           mudActBpfA1, mudActBpfA2,
                                                           deMudEqZ1[static_cast<size_t>(ch)],
                                                           deMudEqZ2[static_cast<size_t>(ch)]);
            const float mudLinGain = 1.0f + deMudEnv * (maxMudLinGain - 1.0f);
            x -= mudBand * (1.0f - mudLinGain);

            float essLp = deEssLpCh[static_cast<size_t>(ch)];
            essLp = aEss * essLp + (1.0f - aEss) * x;
            deEssLpCh[static_cast<size_t>(ch)] = essLp;
            const float highBand = x - essLp;
            float breathLp = breathLpCh[static_cast<size_t>(ch)];
            breathLp = aBreath * breathLp + (1.0f - aBreath) * x;
            breathLpCh[static_cast<size_t>(ch)] = breathLp;
            const float breathBand = x - breathLp;
            x = essLp + highBand * deEssGain;
            x -= breathBand * (1.0f - breathGain);

            x *= compGain;
            if (troubleAmt > 1.0e-6f) {
                for (size_t b = 0; b < troubleCoeffs.size(); ++b) {
                    x = detail::processBiquadDf2(x, troubleCoeffs[b],
                                                 troubleEqZ1[b][static_cast<size_t>(ch)],
                                                 troubleEqZ2[b][static_cast<size_t>(ch)]);
                }
            }

            d[i] = x;
        }
    }

    deMudActivity = deMudAcc / static_cast<float>(numSamples);
    deEssActivity = deEssAcc / static_cast<float>(numSamples);
    breathActivity = breathAcc / static_cast<float>(numSamples);
    plosiveActivity = plosiveAcc / static_cast<float>(numSamples);
    compActivity = compAcc / static_cast<float>(numSamples);
    troubleActivity = juce::jlimit(0.0f, 1.0f, (troubleAcc / static_cast<float>(numSamples)) / 4.0f);
}

} // namespace vxsuite::polish
