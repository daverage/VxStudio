#pragma once

#include "../../framework/VxStudioProcessorBase.h"
#include "../../framework/VxStudioBlockSmoothedControl.h"
#include "../../framework/VxStudioBlockSmoothing.h"
#include "../../framework/VxStudioOutputTrimmer.h"
#include "dsp/VxWidthAdtVoice.h"
#include "dsp/VxWidthDecorrelator.h"
#include "dsp/VxWidthInputAnalyser.h"
#include "dsp/VxWidthLoudnessCompensator.h"
#include "dsp/VxWidthSideOrthogonalizer.h"
#include "dsp/VxWidthSpectralShaping.h"

#include <array>
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
    float getOrthogonalizerEnergyReductionDb() const noexcept { return sideOrthogonalizer.lastEnergyReductionDb(); }
    float getOrthogonalizerCoefficientEnergy() const noexcept { return sideOrthogonalizer.coefficientEnergy(); }
    void setOrthogonalizerBypassed(const bool b) noexcept { sideOrthogonalizer.setBypassed(b); }
    void setOrthogonalizerSamePolarityOnly(const bool b) noexcept { sideOrthogonalizer.setSamePolarityOnly(b); }

    // EXPERIMENTAL (2026-08-07 ADT micro-pitch investigation) - default OFF.
    // Now exposed as a plain header toggle (see makeIdentity()'s
    // `simpleToggleParamId`) so it can actually be A/B'd from a DAW, but
    // deliberately kept off the four main knobs and undocumented in the
    // product's help content while it's still evidence-gathering, not a
    // committed feature. This setter is also called directly by tests.
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
    vxsuite::width::OnePoleTilt focusTiltMid;
    vxsuite::width::OnePoleTilt focusTiltSide;
    vxsuite::width::OnePoleHighpass subBassProtect;
    vxsuite::width::LoudnessCompensator loudnessCompensator;
    vxsuite::width::ContentPredictabilityRestraint contentPredictabilityRestraint;
    vxsuite::width::MonoDownmixGuardrail monoDownmixGuardrail;
    vxsuite::width::SideOrthogonalizer sideOrthogonalizer;
    float adtMaxPitchShiftCentsObserved = 0.0f;

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
