#include "VxSpeechClarityProcessor.h"
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
    identity.theme.accentRgb = { 0.42f, 0.78f, 0.94f };      // Blue
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

int VXSpeechClarityAudioProcessor::getActivityLightCount() const noexcept {
    return 3;
}

float VXSpeechClarityAudioProcessor::getActivityLight(int index) const noexcept {
    switch (index) {
        case 0: return detectionState.sibilanceIntensity;
        case 1: return detectionState.plosiveIntensity;
        case 2: return detectionState.breathIntensity;
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

    // Configure detection filters
    sibilanceEnvFollower.setSampleRate(currentSampleRateHz);
    sibilanceEnvFollower.setTimings(5.0f, 50.0f);
    sibilanceBandFilter.setCoefficients(vxsuite::detectors::BiquadFilter::Type::BandPass,
                                       5500.0f, 2.0f, currentSampleRateHz);

    plosiveEnvFollower.setSampleRate(currentSampleRateHz);
    plosiveEnvFollower.setTimings(2.0f, 100.0f);
    plosiveBandFilter.setCoefficients(vxsuite::detectors::BiquadFilter::Type::LowPass,
                                     300.0f, 1.0f, currentSampleRateHz);

    breathEnvFollower.setSampleRate(currentSampleRateHz);
    breathEnvFollower.setTimings(10.0f, 200.0f);
    breathBandFilter.setCoefficients(vxsuite::detectors::BiquadFilter::Type::LowPass,
                                    500.0f, 0.7f, currentSampleRateHz);

    onsetDetector.setSampleRate(currentSampleRateHz);

    resetSuite();
}

void VXSpeechClarityAudioProcessor::resetSuite() {
    sibilanceEnvFollower.reset();
    plosiveEnvFollower.reset();
    breathEnvFollower.reset();
    sibilanceBandFilter.reset();
    plosiveBandFilter.reset();
    breathBandFilter.reset();
    detectionState = {};
    needsPreAnalysis = true;
}

void VXSpeechClarityAudioProcessor::performPreAnalysis(const juce::AudioBuffer<float>& buffer) {
    // Scan buffer to establish adaptive thresholds
    float maxSibilanceEnergy = 0.0f;
    float maxPlosiveEnergy = 0.0f;
    float maxBreathEnergy = 0.0f;

    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    for (int ch = 0; ch < numChannels; ++ch) {
        const float* data = buffer.getReadPointer(ch);

        for (int i = 0; i < numSamples; ++i) {
            const float sample = data[i];

            // Measure sibilance band energy
            float sibFiltered = sibilanceBandFilter.process(sample);
            float sibEnv = std::abs(sibFiltered);
            maxSibilanceEnergy = std::max(maxSibilanceEnergy, sibEnv);

            // Measure plosive band energy
            float plosFiltered = plosiveBandFilter.process(sample);
            float plosEnv = std::abs(plosFiltered);
            maxPlosiveEnergy = std::max(maxPlosiveEnergy, plosEnv);

            // Measure breath band energy
            float breathFiltered = breathBandFilter.process(sample);
            float breathEnv = std::abs(breathFiltered);
            maxBreathEnergy = std::max(maxBreathEnergy, breathEnv);
        }
    }

    // Set adaptive thresholds at ~80% of max detected energy
    preAnalysisMetrics.sibilanceThreshold = maxSibilanceEnergy * 0.80f;
    preAnalysisMetrics.plosiveThreshold = maxPlosiveEnergy * 0.80f;
    preAnalysisMetrics.breathThreshold = maxBreathEnergy * 0.80f;
    preAnalysisMetrics.isValid = true;

    needsPreAnalysis = false;
}

float VXSpeechClarityAudioProcessor::detectSibilance(const juce::AudioBuffer<float>& buffer) {
    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    float maxEnvelope = 0.0f;

    for (int ch = 0; ch < numChannels; ++ch) {
        const float* data = buffer.getReadPointer(ch);

        for (int i = 0; i < numSamples; ++i) {
            float filtered = sibilanceBandFilter.process(data[i]);
            float env = sibilanceEnvFollower.process(std::abs(filtered));
            maxEnvelope = std::max(maxEnvelope, env);
        }
    }

    // Detection strength normalized to threshold
    const float threshold = preAnalysisMetrics.sibilanceThreshold;
    if (maxEnvelope <= threshold) return 0.0f;

    return vxsuite::detectors::clamp01((maxEnvelope - threshold) / (threshold * 0.5f));
}

float VXSpeechClarityAudioProcessor::detectPlosive(const juce::AudioBuffer<float>& buffer) {
    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    float maxBurstEnergy = 0.0f;

    for (int ch = 0; ch < numChannels; ++ch) {
        const float* data = buffer.getReadPointer(ch);

        for (int i = 0; i < numSamples; ++i) {
            float filtered = plosiveBandFilter.process(data[i]);
            float env = plosiveEnvFollower.process(std::abs(filtered));
            maxBurstEnergy = std::max(maxBurstEnergy, env);
        }
    }

    // Detection strength normalized to threshold
    const float threshold = preAnalysisMetrics.plosiveThreshold;
    if (maxBurstEnergy <= threshold) return 0.0f;

    return vxsuite::detectors::clamp01((maxBurstEnergy - threshold) / (threshold * 0.5f));
}

float VXSpeechClarityAudioProcessor::detectBreath(const juce::AudioBuffer<float>& buffer) {
    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    float maxBreathEnergy = 0.0f;

    for (int ch = 0; ch < numChannels; ++ch) {
        const float* data = buffer.getReadPointer(ch);

        for (int i = 0; i < numSamples; ++i) {
            float filtered = breathBandFilter.process(data[i]);
            float env = breathEnvFollower.process(std::abs(filtered));
            maxBreathEnergy = std::max(maxBreathEnergy, env);
        }
    }

    // Detection strength normalized to threshold
    const float threshold = preAnalysisMetrics.breathThreshold;
    if (maxBreathEnergy <= threshold) return 0.0f;

    return vxsuite::detectors::clamp01((maxBreathEnergy - threshold) / (threshold * 0.5f));
}

void VXSpeechClarityAudioProcessor::processProduct(juce::AudioBuffer<float>& buffer,
                                                   juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0)
        return;

    // 1. PRE-ANALYSIS (first call or on reset)
    if (needsPreAnalysis) {
        performPreAnalysis(buffer);
    }

    // 2. READ CONTROL DIALS
    const float sibilanceStrength = vxsuite::readNormalized(parameters, productIdentity.primaryParamId, 0.0f);
    const float plosiveStrength = vxsuite::readNormalized(parameters, productIdentity.secondaryParamId, 0.0f);
    const float breathStrength = vxsuite::readNormalized(parameters, productIdentity.tertiaryParamId, 0.0f);

    // 3. DETECT ARTIFACTS
    detectionState.sibilanceIntensity = detectSibilance(buffer);
    detectionState.plosiveIntensity = detectPlosive(buffer);
    detectionState.breathIntensity = detectBreath(buffer);

    // 4. APPLY PROCESSING (placeholder - DSP will be implemented)
    // TODO: Apply sibilance reduction when sibilanceStrength > 0
    // TODO: Apply plosive reduction when plosiveStrength > 0
    // TODO: Apply breath reduction when breathStrength > 0
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
