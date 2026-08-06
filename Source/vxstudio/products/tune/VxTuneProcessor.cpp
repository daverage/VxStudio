#include "VxTuneProcessor.h"

#include "vxstudio/framework/VxStudioHelpContent.h"
#include "vxstudio/framework/VxStudioParameters.h"
#include "VxStudioVersions.h"

#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <fstream>
#include <memory>

namespace {

constexpr std::string_view kProductName  = "VX Studio Tune";
constexpr std::string_view kShortTag     = "TUN";
constexpr std::string_view kAmountParam  = "amount";
constexpr std::string_view kNaturalParam = "natural";
constexpr std::string_view kSpeedParam   = "speed";
constexpr std::string_view kFocusParam   = "focus";
constexpr std::string_view kListenParam  = "listen";
constexpr std::string_view kKeyRootParam = "keyroot";
constexpr std::string_view kScaleParam   = "scale";

const char* noteNameForMidi(const int midi) noexcept {
    static const char* names[12] = { "C", "C#", "D", "D#", "E", "F",
                                     "F#", "G", "G#", "A", "A#", "B" };
    return names[((midi % 12) + 12) % 12];
}

constexpr int kIgnoreKeyChoice = 13;

// Pearson correlation against a Krumhansl-Schmuckler profile (see
// VxTuneKeyProfiles.h) is bounded to [-1, 1] and, unlike the old
// score-margin ratio, is directly usable as a confidence value. This
// threshold is a reasoned starting point (real correlations on vocal-only
// material, after ornamental down-weighting, run lower than a full-mix
// chromagram would) - validated against a known-key vocal take, not yet
// against a broad corpus.
constexpr float kAutoKeyConfidenceGate = 0.15f;

// Keep Auto at index 0 so existing sessions retain their meaning. Ignore is
// appended as a new choice and deliberately forces chromatic correction.
constexpr std::array<std::string_view, 14> kKeyRootLabels = {
    "Auto", "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B", "Ignore",
};

constexpr std::array<std::string_view, 6> kScaleLabels = {
    "Auto", "Chromatic", "Major", "Natural Minor", "Harmonic Minor", "Melodic Minor",
};

std::uint16_t maskForRootAndScale(const int root, const int scaleChoice) noexcept {
    if (root < 0 || root > 11 || scaleChoice <= 1)
        return vxsuite::tune::CorrectionEngine::kChromaticMask;
    constexpr int majorSteps[7] = { 0, 2, 4, 5, 7, 9, 11 };
    constexpr int naturalMinorSteps[7] = { 0, 2, 3, 5, 7, 8, 10 };
    constexpr int harmonicMinorSteps[7] = { 0, 2, 3, 5, 7, 8, 11 };
    constexpr int melodicMinorSteps[7] = { 0, 2, 3, 5, 7, 9, 11 };
    const int* steps = majorSteps;
    if (scaleChoice == 3)
        steps = naturalMinorSteps;
    else if (scaleChoice == 4)
        steps = harmonicMinorSteps;
    else if (scaleChoice == 5)
        steps = melodicMinorSteps;
    std::uint16_t mask = 0;
    for (int i = 0; i < 7; ++i) {
        const int step = steps[i];
        mask = static_cast<std::uint16_t>(mask | (1u << ((root + step) % 12)));
    }
    return mask;
}

int midiPitchClassFromCents(const float cents) noexcept {
    const int midi = 69 + static_cast<int>(std::lround(cents / 100.0f));
    return ((midi % 12) + 12) % 12;
}

std::string_view keyRootLabel(const int root) noexcept {
    const int index = std::clamp(root + 1, 1, 12);
    return kKeyRootLabels[static_cast<size_t>(index)];
}

std::string_view scaleLabel(const int scaleChoice) noexcept {
    const int index = std::clamp(scaleChoice, 0, static_cast<int>(kScaleLabels.size()) - 1);
    return kScaleLabels[static_cast<size_t>(index)];
}

} // namespace

static juce::AudioProcessorValueTreeState::ParameterLayout createTuneParameterLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "amount", 1 },
        "Amount",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f },
        0.5f,
        vxsuite::makePercentFloatAttributes()));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "natural", 1 },
        "Natural",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f },
        0.65f,
        vxsuite::makePercentFloatAttributes()));

    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { "listen", 1 },
        "Listen",
        false,
        vxsuite::makeListenAttributes()));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "speed", 1 },
        "Speed",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f },
        0.3f,
        vxsuite::makePercentFloatAttributes()));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "focus", 1 },
        "Focus",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f },
        0.6f,
        vxsuite::makePercentFloatAttributes()));

    juce::StringArray keyChoices;
    for (const auto label : kKeyRootLabels)
        keyChoices.add(vxsuite::toJuceString(label));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { kKeyRootParam.data(), 1 },
        "Key",
        keyChoices,
        0,
        vxsuite::makeChoiceAttributes("Key")));

    juce::StringArray scaleChoices;
    for (const auto label : kScaleLabels)
        scaleChoices.add(vxsuite::toJuceString(label));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { kScaleParam.data(), 1 },
        "Scale",
        scaleChoices,
        0,
        vxsuite::makeChoiceAttributes("Scale")));

    return layout;
}

