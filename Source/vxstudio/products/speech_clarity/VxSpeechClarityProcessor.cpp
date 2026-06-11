#include "VxSpeechClarityProcessor.h"
#include "../../framework/VxStudioHelpContent.h"
#include "VxStudioVersions.h"

namespace {

constexpr std::string_view kProductName = "VX Studio Speech Clarity";
constexpr std::string_view kShortTag = "CLR";
constexpr std::string_view kClickParam     = "click";
constexpr std::string_view kPlosiveParam   = "plosive";
constexpr std::string_view kBreathParam    = "breath";
constexpr std::string_view kSibilanceParam = "sibilance";
constexpr std::string_view kModeParam = "mode";
constexpr std::string_view kListenParam = "listen";

} // namespace

VXSpeechClarityAudioProcessor::VXSpeechClarityAudioProcessor()
    : ProcessorBase(makeIdentity()) {}

vxsuite::ProductIdentity VXSpeechClarityAudioProcessor::makeIdentity() {
    vxsuite::ProductIdentity identity {};
    identity.productName = kProductName;
    identity.shortTag = kShortTag;
    identity.primaryParamId    = kSibilanceParam;
    identity.secondaryParamId  = kPlosiveParam;
    identity.tertiaryParamId   = kBreathParam;
    identity.quaternaryParamId = kClickParam;
    identity.modeParamId       = kModeParam;
    identity.listenParamId     = kListenParam;
    identity.defaultMode       = vxsuite::Mode::vocal;
    identity.primaryLabel    = "Sibilance";
    identity.secondaryLabel  = "Plosive";
    identity.tertiaryLabel   = "Breath";
    identity.quaternaryLabel = "Click";
    identity.primaryDefaultValue    = 0.40f;
    identity.secondaryDefaultValue  = 0.45f;
    identity.tertiaryDefaultValue   = 0.35f;
    identity.quaternaryDefaultValue = 0.45f;
    identity.primaryHint    = "Reduce harsh sibilance (/s/ and /z/ sounds).";
    identity.secondaryHint  = "Reduce plosive bursts (/p/, /b/, /t/, /d/, /k/, /g/).";
    identity.tertiaryHint   = "Reduce breathing and wind noise.";
    identity.quaternaryHint = "Repair clicks, lip ticks, and mouth noise.";
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
    return 4;
}

float VXSpeechClarityAudioProcessor::getActivityLight(int index) const noexcept {
    switch (index) {
        case 0: return detectionState.sibilanceIntensity;
        case 1: return detectionState.plosiveIntensity;
        case 2: return detectionState.breathIntensity;
        case 3: return detectionState.clickIntensity;
        default: return 0.0f;
    }
}

std::string_view VXSpeechClarityAudioProcessor::getActivityLightLabel(int index) const noexcept {
    switch (index) {
        case 0: return "Sibl";
        case 1: return "Plos";
        case 2: return "Brth";
        case 3: return "Clck";
        default: return {};
    }
}

vxsuite::MeteringSnapshot VXSpeechClarityAudioProcessor::getMeteringSnapshot() const noexcept {
    vxsuite::MeteringSnapshot s;
    s.activeBandCount = 4;
    s.bandActivity[0] = detectionState.sibilanceIntensity;
    s.bandActivity[1] = detectionState.plosiveIntensity;
    s.bandActivity[2] = detectionState.breathIntensity;
    s.bandActivity[3] = detectionState.clickIntensity;
    return s;
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
    breathBandFilter.setCoefficients(vxsuite::detectors::BiquadFilter::Type::HighPass,
                                    900.0f, 0.7f, currentSampleRateHz);

    onsetDetector.setSampleRate(currentSampleRateHz);

    const int numChannels = getTotalNumOutputChannels();
    deClickDsp.prepare(currentSampleRateHz, samplesPerBlock, numChannels);
    dePlosiveDsp.prepare(currentSampleRateHz, samplesPerBlock, numChannels);
    deBreathDsp.prepare(currentSampleRateHz, samplesPerBlock, numChannels);
    deEsserDsp.prepare(currentSampleRateHz, samplesPerBlock, numChannels);

    setReportedLatencySamples(deClickDsp.getLatencySamples());

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
    lastMode = vxsuite::Mode::vocal;
    deClickDsp.reset();
    dePlosiveDsp.reset();
    deBreathDsp.reset();
    deEsserDsp.reset();
}

