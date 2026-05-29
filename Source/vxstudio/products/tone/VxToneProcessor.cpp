#include "VxToneProcessor.h"

#include "vxstudio/framework/VxStudioHelpContent.h"
#include "vxstudio/framework/VxStudioBlockSmoothing.h"
#include "vxstudio/framework/VxStudioParameters.h"
#include "VxStudioVersions.h"

#include <cmath>
#include <memory>

namespace {

constexpr std::string_view kProductName = "VX Studio Tone";
constexpr std::string_view kShortTag     = "TNE";
constexpr std::string_view kBassParam    = "bass";
constexpr std::string_view kMidParam     = "mid";
constexpr std::string_view kTrebleParam  = "treble";
constexpr std::string_view kModeParam    = "mode";
constexpr std::string_view kListenParam  = "listen";

// Vocal mode: narrower shelves that leave the fundamental speech band (200-4kHz) untouched.
// General mode: full-range shelves with more headroom.
constexpr float kVocalBassFreqHz    = 180.f;
constexpr float kVocalMidFreqHz     = 2000.f;
constexpr float kVocalTrebleFreqHz  = 5600.f;
constexpr float kVocalMaxGainDb     = 14.f;
constexpr float kVocalMidMaxGainDb  = 12.f;
constexpr float kGeneralBassFreqHz  = 90.f;
constexpr float kGeneralMidFreqHz   = 1000.f;
constexpr float kGeneralTrebleFreqHz = 10500.f;
constexpr float kGeneralMaxGainDb   = 22.f;
constexpr float kGeneralMidMaxGainDb = 18.f;
constexpr float kToneCurveExponent  = 0.72f;

float shapeToneControl(const float value) noexcept {
    const float centered = juce::jlimit(-1.0f, 1.0f, value * 2.0f - 1.0f);
    const float shaped = std::copysign(std::pow(std::abs(centered), kToneCurveExponent), centered);
    return 0.5f * (shaped + 1.0f);
}

} // namespace

static juce::AudioProcessorValueTreeState::ParameterLayout createToneParameterLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Bass - centered at 0, -100 to +100
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "bass", 1 },
        "Bass",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f },
        0.5f,
        vxsuite::makeCenteredPercentFloatAttributes()));

    // Mid - centered at 0, -100 to +100
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "mid", 1 },
        "Mid",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f },
        0.5f,
        vxsuite::makeCenteredPercentFloatAttributes()));

    // Treble - centered at 0, -100 to +100
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "treble", 1 },
        "Treble",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f },
        0.5f,
        vxsuite::makeCenteredPercentFloatAttributes()));

    // Mode parameter
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { "mode", 1 },
        "Mode",
        juce::StringArray { "Vocal", "General" },
        0));

    // Listen parameter
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { "listen", 1 },
        "Listen",
        false));

    return layout;
}

VXToneAudioProcessor::VXToneAudioProcessor()
    : ProcessorBase(makeIdentity(), createToneParameterLayout()) {}

vxsuite::ProductIdentity VXToneAudioProcessor::makeIdentity() {
    vxsuite::ProductIdentity id {};
    id.productName        = kProductName;
    id.shortTag           = kShortTag;
    id.primaryParamId     = kBassParam;
    id.secondaryParamId   = kMidParam;
    id.tertiaryParamId    = kTrebleParam;
    id.modeParamId        = kModeParam;
    id.listenParamId      = kListenParam;
    id.defaultMode        = vxsuite::Mode::vocal;
    id.primaryLabel       = "Bass";
    id.secondaryLabel     = "Mid";
    id.tertiaryLabel      = "Treble";
    id.primaryHint        = "Low shelf boost or cut. In Vocal mode the shelf sits at 180 Hz to leave speech fundamentals clear.";
    id.secondaryHint      = "Midrange peak boost or cut. In Vocal mode centered at 2 kHz for speech intelligibility; in General mode at 1 kHz for warmth.";
    id.tertiaryHint       = "High shelf boost or cut. In Vocal mode the shelf sits at 5.6 kHz to avoid thinning consonants.";
    id.dspVersion         = vxsuite::versions::plugins::tone;
    id.helpTitle          = vxsuite::help::tone.title;
    id.helpHtml           = vxsuite::help::tone.html;
    id.readmeSection      = vxsuite::help::tone.readmeSection;
    id.primaryDefaultValue   = 0.5f;
    id.secondaryDefaultValue = 0.5f;
    id.tertiaryDefaultValue  = 0.5f;
    id.theme.accentRgb      = { 0.55f, 0.35f, 0.90f };
    id.theme.accent2Rgb     = { 0.09f, 0.06f, 0.12f };
    id.theme.backgroundRgb  = { 0.06f, 0.05f, 0.08f };
    id.theme.panelRgb       = { 0.10f, 0.09f, 0.13f };
    id.theme.textRgb        = { 0.92f, 0.88f, 0.98f };
    return id;
}

