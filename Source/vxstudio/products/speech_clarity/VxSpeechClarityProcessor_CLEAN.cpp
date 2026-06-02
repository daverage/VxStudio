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
    const int ch = getTotalNumOutputChannels();

    deEsserDsp.prepare(currentSampleRateHz, samplesPerBlock, ch);
    dePolosiveDsp.prepare(currentSampleRateHz, samplesPerBlock, ch);
    deBreathDsp.prepare(currentSampleRateHz, samplesPerBlock, ch);

    // HPF: 2nd-order Butterworth at 80 Hz
    {
        using namespace juce;
        const float K  = std::tan(MathConstants<float>::pi * 80.0f / static_cast<float>(currentSampleRateHz));
        const float K2 = K * K;
        const float norm = 1.0f / (1.0f + MathConstants<float>::sqrt2 * K + K2);
        hpfB0 =  norm;
        hpfB1 = -2.0f * norm;
        hpfB2 =  norm;
        hpfA1 =  2.0f * (K2 - 1.0f) * norm;
        hpfA2 = (1.0f - MathConstants<float>::sqrt2 * K + K2) * norm;
    }

    // Hi-shelf: −2.5 dB at 7 kHz (gentle air/harshness softener)
    hiShelfCoeffs = vxsuite::corrective::detail::makeHighShelf(currentSampleRateHz, 7000.0f, 0.66f, -2.5f);

    hpfZ1.assign(static_cast<size_t>(ch), 0.0f);
    hpfZ2.assign(static_cast<size_t>(ch), 0.0f);
    hiShelfZ1.assign(static_cast<size_t>(ch), 0.0f);
    hiShelfZ2.assign(static_cast<size_t>(ch), 0.0f);

    resetSuite();
}

void VXSpeechClarityAudioProcessor::resetSuite() {
    sibilanceDetectionIntensity = 0.0f;
    plosiveDetectionIntensity   = 0.0f;
    breathDetectionIntensity    = 0.0f;
    smoothedPeak = 0.01f;

    std::fill(hpfZ1.begin(),     hpfZ1.end(),     0.0f);
    std::fill(hpfZ2.begin(),     hpfZ2.end(),     0.0f);
    std::fill(hiShelfZ1.begin(), hiShelfZ1.end(), 0.0f);
    std::fill(hiShelfZ2.begin(), hiShelfZ2.end(), 0.0f);

    deEsserDsp.reset();
    dePolosiveDsp.reset();
    deBreathDsp.reset();
}

void VXSpeechClarityAudioProcessor::detectAndUpdateIntensities(const juce::AudioBuffer<float>& buffer) {
    const int numCh      = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();
    if (numSamples < 2) return;

    float hfEnergy = 0.0f, bbEnergy = 0.0f, peakSq = 0.0f;

    for (int ch = 0; ch < numCh; ++ch) {
        const float* d = buffer.getReadPointer(ch);
        for (int i = 1; i < numSamples; ++i) {
            const float s    = d[i];
            const float diff = s - d[i - 1];
            hfEnergy += diff * diff;
            bbEnergy += s * s;
            peakSq = std::max(peakSq, s * s);
        }
    }

    const int   nSamp   = numCh * (numSamples - 1);
    const float bbTotal = std::max(bbEnergy, 1.0e-9f);
    const float rms     = std::sqrt(bbEnergy / std::max(1, nSamp));
    const bool  hasSig  = rms > 0.002f;

    // Sibilance: first-difference energy ratio. Threshold lowered to 0.08 so that
    // sibilance is detected even when mixed under voiced speech content.
    const float hfRatio      = hfEnergy / bbTotal;
    const float sibIntensity = hasSig && hfRatio > 0.08f
        ? std::min(1.0f, (hfRatio - 0.08f) / 0.30f)
        : 0.0f;

    // Plosive: crest factor > 4 (sharp transient burst)
    const float crest         = std::sqrt(peakSq * static_cast<float>(nSamp) / bbTotal);
    const float plosIntensity = hasSig && crest > 4.0f
        ? std::min(1.0f, (crest - 4.0f) / 4.0f)
        : 0.0f;

    // Breath: signal present but well below the running peak level, with some HF content.
    // smoothedPeak tracks the slow-decaying signal peak so quiet passages read as "low relative level".
    smoothedPeak = std::max(smoothedPeak * 0.9998f, rms);
    const float levelRel      = smoothedPeak > 1.0e-6f ? rms / smoothedPeak : 0.0f;
    const float breathIntensity = hasSig && levelRel < 0.35f && hfRatio > 0.06f
        ? std::min(1.0f, (0.35f - levelRel) / 0.20f)
        : 0.0f;

    sibilanceDetectionIntensity = 0.9f * sibilanceDetectionIntensity + 0.1f * sibIntensity;
    plosiveDetectionIntensity   = 0.9f * plosiveDetectionIntensity   + 0.1f * plosIntensity;
    breathDetectionIntensity    = 0.9f * breathDetectionIntensity    + 0.1f * breathIntensity;
}

void VXSpeechClarityAudioProcessor::processProduct(juce::AudioBuffer<float>& buffer,
                                                   juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0)
        return;

    // 1. READ CONTROL DIALS
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

    // HPF and hi-shelf — applied after artifact removal
    const bool hpfOn     = vxsuite::readBool(parameters, kHpfOnParam,     false);
    const bool hiShelfOn = vxsuite::readBool(parameters, kHiShelfOnParam, false);

    const int numCh = buffer.getNumChannels();
    if (hpfOn && !hpfZ1.empty()) {
        for (int ch = 0; ch < std::min(numCh, static_cast<int>(hpfZ1.size())); ++ch) {
            auto* data = buffer.getWritePointer(ch);
            float z1 = hpfZ1[static_cast<size_t>(ch)];
            float z2 = hpfZ2[static_cast<size_t>(ch)];
            for (int i = 0; i < numSamples; ++i)
                data[i] = vxsuite::corrective::detail::processBiquadDf2(data[i], hpfB0, hpfB1, hpfB2, hpfA1, hpfA2, z1, z2);
            hpfZ1[static_cast<size_t>(ch)] = z1;
            hpfZ2[static_cast<size_t>(ch)] = z2;
        }
    }

    if (hiShelfOn && !hiShelfZ1.empty()) {
        for (int ch = 0; ch < std::min(numCh, static_cast<int>(hiShelfZ1.size())); ++ch) {
            auto* data = buffer.getWritePointer(ch);
            float z1 = hiShelfZ1[static_cast<size_t>(ch)];
            float z2 = hiShelfZ2[static_cast<size_t>(ch)];
            for (int i = 0; i < numSamples; ++i)
                data[i] = vxsuite::corrective::detail::processBiquadDf2(data[i], hiShelfCoeffs, z1, z2);
            hiShelfZ1[static_cast<size_t>(ch)] = z1;
            hiShelfZ2[static_cast<size_t>(ch)] = z2;
        }
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
