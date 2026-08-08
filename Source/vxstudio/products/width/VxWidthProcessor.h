#pragma once

#include "../../framework/VxStudioProcessorBase.h"
#include "../../framework/VxStudioBlockSmoothedControl.h"
#include "../../framework/VxStudioBlockSmoothing.h"
#include "../../framework/VxStudioOutputTrimmer.h"
#include "dsp/VxWidthAdtVoice.h"
#include "dsp/VxWidthDecorrelator.h"
#include "dsp/VxWidthInputAnalyser.h"
#include "dsp/VxWidthHarmonicResidualAnalyser.h"
#include "dsp/VxWidthLoudnessCompensator.h"
#include "dsp/VxWidthPhaseRiskGuardrail.h"
#include "dsp/VxWidthSideOrthogonalizer.h"
#include "dsp/VxWidthSpectralShaping.h"

#include <array>
#include <optional>
#include <vector>

// VX Width — stereo image and doubling processor.
// Reference: docs/Task Based/VXWIDTH_BUILD.md (build order, §28).
// This build lands §28 steps 1-6: plugin shell, orthonormal M/S codec, the
// full existing-stereo width engine (§7), the ADT doubling engine (§8),
// Focus spectral placement (§10), transient protection extended to
// Region B/C (§9), loudness/energy management (§12), and channel
// configuration handling (§15: mono input to stereo output, in addition to
// stereo-in/stereo-out - both required configs; the framework's default
// bus negotiation only allows matching input/output channel counts, so
// this product overrides it). Latency is 0 samples (no lookahead anywhere
// in the signal path), already meeting §13's Live-mode target without a
// separate Quality mode - there is currently no lookahead-dependent
// analysis (e.g. spectral) to justify one. §8.8's harmonic/residual/
// transient decomposition and §11's full mono/phase-risk guardrail (beyond
// the correlation-based restraints already in place) remain later
// refinements, not built here.
class VXWidthAudioProcessor final : public vxsuite::ProcessorBase {
public:
    VXWidthAudioProcessor();
    juce::String getStatusText() const override;
    float getLocalOutputTrimMaxReductionDb() const noexcept { return outputTrimmer.getMaxObservedReductionDb(); }
    // Debug/test visibility: max instantaneous ADT Doppler pitch shift (cents)
    // observed across Voice A/B since the last resetSuite() - proves the
    // slew clamp in VxWidthAdtVoice.h actually holds its per-tightness-band
    // budget on real render output, not just by construction.
    float getAdtMaxPitchShiftCentsObserved() const noexcept { return adtMaxPitchShiftCentsObserved; }
    // Debug/test visibility for the SideOrthogonalizer prototype (2026-08-07
    // imbalance investigation, phase 2): how much it's currently reducing
    // generated-Side energy by (dB) and its adaptive coefficient energy -
    // lets tests/telemetry confirm it's removing a targeted correlated
    // component, not silently suppressing the whole effect.
    // These report the DOUBLE-side instance (see sideOrthogonalizerDouble's
    // comment) - existing callers all test with Width=0/Double=1, so this
    // preserves exactly what they were already measuring.
    float getOrthogonalizerEnergyReductionDb() const noexcept { return sideOrthogonalizerDouble.lastEnergyReductionDb(); }
    float getOrthogonalizerCoefficientEnergy() const noexcept { return sideOrthogonalizerDouble.coefficientEnergy(); }

    // §17 debug telemetry (VX_WIDTH_ENGINE_UPGRADE.md): lets tests/UI confirm
    // the target-geometry engine is doing what it claims, all 0..1 except
    // safetyRestraintAmount (0=unrestrained, 1=fully restrained).
    float getEstimatedInputWidth01() const noexcept { return estimatedInputWidth01State; }
    float getRequestedOutputWidth01() const noexcept { return requestedOutputWidth01State; }
    float getActualOutputWidth01() const noexcept { return actualOutputWidth01State; }
    // Width-path-only measurement (VX_ENGINE_AUDIT.md §14): widthOnlySide
    // RMS divided by the RAW INPUT Mid RMS (not the final combined output
    // Mid actualOutputWidth01 uses) - genuinely blind to anything Double
    // does, including its own Mid contribution. Use this, not
    // getActualOutputWidth01(), when testing Width/Double control ownership.
    float getWidthPathOutputRatio() const noexcept { return widthPathOutputRatioState; }
    float getSafetyRestraintAmount() const noexcept { return safetyRestraintAmountState; }
    float getMonoConfidence01() const noexcept { return monoConfidence01State; }
    // §11 full guardrail (VXWIDTH_BUILD.md) development visibility - the
    // combined restraint currently applied to Width's own decorBlend/
    // sideGain, and Double's own doubleSideAmount, plus which specific
    // measurement (of the four) is currently driving each.
    float getPhaseRiskGuardrailWidthRestraint() const noexcept { return phaseRiskGuardrailWidth.currentRestraint(); }
    float getPhaseRiskGuardrailDoubleRestraint() const noexcept { return phaseRiskGuardrailDouble.currentRestraint(); }
    // §8.8 development visibility.
    float getHarmonicConfidence01() const noexcept { return harmonicResidualAnalyser.harmonicConfidence01(); }
    float getTransientWeight01() const noexcept { return harmonicResidualAnalyser.transientWeightValue(); }
    // §22 development visibility (VX_ENGINE_AUDIT.md, final control-
    // ownership pass): RMS of Double's own Mid/Side contribution to the
    // output (post doubleMidAmount/doubleSideAmount, i.e. what's actually
    // added to the mix), block-rate, last-processed-block only.
    float getDoubleMidRms() const noexcept { return doubleMidRmsState; }
    float getDoubleSideRms() const noexcept { return doubleSideRmsState; }

