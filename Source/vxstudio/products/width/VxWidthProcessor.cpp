#include "VxWidthProcessor.h"

#include "vxstudio/framework/VxStudioHelpContent.h"
#include "vxstudio/framework/VxStudioParameters.h"
#include "VxStudioVersions.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace {

constexpr std::string_view kProductName = "VX Studio Width";
constexpr std::string_view kShortTag    = "WID";
constexpr std::string_view kWidthParam      = "width";
constexpr std::string_view kDoubleParam     = "double";
constexpr std::string_view kTightnessParam  = "tightness";
constexpr std::string_view kFocusParam      = "focus";
// EXPERIMENTAL (2026-08-07, off by default) - user-facing so it can
// actually be A/B'd in a DAW, but deliberately not one of the four main
// knobs: a plain header toggle via ProductIdentity::simpleToggleParamId.
constexpr std::string_view kMicroPitchExperimentalParam = "microPitchExperimental";

// Region A (narrow, Width -100..0): SideOut = SideIn * sideScale, 1 at 0, 0 at -100.
// Perceptually smooth curve per spec §7.1 rather than a raw linear map.
float narrowSideScale(const float widthNegative01) noexcept {
    // widthNegative01: 0 at Width=0, 1 at Width=-100.
    const float shaped = std::pow(1.0f - widthNegative01, 1.6f);
    return juce::jlimit(0.0f, 1.0f, shaped);
}

// Region B (bounded expansion, Width 0..+35): direct Side gain, capped per §7.2
// ("approximately +3 dB to +6 dB" max). Region C (decorrelated layer, +35..+100)
// is a later build-order step (§7.3-7.4) and is not applied yet - Width is
// clamped to the Region B ceiling until that lands, so higher settings do not
// silently regress to no-op.
constexpr float kRegionBCeiling = 35.0f;
constexpr float kRegionBMaxGainDb = 4.5f;

// Lag-aware predictability correlation (§11.3): checked at these lags, not
// same-sample only - a delayed copy of Mid (exactly what the ADT voice's
// delay line produces) can be entirely derived from Mid while reading
// near-zero correlation at lag 0.
constexpr float kPredictabilityLagsMs[] = { 0.0f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f };
constexpr float kPredictabilityMaxLagMs = 16.0f;

// monoRiskRestraint: 0 = fully restrained (no boost applied, e.g. broadband
// correlation at -1/anti-phase), 1 = unrestrained (correlation at +1/safe).
// §7.2: "restrained by mono-risk analysis" - a phase-risky signal should not
// have its Side energy boosted further.
float expandedSideGain(const float widthPositivePercent, const float monoRiskRestraint) noexcept {
    const float clamped = juce::jlimit(0.0f, kRegionBCeiling, widthPositivePercent);
    const float gainDb = (clamped / kRegionBCeiling) * kRegionBMaxGainDb * juce::jlimit(0.0f, 1.0f, monoRiskRestraint);
    return juce::Decibels::decibelsToGain(gainDb);
}

// Region C (decorrelated widening, Width +35..+100, §7.3-7.4): blend ramps
// 0..1 across the region so the Region B -> Region C handover is continuous
// (§4.1 "the transition between regions must be continuous and inaudible").
constexpr float kRegionCCeiling = 100.0f;
constexpr float kRegionCMaxLevel = 0.85f;
constexpr float kDecorrelatorWindowMs = 1.2f;

float regionCBlend(const float widthPositivePercent) noexcept {
    if (widthPositivePercent <= kRegionBCeiling)
        return 0.0f;
    const float t = (widthPositivePercent - kRegionBCeiling) / (kRegionCCeiling - kRegionBCeiling);
    return juce::jlimit(0.0f, 1.0f, t);
}

} // namespace

VXWidthAudioProcessor::VXWidthAudioProcessor()
    : ProcessorBase(makeIdentity()) {}

// §15: mono input to stereo output is a REQUIRED configuration (in addition
// to stereo-in/stereo-out), unlike ProcessorBase's default negotiation
// (input channel set must equal output). Output is always stereo - Width
// only makes sense with a stereo output; a genuinely mono-in/mono-out
// bypass path is not part of this product's contract.
bool VXWidthAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto output = layouts.getMainOutputChannelSet();
    if (output != juce::AudioChannelSet::stereo())
        return false;
    const auto input = layouts.getMainInputChannelSet();
    return input == juce::AudioChannelSet::stereo() || input == juce::AudioChannelSet::mono();
}

