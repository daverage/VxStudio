#include "VXStudioAnalyserProcessor.h"

#include "VXStudioAnalyserEditor.h"
#include "../../framework/VxStudioHelpContent.h"
#include "../../framework/VxStudioSpectrumTelemetry.h"
#include "VxStudioVersions.h"

#include <limits>

namespace {

constexpr std::string_view kProductName = "VX Studio Analyser";
constexpr std::string_view kShortTag = "VSA";
constexpr std::string_view kStageId = "vx.studio.analyser";

} // namespace

VXStudioAnalyserAudioProcessor::VXStudioAnalyserAudioProcessor()
    : juce::AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                            .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      identity(makeIdentity()),
      analysisDomainIdValue(0) {
    ensureAnalysisDomain();
}

VXStudioAnalyserAudioProcessor::~VXStudioAnalyserAudioProcessor() {
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

void VXStudioAnalyserAudioProcessor::ensureAnalysisDomain() noexcept {
    if (analysisDomainIdValue != 0)
        return;
    // Register the analyser domain as the central analysis point for this process
    analysisDomainIdValue = vxsuite::analysis::DomainRegistry::instance().registerAnalyserDomain(kStageId);
    if (analysisDomainIdValue == 0) {
        // If registration fails, try to bind to an existing analyser domain
        // This handles cases where another plugin already registered one
        analysisDomainIdValue = vxsuite::analysis::DomainRegistry::instance().fallbackDomainIdForCurrentProcess();
    }
}

void VXStudioAnalyserAudioProcessor::prepareToPlay(const double sampleRate, const int samplesPerBlock) {
    ensureAnalysisDomain();
    signalQualityState.prepare(sampleRate, samplesPerBlock);
    publishSignalQualitySnapshot();
}

void VXStudioAnalyserAudioProcessor::releaseResources() {
    signalQualityState.reset();
    publishSignalQualitySnapshot();
}

void VXStudioAnalyserAudioProcessor::reset() {
    signalQualityState.reset();
    publishSignalQualitySnapshot();
}

void VXStudioAnalyserAudioProcessor::analyzePublishedStages() noexcept {
    using namespace vxsuite::analysis;

    auto& stageRegistry = StageRegistry::instance();
    const int maxSlots = stageRegistry.maxSlots();
    const auto nowMs = static_cast<std::uint64_t>(juce::Time::currentTimeMillis());
    constexpr std::uint64_t kStaleThresholdMs = 2000;  // Consider stages stale after 2 seconds

    StageView firstStage {};
    StageView lastStage {};
    std::uint64_t lowestOrderId = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t highestOrderId = 0;
    int stageCount = 0;

    // Discover active, recent VXSuite stages (like the editor does)
    for (int slotIndex = 0; slotIndex < maxSlots; ++slotIndex) {
        StageView stage {};
        if (!stageRegistry.readStage(slotIndex, stage))
            continue;

        // Must be active
        if (!stage.active)
            continue;

        // Skip stale stages (help with stability in edge cases)
        const auto stageAgeMs = nowMs - stage.telemetry.state.timestampMs;
        if (stageAgeMs > kStaleThresholdMs)
            continue;

        stageCount++;

        // Track first and last stages by order ID
        const auto orderId = stage.telemetry.identity.localOrderId;

        if (stageCount == 1) {
            firstStage = stage;
            lastStage = stage;
            lowestOrderId = orderId;
            highestOrderId = orderId;
        } else {
            if (orderId <= lowestOrderId) {
                firstStage = stage;
                lowestOrderId = orderId;
            }
            if (orderId >= highestOrderId) {
                lastStage = stage;
                highestOrderId = orderId;
            }
        }
    }

    // Update analysis if we found any valid stages
    if (stageCount > 0) {
        signalQualityState.updateFromPublishedSummaries(
            firstStage.telemetry.inputSummary,
            lastStage.telemetry.outputSummary
        );
    }
}

void VXStudioAnalyserAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ignoreUnused(midi);
    // Analyser is a pure passthrough that only analyzes signal quality
    // Analyzes both the real-time audio buffer and published stage data from other effects
    signalQualityState.update(buffer, buffer.getNumSamples());
    analyzePublishedStages();
    publishSignalQualitySnapshot();
}

void VXStudioAnalyserAudioProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ignoreUnused(midi);
    signalQualityState.update(buffer, buffer.getNumSamples());
    analyzePublishedStages();
    publishSignalQualitySnapshot();
}

bool VXStudioAnalyserAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto input = layouts.getMainInputChannelSet();
    const auto output = layouts.getMainOutputChannelSet();
    return input == output && (input == juce::AudioChannelSet::mono() || input == juce::AudioChannelSet::stereo());
}

juce::AudioProcessorEditor* VXStudioAnalyserAudioProcessor::createEditor() {
    return new VXStudioAnalyserEditor(*this);
}

#if !defined(VXSUITE_DISABLE_PLUGIN_ENTRYPOINT) && !defined(VXSTUDIO_DISABLE_PLUGIN_ENTRYPOINT)
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new VXStudioAnalyserAudioProcessor();
}
#endif