void VXSpeechClarityAudioProcessor::performPreAnalysis(const juce::AudioBuffer<float>& buffer) {
    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();
    const int total = numChannels * numSamples;
    if (total <= 0) {
        needsPreAnalysis = false;
        return;
    }

    double sibSum = 0.0, plosSum = 0.0, breathSum = 0.0;

    for (int ch = 0; ch < numChannels; ++ch) {
        const float* data = buffer.getReadPointer(ch);
        for (int i = 0; i < numSamples; ++i) {
            const float sample = data[i];
            sibSum    += std::abs(sibilanceBandFilter.process(sample));
            plosSum   += std::abs(plosiveBandFilter.process(sample));
            breathSum += std::abs(breathBandFilter.process(sample));
        }
    }

    // Threshold = mean × multiplier. Plosives/breath are sharp bursts well above
    // the mean band energy — using max caused constant false-triggering because
    // the peak is often within the normal signal range.
    //
    // If all band means are near-silent (e.g. plugin initialised into silence),
    // defer pre-analysis until there is actual content; setting near-zero thresholds
    // causes every real signal to exceed them and keeps the LEDs permanently lit.
    constexpr float kMinMeanEnergy = 1.0e-3f;
    const float meanSib    = static_cast<float>(sibSum    / total);
    const float meanPlos   = static_cast<float>(plosSum   / total);
    const float meanBreath = static_cast<float>(breathSum / total);
    if (meanSib < kMinMeanEnergy && meanPlos < kMinMeanEnergy && meanBreath < kMinMeanEnergy)
        return; // keep needsPreAnalysis = true; retry on next buffer

    preAnalysisMetrics.sibilanceThreshold = meanSib    * 4.0f;
    preAnalysisMetrics.plosiveThreshold   = meanPlos   * 5.0f;
    preAnalysisMetrics.breathThreshold    = meanBreath * 4.0f;
    preAnalysisMetrics.isValid = true;
    needsPreAnalysis = false;

    // Reset filters so the detect phase runs from a clean state on the next buffer
    // rather than inheriting the warmed state from this pre-analysis pass.
    sibilanceBandFilter.reset();
    plosiveBandFilter.reset();
    breathBandFilter.reset();
    sibilanceEnvFollower.reset();
    plosiveEnvFollower.reset();
    breathEnvFollower.reset();
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
    // Skip detection on the pre-analysis frame — filters are reset at the end of
    // performPreAnalysis so the detect phase must run on the following buffer.
    if (needsPreAnalysis) {
        performPreAnalysis(buffer);
        return;
    }

    // 2. READ CONTROL DIALS
    const float sibilanceStrength = vxsuite::readNormalized(parameters, productIdentity.primaryParamId,    0.0f);
    const float plosiveStrength   = vxsuite::readNormalized(parameters, productIdentity.secondaryParamId,  0.0f);
    const float breathStrength    = vxsuite::readNormalized(parameters, productIdentity.tertiaryParamId,   0.0f);
    const float clickStrength     = vxsuite::readNormalized(parameters, productIdentity.quaternaryParamId, 0.0f);

    // 3. PROPAGATE MODE TO ALL DSPs (only when changed)
    const vxsuite::Mode currentMode = vxsuite::readMode(parameters, productIdentity);
    if (currentMode != lastMode) {
        using CM = vxsuite::speech_clarity::CleanupMode;
        const CM cm = (currentMode == vxsuite::Mode::vocal) ? CM::Speech : CM::General;
        deClickDsp.setMode(cm);
        dePlosiveDsp.setMode(cm);
        deBreathDsp.setMode(cm);
        deEsserDsp.setMode(cm);
        setReportedLatencySamples(deClickDsp.getLatencySamples());
        lastMode = currentMode;
    }

    // 4. DETECT ARTIFACTS
    detectionState.sibilanceIntensity = detectSibilance(buffer);
    detectionState.plosiveIntensity   = detectPlosive(buffer);
    detectionState.breathIntensity    = detectBreath(buffer);
    detectionState.clickIntensity = juce::jlimit(0.0f, 1.0f,
        deClickDsp.getLastHardClickRepair() + deClickDsp.getLastMouthClickRepair());

    // 5. APPLY PROCESSING (order: click → plosive → breath → esser)
    deClickDsp.process(buffer, { clickStrength, clickStrength });
    if (plosiveStrength > 0.001f)
        dePlosiveDsp.process(buffer, { plosiveStrength, detectionState.plosiveIntensity });
    if (breathStrength > 0.001f)
        deBreathDsp.process(buffer, { breathStrength, detectionState.breathIntensity });
    if (sibilanceStrength > 0.001f)
        deEsserDsp.process(buffer, { sibilanceStrength, detectionState.sibilanceIntensity });
}

void VXSpeechClarityAudioProcessor::renderListenOutput(juce::AudioBuffer<float>& outputBuffer,
                                                       const juce::AudioBuffer<float>& /*inputBuffer*/) {
    ensureLatencyAlignedListenDry(outputBuffer.getNumSamples());
    renderAddedDeltaOutput(outputBuffer, getLatencyAlignedListenDryBuffer());
}

#if !defined(VXSUITE_DISABLE_PLUGIN_ENTRYPOINT) && !defined(VXSTUDIO_DISABLE_PLUGIN_ENTRYPOINT)
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new VXSpeechClarityAudioProcessor();
}
#endif
