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
    snapshot.offlineBlockIndex = lastOfflineBlockIndex;
    snapshot.offlineWaiting = offlineWaiting;
    return snapshot;
}

void Dsp::prepare(const double sampleRate, const int maxBlockSize, const int numChannels) {
    sr = sampleRate > 1000.0 ? sampleRate : 48000.0;
    // Invalidate offline analysis if sample rate has changed (block timing would be wrong).
    // prepare() never runs concurrently with process(), so both copies can be
    // cleared directly here.
    if (offlineAnalysisMaster.isValid() && std::abs(offlineAnalysisMaster.sampleRate - sr) > 1.0) {
        clearOfflineAnalysis();
        offlineAnalysis = {};
        offlineProcessedSamples = 0;
    }
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
    offlineAnalysisMaster = analysis;
    offlineMailbox.publish(std::move(analysis));
}

void Dsp::clearOfflineAnalysis() {
    offlineAnalysisMaster = {};
    offlineMailbox.publish({});
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
    rideGate = 0.0f;
    gateEnv = 0.0f;
    longTargetEnv = 0.0f;
    scEnv = 0.0f;
    scConfidence = 0.0f;
    longTargetSettleSamples = 0;
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
    lastOfflineBlockIndex = -1;
    rideGainDbSnapshot = 0.0f;
    rideReferenceDbSnapshot = -100.0f;
    rideGateSnapshot = 0.0f;
    offlineActive = false;
    offlineWaiting = false;
    liftActivity = 0.0f;
    levelActivity = 0.0f;
    tameActivity = 0.0f;
}

