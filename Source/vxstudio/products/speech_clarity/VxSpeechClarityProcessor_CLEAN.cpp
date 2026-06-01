#include "VxSpeechClarityProcessor_CLEAN.h"
#include "../../framework/VxStudioHelpContent.h"
#include "VxStudioVersions.h"

namespace {
constexpr std::string_view kProductName = "VX Studio Speech Clarity";
constexpr std::string_view kShortTag = "CLR";
constexpr std::string_view kSibilanceParam = "sibilance";
constexpr std::string_view kPlosiveParam = "plosive";
constexpr std::string_view kBreathParam = "breath";
constexpr std::string_view kModeParam = "mode";
constexpr std::string_view kListenParam = "listen";
constexpr std::string_view kHpfOnParam = "hpf_on";
constexpr std::string_view kHiShelfOnParam = "hishelf_on";
} // namespace

VXSpeechClarityAudioProcessor::VXSpeechClarityAudioProcessor()
    : ProcessorBase(makeIdentity()) {}

vxsuite::ProductIdentity VXSpeechClarityAudioProcessor::makeIdentity() {
    vxsuite::ProductIdentity identity {};
    identity.productName = kProductName;
    identity.shortTag = kShortTag;
    identity.primaryParamId = kSibilanceParam;
    identity.secondaryParamId = kPlosiveParam;
    identity.tertiaryParamId = kBreathParam;
    identity.modeParamId = kModeParam;
    identity.listenParamId = kListenParam;
    identity.defaultMode = vxsuite::Mode::vocal;
    identity.primaryLabel = "Sibilance";
    identity.secondaryLabel = "Plosive";
    identity.tertiaryLabel = "Breath";
    identity.primaryDefaultValue = 0.0f;
    identity.secondaryDefaultValue = 0.0f;
    identity.tertiaryDefaultValue = 0.0f;
    identity.primaryHint = "Reduce harsh sibilance (/s/ and /z/ sounds).";
    identity.secondaryHint = "Reduce plosive bursts (/p/, /b/, /t/, /d/, /k/, /g/).";
    identity.tertiaryHint = "Reduce breathing and wind noise.";
    identity.dspVersion = vxsuite::versions::plugins::speech_clarity;
    identity.helpTitle = vxsuite::help::speech_clarity.title;
    identity.helpHtml = vxsuite::help::speech_clarity.html;
    identity.readmeSection = vxsuite::help::speech_clarity.readmeSection;
    // Shelf controls (matching original Cleanup)
    identity.showLowShelfIcon = true;
    identity.showHighShelfIcon = true;
    identity.lowShelfParamId = kHpfOnParam;
    identity.highShelfParamId = kHiShelfOnParam;
    identity.defaultLowShelf = false;
    identity.defaultHighShelf = false;
    identity.theme.accentRgb = { 0.42f, 0.78f, 0.94f };
    identity.theme.accent2Rgb = { 0.10f, 0.16f, 0.24f };
    identity.theme.backgroundRgb = { 0.04f, 0.06f, 0.08f };
    identity.theme.panelRgb = { 0.08f, 0.12f, 0.16f };
    identity.theme.textRgb = { 0.88f, 0.94f, 0.99f };
    return identity;
}

juce::String VXSpeechClarityAudioProcessor::getStatusText() const {
    if (isListenEnabled())
        return "Listen - speech clarity delta";
    return "Sibilance, plosive, and breath artifact removal";
}

float VXSpeechClarityAudioProcessor::getActivityLight(int index) const noexcept {
    switch (index) {
        case 0: return sibilanceDetectionIntensity;
        case 1: return plosiveDetectionIntensity;
        case 2: return breathDetectionIntensity;
        default: return 0.0f;
    }
}

std::string_view VXSpeechClarityAudioProcessor::getActivityLightLabel(int index) const noexcept {
    switch (index) {
        case 0: return "Sibl";
        case 1: return "Plos";
        case 2: return "Brth";
        default: return {};
    }
}