VXTuneAudioProcessor::VXTuneAudioProcessor()
    : ProcessorBase(makeIdentity(), createTuneParameterLayout()),
      pitchRenderer(vxsuite::tune::createPitchRenderer(
          vxsuite::tune::VxTunePitchRenderer::Backend::Signalsmith)) {}

VXTuneAudioProcessor::~VXTuneAudioProcessor() {
    flushDebugTrace();
}

vxsuite::ProductIdentity VXTuneAudioProcessor::makeIdentity() {
    vxsuite::ProductIdentity id {};
    id.productName        = kProductName;
    id.shortTag           = kShortTag;
    id.primaryParamId     = kAmountParam;
    id.secondaryParamId   = kNaturalParam;
    id.tertiaryParamId    = kSpeedParam;
    id.quaternaryParamId  = kFocusParam;
    id.listenParamId      = kListenParam;   // shared delta audition: wet - aligned dry
    id.showPitchTrace     = true;
    // Optional: an instrumental sidechain feeds full-chord harmonic
    // evidence into auto Key/Scale (see updateAutoKeyFromSidechain()).
    // Detection-only, never routed to output; the framework's own "SC"
    // badge shows when a connected bus has real signal.
    id.wantsSidechainInput = true;
    id.auxSelectorParamId = kKeyRootParam;
    id.auxSelectorLabel   = "Key";
    id.auxSelectorFollowsGeneralMode = false;   // no mode switch on this product
    for (size_t i = 0; i < kKeyRootLabels.size(); ++i)
        id.auxSelectorChoiceLabels[i] = kKeyRootLabels[i];
    id.auxSelector2ParamId = kScaleParam;
    id.auxSelector2Label   = "Scale";
    id.auxSelector2FollowsGeneralMode = false;
    for (size_t i = 0; i < kScaleLabels.size(); ++i)
        id.auxSelector2ChoiceLabels[i] = kScaleLabels[i];
    id.presetSelectorLabel = "Preset";
    id.presetChoiceCount = 4;
    id.presetChoiceLabels = {
        "Natural",
        "Balanced",
        "Tight",
        "Hard Tune",
        "",
        "",
    };
    id.presetPrimaryValues = {
        0.325f,
        0.50f,
        0.50f,
        1.00f,
        0.0f,
        0.0f,
    };
    id.presetSecondaryValues = {
        0.25f,
        0.65f,
        0.85f,
        1.00f,
        0.0f,
        0.0f,
    };
    id.presetTertiaryValues = {
        0.50f,
        0.30f,
        0.50f,
        1.00f,
        0.0f,
        0.0f,
    };
    id.presetQuaternaryValues = {
        0.50f,
        0.60f,
        0.50f,
        1.00f,
        0.0f,
        0.0f,
    };
    id.presetAuxSelectorIndexes = {
        0,
        0,
        0,
        0,
        -1,
        -1,
    };
    id.presetAuxSelector2Indexes = {
        0,
        0,
        0,
        0,
        -1,
        -1,
    };
    id.primaryLabel       = "Amount";
    id.secondaryLabel     = "Natural";
    id.tertiaryLabel      = "Speed";
    id.quaternaryLabel    = "Focus";
    id.primaryHint        = "How much detected pitch error is removed.";
    id.secondaryHint      = "How readily movement counts as error rather than expression. Left: preserve everything human. Right: tighter.";
    id.tertiaryHint       = "Correction timing. Left: slower, ReaTune-style easing. Right: faster note-centre movement.";
    id.quaternaryHint     = "How assertively stable targets override the musical safety gates.";
    id.dspVersion         = vxsuite::versions::plugins::tune;
    id.helpTitle          = vxsuite::help::tune.title;
    id.helpHtml           = vxsuite::help::tune.html;
    id.readmeSection      = vxsuite::help::tune.readmeSection;
    id.primaryDefaultValue   = 0.5f;
    id.secondaryDefaultValue = 0.65f;
    id.tertiaryDefaultValue = 0.3f;
    id.quaternaryDefaultValue = 0.6f;
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
        return "Listening - no pitched voice detected" + autoKeyStatusText();

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
    text += autoKeyStatusText();
    return text;
}

juce::String VXTuneAudioProcessor::autoKeyStatusText() const {
    const int keyChoice = vxsuite::readChoiceIndex(parameters, kKeyRootParam, 0);
    const int scaleChoice = vxsuite::readChoiceIndex(parameters, kScaleParam, 0);
    if (keyChoice == kIgnoreKeyChoice && scaleChoice == 1)
        return "  key ignored (chromatic)";
    if (keyChoice == kIgnoreKeyChoice)
        return "  key auto root " + vxsuite::toJuceString(scaleLabel(scaleChoice));
    const bool autoKey = keyChoice <= 0;
    const bool autoScale = scaleChoice <= 0;
    if (!autoKey && !autoScale)
        return {};

    const int frames = lastAutoKeyFrames.load(std::memory_order_relaxed);
    const float confidence = lastAutoKeyConfidence.load(std::memory_order_relaxed);
    if (frames < autoKeyMinFrames || confidence < kAutoKeyConfidenceGate)
        return "  key learning";

    const int root = autoKey
        ? lastAutoKeyRoot.load(std::memory_order_relaxed)
        : std::clamp(keyChoice - 1, 0, 11);
    const int scale = autoScale
        ? lastAutoKeyScale.load(std::memory_order_relaxed)
        : scaleChoice;

    return "  key " + vxsuite::toJuceString(keyRootLabel(root))
        + " " + vxsuite::toJuceString(scaleLabel(scale))
        + juce::String::formatted(" %.0f%%", confidence * 100.0f);
}

