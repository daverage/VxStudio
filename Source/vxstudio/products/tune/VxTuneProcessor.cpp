#include "VxTuneProcessor.h"

#include "vxstudio/framework/VxStudioHelpContent.h"
#include "vxstudio/framework/VxStudioParameters.h"
#include "VxStudioVersions.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace {

constexpr std::string_view kProductName  = "VX Studio Tune";
constexpr std::string_view kShortTag     = "TUN";
constexpr std::string_view kAmountParam  = "amount";
constexpr std::string_view kNaturalParam = "natural";
constexpr std::string_view kListenParam  = "listen";
constexpr std::string_view kKeyScaleParam = "keyscale";

const char* noteNameForMidi(const int midi) noexcept {
    static const char* names[12] = { "C", "C#", "D", "D#", "E", "F",
                                     "F#", "G", "G#", "A", "A#", "B" };
    return names[((midi % 12) + 12) % 12];
}

// Key/Scale choices: 0 = Chromatic, 1..12 = C..B Major, 13..24 = C..B Minor.
constexpr std::array<std::string_view, 25> kKeyScaleLabels = {
    "Chromatic",
    "C Major", "C# Major", "D Major", "D# Major", "E Major", "F Major",
    "F# Major", "G Major", "G# Major", "A Major", "A# Major", "B Major",
    "C Minor", "C# Minor", "D Minor", "D# Minor", "E Minor", "F Minor",
    "F# Minor", "G Minor", "G# Minor", "A Minor", "A# Minor", "B Minor",
};

std::uint16_t scaleMaskForChoice(const int choice) noexcept {
    if (choice <= 0 || choice > 24)
        return vxsuite::tune::CorrectionEngine::kChromaticMask;
    const bool minor = choice > 12;
    const int root = (choice - 1) % 12;
    constexpr int majorSteps[7] = { 0, 2, 4, 5, 7, 9, 11 };
    constexpr int minorSteps[7] = { 0, 2, 3, 5, 7, 8, 10 };
    std::uint16_t mask = 0;
    for (const int step : (minor ? minorSteps : majorSteps))
        mask = static_cast<std::uint16_t>(mask | (1u << ((root + step) % 12)));
    return mask;
}

} // namespace

static juce::AudioProcessorValueTreeState::ParameterLayout createTuneParameterLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "amount", 1 },
        "Amount",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f },
        0.6f,
        vxsuite::makePercentFloatAttributes()));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "natural", 1 },
        "Natural",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f },
        0.35f,
        vxsuite::makePercentFloatAttributes()));

    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { "listen", 1 },
        "Listen",
        false,
        vxsuite::makeListenAttributes()));

    juce::StringArray keyScaleChoices;
    for (const auto label : kKeyScaleLabels)
        keyScaleChoices.add(vxsuite::toJuceString(label));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { "keyscale", 1 },
        "Key",
        keyScaleChoices,
        0,
        vxsuite::makeChoiceAttributes("Key")));

    return layout;
}

VXTuneAudioProcessor::VXTuneAudioProcessor()
    : ProcessorBase(makeIdentity(), createTuneParameterLayout()) {}