void VXSpeechClarityAudioProcessor::prepareSuite(const double sampleRate, const int samplesPerBlock) {
    currentSampleRateHz = sampleRate > 1000.0 ? sampleRate : 48000.0;

    // Initialize all DSP components
    deEsserDsp.prepare(currentSampleRateHz, samplesPerBlock, getTotalNumOutputChannels());
    dePolosiveDsp.prepare(currentSampleRateHz, samplesPerBlock, getTotalNumOutputChannels());
    deBreathDsp.prepare(currentSampleRateHz, samplesPerBlock, getTotalNumOutputChannels());

    resetSuite();
}

void VXSpeechClarityAudioProcessor::resetSuite() {
    detectorState.needsPreAnalysis = true;
    detectorState.sibilanceEnvelope = 0.0f;
    detectorState.plosiveEnvelope = 0.0f;
    detectorState.breathEnvelope = 0.0f;
    sibilanceDetectionIntensity = 0.0f;
    plosiveDetectionIntensity = 0.0f;
    breathDetectionIntensity = 0.0f;

    // Reset DSP components
    deEsserDsp.reset();
    dePolosiveDsp.reset();
    deBreathDsp.reset();
}

void VXSpeechClarityAudioProcessor::performPreAnalysis(const juce::AudioBuffer<float>& buffer) {
    // Pre-analysis pass: scan buffer to establish adaptive detection thresholds
    // This ensures consistent detection throughout the track (not learning as it goes)

    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    float maxSibilanceEnergy = 0.0f;
    float maxPlosiveEnergy = 0.0f;
    float maxBreathEnergy = 0.0f;

    // Simple envelope-based measurement for each artifact band
    // Sibilance: high-frequency energy (4.5-8 kHz region approximation)
    // Plosive: low-frequency burst energy (50-300 Hz region approximation)
    // Breath: very low-frequency rumble (sub-500 Hz)

    for (int ch = 0; ch < numChannels; ++ch) {
        const float* data = buffer.getReadPointer(ch);

        // Simple one-pole filtering to approximate band-pass characteristics
        float sibilanceAccum = 0.0f;
        float plosiveAccum = 0.0f;
        float breathAccum = 0.0f;

        for (int i = 0; i < numSamples; ++i) {
            const float sample = data[i];

            // Very rough approximation: use spectral slope as proxy for bands
            // In production, would use proper band-pass filters
            sibilanceAccum = 0.95f * sibilanceAccum + 0.05f * std::abs(sample);
            plosiveAccum = 0.98f * plosiveAccum + 0.02f * std::abs(sample);
            breathAccum = 0.99f * breathAccum + 0.01f * std::abs(sample);

            maxSibilanceEnergy = std::max(maxSibilanceEnergy, sibilanceAccum);
            maxPlosiveEnergy = std::max(maxPlosiveEnergy, plosiveAccum);
            maxBreathEnergy = std::max(maxBreathEnergy, breathAccum);
        }
    }

    // Set adaptive thresholds at 80% of maximum detected energy
    // This means detection "lights up" when artifact intensity exceeds normal baseline
    sibilanceThreshold = maxSibilanceEnergy * 0.80f;
    plosiveThreshold = maxPlosiveEnergy * 0.80f;
    breathThreshold = maxBreathEnergy * 0.80f;

    detectorState.needsPreAnalysis = false;
}

