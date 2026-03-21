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
    identity.selectorChoiceLabels = { "Voice", "General" };
    identity.defaultMode = vxsuite::Mode::general;
    identity.primaryLabel = "Level";
    identity.secondaryLabel = "Control";
    identity.primaryDefaultValue = 0.0f;
    identity.secondaryDefaultValue = 0.0f;
    identity.primaryHint = "Ride the performance toward a more even overall level without splitting the track.";
    identity.secondaryHint = "Firm up spikes and containment while keeping the performance feeling alive.";
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
        ? "Speech-aware adaptive levelling for instrument-to-camera performances"
        : "General adaptive levelling for whole-track consistency";
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
        case 0: return "Lift";
        case 1: return "Level";
        case 2: return "Tame";
        default: return {};
    }
}

void VXLevelerAudioProcessor::prepareSuite(const double sampleRate, const int samplesPerBlock) {
    currentSampleRateHz = sampleRate > 1000.0 ? sampleRate : 48000.0;
    detector.prepare(currentSampleRateHz, samplesPerBlock);
    dsp.prepare(currentSampleRateHz, samplesPerBlock, getTotalNumOutputChannels());
    setReportedLatencySamples(dsp.latencySamples());
    outputTrimmer.setReleaseSeconds(0.20f);
    resetSuite();
}

void VXLevelerAudioProcessor::resetSuite() {
    detector.reset();
    dsp.reset();
    outputTrimmer.reset();
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

    const auto detectorSnapshot = detector.analyse(buffer, getVoiceAnalysisSnapshot());
    dsp.setParams(params);
    dsp.process(buffer, detectorSnapshot);
    outputTrimmer.process(buffer, currentSampleRateHz);
}

#if !defined(VXSUITE_DISABLE_PLUGIN_ENTRYPOINT)
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new VXLevelerAudioProcessor();
}
#endif
