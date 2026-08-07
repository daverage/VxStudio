#pragma once

#include "VxStudioProduct.h"
#include "VxStudioSpatialWidthTelemetry.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace vxsuite {

// VX Width Phase 2 (docs/Task Based/VX_WIDTH_ENGINE_UPGRADE.md §6-§12): the
// central real-time spatial-width visualiser. Purely a READER of
// SpatialWidthTelemetry - never touches parameters or DSP state (§1/§17).
//
// Geometry: a vertical centre stem with rays fanning down to an L---R
// baseline. Ray extent = width (0 = coincides with the stem, 1 = reaches the
// baseline edge) - the SAME geometric model serves both mono (§9's
// centre->full-spread) and stereo (§9's collapse/preserve/expand) Width
// behaviour, since stereo's negative range is just "target extent below
// input extent" with no separate code path.
//
// Visual hierarchy (revised after first-pass user feedback: the initial
// build made input/target/actual near-equal weight, which read as an
// ambiguous static "cone" rather than live before/after telemetry):
//   1. max-extent boundary + L/R baseline - faint, constant REFERENCE only.
//   2. input width - dim, the "before" trace.
//   3. actual output width - brightest/thickest, the dominant element -
//      driven from MEASURED output, not from knob position, so the display
//      never overstates what the engine actually achieved (§17: report a
//      real DSP discrepancy rather than hide it).
//   4. target width - only a small tick when it diverges from actual, never
//      a second competing shape.
//   5. Double - small dots riding the actual rays, not a wider ray pair -
//      reads as "another performance", not "more width" (§10).
// Called at the editor's ~12Hz UI tick (see EditorBase::timerCallback) -
// ballistic smoothing constants below are tuned for that rate, matching the
// pattern GainMeterView/LevelTraceView already use (no separate Timer of
// its own; the caller's tick drives it).
class SpatialWidthView final : public juce::Component {
public:
    explicit SpatialWidthView(const ProductTheme& theme);

    // isMonoLike: current UI presentation mode (already hysteresis-settled
    // by the caller - see EditorBase). This view does its own ballistic
    // smoothing of the telemetry VALUES for fluid animation, plus a
    // separate short cross-fade when isMonoLike itself changes, but owns no
    // classification logic - that stays a single source of truth upstream
    // so the knob label and this view can never disagree.
    void setTelemetry(const SpatialWidthTelemetry& telemetry, bool isMonoLike) noexcept;

    void paint(juce::Graphics&) override;

private:
    // §7 perceptual calibration: raw estimatedWidth01 is the Side/Mid-based
    // engine metric (0=centre, 1=theoretical max). Phase 1/1.1 regression
    // measurements (tests/VXWidthShellCheck.cpp) on representative content
    // never approached 1.0 even at full engagement - moderate stereo ~0.13
    // ("§16 monotonic expansion" w25), broad decorrelated stereo ~0.40
    // ("Phase1.1 estimatedWidth01" wBroad), typical mono full-width ~0.14-
    // 0.16 ("§16 mono endpoint", "§16 monotonic expansion" w100). This maps
    // that MEASURED practical range onto the visual 0..1 extent, so "broad"
    // reads as genuinely broad on screen without stretching the DSP to hit
    // a mathematical 1.0 (§15's explicit instruction).
    float perceptualMap(float rawWidth01) const noexcept;

    juce::Colour accentColour;
    // Distinct hue (not just a dimmer accentColour) for the input/"before"
    // trace - alpha-only differentiation from the actual/"after" trace
    // proved insufficient in practice (user feedback: input collapses to
    // near-invisible under the centre stem for typical material, leaving
    // no perceivable before/after story). A different colour reads as
    // unmistakably "the other thing" even when the two traces nearly
    // overlap geometrically.
    juce::Colour inputColour;
    juce::Colour dimColour;
    juce::Colour textColour;

    // Ballistic display state (smoothed toward the latest telemetry each
    // timer tick) - keeps the rays animating fluidly rather than snapping
    // block-to-block, on top of the DSP's own ~450ms smoothing.
    float dispInput = 0.0f;
    float dispTarget = 0.0f;
    float dispActual = 0.0f;
    float dispDouble = 0.0f;
    float dispTightness = 0.6f;
    float dispFocus = 0.55f;
    // 0 = stereo presentation, 1 = mono presentation - animated, not
    // stepped, so a mode change never visually jumps (§14).
    float dispModeBlend = 0.0f;
    bool targetMonoLike = false;

    SpatialWidthTelemetry latest;
};

} // namespace vxsuite