void VXTuneAudioProcessor::prepareSuite(const double sampleRate, const int samplesPerBlock) {
    // Flush whatever the previous configuration collected before
    // configureDebugTrace() clears the row buffer. This is the only place
    // the (blocking) flush is safe to run: prepareToPlay is always called
    // from the message thread, whereas resetSuite() can also be reached
    // from AudioProcessor::reset(), which some hosts call on the audio
    // thread (e.g. transport-loop restarts).
    flushDebugTrace();
    configureDebugTrace(sampleRate);

    vxsuite::tune::PitchDetector::Config detectorConfig;
    detector.prepare(sampleRate, detectorConfig);

    const double frameRate = sampleRate / detector.hopSamples();
    decomposition.prepare(frameRate, vxsuite::tune::PerformanceDecomposition::Config {});
    correctionEngine.prepare(frameRate, vxsuite::tune::CorrectionEngine::Config {});

    // Auto-key memory/commit-gate in real time, not frame count: the hop
    // size is fixed in samples, so frame rate (and an uncorrected per-frame
    // decay/count) would otherwise scale with sample rate.
    constexpr float kAutoKeyHalfLifeSeconds = 45.0f;
    constexpr float kAutoKeyMinSeconds = 2.0f;
    autoKeyDecayPerFrame = std::pow(0.5f,
        1.0f / std::max(1.0f, static_cast<float>(frameRate) * kAutoKeyHalfLifeSeconds));
    autoKeyMinFrames = std::max(16,
        static_cast<int>(std::lround(frameRate * kAutoKeyMinSeconds)));

    const int block = std::max(64, samplesPerBlock);
    vxsuite::tune::VxTunePitchRenderer::PrepareSpec rendererSpec;
    rendererSpec.sampleRate = sampleRate;
    rendererSpec.maximumBlockSize = block;
    rendererSpec.numChannels = 2;
    // Live keeps round-trip latency low enough to monitor through while
    // tracking (~30-45ms vs Studio's ~150ms); there is no user-facing
    // control to choose Studio's higher-latency/higher-quality STFT window,
    // so default to the profile this product is actually meant for.
    rendererSpec.qualityProfile = vxsuite::tune::VxTunePitchRenderer::QualityProfile::Live;
    pitchRenderer->prepare(rendererSpec);
    setReportedLatencySamples(pitchRenderer->latencySamples());

    monoScratch.assign(static_cast<size_t>(block), 0.0f);
    sidechainMonoScratch.assign(static_cast<size_t>(block), 0.0f);
    harmonicContext.prepare(sampleRate, block);
    resetAutoKeyState();
    resetSuite();
}

// The learned key/scale is session-level knowledge about the song, built up
// over many seconds - it should survive transport stops, rewinds, and loop
// wraparounds, not be wiped by them. resetSuite() runs on every one of those
// (via AudioProcessor::reset()/releaseResources()), so this only runs from
// prepareSuite(): a genuine reconfiguration (first load or a sample-rate/
// block-size change), never a mid-session transport event. Getting this
// wrong meant the histogram never accumulated the ~2s of continuous
// evidence needed to clear the confidence gate in any DAW that resets on
// loop (which is most of them) - it looked fine offline only because batch
// rendering never calls reset() mid-file.
void VXTuneAudioProcessor::resetAutoKeyState() {
    autoKeyHistogram.fill(0.0f);
    autoKeyFrameCount = 0;
    autoDetectedRoot = 0;
    autoDetectedScale = 2;
    autoKeyConfidence = 0.0f;
    autoKeyIsMajor = true;
    autoModeForFixedRootHeldRoot = -1;
    autoModeForFixedRootIsMajor = true;
    autoRootForFixedModeHeldIsMajor = true;
    autoRootForFixedMode = 0;
    lastAutoKeyRoot.store(autoDetectedRoot, std::memory_order_relaxed);
    lastAutoKeyScale.store(autoDetectedScale, std::memory_order_relaxed);
    lastAutoKeyConfidence.store(autoKeyConfidence, std::memory_order_relaxed);
    lastAutoKeyFrames.store(autoKeyFrameCount, std::memory_order_relaxed);
}