juce::String VXToneAudioProcessor::getStatusText() const {
    const bool isVoice = vxsuite::readMode(parameters, productIdentity) == vxsuite::Mode::vocal;
    return isVoice ? "Vocal - Bass/Treble shelves positioned to leave the 200 Hz-6 kHz speech band intact; Mid centered at 2 kHz for intelligibility"
                   : "General - full-range Bass (120 Hz) and Treble (8 kHz) shelves; Mid centered at 1 kHz for warmth";
}

void VXToneAudioProcessor::prepareSuite(const double sampleRate, const int /*samplesPerBlock*/) {
    currentSampleRateHz = sampleRate > 1000.0 ? sampleRate : 48000.0;
    const int ch = std::max(1, getTotalNumOutputChannels());
    bassState.assign(static_cast<size_t>(ch), BiquadState{});
    midState.assign(static_cast<size_t>(ch), BiquadState{});
    trebleState.assign(static_cast<size_t>(ch), BiquadState{});
    outputTrimmer.setCeiling(0.96f);
    outputTrimmer.setReleaseSeconds(0.16f);
    resetSuite();
}

void VXToneAudioProcessor::resetSuite() {
    for (auto& s : bassState)   s = BiquadState{};
    for (auto& s : midState)    s = BiquadState{};
    for (auto& s : trebleState) s = BiquadState{};
    outputTrimmer.reset();
    smoothedOutputTrimDb = 0.0f;
    const float bass   = vxsuite::readNormalized(parameters, productIdentity.primaryParamId,   0.5f);
    const float mid    = vxsuite::readNormalized(parameters, productIdentity.secondaryParamId, 0.5f);
    const float treble = vxsuite::readNormalized(parameters, productIdentity.tertiaryParamId,  0.5f);
    controls.reset(bass, mid, treble);
}

