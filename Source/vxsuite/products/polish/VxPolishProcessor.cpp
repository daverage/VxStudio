#include "VxPolishProcessor.h"

#include <cmath>

namespace {

constexpr std::string_view kSuiteName = "VX Suite";
constexpr std::string_view kProductName = "Polish";
constexpr std::string_view kShortTag = "PLS";
constexpr std::string_view kPolishParam   = "polish";
constexpr std::string_view kBodyParam     = "body";
constexpr std::string_view kFocusParam    = "focus";
constexpr std::string_view kModeParam     = "mode";
constexpr std::string_view kListenParam   = "listen";
constexpr std::string_view kDeMudOnParam  = "demud_on";
constexpr std::string_view kDeEssOnParam  = "deess_on";
constexpr std::string_view kHpfOnParam    = "hpf_on";
constexpr std::string_view kHiShelfOnParam = "hishelf_on";

float clamp01(const float value) {
    return juce::jlimit(0.0f, 1.0f, value);
}

float blockBlendAlpha(const double sampleRate, const int numSamples, const float timeSeconds) {
    if (sampleRate <= 1000.0 || numSamples <= 0 || timeSeconds <= 0.0f)
        return 1.0f;
    return 1.0f - std::exp(-static_cast<float>(numSamples) / (timeSeconds * static_cast<float>(sampleRate)));
}

float onePoleCoeff(const double sampleRate, const float cutoffHz) {
    if (sampleRate <= 0.0 || cutoffHz <= 0.0f)
        return 0.0f;
    return std::exp(-2.0f * juce::MathConstants<float>::pi * cutoffHz / static_cast<float>(sampleRate));
}

} // namespace

VXPolishAudioProcessor::VXPolishAudioProcessor()
    : ProcessorBase(makeIdentity(), vxsuite::createSimpleParameterLayout(makeIdentity())) {}

vxsuite::ProductIdentity VXPolishAudioProcessor::makeIdentity() {
    vxsuite::ProductIdentity identity {};
    identity.suiteName = kSuiteName;
    identity.productName = kProductName;
    identity.shortTag = kShortTag;
    identity.primaryParamId = kPolishParam;
    identity.secondaryParamId = kBodyParam;
    identity.tertiaryParamId = kFocusParam;
    identity.modeParamId = kModeParam;
    identity.listenParamId = kListenParam;
    identity.defaultMode = vxsuite::Mode::vocal;
    identity.primaryLabel = "Polish";
    identity.secondaryLabel = "Body";
    identity.tertiaryLabel = "Focus";
    identity.primaryHint = "Smooth mud, boxiness, harshness, and fizz with one smart macro.";
    identity.secondaryHint = "Restore useful low and low-mid weight after smoothing so the voice stays natural.";
    identity.tertiaryHint = "Steer correction from low-mid cleanup toward upper-mid and top-end smoothing.";
    identity.showLowShelfIcon  = true;
    identity.showHighShelfIcon = true;
    identity.lowShelfParamId   = kHpfOnParam;
    identity.highShelfParamId  = kHiShelfOnParam;
    identity.defaultLowShelf   = false;
    identity.defaultHighShelf  = false;
    identity.theme.accentRgb = { 0.86f, 0.42f, 0.16f };
    identity.theme.accent2Rgb = { 0.15f, 0.09f, 0.06f };
    identity.theme.backgroundRgb = { 0.07f, 0.05f, 0.04f };
    identity.theme.panelRgb = { 0.12f, 0.09f, 0.07f };
    identity.theme.textRgb = { 0.97f, 0.93f, 0.86f };
    return identity;
}

const juce::String VXPolishAudioProcessor::getName() const {
    return "VX Polish";
}

juce::String VXPolishAudioProcessor::getStatusText() const {
    if (isListenEnabled())
        return "Listen - removed trouble only";

    const bool isVoice = vxsuite::readMode(parameters, productIdentity) == vxsuite::Mode::vocal;
    return isVoice ? "Vocal - speech-safe smoothing with restrained lift"
                   : "General - broader cleanup with more full-range reach";
}

