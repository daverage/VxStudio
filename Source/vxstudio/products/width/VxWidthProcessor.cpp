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

// Region A (narrow, Width -100..0): SideOut = SideIn * sideScale, 1 at 0, 0 at -100.
// Perceptually smooth curve per spec §7.1 rather than a raw linear map.
float narrowSideScale(const float widthNegative01) noexcept {
    // widthNegative01: 0 at Width=0, 1 at Width=-100.
    const float shaped = std::pow(1.0f - widthNegative01, 1.6f);
    return juce::jlimit(0.0f, 1.0f, shaped);
}

// Region B (bounded expansion): direct Side gain, capped per §7.2
// ("approximately +3 dB to +6 dB" max). Region C is the decorrelated layer
// that fills whatever gap Region B's bound can't close.
//
// Both regions are now driven by GAP-TO-TARGET (VX_WIDTH_ENGINE_UPGRADE.md
// §5/§7), not by the raw requested Width value: widthGapPercent below is
// (1 - estimatedInputWidth01) * widthPositivePercent - "how much of the
// remaining distance to max practical width is being requested." For mono
// input estimatedInputWidth01≈0, so widthGapPercent≈widthPositivePercent and
// behaviour is unchanged from before this rebuild (§3's full-range mono
// mapping). For material that's already wide, the same requested Width
// percent produces a much smaller gap - satisfying §16's "already-wide
// material must remain safe, only a small change" requirement structurally,
// not via a special-cased threshold.
constexpr float kRegionBCeiling = 35.0f;
constexpr float kRegionBMaxGainDb = 4.5f;

// Phase 1.2 (VX_WIDTH_ENGINE_1.2.md §2-§4): the fixed 35%/4.5dB ceiling
// above is the PROVEN MONO law and must stay exactly as-is for mono-like
// input (§1's hard requirement). For content that already reads as
// genuinely stereo, that same ceiling was the plateau bug - Region B
// saturated by widthGapPercent=35 (often around Width+50 on real stereo
// material) and only Region C's decorrelated layer continued growing,
// which doesn't read as "the existing image getting wider" (§3).
//
// Fix: extend the existing-Side expansion law itself across (almost) the
// full 0..100 gap domain for stereo-like content, continuously blended by
// stereoEvidence (§4) so there's no hard branch and mono is untouched at
// stereoEvidence=0. Ceiling stops short of 100 (kStereoSideCeiling=85) so
// Region C still has a supplementary 15-point band to fill per §7 - it's
// no longer asked to carry everything above 35.
constexpr float kStereoSideCeiling = 85.0f;
constexpr float kStereoSideMaxGainDb = 9.0f;

// stereoEvidence: 0 for mono-like input, 1 once the input's own estimated
// width clearly reads as stereo. Built from analysis.estimatedWidth01
// (W0), which is already ~0 for true-mono material and rises with genuine
// stereo content - reusing it here (rather than a second classifier) keeps
// this consistent with the same W0 that already drives widthGapPercent.
constexpr float kStereoEvidenceLowW0 = 0.05f;
constexpr float kStereoEvidenceHighW0 = 0.35f;

float stereoEvidenceFromWidth01(const float estimatedInputWidth01) noexcept {
    return juce::jlimit(0.0f, 1.0f,
        (estimatedInputWidth01 - kStereoEvidenceLowW0) / (kStereoEvidenceHighW0 - kStereoEvidenceLowW0));
}

// Target-seeking direct-Side solver (design brief, 2026-08-07 - see
// tasks/todo.md). Maps target width01 -> target Side/Mid ratio. Linear
// against the same practical-maximum-ratio constant InputAnalyser's
// sideMidWidth01 already normalises against (its local kMaxPracticalSideMidRatio
// = 1.0) so R0 and the target stay on one consistent scale rather than
// inventing a second one. A non-linear curve was explicitly invited but not
// evidenced yet (no listening session available this pass); linear is the
// simplest choice that is monotonic and bounded, picked as the starting
// point pending a real listening pass (see tasks/todo.md report).
constexpr float kMaxPracticalSideMidRatio = 1.0f;
float targetRatioForWidth(const float width01) noexcept {
    return juce::jlimit(0.0f, 1.0f, width01) * kMaxPracticalSideMidRatio;
}

// Upper bound on the direct existing-Side gain the solver may request.
// VX_ENGINE_AUDIT.md §9/§10 (final control-ownership pass, 2026-08-08):
// extended the original 9/12/15dB comparison to 9/12/15/18dB - see
// VXWidthShellCheck.cpp's [gain-limit sweep] diagnostic for the full
// measured numbers. On moderate-stereo material (sideGain=0.30), width
// keeps growing with NO plateau all the way to 18dB (w100: 9dB=0.1749,
// 12dB=0.1944, 15dB=0.2194, 18dB=0.2367 - each step is a real, non-marginal
// gain, not diminishing returns). Critically, PhaseRiskGuardrailWidth's
// restraint at Width=100 was measured IDENTICAL across all four ceilings
// for every fixture tested (narrow/moderate/wide) - raising the ceiling
// bought more width without the new safety system reading any additional
// danger. That is exactly §9's stated philosophy: "allow the solver enough
// authority to fulfil the Width request; let actual safety systems
// constrain pathological outcomes" - not stopping at an arbitrary number.
// Raised to 18dB, the full extent measured. Still provisional: this is the
// measurable half of the experiment (monotonicity, width reached, safety
// engagement); the perceptual half (image quality, hollowness, mono
// compatibility by ear at the top of this range) has not been confirmed by
// a real listening pass in this environment - revisit if that surfaces an
// issue this measurement couldn't catch.
constexpr float kDirectSideGainCeilingDb = 18.0f;
constexpr float kR0Epsilon = 1.0e-4f;

// Asymmetric block-rate smoothing for the solver-driven direct-Side gain
// (§12): a distinct time constant from InputAnalyser's ~450ms width-estimate
// constant (deliberately not reused, per §12), slower on release than
// attack so a momentarily-dropping required gain doesn't audibly duck the
// image while genuinely growing stereo content isn't kept waiting too long.
constexpr float kDirectGainAttackSeconds = 0.35f;
constexpr float kDirectGainReleaseSeconds = 0.6f;

// Lag-aware predictability correlation (§11.3): checked at these lags, not
// same-sample only - a delayed copy of Mid (exactly what the ADT voice's
// delay line produces) can be entirely derived from Mid while reading
// near-zero correlation at lag 0.
constexpr float kPredictabilityLagsMs[] = { 0.0f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f };
constexpr float kPredictabilityMaxLagMs = 16.0f;

// monoRiskRestraint: 0 = fully restrained (no boost applied, e.g. broadband
// correlation at -1/anti-phase), 1 = unrestrained (correlation at +1/safe).
// §7.2: "restrained by mono-risk analysis" - a phase-risky signal should not
// have its Side energy boosted further. Takes widthGapPercent (see comment
// above kRegionBCeiling), not the raw requested Width.
float expandedSideGain(const float widthGapPercent, const float monoRiskRestraint,
                        const float ceiling, const float maxGainDb) noexcept {
    const float clamped = juce::jlimit(0.0f, ceiling, widthGapPercent);
    const float gainDb = (clamped / ceiling) * maxGainDb * juce::jlimit(0.0f, 1.0f, monoRiskRestraint);
    return juce::Decibels::decibelsToGain(gainDb);
}

