#include "VxSuiteProcessorBase.h"
#include "VxSuiteEditorBase.h"

namespace vxsuite {

ProcessorBase::ProcessorBase(ProductIdentity identity)
    : ProcessorBase(identity, createSimpleParameterLayout(identity)) {}

ProcessorBase::ProcessorBase(ProductIdentity identity,
                             juce::AudioProcessorValueTreeState::ParameterLayout parameterLayout)
    : juce::AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                            .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      productIdentity(std::move(identity)),
      parameters(*this, nullptr, "STATE", std::move(parameterLayout)),
      spectrumPublisher(productIdentity, true),
      stagePublisher(productIdentity) {}

ProcessorBase::~ProcessorBase() = default;

juce::AudioProcessorEditor* ProcessorBase::createEditor() {
    return new EditorBase(*this);
}

void ProcessorBase::prepareToPlay(const double sampleRate, const int samplesPerBlock) {
    currentSampleRateHz = sampleRate > 1000.0 ? sampleRate : 48000.0;
    tailLengthSeconds = 0.0;
    voiceAnalysis.prepare(sampleRate, samplesPerBlock);
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
    listenInputScratch.clear();
    resetProcessCoordinator();
    spectrumPublisher.reset();
    stagePublisher.reset();
    outputSafetyTrimmer.reset();
    resetSuite();
}

void ProcessorBase::releaseResources() {
    voiceAnalysis.reset();
    listenInputScratch.setSize(0, 0);
    releaseProcessCoordinator();
    spectrumPublisher.reset();
    stagePublisher.reset();
    outputSafetyTrimmer.reset();
    resetSuite();
}

void ProcessorBase::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    const int preparedBlockSize = listenInputScratch.getNumSamples();
    if (preparedBlockSize <= 0 || buffer.getNumSamples() <= preparedBlockSize) {
        processPreparedBlock(buffer, midi);
        return;
    }

    const int chunkSize = std::max(1, preparedBlockSize);
    for (int start = 0; start < buffer.getNumSamples(); start += chunkSize) {
        const int num = std::min(chunkSize, buffer.getNumSamples() - start);
        juce::AudioBuffer<float> blockView(buffer.getArrayOfWritePointers(), buffer.getNumChannels(), start, num);
        processPreparedBlock(blockView, midi);
    }
}

void ProcessorBase::processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ignoreUnused(midi);
    const int preparedBlockSize = listenInputScratch.getNumSamples();
    if (preparedBlockSize <= 0 || buffer.getNumSamples() <= preparedBlockSize) {
        processPreparedBypassedBlock(buffer);
        return;
    }

    const int chunkSize = std::max(1, preparedBlockSize);
    for (int start = 0; start < buffer.getNumSamples(); start += chunkSize) {
        const int num = std::min(chunkSize, buffer.getNumSamples() - start);
        juce::AudioBuffer<float> blockView(buffer.getArrayOfWritePointers(), buffer.getNumChannels(), start, num);
        processPreparedBypassedBlock(blockView);
    }
}

void ProcessorBase::processPreparedBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    voiceAnalysis.update(buffer, buffer.getNumSamples());

    const bool hasDryScratch = listenInputScratch.getNumChannels() >= buffer.getNumChannels()
        && listenInputScratch.getNumSamples() >= buffer.getNumSamples();
    jassert(hasDryScratch);
    if (!hasDryScratch)
        return;

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        listenInputScratch.copyFrom(channel, 0, buffer, channel, 0, buffer.getNumSamples());

    const bool canRenderListen = isListenEnabled();
    processCoordinator.beginBlock(listenInputScratch, canRenderListen);
    processProduct(buffer, midi);
    outputSafetyTrimmer.process(buffer, currentSampleRateHz);
    spectrumPublisher.publish(listenInputScratch, buffer);
    stagePublisher.publish(listenInputScratch, buffer, false);
    if (canRenderListen)
        renderListenOutput(buffer, listenInputScratch);
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
    return input == output && (input == juce::AudioChannelSet::mono() || input == juce::AudioChannelSet::stereo());
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

void ProcessorBase::renderListenOutput(juce::AudioBuffer<float>& outputBuffer,
                                       const juce::AudioBuffer<float>& inputBuffer) {
    juce::ignoreUnused(inputBuffer);
    processCoordinator.renderRemovedDelta(outputBuffer);
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

} // namespace vxsuite