void Dsp::process(juce::AudioBuffer<float>& buffer, const DetectorSnapshot& detector,
                  const ProcessOptions& options) {
    juce::ScopedNoDenormals noDenormals;

    // Install any offline map published by the message thread (swap only —
    // no allocation; the displaced map is reclaimed via collectOfflineGarbage()).
    if (offlineMailbox.consume(offlineAnalysis)) {
        offlineProcessedSamples = 0;
        offlineMapSuppressed = false;
    }

    // Framework integration: ProcessOptions provides baseline protection context
    static_cast<void>(options);  // Use for future protection scaling if needed

    const int numChannels = std::min<int>({ buffer.getNumChannels(), static_cast<int>(channels.size()), 2 });
    const int numSamples = buffer.getNumSamples();
    if (numChannels <= 0 || numSamples <= 0)
        return;

    const float level = clamp01(params.level);
    const float control = clamp01(params.control);
    const bool voiceMode = params.voiceMode;
    const bool neutral = level < 1.0e-4f && control < 1.0e-4f;
    const float signalTrust = juce::jlimit(0.35f, 1.0f, 0.35f + 0.65f * clamp01(params.separationConfidence));
    const float compressionPenalty = clamp01(params.compressionScore);
    if (!voiceMode) {
        processGeneralMode(buffer, level, control, neutral, signalTrust, compressionPenalty);
        return;
    }

    const auto desiredState = neutral ? MixState::neutral : detectState(detector);
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

    const float levelEnvAttack = timeCoeff(sr, 0.035f);
    const float levelEnvRelease = timeCoeff(sr, 0.220f);
    const float anchorRiseCoeff = timeCoeff(sr, 1.750f);
    const float anchorFallCoeff = timeCoeff(sr, 1.750f);
    const float phraseAnchorRiseCoeff = timeCoeff(sr, 0.800f);
    const float phraseAnchorFallCoeff = timeCoeff(sr, 1.800f);
    const float levelGainAttack = timeCoeff(sr, 0.060f);
    const float levelGainRelease = timeCoeff(sr, 0.350f);
    const float liftAttack = timeCoeff(sr, 0.020f);
    const float liftRelease = timeCoeff(sr, 0.160f);
    const float tameAttack = timeCoeff(sr, 0.004f);
    const float tameRelease = timeCoeff(sr, 0.120f);
    const float overrideAttack = timeCoeff(sr, 0.010f);
    const float overrideRelease = timeCoeff(sr, 0.180f);
    const float gateOpenCoeff = timeCoeff(sr, 0.015f);
    const float gateCloseCoeff = timeCoeff(sr, 0.060f);
    const float gateEnvAttack = timeCoeff(sr, 0.004f);
    const float gateEnvRelease = timeCoeff(sr, 0.040f);
    const float gateRelaxCoeff = timeCoeff(sr, 6.0f);
    const float longTargetCoeff = timeCoeff(sr, 30.0f);
    const float longTargetSeedCoeff = timeCoeff(sr, 0.3f);
    const int longTargetSettleLimit = juce::roundToInt(static_cast<float>(sr));
    const float rideDeadbandDb = 1.2f - 0.6f * level;

    const float targetOffsetDb = clampf(params.targetOffsetDb, -12.0f, 12.0f);
    const float gateSenseDb = clampf(params.gateSenseDb, -12.0f, 12.0f);
    // With an analyzed take, pin the long-term reference to the take's active
    // median instead of learning it live - the whole performance rides toward
    // one known level, independent of playback position.
    const bool vocalOffline = params.analysisMode == MixAnalysisMode::offline
        && offlineAnalysis.isValid() && !offlineMapSuppressed;
    const float vocalOfflineRefDb = vocalOffline ? offlineAnalysis.activeMedianDb : 0.0f;

    // Sidechain (music) envelope: same ballistics as the vocal envelope so
    // the two levels stay comparable. Confidence crossfades between
    // music-referenced and self-referenced riding - music stopping hands the
    // reference back smoothly, never snaps.
    const float scEnvAttack = levelEnvAttack;
    const float scEnvRelease = levelEnvRelease;
    const float scConfidenceRise = timeCoeff(sr, 0.200f);
    const float scConfidenceFall = timeCoeff(sr, 2.0f);
    const float scLevelForBlock = scPresent ? std::max(scBlockLevel, 0.0f) : 0.0f;
    const float levelShape = 0.35f + 0.70f * level;
    const float maxUpwardGain = 1.0f + 1.05f * level;
    const float maxDownwardGain = std::max(0.24f, 1.0f - 0.55f * level);
    const float maxLiftAmount = 0.28f + 0.82f * level;
    const float maxTameDepth = 0.15f + 0.60f * control;

    float levelActivityAccum = 0.0f;
    float liftActivityAccum = 0.0f;
    float tameActivityAccum = 0.0f;
    float lastRefDb = rideReferenceDbSnapshot;

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

        const float detectorLevel = std::max(monoAbs, peakAbs * 0.85f);
        const float envCoeff = detectorLevel > levelEnv ? levelEnvAttack : levelEnvRelease;
        levelEnv = envCoeff * levelEnv + (1.0f - envCoeff) * detectorLevel;

        const bool gateWasClosed = rideGate < 0.05f;
        if (!gateWasClosed) {
            const float anchorCoeff = levelEnv > anchorEnv ? anchorRiseCoeff : anchorFallCoeff;
            anchorEnv = anchorCoeff * anchorEnv + (1.0f - anchorCoeff) * levelEnv;
            if (detector.phraseStart > 0.18f || vocalPhraseAnchor <= 1.0e-6f)
                vocalPhraseAnchor = levelEnv;
            const float phraseCoeff = levelEnv > vocalPhraseAnchor ? phraseAnchorRiseCoeff : phraseAnchorFallCoeff;
            if (detector.phraseActivity > 0.16f)
                vocalPhraseAnchor = phraseCoeff * vocalPhraseAnchor + (1.0f - phraseCoeff) * levelEnv;
            else if (detector.phraseEnd > 0.12f)
                vocalPhraseAnchor = 0.985f * vocalPhraseAnchor + 0.015f * levelEnv;
        }

        const float safeEnv = std::max(levelEnv, 1.0e-5f);
        const float safeAnchor = detector.phraseActivity > 0.08f
            ? std::max(vocalPhraseAnchor, 1.0e-5f)
            : std::max(anchorEnv, 1.0e-5f);
        const float envOverAnchor = clamp01((safeEnv - safeAnchor) / (safeAnchor + 1.0e-5f));
        const float envUnderAnchor = clamp01((safeAnchor - safeEnv) / (safeAnchor + 1.0e-5f));
        const float hotInstrumentGuard = clamp01((peakAbs - 0.46f) / 0.24f)
            * clamp01(0.62f * detector.instrumentDominance + 0.38f * detector.transientStrength);

        // The gate needs a much faster level detector than the ride envelope,
        // otherwise the fader pumps up before the gate can close.
        const float gateEnvCoeff = detectorLevel > gateEnv ? gateEnvAttack : gateEnvRelease;
        gateEnv = gateEnvCoeff * gateEnv + (1.0f - gateEnvCoeff) * detectorLevel;
        const float envDb = gainToDbFloor(std::max(gateEnv, 1.0e-5f));
        const float anchorDb = gainToDbFloor(safeAnchor);
        const float gateFromLevel = clamp01((envDb - (anchorDb - 16.0f - gateSenseDb)) / 8.0f);
        // Wide window: phrase evidence may hold the gate open through soft
        // syllables, but not once the level sits far below the anchor
        // (band-share detectors read room tone as speech).
        const float gateFromLevelWide = clamp01((envDb - (anchorDb - 24.0f - gateSenseDb)) / 10.0f);
        const float gateFromPhrase = clamp01(detector.phraseActivity / 0.12f);
        const float gateTargetRaw = clamp01(std::max(gateFromLevel * (0.15f + 0.85f * gateFromPhrase),
                                                      0.85f * gateFromPhrase * gateFromLevelWide));
        const float gateCoeff = gateTargetRaw > rideGate ? gateOpenCoeff : gateCloseCoeff;
        rideGate = gateCoeff * rideGate + (1.0f - gateCoeff) * gateTargetRaw;

        // Long-term held reference: only learns while the vocal is actually
        // present, so a loud chorus stays "loud" instead of becoming the new
        // normal within a couple of seconds.
        if (rideGate > 0.85f) {
            if (longTargetEnv <= 1.0e-6f)
                longTargetEnv = levelEnv;
            // Bootstrap by time, not level: the seed lands while levelEnv is
            // still attacking, so learn fast for the first second of
            // gate-open material, then hold (10 s) so a louder section stays
            // "loud" instead of becoming the new normal.
            const float coeff = longTargetSettleSamples < longTargetSettleLimit
                ? longTargetSeedCoeff
                : longTargetCoeff;
            longTargetEnv = coeff * longTargetEnv + (1.0f - coeff) * levelEnv;
            if (longTargetSettleSamples < longTargetSettleLimit)
                ++longTargetSettleSamples;
        }

        const float scEnvCoeff = scLevelForBlock > scEnv ? scEnvAttack : scEnvRelease;
        scEnv = scEnvCoeff * scEnv + (1.0f - scEnvCoeff) * scLevelForBlock;
        const float scTargetConfidence = (scPresent && gainToDbFloor(scEnv) > -60.0f) ? 1.0f : 0.0f;
        const float scConfCoeff = scTargetConfidence > scConfidence ? scConfidenceRise : scConfidenceFall;
        scConfidence = scConfCoeff * scConfidence + (1.0f - scConfCoeff) * scTargetConfidence;

        const float rideEnvDb = gainToDbFloor(safeEnv);
        const float longRefDb = vocalOffline
            ? vocalOfflineRefDb
            : gainToDbFloor(std::max(longTargetEnv, 1.0e-5f));
        const float selfRefDb = 0.35f * gainToDbFloor(safeAnchor)
            + 0.65f * longRefDb
            + targetOffsetDb;
        // Music-relative reference: sit the vocal a fixed voicing amount above
        // the sidechain, Target setting how hot it sits. Clamped so an extreme
        // sidechain can't drag the ride to the rails.
        const float scRefDb = clampf(gainToDbFloor(std::max(scEnv, 1.0e-5f))
                                         + 2.0f + targetOffsetDb,
                                     -42.0f, -6.0f);
        const float refDb = juce::jmap(scConfidence, selfRefDb, scRefDb);
        const float safeRef = juce::Decibels::decibelsToGain(refDb);
        lastRefDb = refDb;

        float targetGain = 1.0f;
        if (!neutral) {
            const float levelRatio = safeRef / safeEnv;
            const float ratioGain = std::pow(levelRatio, levelShape);
            const float biasGain = juce::jlimit(0.55f,
                                                1.45f,
                                                1.0f + voiceDecision.levelBias * (0.8f + 0.7f * level));
            const float voiceCapBase = clamp01((std::max(level, control) - 0.82f) / 0.18f);
            const float voiceLiftDrive = voiceCapBase * envUnderAnchor;
            const float voiceTameDrive = voiceCapBase * envOverAnchor;
            const float adaptiveMaxUpwardGain = juce::jmap(hotInstrumentGuard,
                                                           maxUpwardGain + (1.95f - maxUpwardGain) * voiceLiftDrive,
                                                           1.08f + 0.12f * envUnderAnchor);
            const float adaptiveMaxDownwardGain = juce::jmap(hotInstrumentGuard,
                                                             maxDownwardGain + (0.18f - maxDownwardGain) * voiceTameDrive,
                                                             0.14f + 0.10f * (1.0f - envOverAnchor));
            targetGain = juce::jlimit(adaptiveMaxDownwardGain,
                                      adaptiveMaxUpwardGain,
                                      ratioGain * biasGain);
        }

        const float heldGain = levellerGain;
        // Deadband: when the vocal already sits at the reference, the fader
        // rests instead of chasing sub-dB errors.
        if (std::abs(rideEnvDb - refDb) < rideDeadbandDb)
            targetGain = heldGain + 0.15f * (targetGain - heldGain);
        targetGain = heldGain + rideGate * (targetGain - heldGain);

        const float levellerCoeff = targetGain < levellerGain ? levelGainAttack : levelGainRelease;
        levellerGain = levellerCoeff * levellerGain + (1.0f - levellerCoeff) * targetGain;
        if (rideGate < 0.05f)
            levellerGain = gateRelaxCoeff * levellerGain + (1.0f - gateRelaxCoeff) * 1.0f;

        const float overrideZone = clamp01((std::max(level, control) - 0.60f) / 0.40f);
        const float problemWeight = targetState == MixState::voiceBuried
            ? 1.0f
            : (targetState == MixState::guitarDominant ? 0.72f
                                                        : (targetState == MixState::voiceLeading ? 0.10f : 0.24f));
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

        const float targetOverrideGain = juce::jlimit(0.12f,
                                                       1.0f,
                                                       1.0f - overrideZone * problemWeight * overrideTrigger * (0.10f + 0.78f * level));
        const float overrideGainCoeff = targetOverrideGain < overrideGain ? overrideAttack : overrideRelease;
        overrideGain = overrideGainCoeff * overrideGain + (1.0f - overrideGainCoeff) * targetOverrideGain;

        const float rawLiftGain = 1.0f + voiceDecision.speechLift * maxLiftAmount * (1.0f - 0.68f * hotInstrumentGuard);
        const float liftGain = 1.0f + rideGate * (rawLiftGain - 1.0f);
        const float liftCoeff = liftGain > speechLiftGain ? liftAttack : liftRelease;
        speechLiftGain = liftCoeff * speechLiftGain + (1.0f - liftCoeff) * liftGain;

        const float rawTargetOverrideLiftGain = juce::jlimit(1.0f,
                                                              3.2f,
                                                              1.0f + overrideZone * speechNeed * overrideTrigger
                                                                        * (0.14f + 1.35f * level + 0.12f * detector.phraseActivity)
                                                                        * (1.0f - 0.78f * hotInstrumentGuard));
        const float targetOverrideLiftGain = 1.0f + rideGate * (rawTargetOverrideLiftGain - 1.0f);
        const float overrideLiftCoeff = targetOverrideLiftGain > overrideLiftGain ? overrideAttack : overrideRelease;
        overrideLiftGain = overrideLiftCoeff * overrideLiftGain
            + (1.0f - overrideLiftCoeff) * targetOverrideLiftGain;

        const float highEnvCoeff = highAbs > highEnv ? tameAttack : tameRelease;
        highEnv = highEnvCoeff * highEnv + (1.0f - highEnvCoeff) * highAbs;

        float tame = 0.0f;
        if (!neutral) {
            tame = voiceDecision.transientTame * maxTameDepth;
            if (targetState == MixState::voiceBuried)
                tame *= 1.3f;
        }
        tame = clamp01(tame);

        const float targetHighTameGain = juce::jlimit(0.12f, 1.0f, 1.0f - tame);
        const float tameCoeff = targetHighTameGain < highTameGain ? tameAttack : tameRelease;
        highTameGain = tameCoeff * highTameGain + (1.0f - tameCoeff) * targetHighTameGain;

        const float targetOverrideTameGain = juce::jlimit(0.08f,
                                                           1.0f,
                                                           1.0f - overrideZone * problemWeight * transientNeed * overrideTrigger
                                                                     * (0.10f + 0.72f * control));
        const float overrideTameCoeff = targetOverrideTameGain < overrideTameGain ? overrideAttack : overrideRelease;
        overrideTameGain = overrideTameCoeff * overrideTameGain
            + (1.0f - overrideTameCoeff) * targetOverrideTameGain;

        const float articulation = 0.40f + 0.34f * detector.transientStrength + 0.10f * detector.intelligibility;
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
            if (targetState == MixState::guitarDominant || targetState == MixState::voiceBuried) {
                const float extraSideTame = (1.0f - effectiveHighTameGain)
                    * (0.35f + 0.65f * detector.stereoSpread)
                    * (0.45f + 0.55f * detector.instrumentDominance);
                sideTameGain = juce::jlimit(0.05f, 1.0f, effectiveHighTameGain - extraSideTame);
            } else {
                sideTameGain = effectiveHighTameGain;
            }

            const float sideSpeechBlend = 0.12f + 0.10f * detector.stereoSpread;
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

    rideGainDbSnapshot = neutral ? 0.0f : gainToDbFloor(levellerGain * overrideGain);
    rideReferenceDbSnapshot = lastRefDb;
    rideGateSnapshot = rideGate;
    offlineActive = vocalOffline;
    offlineWaiting = false;
    lastOfflineBlockIndex = -1;
}