void VXTuneAudioProcessor::resetSuite() {
    detector.reset();
    decomposition.reset();
    correctionEngine.reset();
    pitchRenderer->reset();
    harmonicContext.reset();
    sidechainActive = false;
    renderTargetCents = 0.0f;
    renderProcessCents = 0.0f;
    renderGateMisses = 0;
    appliedScaleMask = vxsuite::tune::CorrectionEngine::kChromaticMask;
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
    const float speed = vxsuite::readNormalized(parameters, productIdentity.tertiaryParamId, 0.5f);
    const float focus = vxsuite::readNormalized(parameters, productIdentity.quaternaryParamId, 0.5f);
    const int keyChoice = vxsuite::readChoiceIndex(parameters, kKeyRootParam, 0);
    const int scaleChoice = vxsuite::readChoiceIndex(parameters, kScaleParam, 0);

    if (const auto* sc = getSidechainBuffer();
        sc != nullptr && sc->getNumChannels() > 0 && sc->getNumSamples() > 0) {
        sidechainActive = true;
        const int scSamples = sc->getNumSamples();
        if (static_cast<int>(sidechainMonoScratch.size()) < scSamples)
            sidechainMonoScratch.assign(static_cast<size_t>(scSamples), 0.0f);
        const float* scLeft = sc->getReadPointer(0);
        if (sc->getNumChannels() > 1) {
            const float* scRight = sc->getReadPointer(1);
            for (int i = 0; i < scSamples; ++i)
                sidechainMonoScratch[static_cast<size_t>(i)] = 0.5f * (scLeft[i] + scRight[i]);
        } else {
            std::copy(scLeft, scLeft + scSamples, sidechainMonoScratch.begin());
        }
        harmonicContext.process(sidechainMonoScratch.data(), scSamples);
        updateAutoKeyFromSidechain();
    } else {
        sidechainActive = false;
    }

    // Keep analysis and rendering marching together. Offline hosts may hand us
    // very large blocks; analysing the whole block before rendering would make
    // the shifter use the final correction value for earlier audio.
    const int capacity = static_cast<int>(monoScratch.size());
    const int renderQuantum = std::max(1, std::min(capacity, detector.hopSamples()));
    for (int offset = 0; offset < totalSamples; offset += renderQuantum) {
        const int n = std::min(renderQuantum, totalSamples - offset);

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
            const std::uint16_t rawScaleMask = currentScaleMask(keyChoice, scaleChoice);
            // Veto rendering hints that disagree wildly with the intent
            // line (octave-error frames): one bad period must not scatter
            // the shifter's epoch grid. Legitimate note jumps re-anchor the
            // centre within a frame, so the veto is momentary.
            const bool octaveSuspect =
                frame.centreHz > 0.0f && std::abs(frame.residualCents) > 350.0f;
            // Only let a changed scale mask take effect between notes
            // (unvoiced or octave-suspect frames): TargetEstimator holds its
            // target as accumulated evidence, so narrowing/widening the
            // valid note set mid-sustain would make the currently-held
            // target stop being reinforced while an alternative starts
            // accumulating - a retarget driven by the key updating, not by
            // the singer's pitch actually moving.
            //
            // Real bug found on real material: auto-key's major/minor mode
            // can flip mid-note (the mode tie-break settling after the note
            // already started) right after a boundary froze the OLD mode's
            // mask, which can exclude the pitch class the singer is
            // currently on for the rest of that note - forcing correction
            // onto the nearest note the stale mask still allows, a full
            // semitone off, for as long as the note lasts. Unlike a general
            // mid-note narrowing/widening (unsafe, per above), specifically
            // ADDING BACK the pitch class already being sung is safe: it
            // cannot destabilise whatever TargetEstimator is currently
            // reinforcing, it only lets the correct candidate start
            // accumulating evidence again instead of staying locked out for
            // the rest of the note.
            const bool currentPitchClassNewlyAllowed = frame.centreHz > 0.0f
                && (appliedScaleMask & (1u << midiPitchClassFromCents(frame.centreCents))) == 0
                && (rawScaleMask & (1u << midiPitchClassFromCents(frame.centreCents))) != 0;
            if (frame.f0Hz.value <= 0.0f || octaveSuspect || currentPitchClassNewlyAllowed)
                appliedScaleMask = rawScaleMask;
            const float correction =
                correctionEngine.process(frame, amount, natural, appliedScaleMask, speed, focus);
            // Runs after correctionEngine.process() so the segmenter (owned
            // by CorrectionEngine, independent of the key/scale mask) has
            // already updated for this frame - auto-key voting can then use
            // its behaviour probabilities to down-weight ornamental motion.
            updateAutoKeyEstimate(frame, correctionEngine.currentSegment());
            vxsuite::tune::VxTunePitchRenderer::FrameInfo renderInfo;
            renderInfo.correctionCents = correction;
            renderInfo.detectedF0Hz = frame.f0Hz.value;
            renderInfo.pitchConfidence = octaveSuspect ? 0.0f : frame.f0Hz.confidence;
            renderInfo.voiced = frame.f0Hz.value > 0.0f && !octaveSuspect;
            pitchRenderer->setFrameInfo(renderInfo);

            const float hardTune = std::max(speed, focus);
            const float hardRender = hardTune > 0.5f
                ? std::sqrt((hardTune - 0.5f) * 2.0f)
                : 0.0f;
            const float renderMinConfidence = 0.6f + (0.25f - 0.6f) * hardRender;
            const float renderMinDecompConfidence = 0.55f + (0.20f - 0.55f) * hardRender;
            const bool renderReasonAllowed =
                frame.f0Hz.reason == vxsuite::tune::EstimateReason::nominal
                || hardTune >= 0.80f;
            const bool renderCorrectionAllowed =
                renderReasonAllowed
                && frame.f0Hz.confidence >= renderMinConfidence
                && frame.decompConfidence >= renderMinDecompConfidence
                && !octaveSuspect;

            const float wantedRender = renderCorrectionAllowed
                && std::abs(correction) >= 3.0f ? correction : 0.0f;
            if (wantedRender != 0.0f) {
                renderGateMisses = 0;
            } else {
                ++renderGateMisses;
            }

            const float renderGoal = renderGateMisses <= 2 ? renderTargetCents : 0.0f;
            const float target = wantedRender != 0.0f ? wantedRender : renderGoal;
            const float maxStep = wantedRender != 0.0f
                ? 6.0f + 30.0f * hardRender
                : 4.0f + 12.0f * hardRender;
            renderTargetCents += std::clamp(target - renderTargetCents, -maxStep, maxStep);
            if (std::abs(renderTargetCents) < 0.5f)
                renderTargetCents = 0.0f;

            appendDebugTraceFrame(frame, amount, natural, speed, focus, appliedScaleMask, correction);

            lastF0Hz.store(frame.f0Hz.value, std::memory_order_relaxed);
            lastConfidence.store(frame.f0Hz.confidence, std::memory_order_relaxed);
            lastCentreCents.store(frame.centreCents, std::memory_order_relaxed);
            lastResidualCents.store(frame.residualCents, std::memory_order_relaxed);
            lastReason.store(static_cast<int>(frame.f0Hz.reason), std::memory_order_relaxed);
            lastCorrectionCents.store(correction, std::memory_order_relaxed);
        }

        const float* inputChans[2] = {
            buffer.getReadPointer(0) + offset,
            numChannels > 1 ? buffer.getReadPointer(1) + offset : nullptr
        };
        float* outputChans[2] = {
            buffer.getWritePointer(0) + offset,
            numChannels > 1 ? buffer.getWritePointer(1) + offset : nullptr
        };
        pitchRenderer->process(inputChans, outputChans, numChannels, n,
                               renderProcessCents, renderTargetCents);
        renderProcessCents = renderTargetCents;
    }
}

