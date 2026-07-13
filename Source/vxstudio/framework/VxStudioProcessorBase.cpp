#include "VxStudioProcessorBase.h"
#include "VxStudioEditorBase.h"

#include <utility>

namespace vxsuite {

ProcessorBase::ProcessorBase(ProductIdentity identity)
    : ProcessorBase(identity, createSimpleParameterLayout(identity)) {}

juce::AudioProcessor::BusesProperties ProcessorBase::makeDefaultBuses(const bool wantsSidechain) {
    auto buses = BusesProperties()
                     .withInput("Input", juce::AudioChannelSet::stereo(), true)
                     .withOutput("Output", juce::AudioChannelSet::stereo(), true);
    if (wantsSidechain)
        buses = buses.withInput("Sidechain", juce::AudioChannelSet::stereo(), false);
    return buses;
}

ProcessorBase::ProcessorBase(ProductIdentity identity,
                             juce::AudioProcessorValueTreeState::ParameterLayout parameterLayout)
    : ProcessorBase(std::move(identity), std::move(parameterLayout),
                    makeDefaultBuses(identity.wantsSidechainInput)) {}

ProcessorBase::ProcessorBase(ProductIdentity identity,
                             juce::AudioProcessorValueTreeState::ParameterLayout parameterLayout,
                             juce::AudioProcessor::BusesProperties busesProperties)
    : juce::AudioProcessor(std::move(busesProperties)),
      productIdentity(std::move(identity)),
      parameters(*this, nullptr, "STATE", std::move(parameterLayout)),
      spectrumPublisher(productIdentity, productIdentity.showLevelTrace),
      stagePublisher(productIdentity) {}

ProcessorBase::~ProcessorBase() = default;

juce::AudioProcessorEditor* ProcessorBase::createEditor() {
    return new EditorBase(*this);
}

void ProcessorBase::prepareToPlay(const double sampleRate, const int samplesPerBlock) {
    currentSampleRateHz = sampleRate > 1000.0 ? sampleRate : 48000.0;
    tailLengthSeconds = 0.0;
    voiceAnalysis.prepare(sampleRate, samplesPerBlock);
    voiceContext.prepare(sampleRate, samplesPerBlock);
    signalQuality.prepare(sampleRate, samplesPerBlock);
    listenInputScratch.setSize(std::max(1, getTotalNumOutputChannels()), std::max(1, samplesPerBlock), false, false, true);
    prepareProcessCoordinator(samplesPerBlock);
    spectrumPublisher.prepare(sampleRate, samplesPerBlock);
    stagePublisher.prepare(sampleRate, samplesPerBlock);
    outputSafetyTrimmer.setCeiling(0.985f);
    outputSafetyTrimmer.setReleaseSeconds(0.12f);
    outputSafetyTrimmer.reset();
    prepareSuite(sampleRate, samplesPerBlock);
}

void ProcessorBase::reset() {
    voiceAnalysis.reset();
    voiceContext.reset();
    signalQuality.reset();
    listenInputScratch.clear();
    resetProcessCoordinator();
    spectrumPublisher.reset();
    stagePublisher.reset();
    outputSafetyTrimmer.reset();
    resetSuite();
}

void ProcessorBase::updateTrackProperties(const TrackProperties& props) {
    // Store track info. Each setter also triggers an early registration so the
    // analyser can find this stage before the audio engine starts (i.e. before
    // prepareToPlay is called), which is important when transport is stopped.
    if (props.channelUID.has_value())
        stagePublisher.setChannelUID(*props.channelUID);
    if (props.runtimeID.has_value())
        stagePublisher.setTrackRuntimeID(*props.runtimeID);
    if (props.name.has_value())
        stagePublisher.setTrackName(*props.name);
    if (props.colourARGB.has_value())
        stagePublisher.setTrackColour(*props.colourARGB);

    // Propagate the track scope key so domain binding can prefer the analyser on this track.
    const juce::String channelUID = props.channelUID.has_value() ? *props.channelUID : juce::String();
    const std::int64_t runtimeID  = props.runtimeID.has_value()  ? *props.runtimeID  : 0;
    const juce::String name       = props.name.has_value()       ? *props.name       : juce::String();
    const auto scopeKey = vxsuite::analysis::StageRegistry::buildTrackStableId(channelUID, runtimeID, name);
    if (scopeKey != 0)
        stagePublisher.setContextKeyHint(scopeKey);
}

void ProcessorBase::releaseResources() {
    voiceAnalysis.reset();
    voiceContext.reset();
    signalQuality.reset();
    listenInputScratch.setSize(0, 0);
    releaseProcessCoordinator();
    spectrumPublisher.reset();
    stagePublisher.reset();
    outputSafetyTrimmer.reset();
    resetSuite();
}

void ProcessorBase::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    // The host buffer carries every bus's channels. The whole downstream
    // pipeline (analysis, listen scratch, publishers, safety trim) must only
    // ever see the main bus, so split here and expose the sidechain - when
    // present - through getSidechainBuffer() for the duration of the block.
    const bool hasSidechainBus = productIdentity.wantsSidechainInput
        && getBusCount(true) > 1
        && getChannelCountOfBus(true, 1) > 0
        && buffer.getNumChannels() >= getMainBusNumInputChannels() + getChannelCountOfBus(true, 1);
    const int mainChannels = hasSidechainBus
        ? std::min(buffer.getNumChannels(), getMainBusNumOutputChannels())
        : buffer.getNumChannels();
    const int scChannels = hasSidechainBus ? getChannelCountOfBus(true, 1) : 0;
    auto* const* channelPointers = buffer.getArrayOfWritePointers();

