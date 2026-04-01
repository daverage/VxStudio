#include "VxLevelerDsp.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace vxsuite::leveler {

namespace {

inline float clamp01(const float x) noexcept {
    return juce::jlimit(0.0f, 1.0f, x);
}

inline float clampf(const float x, const float lo, const float hi) noexcept {
    return juce::jlimit(lo, hi, x);
}

inline float smoothstep01(const float x) noexcept {
    const float t = clampf(x, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

inline float gainToDbFloor(const float gain) noexcept {
    return juce::Decibels::gainToDecibels(std::max(gain, 1.0e-5f), -100.0f);
}

} // namespace

Dsp::DebugSnapshot Dsp::getDebugSnapshot() const noexcept {
    DebugSnapshot snapshot {};
    snapshot.generalMomentaryDb = gainToDbFloor(generalMomentary);
    snapshot.generalShortDb = gainToDbFloor(generalShort);
    snapshot.generalBaselineDb = gainToDbFloor(generalBaseline);
    snapshot.generalWetShortDb = gainToDbFloor(generalWetShort);
    snapshot.generalWetBaselineDb = gainToDbFloor(generalWetBaseline);
    snapshot.generalRideGainDb = generalRideGainDb;
    snapshot.generalNormalizeGainDb = generalNormalizeGainDb;
    snapshot.programRestoreGainDb = programRestoreGainDb;
    snapshot.generalSpikeGainDb = juce::Decibels::gainToDecibels(std::max(generalSpikeGain, 1.0e-5f), -100.0f);
    snapshot.globalBaselineDb = globalTracker.getGlobalBaselineDb();
    snapshot.globalUpperDb = globalTracker.getGlobalUpperDb();
    snapshot.globalDynamicRangeDb = globalTracker.getDynamicRangeDb();
    snapshot.globalConfidence = globalTracker.getConfidence();
    return snapshot;
}

void Dsp::prepare(const double sampleRate, const int maxBlockSize, const int numChannels) {
    sr = sampleRate > 1000.0 ? sampleRate : 48000.0;
    preparedBlockSize = std::max(1, maxBlockSize);
    channels.assign(static_cast<size_t>(std::max(1, numChannels)), ChannelState{});
    delaySamples = std::max(1, juce::roundToInt(static_cast<float>(sr) * 0.010f));
    delayLine.assign(static_cast<size_t>(std::max(1, numChannels) * delaySamples), 0.0f);
    coeff150 = lowpassCoeff(sr, 150.0f);
    coeff2000 = lowpassCoeff(sr, 2000.0f);
    coeff4000 = lowpassCoeff(sr, 4000.0f);
    globalTracker.prepare(sr, preparedBlockSize);
    reset();
}

void Dsp::setOfflineAnalysis(OfflineAnalysisResult analysis) {
    offlineAnalysis = std::move(analysis);
    offlineProcessedSamples = 0;
}

void Dsp::clearOfflineAnalysis() noexcept {
    offlineAnalysis = {};
    offlineProcessedSamples = 0;
}

void Dsp::reset() {
    for (auto& state : channels)
        state = ChannelState{};
    std::fill(delayLine.begin(), delayLine.end(), 0.0f);
    delayWriteIndex = 0;
    levelEnv = 0.0f;
    anchorEnv = 0.0f;
    highEnv = 0.0f;
    levellerGain = 1.0f;
    overrideGain = 1.0f;
    speechLiftGain = 1.0f;
    overrideLiftGain = 1.0f;
    highTameGain = 1.0f;
    overrideTameGain = 1.0f;
    vocalPhraseAnchor = 0.0f;
    activeState = MixState::neutral;
    targetState = MixState::neutral;
    stateTransition = 1.0f;
    generalMomentary = 0.0f;
    generalShort = 0.0f;
    generalBaseline = 0.0f;
    generalWetShort = 0.0f;
    generalWetBaseline = 0.0f;
    programDry = 0.0f;
    programWet = 0.0f;
    programRestoreGainDb = 0.0f;
    generalPrimed = false;
    generalPrimeCooldownSamples = 0;
    generalRideGainDb = 0.0f;
    generalNormalizeGainDb = 0.0f;
    generalSpikeGain = 1.0f;
    generalHighEnv = 0.0f;
    globalTracker.reset();
    offlineProcessedSamples = 0;
    offlineActive = false;
    liftActivity = 0.0f;
    levelActivity = 0.0f;
    tameActivity = 0.0f;
}

void Dsp::process(juce::AudioBuffer<float>& buffer, const DetectorSnapshot& detector) {
    const int numChannels = std::min<int>(buffer.getNumChannels(), channels.size());
    const int numSamples = buffer.getNumSamples();
    if (numChannels <= 0 || numSamples <= 0)
        return;

    const float level = clamp01(params.level);
    const float control = clamp01(params.control);
    const bool voiceMode = params.voiceMode;
    const bool neutral = level < 1.0e-4f && control < 1.0e-4f;
    const float signalTrust = juce::jlimit(0.35f, 1.0f, 0.35f + 0.65f * clamp01(params.separationConfidence));
    const float monoPenalty = clamp01(params.monoScore);
    const float compressionPenalty = clamp01(params.compressionScore);
    const float tiltPenalty = clamp01(params.tiltScore);
    const float mixDecisionTrust = voiceMode
        ? 1.0f
        : clamp01(signalTrust
                  * (1.0f - 0.22f * monoPenalty)
                  * (1.0f - 0.18f * compressionPenalty));
    const float mixTiltPenalty = voiceMode ? 0.0f : tiltPenalty;

    const auto desiredState = (!voiceMode || neutral) ? MixState::neutral : detectState(detector);
    if (desiredState != targetState) {
        activeState = targetState;
        targetState = desiredState;
        stateTransition = 0.0f;
    }

    const float transitionSeconds = 0.15f;
    const float transitionStep = juce::jlimit(0.0f,
                                              1.0f,
                                              static_cast<float>(numSamples)
                                                  / std::max(1.0f, transitionSeconds * static_cast<float>(sr)));
    stateTransition = juce::jlimit(0.0f, 1.0f, stateTransition + transitionStep);

    const MixDecision voiceDecision = neutral
        ? MixDecision{}
        : blendDecision(decide(activeState, detector, level, control),
                        decide(targetState, detector, level, control),
                        stateTransition);

    const float levelEnvAttack = timeCoeff(sr, voiceMode ? 0.035f : 0.080f);
    const float levelEnvRelease = timeCoeff(sr, voiceMode ? 0.220f : 0.300f);
    const float anchorRiseCoeff = timeCoeff(sr, voiceMode ? 1.750f : 1.250f);
    const float anchorFallCoeff = timeCoeff(sr, voiceMode ? 1.750f : 0.450f);
    const float phraseAnchorRiseCoeff = timeCoeff(sr, 0.800f);
    const float phraseAnchorFallCoeff = timeCoeff(sr, 1.800f);
    const float levelGainAttack = timeCoeff(sr, voiceMode ? 0.025f : 0.120f);
    const float levelGainRelease = timeCoeff(sr, voiceMode ? 0.240f : 0.650f);
    const float liftAttack = timeCoeff(sr, 0.020f);
    const float liftRelease = timeCoeff(sr, 0.160f);
    const float tameAttack = timeCoeff(sr, 0.004f);
    const float tameRelease = timeCoeff(sr, voiceMode ? 0.120f : 0.180f);
    const float overrideAttack = timeCoeff(sr, 0.010f);
    const float overrideRelease = timeCoeff(sr, voiceMode ? 0.180f : 0.240f);
    const float generalMomentaryCoeff = timeCoeff(sr, 0.40f);
    const float generalShortCoeff = timeCoeff(sr, 3.0f);
    const float generalBaselineRiseCoeff = timeCoeff(sr, 4.2f);
    const float generalBaselineFallCoeff = timeCoeff(sr, 8.0f);
    const float generalSpikeRelease = timeCoeff(sr, 0.085f);
    const float generalNormalizeAttack = timeCoeff(sr, 1.2f);
    const float generalNormalizeRelease = timeCoeff(sr, 3.0f);
    const float programLoudnessCoeff = timeCoeff(sr, 12.0f);
    const float programRestoreAttack = timeCoeff(sr, 2.2f);
    const float programRestoreRelease = timeCoeff(sr, 5.5f);

    const float levelShape = (voiceMode ? 0.35f : 0.24f) + (voiceMode ? 0.70f : 0.46f) * level;
    const float maxUpwardGain = 1.0f + (voiceMode ? 1.05f : 0.18f) * level;
    const float maxDownwardGain = std::max(voiceMode ? 0.24f : 0.50f,
                                           1.0f - (voiceMode ? 0.55f : 0.46f) * level);
    const float maxLiftAmount = (voiceMode ? 0.28f : 0.18f) + 0.82f * level;
    const float maxTameDepth = (voiceMode ? 0.15f : 0.10f) + (voiceMode ? 0.60f : 0.42f) * control;
    const bool mixOfflineMode = !voiceMode
        && params.analysisMode == MixAnalysisMode::offline
        && offlineAnalysis.isValid();
    const int offlineBlockIndex = mixOfflineMode
        ? std::clamp(static_cast<int>(offlineProcessedSamples / std::max(1, offlineAnalysis.blockSize)),
                     0,
                     std::max(0, static_cast<int>(offlineAnalysis.targetCurveDb.size()) - 1))
        : 0;
    const float offlineTargetDb = mixOfflineMode
        ? offlineAnalysis.targetCurveDb[static_cast<size_t>(offlineBlockIndex)]
        : 0.0f;

    float levelActivityAccum = 0.0f;
    float liftActivityAccum = 0.0f;
    float tameActivityAccum = 0.0f;

    for (int i = 0; i < numSamples; ++i) {
        std::array<float, 2> delayedSample { 0.0f, 0.0f };
        std::array<float, 2> speechBand { 0.0f, 0.0f };
        std::array<float, 2> highBand { 0.0f, 0.0f };

        float monoAbs = 0.0f;
        float peakAbs = 0.0f;
        float lowAbs = 0.0f;
        float highAbs = 0.0f;

        for (int ch = 0; ch < numChannels; ++ch) {
            const float x = buffer.getSample(ch, i);
            const float absX = std::abs(x);
            monoAbs += absX;
            peakAbs = std::max(peakAbs, absX);

            const int delayIndex = ch * delaySamples + delayWriteIndex;
            const float delayed = delayLine[static_cast<size_t>(delayIndex)];
            delayLine[static_cast<size_t>(delayIndex)] = x;
            delayedSample[static_cast<size_t>(ch)] = delayed;

            auto& state = channels[static_cast<size_t>(ch)];
            state.lp150 = coeff150 * state.lp150 + (1.0f - coeff150) * delayed;
            state.lp2000 = coeff2000 * state.lp2000 + (1.0f - coeff2000) * delayed;
            state.lp4000 = coeff4000 * state.lp4000 + (1.0f - coeff4000) * delayed;

            lowAbs += std::abs(state.lp150);
            speechBand[static_cast<size_t>(ch)] = state.lp4000 - state.lp150;
            highBand[static_cast<size_t>(ch)] = delayed - state.lp2000;
            highAbs += std::abs(highBand[static_cast<size_t>(ch)]);
        }
        delayWriteIndex = (delayWriteIndex + 1) % delaySamples;

        monoAbs /= static_cast<float>(numChannels);
        lowAbs /= static_cast<float>(numChannels);
        highAbs /= static_cast<float>(numChannels);

        if (!voiceMode) {
            const float weightedLevel = 0.16f * lowAbs + 0.92f * monoAbs + 0.32f * highAbs;
            const float weightedLevelDb = gainToDbFloor(weightedLevel);
            const bool startupActivity = weightedLevelDb > -65.0f || peakAbs > 0.015f;
            if (!generalPrimed && !startupActivity) {
                generalSpikeGain = 1.0f;
                for (int ch = 0; ch < numChannels; ++ch)
                    buffer.setSample(ch, i, delayedSample[static_cast<size_t>(ch)]);
                continue;
            }

            if (!generalPrimed) {
                generalMomentary = weightedLevel;
                generalShort = weightedLevel;
                generalBaseline = weightedLevel;
                generalWetShort = weightedLevel;
                generalWetBaseline = weightedLevel;
                generalPrimed = true;
                generalPrimeCooldownSamples = std::max(1, juce::roundToInt(static_cast<float>(sr) * 1.25f));
            } else {
                generalMomentary = generalMomentaryCoeff * generalMomentary + (1.0f - generalMomentaryCoeff) * weightedLevel;
                generalShort = generalShortCoeff * generalShort + (1.0f - generalShortCoeff) * weightedLevel;
                const float baselineCoeff = generalShort > generalBaseline ? generalBaselineRiseCoeff : generalBaselineFallCoeff;
                generalBaseline = baselineCoeff * generalBaseline + (1.0f - baselineCoeff) * generalShort;
            }
            const float primeRelax = (!voiceMode
                                      && params.analysisMode == MixAnalysisMode::smartRealtime
                                      && generalPrimeCooldownSamples > 0)
                ? clamp01(static_cast<float>(generalPrimeCooldownSamples) / std::max(1.0f, static_cast<float>(sr) * 1.25f))
                : 0.0f;
            if (generalPrimeCooldownSamples > 0)
                --generalPrimeCooldownSamples;

            generalHighEnv = (highAbs > generalHighEnv ? tameAttack : tameRelease) * generalHighEnv
                + (1.0f - (highAbs > generalHighEnv ? tameAttack : tameRelease)) * highAbs;

            const float momentaryDb = gainToDbFloor(generalMomentary);
            const float shortDb = gainToDbFloor(generalShort);
            const float baselineDb = gainToDbFloor(generalBaseline);

            const auto targetFrame = mixOfflineMode
                ? MixTargetFrame { baselineDb,
                                   offlineTargetDb,
                                   offlineTargetDb,
                                   1.0f }
                : makeMixTargetFrame(shortDb, baselineDb, level);
            const float errorDb = shortDb - targetFrame.finalTargetDb;
            const float targetConfidence = clamp01(targetFrame.confidence * mixDecisionTrust);
            const float deadbandDb = (tuning.mixDeadbandBase - tuning.mixDeadbandLevelWeight * level)
                + (1.0f - targetConfidence) * 0.85f;
            const float rampWidthDb = 2.00f - 0.45f * level;
            float rideTargetDb = 0.0f;
            if (!neutral && std::abs(errorDb) > deadbandDb) {
                const float excessDb = std::abs(errorDb) - deadbandDb;
                const float ramp = clamp01(excessDb / std::max(0.5f, rampWidthDb));
                const float shapedRamp = ramp * ramp * (3.0f - 2.0f * ramp);
                const float downLimitDb = juce::jmap(primeRelax,
                                                     (2.4f + 3.2f * level) * (0.88f + 0.12f * control),
                                                     1.25f + 1.35f * level);
                const float upLimitDb = (1.1f + 2.4f * level) * (0.92f + 0.08f * control);
                rideTargetDb = errorDb > 0.0f
                    ? -downLimitDb * shapedRamp
                    : upLimitDb * shapedRamp;
                rideTargetDb *= targetConfidence;
            }

            const float downRate = (1.4f + 5.2f * level) / static_cast<float>(sr);
            const float upRate = (0.28f + 1.40f * level) / static_cast<float>(sr);
            generalRideGainDb = stepToward(generalRideGainDb,
                                           rideTargetDb,
                                           rideTargetDb < generalRideGainDb ? downRate : upRate);

            const float rideGain = juce::Decibels::decibelsToGain(generalRideGainDb);
            const float overshootDb = momentaryDb - shortDb;
            const float overshootThresholdDb = ((1.35f - 0.55f * control)
                                                + primeRelax * (0.90f - 0.20f * control))
                * juce::jmap(compressionPenalty, 1.0f, 0.82f);
            float spikeTarget = 1.0f;
            if (!neutral && overshootDb > overshootThresholdDb) {
                const float overshootExcess = overshootDb - overshootThresholdDb;
                const float spikeDb = std::min(4.5f + 4.0f * control, overshootExcess * (0.85f + 0.45f * control));
                spikeTarget = juce::Decibels::decibelsToGain(-spikeDb * (0.70f + 0.30f * mixDecisionTrust));
            }

            const float predictedPeak = peakAbs * rideGain * spikeTarget;
            const float peakCeiling = juce::jmap(control, 0.0f, 1.0f, 0.992f, 0.94f);
            if (!neutral && predictedPeak > peakCeiling && predictedPeak > 1.0e-5f)
                spikeTarget = std::min(spikeTarget, peakCeiling / predictedPeak);

            if (spikeTarget < generalSpikeGain)
                generalSpikeGain = spikeTarget;
            else
                generalSpikeGain = generalSpikeRelease * generalSpikeGain + (1.0f - generalSpikeRelease) * spikeTarget;

            const float wetWeightedLevel = weightedLevel * rideGain * generalSpikeGain;
            generalWetShort = generalShortCoeff * generalWetShort + (1.0f - generalShortCoeff) * wetWeightedLevel;
            const float wetBaselineCoeff = generalWetShort > generalWetBaseline ? generalBaselineRiseCoeff : generalBaselineFallCoeff;
            generalWetBaseline = wetBaselineCoeff * generalWetBaseline + (1.0f - wetBaselineCoeff) * generalWetShort;

            const float dryShortDb = gainToDbFloor(generalShort);
            const float wetShortDb = gainToDbFloor(generalWetShort);
            const float dryBaselineDb = gainToDbFloor(generalBaseline);
            const float wetBaselineDb = gainToDbFloor(generalWetBaseline);
            const float shortLostDb = dryShortDb - wetShortDb;
            const float baselineLostDb = dryBaselineDb - wetBaselineDb;
            const float normalizeHeadroomDb = juce::Decibels::gainToDecibels(std::max(peakCeiling / std::max(peakAbs * rideGain * generalSpikeGain, 1.0e-5f), 1.0e-5f), 0.0f);
            const float desiredRecoverDb = std::max(0.0f,
                                                    shortLostDb
                                                        - (tuning.mixNormalizeShortThresholdBase
                                                           - tuning.mixNormalizeShortThresholdLevelWeight * level));
            const float baselineGuardDb = std::max(0.0f,
                                                   baselineLostDb
                                                       - (tuning.mixNormalizeBaselineThresholdBase
                                                          - tuning.mixNormalizeBaselineThresholdLevelWeight * level));
            const float allowedNormalizeDb = juce::jlimit(0.0f,
                                                          tuning.mixNormalizeMaxDb * level,
                                                          std::min(tuning.mixNormalizeMaxDb * level,
                                                                  std::min(desiredRecoverDb * (tuning.mixNormalizeShortScaleBase
                                                                                                + tuning.mixNormalizeShortScaleLevelWeight * level),
                                                                            baselineGuardDb * (tuning.mixNormalizeBaselineScaleBase
                                                                                                + tuning.mixNormalizeBaselineScaleLevelWeight * level) + 1.2f)));
            const float spikePenalty = clamp01((1.0f - generalSpikeGain) / 0.25f);
            const float normalizeTargetDb = neutral
                ? 0.0f
                : juce::jlimit(0.0f,
                               std::max(0.0f, normalizeHeadroomDb - 0.3f),
                               allowedNormalizeDb
                                   * (1.0f - tuning.mixNormalizeSpikePenalty * spikePenalty)
                                   * (0.55f + 0.45f * targetConfidence));
            const float normalizeCoeff = normalizeTargetDb > generalNormalizeGainDb ? generalNormalizeAttack : generalNormalizeRelease;
            generalNormalizeGainDb = normalizeCoeff * generalNormalizeGainDb
                + (1.0f - normalizeCoeff) * normalizeTargetDb;

            const float localFinalGain = juce::Decibels::decibelsToGain(generalNormalizeGainDb) * rideGain * generalSpikeGain;
            programDry = programLoudnessCoeff * programDry + (1.0f - programLoudnessCoeff) * weightedLevel;
            programWet = programLoudnessCoeff * programWet + (1.0f - programLoudnessCoeff) * (weightedLevel * localFinalGain);
            const float programDryDb = gainToDbFloor(programDry);
            const float programWetDb = gainToDbFloor(programWet);
            const float programmeLostDb = programDryDb - programWetDb;
            const float localPredictedPeak = std::max(peakAbs * localFinalGain, 1.0e-5f);
            const float programHeadroomDb = juce::Decibels::gainToDecibels(std::max(peakCeiling / localPredictedPeak, 1.0e-5f), 0.0f);
            const float restorePrimeScale = 1.0f - 0.40f * primeRelax;
            const float desiredProgramRestoreDb = neutral
                ? 0.0f
                : std::max(0.0f,
                           programmeLostDb - (1.10f + 0.35f * (1.0f - level)));
            const float maxProgramRestoreDb = 7.0f * (0.35f + 0.65f * level);
            const float restorePenalty = 1.0f - 0.40f * spikePenalty;
            const float programRestoreTargetDb = juce::jlimit(0.0f,
                                                              std::max(0.0f, programHeadroomDb - 0.15f),
                                                              std::min(maxProgramRestoreDb,
                                                                       desiredProgramRestoreDb
                                                                           * restorePenalty
                                                                           * restorePrimeScale
                                                                           * (0.50f + 0.50f * signalTrust)));
            const float restoreCoeff = programRestoreTargetDb > programRestoreGainDb ? programRestoreAttack : programRestoreRelease;
            programRestoreGainDb = restoreCoeff * programRestoreGainDb
                + (1.0f - restoreCoeff) * programRestoreTargetDb;
            const float finalGain = localFinalGain * juce::Decibels::decibelsToGain(programRestoreGainDb);
            for (int ch = 0; ch < numChannels; ++ch)
                buffer.setSample(ch, i, delayedSample[static_cast<size_t>(ch)] * finalGain);

            levelActivityAccum += (std::abs(generalRideGainDb)
                                   + 0.45f * std::abs(generalNormalizeGainDb)
                                   + 0.35f * std::abs(programRestoreGainDb))
                / std::max(6.0f, 10.5f * level);
            tameActivityAccum += std::abs(1.0f - generalSpikeGain);
            continue;
        }

        const float detectorLevel = std::max(monoAbs, peakAbs * 0.85f);
        const float envCoeff = detectorLevel > levelEnv ? levelEnvAttack : levelEnvRelease;
        levelEnv = envCoeff * levelEnv + (1.0f - envCoeff) * detectorLevel;
        const float anchorCoeff = levelEnv > anchorEnv ? anchorRiseCoeff : anchorFallCoeff;
        anchorEnv = anchorCoeff * anchorEnv + (1.0f - anchorCoeff) * levelEnv;
        if (voiceMode) {
            if (detector.phraseStart > 0.18f || vocalPhraseAnchor <= 1.0e-6f)
                vocalPhraseAnchor = levelEnv;
            const float phraseCoeff = levelEnv > vocalPhraseAnchor ? phraseAnchorRiseCoeff : phraseAnchorFallCoeff;
            if (detector.phraseActivity > 0.16f)
                vocalPhraseAnchor = phraseCoeff * vocalPhraseAnchor + (1.0f - phraseCoeff) * levelEnv;
            else if (detector.phraseEnd > 0.12f)
                vocalPhraseAnchor = 0.985f * vocalPhraseAnchor + 0.015f * levelEnv;
        } else {
            vocalPhraseAnchor = 0.0f;
        }

        const float safeEnv = std::max(levelEnv, 1.0e-5f);
        const float vocalAnchor = detector.phraseActivity > 0.08f
            ? std::max(vocalPhraseAnchor, 1.0e-5f)
            : std::max(anchorEnv, 1.0e-5f);
        const float safeAnchor = voiceMode ? vocalAnchor : std::max(anchorEnv, 1.0e-5f);
        const float envOverAnchor = clamp01((safeEnv - safeAnchor) / (safeAnchor + 1.0e-5f));
        const float envUnderAnchor = clamp01((safeAnchor - safeEnv) / (safeAnchor + 1.0e-5f));
        float targetGain = 1.0f;
        if (!neutral) {
            if (voiceMode) {
                const float levelRatio = safeAnchor / safeEnv;
                const float ratioGain = std::pow(levelRatio, levelShape);
                const float biasGain = juce::jlimit(0.55f,
                                                    1.45f,
                                                    1.0f + voiceDecision.levelBias * (0.8f + 0.7f * level));
                targetGain = juce::jlimit(maxDownwardGain,
                                          maxUpwardGain,
                                          ratioGain * biasGain);
            } else {
                const float envDb = juce::Decibels::gainToDecibels(safeEnv, -120.0f);
                const float anchorDb = juce::Decibels::gainToDecibels(safeAnchor, -120.0f);
                const float diffDb = anchorDb - envDb;
                const float deadbandDb = 1.4f - 0.5f * level;
                const float generalStrength = 0.34f + 0.46f * level;
                if (std::abs(diffDb) > deadbandDb) {
                    const float wantedGainDb = juce::jlimit(-9.0f * level,
                                                            1.8f * level,
                                                            (std::abs(diffDb) - deadbandDb)
                                                                * generalStrength
                                                                * (diffDb >= 0.0f ? 1.0f : -1.0f));
                    targetGain = juce::Decibels::decibelsToGain(wantedGainDb);
                }
                targetGain = juce::jlimit(maxDownwardGain, maxUpwardGain, targetGain);
            }
        }

        const float levellerCoeff = targetGain < levellerGain ? levelGainAttack : levelGainRelease;
        levellerGain = levellerCoeff * levellerGain + (1.0f - levellerCoeff) * targetGain;

        const float overrideZone = voiceMode ? clamp01((std::max(level, control) - 0.60f) / 0.40f) : 0.0f;
        const float problemWeight = voiceMode
            ? (targetState == MixState::voiceBuried
                   ? 1.0f
                   : (targetState == MixState::guitarDominant ? 0.72f
                                                              : (targetState == MixState::voiceLeading ? 0.10f : 0.24f)))
            : 0.0f;
        const float maskingPressure = clamp01(0.42f * detector.instrumentDominance
                                              + 0.34f * detector.buriedSpeech
                                              + 0.24f * envOverAnchor);
        const float overrideTrigger = clamp01((maskingPressure - (0.34f - 0.08f * overrideZone)) / 0.34f);
        const float speechNeed = clamp01(0.70f * detector.buriedSpeech
                                         + 0.25f * detector.speechPresence
                                         + 0.15f * envUnderAnchor
                                         + 0.08f * detector.phraseActivity);
        const float transientNeed = clamp01(0.65f * detector.instrumentDominance
                                            + 0.35f * detector.transientStrength);

        const float targetOverrideGain = voiceMode
            ? juce::jlimit(0.12f,
                           1.0f,
                           1.0f - overrideZone * problemWeight * overrideTrigger * (0.10f + 0.78f * level))
            : 1.0f;
        const float overrideGainCoeff = targetOverrideGain < overrideGain ? overrideAttack : overrideRelease;
        overrideGain = overrideGainCoeff * overrideGain + (1.0f - overrideGainCoeff) * targetOverrideGain;

        const float liftGain = voiceMode
            ? 1.0f + voiceDecision.speechLift * maxLiftAmount
            : 1.0f;
        const float liftCoeff = liftGain > speechLiftGain ? liftAttack : liftRelease;
        speechLiftGain = liftCoeff * speechLiftGain + (1.0f - liftCoeff) * liftGain;

        const float targetOverrideLiftGain = voiceMode
            ? juce::jlimit(1.0f,
                           3.2f,
                           1.0f + overrideZone * speechNeed * overrideTrigger
                                     * (0.14f + 1.35f * level + 0.12f * detector.phraseActivity))
            : 1.0f;
        const float overrideLiftCoeff = targetOverrideLiftGain > overrideLiftGain ? overrideAttack : overrideRelease;
        overrideLiftGain = overrideLiftCoeff * overrideLiftGain
            + (1.0f - overrideLiftCoeff) * targetOverrideLiftGain;

        const float highEnvCoeff = highAbs > highEnv ? tameAttack : tameRelease;
        highEnv = highEnvCoeff * highEnv + (1.0f - highEnvCoeff) * highAbs;

        float tame = 0.0f;
        if (neutral) {
            tame = 0.0f;
        } else if (voiceMode) {
            tame = voiceDecision.transientTame * maxTameDepth;
            if (targetState == MixState::voiceBuried)
                tame *= 1.3f;
        } else {
            const float relativeBrightness = highEnv / (safeEnv + 1.0e-5f);
            const float tameTrigger = clamp01((relativeBrightness - (0.34f - 0.06f * control)) / 0.42f);
            tame = tameTrigger * maxTameDepth * (1.0f - 0.22f * mixTiltPenalty);
        }
        tame = clamp01(tame);

        const float targetHighTameGain = juce::jlimit(0.12f, 1.0f, 1.0f - tame);
        const float tameCoeff = targetHighTameGain < highTameGain ? tameAttack : tameRelease;
        highTameGain = tameCoeff * highTameGain + (1.0f - tameCoeff) * targetHighTameGain;

        const float targetOverrideTameGain = voiceMode
            ? juce::jlimit(0.08f,
                           1.0f,
                           1.0f - overrideZone * problemWeight * transientNeed * overrideTrigger
                                     * (0.10f + 0.72f * control))
            : 1.0f;
        const float overrideTameCoeff = targetOverrideTameGain < overrideTameGain ? overrideAttack : overrideRelease;
        overrideTameGain = overrideTameCoeff * overrideTameGain
            + (1.0f - overrideTameCoeff) * targetOverrideTameGain;

        const float articulation = voiceMode
            ? (0.40f + 0.34f * detector.transientStrength + 0.10f * detector.intelligibility)
            : 0.0f;
        const float effectiveLevellerGain = levellerGain * overrideGain;
        const float effectiveSpeechLiftGain = speechLiftGain * overrideLiftGain;
        const float effectiveHighTameGain = juce::jlimit(0.05f, 1.0f, highTameGain * overrideTameGain);

        if (numChannels >= 2) {
            const float left = delayedSample[0];
            const float right = delayedSample[1];
            const float mid = 0.5f * (left + right);
            const float side = 0.5f * (left - right);

            const float speechMid = 0.5f * (speechBand[0] + speechBand[1]);
            const float speechSide = 0.5f * (speechBand[0] - speechBand[1]);
            const float highMid = 0.5f * (highBand[0] + highBand[1]);
            const float highSide = 0.5f * (highBand[0] - highBand[1]);

            float sideTameGain = highTameGain;
            if (voiceMode && (targetState == MixState::guitarDominant || targetState == MixState::voiceBuried)) {
                const float extraSideTame = (1.0f - effectiveHighTameGain)
                    * (0.35f + 0.65f * detector.stereoSpread)
                    * (0.45f + 0.55f * detector.instrumentDominance);
                sideTameGain = juce::jlimit(0.05f, 1.0f, effectiveHighTameGain - extraSideTame);
            } else {
                sideTameGain = effectiveHighTameGain;
            }

            const float sideSpeechBlend = voiceMode ? (0.12f + 0.10f * detector.stereoSpread) : 0.0f;
            const float outMid = effectiveLevellerGain
                * (mid
                   + articulation * (effectiveSpeechLiftGain - 1.0f) * speechMid
                   + (effectiveHighTameGain - 1.0f) * highMid);
            const float outSide = effectiveLevellerGain
                * (side
                   + sideSpeechBlend * articulation * (effectiveSpeechLiftGain - 1.0f) * speechSide
                   + (sideTameGain - 1.0f) * highSide);

            buffer.setSample(0, i, outMid + outSide);
            buffer.setSample(1, i, outMid - outSide);
            for (int ch = 2; ch < numChannels; ++ch)
                buffer.setSample(ch, i, effectiveLevellerGain * delayedSample[static_cast<size_t>(ch)]);
        } else {
            for (int ch = 0; ch < numChannels; ++ch) {
                const float x = delayedSample[static_cast<size_t>(ch)];
                const float y = effectiveLevellerGain
                    * (x
                       + articulation * (effectiveSpeechLiftGain - 1.0f) * speechBand[static_cast<size_t>(ch)]
                       + (effectiveHighTameGain - 1.0f) * highBand[static_cast<size_t>(ch)]);
                buffer.setSample(ch, i, y);
            }
        }

        levelActivityAccum += std::abs(1.0f - effectiveLevellerGain);
        liftActivityAccum += std::abs(effectiveSpeechLiftGain - 1.0f);
        tameActivityAccum += std::abs(1.0f - effectiveHighTameGain);
    }

    const float invSamples = 1.0f / static_cast<float>(numSamples);
    levelActivity = clamp01(levelActivityAccum * invSamples * 1.8f);
    liftActivity = clamp01(liftActivityAccum * invSamples * 1.2f);
    tameActivity = clamp01(tameActivityAccum * invSamples * 1.5f);
    if (neutral) {
        levelActivity = 0.0f;
        liftActivity = 0.0f;
        tameActivity = 0.0f;
    }

    if (!voiceMode) {
        globalTracker.update(gainToDbFloor(generalShort),
                             gainToDbFloor(generalMomentary),
                             gainToDbFloor(generalShort) > -72.0f,
                             numSamples);
        offlineActive = mixOfflineMode;
        if (mixOfflineMode && offlineAnalysis.isValid())
            offlineProcessedSamples += numSamples;
    } else {
        offlineActive = false;
    }
}

float Dsp::lowpassCoeff(const double sampleRate, const float cutoffHz) noexcept {
    const float srSafe = static_cast<float>(sampleRate > 1000.0 ? sampleRate : 48000.0);
    return std::exp(-2.0f * juce::MathConstants<float>::pi * cutoffHz / srSafe);
}

float Dsp::timeCoeff(const double sampleRate, const float seconds) noexcept {
    const float srSafe = static_cast<float>(sampleRate > 1000.0 ? sampleRate : 48000.0);
    return std::exp(-1.0f / std::max(1.0f, seconds * srSafe));
}

float Dsp::stepToward(const float current, const float target, const float maxDelta) noexcept {
    return current + juce::jlimit(-maxDelta, maxDelta, target - current);
}

float Dsp::shapeConfidence(const float confidence) noexcept {
    const float normalized = clamp01((confidence - 0.20f) / 0.50f);
    return normalized * normalized * (3.0f - 2.0f * normalized);
}

Dsp::MixTargetFrame Dsp::makeMixTargetFrame(const float shortDb,
                                            const float baselineDb,
                                            const float level) const noexcept {
    MixTargetFrame frame {};
    const float localBlend = tuning.mixTargetBlendBase
        + tuning.mixTargetBlendLevelWeight * (1.0f - level);

    frame.localTargetDb = baselineDb + localBlend * (shortDb - baselineDb);

    if (params.analysisMode == MixAnalysisMode::realtime) {
        frame.globalTargetDb = frame.localTargetDb;
        frame.finalTargetDb = frame.localTargetDb;
        return frame;
    }

    const float globalBaselineDb = globalTracker.getGlobalBaselineDb();
    const float globalUpperDb = globalTracker.getGlobalUpperDb();
    const float globalRangeDb = std::max(1.5f, globalTracker.getDynamicRangeDb());
    frame.confidence = shapeConfidence(globalTracker.getConfidence());
    frame.globalTargetDb = globalBaselineDb + (0.12f + 0.10f * (1.0f - level)) * (shortDb - globalBaselineDb);
    if (shortDb > globalUpperDb)
        frame.globalTargetDb = std::min(frame.globalTargetDb, globalUpperDb - 0.12f * globalRangeDb);
    frame.finalTargetDb = juce::jmap(frame.confidence, frame.localTargetDb, frame.globalTargetDb);
    return frame;
}

Dsp::MixState Dsp::detectState(const DetectorSnapshot& detector) noexcept {
    if (detector.buriedSpeech > 0.27f)
        return MixState::voiceBuried;
    if (detector.speechDominance > 0.52f)
        return MixState::voiceLeading;
    if (detector.instrumentDominance > 0.45f)
        return MixState::guitarDominant;
    return MixState::neutral;
}

Dsp::MixDecision Dsp::decide(const MixState state,
                             const DetectorSnapshot& detector,
                             const float level,
                             const float control) noexcept {
    MixDecision decision {};

    switch (state) {
        case MixState::voiceLeading:
            decision.levelBias = 0.08f;
            decision.speechLift = 0.42f * level;
            decision.transientTame = 0.24f * control;
            break;
        case MixState::guitarDominant:
            decision.levelBias = -0.22f;
            decision.speechLift = 0.82f * level;
            decision.transientTame = 0.95f * control;
            break;
        case MixState::voiceBuried:
            decision.levelBias = -0.46f;
            decision.speechLift = 1.55f * level;
            decision.transientTame = 1.40f * control;
            break;
        case MixState::neutral:
        default:
            decision.levelBias = 0.0f;
            decision.speechLift = 0.28f * level;
            decision.transientTame = 0.36f * control;
            break;
    }

    decision.speechLift *= 0.6f + 0.4f * detector.transientStrength;
    decision.transientTame *= 0.5f + 0.5f * detector.instrumentDominance;
    return decision;
}

Dsp::MixDecision Dsp::blendDecision(const MixDecision& a,
                                    const MixDecision& b,
                                    const float amount) noexcept {
    const float t = clamp01(amount);
    MixDecision blended {};
    blended.levelBias = juce::jmap(t, a.levelBias, b.levelBias);
    blended.speechLift = juce::jmap(t, a.speechLift, b.speechLift);
    blended.transientTame = juce::jmap(t, a.transientTame, b.transientTame);
    return blended;
}

} // namespace vxsuite::leveler