void VXToneAudioProcessor::processProduct(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    if (numSamples <= 0 || numChannels <= 0)
        return;

    const float bassTarget   = vxsuite::readNormalized(parameters, productIdentity.primaryParamId,   0.5f);
    const float midTarget    = vxsuite::readNormalized(parameters, productIdentity.secondaryParamId, 0.5f);
    const float trebleTarget = vxsuite::readNormalized(parameters, productIdentity.tertiaryParamId,  0.5f);

    const auto [smoothedBass, smoothedMid, smoothedTreble] = controls.process(
        bassTarget, midTarget, trebleTarget, currentSampleRateHz, numSamples, 0.060f, 0.060f, 0.060f);

    // 1. DETECT MODE (Framework Pattern)
    const bool voiceMode = vxsuite::readMode(parameters, productIdentity) == vxsuite::Mode::vocal;
    const auto& policy = currentModePolicy();
    const auto voiceContext = getVoiceContextSnapshot();

    // 2. CALCULATE VOCAL PRIORITY (Framework Pattern - Transient-aware)
    const float vocalPriority = voiceMode
        ? vxsuite::clamp01(0.40f * voiceContext.vocalDominance
                         + 0.30f * voiceContext.intelligibility
                         + 0.20f * voiceContext.transientRisk
                         + 0.10f * voiceContext.speechPresence)
        : 0.0f;

    // 3. BUILD ProcessOptions (Framework Pattern)
    vxsuite::ProcessOptions options {};
    options.isVoiceMode = voiceMode;
    options.sourceProtect = voiceMode
        ? vxsuite::clamp01(0.70f + 0.25f * vocalPriority)
        : vxsuite::clamp01(0.40f);
    options.guardStrictness = voiceMode
        ? vxsuite::clamp01(0.62f + 0.30f * vocalPriority)
        : vxsuite::clamp01(0.38f);
    options.speechFocus = voiceMode
        ? juce::jmax(0.80f, policy.speechFocus + 0.15f * vocalPriority)
        : juce::jmax(0.25f, policy.speechFocus);
    options.voiceProtect = voiceMode ? 0.88f : 0.55f;
    options.lateTailAggression = policy.lateTailAggression;

    // 4. EFFECT-SPECIFIC PARAMETERS
    const float maxGainDb = voiceMode
        ? kVocalMaxGainDb * (1.0f - 0.05f * vocalPriority)
        : kGeneralMaxGainDb;
    const float maxMidGainDb = voiceMode ? kVocalMidMaxGainDb : kGeneralMidMaxGainDb;

    const float bassControl = shapeToneControl(smoothedBass);
    const float midControl = shapeToneControl(smoothedMid);
    const float trebleControl = shapeToneControl(smoothedTreble);

    const float bassExcursion = bassControl - 0.5f;
    const float midExcursion = midControl - 0.5f;
    const float trebleExcursion = trebleControl - 0.5f;

    const float bassFreqHz = voiceMode
        ? juce::jlimit(120.0f, 210.0f, kVocalBassFreqHz - 28.0f * vocalPriority - 24.0f * bassExcursion)
        : juce::jlimit(72.0f, 125.0f, kGeneralBassFreqHz - 20.0f * bassExcursion);
    const float midFreqHz = voiceMode
        ? juce::jlimit(1200.0f, 3500.0f, kVocalMidFreqHz + 600.0f * vocalPriority + 300.0f * midExcursion)
        : juce::jlimit(600.0f, 1600.0f, kGeneralMidFreqHz - 200.0f * midExcursion);
    const float trebleFreqHz = voiceMode
        ? juce::jlimit(5000.0f, 6900.0f, kVocalTrebleFreqHz + 760.0f * vocalPriority + 420.0f * trebleExcursion)
        : juce::jlimit(8600.0f, 12000.0f, kGeneralTrebleFreqHz + 900.0f * trebleExcursion);

    // Map [0,1] → [-maxGainDb, +maxGainDb] with 0.5 = neutral
    const float bassGainDb   = bassExcursion   * 2.0f * maxGainDb;
    const float midGainDb    = midExcursion    * 2.0f * maxMidGainDb;
    const float trebleGainDb = trebleExcursion * 2.0f * maxGainDb;

    const float positiveBassDb = std::max(0.0f, bassGainDb);
    const float positiveMidDb = std::max(0.0f, midGainDb);
    const float positiveTrebleDb = std::max(0.0f, trebleGainDb);
    const float stackedBoostDb = positiveBassDb + positiveMidDb + positiveTrebleDb;
    const float maxPositiveDb = std::max({positiveBassDb, positiveMidDb, positiveTrebleDb});
    const float outputTrimTargetDb = -juce::jlimit(0.0f, voiceMode ? 6.0f : 7.0f,
        0.25f * stackedBoostDb + 0.15f * maxPositiveDb);

    const float midQ = voiceMode ? 0.85f : 0.70f;
    const BiquadCoeffs bassC   = lowShelfCoeffs (currentSampleRateHz, bassGainDb,   bassFreqHz);
    const BiquadCoeffs midC    = peakingEqCoeffs(currentSampleRateHz, midGainDb,    midFreqHz, midQ);
    const BiquadCoeffs trebleC = highShelfCoeffs(currentSampleRateHz, trebleGainDb, trebleFreqHz);

    smoothedOutputTrimDb = vxsuite::smoothBlockValue(smoothedOutputTrimDb,
                                                     outputTrimTargetDb,
                                                     currentSampleRateHz,
                                                     numSamples,
                                                     0.120f);
    const float outputTrim = juce::Decibels::decibelsToGain(smoothedOutputTrimDb);

    for (int ch = 0; ch < numChannels; ++ch) {
        if (ch >= static_cast<int>(bassState.size()))
            break;
        float* data = buffer.getWritePointer(ch);
        applyBiquad(data, numSamples, bassC,   bassState[static_cast<size_t>(ch)]);
        applyBiquad(data, numSamples, midC,    midState[static_cast<size_t>(ch)]);
        applyBiquad(data, numSamples, trebleC, trebleState[static_cast<size_t>(ch)]);
        if (std::abs(outputTrim - 1.0f) > 1.0e-4f)
            juce::FloatVectorOperations::multiply(data, outputTrim, numSamples);
    }

    outputTrimmer.process(buffer, currentSampleRateHz);
}

void VXToneAudioProcessor::renderListenOutput(juce::AudioBuffer<float>& outputBuffer,
                                              const juce::AudioBuffer<float>& inputBuffer) {
    renderAddedDeltaOutput(outputBuffer, inputBuffer);
}

// --- DSP helpers -----------------------------------------------------------