void VXTuneAudioProcessor::updateAutoKeyEstimate(const vxsuite::tune::PitchFrame& frame,
                                                  const vxsuite::tune::NoteSegment& segment) noexcept {
    const bool reliable =
        frame.f0Hz.value > 0.0f
        && frame.f0Hz.reason == vxsuite::tune::EstimateReason::nominal
        && frame.f0Hz.confidence >= 0.60f
        && frame.decompConfidence >= 0.50f
        && frame.levelDb > -70.0f;
    if (!reliable)
        return;

    // Down-weight ornamental motion: a passing/chromatic note or a fresh
    // note onset is a real event but weak evidence for the song's key
    // compared to a held, settled note. Continuous weighting (not a hard
    // gate) mirrors how CorrectionEngine treats the same probabilities.
    using vxsuite::tune::Behaviour;
    const float passingNoteProb = segment.behaviourProb[static_cast<int>(Behaviour::passingNote)];
    const float onsetProb = segment.behaviourProb[static_cast<int>(Behaviour::onset)];
    const float ornamentalWeight = std::clamp(
        1.0f - std::max(passingNoteProb, 0.6f * onsetProb), 0.0f, 1.0f);
    if (ornamentalWeight <= 0.02f)
        return;

    for (auto& bin : autoKeyHistogram)
        bin *= autoKeyDecayPerFrame;

    // Pitch class from the decomposed note centre only: residualCents is
    // the fast expressive layer (vibrato/bends/scoops) and can cross a
    // semitone boundary even while the underlying note stays diatonic.
    const int pitchClass = midiPitchClassFromCents(frame.centreCents);
    const float weight = ornamentalWeight
        * std::clamp(frame.f0Hz.confidence * frame.decompConfidence, 0.0f, 1.0f);
    autoKeyHistogram[static_cast<size_t>(pitchClass)] += weight;
    autoKeyFrameCount = std::min(autoKeyFrameCount + 1, 2000000);
    finalizeAutoKeyEstimate();
}

void VXTuneAudioProcessor::updateAutoKeyFromSidechain() noexcept {
    const float presence = harmonicContext.presence();
    // Below this, the analysed window is closer to noise floor than real
    // instrumental signal - don't let near-silence chroma pollute the
    // histogram (a Fourier transform of noise/hum still produces SOME
    // pitch-class energy, it just isn't harmony).
    constexpr float kPresenceGate = 0.12f;
    if (presence < kPresenceGate)
        return;

    // Sidechain evidence does NOT decay the histogram itself (only the
    // vocal path does, in updateAutoKeyEstimate() above) - an instrumental
    // often plays alone before the vocal enters, and un-decayed
    // accumulation during that stretch is exactly what lets the key be
    // known by the time singing starts, matching the "section-based, not
    // first-note-based" auto-key design already in place for the vocal.
    //
    // Weight chosen so a clear, sustained chord accumulates influence
    // comparable to several seconds of confident vocal evidence (weight
    // up to ~1.0 per hop there) rather than instantly overwhelming it -
    // the instrumental is a richer signal but a single FFT window is still
    // one noisy observation, not ground truth.
    constexpr float kSidechainWeight = 0.6f;
    const auto& chroma = harmonicContext.chroma();
    const float scale = kSidechainWeight * presence;
    for (int i = 0; i < 12; ++i)
        autoKeyHistogram[static_cast<size_t>(i)] += chroma[static_cast<size_t>(i)] * scale;
    finalizeAutoKeyEstimate();
}

