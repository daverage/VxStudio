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

float clamp01(const float v) { return juce::jlimit(0.f, 1.f, v); }

} // namespace

VXDenoiserAudioProcessor::VXDenoiserAudioProcessor()
    : ProcessorBase(makeIdentity(), makeLayout(makeIdentity())) {}

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
    return id;
}

juce::AudioProcessorValueTreeState::ParameterLayout
VXDenoiserAudioProcessor::makeLayout(const vxsuite::ProductIdentity& id) {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { kCleanParam.data(), 1 },
        vxsuite::toJuceString(id.primaryLabel),
        juce::NormalisableRange<float> { 0.f, 1.f, 0.001f },
        0.5f,
        juce::AudioParameterFloatAttributes().withLabel(id.primaryLabel.data())));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { kGuardParam.data(), 1 },
        vxsuite::toJuceString(id.secondaryLabel),
        juce::NormalisableRange<float> { 0.f, 1.f, 0.001f },
        0.5f,
        juce::AudioParameterFloatAttributes().withLabel(id.secondaryLabel.data())));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { kModeParam.data(), 1 },
        "Mode",
        vxsuite::makeModeChoiceLabels(),
        static_cast<int>(vxsuite::Mode::vocal),
        vxsuite::makeModeAttributes()));

    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { kListenParam.data(), 1 },
        "Listen",
        false,
        vxsuite::makeListenAttributes()));

    return layout;
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
    denoiserDsp.prepare(currentSampleRateHz, samplesPerBlock);
    setLatencySamples(denoiserDsp.getLatencySamples());
    ensureScratchCapacity(getTotalNumOutputChannels(), samplesPerBlock);
    resetSuite();
}

void VXDenoiserAudioProcessor::resetSuite() {
    denoiserDsp.reset();
    dryScratch.clear();
    alignedDryScratch.clear();
    for (auto& line : dryDelayLines)
        std::fill(line.begin(), line.end(), 0.0f);
    std::fill(dryDelayWritePos.begin(), dryDelayWritePos.end(), 0);
    smoothedClean  = 0.0f;
    smoothedGuard  = 0.5f;
    controlsPrimed = false;
}

void VXDenoiserAudioProcessor::ensureScratchCapacity(const int channels, const int samples) {
    const int ch  = std::max(1, channels);
    const int smp = std::max(8192, samples);
    dryScratch      .setSize(ch, smp, false, false, true);
    alignedDryScratch.setSize(ch, smp, false, false, true);
    const int latency     = std::max(0, denoiserDsp.getLatencySamples());
    const int delayCapacity = std::max(1, latency + smp + 1);
    dryDelayLines   .assign(static_cast<size_t>(ch),
                            std::vector<float>(static_cast<size_t>(delayCapacity), 0.0f));
    dryDelayWritePos.assign(static_cast<size_t>(ch), 0);
}

void VXDenoiserAudioProcessor::fillAlignedDryScratch(const juce::AudioBuffer<float>& dry,
                                                      const int numSamples) {
    const int latency = std::max(0, denoiserDsp.getLatencySamples());
    for (int ch = 0; ch < dry.getNumChannels(); ++ch) {
        const auto* src = dry.getReadPointer(ch);
        auto*       dst = alignedDryScratch.getWritePointer(ch);
        auto&       line = dryDelayLines[static_cast<size_t>(ch)];
        const int   sz   = static_cast<int>(line.size());
        int         wp   = dryDelayWritePos[static_cast<size_t>(ch)];
        for (int i = 0; i < numSamples; ++i) {
            line[static_cast<size_t>(wp)] = src[i];
            const int rp = (wp + sz - latency) % sz;
            dst[i] = line[static_cast<size_t>(rp)];
            wp = (wp + 1) % sz;
        }
        dryDelayWritePos[static_cast<size_t>(ch)] = wp;
    }
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
        const float sr  = static_cast<float>(currentSampleRateHz);
        const float blC = 1.f - std::exp(-static_cast<float>(numSamples) / (0.060f * sr));
        const float blG = 1.f - std::exp(-static_cast<float>(numSamples) / (0.080f * sr));
        smoothedClean += blC * (cleanTarget - smoothedClean);
        smoothedGuard += blG * (guardTarget - smoothedGuard);
    }

    // Capture dry before processing — needed for latency-aligned listen output
    const int numCh = buffer.getNumChannels();
    if (dryScratch.getNumChannels() >= numCh && dryScratch.getNumSamples() >= numSamples)
        for (int ch = 0; ch < numCh; ++ch)
            dryScratch.copyFrom(ch, 0, buffer, ch, 0, numSamples);

    const bool isVoice  = vxsuite::readMode(parameters, productIdentity)
                       == vxsuite::Mode::vocal;
    const auto& policy  = currentModePolicy();

    // Map user controls + ModePolicy onto ProcessOptions
    vxsuite::ProcessOptions opts;
    opts.isVoiceMode        = isVoice;
    opts.sourceProtect      = clamp01(smoothedGuard * policy.sourceProtect);
    opts.lateTailAggression = policy.lateTailAggression;
    opts.guardStrictness    = clamp01(smoothedGuard * policy.guardStrictness);
    opts.speechFocus        = policy.speechFocus;

    denoiserDsp.processInPlace(buffer, clamp01(smoothedClean), opts);

    // Build latency-aligned dry scratch for listen output
    if (alignedDryScratch.getNumChannels() >= numCh
        && alignedDryScratch.getNumSamples() >= numSamples)
        fillAlignedDryScratch(dryScratch, numSamples);
}

void VXDenoiserAudioProcessor::renderListenOutput(juce::AudioBuffer<float>& outputBuffer,
                                                   const juce::AudioBuffer<float>& /*inputBuffer*/) {
    // Output aligned dry minus wet — auditions what was removed.
    // Uses alignedDryScratch (latency-compensated) rather than the base-class
    // undelayed inputBuffer, which would be misaligned by ~32 ms.
    const int channels = std::min(outputBuffer.getNumChannels(),
                                  alignedDryScratch.getNumChannels());
    const int samples  = std::min(outputBuffer.getNumSamples(),
                                  alignedDryScratch.getNumSamples());
    for (int ch = 0; ch < channels; ++ch) {
        auto*       out = outputBuffer.getWritePointer(ch);
        const auto* dry = alignedDryScratch.getReadPointer(ch);
        for (int i = 0; i < samples; ++i)
            out[i] = dry[i] - out[i];
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new VXDenoiserAudioProcessor();
}