float VXPolishAudioProcessor::getLowShelfActivity()  const noexcept { return polishChain.getDeMudActivity(); }
float VXPolishAudioProcessor::getHighShelfActivity() const noexcept { return polishChain.getDeEssActivity(); }
int VXPolishAudioProcessor::getActivityLightCount() const noexcept { return 4; }
float VXPolishAudioProcessor::getActivityLight(int index) const noexcept {
    switch (index) {
        case 0: return polishChain.getDeEssActivity();
        case 1: return polishChain.getPlosiveActivity();
        case 2: return polishChain.getCompActivity();
        case 3: return polishChain.getLimiterActivity();
        default: return 0.0f;
    }
}
std::string_view VXPolishAudioProcessor::getActivityLightLabel(int index) const noexcept {
    switch (index) {
        case 0: return "De-ess";
        case 1: return "Plosive";
        case 2: return "Comp";
        case 3: return "Limit";
        default: return {};
    }
}

juce::AudioProcessorEditor* VXPolishAudioProcessor::createEditor() {
    return new vxsuite::EditorBase(*this);
}

void VXPolishAudioProcessor::prepareSuite(const double sampleRate, const int samplesPerBlock) {
    currentSampleRateHz = sampleRate > 1000.0 ? sampleRate : 48000.0;
    polishChain.prepare(currentSampleRateHz, samplesPerBlock, getTotalNumOutputChannels());
    resetSuite();
}

void VXPolishAudioProcessor::resetSuite() {
    polishChain.reset();
    smoothedDetrouble = 0.0f;
    smoothedBody = 0.0f;
    smoothedFocus = 0.5f;
    tonalLowLp = 0.0f;
    tonalLowMidLp = 0.0f;
    tonalPresenceLoLp = 0.0f;
    tonalPresenceHiLp = 0.0f;
    tonalAirLp = 0.0f;
    tonalInputEnv = 0.0f;
    tonalLowMidEnv = 0.0f;
    tonalPresenceEnv = 0.0f;
    tonalAirEnv = 0.0f;
    tonalNoiseFloorDb = -80.0f;
    controlsPrimed = false;
}

void VXPolishAudioProcessor::updateTonalAnalysis(const juce::AudioBuffer<float>& buffer, const int numSamples) {
    const int channels = buffer.getNumChannels();
    if (channels <= 0 || numSamples <= 0)
        return;

    const float lowA = onePoleCoeff(currentSampleRateHz, 180.0f);
    const float lowMidA = onePoleCoeff(currentSampleRateHz, 850.0f);
    const float presenceLoA = onePoleCoeff(currentSampleRateHz, 1800.0f);
    const float presenceHiA = onePoleCoeff(currentSampleRateHz, 5200.0f);
    const float airA = onePoleCoeff(currentSampleRateHz, 7200.0f);
    const float envA = std::exp(-1.0f / (0.040f * static_cast<float>(currentSampleRateHz)));
    const float noiseA = std::exp(-1.0f / (0.500f * static_cast<float>(currentSampleRateHz)));

    for (int i = 0; i < numSamples; ++i) {
        float mono = 0.0f;
        for (int ch = 0; ch < channels; ++ch)
            mono += buffer.getReadPointer(ch)[i];
        mono /= static_cast<float>(channels);

        tonalLowLp = lowA * tonalLowLp + (1.0f - lowA) * mono;
        tonalLowMidLp = lowMidA * tonalLowMidLp + (1.0f - lowMidA) * mono;
        tonalPresenceLoLp = presenceLoA * tonalPresenceLoLp + (1.0f - presenceLoA) * mono;
        tonalPresenceHiLp = presenceHiA * tonalPresenceHiLp + (1.0f - presenceHiA) * mono;
        tonalAirLp = airA * tonalAirLp + (1.0f - airA) * mono;

        const float lowMidBand = tonalLowMidLp - tonalLowLp;
        const float presenceBand = tonalPresenceHiLp - tonalPresenceLoLp;
        const float airBand = mono - tonalAirLp;
        const float monoAbs = std::abs(mono);

        tonalInputEnv = envA * tonalInputEnv + (1.0f - envA) * monoAbs;
        tonalLowMidEnv = envA * tonalLowMidEnv + (1.0f - envA) * std::abs(lowMidBand);
        tonalPresenceEnv = envA * tonalPresenceEnv + (1.0f - envA) * std::abs(presenceBand);
        tonalAirEnv = envA * tonalAirEnv + (1.0f - envA) * std::abs(airBand);

        const float envDb = juce::Decibels::gainToDecibels(tonalInputEnv + 1.0e-6f, -120.0f);
        if (envDb < tonalNoiseFloorDb)
            tonalNoiseFloorDb = noiseA * tonalNoiseFloorDb + (1.0f - noiseA) * envDb;
        else
            tonalNoiseFloorDb = 0.9995f * tonalNoiseFloorDb + 0.0005f * envDb;
    }
}