    const int preparedBlockSize = listenInputScratch.getNumSamples();
    const int chunkSize = (preparedBlockSize <= 0 || buffer.getNumSamples() <= preparedBlockSize)
        ? std::max(1, buffer.getNumSamples())
        : preparedBlockSize;

    for (int start = 0; start < buffer.getNumSamples(); start += chunkSize) {
        const int num = std::min(chunkSize, buffer.getNumSamples() - start);
        if (hasSidechainBus) {
            sidechainView = juce::AudioBuffer<float>(channelPointers + mainChannels, scChannels, start, num);
            activeSidechain = &sidechainView;
        }
        juce::AudioBuffer<float> blockView(channelPointers, mainChannels, start, num);
        processPreparedBlock(blockView, midi);
        activeSidechain = nullptr;
    }
}

void ProcessorBase::processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ignoreUnused(midi);
    const int mainChannels = productIdentity.wantsSidechainInput
        ? std::min(buffer.getNumChannels(), getMainBusNumOutputChannels())
        : buffer.getNumChannels();
    const int preparedBlockSize = listenInputScratch.getNumSamples();
    const int chunkSize = (preparedBlockSize <= 0 || buffer.getNumSamples() <= preparedBlockSize)
        ? std::max(1, buffer.getNumSamples())
        : preparedBlockSize;
    for (int start = 0; start < buffer.getNumSamples(); start += chunkSize) {
        const int num = std::min(chunkSize, buffer.getNumSamples() - start);
        juce::AudioBuffer<float> blockView(buffer.getArrayOfWritePointers(), mainChannels, start, num);
        processPreparedBypassedBlock(blockView);
    }
}

void ProcessorBase::processPreparedBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    if (productIdentity.requiresVoiceAnalysis) {
        voiceAnalysis.update(buffer, buffer.getNumSamples());
        voiceContext.update(buffer, voiceAnalysis.snapshot());
    }
    if (productIdentity.requiresSignalQuality)
        signalQuality.update(buffer, buffer.getNumSamples());

    const bool hasDryScratch = listenInputScratch.getNumChannels() >= buffer.getNumChannels()
        && listenInputScratch.getNumSamples() >= buffer.getNumSamples();
    jassert(hasDryScratch);
    if (!hasDryScratch)
        return;

    const bool canRenderListen = isListenEnabled();
    const bool needsDryBuffer = canRenderListen || spectrumPublisher.isActive() || stagePublisher.isActive();
    if (needsDryBuffer) {
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            listenInputScratch.copyFrom(channel, 0, buffer, channel, 0, buffer.getNumSamples());
    }

    processCoordinator.beginBlock(needsDryBuffer ? listenInputScratch : buffer, canRenderListen);
    processProduct(buffer, midi);
    outputSafetyTrimmer.process(buffer, currentSampleRateHz);
    spectrumPublisher.publish(listenInputScratch, buffer);
    stagePublisher.publish(listenInputScratch, buffer, false);
    if (canRenderListen) {
        ensureLatencyAlignedListenDry(buffer.getNumSamples());
        renderListenOutput(buffer, getLatencyAlignedListenDryBuffer());
    }
}

void ProcessorBase::processPreparedBypassedBlock(juce::AudioBuffer<float>& buffer) noexcept {
    const bool hasDryScratch = listenInputScratch.getNumChannels() >= buffer.getNumChannels()
        && listenInputScratch.getNumSamples() >= buffer.getNumSamples();
    if (hasDryScratch) {
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            listenInputScratch.copyFrom(channel, 0, buffer, channel, 0, buffer.getNumSamples());
        processCoordinator.beginBlock(listenInputScratch, false);
        processCoordinator.ensureAlignedDry(buffer.getNumSamples());
        stagePublisher.publishBypassed(buffer);
    } else {
        stagePublisher.publishBypassed(buffer);
    }
    spectrumPublisher.publishSilence();
}

