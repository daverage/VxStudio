#include "VxDenoiserProcessor.h"

#include <cmath>

namespace {

constexpr std::string_view kSuiteName    = "VX Suite";
constexpr std::string_view kProductName  = "Denoiser";
constexpr std::string_view kShortTag     = "DN";
constexpr std::string_view kCleanParam   = "clean";
constexpr std::string_view kGuardParam   = "guard";
constexpr std::string_view kModeParam    = "mode";
constexpr std::string_view kListenParam  = "listen";

} // namespace

VXDenoiserAudioProcessor::VXDenoiserAudioProcessor()
    : ProcessorBase(makeIdentity(), vxsuite::createSimpleParameterLayout(makeIdentity())) {}

vxsuite::ProductIdentity VXDenoiserAudioProcessor::makeIdentity() {
    vxsuite::ProductIdentity id {};
    id.suiteName        = kSuiteName;
    id.productName      = kProductName;
    id.shortTag         = kShortTag;
    id.primaryParamId   = kCleanParam;
    id.secondaryParamId = kGuardParam;
    id.modeParamId      = kModeParam;
    id.listenParamId    = kListenParam;
    id.defaultMode      = vxsuite::Mode::vocal;
    id.primaryLabel     = "Clean";
    id.secondaryLabel   = "Guard";
    id.primaryHint      = "Spectral noise reduction - how much noise to remove.";
    id.secondaryHint    = "Artifact protection - guards harmonics and transients from over-processing.";
    // Emerald green
    id.theme.accentRgb     = { 0.15f, 0.85f, 0.50f };
    id.theme.accent2Rgb    = { 0.04f, 0.10f, 0.06f };
    id.theme.backgroundRgb = { 0.04f, 0.06f, 0.05f };
    id.theme.panelRgb      = { 0.07f, 0.10f, 0.08f };
    id.theme.textRgb       = { 0.85f, 0.95f, 0.88f };
    id.primaryDefaultValue = 0.5f;
    id.secondaryDefaultValue = 0.5f;
    return id;
}

const juce::String VXDenoiserAudioProcessor::getName() const {
    return "VX Denoiser";
}

juce::String VXDenoiserAudioProcessor::getStatusText() const {
    if (isListenEnabled())
        return "Listen - removed noise only";
    const bool isVoice = vxsuite::readMode(parameters, productIdentity)
                      == vxsuite::Mode::vocal;
    return isVoice ? "Vocal - OM-LSA denoiser with harmonic guard"
                   : "General - broadband spectral noise reduction";
}

juce::AudioProcessorEditor* VXDenoiserAudioProcessor::createEditor() {
    return new vxsuite::EditorBase(*this);
}

void VXDenoiserAudioProcessor::prepareSuite(const double sampleRate,
                                             const int    samplesPerBlock) {
    currentSampleRateHz = sampleRate > 1000.0 ? sampleRate : 48000.0;
    stageChain.prepare(currentSampleRateHz, samplesPerBlock);
    setReportedLatencySamples(stageChain.totalLatencySamples());
    resetSuite();
}

void VXDenoiserAudioProcessor::resetSuite() {
    stageChain.reset();
    smoothedClean  = 0.0f;
    smoothedGuard  = 0.5f;
    controlsPrimed = false;
}

void VXDenoiserAudioProcessor::processProduct(juce::AudioBuffer<float>& buffer,
                                               juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0) return;

    const float cleanTarget = vxsuite::readNormalized(parameters, kCleanParam, 0.5f);
    const float guardTarget = vxsuite::readNormalized(parameters, kGuardParam, 0.5f);

    if (!controlsPrimed) {
        smoothedClean  = cleanTarget;
        smoothedGuard  = guardTarget;
        controlsPrimed = true;
    } else {
        smoothedClean = vxsuite::smoothBlockValue(smoothedClean, cleanTarget, currentSampleRateHz, numSamples, 0.060f);
        smoothedGuard = vxsuite::smoothBlockValue(smoothedGuard, guardTarget, currentSampleRateHz, numSamples, 0.080f);
    }

    const bool isVoice  = vxsuite::readMode(parameters, productIdentity)
                       == vxsuite::Mode::vocal;
    const auto& policy  = currentModePolicy();

    // Map user controls + ModePolicy onto ProcessOptions
    vxsuite::ProcessOptions opts;
    const float effectiveClean = isVoice ? vxsuite::clamp01(0.58f * smoothedClean)
                                         : vxsuite::clamp01(0.72f * smoothedClean);
    opts.isVoiceMode        = isVoice;
    opts.sourceProtect      = vxsuite::clamp01(0.35f + 0.65f * smoothedGuard * policy.sourceProtect);
    opts.lateTailAggression = policy.lateTailAggression;
    opts.guardStrictness    = vxsuite::clamp01(0.40f + 0.60f * smoothedGuard * policy.guardStrictness);
    opts.speechFocus        = isVoice ? juce::jmax(0.60f, policy.speechFocus) : policy.speechFocus;

    stageChain.processInPlace(buffer, { effectiveClean }, opts);

    ensureLatencyAlignedListenDry(numSamples);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new VXDenoiserAudioProcessor();
}
