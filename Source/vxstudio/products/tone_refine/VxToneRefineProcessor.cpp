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
constexpr std::string_view kHpfOnParam = "hpf_on";
constexpr std::string_view kHiShelfOnParam = "hishelf_on";
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
    identity.primaryDefaultValue = 0.25f;  // Mud  -  gentle low-mid cleanup
    identity.secondaryDefaultValue = 0.20f; // Harshness  -  light presence softening
    identity.tertiaryDefaultValue = 0.15f;  // Smooth  -  barely perceptible on clean sources
    identity.primaryHint = "Remove low-mid buildup (boxiness, muddiness).";
    identity.secondaryHint = "Reduce presence peak harshness (2-5 kHz brittleness).";
    identity.tertiaryHint = "Apply transparent tonal smoothing.";
    identity.showLowShelfIcon  = true;
    identity.showHighShelfIcon = true;
    identity.lowShelfParamId   = kHpfOnParam;
    identity.highShelfParamId  = kHiShelfOnParam;
    identity.defaultLowShelf   = false;
    identity.defaultHighShelf  = false;
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

vxsuite::MeteringSnapshot VXToneRefineAudioProcessor::getMeteringSnapshot() const noexcept {
    vxsuite::MeteringSnapshot s;
    s.activeBandCount = 3;
    s.bandActivity[0] = mudDetectionIntensity;
    s.bandActivity[1] = harshnessDetectionIntensity;
    s.bandActivity[2] = roughnessDetectionIntensity;
    return s;
}

void VXToneRefineAudioProcessor::rebuildFilters(const bool hpfOn, const bool hiShelfOn, const bool voiceMode) {
    using namespace vxsuite::corrective::detail;
    const int numCh = std::max(1, getTotalNumOutputChannels());

    // HPF: 2nd-order Butterworth. Vocal: 80 Hz, General: 40 Hz.
    {
        const float fc = voiceMode ? 80.0f : 40.0f;
        const float K  = std::tan(juce::MathConstants<float>::pi * fc / static_cast<float>(currentSampleRateHz));
        const float K2 = K * K;
        const float norm = 1.0f / (1.0f + juce::MathConstants<float>::sqrt2 * K + K2);
        hpfB0 =  norm;
        hpfB1 = -2.0f * norm;
        hpfB2 =  norm;
        hpfA1 =  2.0f * (K2 - 1.0f) * norm;
        hpfA2 = (1.0f - juce::MathConstants<float>::sqrt2 * K + K2) * norm;

        const bool toggled = hpfOn != lastHpfOn;
        if (hpfZ1.size() != static_cast<size_t>(numCh)) {
            hpfZ1.assign(static_cast<size_t>(numCh), 0.0f);
            hpfZ2.assign(static_cast<size_t>(numCh), 0.0f);
        } else if (toggled) {
            std::fill(hpfZ1.begin(), hpfZ1.end(), 0.0f);
            std::fill(hpfZ2.begin(), hpfZ2.end(), 0.0f);
        }
    }

    // Hi-Shelf: gentle air cut. Vocal: -3 dB @ 8 kHz, General: -4 dB @ 10 kHz.
    {
        const float fc     = voiceMode ? 8000.0f : 10000.0f;
        const float gainDb = voiceMode ? -3.0f   : -4.0f;
        hiShelfCoeffs = makeHighShelf(currentSampleRateHz, fc, 0.7f, gainDb);

        const bool toggled = hiShelfOn != lastHiShelfOn;
        if (hiShelfZ1.size() != static_cast<size_t>(numCh)) {
            hiShelfZ1.assign(static_cast<size_t>(numCh), 0.0f);
            hiShelfZ2.assign(static_cast<size_t>(numCh), 0.0f);
        } else if (toggled) {
            std::fill(hiShelfZ1.begin(), hiShelfZ1.end(), 0.0f);
            std::fill(hiShelfZ2.begin(), hiShelfZ2.end(), 0.0f);
        }
    }

    lastHpfOn      = hpfOn;
    lastHiShelfOn  = hiShelfOn;
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

    // Reset filter state
    std::fill(hpfZ1.begin(), hpfZ1.end(), 0.0f);
    std::fill(hpfZ2.begin(), hpfZ2.end(), 0.0f);
    std::fill(hiShelfZ1.begin(), hiShelfZ1.end(), 0.0f);
    std::fill(hiShelfZ2.begin(), hiShelfZ2.end(), 0.0f);
    lastHpfOn     = false;
    lastHiShelfOn = false;
}

void VXToneRefineAudioProcessor::performPreAnalysis(const juce::AudioBuffer<float>& buffer) {
    // Pre-analysis now sets fixed, ratio-based thresholds  -  no signal scanning needed.
    // The previous wideband amplitude approach caused false positives on any signal.
    //
    // Mud threshold: lag-1 autocorrelation. Speech typically R1 ~ 0.60-0.80;
    //   LF-dominant (muddy) content pushes R1 above 0.85.
    // Harshness threshold: HF energy ratio (first-difference / broadband).
    //   Normal speech ~ 0.05-0.18; elevated mid-HF (harshness) > 0.20.
    // Roughness threshold: derivative/RMS ratio. Set relative so it's level-independent.
    (void)buffer;
    mudThreshold       = 0.85f;  // R1 autocorrelation threshold
    harshnessThreshold = 0.10f;  // HF ratio threshold (first-difference / broadband)
    roughnessThreshold = 0.30f;  // derivative/RMS ratio threshold
    detectorState.needsPreAnalysis = false;
}