vxsuite::ProductIdentity VXTuneAudioProcessor::makeIdentity() {
    vxsuite::ProductIdentity id {};
    id.productName        = kProductName;
    id.shortTag           = kShortTag;
    id.primaryParamId     = kAmountParam;
    id.secondaryParamId   = kNaturalParam;
    id.listenParamId      = kListenParam;   // shared delta audition: wet - aligned dry
    id.showPitchTrace     = true;
    id.auxSelectorParamId = kKeyScaleParam;
    id.auxSelectorLabel   = "Key";
    id.auxSelectorFollowsGeneralMode = false;   // no mode switch on this product
    for (size_t i = 0; i < kKeyScaleLabels.size(); ++i)
        id.auxSelectorChoiceLabels[i] = kKeyScaleLabels[i];
    id.primaryLabel       = "Amount";
    id.secondaryLabel     = "Natural";
    id.primaryHint        = "How much detected pitch error is removed.";
    id.secondaryHint      = "How readily movement counts as error rather than expression. Left: preserve everything human. Right: tighter.";
    id.dspVersion         = vxsuite::versions::plugins::tune;
    id.helpTitle          = vxsuite::help::tune.title;
    id.helpHtml           = vxsuite::help::tune.html;
    id.readmeSection      = vxsuite::help::tune.readmeSection;
    id.primaryDefaultValue   = 0.6f;
    id.secondaryDefaultValue = 0.35f;
    id.theme.accentRgb      = { 0.20f, 0.85f, 0.65f };
    id.theme.accent2Rgb     = { 0.05f, 0.11f, 0.09f };
    id.theme.backgroundRgb  = { 0.04f, 0.07f, 0.06f };
    id.theme.panelRgb       = { 0.08f, 0.12f, 0.10f };
    id.theme.textRgb        = { 0.88f, 0.97f, 0.93f };
    return id;
}

juce::String VXTuneAudioProcessor::getStatusText() const {
    const float f0 = lastF0Hz.load(std::memory_order_relaxed);
    if (f0 <= 0.0f)
        return "Listening - no pitched voice detected";

    const float conf = lastConfidence.load(std::memory_order_relaxed);
    const float centreCents = lastCentreCents.load(std::memory_order_relaxed);
    const float correction = lastCorrectionCents.load(std::memory_order_relaxed);
    // Nearest note from the centre line (A440 = MIDI 69).
    const int midi = 69 + static_cast<int>(std::lround(centreCents / 100.0f));
    const int octave = midi / 12 - 1;
    const float offsetCents = centreCents - 100.0f * static_cast<float>(midi - 69);

    juce::String text = juce::String(noteNameForMidi(midi)) + juce::String(octave)
        + juce::String::formatted(" %+.0fc  conf %.0f%%", offsetCents, conf * 100.0f);
    text += std::abs(correction) >= 1.0f
        ? juce::String::formatted("  correcting %+.0fc", correction)
        : juce::String("  not intervening");
    return text;
}

void VXTuneAudioProcessor::prepareSuite(const double sampleRate, const int samplesPerBlock) {
    vxsuite::tune::PitchDetector::Config detectorConfig;
    detector.prepare(sampleRate, detectorConfig);

    const double frameRate = sampleRate / detector.hopSamples();
    decomposition.prepare(frameRate, vxsuite::tune::PerformanceDecomposition::Config {});
    correctionEngine.prepare(frameRate, vxsuite::tune::CorrectionEngine::Config {});

    const int block = std::max(64, samplesPerBlock);
    shifter.prepare(sampleRate, block, 2);
    setReportedLatencySamples(shifter.latencySamples());

    monoScratch.assign(static_cast<size_t>(block), 0.0f);
    resetSuite();
}

void VXTuneAudioProcessor::resetSuite() {
    detector.reset();
    decomposition.reset();
    correctionEngine.reset();
    shifter.reset();
    lastCorrectionCents.store(0.0f, std::memory_order_relaxed);
    lastF0Hz.store(0.0f, std::memory_order_relaxed);
    lastConfidence.store(0.0f, std::memory_order_relaxed);
    lastCentreCents.store(0.0f, std::memory_order_relaxed);
    lastResidualCents.store(0.0f, std::memory_order_relaxed);
    lastReason.store(0, std::memory_order_relaxed);
}