// Region C (decorrelated widening): blend ramps 0..1 across the region so the
// Region B -> Region C handover is continuous (§4.1 "the transition between
// regions must be continuous and inaudible"). kRegionCMaxLevel=1.0 so a mono
// source at full gap (widthGapPercent=100, i.e. Width=+100) can reach a
// decorrelated Side level on par with Mid - §3/§8's "generated pair reaches
// the left/right extremes" endpoint - rather than being capped short of it.
constexpr float kRegionCCeiling = 100.0f;
constexpr float kRegionCMaxLevel = 1.0f;
constexpr float kDecorrelatorWindowMs = 1.2f;

float regionCBlend(const float widthGapPercent, const float ceiling) noexcept {
    if (widthGapPercent <= ceiling)
        return 0.0f;
    const float t = (widthGapPercent - ceiling) / (kRegionCCeiling - ceiling);
    return juce::jlimit(0.0f, 1.0f, t);
}

// §3-8 of the final control-ownership audit (2026-08-08): ADT spatial
// separation must be a function of WIDTH, not of Double amount. Previously
// doubleSide's placement was fixed (always 0.5*(A-B)), so raising Double
// alone widened the image - Width owned nothing about ADT geometry. This
// returns 0..1: how far apart the generated A/B voices should sit,
// independent of Double/Tightness/Focus (only widthSigned is read).
//
// Monotonic and continuous through 0 by construction (both branches meet at
// kAdtSeparationAtZeroWidth). Negative Width pulls voices toward centre
// (reaching exactly 0 - fully collapsed/mono - at Width=-100, matching
// "generated performances collapse toward centre/mono"). Positive Width
// pushes them apart, reaching 1 (today's original always-on 0.5/0.5 split)
// at Width=+100. At Width=0 the voices stay only MINIMALLY separated
// (§6: "remain centred or minimally separated"), not fully collapsed - a
// literal 0 here would make Double sound identical at every negative-to-
// zero Width setting, which isn't what "minimally separated" asks for.
constexpr float kAdtSeparationAtZeroWidth = 0.15f;

float adtSeparationForWidth(const float widthSigned) noexcept {
    if (widthSigned >= 0.0f)
        return kAdtSeparationAtZeroWidth + (1.0f - kAdtSeparationAtZeroWidth) * juce::jlimit(0.0f, 1.0f, widthSigned / 100.0f);
    return kAdtSeparationAtZeroWidth * (1.0f - juce::jlimit(0.0f, 1.0f, -widthSigned / 100.0f));
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
    id.tertiaryHint   = "How closely the generated performance follows the original - loose and separate toward 0%, tight and precise toward 100%.";
    id.quaternaryHint = "Where in the spectrum the doubling effect concentrates - body, full range, or air.";
    id.dspVersion    = vxsuite::versions::plugins::width;
    id.helpTitle     = vxsuite::help::width.title;
    id.helpHtml      = vxsuite::help::width.html;
    id.readmeSection = vxsuite::help::width.readmeSection;
    // Width: centred -100..+100, default 0 (Original).
    id.primaryDefaultValue   = 0.5f;
    // Double: 0..100, default 0.
    id.secondaryDefaultValue = 0.0f;
    // Tightness: 0..100, default 60 (spec §4.3). User-facing knob convention
    // inverted 2026-08-07 (user feedback: 0%=loosest/100%=tightest read
    // backwards against the control's own name) - 100% now means "tight",
    // 0% means "loose", matching kTightnessLabel. The underlying DSP call
    // (AdtVoice::process, VxWidthAdtVoice.h) still expects its OWN
    // internal 0=Tight/1=Loose convention unchanged - see the
    // `1.0f - smoothed.tertiary` inversion at the call site in
    // processProduct(). This default (0.60) is the OLD default (0.40)
    // inverted, so the actual DSP behaviour at the factory default is
    // unchanged - only the knob's own labelling/direction flipped.
    id.tertiaryDefaultValue  = 0.60f;
    // Focus: 0..100, default 55 (spec §4.4).
    id.quaternaryDefaultValue = 0.55f;
    id.theme.accentRgb     = { 0.20f, 0.78f, 0.72f };
    id.theme.accent2Rgb    = { 0.05f, 0.13f, 0.12f };
    id.theme.backgroundRgb = { 0.04f, 0.07f, 0.07f };
    id.theme.panelRgb      = { 0.08f, 0.12f, 0.12f };
    id.theme.textRgb       = { 0.90f, 0.98f, 0.96f };
    id.requiresSignalQuality = true; // side/mid mono score feeds InputAnalyser (§6.1).
    id.showSpatialWidthVisualizer = true; // Phase 2 spatial-width UI (VX_WIDTH_ENGINE_UPGRADE.md).

    // Micro-pitch (2026-08-07): graduated from an experimental, user-facing
    // toggle to a permanent, always-on part of the ADT engine - it's a nice
    // effect and there's no longer a reason to gate it behind a control.
    // No simpleToggleParamId set (== no UI checkbox, no APVTS parameter -
    // see ProductIdentity::supportsSimpleToggle()); setAdtMicroPitchEnabled
    // is instead called unconditionally in prepareSuite()/resetSuite(). See
    // AdtVoice::setMicroPitchEnabled()'s comment for what this actually does.

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
    // Tertiary (Tightness) values below are the pre-inversion values
    // (0=Tight/1=Loose) flipped via (1.0-old) so the actual DSP sound each
    // preset produces is UNCHANGED by the 2026-08-07 knob-direction fix -
    // only the stored number's meaning changed, not the audible result.
    id.presetPrimaryValues[0]   = 0.0f;
    id.presetSecondaryValues[0] = 0.0f;
    id.presetTertiaryValues[0]  = 0.60f;
    id.presetQuaternaryValues[0] = 0.55f;
    // Width=+15 (Region B, conservative expansion).
    id.presetPrimaryValues[1]   = 0.5f + 15.0f / 200.0f;
    id.presetSecondaryValues[1] = 0.0f;
    id.presetTertiaryValues[1]  = 0.60f;
    id.presetQuaternaryValues[1] = 0.55f;
    // Width=+60 (into Region C decorrelated widening).
    id.presetPrimaryValues[2]   = 0.5f + 60.0f / 200.0f;
    id.presetSecondaryValues[2] = 0.0f;
    id.presetTertiaryValues[2]  = 0.60f;
    id.presetQuaternaryValues[2] = 0.55f;
    // Width=0, Double engaged, tighter/precise, Focus toward Air.
    id.presetPrimaryValues[3]   = 0.5f;
    id.presetSecondaryValues[3] = 0.55f;
    id.presetTertiaryValues[3]  = 0.65f;
    id.presetQuaternaryValues[3] = 0.65f;
    // Width mild, Double strong, Tightness loose (separate-take feel).
    id.presetPrimaryValues[4]   = 0.5f + 10.0f / 200.0f;
    id.presetSecondaryValues[4] = 0.70f;
    id.presetTertiaryValues[4]  = 0.25f;
    id.presetQuaternaryValues[4] = 0.55f;
    // Width=+100 (max), Double moderate.
    id.presetPrimaryValues[5]   = 1.0f;
    id.presetSecondaryValues[5] = 0.30f;
    id.presetTertiaryValues[5]  = 0.60f;
    id.presetQuaternaryValues[5] = 0.55f;

    return id;
}

