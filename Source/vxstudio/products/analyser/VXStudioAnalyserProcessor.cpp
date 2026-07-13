#include "VXStudioAnalyserProcessor.h"

#include "VXStudioAnalyserEditor.h"
#include "../../framework/VxStudioHelpContent.h"
#include "../../framework/VxStudioSpectrumTelemetry.h"
#include "VxStudioVersions.h"

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
    if (analysisDomainIdValue != 0) {
        vxsuite::analysis::DomainRegistry::instance().unregisterAnalyserDomain(analysisDomainIdValue);
        analysisDomainIdValue = 0;
    }
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
        // If registration fails, bind to an existing active analyser domain before
        // falling back to a synthetic process-local domain.
        auto& domainRegistry = vxsuite::analysis::DomainRegistry::instance();
        vxsuite::analysis::DomainView activeDomain {};
        if (domainRegistry.latestDomainForProcess(domainRegistry.currentProcessId(), activeDomain))
            analysisDomainIdValue = activeDomain.analysisDomainId;
        else
            analysisDomainIdValue = domainRegistry.fallbackDomainIdForCurrentProcess();
    }
}

void VXStudioAnalyserAudioProcessor::prepareToPlay(const double sampleRate, const int samplesPerBlock) {
    ensureAnalysisDomain();
    signalQualityState.prepare(sampleRate, samplesPerBlock);

    if (!liveAccumulator)
        liveAccumulator = std::make_unique<vxsuite::analysis::SummaryAccumulator>();
    liveAccumulator->prepare(sampleRate, samplesPerBlock);

    publishSignalQualitySnapshot();
}

void VXStudioAnalyserAudioProcessor::releaseResources() {
    signalQualityState.reset();
    if (liveAccumulator) liveAccumulator->reset();
    liveInputSnapshotValid.store(false, std::memory_order_relaxed);
    publishSignalQualitySnapshot();
}

void VXStudioAnalyserAudioProcessor::reset() {
    signalQualityState.reset();
    if (liveAccumulator) liveAccumulator->reset();
    liveInputSnapshotValid.store(false, std::memory_order_relaxed);
    publishSignalQualitySnapshot();
}

void VXStudioAnalyserAudioProcessor::updateTrackProperties(const TrackProperties& properties) {
    if (properties.name.has_value() && properties.name->isNotEmpty())
        analyserTrackName = *properties.name;

    const juce::String channelUID = properties.channelUID.has_value() ? *properties.channelUID : juce::String();
    const std::int64_t runtimeID  = properties.runtimeID.has_value()  ? *properties.runtimeID  : 0;

    const auto id = vxsuite::analysis::StageRegistry::buildTrackStableId(channelUID, runtimeID, analyserTrackName);
    analyserTrackStableId.store(id, std::memory_order_relaxed);

    // Stamp the analyser's registered domain with the track scope key so VX plugins
    // on this track will prefer this domain during their next domain binding refresh.
    if (id != 0)
        vxsuite::analysis::DomainRegistry::instance().updateDomainContextKey(analysisDomainId(), id);
}

void VXStudioAnalyserAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ignoreUnused(midi);
    isBypassed.store(false, std::memory_order_relaxed);
    signalQualityState.update(buffer, buffer.getNumSamples());
    publishSignalQualitySnapshot();

    if (liveAccumulator) {
        liveAccumulator->update(buffer);
        publishLiveInputSummary();
    }
}

void VXStudioAnalyserAudioProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ignoreUnused(buffer, midi);
    isBypassed.store(true, std::memory_order_relaxed);
    signalQualityState.reset();
    if (liveAccumulator) liveAccumulator->reset();
    liveInputSnapshotValid.store(false, std::memory_order_relaxed);
    publishSignalQualitySnapshot();
}

bool VXStudioAnalyserAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto input = layouts.getMainInputChannelSet();
    const auto output = layouts.getMainOutputChannelSet();
    return input == output && (input == juce::AudioChannelSet::mono() || input == juce::AudioChannelSet::stereo());
}

void VXStudioAnalyserAudioProcessor::publishLiveInputSummary() noexcept {
    if (!liveAccumulator) return;
    const auto snap = liveAccumulator->summary();
    liveInputVersion.fetch_add(1, std::memory_order_acq_rel);  // version → odd (writing)
    liveInputSnapshot = snap;
    liveInputVersion.fetch_add(1, std::memory_order_release);  // version → even (done)
    liveInputSnapshotValid.store(true, std::memory_order_release);
}

vxsuite::analysis::AnalysisSummary VXStudioAnalyserAudioProcessor::liveInputSummary() const noexcept {
    vxsuite::analysis::AnalysisSummary snap;
    uint32_t v;
    do {
        do { v = liveInputVersion.load(std::memory_order_acquire); } while (v & 1);
        snap = liveInputSnapshot;
        std::atomic_thread_fence(std::memory_order_acquire);
    } while (liveInputVersion.load(std::memory_order_acquire) != v);
    return snap;
}

bool VXStudioAnalyserAudioProcessor::liveInputSummaryValid() const noexcept {
    return liveInputSnapshotValid.load(std::memory_order_acquire);
}

juce::AudioProcessorEditor* VXStudioAnalyserAudioProcessor::createEditor() {
    return new VXStudioAnalyserEditor(*this);
}

#if !defined(VXSTUDIO_DISABLE_PLUGIN_ENTRYPOINT)
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new VXStudioAnalyserAudioProcessor();
}
#endif