    // Target-seeking engine instrumentation (docs brief, 2026-08-07): the
    // ACTUAL input Side/Mid RMS ratio (R0), distinct from estimatedWidth01
    // (W0) - W0 already includes channelBalance/phaseRiskFactor corrections,
    // R0 is the raw DSP quantity the target-gain solver divides by.
    float getInputSideMidRatio() const noexcept { return inputSideMidRatioState; }
    // Development telemetry for the target-ratio solver (design brief §16):
    // directStereoWeight (==stereoEvidence, §9), the residual gap Region C
    // is now driven by (§14), and the solver's smoothed/applied direct-Side
    // gain in dB (post safety-limit and phase-risk restraint).
    float getDirectStereoWeight() const noexcept { return directStereoWeightState; }
    float getRemainingWidthGap() const noexcept { return remainingWidthGapState; }
    float getTargetSideMidRatio() const noexcept { return targetSideMidRatioState; }
    float getLimitedSideGainDb() const noexcept { return limitedSideGainDbState; }
    // Test-only: overrides the solver's direct-Side gain ceiling (production
    // default 12dB, see VxWidthProcessor.cpp) so VXWidthShellCheck's
    // [gain-limit sweep] can compare 9/12/15dB per the design brief's §18/
    // §30.2 requirement, without a second production constant to keep in sync.
    void setDirectSideGainCeilingDbForTesting(const float db) noexcept { directSideGainCeilingDbState = db; }

    // Phase 2 UI contract (VX_WIDTH_ENGINE_UPGRADE.md §6-§9) - read-only,
    // never writes back to parameters/DSP state.
    std::optional<vxsuite::SpatialWidthTelemetry> getSpatialWidthTelemetry() const noexcept override;
    void setOrthogonalizerBypassed(const bool b) noexcept { sideOrthogonalizerDouble.setBypassed(b); }
    void setOrthogonalizerSamePolarityOnly(const bool b) noexcept { sideOrthogonalizerDouble.setSamePolarityOnly(b); }