void VXTuneAudioProcessor::finalizeAutoKeyEstimate() noexcept {
    float total = 0.0f;
    for (const float value : autoKeyHistogram)
        total += value;
    if (total <= 0.0001f)
        return;

    // Krumhansl-Schmuckler profile correlation across all 24 major/minor
    // candidates in one pass (see VxTuneKeyProfiles.h) - replaces the old
    // two-stage "which 7 notes, then major/minor tie-break" scoring. That
    // scheme scored relative major/minor as separate rivals sharing an
    // identical in/out-of-scale mask, so the real margin was buried under a
    // near-zero tie; a graded profile tells them apart directly and gives a
    // score that's already bounded to [-1, 1], usable as confidence as-is.
    const auto match = vxsuite::tune::findBestKeyProfile(autoKeyHistogram);

    // Hysteresis on which candidate we hold, mirroring CorrectionEngine's
    // engage/disengage pattern: only switch away from the currently-held
    // key once a challenger's correlation clearly exceeds the held
    // candidate's own correlation against the *current* histogram, so noise
    // near a tie doesn't flip the display every update (verified against a
    // full vocal take, where the note-collection choice was correct ~86% of
    // the time but an un-hysteresised major/minor decision still flipped
    // 24 times across the song).
    const float heldCorrelation = vxsuite::tune::keyProfileCorrelation(
        autoKeyHistogram,
        autoKeyIsMajor ? vxsuite::tune::kKsMajorProfile : vxsuite::tune::kKsMinorProfile,
        autoDetectedRoot);
    constexpr float kKeyHysteresisMargin = 0.05f;
    float displayedCorrelation = heldCorrelation;
    if ((match.root != autoDetectedRoot || match.isMajor != autoKeyIsMajor)
        && match.correlation > heldCorrelation + kKeyHysteresisMargin) {
        autoDetectedRoot = match.root;
        autoKeyIsMajor = match.isMajor;
        displayedCorrelation = match.correlation;
    }

    autoDetectedScale = autoKeyIsMajor ? 2 : 3;
    autoKeyConfidence = std::clamp(displayedCorrelation, 0.0f, 1.0f);
    lastAutoKeyRoot.store(autoDetectedRoot, std::memory_order_relaxed);
    lastAutoKeyScale.store(autoDetectedScale, std::memory_order_relaxed);
    lastAutoKeyConfidence.store(autoKeyConfidence, std::memory_order_relaxed);
    lastAutoKeyFrames.store(autoKeyFrameCount, std::memory_order_relaxed);
}

bool VXTuneAudioProcessor::bestModeForFixedRoot(const int root) noexcept {
    if (root != autoModeForFixedRootHeldRoot) {
        // The pinned root changed - the previous hysteresis decision was
        // about a different root and doesn't apply; reseed from scratch.
        autoModeForFixedRootHeldRoot = root;
        autoModeForFixedRootIsMajor =
            vxsuite::tune::keyProfileCorrelation(autoKeyHistogram, vxsuite::tune::kKsMajorProfile, root)
            >= vxsuite::tune::keyProfileCorrelation(autoKeyHistogram, vxsuite::tune::kKsMinorProfile, root);
    }
    const float majorCorr =
        vxsuite::tune::keyProfileCorrelation(autoKeyHistogram, vxsuite::tune::kKsMajorProfile, root);
    const float minorCorr =
        vxsuite::tune::keyProfileCorrelation(autoKeyHistogram, vxsuite::tune::kKsMinorProfile, root);
    constexpr float kMargin = 0.05f;
    if (autoModeForFixedRootIsMajor) {
        if (minorCorr > majorCorr + kMargin)
            autoModeForFixedRootIsMajor = false;
    } else {
        if (majorCorr > minorCorr + kMargin)
            autoModeForFixedRootIsMajor = true;
    }
    return autoModeForFixedRootIsMajor;
}

