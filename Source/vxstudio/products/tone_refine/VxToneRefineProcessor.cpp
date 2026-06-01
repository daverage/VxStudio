#include "VxToneRefineProcessor.h"
#include "../../framework/VxStudioHelpContent.h"
#include "VxStudioVersions.h"

namespace {
constexpr std::string_view kProductName = "VX Studio Tone Refine";
constexpr std::string_view kShortTag = "TRF";
constexpr std::string_view kMudParam = "mud";
constexpr std::string_view kHarshnessParam = "harshness";
constexpr std::string_view kSmoothParam = "smooth";
constexpr std::string_view kModeParam = "mode";
constexpr std::string_view kListenParam = "listen";
} // namespace

VXToneRefineAudioProcessor::VXToneRefineAudioProcessor()
    : ProcessorBase(makeIdentity()) {}

vxsuite::ProductIdentity VXToneRefineAudioProcessor::makeIdentity() {
    vxsuite::ProductIdentity identity {};
    identity.productName = kProductName;
    identity.shortTag = kShortTag;
    identity.primaryParamId = kMudParam;
    identity.secondaryParamId = kHarshnessParam;
    identity.tertiaryParamId = kSmoothParam;
    identity.modeParamId = kModeParam;
    identity.listenParamId = kListenParam;
    identity.defaultMode = vxsuite::Mode::vocal;
    identity.primaryLabel = "Mud";
    identity.secondaryLabel = "Harshness";
    identity.tertiaryLabel = "Smooth";
    identity.primaryDefaultValue = 0.0f;
    identity.secondaryDefaultValue = 0.0f;
    identity.tertiaryDefaultValue = 0.0f;
    identity.primaryHint = "Remove low-mid buildup (boxiness, muddiness).";
    identity.secondaryHint = "Reduce presence peak harshness (2-5 kHz brittleness).";
    identity.tertiaryHint = "Apply transparent tonal smoothing.";
    identity.dspVersion = vxsuite::versions::plugins::tone_refine;
    identity.helpTitle = vxsuite::help::tone_refine.title;
    identity.helpHtml = vxsuite::help::tone_refine.html;
    identity.readmeSection = vxsuite::help::tone_refine.readmeSection;
    identity.theme.accentRgb = { 0.94f, 0.68f, 0.25f };      // Gold
    identity.theme.accent2Rgb = { 0.24f, 0.16f, 0.06f };
    identity.theme.backgroundRgb = { 0.08f, 0.06f, 0.03f };
    identity.theme.panelRgb = { 0.14f, 0.10f, 0.05f };
    identity.theme.textRgb = { 0.99f, 0.93f, 0.81f };
    return identity;
}

juce::String VXToneRefineAudioProcessor::getStatusText() const {
    if (isListenEnabled())
        return "Listen - tone refine delta";
    return "Low-mid, harshness, and tonal smoothing refinement";
}

float VXToneRefineAudioProcessor::getActivityLight(int index) const noexcept {
    switch (index) {
        case 0: return mudDetectionIntensity;
        case 1: return harshnessDetectionIntensity;
        case 2: return roughnessDetectionIntensity;
        default: return 0.0f;
    }
}

std::string_view VXToneRefineAudioProcessor::getActivityLightLabel(int index) const noexcept {
    switch (index) {
        case 0: return "Mud";
        case 1: return "Harsh";
        case 2: return "Rough";
        default: return {};
    }
}

void VXToneRefineAudioProcessor::prepareSuite(const double sampleRate, const int samplesPerBlock) {
    currentSampleRateHz = sampleRate > 1000.0 ? sampleRate : 48000.0;

    // Initialize all DSP components
    deMudDsp.prepare(currentSampleRateHz, samplesPerBlock, getTotalNumOutputChannels());
    deHarshnessDsp.prepare(currentSampleRateHz, samplesPerBlock, getTotalNumOutputChannels());
    intelligentSmoothDsp.prepare(currentSampleRateHz, samplesPerBlock, getTotalNumOutputChannels());

    resetSuite();
}

void VXToneRefineAudioProcessor::resetSuite() {
    detectorState.needsPreAnalysis = true;
    detectorState.mudEnvelope = 0.0f;
    detectorState.harshnessEnvelope = 0.0f;
    detectorState.roughnessMetric = 0.0f;
    mudDetectionIntensity = 0.0f;
    harshnessDetectionIntensity = 0.0f;
    roughnessDetectionIntensity = 0.0f;

    // Reset DSP components
    deMudDsp.reset();
    deHarshnessDsp.reset();
    intelligentSmoothDsp.reset();
}

