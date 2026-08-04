#include "VxProximityProcessor.h"
#include "../../framework/VxStudioHelpContent.h"
#include "VxStudioVersions.h"

#include <cmath>

namespace {

constexpr std::string_view kProductName = "VX Studio Proximity";
constexpr std::string_view kShortTag     = "PRX";
constexpr std::string_view kCloserParam  = "closer";
constexpr std::string_view kAirParam     = "air";
constexpr std::string_view kMudParam     = "mud";
constexpr std::string_view kModeParam    = "mode";
constexpr std::string_view kListenParam  = "listen";

} // namespace

VXProximityAudioProcessor::VXProximityAudioProcessor()
    : ProcessorBase(makeIdentity()) {}

vxsuite::ProductIdentity VXProximityAudioProcessor::makeIdentity() {
    vxsuite::ProductIdentity identity {};
    identity.productName       = kProductName;
    identity.shortTag          = kShortTag;
    identity.primaryParamId    = kCloserParam;
    identity.secondaryParamId  = kAirParam;
    identity.tertiaryParamId   = kMudParam;
    identity.modeParamId       = kModeParam;
    identity.listenParamId     = kListenParam;
    identity.defaultMode       = vxsuite::Mode::vocal;
    identity.primaryLabel      = "Closer";
    identity.secondaryLabel    = "Air";
    identity.tertiaryLabel     = "Mud";
    identity.primaryHint       = "Simulate moving the mic closer for natural bass body and warmth.";
    identity.secondaryHint     = "Add upper presence and clarity that characterises a close placement.";
    identity.tertiaryHint      = "Control boom compensation - balance bass depth vs mud for different sources.";
    identity.primaryDefaultValue = 0.0f;
    identity.secondaryDefaultValue = 0.0f;
    identity.tertiaryDefaultValue = 0.5f;
    identity.stageId           = "vx.proximity";
    identity.stageType         = vxsuite::StageType::mixed;
    identity.dspVersion        = vxsuite::versions::plugins::proximity;
    identity.helpTitle         = vxsuite::help::proximity.title;
    identity.helpHtml          = vxsuite::help::proximity.html;
    identity.readmeSection     = vxsuite::help::proximity.readmeSection;
    identity.theme.accentRgb      = { 1.00f, 0.65f, 0.10f };
    identity.theme.accent2Rgb     = { 0.10f, 0.08f, 0.05f };
    identity.theme.backgroundRgb  = { 0.06f, 0.05f, 0.04f };
    identity.theme.panelRgb       = { 0.10f, 0.09f, 0.07f };
    identity.theme.textRgb        = { 0.95f, 0.90f, 0.80f };
    return identity;
}

juce::String VXProximityAudioProcessor::getStatusText() const {
    if (isListenEnabled())
        return "Listen - removed distance shaping only";

    const auto& modePolicy = currentModePolicy();
    return modePolicy.mode == vxsuite::Mode::vocal
        ? "Vocal - close-mic body with controlled low-mid cleanup and presence focus"
        : "General - broader close-mic weight with capsule air and restrained mud";
}

void VXProximityAudioProcessor::prepareSuite(const double sampleRate,
                                              const int samplesPerBlock) {
    currentSampleRateHz = sampleRate > 1000.0 ? sampleRate : 48000.0;
    proximityDsp.setChannelCount(getTotalNumOutputChannels());
    proximityDsp.prepare(currentSampleRateHz, samplesPerBlock);
    drynessDsp.setChannelCount(getTotalNumOutputChannels());
    drynessDsp.prepare(currentSampleRateHz, samplesPerBlock);
    outputTrimmer.setCeiling(0.96f);
    outputTrimmer.setReleaseSeconds(0.16f);
    setReportedLatencySamples(drynessDsp.getLatencySamples());
    resetSuite();
}

void VXProximityAudioProcessor::resetSuite() {
    proximityDsp.reset();
    drynessDsp.reset();
    outputTrimmer.reset();
    const float closer = vxsuite::readNormalized(parameters, productIdentity.primaryParamId,   0.0f);
    const float air    = vxsuite::readNormalized(parameters, productIdentity.secondaryParamId, 0.0f);
    const float mud    = vxsuite::readNormalized(parameters, productIdentity.tertiaryParamId,  0.5f);
    controls.reset(closer, air, mud);
}