vxsuite::ProductIdentity VXWidthAudioProcessor::makeIdentity() {
    vxsuite::ProductIdentity id {};
    id.productName      = kProductName;
    id.shortTag         = kShortTag;
    id.primaryParamId   = kWidthParam;
    id.secondaryParamId = kDoubleParam;
    id.tertiaryParamId  = kTightnessParam;
    id.quaternaryParamId = kFocusParam;
    id.primaryLabel      = "Width";
    id.secondaryLabel    = "Double";
    id.tertiaryLabel     = "Tightness";
    id.quaternaryLabel   = "Focus";
    id.primaryHint    = "Stereo image size: left narrows toward mono, right widens the image.";
    id.secondaryHint  = "Introduces a synthetic doubled performance alongside the original.";
    id.tertiaryHint   = "How closely the generated performance follows the original - tight and precise, or loose and separate.";
    id.quaternaryHint = "Where in the spectrum the width and doubling effect concentrates - body, full range, or air.";
    id.dspVersion    = vxsuite::versions::plugins::width;
    id.helpTitle     = vxsuite::help::width.title;
    id.helpHtml      = vxsuite::help::width.html;
    id.readmeSection = vxsuite::help::width.readmeSection;
    // Width: centred -100..+100, default 0 (Original).
    id.primaryDefaultValue   = 0.5f;
    // Double: 0..100, default 0.
    id.secondaryDefaultValue = 0.0f;
    // Tightness: 0..100, default 40 (spec §4.3).
    id.tertiaryDefaultValue  = 0.40f;
    // Focus: 0..100, default 55 (spec §4.4).
    id.quaternaryDefaultValue = 0.55f;
    id.theme.accentRgb     = { 0.20f, 0.78f, 0.72f };
    id.theme.accent2Rgb    = { 0.05f, 0.13f, 0.12f };
    id.theme.backgroundRgb = { 0.04f, 0.07f, 0.07f };
    id.theme.panelRgb      = { 0.08f, 0.12f, 0.12f };
    id.theme.textRgb       = { 0.90f, 0.98f, 0.96f };
    id.requiresSignalQuality = true; // side/mid mono score feeds InputAnalyser (§6.1).

    // EXPERIMENTAL micro-pitch toggle (2026-08-07, off by default via
    // simpleToggleDefaultValue) - a plain header button, not one of the
    // four main knobs. See AdtVoice::setMicroPitchEnabled()'s comment for
    // what this actually does; see tasks/todo.md for the investigation
    // this came out of and why it's gated separately from the four-knob
    // "production" surface rather than exposed as a real control yet.
    id.simpleToggleParamId = kMicroPitchExperimentalParam;
    id.simpleToggleLabel = "Micro-Pitch (Experimental)";
    id.simpleToggleDefaultValue = false;

    // §20: factory presets demonstrate use cases, not technical parameters -
    // only 4 knobs are ever touched. Spec §20 lists 14 example names; the
    // framework's preset selector caps at 6 slots (maxUiPresets) - picked a
    // curated subset spanning the full range (narrow/subtle/safe-wide/
    // vocal-double/loose-ADT/extreme) rather than trying to cram all 14 in,
    // matching §20's own "avoid dozens of near-duplicate presets."
    id.presetSelectorLabel = "Preset";
    id.presetChoiceCount = 6;
    id.presetChoiceLabels[0] = "Mono Maker";
    id.presetChoiceLabels[1] = "Natural Width";
    id.presetChoiceLabels[2] = "Wide but Safe";
    id.presetChoiceLabels[3] = "Vocal Double";
    id.presetChoiceLabels[4] = "Loose ADT";
    id.presetChoiceLabels[5] = "Extreme Width";
    // Width=-100 (mono), everything else at default.
    id.presetPrimaryValues[0]   = 0.0f;
    id.presetSecondaryValues[0] = 0.0f;
    id.presetTertiaryValues[0]  = 0.40f;
    id.presetQuaternaryValues[0] = 0.55f;
    // Width=+15 (Region B, conservative expansion).
    id.presetPrimaryValues[1]   = 0.5f + 15.0f / 200.0f;
    id.presetSecondaryValues[1] = 0.0f;
    id.presetTertiaryValues[1]  = 0.40f;
    id.presetQuaternaryValues[1] = 0.55f;
    // Width=+60 (into Region C decorrelated widening).
    id.presetPrimaryValues[2]   = 0.5f + 60.0f / 200.0f;
    id.presetSecondaryValues[2] = 0.0f;
    id.presetTertiaryValues[2]  = 0.40f;
    id.presetQuaternaryValues[2] = 0.55f;
    // Width=0, Double engaged, tighter/precise, Focus toward Air.
    id.presetPrimaryValues[3]   = 0.5f;
    id.presetSecondaryValues[3] = 0.55f;
    id.presetTertiaryValues[3]  = 0.35f;
    id.presetQuaternaryValues[3] = 0.65f;
    // Width mild, Double strong, Tightness loose (separate-take feel).
    id.presetPrimaryValues[4]   = 0.5f + 10.0f / 200.0f;
    id.presetSecondaryValues[4] = 0.70f;
    id.presetTertiaryValues[4]  = 0.75f;
    id.presetQuaternaryValues[4] = 0.55f;
    // Width=+100 (max), Double moderate.
    id.presetPrimaryValues[5]   = 1.0f;
    id.presetSecondaryValues[5] = 0.30f;
    id.presetTertiaryValues[5]  = 0.40f;
    id.presetQuaternaryValues[5] = 0.55f;

    return id;
}

