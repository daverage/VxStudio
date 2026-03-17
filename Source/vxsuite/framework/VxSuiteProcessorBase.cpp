#include "VxSuiteProcessorBase.h"

namespace vxsuite {

ProcessorBase::ProcessorBase(ProductIdentity identity,
                             juce::AudioProcessorValueTreeState::ParameterLayout parameterLayout)
    : juce::AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                            .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      productIdentity(std::move(identity)),
      parameters(*this, nullptr, "STATE", std::move(parameterLayout)) {}

void ProcessorBase::prepareToPlay(const double sampleRate, const int samplesPerBlock) {
    voiceAnalysis.prepare(sampleRate, samplesPerBlock);
    listenInputScratch.setSize(std::max(1, getTotalNumOutputChannels()), std::max(1, samplesPerBlock), false, false, true);
    prepareProcessCoordinator(samplesPerBlock);
    prepareSuite(sampleRate, samplesPerBlock);
}

void ProcessorBase::reset() {
    voiceAnalysis.reset();
    listenInputScratch.clear();
    resetProcessCoordinator();
    resetSuite();
}

void ProcessorBase::releaseResources() {
    voiceAnalysis.reset();
    listenInputScratch.setSize(0, 0);
    releaseProcessCoordinator();
    resetSuite();
}

void ProcessorBase::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    voiceAnalysis.update(buffer, buffer.getNumSamples());
    const bool canRenderListen = isListenEnabled()
        && listenInputScratch.getNumChannels() >= buffer.getNumChannels()
        && listenInputScratch.getNumSamples() >= buffer.getNumSamples();
    if (canRenderListen) {
        listenInputScratch.makeCopyOf(buffer, true);
    }
    processCoordinator.beginBlock(buffer, canRenderListen);
    processProduct(buffer, midi);
    if (canRenderListen)
        renderListenOutput(buffer, listenInputScratch);
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

} // namespace vxsuite