void VXProximityAudioProcessor::processProduct(juce::AudioBuffer<float>& buffer,
                                                juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0)
        return;

    const float closerTarget = vxsuite::readNormalized(parameters, productIdentity.primaryParamId,   0.f);
    const float airTarget    = vxsuite::readNormalized(parameters, productIdentity.secondaryParamId, 0.f);
    const float mudTarget    = vxsuite::readNormalized(parameters, productIdentity.tertiaryParamId,  0.5f);

    const auto [smoothedCloser, smoothedAir, smoothedMud] = controls.process(
        closerTarget, airTarget, mudTarget,
        currentSampleRateHz, numSamples,
        0.060f, 0.090f, 0.080f);

    const auto& modePolicy = currentModePolicy();
    const bool isVoice = modePolicy.mode == vxsuite::Mode::vocal;
    const auto voiceContext = getVoiceContextSnapshot();
    const float vocalPriority = isVoice
        ? vxsuite::clamp01(0.35f * voiceContext.vocalDominance
                         + 0.25f * voiceContext.intelligibility
                         + 0.20f * voiceContext.phraseActivity
                         + 0.20f * voiceContext.transientRisk)
        : 0.0f;
    // Front-load air: a straight-line knob-to-dB mapping meant early knob
    // travel produced very little audible change, so the control felt like
    // it needed to be maxed out to do anything. `closer` and `mud` are
    // deliberately NOT shaped: `closer` also drives the dryness pre-stage
    // and feeds proximityGainDb's own clamp, and `mud` feeds outputTrimDb's
    // compensation — testProximityCloserKeepsLoudnessSteadierAcrossSources /
    // ...ActsMonotonicallyWithCloser / ...GrowthIsNonlinearWithCloser have
    // essentially zero margin against either (even shaping `mud`'s *default*
    // value alone was enough to flip monotonicity by a hair). Front-loading
    // `closer` properly needs the whole downstream chain (physics clamp,
    // dryness amount, output trim) reworked together, not just the input -
    // scoped as separate follow-up, same as the mud-bell issue above.
    const float shapedAir = vxsuite::frontLoadedControl(smoothedAir, 0.80f);

    const float effectiveCloser = isVoice
        ? vxsuite::clamp01(smoothedCloser * (0.96f + 0.14f * voiceContext.buriedSpeech + 0.06f * modePolicy.bodyRecovery))
        : vxsuite::clamp01(smoothedCloser * (0.96f + 0.06f * modePolicy.bodyRecovery));
    const float effectiveAir = isVoice
        ? vxsuite::clamp01(shapedAir * (0.94f - 0.10f * vocalPriority + 0.10f * voiceContext.intelligibility))
        : vxsuite::clamp01(shapedAir * (0.92f + 0.08f * modePolicy.speechFocus));

    // Dryness pre-stage: reduce room/reverberant energy on the raw input so
    // "closer" also shrinks the direct-to-reverberant ratio, not just the bass
    // EQ curve. Amount is intentionally gentle (this simulates distance, it
    // isn't a reverb-removal tool) and tracks the same effectiveCloser used
    // for the EQ so both cues move together. No separate makeup gain here —
    // ProximityDsp's own content analysis and adaptive output trim run right
    // after this and already coordinate loudness across different spectral
    // balances; a second, independent compensator stacked on top of that
    // (tried first) broke exactly the invariant it was meant to protect,
    // because the two systems don't know about each other's gain moves.
    constexpr float kMaxDrynessAmount = 0.15f;
    const vxsuite::ProcessOptions drynessOptions {};
    drynessDsp.processInPlace(buffer, effectiveCloser * kMaxDrynessAmount, drynessOptions);

    proximityDsp.processInPlace(buffer, numSamples,
                                effectiveCloser,
                                effectiveAir,
                                isVoice,
                                vocalPriority,
                                smoothedMud);

    outputTrimmer.process(buffer, currentSampleRateHz);
}

void VXProximityAudioProcessor::renderListenOutput(juce::AudioBuffer<float>& outputBuffer,
                                                    const juce::AudioBuffer<float>& inputBuffer) {
    renderAddedDeltaOutput(outputBuffer, inputBuffer);
}

vxsuite::MeteringSnapshot VXProximityAudioProcessor::getMeteringSnapshot() const noexcept {
    vxsuite::MeteringSnapshot s;
    s.gainReductionDb = std::max(0.0f, -lastProximityGainDb);
    s.compActivity    = outputTrimmer.getCurrentActivity01();
    return s;
}

#if !defined(VXSTUDIO_DISABLE_PLUGIN_ENTRYPOINT)
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new VXProximityAudioProcessor();
}
#endif