    // Permanent, always-on ADT micro-pitch stage (2026-08-07 - graduated
    // from an experimental toggle, see makeIdentity()'s comment). Called
    // unconditionally from resetSuite(); kept as a public setter mainly for
    // test access (some tests still call it directly for clarity).
    void setAdtMicroPitchEnabled(const bool b) noexcept {
        adtVoiceA.setMicroPitchEnabled(b);
        adtVoiceB.setMicroPitchEnabled(b);
    }
    float getAdtVoiceAMicroPitchCents() const noexcept { return adtVoiceA.lastMicroPitchCentsApplied(); }
    float getAdtVoiceBMicroPitchCents() const noexcept { return adtVoiceB.lastMicroPitchCentsApplied(); }
    float getAdtVoiceALongTermMicroPitchCents() const noexcept { return adtVoiceA.longTermMicroPitchCentsAverage(); }
    float getAdtVoiceBLongTermMicroPitchCents() const noexcept { return adtVoiceB.longTermMicroPitchCentsAverage(); }
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

protected:
    void prepareSuite(double sampleRate, int samplesPerBlock) override;
    void resetSuite() override;
    void processProduct(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

private:
    static vxsuite::ProductIdentity makeIdentity();

    double currentSampleRateHz = 48000.0;
    vxsuite::BlockSmoothedControlQuad controls;
    vxsuite::OutputTrimmer outputTrimmer;
    vxsuite::width::InputAnalyser inputAnalyser;
    vxsuite::width::VelvetDecorrelator decorrelatorA;
    vxsuite::width::VelvetDecorrelator decorrelatorB;
    vxsuite::width::AdtVoice adtVoiceA;
    vxsuite::width::AdtVoice adtVoiceB;
    // Double-only (Focus/Tightness must not affect Width - see the note
    // above decorSource/focusedMid in processProduct()). No focusTiltSide:
    // Region C's decorrelator source is the untilted Mid/Side now, so only
    // a single Mid tilt (feeding the ADT voices) is needed.
    vxsuite::width::OnePoleTilt focusTiltMid;
    // Separate instances (VX_ENGINE_AUDIT.md §7, 2026-08-07): a shared
    // stateful filter has MEMORY - even with widthOrthogonalized itself
    // provably Focus/Tightness-independent, running it through ONE
    // filter instance whose rolling state also reflects whatever Double
    // contributed meant Width's own post-filter output was still coloured
    // by Double's history. Each instance now only ever sees its own
    // source's content.
    vxsuite::width::OnePoleHighpass subBassProtectWidth;
    vxsuite::width::OnePoleHighpass subBassProtectDouble;
    vxsuite::width::LoudnessCompensator loudnessCompensator;
    // Separate instances (VX_ENGINE_AUDIT.md §7/§11/§12, 2026-08-07): a
    // single shared restraint multiplying widthOrthogonalized+
    // doubleOrthogonalized combined meant Tightness/Focus changing DOUBLE's
    // own correlation-with-Mid could change the restraint gain applied to
    // WIDTH's contribution too, even with Width itself unchanged. Each
    // instance now measures and restrains only its own source's
    // correlation with Mid, matching the SideOrthogonalizer split above.
    vxsuite::width::ContentPredictabilityRestraint contentPredictabilityRestraintWidth;
    vxsuite::width::ContentPredictabilityRestraint contentPredictabilityRestraintDouble;
    vxsuite::width::MonoDownmixGuardrail monoDownmixGuardrail;
    // Separate instances (2026-08-07, "nothing but Width should affect
    // Width" fix): a single shared orthogonalizer's NLMS weights adapted
    // on the COMBINED Width+Double generated signal, and its dynamic taps
    // track the ADT voices' delay - both meant Tightness (which moves that
    // delay) measurably nudged Width's own output even though Width's
    // decorSideRaw itself never depended on Tightness. Width's instance
    // uses ONLY fixed lags (dynamic delay args always 0,0 - see its call
    // site in processProduct()), so it's structurally independent of the
    // ADT voices/Tightness entirely, not just close.
    vxsuite::width::SideOrthogonalizer sideOrthogonalizerWidth;
    vxsuite::width::SideOrthogonalizer sideOrthogonalizerDouble;
    // Full mono/phase-risk guardrail (VXWIDTH_BUILD.md §11, 2026-08-08):
    // per-band coherence, downmix spectral deviation, centre-image
    // displacement, sustained negative-correlation duration - supplements
    // the existing broadband monoRiskRestraint/MonoDownmixGuardrail. Split
    // per-source (same reasoning as every other split above): each instance
    // compares the ORIGINAL input against only its own source's hypothetical
    // contribution (dry + Width's own generated Side; dry + Double's own
    // Mid+Side), so Tightness/Focus changing Double's output can never move
    // Width's own restraint via this mechanism either.
    vxsuite::width::PhaseRiskGuardrail phaseRiskGuardrailWidth;
    vxsuite::width::PhaseRiskGuardrail phaseRiskGuardrailDouble;
    // §8.8 harmonic/residual/transient decomposition (VXWIDTH_BUILD.md,
    // 2026-08-08) - see its own header comment for the lightweight (non-
    // STFT) approach and why. Single instance: this analyses the ORIGINAL
    // input Mid, not anything Width/Double generate, so it needs no
    // per-source split (unlike everything else added this session).
    vxsuite::width::HarmonicResidualAnalyser harmonicResidualAnalyser;
    float adtMaxPitchShiftCentsObserved = 0.0f;

    // §17 debug telemetry state, updated once per block in processProduct().
    float estimatedInputWidth01State = 0.0f;
    float requestedOutputWidth01State = 0.0f;
    float actualOutputWidth01State = 0.0f;
    float widthPathOutputRatioState = 0.0f;
    float doubleMidRmsState = 0.0f;
    float doubleSideRmsState = 0.0f;
    float safetyRestraintAmountState = 0.0f;
    float monoConfidence01State = 0.0f;
    bool hasSignalState = false;
    // R0 (actual input Side/Mid RMS ratio), smoothed - see getInputSideMidRatio().
    float inputSideMidRatioState = 0.0f;
    // Target-ratio solver state/telemetry - see the getters above.
    float directSideGainSmoothedState = 1.0f;
    float directStereoWeightState = 0.0f;
    float remainingWidthGapState = 0.0f;
    float targetSideMidRatioState = 0.0f;
    float limitedSideGainDbState = 0.0f;
    // Production default 18dB - see kDirectSideGainCeilingDb's comment in
    // VxWidthProcessor.cpp for why. Overridable only via the test-only
    // setter above.
    float directSideGainCeilingDbState = 18.0f;

    // Lag-aware predictability correlation (§11.3, see ContentPredictabilityRestraint's
    // header comment): rolling history of Mid samples so the generated
    // content can be compared against Mid at several past lags, not just
    // same-sample. Sized once in prepareSuite() for the longest lag (16ms),
    // no allocation after.
    std::vector<float> midHistory;
    int midHistoryWritePos = 0;
    int midHistoryMaxLagSamples = 0;
    std::array<int, 6> predictabilityLagSamples {};
};