void VXToneRefineAudioProcessor::performPreAnalysis(const juce::AudioBuffer<float>& buffer) {
    // Pre-analysis: establish adaptive thresholds for mud, harshness, roughness

    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    float maxMudEnergy = 0.0f;
    float maxHarshnessEnergy = 0.0f;
    float maxRoughness = 0.0f;

    // Simple envelope-based measurement for each band
    for (int ch = 0; ch < numChannels; ++ch) {
        const float* data = buffer.getReadPointer(ch);

        float mudAccum = 0.0f;
        float harshnessAccum = 0.0f;
        float prevSample = 0.0f;

        for (int i = 0; i < numSamples; ++i) {
            const float sample = data[i];

            // Mud: very low mid (100-500 Hz approximation)
            mudAccum = 0.98f * mudAccum + 0.02f * std::abs(sample);

            // Harshness: presence peak (2-5 kHz approximation)
            harshnessAccum = 0.97f * harshnessAccum + 0.03f * std::abs(sample);

            // Roughness: spectral derivative (rate of change)
            const float derivative = std::abs(sample - prevSample);

            maxMudEnergy = std::max(maxMudEnergy, mudAccum);
            maxHarshnessEnergy = std::max(maxHarshnessEnergy, harshnessAccum);
            maxRoughness = std::max(maxRoughness, derivative);

            prevSample = sample;
        }
    }

    // Set adaptive thresholds
    mudThreshold = maxMudEnergy * 0.80f;
    harshnessThreshold = maxHarshnessEnergy * 0.80f;
    roughnessThreshold = maxRoughness * 0.80f;

    detectorState.needsPreAnalysis = false;
}

void VXToneRefineAudioProcessor::detectAndUpdateIntensities(const juce::AudioBuffer<float>& buffer) {
    // Per-block detection for mud, harshness, roughness

    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    float blockMudMax = 0.0f;
    float blockHarshnessMax = 0.0f;
    float blockRoughnessMax = 0.0f;

    for (int ch = 0; ch < numChannels; ++ch) {
        const float* data = buffer.getReadPointer(ch);

        float mudEnv = 0.0f;
        float harshnessEnv = 0.0f;
        float prevSample = 0.0f;

        for (int i = 0; i < numSamples; ++i) {
            const float sample = std::abs(data[i]);

            // Mud envelope (slow accumulation in low-mid)
            mudEnv = 0.98f * mudEnv + 0.02f * sample;

            // Harshness envelope (slightly faster in presence region)
            harshnessEnv = 0.97f * harshnessEnv + 0.03f * sample;

            // Roughness metric (spectral derivative)
            const float derivative = std::abs(data[i] - prevSample);

            blockMudMax = std::max(blockMudMax, mudEnv);
            blockHarshnessMax = std::max(blockHarshnessMax, harshnessEnv);
            blockRoughnessMax = std::max(blockRoughnessMax, derivative);

            prevSample = data[i];
        }
    }

    // Convert to LED intensity (0-1)
    const float mudIntensity = blockMudMax > mudThreshold
        ? std::min(1.0f, (blockMudMax - mudThreshold) / (mudThreshold * 0.5f))
        : 0.0f;

    const float harshnessIntensity = blockHarshnessMax > harshnessThreshold
        ? std::min(1.0f, (blockHarshnessMax - harshnessThreshold) / (harshnessThreshold * 0.5f))
        : 0.0f;

    const float roughnessIntensity = blockRoughnessMax > roughnessThreshold
        ? std::min(1.0f, (blockRoughnessMax - roughnessThreshold) / (roughnessThreshold * 0.5f))
        : 0.0f;

    // Smooth LED feedback
    mudDetectionIntensity = 0.9f * mudDetectionIntensity + 0.1f * mudIntensity;
    harshnessDetectionIntensity = 0.9f * harshnessDetectionIntensity + 0.1f * harshnessIntensity;
    roughnessDetectionIntensity = 0.9f * roughnessDetectionIntensity + 0.1f * roughnessIntensity;
}

void VXToneRefineAudioProcessor::processProduct(juce::AudioBuffer<float>& buffer,
                                               juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0)
        return;

    // 1. PRE-ANALYSIS
    if (detectorState.needsPreAnalysis) {
        performPreAnalysis(buffer);
    }

    // 2. READ CONTROL DIALS
    const float mudStrength = vxsuite::readNormalized(
        parameters, productIdentity.primaryParamId, 0.0f);
    const float harshnessStrength = vxsuite::readNormalized(
        parameters, productIdentity.secondaryParamId, 0.0f);
    const float smoothStrength = vxsuite::readNormalized(
        parameters, productIdentity.tertiaryParamId, 0.0f);

    // 3. DETECT TONAL ISSUES AND UPDATE LED FEEDBACK
    detectAndUpdateIntensities(buffer);

    // 4. APPLY PROCESSING (each DSP runs independently if its dial > 0)
    if (mudStrength > 0.001f) {
        vxsuite::tone_refine::DeMudDsp::Params mudParams {
            mudStrength,
            mudDetectionIntensity
        };
        deMudDsp.process(buffer, mudParams);
    }

    if (harshnessStrength > 0.001f) {
        vxsuite::tone_refine::DeHarshnessDsp::Params harshnessParams {
            harshnessStrength,
            harshnessDetectionIntensity
        };
        deHarshnessDsp.process(buffer, harshnessParams);
    }

    if (smoothStrength > 0.001f) {
        vxsuite::tone_refine::IntelligentSmoothDsp::Params smoothParams {
            smoothStrength,
            roughnessDetectionIntensity
        };
        intelligentSmoothDsp.process(buffer, smoothParams);
    }
}

void VXToneRefineAudioProcessor::renderListenOutput(juce::AudioBuffer<float>& outputBuffer,
                                                    const juce::AudioBuffer<float>& inputBuffer) {
    renderAddedDeltaOutput(outputBuffer, inputBuffer);
}

#if !defined(VXSUITE_DISABLE_PLUGIN_ENTRYPOINT) && !defined(VXSTUDIO_DISABLE_PLUGIN_ENTRYPOINT)
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new VXToneRefineAudioProcessor();
}
#endif