juce::String VXWidthAudioProcessor::getStatusText() const {
    return "Stereo image and doubling - narrow, widen, or double a signal with four musical controls";
}

void VXWidthAudioProcessor::prepareSuite(const double sampleRate, const int /*samplesPerBlock*/) {
    currentSampleRateHz = sampleRate > 1000.0 ? sampleRate : 48000.0;
    outputTrimmer.setCeiling(0.98f);
    outputTrimmer.setReleaseSeconds(0.12f);
    inputAnalyser.prepare(currentSampleRateHz);
    decorrelatorA.prepare(currentSampleRateHz, kDecorrelatorWindowMs, vxsuite::width::kDecorrelatorTapsA);
    decorrelatorB.prepare(currentSampleRateHz, kDecorrelatorWindowMs, vxsuite::width::kDecorrelatorTapsB);
    // Distinct, deterministic per-voice seeds (§17) so Voice A/B never
    // correlate. `0x2222` (not the "obvious" `0x5678`) was chosen by a
    // single-fixture seed search (2026-08-07). A LATER corpus-wide,
    // holdout-validated search (`VXWidthAdtSeedOptimizer`, tests/
    // VXWidthAdtSeedOptimizer.cpp) found 0x3333/0x5678 scored ~2.2x better
    // on an isolated-voice correlation proxy - but measured through the
    // REAL processor pipeline (representative-corpus L/R imbalance,
    // VXWidthShellCheck), that "improvement" evaporated (0x1234/0x2222:
    // widthAvg=0.111/doubleAvg=0.046; 0x3333/0x5678: widthAvg=0.111/
    // doubleAvg=0.049 - statistically identical, old config marginally
    // better). Root cause: ContentPredictabilityRestraint already reacts
    // adaptively to whatever correlation is present, largely equalising the
    // outcome regardless of the underlying seed pair - the isolated-voice
    // proxy wasn't measuring what actually reaches the output. Reverted to
    // this seed pair (the one with real end-to-end evidence); the optimizer
    // itself is kept as research tooling, not applied to production. See
    // tasks/todo.md for the full investigation.
    // EXPERIMENTAL micro-pitch config (2026-08-07, off by default via the
    // UI toggle - nothing here changes when the toggle is off). Asymmetric
    // per-voice centre/range, NOT a mirrored +/-detune - weighted average
    // centre stays approximately neutral so the source doesn't read as
    // globally detuned.
    //
    // Range history: the original +/-1.5c/+/-2c ("a few cents", per the
    // initial design brief) was confirmed INAUDIBLE by the user across
    // Double 10-100%, all Tightness/Focus settings - masked by the ADT
    // engine's existing 3-10c Doppler variation + gain/tilt movement. A
    // diagnostic build at -18..-2c/+4..+20c (clearly chorus-territory,
    // never a proposed production value) confirmed the wiring itself is
    // correct - user heard an obvious chorus effect there. Current value
    // is the deliberate middle-ground next step: Voice A -4c+/-4c (range
    // -8c..0c), Voice B +5c+/-3c (range +2c..+8c) - 2-4x the original
    // magnitude, still well short of the confirmed-audible ~10-20c range.
    // Weighted average centre ((-4+5)/2=+0.5c) stays near-neutral.
    adtVoiceA.prepare(currentSampleRateHz, 0x1234u, -4.0f, 4.0f);
    adtVoiceB.prepare(currentSampleRateHz, 0x2222u, 5.0f, 3.0f);
    focusTiltMid.prepare(currentSampleRateHz, 700.0f);
    focusTiltSide.prepare(currentSampleRateHz, 700.0f);
    subBassProtect.prepare(currentSampleRateHz, 80.0f);
    loudnessCompensator.prepare(currentSampleRateHz);
    contentPredictabilityRestraint.prepare(currentSampleRateHz);
    monoDownmixGuardrail.prepare(currentSampleRateHz);
    sideOrthogonalizer.prepare(currentSampleRateHz);
    midHistoryMaxLagSamples = static_cast<int>(std::ceil(kPredictabilityMaxLagMs * 0.001 * currentSampleRateHz)) + 1;
    midHistory.assign(static_cast<size_t>(midHistoryMaxLagSamples + 1), 0.0f);
    midHistoryWritePos = 0;
    for (size_t k = 0; k < predictabilityLagSamples.size(); ++k)
        predictabilityLagSamples[k] = static_cast<int>(std::round(kPredictabilityLagsMs[k] * 0.001 * currentSampleRateHz));
    resetSuite();
}

