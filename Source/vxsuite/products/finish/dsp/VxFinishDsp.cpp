#include "VxFinishDsp.h"

#include "../../polish/dsp/VxPolishDspCommon.h"

namespace vxsuite::finish {

void Dsp::prepare(double sampleRate, int, int numChannels) {
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    channels = std::max(1, numChannels);

    const float fsr = static_cast<float>(sr);
    cDetectorA = std::exp(-1.0f / (0.030f * fsr));
    cLowA_voice = vxsuite::polish::detail::onePoleCoeff(sr, 380.0f);
    cLowA_general = vxsuite::polish::detail::onePoleCoeff(sr, 320.0f);
    cLowMidA_voice = vxsuite::polish::detail::onePoleCoeff(sr, 820.0f);
    cLowMidA_general = vxsuite::polish::detail::onePoleCoeff(sr, 700.0f);
    cPresenceLoA = vxsuite::polish::detail::onePoleCoeff(sr, 1800.0f);
    cPresenceHiA = vxsuite::polish::detail::onePoleCoeff(sr, 5200.0f);
    cAirLoA = vxsuite::polish::detail::onePoleCoeff(sr, 6400.0f);
    cLimiterGainSmooth = std::exp(-1.0f / (0.0020f * fsr));

    hiShelfZ1.assign(static_cast<size_t>(channels), 0.0f);
    recoveryLowCh.assign(static_cast<size_t>(channels), 0.0f);
    recoveryLowMidCh.assign(static_cast<size_t>(channels), 0.0f);
    recoveryPresenceLoCh.assign(static_cast<size_t>(channels), 0.0f);
    recoveryPresenceHiCh.assign(static_cast<size_t>(channels), 0.0f);
    recoveryAirLoCh.assign(static_cast<size_t>(channels), 0.0f);
    corrective.prepare(sampleRate, numChannels);
    reset();
}

void Dsp::setParams(const Params& newParams) {
    if (newParams.hiShelfOn != params.hiShelfOn || newParams.contentMode != params.contentMode) {
        const float fc = (newParams.contentMode == 0) ? 12000.0f : 16000.0f;
        const float dB = (newParams.contentMode == 0) ? -4.0f : -3.0f;
        const float G = std::pow(10.0f, dB / 20.0f);
        const float K = std::tan(juce::MathConstants<float>::pi * fc / static_cast<float>(sr));
        const float denom = 1.0f + K;
        hiShelfB0 = (G + K) / denom;
        hiShelfB1 = (G - K) / denom;
        hiShelfA1 = (K - 1.0f) / denom;
        if (newParams.hiShelfOn && !params.hiShelfOn)
            std::fill(hiShelfZ1.begin(), hiShelfZ1.end(), 0.0f);
    }

    params = newParams;
    corrective.setParams(newParams);
}

void Dsp::reset() {
    corrective.reset();
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
    limitEnv = 0.0f;
    limitGain = 1.0f;
    recoveryActivity = 0.0f;
    limiterActivity = 0.0f;
    std::fill(hiShelfZ1.begin(), hiShelfZ1.end(), 0.0f);
}

void Dsp::processCorrective(juce::AudioBuffer<float>& buffer) {
    corrective.process(buffer);
}

void Dsp::processRecovery(juce::AudioBuffer<float>& buffer) {
    const int numChannels = std::min(channels, buffer.getNumChannels());
    const int numSamples = buffer.getNumSamples();
    if (numChannels <= 0 || numSamples <= 0)
        return;

    const float recoveryAmt = juce::jlimit(0.0f, 1.0f, params.recovery);
    const float smartGainAmt = juce::jlimit(0.0f, 1.0f, params.smartGain);
    if (recoveryAmt <= 1.0e-5f) {
        recoveryActivity = 0.0f;
        return;
    }

    const bool voiceMode = params.contentMode == 0;
    const float voicePreserve = juce::jlimit(0.0f, 1.0f, params.voicePreserve);
    const float denoiseAmt = juce::jlimit(0.0f, 1.0f, params.denoiseAmount);
    const float artifactRisk = juce::jlimit(0.0f, 1.0f, params.artifactRisk);
    const float speechPresence = juce::jlimit(0.0f, 1.0f, params.speechPresence);
    const float loudNorm = juce::jlimit(0.0f, 1.0f, (params.speechLoudnessDb + 48.0f) / 42.0f);

    const float strength = juce::jlimit(0.0f, 1.0f,
        recoveryAmt * (0.35f + 0.65f * voicePreserve) * (0.45f + 0.55f * denoiseAmt) * (1.0f - 0.60f * artifactRisk));
    if (strength <= 1.0e-5f) {
        recoveryActivity = 0.0f;
        return;
    }

    const float lowA = voiceMode ? cLowA_voice : cLowA_general;
    const float lowMidA = voiceMode ? cLowMidA_voice : cLowMidA_general;

    const float targetBodyRatioClamped = voiceMode ? 0.24f : 0.18f;
    const float targetPresenceRatioClamped = voiceMode ? 0.18f : 0.14f;
    const float targetAirRatioClamped = voiceMode ? 0.10f : 0.08f;
    const float bodyMaxDbBase = voiceMode ? 6.2f : 4.5f;
    const float presenceMaxDbBase = voiceMode ? 5.4f : 3.8f;
    const float airMaxDbBase = voiceMode ? 3.2f : 2.4f;
    const float smartBodyPush = voiceMode ? (0.35f + 0.35f * (1.0f - speechPresence)) : (0.55f + 0.20f * (1.0f - speechPresence));
    const float smartPresencePush = voiceMode ? (0.55f + 0.20f * speechPresence) : (0.42f + 0.12f * speechPresence);
    const float smartAirPush = voiceMode ? (0.50f + 0.25f * speechPresence) : (0.22f + 0.10f * speechPresence);
    const float bodyMaxDb = bodyMaxDbBase * strength * (1.0f + smartGainAmt * smartBodyPush);
    const float presenceMaxDb = presenceMaxDbBase * strength * (1.0f + smartGainAmt * smartPresencePush);
    const float airMaxDb = airMaxDbBase * strength * (0.65f + 0.35f * speechPresence) * (1.0f + smartGainAmt * smartAirPush);

    float liftAccDb = 0.0f;
    for (int i = 0; i < numSamples; ++i) {
        float mono = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            mono += buffer.getReadPointer(ch)[i];
        mono *= 1.0f / static_cast<float>(numChannels);

        const float monoAbs = std::abs(mono);
        recoveryMonoLowLp = lowA * recoveryMonoLowLp + (1.0f - lowA) * mono;
        recoveryMonoLowMidLp = lowMidA * recoveryMonoLowMidLp + (1.0f - lowMidA) * mono;
        const float lowMidBand = recoveryMonoLowMidLp - recoveryMonoLowLp;

        recoveryMonoPresenceLoLp = cPresenceLoA * recoveryMonoPresenceLoLp + (1.0f - cPresenceLoA) * mono;
        recoveryMonoPresenceHiLp = cPresenceHiA * recoveryMonoPresenceHiLp + (1.0f - cPresenceHiA) * mono;
        const float presenceBand = recoveryMonoPresenceHiLp - recoveryMonoPresenceLoLp;
        recoveryMonoAirLp = cAirLoA * recoveryMonoAirLp + (1.0f - cAirLoA) * mono;
        const float monoAir = mono - recoveryMonoAirLp;

        recoveryInputEnv = cDetectorA * recoveryInputEnv + (1.0f - cDetectorA) * monoAbs;
        recoveryBodyEnv = cDetectorA * recoveryBodyEnv + (1.0f - cDetectorA) * std::abs(lowMidBand);
        recoveryPresenceEnv = cDetectorA * recoveryPresenceEnv + (1.0f - cDetectorA) * std::abs(presenceBand);
        recoveryAirEnv = cDetectorA * recoveryAirEnv + (1.0f - cDetectorA) * std::abs(monoAir);

        const float inEnv = std::max(1.0e-6f, recoveryInputEnv);
        const float inDb = 20.0f * std::log10(inEnv);
        const float noiseGuard = juce::jlimit(0.0f, 1.0f, (inDb - (params.noiseFloorDb + 2.0f)) / 12.0f);

        const float bodyRatio = recoveryBodyEnv / inEnv;
        const float presenceRatio = recoveryPresenceEnv / inEnv;
        const float airRatio = recoveryAirEnv / inEnv;

        const float bodyNeed = juce::jlimit(0.0f, 1.0f, (targetBodyRatioClamped - bodyRatio) / targetBodyRatioClamped);
        const float presenceNeed = juce::jlimit(0.0f, 1.0f, (targetPresenceRatioClamped - presenceRatio) / targetPresenceRatioClamped);
        const float airNeed = juce::jlimit(0.0f, 1.0f, (targetAirRatioClamped - airRatio) / targetAirRatioClamped);

        const float loudGuard = juce::jlimit(0.35f, 1.0f, 1.0f - (0.55f - 0.12f * smartGainAmt) * loudNorm);
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
            pLo = cPresenceLoA * pLo + (1.0f - cPresenceLoA) * x;
            recoveryPresenceLoCh[static_cast<size_t>(ch)] = pLo;
            float pHi = recoveryPresenceHiCh[static_cast<size_t>(ch)];
            pHi = cPresenceHiA * pHi + (1.0f - cPresenceHiA) * x;
            recoveryPresenceHiCh[static_cast<size_t>(ch)] = pHi;
            const float presenceBandCh = pHi - pLo;

            float airLo = recoveryAirLoCh[static_cast<size_t>(ch)];
            airLo = cAirLoA * airLo + (1.0f - cAirLoA) * x;
            recoveryAirLoCh[static_cast<size_t>(ch)] = airLo;
            const float airBand = x - airLo;

            d[i] = x
                 + bodyBand * (bodyGain - 1.0f)
                 + presenceBandCh * (presenceGain - 1.0f)
                 + airBand * (airGain - 1.0f);
        }
    }