VXToneAudioProcessor::BiquadCoeffs
VXToneAudioProcessor::lowShelfCoeffs(const double sampleRate,
                                     const float  gainDb,
                                     const float  freqHz) noexcept {
    if (std::abs(gainDb) < 0.01f)
        return BiquadCoeffs{};  // passthrough

    const float A   = std::pow(10.f, gainDb / 40.f);
    const float w0  = 2.f * juce::MathConstants<float>::pi * freqHz / static_cast<float>(sampleRate);
    const float cw  = std::cos(w0);
    const float sw  = std::sin(w0);
    const float alpha = sw / std::sqrt(2.f);  // S=1: sin(w0) / sqrt(2)
    const float twoSqrtA = 2.f * std::sqrt(A);

    const float b0 =   A * ((A + 1.f) - (A - 1.f) * cw + twoSqrtA * alpha);
    const float b1 = 2.f * A * ((A - 1.f) - (A + 1.f) * cw);
    const float b2 =   A * ((A + 1.f) - (A - 1.f) * cw - twoSqrtA * alpha);
    const float a0 =        (A + 1.f) + (A - 1.f) * cw + twoSqrtA * alpha;
    const float a1 =  -2.f * ((A - 1.f) + (A + 1.f) * cw);
    const float a2 =         (A + 1.f) + (A - 1.f) * cw - twoSqrtA * alpha;

    const float inv = 1.f / a0;
    return { b0 * inv, b1 * inv, b2 * inv, a1 * inv, a2 * inv };
}

VXToneAudioProcessor::BiquadCoeffs
VXToneAudioProcessor::highShelfCoeffs(const double sampleRate,
                                      const float  gainDb,
                                      const float  freqHz) noexcept {
    if (std::abs(gainDb) < 0.01f)
        return BiquadCoeffs{};

    const float A   = std::pow(10.f, gainDb / 40.f);
    const float w0  = 2.f * juce::MathConstants<float>::pi * freqHz / static_cast<float>(sampleRate);
    const float cw  = std::cos(w0);
    const float sw  = std::sin(w0);
    const float alpha = sw / std::sqrt(2.f);
    const float twoSqrtA = 2.f * std::sqrt(A);

    const float b0 =   A * ((A + 1.f) + (A - 1.f) * cw + twoSqrtA * alpha);
    const float b1 = -2.f * A * ((A - 1.f) + (A + 1.f) * cw);
    const float b2 =   A * ((A + 1.f) + (A - 1.f) * cw - twoSqrtA * alpha);
    const float a0 =        (A + 1.f) - (A - 1.f) * cw + twoSqrtA * alpha;
    const float a1 =   2.f * ((A - 1.f) - (A + 1.f) * cw);
    const float a2 =         (A + 1.f) - (A - 1.f) * cw - twoSqrtA * alpha;

    const float inv = 1.f / a0;
    return { b0 * inv, b1 * inv, b2 * inv, a1 * inv, a2 * inv };
}

VXToneAudioProcessor::BiquadCoeffs
VXToneAudioProcessor::peakingEqCoeffs(const double sampleRate,
                                      const float  gainDb,
                                      const float  freqHz,
                                      const float  q) noexcept {
    if (std::abs(gainDb) < 0.01f)
        return BiquadCoeffs{};

    const float A   = std::pow(10.f, gainDb / 40.f);
    const float w0  = 2.f * juce::MathConstants<float>::pi * freqHz / static_cast<float>(sampleRate);
    const float cw  = std::cos(w0);
    const float sw  = std::sin(w0);
    const float alpha = sw / (2.f * q);
    const float twoSqrtA = 2.f * std::sqrt(A);

    const float b0 = 1.f + alpha * A;
    const float b1 = -2.f * cw;
    const float b2 = 1.f - alpha * A;
    const float a0 = 1.f + alpha / A;
    const float a1 = -2.f * cw;
    const float a2 = 1.f - alpha / A;

    const float inv = 1.f / a0;
    return { b0 * inv, b1 * inv, b2 * inv, a1 * inv, a2 * inv };
}

void VXToneAudioProcessor::applyBiquad(float* samples, const int numSamples,
                                       const BiquadCoeffs& c, BiquadState& s) noexcept {
    for (int i = 0; i < numSamples; ++i) {
        const float x = samples[i];
        const float y = c.b0 * x + c.b1 * s.x1 + c.b2 * s.x2
                                  - c.a1 * s.y1 - c.a2 * s.y2;
        s.x2 = s.x1;  s.x1 = x;
        s.y2 = s.y1;  s.y1 = y;
        samples[i] = y;
    }
}

#if !defined(VXSUITE_DISABLE_PLUGIN_ENTRYPOINT) && !defined(VXSTUDIO_DISABLE_PLUGIN_ENTRYPOINT)
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new VXToneAudioProcessor();
}
#endif