int VXTuneAudioProcessor::bestRootForFixedMode(const bool isMajor) noexcept {
    const auto& profile = isMajor ? vxsuite::tune::kKsMajorProfile : vxsuite::tune::kKsMinorProfile;
    if (isMajor != autoRootForFixedModeHeldIsMajor) {
        // The pinned mode changed - re-seed from the outright best root for
        // the new mode rather than carrying over a root chosen for the
        // other mode.
        autoRootForFixedModeHeldIsMajor = isMajor;
        autoRootForFixedMode = 0;
        float best = -2.0f;
        for (int r = 0; r < 12; ++r) {
            const float c = vxsuite::tune::keyProfileCorrelation(autoKeyHistogram, profile, r);
            if (c > best) {
                best = c;
                autoRootForFixedMode = r;
            }
        }
    }
    const float heldCorr =
        vxsuite::tune::keyProfileCorrelation(autoKeyHistogram, profile, autoRootForFixedMode);
    int bestRoot = autoRootForFixedMode;
    float bestCorr = heldCorr;
    for (int r = 0; r < 12; ++r) {
        const float c = vxsuite::tune::keyProfileCorrelation(autoKeyHistogram, profile, r);
        if (c > bestCorr) {
            bestCorr = c;
            bestRoot = r;
        }
    }
    constexpr float kMargin = 0.05f;
    if (bestRoot != autoRootForFixedMode && bestCorr > heldCorr + kMargin)
        autoRootForFixedMode = bestRoot;
    return autoRootForFixedMode;
}

std::uint16_t VXTuneAudioProcessor::currentScaleMask(const int keyChoice, const int scaleChoice) noexcept {
    if (scaleChoice == 1)
        return vxsuite::tune::CorrectionEngine::kChromaticMask;

    // Ignore is the initial no-key shortcut. If the user subsequently chooses
    // a non-chromatic scale, use the learned root so that scale choice remains
    // useful without requiring another key selection.
    const bool needsAutoKey = keyChoice <= 0 || keyChoice == kIgnoreKeyChoice;
    const bool needsAutoScale = scaleChoice <= 0;
    if ((needsAutoKey || needsAutoScale)
        && (autoKeyFrameCount < autoKeyMinFrames || autoKeyConfidence < kAutoKeyConfidenceGate))
        return vxsuite::tune::CorrectionEngine::kChromaticMask;

    int root;
    int scale;
    if (needsAutoKey && needsAutoScale) {
        // Free joint pick: already validated as a pair by findBestKeyProfile.
        root = autoDetectedRoot;
        scale = autoDetectedScale;
    } else if (!needsAutoKey && needsAutoScale) {
        // Root pinned, mode free: pick the best-correlated mode for THAT
        // root, not whatever mode happened to win for the (possibly
        // different) free auto root.
        root = std::clamp(keyChoice - 1, 0, 11);
        scale = bestModeForFixedRoot(root) ? 2 : 3;
    } else if (needsAutoKey && !needsAutoScale) {
        // Mode pinned, root free: pick the best-correlated root for THAT
        // mode. Harmonic/melodic minor share the natural-minor profile's
        // root preference closely enough to use it here too - a dedicated
        // profile for those variants isn't available and would be
        // over-scoped for root selection alone.
        const bool wantsMajor = scaleChoice == 2;
        root = bestRootForFixedMode(wantsMajor);
        scale = scaleChoice;
    } else {
        // Both pinned - fully manual, no auto involvement.
        root = std::clamp(keyChoice - 1, 0, 11);
        scale = scaleChoice;
    }
    return maskForRootAndScale(root, scale);
}

void VXTuneAudioProcessor::configureDebugTrace(const double sampleRate) {
    debugTraceSampleRate = sampleRate > 1000.0 ? sampleRate : 48000.0;
    const char* path = std::getenv("VXTUNE_DEBUG_CSV");
    debugTraceEnabled = path != nullptr && path[0] != '\0';
    debugTracePath = debugTraceEnabled ? path : std::string {};
    debugTraceRows.clear();
    if (debugTraceEnabled)
        debugTraceRows.reserve(300000); // ~26 minutes at 48k / 256-hop.
}

void VXTuneAudioProcessor::appendDebugTraceFrame(
    const vxsuite::tune::PitchFrame& frame, const float amount, const float natural,
    const float speed, const float focus, const std::uint16_t scaleMask, const float correction) {
    if (!debugTraceEnabled || debugTraceRows.size() >= debugTraceRows.capacity())
        return;

    DebugTraceRow row;
    row.sample = frame.timeSamples;
    row.seconds = static_cast<double>(frame.timeSamples) / debugTraceSampleRate;
    row.f0Hz = frame.f0Hz.value;
    row.confidence = frame.f0Hz.confidence;
    row.reason = static_cast<int>(frame.f0Hz.reason);
    row.centreCents = frame.centreCents;
    row.residualCents = frame.residualCents;
    row.decompConfidence = frame.decompConfidence;
    row.correctionCents = correction;
    row.correctedCents = frame.centreCents + frame.residualCents + correction;
    const auto authority = correctionEngine.currentAuthoritySnapshot();
    row.musicalAuthority = authority.total;
    row.authorityConfidence = authority.confidence;
    row.authorityPassing = authority.passing;
    row.authorityOnset = authority.onset;
    row.authorityTransition = authority.transition;
    row.authorityTarget = authority.target;
    row.authorityStableBoost = authority.stableBoost;
    row.authorityNearCorrect = authority.nearCorrect;
    const auto target = correctionEngine.currentTargetSnapshot();
    row.targetMidiNote = target.midiNote;
    row.runnerUpMidiNote = target.runnerUpMidiNote;
    row.targetErrorCents = target.errorCents;
    row.targetMarginLog = target.marginLog;
    const auto shifterDebug = pitchRenderer->debugSnapshot();
    row.shifterTargetCents = shifterDebug.requestedCents;
    row.shifterSmoothedCents = shifterDebug.appliedCents;
    row.shifterMix = shifterDebug.mix;
    row.shifterHintPeriod = shifterDebug.hintPeriod;
    row.shifterHintConfidence = shifterDebug.hintConfidence;
    row.shifterVoicedActive = shifterDebug.voicedActive;
    row.shifterParkedNow = shifterDebug.parkedNow;
    row.shifterParkedRun = shifterDebug.parkedRun;
    row.shifterEpochCount = shifterDebug.epochCount;
    row.rendererBackend = shifterDebug.backendName;
    row.rendererProfile = shifterDebug.qualityProfileName;
    row.rendererLatencySamples = shifterDebug.latencySamples;
    row.rendererBlockSamples = shifterDebug.blockSamples;
    row.rendererIntervalSamples = shifterDebug.intervalSamples;
    row.rendererSplitComputation = shifterDebug.splitComputation;
    row.amount = amount;
    row.natural = natural;
    row.speed = speed;
    row.focus = focus;
    row.scaleMask = scaleMask;
    row.autoKeyRoot = autoDetectedRoot;
    row.autoKeyScale = autoDetectedScale;
    row.autoKeyConfidence = autoKeyConfidence;
    row.autoKeyFrames = autoKeyFrameCount;

    const auto& segment = correctionEngine.currentSegment();
    for (int i = 0; i < static_cast<int>(vxsuite::tune::Behaviour::count); ++i)
        row.behaviour[i] = segment.behaviourProb[i];

    debugTraceRows.push_back(row);
}

