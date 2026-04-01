#include "VXStudioAnalyserProcessor.h"

#include "VXStudioAnalyserEditor.h"
#include "../../framework/VxStudioHelpContent.h"
#include "VxStudioVersions.h"

namespace {

constexpr std::string_view kProductName = "Studio Analyser";
constexpr std::string_view kShortTag = "VSA";
constexpr std::string_view kStageId = "vx.studio.analyser";

} // namespace

VXStudioAnalyserAudioProcessor::VXStudioAnalyserAudioProcessor()
    : juce::AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                            .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      identity(makeIdentity()),
      analysisDomainIdValue(vxsuite::analysis::DomainRegistry::instance().registerAnalyserDomain(kStageId)),
      stagePublisher(identity) {}

VXStudioAnalyserAudioProcessor::~VXStudioAnalyserAudioProcessor() {
    vxsuite::analysis::DomainRegistry::instance().unregisterAnalyserDomain(analysisDomainIdValue);
}

vxsuite::ProductIdentity VXStudioAnalyserAudioProcessor::makeIdentity() {
    vxsuite::ProductIdentity identity {};
    identity.productName = kProductName;
    identity.shortTag = kShortTag;
    identity.stageId = kStageId;
    identity.dspVersion = vxsuite::versions::plugins::analyser;
    identity.helpTitle = vxsuite::help::analyser.title;
    identity.helpHtml = vxsuite::help::analyser.html;
    identity.readmeSection = vxsuite::help::analyser.readmeSection;
    identity.stageType = vxsuite::StageType::mixed;
    identity.theme.accentRgb = { 0.32f, 0.90f, 0.95f };
    identity.theme.accent2Rgb = { 0.07f, 0.15f, 0.18f };
    identity.theme.backgroundRgb = { 0.03f, 0.05f, 0.08f };
    identity.theme.panelRgb = { 0.06f, 0.09f, 0.13f };
    identity.theme.textRgb = { 0.88f, 0.95f, 0.98f };
    return identity;
}

vxsuite::SignalQualitySnapshot VXStudioAnalyserAudioProcessor::getSignalQualitySnapshot() const noexcept {
    return {
        monoScore.load(std::memory_order_relaxed),
        compressionScore.load(std::memory_order_relaxed),
        tiltScore.load(std::memory_order_relaxed),
        separationConfidence.load(std::memory_order_relaxed)
    };
}

void VXStudioAnalyserAudioProcessor::publishSignalQualitySnapshot() noexcept {
    const auto snapshot = signalQualityState.snapshot();
    monoScore.store(snapshot.monoScore, std::memory_order_relaxed);
    compressionScore.store(snapshot.compressionScore, std::memory_order_relaxed);
    tiltScore.store(snapshot.tiltScore, std::memory_order_relaxed);
    separationConfidence.store(snapshot.separationConfidence, std::memory_order_relaxed);
}

void VXStudioAnalyserAudioProcessor::prepareToPlay(const double sampleRate, const int samplesPerBlock) {
    stagePublisher.prepare(sampleRate, samplesPerBlock);
    signalQualityState.prepare(sampleRate, samplesPerBlock);
    publishSignalQualitySnapshot();
}

void VXStudioAnalyserAudioProcessor::releaseResources() {
    stagePublisher.reset();
    signalQualityState.reset();
    publishSignalQualitySnapshot();
}

void VXStudioAnalyserAudioProcessor::reset() {
    stagePublisher.reset();
    signalQualityState.reset();
    publishSignalQualitySnapshot();
}

void VXStudioAnalyserAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ignoreUnused(midi);
    signalQualityState.update(buffer, buffer.getNumSamples());
    publishSignalQualitySnapshot();
    stagePublisher.publish(buffer, buffer, false);
}

void VXStudioAnalyserAudioProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ignoreUnused(midi);
    signalQualityState.update(buffer, buffer.getNumSamples());
    publishSignalQualitySnapshot();
    stagePublisher.publishBypassed(buffer);
}

bool VXStudioAnalyserAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto input = layouts.getMainInputChannelSet();
    const auto output = layouts.getMainOutputChannelSet();
    return input == output && (input == juce::AudioChannelSet::mono() || input == juce::AudioChannelSet::stereo());
}

juce::AudioProcessorEditor* VXStudioAnalyserAudioProcessor::createEditor() {
    return new VXStudioAnalyserEditor(*this);
}

#if !defined(VXSUITE_DISABLE_PLUGIN_ENTRYPOINT)
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new VXStudioAnalyserAudioProcessor();
}
#endif
