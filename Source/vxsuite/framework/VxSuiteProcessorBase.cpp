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
    listenInputScratch.setSize(std::max(1, getTotalNumOutputChannels()), std::max(8192, samplesPerBlock), false, false, true);
    prepareSuite(sampleRate, samplesPerBlock);
}

void ProcessorBase::releaseResources() {
    voiceAnalysis.reset();
    listenInputScratch.setSize(0, 0);
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

void ProcessorBase::renderListenOutput(juce::AudioBuffer<float>& outputBuffer,
                                       const juce::AudioBuffer<float>& inputBuffer) {
    const int channels = std::min(outputBuffer.getNumChannels(), inputBuffer.getNumChannels());
    const int samples = std::min(outputBuffer.getNumSamples(), inputBuffer.getNumSamples());
    for (int ch = 0; ch < channels; ++ch) {
        auto* out = outputBuffer.getWritePointer(ch);
        const auto* in = inputBuffer.getReadPointer(ch);
        for (int i = 0; i < samples; ++i)
            out[i] = in[i] - out[i];
    }
}

} // namespace vxsuite