void VXWidthAudioProcessor::resetSuite() {
    outputTrimmer.reset();
    inputAnalyser.reset();
    decorrelatorA.reset();
    decorrelatorB.reset();
    adtVoiceA.reset();
    adtVoiceB.reset();
    focusTiltMid.reset();
    focusTiltSide.reset();
    subBassProtect.reset();
    loudnessCompensator.reset();
    contentPredictabilityRestraint.reset();
    monoDownmixGuardrail.reset();
    sideOrthogonalizer.reset();
    adtMaxPitchShiftCentsObserved = 0.0f;
    std::fill(midHistory.begin(), midHistory.end(), 0.0f);
    midHistoryWritePos = 0;
    const float width     = vxsuite::readNormalized(parameters, productIdentity.primaryParamId,   0.5f);
    const float doubleAmt = vxsuite::readNormalized(parameters, productIdentity.secondaryParamId, 0.0f);
    const float tightness = vxsuite::readNormalized(parameters, productIdentity.tertiaryParamId,  0.40f);
    const float focus     = vxsuite::readNormalized(parameters, productIdentity.quaternaryParamId, 0.55f);
    controls.reset(width, doubleAmt, tightness, focus);
}

void VXWidthAudioProcessor::processProduct(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    if (numSamples <= 0)
        return;

    const float widthTarget  = vxsuite::readNormalized(parameters, productIdentity.primaryParamId,   0.5f);
    const float doubleTarget = vxsuite::readNormalized(parameters, productIdentity.secondaryParamId, 0.0f);
    const float tightTarget  = vxsuite::readNormalized(parameters, productIdentity.tertiaryParamId,  0.40f);
    const float focusTarget  = vxsuite::readNormalized(parameters, productIdentity.quaternaryParamId, 0.55f);

    // EXPERIMENTAL (off by default) - see AdtVoice::setMicroPitchEnabled()'s
    // comment. Read once per block; toggling mid-stream just changes
    // whether the (already-running) micro-pitch walkers/shifters are
    // applied, no re-prepare needed.
    setAdtMicroPitchEnabled(vxsuite::readBool(parameters, productIdentity.simpleToggleParamId, false));

    const auto smoothed = controls.process(
        widthTarget, doubleTarget, tightTarget, focusTarget,
        currentSampleRateHz, numSamples,
        0.040f, 0.060f, 0.060f, 0.060f);

    // §15: defensive only - `isBusesLayoutSupported` always requires a
    // stereo output bus, so the host-provided buffer should never have
    // fewer than 2 channels here. If it somehow does, fall back to plain
    // passthrough rather than reading past the buffer.
    if (numChannels < 2) {
        outputTrimmer.process(buffer, currentSampleRateHz);
        return;
    }

    // §15: mono input to stereo output. `ProcessorBase::processBlock`
    // already normalises a mono input bus by duplicating channel 0 into the
    // rest BEFORE any analysis or product DSP runs - by the time
    // processProduct() is called, a mono-bus source already reads as true
    // dual-mono, exactly like the already-tested mono-to-stereo case.

    // §6: input analysis runs on the raw input ahead of this DSP - ProcessorBase
    // already computed voiceAnalysis/signalQuality on this exact block.
    const auto voice = getVoiceAnalysisSnapshot();
    const auto signalQuality = getSignalQualitySnapshot();
    inputAnalyser.update(buffer, numSamples, voice.centerConfidence, signalQuality.monoScore, voice.transientRisk);
    const auto analysis = inputAnalyser.snapshot();

    // Width -100..+100 from the 0..1 normalised control (0.5 = Original).
    const float widthSigned = (smoothed.primary - 0.5f) * 200.0f;

    auto* left  = buffer.getWritePointer(0);
    auto* right = buffer.getWritePointer(1);

    // 0 at correlation=-1 (anti-phase, fully restrained), 1 at correlation=+1 (safe).
    const float monoRiskRestraint = juce::jlimit(0.0f, 1.0f, (analysis.broadbandCorrelation + 1.0f) * 0.5f);
    // §11.3: compares processed output against unprocessed input, one block
    // delayed - see MonoDownmixGuardrail's own comment.
    const float monoDownmixRestraint = monoDownmixGuardrail.currentRestraint();

    // §9: reduce generated Side/decorrelator contribution during transients
    // (Voice A/B already reduce their own level internally - this covers
    // Region B/C, which didn't have any transient protection before now).
    const float transientProtect = 1.0f - 0.5f * juce::jlimit(0.0f, 1.0f, voice.transientRisk);

    // §8.8 (partial): the spec's full harmonic/residual/transient
    // decomposition needs actual spectral separation (STFT-level work) and
    // is explicitly a later differentiator, not attempted here. This is a
    // much coarser BLOCK-RATE proxy using signals the framework already
    // computes for exactly this purpose (VoiceAnalysisSnapshot): stable,
    // non-transient content reads as "harmonic-like" and gets modestly
    // LESS decorrelation ("limited decorrelation... protect fundamentals"
    // per §8.8's harmonic-component guidance) - it does not decompose the
    // signal into three separate streams, only scales overall Region C
    // intensity by how tonal/sustained the current block reads.
    const float harmonicProtect = 1.0f - 0.30f * voice.speechStability * (1.0f - voice.transientRisk);

    const float sideScale = widthSigned < 0.0f
        ? narrowSideScale(-widthSigned / 100.0f)
        : 1.0f;
    const float sideGain = widthSigned > 0.0f
        ? expandedSideGain(widthSigned, monoRiskRestraint) * transientProtect
        : 1.0f;
    // Region C blend/level, restrained by mono-risk AND the mono-downmix
    // guardrail (§11.3 priority: decorrelated width is restrained FIRST,
    // ahead of DoubleSide - see doubleSideAmount below).
    const float decorBlend = widthSigned > 0.0f
        ? regionCBlend(widthSigned) * monoRiskRestraint * monoDownmixRestraint
              * kRegionCMaxLevel * transientProtect * harmonicProtect
        : 0.0f;

    // §10: Focus -100(Body)..+100(Air) as -6..+6dB tilt on the WET paths'
    // source only (decorrelation + ADT) - the dry Region A/B path is
    // untouched, so Focus steers where the SPATIAL EFFECT concentrates
    // without colouring the underlying signal.
    const float focusTiltDb = (smoothed.quaternary - 0.5f) * 2.0f * 6.0f;

    // §8: Double 0..100 -> Side amount 0..1 directly; Mid amount capped at a
    // smaller fraction so "the original Mid must remain dominant" (§8.2)
    // even at Double=100. Tightness 0..100 -> 0..1 into the voice's own
    // Tight/Natural/Loose band interpolation (§8.3/§8.7).
    //
    // Double should be driven by CENTRE-confidence, not mono-confidence: a
    // clearly stereo mix can still contain a very suitable centred source
    // (vocal, snare) worth doubling, and mono-confidence would wrongly
    // discount that just because the overall signal reads as stereo. Uses
    // the framework's `centerConfidence` (block-rate L~=R correlation)
    // directly, not `analysis.monoConfidence` (which blends in the
    // broadband side/mid mono score - the wrong signal for this).
    // Soft floor so Double never fully mutes on wide/panned material, just
    // restrains toward selectivity. This is a BROADBAND, BLOCK-RATE gate -
    // it can tell "this block is mostly centred" from "this block is wide/
    // panned", but can't isolate a centred vocal from simultaneous
    // hard-panned guitars within the same block - that needs the still-
    // deferred §8.8 harmonic/residual/transient decomposition.
    constexpr float kDoubleCentreGateFloor = 0.25f;
    const float doubleCentreGate = kDoubleCentreGateFloor
        + (1.0f - kDoubleCentreGateFloor) * juce::jlimit(0.0f, 1.0f, voice.centerConfidence);
    const float doubleSideAmount = smoothed.secondary * doubleCentreGate * monoDownmixRestraint;
    const float doubleMidAmount = smoothed.secondary * 0.35f * doubleCentreGate;
    const float tightness01 = smoothed.tertiary;
    const float transientRisk = voice.transientRisk;

    double inEnergySum = 0.0;
    double outEnergySum = 0.0;
    double inMonoSumSquared = 0.0;
    double outMonoSumSquared = 0.0;
    double sumMidLagTimesGenerated[6] = {};
    double sumMidLagSquared[6] = {};
    double sumGeneratedSquared = 0.0;
    const float compGain = loudnessCompensator.currentGain();
    const float predictabilityRestraint = contentPredictabilityRestraint.currentRestraint();
    const int midHistorySize = static_cast<int>(midHistory.size());

    static constexpr float kInvSqrt2 = 0.70710678f;
    for (int i = 0; i < numSamples; ++i) {
        const float l = left[i];
        const float r = right[i];
        inEnergySum += static_cast<double>(l) * l + static_cast<double>(r) * r;
        const double inMono = static_cast<double>(l) + r;
        inMonoSumSquared += inMono * inMono;

        const float mid  = (l + r) * kInvSqrt2;
        const float sideOriginal = (l - r) * kInvSqrt2;

        // §10: shape only the wet paths' source, not the dry Region A/B path.
        const float focusedMid  = focusTiltMid.process(mid, focusTiltDb);
        const float focusedSide = focusTiltSide.process(sideOriginal, focusTiltDb);

        // §5.1: the decorrelation path runs in parallel off the original
        // Mid/Side, not the already-widened Region A/B output - the two
        // regions are conceptually separate paths (§5.1's "three spatial
        // paths must remain conceptually separate"). Always run the
        // decorrelator (even when its blend is 0) so its ring buffer stays
        // live and doesn't replay stale audio when Width crosses back above
        // the Region B ceiling later.
        const float decorSource = 0.7f * focusedMid + 0.3f * focusedSide;
        const float decorSideRaw = decorrelatorA.process(decorSource) - decorrelatorB.process(decorSource);

        // §8.2: Voice A/B always run (off the focused Mid), gated into the
        // output by doubleSideAmount/doubleMidAmount - same always-live-ring
        // reasoning as the decorrelator above, so re-engaging Double after
        // it's been at 0 doesn't replay stale delay-line content.
        const float voiceA = adtVoiceA.process(focusedMid, tightness01, transientRisk);
        const float voiceB = adtVoiceB.process(focusedMid, tightness01, transientRisk);
        adtMaxPitchShiftCentsObserved = juce::jmax(adtMaxPitchShiftCentsObserved,
            adtVoiceA.lastInstantaneousPitchShiftCents(), adtVoiceB.lastInstantaneousPitchShiftCents());
        const float doubleMid  = 0.5f * (voiceA + voiceB);
        const float doubleSide = 0.5f * (voiceA - voiceB);

        // Generated content is a fixed, deterministic function of Mid, so it
        // can end up correlated with Mid rather than truly decorrelated -
        // biasing energy toward one output channel (L=Mid+Side, R=Mid-Side)
        // for a whole session. Two layers of defence, in order:
        // (1) SideOrthogonalizer (prototype, 2026-08-07 phase 2): a sparse
        //     adaptive NLMS predictor removes ONLY the Mid-derived component
        //     of the raw generated Side, sample by sample - keeps the rest
        //     of the generated content (the actually useful spatial
        //     information) intact, unlike a blanket scale-down.
        // (2) ContentPredictabilityRestraint: secondary/emergency guardrail
        //     for whatever correlation the orthogonaliser doesn't catch
        //     (its own header comment explains why it restrains rather than
        //     tries to cancel - it's now a safety net, not the primary
        //     mechanism).
        const float rawGenerated = decorSideRaw * decorBlend + doubleSide * doubleSideAmount;
        const float orthogonalized = sideOrthogonalizer.process(focusedMid, rawGenerated,
            adtVoiceA.currentDelayMs(), adtVoiceB.currentDelayMs(), transientRisk);
        const float generatedRaw = orthogonalized * predictabilityRestraint;

        // Lag-aware correlation accumulation: write this sample's Mid into
        // the ring buffer, then read it back at each lag to compare against
        // generatedRaw. Same-sample-only correlation misses a delayed copy
        // of Mid (the ADT voice's own delay-line output) - see
        // ContentPredictabilityRestraint's header comment.
        midHistory[static_cast<size_t>(midHistoryWritePos)] = mid;
        for (size_t k = 0; k < predictabilityLagSamples.size(); ++k) {
            int idx = midHistoryWritePos - predictabilityLagSamples[k];
            if (idx < 0)
                idx += midHistorySize;
            const float midLagged = midHistory[static_cast<size_t>(idx)];
            sumMidLagTimesGenerated[k] += static_cast<double>(midLagged) * generatedRaw;
            sumMidLagSquared[k] += static_cast<double>(midLagged) * midLagged;
        }
        midHistoryWritePos = (midHistoryWritePos + 1) % midHistorySize;
        sumGeneratedSquared += static_cast<double>(generatedRaw) * generatedRaw;

        // §10.3: always-on sub-bass protection on GENERATED content only -
        // independent of Focus, applies to the decorrelated + doubled Side
        // energy before it joins the dry Region A/B side.
        const float generatedSide = subBassProtect.process(generatedRaw);

        float side = sideOriginal * sideScale * sideGain;
        side += generatedSide;
        const float midOut = mid + doubleMid * doubleMidAmount;

        const float outL = (midOut + side) * kInvSqrt2 * compGain;
        const float outR = (midOut - side) * kInvSqrt2 * compGain;
        left[i]  = outL;
        right[i] = outR;
        outEnergySum += static_cast<double>(outL) * outL + static_cast<double>(outR) * outR;
        const double outMono = static_cast<double>(outL) + outR;
        outMonoSumSquared += outMono * outMono;
    }

    // §12: bounded, slow makeup gain for the NEXT block - see
    // VxWidthLoudnessCompensator.h for why this is delayed feedback, not a
    // same-block correction.
    const double inRms = std::sqrt(inEnergySum / (2.0 * numSamples));
    const double outRms = std::sqrt(outEnergySum / (2.0 * numSamples));
    loudnessCompensator.updateForNextBlock(inRms, outRms, numSamples);

    float maxAbsRho = 0.0f;
    for (size_t k = 0; k < predictabilityLagSamples.size(); ++k) {
        const double denom = sumMidLagSquared[k] * sumGeneratedSquared;
        if (denom < 1.0e-12)
            continue;
        const float rho = juce::jlimit(-1.0f, 1.0f,
            static_cast<float>(sumMidLagTimesGenerated[k] / std::sqrt(denom)));
        maxAbsRho = juce::jmax(maxAbsRho, std::abs(rho));
    }
    contentPredictabilityRestraint.updateForNextBlock(maxAbsRho, numSamples);
    monoDownmixGuardrail.updateForNextBlock(inMonoSumSquared, outMonoSumSquared, numSamples);

    outputTrimmer.process(buffer, currentSampleRateHz);
}

#if !defined(VXSTUDIO_DISABLE_PLUGIN_ENTRYPOINT)
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new VXWidthAudioProcessor();
}
#endif