    recoveryActivity = juce::jlimit(0.0f, 1.0f, (liftAccDb / static_cast<float>(numSamples)) / 6.0f);
}

void Dsp::processLimiter(juce::AudioBuffer<float>& buffer) {
    const int numChannels = std::min(channels, buffer.getNumChannels());
    const int numSamples = buffer.getNumSamples();
    if (numChannels <= 0 || numSamples <= 0)
        return;

    const float limitAmt = juce::jlimit(0.0f, 1.0f, params.limit);
    if (limitAmt <= 1.0e-6f) {
        limiterActivity = 0.0f;
        return;
    }

    const bool voiceMode = params.contentMode == 0;
    const float limiterAttackA = std::exp(-1.0f / ((0.0008f - 0.00045f * limitAmt) * static_cast<float>(sr)));
    const float limiterReleaseA = std::exp(-1.0f / ((0.090f - 0.040f * limitAmt) * static_cast<float>(sr)));
    const float limiterCeil = juce::Decibels::decibelsToGain(-(voiceMode ? 0.5f : 0.8f) - 7.2f * limitAmt);

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
        limitGain = cLimiterGainSmooth * limitGain + (1.0f - cLimiterGainSmooth) * limitTargetGain;
        limiterAcc += std::max(0.0f, -juce::Decibels::gainToDecibels(std::max(limitGain, 1.0e-6f), -120.0f));

        for (int ch = 0; ch < numChannels; ++ch) {
            auto* d = buffer.getWritePointer(ch);
            d[i] = juce::jlimit(-1.0f, 1.0f, d[i] * limitGain);
        }
    }
    limiterActivity = juce::jlimit(0.0f, 1.0f, (limiterAcc / static_cast<float>(numSamples)) / 6.0f);

    if (params.hiShelfOn) {
        for (int ch = 0; ch < numChannels; ++ch) {
            auto* buf = buffer.getWritePointer(ch);
            for (int i = 0; i < numSamples; ++i) {
                const float x = buf[i];
                const float y = hiShelfB0 * x + hiShelfZ1[static_cast<size_t>(ch)];
                hiShelfZ1[static_cast<size_t>(ch)] = hiShelfB1 * x - hiShelfA1 * y;
                buf[i] = y;
            }
        }
    }
}

} // namespace vxsuite::finish