void VXTuneAudioProcessor::processProduct(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    const int numChannels = std::min(buffer.getNumChannels(), 2);
    const int totalSamples = buffer.getNumSamples();
    if (numChannels <= 0 || totalSamples <= 0)
        return;

    const float amount = vxsuite::readNormalized(parameters, productIdentity.primaryParamId, 0.6f);
    const float natural = vxsuite::readNormalized(parameters, productIdentity.secondaryParamId, 0.35f);
    const auto* keyScaleRaw = parameters.getRawParameterValue(kKeyScaleParam.data());
    const std::uint16_t scaleMask = scaleMaskForChoice(
        keyScaleRaw != nullptr ? static_cast<int>(keyScaleRaw->load() + 0.5f) : 0);

    // Chunk so oversized host blocks never outgrow the preallocated scratch.
    const int capacity = static_cast<int>(monoScratch.size());
    for (int offset = 0; offset < totalSamples; offset += capacity) {
        const int n = std::min(capacity, totalSamples - offset);

        // Analyse the mono mix of the (pre-shift) input.
        const float* left = buffer.getReadPointer(0) + offset;
        if (numChannels == 1) {
            std::copy(left, left + n, monoScratch.begin());
        } else {
            const float* right = buffer.getReadPointer(1) + offset;
            for (int i = 0; i < n; ++i)
                monoScratch[static_cast<size_t>(i)] = 0.5f * (left[i] + right[i]);
        }

        const int produced = detector.process(
            monoScratch.data(), n,
            observationScratch.data(), static_cast<int>(observationScratch.size()));

        for (int i = 0; i < produced; ++i) {
            const auto& observation = observationScratch[static_cast<size_t>(i)];
            const auto frame = decomposition.process(observation);
            // Veto rendering hints that disagree wildly with the intent
            // line (octave-error frames): one bad period must not scatter
            // the shifter's epoch grid. Legitimate note jumps re-anchor the
            // centre within a frame, so the veto is momentary.
            const bool octaveSuspect =
                frame.centreHz > 0.0f && std::abs(frame.residualCents) > 350.0f;
            shifter.setPeriodHint(
                (frame.f0Hz.value > 0.0f && !octaveSuspect)
                    ? static_cast<float>(detector.sampleRate()) / frame.f0Hz.value
                    : 0.0f,
                octaveSuspect ? 0.0f : frame.f0Hz.confidence);
            const float correction = correctionEngine.process(frame, amount, natural, scaleMask);

            lastF0Hz.store(frame.f0Hz.value, std::memory_order_relaxed);
            lastConfidence.store(frame.f0Hz.confidence, std::memory_order_relaxed);
            lastCentreCents.store(frame.centreCents, std::memory_order_relaxed);
            lastResidualCents.store(frame.residualCents, std::memory_order_relaxed);
            lastReason.store(static_cast<int>(frame.f0Hz.reason), std::memory_order_relaxed);
            lastCorrectionCents.store(correction, std::memory_order_relaxed);
        }

        // Render: one grain schedule applied to every channel keeps the
        // stereo image intact; sustained zero correction is a pure delay.
        shifter.setShiftCents(correctionEngine.currentCorrectionCents());
        float* chans[2] = { buffer.getWritePointer(0) + offset,
                            numChannels > 1 ? buffer.getWritePointer(1) + offset : nullptr };
        shifter.process(chans, numChannels, n);
    }
}

vxsuite::tune::PitchFrame VXTuneAudioProcessor::latestFrameForTests() const noexcept {
    vxsuite::tune::PitchFrame frame;
    frame.f0Hz.value = lastF0Hz.load(std::memory_order_relaxed);
    frame.f0Hz.confidence = lastConfidence.load(std::memory_order_relaxed);
    frame.f0Hz.reason = static_cast<vxsuite::tune::EstimateReason>(
        lastReason.load(std::memory_order_relaxed));
    frame.centreCents = lastCentreCents.load(std::memory_order_relaxed);
    frame.residualCents = lastResidualCents.load(std::memory_order_relaxed);
    return frame;
}

#if !defined(VXSTUDIO_DISABLE_PLUGIN_ENTRYPOINT)
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new VXTuneAudioProcessor();
}
#endif
