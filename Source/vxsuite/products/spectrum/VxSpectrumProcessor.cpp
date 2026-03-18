#include "VxSpectrumProcessor.h"

#include "VxSpectrumEditor.h"

namespace {

constexpr std::string_view kProductName = "Spectrum";
constexpr std::string_view kShortTag = "SPC";

} // namespace

VXSpectrumAudioProcessor::VXSpectrumAudioProcessor()
    : juce::AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                            .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      identity(makeIdentity()),
      telemetryPublisher(identity, false) {}

vxsuite::ProductIdentity VXSpectrumAudioProcessor::makeIdentity() {
    vxsuite::ProductIdentity identity {};
    identity.productName = kProductName;
    identity.shortTag = kShortTag;
    identity.theme.accentRgb = { 0.32f, 0.90f, 0.95f };
    identity.theme.accent2Rgb = { 0.07f, 0.15f, 0.18f };
    identity.theme.backgroundRgb = { 0.03f, 0.05f, 0.08f };
    identity.theme.panelRgb = { 0.06f, 0.09f, 0.13f };
    identity.theme.textRgb = { 0.88f, 0.95f, 0.98f };
    return identity;
}

void VXSpectrumAudioProcessor::prepareToPlay(const double sampleRate, const int samplesPerBlock) {
    dryScratch.setSize(std::max(1, getTotalNumInputChannels()),
                       std::max(1, samplesPerBlock),
                       false,
                       false,
                       true);
    telemetryPublisher.prepare(sampleRate, samplesPerBlock);
}

void VXSpectrumAudioProcessor::releaseResources() {
    dryScratch.setSize(0, 0);
    telemetryPublisher.reset();
}

void VXSpectrumAudioProcessor::reset() {
    dryScratch.clear();
    telemetryPublisher.reset();
}

void VXSpectrumAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    if (dryScratch.getNumChannels() >= buffer.getNumChannels()
        && dryScratch.getNumSamples() >= buffer.getNumSamples()) {
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            dryScratch.copyFrom(channel, 0, buffer, channel, 0, buffer.getNumSamples());
        telemetryPublisher.publish(dryScratch, buffer);
    }
}

void VXSpectrumAudioProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ignoreUnused(buffer, midi);
    telemetryPublisher.publishSilence();
}

bool VXSpectrumAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto input = layouts.getMainInputChannelSet();
    const auto output = layouts.getMainOutputChannelSet();
    return input == output && (input == juce::AudioChannelSet::mono() || input == juce::AudioChannelSet::stereo());
}

juce::AudioProcessorEditor* VXSpectrumAudioProcessor::createEditor() {
    return new VXSpectrumEditor(*this);
}

#if !defined(VXSUITE_DISABLE_PLUGIN_ENTRYPOINT)
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new VXSpectrumAudioProcessor();
}
#endif