void VXTuneAudioProcessor::flushDebugTrace() {
    if (!debugTraceEnabled || debugTracePath.empty() || debugTraceRows.empty())
        return;

    bool writeHeader = true;
    {
        std::ifstream existing(debugTracePath, std::ios::ate);
        writeHeader = !existing || existing.tellg() <= 0;
    }

    std::ofstream out(debugTracePath, std::ios::out | std::ios::app);
    if (!out)
        return;

    if (writeHeader) {
        out << "sample,seconds,f0_hz,confidence,reason,centre_cents,residual_cents,"
               "decomp_confidence,correction_cents,corrected_cents,amount,natural,speed,focus,scale_mask,"
               "auto_key_root,auto_key_scale,auto_key_confidence,auto_key_frames,"
               "musical_authority,target_margin_log,"
               "authority_confidence,authority_passing,authority_onset,"
               "authority_transition,authority_target,authority_stable_boost,"
               "authority_near_correct,target_midi_note,runner_up_midi_note,"
               "target_error_cents,"
               "shifter_target_cents,shifter_smoothed_cents,shifter_mix,"
               "shifter_hint_period,shifter_hint_confidence,shifter_voiced_active,"
               "shifter_parked_now,shifter_parked_run,shifter_epoch_count,"
               "renderer_backend,renderer_profile,renderer_latency_samples,"
               "renderer_block_samples,renderer_interval_samples,renderer_split_computation,"
               "behaviour_unvoiced,behaviour_onset,behaviour_sustain,behaviour_vibrato,"
               "behaviour_bend,behaviour_slide,behaviour_scoop,behaviour_fall,"
               "behaviour_passing_note\n";
    }
    for (const auto& row : debugTraceRows) {
        out << row.sample << ','
            << row.seconds << ','
            << row.f0Hz << ','
            << row.confidence << ','
            << row.reason << ','
            << row.centreCents << ','
            << row.residualCents << ','
            << row.decompConfidence << ','
            << row.correctionCents << ','
            << row.correctedCents << ','
            << row.amount << ','
            << row.natural << ','
            << row.speed << ','
            << row.focus << ','
            << row.scaleMask << ','
            << row.autoKeyRoot << ','
            << row.autoKeyScale << ','
            << row.autoKeyConfidence << ','
            << row.autoKeyFrames << ','
            << row.musicalAuthority << ','
            << row.targetMarginLog << ','
            << row.authorityConfidence << ','
            << row.authorityPassing << ','
            << row.authorityOnset << ','
            << row.authorityTransition << ','
            << row.authorityTarget << ','
            << row.authorityStableBoost << ','
            << row.authorityNearCorrect << ','
            << row.targetMidiNote << ','
            << row.runnerUpMidiNote << ','
            << row.targetErrorCents << ','
            << row.shifterTargetCents << ','
            << row.shifterSmoothedCents << ','
            << row.shifterMix << ','
            << row.shifterHintPeriod << ','
            << row.shifterHintConfidence << ','
            << row.shifterVoicedActive << ','
            << row.shifterParkedNow << ','
            << row.shifterParkedRun << ','
            << row.shifterEpochCount << ','
            << row.rendererBackend << ','
            << row.rendererProfile << ','
            << row.rendererLatencySamples << ','
            << row.rendererBlockSamples << ','
            << row.rendererIntervalSamples << ','
            << row.rendererSplitComputation;
        for (const float p : row.behaviour)
            out << ',' << p;
        out << '\n';
    }
    debugTraceRows.clear();
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