void VXPolishAudioProcessor::processProduct(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0)
        return;

    const float detroubleTarget = vxsuite::readNormalized(parameters, productIdentity.primaryParamId, 0.0f);
    const float bodyTarget = vxsuite::readNormalized(parameters, productIdentity.secondaryParamId, 0.0f);
    const float focusTarget = vxsuite::readNormalized(parameters, productIdentity.tertiaryParamId, 0.5f);

    if (!controlsPrimed) {
        smoothedDetrouble = detroubleTarget;
        smoothedBody = bodyTarget;
        smoothedFocus = focusTarget;
        controlsPrimed = true;
    } else {
        smoothedDetrouble += blockBlendAlpha(currentSampleRateHz, numSamples, 0.050f) * (detroubleTarget - smoothedDetrouble);
        smoothedBody += blockBlendAlpha(currentSampleRateHz, numSamples, 0.090f) * (bodyTarget - smoothedBody);
        smoothedFocus += blockBlendAlpha(currentSampleRateHz, numSamples, 0.070f) * (focusTarget - smoothedFocus);
    }

    const bool voiceMode = vxsuite::readMode(parameters, productIdentity) == vxsuite::Mode::vocal;
    const auto& modePolicy = currentModePolicy();
    const auto analysis = getVoiceAnalysisSnapshot();
    const float detrouble = clamp01(smoothedDetrouble);
    const float body = clamp01(smoothedBody);
    const float focus = clamp01(smoothedFocus);
    const float lowBias = 1.0f - focus;
    const float highBias = focus;

    updateTonalAnalysis(buffer, numSamples);

    const float inputEnv = std::max(tonalInputEnv, 1.0e-6f);
    const float lowMidRatio = juce::jlimit(0.0f, 1.5f, tonalLowMidEnv / inputEnv);
    const float presenceRatio = juce::jlimit(0.0f, 1.5f, tonalPresenceEnv / inputEnv);
    const float airRatio = juce::jlimit(0.0f, 1.5f, tonalAirEnv / inputEnv);
    const float mudExcess = juce::jlimit(0.0f, 1.0f, (lowMidRatio - 0.20f) / 0.22f);
    const float harshExcess = juce::jlimit(0.0f, 1.0f, (presenceRatio - 0.16f) / 0.20f);
    const float sizzleExcess = juce::jlimit(0.0f, 1.0f, (airRatio - 0.08f) / 0.16f);
    const float speechConfidence = juce::jlimit(0.0f, 1.0f,
                                                0.45f * analysis.speechPresence
                                              + 0.25f * analysis.speechStability
                                              + 0.30f * analysis.protectVoice);
    const float artifactRisk = juce::jlimit(0.0f, 1.0f,
                                            0.50f * analysis.transientRisk
                                          + 0.25f * analysis.tailLikelihood
                                          + 0.25f * (1.0f - analysis.speechStability));
    const float lowTrouble = juce::jlimit(0.0f, 1.0f,
                                          0.65f * mudExcess + 0.35f * (1.0f - analysis.directness));
    const float highTrouble = juce::jlimit(0.0f, 1.0f,
                                           0.55f * harshExcess + 0.45f * sizzleExcess);
    const float polishDrive = detrouble * (0.75f + 0.25f * juce::jmax(lowTrouble, highTrouble));
    const float protectBias = voiceMode ? modePolicy.sourceProtect : 0.45f * modePolicy.sourceProtect;
    const float recoveryNeed = juce::jlimit(0.0f, 1.0f,
                                            0.35f + 0.35f * analysis.protectVoice + 0.30f * mudExcess);

    const bool deMudOn = vxsuite::readBool(parameters, kDeMudOnParam, true);
    const bool deEssOn = vxsuite::readBool(parameters, kDeEssOnParam, true);
    const bool hpfOn     = vxsuite::readBool(parameters, kHpfOnParam,     false);
    const bool hiShelfOn = vxsuite::readBool(parameters, kHiShelfOnParam, false);

    vxsuite::polish::Dsp::Params params {};
    params.contentMode = voiceMode ? 0 : 1;
    params.deMud = deMudOn ? polishDrive * juce::jlimit(0.0f, 1.0f, (0.25f + 0.55f * lowBias) * (0.45f + 0.55f * lowTrouble)) : 0.0f;
    params.deEss = deEssOn ? polishDrive * juce::jlimit(0.0f, 1.0f, (0.15f + 0.75f * highBias) * (0.35f + 0.65f * highTrouble)) : 0.0f;
    params.plosive = polishDrive * juce::jlimit(0.0f, 1.0f,
                                                (voiceMode ? 0.10f : 0.06f)
                                              + 0.24f * analysis.transientRisk
                                              + 0.10f * lowBias);
    params.compress = polishDrive * juce::jlimit(0.0f, 1.0f,
                                                 (voiceMode ? 0.10f : 0.14f)
                                               + 0.18f * (1.0f - analysis.speechStability));
    params.troubleSmooth = deEssOn ? polishDrive * juce::jlimit(0.0f, 1.0f,
                                                      0.20f + 0.55f * highBias + 0.25f * highTrouble) : 0.0f;
    params.limit = juce::jlimit(0.12f, 0.58f,
                                0.16f + 0.20f * polishDrive + 0.12f * body + 0.10f * analysis.transientRisk);
    params.recovery = body * recoveryNeed * juce::jlimit(0.0f, 1.0f, 0.25f + 0.75f * polishDrive);
    params.voicePreserve = juce::jlimit(0.0f, 1.0f, 0.45f + 0.55f * protectBias);
    params.denoiseAmount = juce::jlimit(0.0f, 1.0f, 0.50f * polishDrive + 0.50f * highTrouble);
    params.artifactRisk = artifactRisk;
    params.compSidechainBoostDb = juce::jlimit(0.0f, 3.0f, 0.50f + 2.0f * analysis.speechPresence);
    params.sourcePreset = 0;
    params.speechLoudnessDb = juce::Decibels::gainToDecibels(inputEnv, -120.0f);
    params.proximityContext = juce::jlimit(0.0f, 1.0f, 0.55f * lowBias + 0.45f * (1.0f - analysis.directness));
    params.speechPresence = juce::jlimit(0.0f, 1.0f, speechConfidence);
    params.noiseFloorDb = juce::jlimit(-96.0f, -36.0f, tonalNoiseFloorDb);
    params.hpfOn     = hpfOn;
    params.hiShelfOn = hiShelfOn;

    polishChain.setParams(params);
    polishChain.processCorrective(buffer);
    polishChain.processRecovery(buffer);
    polishChain.processLimiter(buffer);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new VXPolishAudioProcessor();
}
