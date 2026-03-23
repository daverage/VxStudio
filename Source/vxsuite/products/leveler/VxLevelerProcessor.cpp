#include "VxLevelerProcessor.h"

#include "vxsuite/framework/VxSuiteBlockSmoothing.h"
#include "vxsuite/framework/VxSuiteParameters.h"

namespace {

constexpr std::string_view kProductName = "Leveler";
constexpr std::string_view kShortTag = "LVL";
constexpr std::string_view kLevelParam = "level";
constexpr std::string_view kControlParam = "control";
constexpr std::string_view kModeParam = "mode";

} // namespace

VXLevelerAudioProcessor::VXLevelerAudioProcessor()
    : ProcessorBase(makeIdentity()) {}

vxsuite::ProductIdentity VXLevelerAudioProcessor::makeIdentity() {
    vxsuite::ProductIdentity identity {};
    identity.productName = kProductName;
    identity.shortTag = kShortTag;
    identity.primaryParamId = kLevelParam;
    identity.secondaryParamId = kControlParam;
    identity.modeParamId = kModeParam;
    identity.selectorChoiceLabels = { "Vocal Rider", "Mix Leveler" };
    identity.defaultMode = vxsuite::Mode::general;
    identity.primaryLabel = "Level";
    identity.secondaryLabel = "Control";
    identity.primaryDefaultValue = 0.0f;
    identity.secondaryDefaultValue = 0.0f;
    identity.primaryHint = "Ride voice phrases or the full mix toward a more even perceived level.";
    identity.secondaryHint = "Set how firmly peaks and harsh bursts are contained without flattening the take.";
    identity.showLevelTrace = true;
    identity.stageType = vxsuite::StageType::mixed;
    identity.theme.accentRgb = { 0.82f, 0.64f, 0.24f };
    identity.theme.accent2Rgb = { 0.12f, 0.10f, 0.06f };
    identity.theme.backgroundRgb = { 0.06f, 0.05f, 0.04f };
    identity.theme.panelRgb = { 0.12f, 0.10f, 0.08f };
    identity.theme.textRgb = { 0.97f, 0.94f, 0.88f };
    return identity;
}

juce::String VXLevelerAudioProcessor::getStatusText() const {
    return vxsuite::readMode(parameters, productIdentity) == vxsuite::Mode::vocal
        ? "Intelligent vocal rider for speech-led performance recordings"
        : "Adaptive mix leveler for whole-track consistency";
}

int VXLevelerAudioProcessor::getActivityLightCount() const noexcept { return 3; }

float VXLevelerAudioProcessor::getActivityLight(int index) const noexcept {
    switch (index) {
        case 0: return dsp.getLiftActivity();
        case 1: return dsp.getLevelActivity();
        case 2: return dsp.getTameActivity();
        default: return 0.0f;
    }
}

std::string_view VXLevelerAudioProcessor::getActivityLightLabel(int index) const noexcept {
    switch (index) {
        case 0: return "Ride";
        case 1: return "Level";
        case 2: return "Protect";
        default: return {};
    }
}

void VXLevelerAudioProcessor::setDebugTuning(const vxsuite::leveler::Dsp::Tuning& tuning) noexcept {
    dsp.setTuning(tuning);
}

void VXLevelerAudioProcessor::prepareSuite(const double sampleRate, const int samplesPerBlock) {
    currentSampleRateHz = sampleRate > 1000.0 ? sampleRate : 48000.0;
    detector.prepare(currentSampleRateHz, samplesPerBlock);
    dsp.prepare(currentSampleRateHz, samplesPerBlock, getTotalNumOutputChannels());
    setReportedLatencySamples(dsp.latencySamples());
    resetSuite();
}

void VXLevelerAudioProcessor::resetSuite() {
    detector.reset();
    dsp.reset();
    smoothedLevel = 0.0f;
    smoothedControl = 0.0f;
    controlsPrimed = false;
}

void VXLevelerAudioProcessor::processProduct(juce::AudioBuffer<float>& buffer,
                                             juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0)
        return;

    const float levelTarget = vxsuite::readNormalized(parameters, productIdentity.primaryParamId, 0.0f);
    const float controlTarget = vxsuite::readNormalized(parameters, productIdentity.secondaryParamId, 0.0f);

    if (!controlsPrimed) {
        smoothedLevel = levelTarget;
        smoothedControl = controlTarget;
        controlsPrimed = true;
    } else {
        smoothedLevel = vxsuite::smoothBlockValue(smoothedLevel, levelTarget, currentSampleRateHz, numSamples, 0.080f);
        smoothedControl = vxsuite::smoothBlockValue(smoothedControl, controlTarget, currentSampleRateHz, numSamples, 0.080f);
    }

    vxsuite::leveler::Dsp::Params params {};
    params.level = juce::jlimit(0.0f, 1.0f, smoothedLevel);
    params.control = juce::jlimit(0.0f, 1.0f, smoothedControl);
    params.voiceMode = vxsuite::readMode(parameters, productIdentity) == vxsuite::Mode::vocal;

    const auto detectorSnapshot = detector.analyse(buffer,
                                                   getVoiceAnalysisSnapshot(),
                                                   getVoiceContextSnapshot());
    dsp.setParams(params);
    dsp.process(buffer, detectorSnapshot);
}

#if !defined(VXSUITE_DISABLE_PLUGIN_ENTRYPOINT)
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new VXLevelerAudioProcessor();
}
#endif
