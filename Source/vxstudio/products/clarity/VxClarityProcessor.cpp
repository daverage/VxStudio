#include "VxClarityProcessor.h"

#include "../../framework/VxStudioHelpContent.h"
#include "../../framework/VxStudioParameters.h"
#include "VxStudioVersions.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr std::string_view kProductName = "Clarity";
constexpr std::string_view kShortTag = "CLA";
constexpr std::string_view kCleanParam = "clean";
constexpr std::string_view kFocusParam = "focus";
constexpr std::string_view kModeParam = "mode";

} // namespace

VXClarityAudioProcessor::VXClarityAudioProcessor()
    : ProcessorBase(makeIdentity(), makeParameterLayout(), makeBusesProperties()) {}

vxsuite::ProductIdentity VXClarityAudioProcessor::makeIdentity() {
    vxsuite::ProductIdentity identity {};
    identity.productName = kProductName;
    identity.shortTag = kShortTag;
    identity.primaryParamId = kCleanParam;
    identity.secondaryParamId = kFocusParam;
    identity.modeParamId = kModeParam;
    identity.defaultMode = vxsuite::Mode::general;
    identity.primaryLabel = "Clean";
    identity.secondaryLabel = "Focus";
    identity.primaryHint = "Remove unwanted thickness and overlap with a gentle, persistent cleanup curve.";
    identity.secondaryHint = "Shift the result between body and clarity without making the mix hollow or hyped.";
    identity.dspVersion = vxsuite::versions::plugins::clarity;
    identity.helpTitle = vxsuite::help::clarity.title;
    identity.helpHtml = vxsuite::help::clarity.html;
    identity.readmeSection = vxsuite::help::clarity.readmeSection;
    identity.theme.accentRgb = { 0.22f, 0.78f, 0.72f };
    identity.theme.accent2Rgb = { 0.06f, 0.12f, 0.11f };
    identity.theme.backgroundRgb = { 0.05f, 0.07f, 0.07f };
    identity.theme.panelRgb = { 0.09f, 0.13f, 0.13f };
    identity.theme.textRgb = { 0.90f, 0.98f, 0.97f };
    identity.primaryDefaultValue = 0.0f;
    identity.secondaryDefaultValue = 0.5f;
    return identity;
}

juce::AudioProcessor::BusesProperties VXClarityAudioProcessor::makeBusesProperties() {
    return juce::AudioProcessor::BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withInput("Sidechain", juce::AudioChannelSet::stereo(), false)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true);
}

juce::AudioProcessorValueTreeState::ParameterLayout VXClarityAudioProcessor::makeParameterLayout() {
    return vxsuite::createSimpleParameterLayout(makeIdentity());
}

juce::String VXClarityAudioProcessor::getStatusText() const {
    const auto policy = currentModePolicy();
    const bool active = controls.getPrimary() > 0.001f || std::abs(controls.getSecondary() - 0.5f) > 0.001f;
    return juce::String("Adaptive clarity - ") + vxsuite::toJuceString(policy.label)
        + " - "
        + (active ? "processing" : "neutral");
}

void VXClarityAudioProcessor::prepareSuite(const double sampleRate, const int samplesPerBlock) {
    currentSampleRateHz = sampleRate > 1000.0 ? sampleRate : 48000.0;
    dsp.prepare(currentSampleRateHz, samplesPerBlock, getTotalNumOutputChannels());
    resetSuite();
}

void VXClarityAudioProcessor::resetSuite() {
    dsp.reset();
    controls.reset(0.0f, 0.5f);
}

bool VXClarityAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto mainInput = layouts.getMainInputChannelSet();
    const auto mainOutput = layouts.getMainOutputChannelSet();
    if (mainInput != mainOutput)
        return false;
    if (mainInput != juce::AudioChannelSet::mono() && mainInput != juce::AudioChannelSet::stereo())
        return false;

    if (getBusCount(true) > 1) {
        const auto sidechain = layouts.getChannelSet(true, 1);
        if (sidechain != juce::AudioChannelSet::disabled()
            && sidechain != juce::AudioChannelSet::mono()
            && sidechain != juce::AudioChannelSet::stereo())
            return false;
    }

    return true;
}

void VXClarityAudioProcessor::processProduct(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0)
        return;

    const float cleanTarget = vxsuite::readNormalized(parameters, productIdentity.primaryParamId, 0.0f);
    const float focusTarget = vxsuite::readNormalized(parameters, productIdentity.secondaryParamId, 0.5f);
    const auto [smoothedClean, smoothedFocus] = controls.process(
        cleanTarget,
        focusTarget,
        currentSampleRateHz,
        numSamples,
        0.080f,
        0.090f);

    const auto policy = currentModePolicy();
    const auto signalQuality = getSignalQualitySnapshot();
    const bool voiceMode = policy.mode == vxsuite::Mode::vocal;

    vxsuite::clarity::Params params {};
    params.clean = juce::jlimit(0.0f, 1.0f, smoothedClean);
    params.focus = juce::jlimit(0.0f, 1.0f, smoothedFocus);
    params.voiceMode = voiceMode;
    params.monoScore = signalQuality.monoScore;
    params.compressionScore = signalQuality.compressionScore;
    params.tiltScore = signalQuality.tiltScore;
    params.separationConfidence = signalQuality.separationConfidence;
    params.speechFocus = policy.speechFocus;
    params.bodyRecovery = policy.bodyRecovery;
    params.guardStrictness = policy.guardStrictness;
    params.sourceProtect = policy.sourceProtect;

    const juce::AudioBuffer<float>* sidechainBuffer = nullptr;
    if (getBusCount(true) > 1) {
        auto sidechainView = getBusBuffer(buffer, true, 1);
        if (sidechainView.getNumChannels() > 0 && sidechainView.getNumSamples() > 0)
            sidechainBuffer = &sidechainView;
    }

    params.sidechainPresent = sidechainBuffer != nullptr;
    dsp.process(buffer, sidechainBuffer, params);
}

#if !defined(VXSUITE_DISABLE_PLUGIN_ENTRYPOINT) && !defined(VXSTUDIO_DISABLE_PLUGIN_ENTRYPOINT)
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new VXClarityAudioProcessor();
}
#endif
