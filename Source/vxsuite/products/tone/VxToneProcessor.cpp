#include "VxToneProcessor.h"

#include "vxsuite/framework/VxSuiteHelpContent.h"
#include "vxsuite/framework/VxSuiteBlockSmoothing.h"
#include "vxsuite/framework/VxSuiteParameters.h"
#include "VxSuiteVersions.h"

#include <cmath>

namespace {

constexpr std::string_view kProductName  = "Tone";
constexpr std::string_view kShortTag     = "TNE";
constexpr std::string_view kBassParam    = "bass";
constexpr std::string_view kTrebleParam  = "treble";
constexpr std::string_view kModeParam    = "mode";
constexpr std::string_view kListenParam  = "listen";

// Vocal mode: narrower shelves that leave the fundamental speech band (200-4kHz) untouched.
// General mode: full-range shelves with more headroom.
constexpr float kVocalBassFreqHz    = 200.f;
constexpr float kVocalTrebleFreqHz  = 6000.f;
constexpr float kVocalMaxGainDb     = 5.f;
constexpr float kGeneralBassFreqHz  = 120.f;
constexpr float kGeneralTrebleFreqHz = 8000.f;
constexpr float kGeneralMaxGainDb   = 6.f;

} // namespace

VXToneAudioProcessor::VXToneAudioProcessor()
    : ProcessorBase(makeIdentity()) {}

vxsuite::ProductIdentity VXToneAudioProcessor::makeIdentity() {
    vxsuite::ProductIdentity id {};
    id.productName        = kProductName;
    id.shortTag           = kShortTag;
    id.primaryParamId     = kBassParam;
    id.secondaryParamId   = kTrebleParam;
    id.modeParamId        = kModeParam;
    id.listenParamId      = kListenParam;
    id.defaultMode        = vxsuite::Mode::vocal;
    id.primaryLabel       = "Bass";
    id.secondaryLabel     = "Treble";
    id.primaryHint        = "Low shelf boost or cut. In Vocal mode the shelf sits at 200 Hz to leave speech fundamentals clear.";
    id.secondaryHint      = "High shelf boost or cut. In Vocal mode the shelf sits at 6 kHz to avoid thinning consonants.";
    id.dspVersion         = vxsuite::versions::plugins::tone;
    id.helpTitle          = vxsuite::help::tone.title;
    id.helpHtml           = vxsuite::help::tone.html;
    id.readmeSection      = vxsuite::help::tone.readmeSection;
    id.primaryDefaultValue   = 0.5f;
    id.secondaryDefaultValue = 0.5f;
    id.theme.accentRgb      = { 0.55f, 0.35f, 0.90f };
    id.theme.accent2Rgb     = { 0.09f, 0.06f, 0.12f };
    id.theme.backgroundRgb  = { 0.06f, 0.05f, 0.08f };
    id.theme.panelRgb       = { 0.10f, 0.09f, 0.13f };
    id.theme.textRgb        = { 0.92f, 0.88f, 0.98f };
    return id;
}

juce::String VXToneAudioProcessor::getStatusText() const {
    const bool isVoice = vxsuite::readMode(parameters, productIdentity) == vxsuite::Mode::vocal;
    return isVoice ? "Vocal - shelves positioned to leave the 200 Hz-6 kHz speech band intact"
                   : "General - full-range bass (120 Hz) and treble (8 kHz) shelves";
}

void VXToneAudioProcessor::prepareSuite(const double sampleRate, const int /*samplesPerBlock*/) {
    currentSampleRateHz = sampleRate > 1000.0 ? sampleRate : 48000.0;
    const int ch = std::max(1, getTotalNumOutputChannels());
    bassState.assign(static_cast<size_t>(ch), BiquadState{});
    trebleState.assign(static_cast<size_t>(ch), BiquadState{});
    resetSuite();
}

void VXToneAudioProcessor::resetSuite() {
    for (auto& s : bassState)   s = BiquadState{};
    for (auto& s : trebleState) s = BiquadState{};
    controls.reset(0.5f, 0.5f);
}

void VXToneAudioProcessor::processProduct(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    if (numSamples <= 0 || numChannels <= 0)
        return;

    const float bassTarget   = vxsuite::readNormalized(parameters, productIdentity.primaryParamId,   0.5f);
    const float trebleTarget = vxsuite::readNormalized(parameters, productIdentity.secondaryParamId, 0.5f);

    const auto [smoothedBass, smoothedTreble] = controls.process(
        bassTarget, trebleTarget, currentSampleRateHz, numSamples, 0.060f, 0.060f);

    const bool voiceMode = vxsuite::readMode(parameters, productIdentity) == vxsuite::Mode::vocal;
    const auto voiceContext = getVoiceContextSnapshot();
    const float vocalPriority = voiceMode
        ? vxsuite::clamp01(0.40f * voiceContext.vocalDominance
                         + 0.30f * voiceContext.intelligibility
                         + 0.20f * voiceContext.transientRisk
                         + 0.10f * voiceContext.speechPresence)
        : 0.0f;
    const float maxGainDb = voiceMode
        ? kVocalMaxGainDb * (1.0f - 0.18f * vocalPriority)
        : kGeneralMaxGainDb;
    const float bassFreqHz = voiceMode
        ? juce::jlimit(150.0f, 220.0f, kVocalBassFreqHz - 40.0f * vocalPriority)
        : kGeneralBassFreqHz;
    const float trebleFreqHz = voiceMode
        ? juce::jlimit(6000.0f, 7200.0f, kVocalTrebleFreqHz + 900.0f * vocalPriority)
        : kGeneralTrebleFreqHz;

    // Map [0,1] → [-maxGainDb, +maxGainDb] with 0.5 = neutral
    const float bassGainDb   = (smoothedBass   - 0.5f) * 2.0f * maxGainDb;
    const float trebleGainDb = (smoothedTreble - 0.5f) * 2.0f * maxGainDb;

    const BiquadCoeffs bassC   = lowShelfCoeffs (currentSampleRateHz, bassGainDb,   bassFreqHz);
    const BiquadCoeffs trebleC = highShelfCoeffs(currentSampleRateHz, trebleGainDb, trebleFreqHz);

    for (int ch = 0; ch < numChannels; ++ch) {
        if (ch >= static_cast<int>(bassState.size()))
            break;
        float* data = buffer.getWritePointer(ch);
        applyBiquad(data, numSamples, bassC,   bassState[static_cast<size_t>(ch)]);
        applyBiquad(data, numSamples, trebleC, trebleState[static_cast<size_t>(ch)]);
    }
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
    const float al  = sw * 0.5f * std::sqrt((A + 1.f / A) * (1.f / 1.f - 1.f) + 2.f); // S=1

    // Simpler form for S=1: alpha = sin(w0)/2 * sqrt(2) = sin(w0) / sqrt(2)
    const float alpha = sw / std::sqrt(2.f);
    const float twoSqrtA = 2.f * std::sqrt(A);
    const float juce_unused = al; (void)juce_unused;

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

#if !defined(VXSUITE_DISABLE_PLUGIN_ENTRYPOINT)
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new VXToneAudioProcessor();
}
#endif