void VXSpeechClarityAudioProcessor::detectAndUpdateIntensities(const juce::AudioBuffer<float>& buffer) {
    // Per-block detection: measure artifact presence and update LED intensity feedback
    // Intensity is normalized to threshold: 0 = not present, 1 = very strong

    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    float blockSibilanceMax = 0.0f;
    float blockPlosiveMax = 0.0f;
    float blockBreathMax = 0.0f;

    for (int ch = 0; ch < numChannels; ++ch) {
        const float* data = buffer.getReadPointer(ch);

        float sibEnv = 0.0f;
        float plosEnv = 0.0f;
        float breathEnv = 0.0f;

        for (int i = 0; i < numSamples; ++i) {
            const float sample = std::abs(data[i]);

            // Fast attack, slow release envelope following
            sibEnv = (sample > sibEnv) ? sample : 0.95f * sibEnv + 0.05f * sample;
            plosEnv = (sample > plosEnv) ? sample : 0.98f * plosEnv + 0.02f * sample;
            breathEnv = 0.99f * breathEnv + 0.01f * sample;

            blockSibilanceMax = std::max(blockSibilanceMax, sibEnv);
            blockPlosiveMax = std::max(blockPlosiveMax, plosEnv);
            blockBreathMax = std::max(blockBreathMax, breathEnv);
        }
    }

    // Convert to LED intensity (0-1 range, normalized to threshold)
    const float sibIntensity = blockSibilanceMax > sibilanceThreshold
        ? std::min(1.0f, (blockSibilanceMax - sibilanceThreshold) / (sibilanceThreshold * 0.5f))
        : 0.0f;

    const float plosIntensity = blockPlosiveMax > plosiveThreshold
        ? std::min(1.0f, (blockPlosiveMax - plosiveThreshold) / (plosiveThreshold * 0.5f))
        : 0.0f;

    const float breathIntensity = blockBreathMax > breathThreshold
        ? std::min(1.0f, (blockBreathMax - breathThreshold) / (breathThreshold * 0.5f))
        : 0.0f;

    // Smooth LED feedback (light persistence - slightly lag changes)
    sibilanceDetectionIntensity = 0.9f * sibilanceDetectionIntensity + 0.1f * sibIntensity;
    plosiveDetectionIntensity = 0.9f * plosiveDetectionIntensity + 0.1f * plosIntensity;
    breathDetectionIntensity = 0.9f * breathDetectionIntensity + 0.1f * breathIntensity;
}

void VXSpeechClarityAudioProcessor::processProduct(juce::AudioBuffer<float>& buffer,
                                                   juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0)
        return;

    // 1. PRE-ANALYSIS (establishes adaptive thresholds once per session/reset)
    if (detectorState.needsPreAnalysis) {
        performPreAnalysis(buffer);
    }

    // 2. READ CONTROL DIALS
    const float sibilanceStrength = vxsuite::readNormalized(
        parameters, productIdentity.primaryParamId, 0.0f);
    const float plosiveStrength = vxsuite::readNormalized(
        parameters, productIdentity.secondaryParamId, 0.0f);
    const float breathStrength = vxsuite::readNormalized(
        parameters, productIdentity.tertiaryParamId, 0.0f);

    // 3. DETECT ARTIFACTS AND UPDATE LED FEEDBACK
    detectAndUpdateIntensities(buffer);

    // 4. APPLY PROCESSING (each DSP runs independently if its dial > 0)
    if (sibilanceStrength > 0.001f) {
        vxsuite::speech_clarity::DeEsserDsp::Params sibilanceParams {
            sibilanceStrength,
            sibilanceDetectionIntensity
        };
        deEsserDsp.process(buffer, sibilanceParams);
    }

    if (plosiveStrength > 0.001f) {
        vxsuite::speech_clarity::DePolosiveDsp::Params plosiveParams {
            plosiveStrength,
            plosiveDetectionIntensity
        };
        dePolosiveDsp.process(buffer, plosiveParams);
    }

    if (breathStrength > 0.001f) {
        vxsuite::speech_clarity::DeBreathDsp::Params breathParams {
            breathStrength,
            breathDetectionIntensity
        };
        deBreathDsp.process(buffer, breathParams);
    }
}

void VXSpeechClarityAudioProcessor::renderListenOutput(juce::AudioBuffer<float>& outputBuffer,
                                                       const juce::AudioBuffer<float>& inputBuffer) {
    renderAddedDeltaOutput(outputBuffer, inputBuffer);
}

#if !defined(VXSUITE_DISABLE_PLUGIN_ENTRYPOINT) && !defined(VXSTUDIO_DISABLE_PLUGIN_ENTRYPOINT)
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new VXSpeechClarityAudioProcessor();
}
#endif