void VXToneRefineAudioProcessor::detectAndUpdateIntensities(const juce::AudioBuffer<float>& buffer) {
    // Frequency-discriminating detection  -  replaces the wideband envelope approach
    // that fired for any speech energy regardless of its spectral character.
    //
    // Mud     (100-500 Hz dominance): lag-1 autocorrelation. LF-heavy signals change
    //   slowly so adjacent samples correlate strongly (R1 near 1.0). Normal speech
    //   sits at R1 ~ 0.60-0.80; mud pushes R1 > 0.85.
    //
    // Harshness (2-5 kHz presence): HF energy proxy via first-difference, targeting
    //   a moderate range above normal speech but below sibilance. Fires when HF content
    //   is elevated (presence peak) without being sibilance-dominated.
    //
    // Roughness: first-difference magnitude normalised by RMS (level-independent
    //   spectral irregularity). State-free and genuinely HF-sensitive  -  unchanged.

    const int numCh = buffer.getNumChannels();
    const int n     = buffer.getNumSamples();
    if (n < 2) return;

    float sumSq = 0.0f, sumProd = 0.0f, hfEnergy = 0.0f, derivPeak = 0.0f;

    for (int ch = 0; ch < numCh; ++ch) {
        const float* d = buffer.getReadPointer(ch);
        for (int i = 1; i < n; ++i) {
            const float s    = d[i];
            const float prev = d[i - 1];
            const float diff = s - prev;

            sumSq    += s * s;
            sumProd  += s * prev;             // lag-1 cross-product
            hfEnergy += diff * diff;          // first-difference energy (HF proxy)
            derivPeak = std::max(derivPeak, std::abs(diff));
        }
    }

    const float bbTotal = std::max(sumSq, 1.0e-9f);
    const float rms     = std::sqrt(sumSq / std::max(1, numCh * (n - 1)));
    const bool  hasSig  = rms > 0.002f;  // −54 dBFS silence gate

    // Mud: R1 (lag-1 autocorrelation) high → LF-dominated
    const float r1 = sumProd / bbTotal;
    const float mudIntensity = hasSig && r1 > mudThreshold
        ? std::min(1.0f, (r1 - mudThreshold) / 0.10f)
        : 0.0f;

    // Harshness: HF ratio in the moderate range (above normal speech, below sibilance)
    const float hfRatio = hfEnergy / bbTotal;
    const float harshnessIntensity = hasSig && hfRatio > harshnessThreshold && hfRatio < 0.55f
        ? std::min(1.0f, (hfRatio - harshnessThreshold) / 0.15f)
        : 0.0f;

    // Roughness: normalised derivative peak  -  level-independent spectral irregularity
    const float normDeriv = rms > 1.0e-6f ? derivPeak / rms : 0.0f;
    const float roughnessIntensity = hasSig && normDeriv > roughnessThreshold
        ? std::min(1.0f, (normDeriv - roughnessThreshold) / 1.0f)
        : 0.0f;

    mudDetectionIntensity       = 0.9f * mudDetectionIntensity       + 0.1f * mudIntensity;
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

    // 5. APPLY HP / HI-SHELF FILTERS
    const bool hpfOn     = vxsuite::readBool(parameters, kHpfOnParam,     false);
    const bool hiShelfOn = vxsuite::readBool(parameters, kHiShelfOnParam, false);
    const bool voiceMode = vxsuite::readMode(parameters, productIdentity) == vxsuite::Mode::vocal;

    if (hpfOn != lastHpfOn || hiShelfOn != lastHiShelfOn)
        rebuildFilters(hpfOn, hiShelfOn, voiceMode);

    if (hpfOn) {
        const int numCh = buffer.getNumChannels();
        for (int ch = 0; ch < numCh && ch < static_cast<int>(hpfZ1.size()); ++ch) {
            float* buf = buffer.getWritePointer(ch);
            float& z1  = hpfZ1[static_cast<size_t>(ch)];
            float& z2  = hpfZ2[static_cast<size_t>(ch)];
            for (int i = 0; i < numSamples; ++i)
                buf[i] = vxsuite::corrective::detail::processBiquadDf2(
                    buf[i], hpfB0, hpfB1, hpfB2, hpfA1, hpfA2, z1, z2);
        }
    }

    if (hiShelfOn) {
        const int numCh = buffer.getNumChannels();
        for (int ch = 0; ch < numCh && ch < static_cast<int>(hiShelfZ1.size()); ++ch) {
            float* buf = buffer.getWritePointer(ch);
            float& z1  = hiShelfZ1[static_cast<size_t>(ch)];
            float& z2  = hiShelfZ2[static_cast<size_t>(ch)];
            for (int i = 0; i < numSamples; ++i)
                buf[i] = vxsuite::corrective::detail::processBiquadDf2(
                    buf[i], hiShelfCoeffs, z1, z2);
        }
    }
}

void VXToneRefineAudioProcessor::renderListenOutput(juce::AudioBuffer<float>& outputBuffer,
                                                    const juce::AudioBuffer<float>& inputBuffer) {
    renderAddedDeltaOutput(outputBuffer, inputBuffer);
}

#if !defined(VXSTUDIO_DISABLE_PLUGIN_ENTRYPOINT)
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new VXToneRefineAudioProcessor();
}
#endif