bool ProcessorBase::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto input = layouts.getMainInputChannelSet();
    const auto output = layouts.getMainOutputChannelSet();
    if (input != output || (input != juce::AudioChannelSet::mono() && input != juce::AudioChannelSet::stereo()))
        return false;
    if (layouts.inputBuses.size() > 1) {
        if (!productIdentity.wantsSidechainInput)
            return false;
        const auto sidechain = layouts.getChannelSet(true, 1);
        if (!sidechain.isDisabled()
            && sidechain != juce::AudioChannelSet::mono()
            && sidechain != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}

void ProcessorBase::getStateInformation(juce::MemoryBlock& destData) {
    if (const auto xml = parameters.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void ProcessorBase::setStateInformation(const void* data, const int sizeInBytes) {
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState != nullptr && xmlState->hasTagName(parameters.state.getType()))
        parameters.replaceState(juce::ValueTree::fromXml(*xmlState));
}

const ModePolicy& ProcessorBase::currentModePolicy() const noexcept {
    return readModePolicy(parameters, productIdentity);
}

bool ProcessorBase::isListenEnabled() const noexcept {
    return productIdentity.supportsListenMode()
        && readBool(parameters, productIdentity.listenParamId, false);
}

void ProcessorBase::prepareProcessCoordinator(const int maxBlockSize) {
    processCoordinator.prepare(getTotalNumOutputChannels(), std::max(1, maxBlockSize), getLatencySamples());
}

void ProcessorBase::resetProcessCoordinator() {
    processCoordinator.reset();
}

void ProcessorBase::releaseProcessCoordinator() {
    processCoordinator.release();
}

void ProcessorBase::setReportedLatencySamples(const int latencySamples) {
    processCoordinator.setLatencySamples(latencySamples);
    juce::AudioProcessor::setLatencySamples(processCoordinator.latencySamples());
}

void ProcessorBase::setReportedTailLengthSeconds(const double seconds) noexcept {
    tailLengthSeconds = std::max(0.0, seconds);
}

void ProcessorBase::ensureLatencyAlignedListenDry(const int numSamples) {
    processCoordinator.ensureAlignedDry(numSamples);
}

const juce::AudioBuffer<float>& ProcessorBase::getLatencyAlignedListenDryBuffer() const noexcept {
    return processCoordinator.alignedDryBuffer();
}

void ProcessorBase::resetOutputSafetyTrimmer() noexcept {
    outputSafetyTrimmer.reset();
}

void ProcessorBase::renderListenOutput(juce::AudioBuffer<float>& outputBuffer,
                                       const juce::AudioBuffer<float>& inputBuffer) {
    // Listen mode: output pure delta (processed - input)
    // without latency-based delay line complexity
    const int channels = std::min(outputBuffer.getNumChannels(), inputBuffer.getNumChannels());
    const int samples = std::min(outputBuffer.getNumSamples(), inputBuffer.getNumSamples());
    for (int ch = 0; ch < channels; ++ch) {
        auto* out = outputBuffer.getWritePointer(ch);
        const auto* in = inputBuffer.getReadPointer(ch);
        for (int i = 0; i < samples; ++i)
            out[i] = out[i] - in[i];  // delta = wet - dry
    }
}

void ProcessorBase::renderAddedDeltaOutput(juce::AudioBuffer<float>& outputBuffer,
                                           const juce::AudioBuffer<float>& inputBuffer) const noexcept {
    const int channels = std::min(outputBuffer.getNumChannels(), inputBuffer.getNumChannels());
    const int samples = std::min(outputBuffer.getNumSamples(), inputBuffer.getNumSamples());
    for (int ch = 0; ch < channels; ++ch) {
        auto* out = outputBuffer.getWritePointer(ch);
        const auto* in = inputBuffer.getReadPointer(ch);
        for (int i = 0; i < samples; ++i)
            out[i] = out[i] - in[i];
    }
}

bool ProcessorBase::getSpectrumSnapshotView(spectrum::SnapshotView& out) const noexcept {
    if (!spectrumPublisher.isActive())
        return false;

    for (int slotIndex = 0; slotIndex < spectrum::SnapshotRegistry::instance().maxSlots(); ++slotIndex) {
        spectrum::SnapshotView snapshot;
        if (!spectrum::SnapshotRegistry::instance().readSlot(slotIndex, snapshot))
            continue;
        if (snapshot.instanceId != spectrumPublisher.instanceId())
            continue;
        out = snapshot;
        return true;
    }
    return false;
}

} // namespace vxsuite