void Dsp::processGeneralMode(juce::AudioBuffer<float>& buffer,
                             const float level,
                             const float control,
                             const bool neutral,
                             const float signalTrust,
                             const float compressionPenalty) noexcept {

    const int numChannels = std::min<int>({ buffer.getNumChannels(), static_cast<int>(channels.size()), 2 });
    const int numSamples = buffer.getNumSamples();
    if (numChannels <= 0 || numSamples <= 0)
        return;

    const float mixDecisionTrust = clamp01(signalTrust
                                           * (1.0f - 0.22f * clamp01(params.monoScore))
                                           * (1.0f - 0.18f * compressionPenalty));
    const float generalMomentaryCoeff = timeCoeff(sr, 0.40f);
    const float generalShortCoeff = timeCoeff(sr, 3.0f);
    const float generalBaselineRiseCoeff = timeCoeff(sr, 4.2f);
    const float generalBaselineFallCoeff = timeCoeff(sr, 8.0f);
    const float tameAttack = timeCoeff(sr, 0.004f);
    const float tameRelease = timeCoeff(sr, 0.180f);
    const float generalSpikeRelease = timeCoeff(sr, 0.085f);
    const float generalNormalizeAttack = timeCoeff(sr, 1.2f);
    const float generalNormalizeRelease = timeCoeff(sr, 3.0f);
    const float programLoudnessCoeff = timeCoeff(sr, 12.0f);
    const float programRestoreAttack = timeCoeff(sr, 2.2f);
    const float programRestoreRelease = timeCoeff(sr, 5.5f);
    const float targetOffsetDb = clampf(params.targetOffsetDb, -12.0f, 12.0f);
    // Gate knob in mix mode acts as ride sensitivity: right shrinks the
    // deadband (rides deeper into small deviations), left grows it (calmer).
    const float senseDeadbandTrimDb = -0.12f * clampf(params.gateSenseDb, -12.0f, 12.0f);
    const bool mixOfflineMode = params.analysisMode == MixAnalysisMode::offline
        && offlineAnalysis.isValid() && !offlineMapSuppressed;
    bool offlineMapIndexed = false;   // a curve entry is usable for this block
    bool offlineOutOfRange = false;   // map exists but timeline is outside it
    int offlineBlockIndex = 0;
    if (mixOfflineMode) {
        if (timelineSample >= 0 && offlineAnalysis.startSample >= 0) {
            const std::int64_t rel = timelineSample - offlineAnalysis.startSample;
            const std::int64_t index = rel / static_cast<std::int64_t>(std::max(1, offlineAnalysis.blockSize));
            if (rel >= 0 && index < static_cast<std::int64_t>(offlineAnalysis.targetCurveDb.size())) {
                offlineBlockIndex = static_cast<int>(index);
                offlineMapIndexed = true;
            } else {
                offlineOutOfRange = true;
            }
        } else {
            // No playhead position (or map from an older session): fall back to
            // the free-running counter, which assumes linear playback from the
            // last reset().
            offlineBlockIndex = std::clamp(static_cast<int>(offlineProcessedSamples / std::max(1, offlineAnalysis.blockSize)),
                                           0,
                                           std::max(0, static_cast<int>(offlineAnalysis.targetCurveDb.size()) - 1));
            offlineMapIndexed = true;
        }
    }
    const float offlineTargetDb = offlineMapIndexed
        ? offlineAnalysis.targetCurveDb[static_cast<size_t>(offlineBlockIndex)]
        : 0.0f;

    float levelActivityAccum = 0.0f;
    float tameActivityAccum = 0.0f;
    float lastTargetDb = rideReferenceDbSnapshot;

    for (int i = 0; i < numSamples; ++i) {
        std::array<float, 2> delayedSample { 0.0f, 0.0f };
        std::array<float, 2> presenceBandSample { 0.0f, 0.0f };
        std::array<float, 2> highBandSample { 0.0f, 0.0f };
        float monoAbs = 0.0f;
        float peakAbs = 0.0f;
        float lowAbs = 0.0f;
        float lowMidAbs = 0.0f;
        float presenceAbs = 0.0f;
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

            const float lowBand = state.lp150;
            const float lowMidBand = state.lp2000 - state.lp150;
            const float presenceBand = state.lp4000 - state.lp2000;
            const float highBand = delayed - state.lp4000;
            presenceBandSample[static_cast<size_t>(ch)] = presenceBand;
            highBandSample[static_cast<size_t>(ch)] = highBand;

            lowAbs += std::abs(lowBand);
            lowMidAbs += std::abs(lowMidBand);
            presenceAbs += std::abs(presenceBand);
            highAbs += std::abs(highBand);
        }
        delayWriteIndex = (delayWriteIndex + 1) % delaySamples;

        monoAbs /= static_cast<float>(numChannels);
        lowAbs /= static_cast<float>(numChannels);
        lowMidAbs /= static_cast<float>(numChannels);
        presenceAbs /= static_cast<float>(numChannels);
        highAbs /= static_cast<float>(numChannels);

        const float weightedLevel = 0.03f * lowAbs
            + 0.80f * monoAbs
            + 0.12f * lowMidAbs
            + 0.65f * presenceAbs
            + 0.24f * highAbs;
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

        const float primeRelax = params.analysisMode == MixAnalysisMode::smartRealtime && generalPrimeCooldownSamples > 0
            ? clamp01(static_cast<float>(generalPrimeCooldownSamples) / std::max(1.0f, static_cast<float>(sr) * 1.25f))
            : 0.0f;
        if (generalPrimeCooldownSamples > 0)
            --generalPrimeCooldownSamples;

        generalHighEnv = (highAbs > generalHighEnv ? tameAttack : tameRelease) * generalHighEnv
            + (1.0f - (highAbs > generalHighEnv ? tameAttack : tameRelease)) * highAbs;

        const float momentaryDb = gainToDbFloor(generalMomentary);
        const float shortDb = gainToDbFloor(generalShort);
        const float baselineDb = gainToDbFloor(generalBaseline);
        MixTargetFrame targetFrame {};
        if (offlineMapIndexed) {
            targetFrame = MixTargetFrame { baselineDb, offlineTargetDb, offlineTargetDb, 1.0f };
        } else if (offlineOutOfRange) {
            // Timeline is outside the analysed region: use the offline global
            // stats as a fixed target (mirrors makeMixTargetFrame's global
            // branch) so behaviour stays stable across loops and seeks
            // instead of silently drifting on a misaligned curve.
            float globalTargetDb = offlineAnalysis.globalMedianDb
                + 0.12f * (shortDb - offlineAnalysis.globalMedianDb);
            if (shortDb > offlineAnalysis.globalUpperDb) {
                const float rangeDb = std::max(1.5f, offlineAnalysis.globalDynamicRangeDb);
                globalTargetDb = std::min(globalTargetDb, offlineAnalysis.globalUpperDb - 0.12f * rangeDb);
            }
            targetFrame = MixTargetFrame { baselineDb, globalTargetDb, globalTargetDb, 0.9f };
        } else {
            targetFrame = makeMixTargetFrame(shortDb, baselineDb, level);
        }
        lastTargetDb = targetFrame.finalTargetDb + targetOffsetDb;
        const float errorDb = shortDb - lastTargetDb;
        const float targetConfidence = clamp01(targetFrame.confidence * mixDecisionTrust);
        const float deadbandDb = std::max(0.2f,
                                          (tuning.mixDeadbandBase - tuning.mixDeadbandLevelWeight * level)
                                              + (1.0f - targetConfidence) * 0.85f
                                              + senseDeadbandTrimDb);
        const float rampWidthDb = 2.00f - 0.45f * level;
        const float hotMixGuard = clamp01((peakAbs - 0.52f) / 0.24f)
            * (0.45f + 0.55f * compressionPenalty)
            * (0.35f + 0.65f * control);

        float rideTargetDb = 0.0f;
        if (!neutral && std::abs(errorDb) > deadbandDb) {
            const float excessDb = std::abs(errorDb) - deadbandDb;
            const float ramp = clamp01(excessDb / std::max(0.5f, rampWidthDb));
            const float shapedRamp = ramp * ramp * (3.0f - 2.0f * ramp);
            const float downLimitDb = juce::jmap(primeRelax,
                                                 (3.0f + 4.6f * level) * (0.90f + 0.14f * control),
                                                 1.6f + 2.0f * level) * (1.0f + 0.22f * hotMixGuard);
            const float upLimitDb = (1.4f + 3.2f * level) * (0.94f + 0.10f * control)
                * (1.0f - 0.92f * hotMixGuard);
            rideTargetDb = errorDb > 0.0f ? -downLimitDb * shapedRamp : upLimitDb * shapedRamp;
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
            const float spikeDb = std::min(5.8f + 5.0f * control, overshootExcess * (0.92f + 0.55f * control));
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
        const float normalizeShortThreshold = tuning.mixNormalizeShortThresholdBase
            + tuning.mixNormalizeShortThresholdLevelWeight * level
            + tuning.mixNormalizeSpikePenalty * clamp01((1.0f - generalSpikeGain) / 0.25f);
        const float normalizeBaselineThreshold = tuning.mixNormalizeBaselineThresholdBase
            + tuning.mixNormalizeBaselineThresholdLevelWeight * level;
        const float shortRecoverDb = std::max(0.0f, shortLostDb - normalizeShortThreshold)
            * (tuning.mixNormalizeShortScaleBase + tuning.mixNormalizeShortScaleLevelWeight * level);
        const float baselineRecoverDb = std::max(0.0f, baselineLostDb - normalizeBaselineThreshold)
            * (tuning.mixNormalizeBaselineScaleBase + tuning.mixNormalizeBaselineScaleLevelWeight * level);
        const float restoreLimitDb = neutral ? 0.0f : 0.65f + 3.4f * level;
        const float restoreTargetDb = juce::jlimit(0.0f,
                                                   restoreLimitDb,
                                                   std::max(shortRecoverDb, baselineRecoverDb));

        const float restoreCoeff = restoreTargetDb > programRestoreGainDb ? programRestoreAttack : programRestoreRelease;
        programRestoreGainDb = restoreCoeff * programRestoreGainDb
            + (1.0f - restoreCoeff) * restoreTargetDb;

        programDry = programLoudnessCoeff * programDry + (1.0f - programLoudnessCoeff) * weightedLevel;
        const float normalizedWetLevel = wetWeightedLevel * juce::Decibels::decibelsToGain(programRestoreGainDb);
        programWet = programLoudnessCoeff * programWet + (1.0f - programLoudnessCoeff) * normalizedWetLevel;

        float normalizeTargetDb = programRestoreGainDb;
        if (programWet > 1.0e-6f && programDry > 1.0e-6f) {
            const float programErrorDb = gainToDbFloor(programDry) - gainToDbFloor(programWet);
            const float normalizeErrorDb = clampf(programErrorDb, -2.0f, tuning.mixNormalizeMaxDb);
            normalizeTargetDb = std::max(normalizeTargetDb, normalizeErrorDb);
        }
        normalizeTargetDb = juce::jlimit(0.0f, tuning.mixNormalizeMaxDb, normalizeTargetDb);

        const float normalizeCoeff = normalizeTargetDb > generalNormalizeGainDb ? generalNormalizeAttack : generalNormalizeRelease;
        generalNormalizeGainDb = normalizeCoeff * generalNormalizeGainDb
            + (1.0f - normalizeCoeff) * normalizeTargetDb;

        const float localFinalGain =
            juce::Decibels::decibelsToGain(generalNormalizeGainDb) * rideGain * generalSpikeGain;
        const float upperShare = clamp01((presenceAbs + highAbs)
                                         / std::max(1.0e-6f, lowAbs + lowMidAbs + presenceAbs + highAbs));
        const float upperTame = 1.0f - 0.16f * level * control * upperShare;
        for (int ch = 0; ch < numChannels; ++ch) {
            const auto index = static_cast<size_t>(ch);
            const float upper = presenceBandSample[index] + highBandSample[index];
            const float shaped = delayedSample[index] + (upperTame - 1.0f) * upper;
            buffer.setSample(ch, i, shaped * localFinalGain);
        }

        levelActivityAccum += (std::abs(generalRideGainDb)
                               + 0.45f * std::abs(generalNormalizeGainDb)
                               + 0.30f * std::abs(programRestoreGainDb)) / 9.0f;
        tameActivityAccum += std::abs(1.0f - generalSpikeGain);
    }

    updateActivity(levelActivityAccum, 0.0f, tameActivityAccum, numSamples, neutral);
    globalTracker.update(gainToDbFloor(generalShort),
                         gainToDbFloor(generalMomentary),
                         gainToDbFloor(generalShort) > -72.0f,
                         numSamples);
    rideGainDbSnapshot = neutral
        ? 0.0f
        : generalRideGainDb + generalNormalizeGainDb + programRestoreGainDb
            + gainToDbFloor(std::max(generalSpikeGain, 1.0e-5f));
    rideReferenceDbSnapshot = lastTargetDb;
    rideGateSnapshot = generalPrimed ? 1.0f : 0.0f;
    offlineActive = offlineMapIndexed;
    offlineWaiting = offlineOutOfRange;
    lastOfflineBlockIndex = offlineMapIndexed ? offlineBlockIndex : -1;
    if (mixOfflineMode)
        offlineProcessedSamples += numSamples;
}

void Dsp::updateActivity(const float levelActivityAccum,
                         const float liftActivityAccum,
                         const float tameActivityAccum,
                         const int numSamples,
                         const bool neutral) noexcept {
    const float invSamples = 1.0f / static_cast<float>(std::max(1, numSamples));
    levelActivity = clamp01(levelActivityAccum * invSamples * 1.8f);
    liftActivity = clamp01(liftActivityAccum * invSamples * 1.2f);
    tameActivity = clamp01(tameActivityAccum * invSamples * 1.5f);
    if (neutral) {
        levelActivity = 0.0f;
        liftActivity = 0.0f;
        tameActivity = 0.0f;
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
        // The local target is the only reference in Realtime mode - trust it
        // fully. (Leaving confidence at 0 multiplied the ride target to zero,
        // which silently disabled riding in this mode.)
        frame.confidence = 1.0f;
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