std::optional<vxsuite::SpatialWidthTelemetry> VXWidthAudioProcessor::getSpatialWidthTelemetry() const noexcept {
    vxsuite::SpatialWidthTelemetry t;
    t.monoConfidence01 = monoConfidence01State;
    t.estimatedInputWidth01 = estimatedInputWidth01State;
    t.requestedOutputWidth01 = requestedOutputWidth01State;
    t.actualOutputWidth01 = actualOutputWidth01State;
    t.doubleAmount01 = vxsuite::readNormalized(parameters, productIdentity.secondaryParamId, 0.0f);
    t.tightness01 = vxsuite::readNormalized(parameters, productIdentity.tertiaryParamId, 0.60f);
    t.focus01 = vxsuite::readNormalized(parameters, productIdentity.quaternaryParamId, 0.55f);
    t.hasSignal = hasSignalState;
    return t;
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
    subBassProtectWidth.prepare(currentSampleRateHz, 80.0f);
    subBassProtectDouble.prepare(currentSampleRateHz, 80.0f);
    loudnessCompensator.prepare(currentSampleRateHz);
    contentPredictabilityRestraintWidth.prepare(currentSampleRateHz);
    contentPredictabilityRestraintDouble.prepare(currentSampleRateHz);
    monoDownmixGuardrail.prepare(currentSampleRateHz);
    sideOrthogonalizerWidth.prepare(currentSampleRateHz);
    sideOrthogonalizerDouble.prepare(currentSampleRateHz);
    phaseRiskGuardrailWidth.prepare(currentSampleRateHz);
    phaseRiskGuardrailDouble.prepare(currentSampleRateHz);
    harmonicResidualAnalyser.prepare(currentSampleRateHz);
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
    // Permanent, always-on (2026-08-07 - see makeIdentity()'s comment);
    // AdtVoice::reset() doesn't touch this flag itself, so re-assert it
    // here rather than relying on construction-time defaults surviving
    // every reset path.
    setAdtMicroPitchEnabled(true);
    focusTiltMid.reset();
    subBassProtectWidth.reset();
    subBassProtectDouble.reset();
    loudnessCompensator.reset();
    contentPredictabilityRestraintWidth.reset();
    contentPredictabilityRestraintDouble.reset();
    monoDownmixGuardrail.reset();
    sideOrthogonalizerWidth.reset();
    sideOrthogonalizerDouble.reset();
    phaseRiskGuardrailWidth.reset();
    phaseRiskGuardrailDouble.reset();
    harmonicResidualAnalyser.reset();
    adtMaxPitchShiftCentsObserved = 0.0f;
    estimatedInputWidth01State = 0.0f;
    requestedOutputWidth01State = 0.0f;
    actualOutputWidth01State = 0.0f;
    widthPathOutputRatioState = 0.0f;
    doubleMidRmsState = 0.0f;
    doubleSideRmsState = 0.0f;
    safetyRestraintAmountState = 0.0f;
    monoConfidence01State = 0.0f;
    hasSignalState = false;
    inputSideMidRatioState = 0.0f;
    directSideGainSmoothedState = 1.0f;
    directStereoWeightState = 0.0f;
    remainingWidthGapState = 0.0f;
    targetSideMidRatioState = 0.0f;
    limitedSideGainDbState = 0.0f;
    std::fill(midHistory.begin(), midHistory.end(), 0.0f);
    midHistoryWritePos = 0;
    const float width     = vxsuite::readNormalized(parameters, productIdentity.primaryParamId,   0.5f);
    const float doubleAmt = vxsuite::readNormalized(parameters, productIdentity.secondaryParamId, 0.0f);
    const float tightness = vxsuite::readNormalized(parameters, productIdentity.tertiaryParamId,  0.60f);
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
    const float tightTarget  = vxsuite::readNormalized(parameters, productIdentity.tertiaryParamId,  0.60f);
    const float focusTarget  = vxsuite::readNormalized(parameters, productIdentity.quaternaryParamId, 0.55f);

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
    // §11 full guardrail (VXWIDTH_BUILD.md, 2026-08-08) - see
    // PhaseRiskGuardrail's own comment for what each covers.
    const float phaseRiskRestraintWidth = phaseRiskGuardrailWidth.currentRestraint();
    const float phaseRiskRestraintDouble = phaseRiskGuardrailDouble.currentRestraint();

    // §9: reduce generated Side/decorrelator contribution during transients
    // (Voice A/B already reduce their own level internally - this covers
    // Region B/C, which didn't have any transient protection before now).
    // Coefficient lowered 0.5->0.3 (VX_ENGINE_AUDIT.md §11, final control-
    // ownership pass, 2026-08-08): the role of transient protection is
    // "preserve localisation, avoid smearing attacks" - it should not
    // routinely mean the user gets half their requested Width on every
    // onset. 0.3 caps the maximum cut at 30% (was 50%) at full transient
    // risk. This is the measurable half of the candidate comparison (0.5/
    // 0.3/0.2, i.e. worst-case remaining protect of 0.50/0.70/0.80) - the
    // perceptual half (drums, acoustic/picked guitar, piano, consonant-
    // heavy vocals - does attack clarity survive at this shallower depth)
    // needs a real listening pass to fully confirm.
    const float transientProtect = 1.0f - 0.3f * juce::jlimit(0.0f, 1.0f, voice.transientRisk);

    // §8.8 harmonic/residual/transient decomposition (VXWIDTH_BUILD.md,
    // 2026-08-08): §8.8 explicitly says this "does not require source
    // separation or machine learning" and lists lightweight candidate
    // methods - a full STFT-based harmonic-percussive mask would need new
    // FFT/overlap-add infrastructure and a latency budget §13's Live mode
    // doesn't have, so this uses the lightweight path instead: block-rate
    // normalised autocorrelation (HarmonicResidualAnalyser, fed by
    // accumulateSample() in the loop below) for the harmonic/residual axis,
    // combined with the framework's own transient-risk analysis. Feed-
    // forward/one-block-delayed like every other analysis-driven value in
    // this function - reads THIS block's harmonicWeight/residualWeight/
    // transientWeight as measured up to the END of the PREVIOUS block.
    //
    // §8.8's processing table: harmonic content gets "limited decorrelation
    // ... protect fundamentals" (factor <1); residual gets "stronger
    // decorrelation ... more width" (factor >1); transient is already
    // separately protected via transientProtect above, so its own factor
    // here stays neutral (1.0) rather than double-restraining it.
    // Harmonic factor raised 0.5->0.7 (VX_ENGINE_AUDIT.md §12, final
    // control-ownership pass, 2026-08-08): 0.5 is a MAJOR authority cut
    // (strongly tonal material got only half the requested Region C
    // contribution before any other restraint applied). §12's own framing:
    // "the question is how much protection is genuinely needed before
    // tonal material becomes phasey/hollow" - not an assumption that
    // fundamentals always need a 50% cut. 0.7 is the measurable middle of
    // the requested 0.5/0.65/0.75/1.0 comparison; the perceptual
    // confirmation (does pure-tonal content stay clean at this shallower
    // protection depth) needs a real listening pass.
    constexpr float kHarmonicDecorFactor = 0.7f;
    constexpr float kResidualDecorFactor = 1.3f;
    constexpr float kTransientDecorFactor = 1.0f;
    const float harmonicResidualDecorFactor =
        harmonicResidualAnalyser.harmonicWeight() * kHarmonicDecorFactor
        + harmonicResidualAnalyser.residualWeight() * kResidualDecorFactor
        + harmonicResidualAnalyser.transientWeightValue() * kTransientDecorFactor;

    // §5/§7: Width is a request RELATIVE TO the estimated current width
    // (analysis.estimatedWidth01), not an absolute gain on the slider value.
    // widthGapPercent = "how much of the remaining distance to max practical
    // width (1 - W0) is this request asking for" - see the comment above
    // kRegionBCeiling for why this collapses to the old widthPositivePercent
    // behaviour for mono input (W0≈0) and shrinks toward 0 for already-wide
    // stereo input (W0≈1), satisfying §16's mono-endpoint and
    // already-wide-stays-safe requirements from one rule.
    //
    // IMPORTANT (product decision, see EditorBase.cpp's applyWidthKnobRangeForCurrentMode):
    // the stored parameter's bipolar meaning (v=0.5 is ALWAYS an exact
    // no-op, regardless of mono/stereo classification) is a foundational,
    // regression-tested invariant that must never be conditioned on
    // classification. Mono's "0..100, full physical sweep" presentation is
    // achieved entirely in the UI by remapping the KNOB's interactive
    // range to the parameter's existing upper half [0.5,1.0] - the DSP
    // here stays exactly as Phase 1/1.1 left it, unconditional on
    // monoConfidence. (An earlier attempt blended this per-block by
    // monoConfidence and broke the neutral-null invariant for ALL content,
    // since monoConfidence is a continuous, never-exactly-zero measurement
    // - reverted.)
    const float widthGapPercent = widthSigned > 0.0f
        ? (1.0f - analysis.estimatedWidth01) * widthSigned
        : 0.0f;

    const float sideScale = widthSigned < 0.0f
        ? narrowSideScale(-widthSigned / 100.0f)
        : 1.0f;

    // §4/§9: continuous mono<->stereo blend, using this block's own
    // estimated input width as stereoEvidence (== directStereoWeight, §9 -
    // the same continuous evidence answers both "how mono-like is this" and
    // "how eligible is the direct-Side solver"). At stereoEvidence=0 the
    // blends below resolve to bit-for-bit the original mono law - required
    // by §1 - because they now interpolate between the UNCHANGED mono-law
    // GAIN/BLEND VALUES and the new solver's values, rather than blending
    // the old law's ceiling/max-gain constants the way Phase 1.2 did.
    const float stereoEvidence = stereoEvidenceFromWidth01(analysis.estimatedWidth01);

    // T (§4): target width01 this request is asking for, relative to W0.
    // Computed once here and reused for requestedOutputWidth01State below so
    // the two definitions can't drift apart.
    const float targetWidth01 = widthSigned >= 0.0f
        ? juce::jlimit(0.0f, 1.0f, analysis.estimatedWidth01 + (1.0f - analysis.estimatedWidth01) * (widthSigned / 100.0f))
        : juce::jlimit(0.0f, 1.0f, analysis.estimatedWidth01 * (1.0f - (-widthSigned) / 100.0f));

    // Mono-law component: Region B's existing-Side gain law is unchanged
    // (§1 hard invariant) - still evaluated at the fixed mono ceiling/max-gain.
    //
    // Region C's mono blend is DELIBERATELY no longer gated behind that same
    // 35% ceiling (user report, 2026-08-07): for bit-exact dual-mono input,
    // sideOriginal is exactly 0, so Region B's gain has literally nothing to
    // amplify - Region B contributes ZERO regardless of its own ceiling. That
    // left Region C (the only path able to generate width from nothing) as
    // the sole source of any change on true mono, but it was gated to not
    // start until widthGapPercent>35 - a measured, confirmed dead zone
    // (VXWidthShellCheck.cpp's [near-mono visualizer sensitivity]
    // diagnostic: diffVsWidth0Rms==0.0 exactly for widths 0/10/25/35 on
    // bit-exact mono). Using ceiling=0 here makes Region C ramp linearly
    // and continuously from Width=0 (still an exact null AT 0 - regionCBlend
    // returns 0 only when widthGapPercent<=ceiling, so widthGapPercent=0
    // still yields 0) instead of sitting inert for the first third of the
    // knob's range.
    const float monoLawSideGain = expandedSideGain(widthGapPercent, monoRiskRestraint, kRegionBCeiling, kRegionBMaxGainDb);
    constexpr float kMonoRegionCCeiling = 0.0f;
    const float monoLawDecorBlendFraction = regionCBlend(widthGapPercent, kMonoRegionCCeiling);

    // Target-ratio solver (§6-§11): R0 is LAST block's measured input
    // Side/Mid ratio (inputSideMidRatioState, updated at the end of this
    // function) - feed-forward, block-rate, not sample-chasing (§13).
    const float targetSideMidRatio = targetRatioForWidth(targetWidth01);
    const float requiredGain = targetSideMidRatio / juce::jmax(inputSideMidRatioState, kR0Epsilon);
    const float ceilingGain = juce::Decibels::decibelsToGain(directSideGainCeilingDbState);
    const float boundedGain = juce::jlimit(1.0f, ceilingGain, requiredGain);
    // §11: phase risk restrains the ALLOWED gain, not the target itself -
    // same restrain-the-dB pattern expandedSideGain() already uses for the
    // mono law, so both paths respond to phase risk identically.
    const float restrainedGainDb = juce::Decibels::gainToDecibels(boundedGain) * juce::jlimit(0.0f, 1.0f, monoRiskRestraint);
    const float solverGainThisBlock = juce::Decibels::decibelsToGain(restrainedGainDb);

    // §12: asymmetric attack/release smoothing, always updated (even while
    // stereoEvidence is currently low) so the smoothed value doesn't jump
    // when evidence rises later - same "always-live" reasoning the
    // decorrelator/ADT voices already use elsewhere in this function.
    const bool solverGainRising = solverGainThisBlock > directSideGainSmoothedState;
    const float directGainTauSeconds = solverGainRising ? kDirectGainAttackSeconds : kDirectGainReleaseSeconds;
    const float directGainAlpha = std::exp(-static_cast<float>(numSamples) / (directGainTauSeconds * static_cast<float>(currentSampleRateHz)));
    directSideGainSmoothedState = directGainAlpha * directSideGainSmoothedState + (1.0f - directGainAlpha) * solverGainThisBlock;

    // §14: Region C now supplements whatever the direct-Side solver couldn't
    // reach, instead of picking up everything above a fixed ceiling.
    const float directAchievedWidth01 = juce::jlimit(0.0f, 1.0f,
        (inputSideMidRatioState * directSideGainSmoothedState) / kMaxPracticalSideMidRatio);
    const float remainingWidthGap = juce::jmax(0.0f, targetWidth01 - directAchievedWidth01);

    // §11.3 "reduce direct Side expansion third": phaseRiskRestraintWidth
    // blends the gain toward UNITY (not toward 0) as it restrains, same
    // amplitude-domain pattern the solver's own monoRiskRestraint scaling
    // already uses - a gain that MULTIPLIES Side should restrain toward
    // "no change", not toward "mute".
    const float rawSideGain = widthSigned > 0.0f
        ? juce::jmap(stereoEvidence, monoLawSideGain, directSideGainSmoothedState) * transientProtect
        : 1.0f;
    const float sideGain = 1.0f + (rawSideGain - 1.0f) * phaseRiskRestraintWidth;
    // Region C blend/level, restrained by mono-risk. The blend FRACTION
    // interpolates between the untouched mono law (§1) and the residual-
    // gap-driven fraction (§14), by the same stereoEvidence.
    //
    // NOT restrained by monoDownmixRestraint (removed 2026-08-07,
    // VX_ENGINE_AUDIT.md §11/§12/§15 audit): outL+outR = 2*midOut*k*compGain
    // algebraically - Side cancels exactly in that sum for ANY Side value,
    // by construction of the Mid/Side encoding (L=Mid+Side, R=Mid-Side).
    // MonoDownmixGuardrail measures exactly that (L+R) sum, so it can only
    // ever be responding to changes in midOut (i.e. Double's doubleMid
    // contribution) - it was mathematically incapable of ever detecting
    // anything Region C's decorBlend (a pure Side-domain quantity) did, so
    // gating decorBlend by it was measuring one thing and restraining
    // another. Worse: this made Region C's OWN amount silently move
    // whenever DOUBLE's midOut degraded the mono downmix (e.g. via
    // Tightness/Focus changing the ADT voices) - exactly the indirect
    // Double->Width coupling the audit's Test 3/4 checks for. Moved to
    // doubleMidAmount below, the one quantity that can actually affect
    // what this guardrail measures.
    const float decorBlendFraction = widthSigned > 0.0f
        ? juce::jmap(stereoEvidence, monoLawDecorBlendFraction, remainingWidthGap)
        : 0.0f;
    const float decorBlend = decorBlendFraction * monoRiskRestraint * phaseRiskRestraintWidth
        * kRegionCMaxLevel * transientProtect * harmonicResidualDecorFactor;

    // §10: Focus -100(Body)..+100(Air) as -6..+6dB tilt on the ADT (Double)
    // voices' source only - see the "Focus/Tightness are Double-only
    // controls" comment below for why this no longer includes Width's own
    // decorrelator (Region C runs off the untilted Mid/Side; only Double's
    // ADT voices see the Focus-tilted signal). Focus steers where the
    // DOUBLING effect concentrates without colouring the underlying signal
    // or moving Width's own output.
    const float focusTiltDb = (smoothed.quaternary - 0.5f) * 2.0f * 6.0f;

    // §8: Double 0..100 -> generated A/B amount (Width/the crossfade below
    // decides Mid vs Side split; Tightness 0..100 -> 0..1 into the voice's
    // own Tight/Natural/Loose band interpolation (§8.3/§8.7)).
    //
    // Centre-confidence amplitude gate REMOVED (2026-08-08, final control-
    // ownership audit §14): this used to scale doubleCentreGate down for
    // wide/panned material (originally to bias Double toward "suitable"
    // centred sources like vocals/snares over an already-wide mix). Tested
    // removing it entirely (floor raised to 1.0, i.e. no-op, then deleted):
    // the [Double centre-confidence gate] regression - hard-panned material
    // still doubles measurably less than centred material - CONTINUES TO
    // PASS, more strongly than before (pannedDiff/centredDiff ratio ~0.50
    // vs the ~0.6 threshold, previously achieved by the gate directly).
    // That differentiation now emerges from phaseRiskGuardrailDouble's
    // centre-image-displacement measurement instead: de-panning a hard-
    // panned source by adding centred generated content is exactly what
    // that measurement is designed to catch, and it's a real, evidenced
    // safety signal (§18's "significant centre displacement") rather than
    // a confidence-based blanket amplitude cut. L/R imbalance also measured
    // BETTER without the gate (doubleAvg 0.003 vs a pre-removal ~0.02-0.03)
    // - no measurable safety benefit was lost, so per §14's own criterion
    // ("if no meaningful safety benefit exists, remove the amplitude gate")
    // this is removed rather than kept as a redundant, weaker duplicate of
    // a real safety system. The Double knob is now the sole authority over
    // Double amount, as §16/§24 require.
    // fixed 0.35 doubleMidAmount cap ("Mid amount capped at a smaller
    // fraction so the original Mid must remain dominant") predates Width
    // owning ADT Mid/Side distribution and directly conflicts with it - a
    // fixed 35% Mid ceiling would still mean "Width=0, Double=100" sounds
    // thin no matter how much the NEW equal-power crossfade below (driven
    // by Width) wants to put into Mid at low Width. Replaced with ONE
    // shared amount: Double controls how much A/B energy exists at all;
    // the crossfade alone controls how that energy splits Mid vs Side.
    // Each domain keeps only its OWN genuine safety restraint (not an
    // arbitrary asymmetric cap) - monoDownmixRestraint on Mid (the only
    // thing it can mathematically detect, see decorBlend's comment above),
    // phaseRiskRestraintDouble on Side (§11.3 "reduce generated DoubleSide").
    const float doubleAmount = smoothed.secondary;
    const float doubleSideAmount = doubleAmount * phaseRiskRestraintDouble;
    const float doubleMidAmount = doubleAmount * monoDownmixRestraint;
    static constexpr float kInvSqrt2 = 0.70710678f;
    // §3-8: ADT spatial separation is a function of WIDTH alone (see
    // adtSeparationForWidth's own comment). VXWIDTH_AUDIT.md #1 fix
    // (2026-08-08): this used to crossfade between "fully Mid" and "fully
    // Side" (cos/sin of separation*pi/2), which at separation=1 produced a
    // PURE Side/anti-phase signal (M=0), not actual A-left/B-right placement
    // - hard-panned A/B reconstructed through L=Mid+Side, R=Mid-Side
    // requires BOTH Mid and Side energy, not Side alone. Correct model:
    // A and B are each an independent equal-power pan (A moving centre->left,
    // B moving centre->right as separation 0->1); summing the two pans'
    // contributions into Mid/Side gives M=(A+B)*cos(separation*pi/4)/sqrt2,
    // S=(A-B)*sin(separation*pi/4)/sqrt2 (derivation: VXWIDTH_AUDIT.md #1).
    // At separation=0, M is at its LARGEST (equal-power centre pan sums
    // in-phase), S=0 - both voices genuinely centred, not just crossfaded
    // away. At separation=1, M/S carry equal energy (0.5/0.5 of A+B/A-B),
    // matching the original always-on split so Width=+100 still reproduces
    // what Double always sounded like. Verified against an explicit L/R-pan
    // reference implementation in VXWidthShellCheck.cpp's [ADT M/S vs L/R
    // pan reference] test.
    const float adtSeparation = adtSeparationForWidth(widthSigned);
    const float adtPanTheta = adtSeparation * juce::MathConstants<float>::pi * 0.25f;
    const float adtMidWeight = std::cos(adtPanTheta) * kInvSqrt2;
    const float adtSideWeight = std::sin(adtPanTheta) * kInvSqrt2;
    // Tightness knob convention inverted 2026-08-07 (0%=loose, 100%=tight,
    // matching the control's own name - was backwards before). AdtVoice::
    // process() below still expects ITS OWN unchanged internal convention
    // (0=Tight, 1=Loose - see VxWidthAdtVoice.h), so invert here at the one
    // call site rather than touching that already-tested band mapping.
    const float tightness01 = 1.0f - smoothed.tertiary;
    const float transientRisk = voice.transientRisk;

    double inEnergySum = 0.0;
    double outEnergySum = 0.0;
    double inMonoSumSquared = 0.0;
    double outMonoSumSquared = 0.0;
    // Separate lag-correlation accumulators per source (VX_ENGINE_AUDIT.md
    // §7/§11/§12) - see contentPredictabilityRestraintWidth/Double's comment.
    double sumMidLagTimesGeneratedWidth[6] = {};
    double sumMidLagSquaredWidth[6] = {};
    double sumGeneratedSquaredWidth = 0.0;
    double sumMidLagTimesGeneratedDouble[6] = {};
    double sumMidLagSquaredDouble[6] = {};
    double sumGeneratedSquaredDouble = 0.0;
    double sumOutMidSq = 0.0, sumWidthOnlySideSq = 0.0; // §17 telemetry: actualOutputWidth01 (Width-only, excludes Double)
    double sumInMidSq = 0.0, sumInSideSq = 0.0; // target-seeking engine instrumentation: R0 = sideRms/midRms of the raw input
    double sumDoubleMidSq = 0.0, sumDoubleSideSq = 0.0; // §22: DoubleMid/DoubleSide RMS telemetry
    const float compGain = loudnessCompensator.currentGain();
    const float predictabilityRestraintWidth = contentPredictabilityRestraintWidth.currentRestraint();
    const float predictabilityRestraintDouble = contentPredictabilityRestraintDouble.currentRestraint();
    const int midHistorySize = static_cast<int>(midHistory.size());

    for (int i = 0; i < numSamples; ++i) {
        const float l = left[i];
        const float r = right[i];
        inEnergySum += static_cast<double>(l) * l + static_cast<double>(r) * r;
        const double inMono = static_cast<double>(l) + r;
        inMonoSumSquared += inMono * inMono;

        const float mid  = (l + r) * kInvSqrt2;
        const float sideOriginal = (l - r) * kInvSqrt2;
        sumInMidSq += static_cast<double>(mid) * mid;
        sumInSideSq += static_cast<double>(sideOriginal) * sideOriginal;
        // §8.8: accumulate for THIS block's harmonic/residual confidence -
        // see HarmonicResidualAnalyser's own comment for why this is
        // per-sample accumulation, not a whole-buffer pass.
        harmonicResidualAnalyser.accumulateSample(mid);

        // Focus/Tightness are Double-only controls (user correction,
        // 2026-08-07: "nothing except the width control should be affecting
        // width. Focus and Tightness are part of double and should have no
        // effect on width itself"). Previously BOTH Region C's decorrelator
        // (Width) and the ADT voices (Double) ran off this same
        // Focus-tilted signal, so turning Focus changed Region C's output
        // energy even at a fixed Width (confirmed: ~1.7x width swing across
        // Focus 0->100 even after making the tilt itself energy-neutral -
        // see OnePoleTilt's comment in VxWidthSpectralShaping.h. The
        // decorrelator's sparse tap pattern is effectively a non-flat comb
        // filter, so ANY spectral reshaping of its source - not just an
        // unbalanced one - shows up as a change in its output level).
        // Fix is architectural, not a compensation gain: only the ADT
        // voices (Double) see the Focus-tilted signal now; Region C
        // (Width) runs off the untilted Mid/Side, same as Region B always
        // has, so Focus/Tightness genuinely cannot move Width's own output.
        const float focusedMid  = focusTiltMid.process(mid, focusTiltDb);

        // §5.1: the decorrelation path runs in parallel off the original
        // Mid/Side, not the already-widened Region A/B output - the two
        // regions are conceptually separate paths (§5.1's "three spatial
        // paths must remain conceptually separate"). Always run the
        // decorrelator (even when its blend is 0) so its ring buffer stays
        // live and doesn't replay stale audio when Width crosses back above
        // the Region B ceiling later.
        const float decorSource = 0.7f * mid + 0.3f * sideOriginal;
        const float decorSideRaw = decorrelatorA.process(decorSource) - decorrelatorB.process(decorSource);

        // §8.2: Voice A/B always run (off the focused Mid - Double's own
        // wet path, per the Focus-scoping note above), gated into the
        // output by doubleSideAmount/doubleMidAmount - same always-live-ring
        // reasoning as the decorrelator above, so re-engaging Double after
        // it's been at 0 doesn't replay stale delay-line content.
        const float voiceA = adtVoiceA.process(focusedMid, tightness01, transientRisk);
        const float voiceB = adtVoiceB.process(focusedMid, tightness01, transientRisk);
        adtMaxPitchShiftCentsObserved = juce::jmax(adtMaxPitchShiftCentsObserved,
            adtVoiceA.lastInstantaneousPitchShiftCents(), adtVoiceB.lastInstantaneousPitchShiftCents());
        // §3-8: Width-driven equal-power A/B pan (adtMidWeight/adtSideWeight,
        // computed once per block above, already include the 1/sqrt2
        // normalisation - see that comment) replaces the old Mid<->Side
        // crossfade - Width now owns how far apart A/B sit, Double still
        // owns how much of them exists (via doubleMidAmount/doubleSideAmount
        // below).
        const float doubleMid  = adtMidWeight * (voiceA + voiceB);
        const float doubleSide = adtSideWeight * (voiceA - voiceB);
        // §22 telemetry: Double's own actual Mid/Side contribution (post
        // doubleMidAmount/doubleSideAmount), for the ADT spatial-placement
        // regression tests.
        const float doubleMidContribution = doubleMid * doubleMidAmount;
        const float doubleSideContribution = doubleSide * doubleSideAmount;
        sumDoubleMidSq += static_cast<double>(doubleMidContribution) * doubleMidContribution;
        sumDoubleSideSq += static_cast<double>(doubleSideContribution) * doubleSideContribution;

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
        const float widthRawContribution  = decorSideRaw * decorBlend;
        const float doubleRawContribution = doubleSide * doubleSideAmount;
        // Separate orthogonalizer instances (2026-08-07, "nothing but
        // Width should affect Width" fix - see sideOrthogonalizerWidth's
        // header comment). Width's predictor uses ONLY fixed lags (dynamic
        // delay args fixed at 0,0 - it has no ADT voices to track and never
        // did), so it is structurally independent of Tightness/Focus.
        // Double's instance keeps the real ADT-tracking dynamic taps,
        // unchanged from before the split. Predictor reference is the TRUE
        // Mid signal (not focusedMid) for the Double instance too, so
        // Focus can't leak in via the predictor's own level either.
        const float widthOrthogonalized = sideOrthogonalizerWidth.process(mid, widthRawContribution,
            0.0f, 0.0f, transientRisk);
        const float doubleOrthogonalized = sideOrthogonalizerDouble.process(mid, doubleRawContribution,
            adtVoiceA.currentDelayMs(), adtVoiceB.currentDelayMs(), transientRisk);
        // Separate restraint per source (VX_ENGINE_AUDIT.md §7/§11/§12): a
        // single shared restraint scaling the combined sum would let
        // Double's own correlation-with-Mid (which Tightness/Focus can
        // change) alter the gain applied to Width's contribution too, even
        // with Width itself unchanged.
        const float generatedRawWidth = widthOrthogonalized * predictabilityRestraintWidth;
        const float generatedRawDouble = doubleOrthogonalized * predictabilityRestraintDouble;
        const float generatedRaw = generatedRawWidth + generatedRawDouble;

        // Lag-aware correlation accumulation, split per source (matches the
        // restraint split above): write this sample's Mid into the ring
        // buffer, then read it back at each lag to compare against EACH
        // source's own generated content separately. Same-sample-only
        // correlation misses a delayed copy of Mid (the ADT voice's own
        // delay-line output) - see ContentPredictabilityRestraint's header
        // comment. (Pearson correlation is scale-invariant, so measuring on
        // the pre- or post-restraint signal gives the same rho; using the
        // post-restraint value here just matches the original single-path
        // code's convention.)
        midHistory[static_cast<size_t>(midHistoryWritePos)] = mid;
        for (size_t k = 0; k < predictabilityLagSamples.size(); ++k) {
            int idx = midHistoryWritePos - predictabilityLagSamples[k];
            if (idx < 0)
                idx += midHistorySize;
            const float midLagged = midHistory[static_cast<size_t>(idx)];
            sumMidLagTimesGeneratedWidth[k] += static_cast<double>(midLagged) * generatedRawWidth;
            sumMidLagSquaredWidth[k] += static_cast<double>(midLagged) * midLagged;
            sumMidLagTimesGeneratedDouble[k] += static_cast<double>(midLagged) * generatedRawDouble;
            sumMidLagSquaredDouble[k] += static_cast<double>(midLagged) * midLagged;
        }
        midHistoryWritePos = (midHistoryWritePos + 1) % midHistorySize;
        sumGeneratedSquaredWidth += static_cast<double>(generatedRawWidth) * generatedRawWidth;
        sumGeneratedSquaredDouble += static_cast<double>(generatedRawDouble) * generatedRawDouble;

        // §10.3: always-on sub-bass protection on GENERATED content only -
        // independent of Focus, applies to the decorrelated + doubled Side
        // energy before it joins the dry Region A/B side. Separate filter
        // instances (VX_ENGINE_AUDIT.md §7) so Width's own post-filter
        // output can never be coloured by Double's filter history.
        const float generatedSideWidth = subBassProtectWidth.process(generatedRawWidth);
        const float generatedSideDouble = subBassProtectDouble.process(generatedRawDouble);
        const float generatedSide = generatedSideWidth + generatedSideDouble;

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
        sumOutMidSq += outMono * outMono;
        // §17 telemetry: Width-only Side energy (dry Region A/B + Width's
        // own filtered generated content) - an EXACT split now (no fraction
        // needed), since generatedSideWidth never contains any Double
        // content at any stage.
        const float widthOnlySide = sideOriginal * sideScale * sideGain + generatedSideWidth;
        sumWidthOnlySideSq += static_cast<double>(widthOnlySide) * widthOnlySide;

        // §11 full guardrail (VXWIDTH_BUILD.md, 2026-08-08): each instance
        // compares the ORIGINAL input against a HYPOTHETICAL "what would the
        // output be if only THIS source's own generated content were
        // present" - widthOnly uses the dry Mid (Width never touches Mid at
        // all) plus widthOnlySide; doubleOnly uses Double's own Mid+Side
        // contribution alone. Neither hypothetical is ever written to
        // left[i]/right[i] - purely a measurement, matching kInvSqrt2
        // scaling so units match the real output. Deliberately EXCLUDES
        // compGain (VXWIDTH_AUDIT.md #4 fix, 2026-08-08): compGain is
        // derived from the COMBINED Width+Double output energy, so including
        // it here let more Width Side raise overall RMS -> lower compGain ->
        // this guardrail read a spurious reduction that was really loudness
        // correction, not a phase/mono problem - a hidden cross-coupling
        // where Double's safety restraint could silently move in response to
        // Width's own level (or vice versa) via a channel neither guardrail
        // is supposed to see. Safety analysis is now pre-loudness-
        // compensation throughout: DSP geometry -> safety check -> loudness
        // compensation -> output (compGain is applied only once, to the
        // actual left[i]/right[i] write above).
        const float widthOnlyMidOut = mid;
        const float widthOnlyOutL = (widthOnlyMidOut + widthOnlySide) * kInvSqrt2;
        const float widthOnlyOutR = (widthOnlyMidOut - widthOnlySide) * kInvSqrt2;
        phaseRiskGuardrailWidth.accumulateSample(l, r, widthOnlyOutL, widthOnlyOutR);

        const float doubleOnlyMidOut = mid + doubleMid * doubleMidAmount;
        const float doubleOnlyOutL = (doubleOnlyMidOut + generatedSideDouble) * kInvSqrt2;
        const float doubleOnlyOutR = (doubleOnlyMidOut - generatedSideDouble) * kInvSqrt2;
        phaseRiskGuardrailDouble.accumulateSample(l, r, doubleOnlyOutL, doubleOnlyOutR);
    }

    // §12: bounded, slow makeup gain for the NEXT block - see
    // VxWidthLoudnessCompensator.h for why this is delayed feedback, not a
    // same-block correction.
    const double inRms = std::sqrt(inEnergySum / (2.0 * numSamples));
    // VXWIDTH_AUDIT.md #3 fix (2026-08-08): outEnergySum is accumulated from
    // outL/outR, which already have THIS block's compGain baked in (see the
    // outL/outR assignment above) - feeding that straight into
    // updateForNextBlock() as "outputRms" violates its own documented
    // contract (pre-compensation output RMS) and turns the loop into
    // targetGain=inputRms/(effectRms*compGain), whose fixed point is
    // compGain=1/sqrt(effectRms) instead of the intended 1/effectRms - only
    // ~half the level difference (in dB) ever gets corrected in steady
    // state. compGain is constant across this whole block (read once, above
    // the sample loop), so dividing back out here is exact, not an
    // approximation - cheaper than a second energy accumulator.
    const double outRms = std::sqrt(outEnergySum / (2.0 * numSamples));
    const double outRmsPreComp = compGain > 1.0e-6f ? outRms / compGain : outRms;
    loudnessCompensator.updateForNextBlock(inRms, outRmsPreComp, numSamples);

    auto maxAbsRhoFor = [&](const double (&sumMidLagTimesGenerated)[6], const double (&sumMidLagSquared)[6],
                            const double sumGeneratedSquared) {
        float maxAbsRho = 0.0f;
        for (size_t k = 0; k < predictabilityLagSamples.size(); ++k) {
            const double denom = sumMidLagSquared[k] * sumGeneratedSquared;
            if (denom < 1.0e-12)
                continue;
            const float rho = juce::jlimit(-1.0f, 1.0f,
                static_cast<float>(sumMidLagTimesGenerated[k] / std::sqrt(denom)));
            maxAbsRho = juce::jmax(maxAbsRho, std::abs(rho));
        }
        return maxAbsRho;
    };
    contentPredictabilityRestraintWidth.updateForNextBlock(
        maxAbsRhoFor(sumMidLagTimesGeneratedWidth, sumMidLagSquaredWidth, sumGeneratedSquaredWidth), numSamples);
    contentPredictabilityRestraintDouble.updateForNextBlock(
        maxAbsRhoFor(sumMidLagTimesGeneratedDouble, sumMidLagSquaredDouble, sumGeneratedSquaredDouble), numSamples);
    // VXWIDTH_AUDIT.md #4 fix: outMonoSumSquared is accumulated from
    // outL+outR, which include this block's compGain - divide it back out
    // (compGain is constant across the block, so this is exact) so the mono-
    // downmix safety check sees DSP geometry only, not loudness correction -
    // see the widthOnly/doubleOnly hypothetical-output comment above for why
    // this cross-coupling matters.
    const double outMonoSumSquaredPreComp = compGain > 1.0e-6f
        ? outMonoSumSquared / (static_cast<double>(compGain) * compGain) : outMonoSumSquared;
    monoDownmixGuardrail.updateForNextBlock(inMonoSumSquared, outMonoSumSquaredPreComp, numSamples);
    phaseRiskGuardrailWidth.updateForNextBlock(analysis.broadbandCorrelation, numSamples);
    phaseRiskGuardrailDouble.updateForNextBlock(analysis.broadbandCorrelation, numSamples);
    harmonicResidualAnalyser.updateForNextBlock(voice.transientRisk, numSamples);

    // §17 debug telemetry: what the engine actually did this block, so
    // tests/UI can verify the requested-width model without disagreeing
    // definitions (§6's "the UI and DSP must never disagree").
    estimatedInputWidth01State = analysis.estimatedWidth01;
    requestedOutputWidth01State = targetWidth01;
    directStereoWeightState = stereoEvidence;
    remainingWidthGapState = remainingWidthGap;
    targetSideMidRatioState = targetSideMidRatio;
    limitedSideGainDbState = juce::Decibels::gainToDecibels(directSideGainSmoothedState);
    // §17 UI display only (does not feed back into any DSP decision): the
    // raw per-block ratio is measured over a single ~5ms block, which is
    // inherently noisy sample-to-sample (few cycles of signal, phase-
    // dependent). estimatedInputWidth01 already gets ~450ms smoothing
    // inside InputAnalyser; this matches that so the visualiser's actual-
    // width ray doesn't visibly jitter block-to-block the way an unsmoothed
    // value does (reported directly by the user against the live plugin).
    // Energy gate (same principle as SideOrthogonalizer's kEnergyFloor): a
    // ratio-of-small-numbers is unstable at low signal level - near
    // silence, both sumWidthOnlySideSq and sumOutMidSq are dominated by
    // whatever tiny noise floor is present, so the RATIO can swing wildly
    // even though nothing meaningful changed. Below the floor, hold the
    // last known value rather than blending toward that noise (this is
    // what the earlier ~350ms-only smoothing missed - it dampened the
    // SIZE of each jump but still chased every noisy sample during quiet
    // passages/transient gaps, which is what read as "fluctuates
    // constantly" against real programme material).
    const double meanMidSq = sumOutMidSq / juce::jmax(1, numSamples);
    constexpr double kMidEnergyFloor = 1.0e-6; // ~ -120 dBish in this L+R-summed metric
    if (meanMidSq > kMidEnergyFloor) {
        const float rawActualWidth01 = juce::jlimit(0.0f, 1.0f,
            static_cast<float>(std::sqrt(sumWidthOnlySideSq / sumOutMidSq)));
        // ~250ms: this stacks with the UI's OWN ballistic smoothing
        // (VxStudioSpatialWidthView.cpp's kWidthValueSmoothing) - an
        // earlier 0.6s value here compounded with that second smoothing
        // pass into a visibly sluggish trace (user: the width rays weren't
        // tracking live while Double's independently-animated ghost dots
        // clearly were, reading as "the lines don't move"). One smoothing
        // stage should own most of the settling time, not both stacked.
        const float actualWidthBlockAlpha = std::exp(-static_cast<float>(numSamples) / (0.25f * static_cast<float>(currentSampleRateHz)));
        actualOutputWidth01State = actualWidthBlockAlpha * actualOutputWidth01State
            + (1.0f - actualWidthBlockAlpha) * rawActualWidth01;
    }
    // Width-path-only telemetry (VX_ENGINE_AUDIT.md §14): actualOutputWidth01
    // above divides by sumOutMidSq (the FINAL combined output's Mid, which
    // legitimately includes Double's own doubleMidAmount contribution) - a
    // control-ownership test comparing actualOutputWidth01 across Tightness/
    // Focus with Double active would therefore see the ratio move even
    // though the Side-domain numerator (widthOnlySide) is untouched, purely
    // because Double's own Mid contribution shifts the denominator. This
    // divides by sumInMidSq instead - the RAW INPUT Mid, entirely prior to
    // any processing - so it is genuinely blind to anything Double does,
    // not just to the numerator.
    if (sumInMidSq / juce::jmax(1, numSamples) > kMidEnergyFloor) {
        const float rawWidthPathRatio = static_cast<float>(std::sqrt(sumWidthOnlySideSq / juce::jmax(1.0e-12, sumInMidSq)));
        const float widthPathBlockAlpha = std::exp(-static_cast<float>(numSamples) / (0.25f * static_cast<float>(currentSampleRateHz)));
        widthPathOutputRatioState = widthPathBlockAlpha * widthPathOutputRatioState
            + (1.0f - widthPathBlockAlpha) * rawWidthPathRatio;
    }
    // §22 telemetry: last-processed-block RMS (no smoothing - tests read
    // this immediately after rendering, matching getAdtMaxPitchShiftCentsObserved's
    // own "measure what actually happened this render" convention).
    doubleMidRmsState = static_cast<float>(std::sqrt(sumDoubleMidSq / juce::jmax(1, numSamples)));
    doubleSideRmsState = static_cast<float>(std::sqrt(sumDoubleSideSq / juce::jmax(1, numSamples)));
    safetyRestraintAmountState = 1.0f - juce::jmin(monoRiskRestraint, juce::jmin(monoDownmixRestraint,
        juce::jmin(predictabilityRestraintWidth, predictabilityRestraintDouble)),
        juce::jmin(phaseRiskRestraintWidth, phaseRiskRestraintDouble));
    monoConfidence01State = analysis.monoConfidence;
    hasSignalState = meanMidSq > kMidEnergyFloor;

    // Target-seeking engine instrumentation: R0 = actual input Side/Mid RMS
    // ratio, distinct from W0 (analysis.estimatedWidth01, which already
    // applies channelBalance/phaseRiskFactor corrections - see §6 of the
    // brief). Telemetry only for now; same energy-gate/hold-last-value and
    // smoothing pattern as actualOutputWidth01State above, for the same
    // reason (ratio-of-small-numbers is unstable near silence).
    if (sumInMidSq / juce::jmax(1, numSamples) > kMidEnergyFloor) {
        const float rawR0 = static_cast<float>(std::sqrt(sumInSideSq / juce::jmax(1.0e-12, sumInMidSq)));
        const float r0BlockAlpha = std::exp(-static_cast<float>(numSamples) / (0.25f * static_cast<float>(currentSampleRateHz)));
        inputSideMidRatioState = r0BlockAlpha * inputSideMidRatioState + (1.0f - r0BlockAlpha) * rawR0;
    }

    outputTrimmer.process(buffer, currentSampleRateHz);
}

#if !defined(VXSTUDIO_DISABLE_PLUGIN_ENTRYPOINT)
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new VXWidthAudioProcessor();
}
#endif
