#include "VxStudioSpatialWidthView.h"

#include <algorithm>
#include <cmath>

namespace vxsuite {

namespace {
// Measured practical ceilings (see header comment for the exact regression
// numbers these were read from) - a small margin above the highest observed
// value for each material type so genuinely maximal material approaches,
// but doesn't pin, the visual limit. TWO separate ceilings, not one: mono's
// own designed maximum (~0.14-0.16 measured) is far below broad stereo's
// (~0.40) - mapping mono onto the stereo scale meant mono's own Width=100
// (§2's literal "maximum designed VX Width result") could never visually
// approach the boundary, which is exactly what user feedback flagged
// ("width never reaches the edges"). perceptualMap() picks between these
// based on the current mono/stereo presentation blend.
constexpr float kPerceptualReferenceMaxStereo = 0.42f;
constexpr float kPerceptualReferenceMaxMono   = 0.17f;
// Expansive gamma (<1) so the low end of the range is visibly readable,
// not just the ceiling. 0.85 (round 1's original value) read as "closes
// too early" against live testing - low-end movement was barely visible
// for most of the lower knob range before the curve finally opened up
// near the top. Lowered to push more of the visible motion earlier.
constexpr float kPerceptualGamma = 0.5f;

// Per-tick ballistic smoothing (called at ~12Hz - see header). Width-
// related values are smoothed a bit more than Double/Tightness/Focus
// (which read straight off the parameters, not noisy audio measurements),
// but NOT heavily - the DSP side (VxWidthProcessor.cpp) already owns most
// of the settling time (~250ms) since round 3's fix; stacking two slow
// smoothing passes was what made the width rays look frozen next to
// Double's independently-animated dots (user feedback, round 4). This
// pass is now the lighter of the two, mainly for inter-tick visual
// smoothness rather than noise rejection.
constexpr float kWidthValueSmoothing = 0.40f;
constexpr float kControlValueSmoothing = 0.55f; // double/tightness/focus
constexpr float kModeBlendSmoothing = 0.72f; // mono<->stereo cross-fade
} // namespace

SpatialWidthView::SpatialWidthView(const ProductTheme& theme) {
    const auto& a = theme.accentRgb;
    const auto& t = theme.textRgb;
    accentColour = juce::Colour::fromFloatRGBA(a[0], a[1], a[2], 1.0f);
    // Soft slate-blue - deliberately a different hue family from the teal
    // accent (not derived from it), so it never reads as "a fainter version
    // of the same line" even at low alpha or when both traces coincide.
    inputColour  = juce::Colour::fromFloatRGBA(0.58f, 0.67f, 0.92f, 1.0f);
    dimColour    = juce::Colour::fromFloatRGBA(t[0], t[1], t[2], 0.35f);
    textColour   = juce::Colour::fromFloatRGBA(t[0], t[1], t[2], 0.80f);
    setInterceptsMouseClicks(false, false);
}

float SpatialWidthView::perceptualMap(const float rawWidth01) const noexcept {
    // dispModeBlend: 0=stereo, 1=mono - blended, not switched, so the
    // reference ceiling itself animates smoothly through a mode change
    // rather than jumping (§14).
    const float referenceMax = juce::jmap(dispModeBlend, kPerceptualReferenceMaxStereo, kPerceptualReferenceMaxMono);
    const float normalised = juce::jlimit(0.0f, 1.0f, rawWidth01 / referenceMax);
    return std::pow(normalised, kPerceptualGamma);
}

void SpatialWidthView::setTelemetry(const SpatialWidthTelemetry& telemetry, const bool isMonoLike) noexcept {
    latest = telemetry;
    targetMonoLike = isMonoLike;

    // Updated first so perceptualMap() below (and the ghost-dot animation)
    // both see this tick's mode blend, not last tick's.
    const float modeTarget = targetMonoLike ? 1.0f : 0.0f;
    dispModeBlend += (modeTarget - dispModeBlend) * kModeBlendSmoothing;

    const float targetInput  = perceptualMap(telemetry.estimatedInputWidth01);
    const float targetTarget = perceptualMap(telemetry.requestedOutputWidth01);
    const float targetActual = perceptualMap(telemetry.actualOutputWidth01);

    dispInput     += (targetInput  - dispInput)     * kWidthValueSmoothing;
    dispTarget    += (targetTarget - dispTarget)    * kWidthValueSmoothing;
    dispActual    += (targetActual - dispActual)    * kWidthValueSmoothing;
    dispDouble    += (telemetry.doubleAmount01 - dispDouble) * kControlValueSmoothing;
    dispTightness += (telemetry.tightness01    - dispTightness) * kControlValueSmoothing;
    dispFocus     += (telemetry.focus01        - dispFocus) * kControlValueSmoothing;

    repaint();
}

void SpatialWidthView::paint(juce::Graphics& g) {
    const auto bounds = getLocalBounds().toFloat();
    if (bounds.isEmpty())
        return;

    const float baselineY = bounds.getBottom() - bounds.getHeight() * 0.14f;
    const float apexY     = bounds.getY() + bounds.getHeight() * 0.10f;
    const float centreX   = bounds.getCentreX();
    const float halfWidth = bounds.getWidth() * 0.44f;

    auto rayEndX = [&](const float extent01, const bool leftSide) {
        return centreX + (leftSide ? -1.0f : 1.0f) * extent01 * halfWidth;
    };
    auto strokeRayPair = [&](const float extent01, const juce::Colour colour, const float strokeWidth) {
        juce::Path p;
        p.startNewSubPath(rayEndX(extent01, true), baselineY);
        p.lineTo(centreX, apexY);
        p.lineTo(rayEndX(extent01, false), baselineY);
        g.setColour(colour);
        g.strokePath(p, juce::PathStrokeType(strokeWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    };

    // Hierarchy (revised per user feedback on the first pass): the maximum-
    // extent boundary and the L/R baseline are a faint, constant REFERENCE
    // frame drawn first - not live telemetry, so they must not dominate.
    // Actual output is the strongest thing on screen; input is a dim
    // before-trace; target is at most a small tick, not a competing shape.
    g.setColour(dimColour.withAlpha(0.22f));
    g.drawLine(centreX - halfWidth, apexY, centreX - halfWidth, baselineY, 1.0f);
    g.drawLine(centreX + halfWidth, apexY, centreX + halfWidth, baselineY, 1.0f);
    g.drawLine(centreX - halfWidth, baselineY, centreX + halfWidth, baselineY, 1.0f);
    g.setFont(juce::FontOptions().withHeight(13.0f).withStyle("Bold"));
    g.setColour(textColour.withAlpha(0.55f));
    g.drawText("L", juce::Rectangle<float>(centreX - halfWidth - 20.0f, baselineY - 9.0f, 20.0f, 18.0f),
               juce::Justification::centredRight);
    g.drawText("R", juce::Rectangle<float>(centreX + halfWidth, baselineY - 9.0f, 20.0f, 18.0f),
               juce::Justification::centredLeft);

    // Minimal legend: user feedback (first two revisions) was that colour/
    // alpha alone didn't unambiguously say which trace was which. Three
    // short swatch+word pairs, top-right, small and dim - not a numeric
    // readout, just naming the two colours and the dot cue already on
    // screen.
    {
        const float legendY = bounds.getY() + 4.0f;
        float legendX = bounds.getRight() - 8.0f;
        g.setFont(juce::FontOptions().withHeight(11.0f));
        auto drawLegendItem = [&](const juce::String& label, const juce::Colour swatchColour, const bool dot) {
            const float textWidth = g.getCurrentFont().getStringWidthFloat(label) + 4.0f;
            legendX -= textWidth;
            g.setColour(textColour.withAlpha(0.62f));
            g.drawText(label, juce::Rectangle<float>(legendX, legendY, textWidth, 14.0f), juce::Justification::centredLeft);
            legendX -= 14.0f;
            g.setColour(swatchColour);
            if (dot)
                g.fillEllipse(legendX + 4.0f, legendY + 4.5f, 5.0f, 5.0f);
            else
                g.drawLine(legendX, legendY + 7.0f, legendX + 12.0f, legendY + 7.0f, 2.0f);
            legendX -= 16.0f;
        };
        drawLegendItem("double", accentColour.withAlpha(0.85f), true);
        drawLegendItem("output", accentColour, false);
        drawLegendItem("input", inputColour, false);
    }

    // Development numeric readout (2026-08-07 - debugging a live/offline
    // mismatch report where the ray's on-screen extent didn't match what
    // any offline reproduction measured). Exposes the exact raw telemetry
    // numbers so they can be read/reported precisely instead of estimating
    // ray length from a screenshot. Small and dim - not meant as a
    // permanent user-facing feature, but cheap to leave in since it's a
    // direct passthrough of telemetry this view already receives.
    {
        g.setFont(juce::FontOptions().withHeight(10.0f));
        g.setColour(textColour.withAlpha(0.5f));
        const juce::String debugText =
            "in=" + juce::String(latest.estimatedInputWidth01, 4)
            + " out=" + juce::String(latest.actualOutputWidth01, 4)
            + " req=" + juce::String(latest.requestedOutputWidth01, 4)
            + " mode=" + (targetMonoLike ? "mono" : "stereo")
            + " blend=" + juce::String(dispModeBlend, 2);
        g.drawText(debugText, juce::Rectangle<float>(bounds.getX() + 6.0f, bounds.getY() + 4.0f, 400.0f, 14.0f),
                   juce::Justification::centredLeft);
    }

    // Centre stem: the thing narrow/mono material should visibly collapse
    // onto, and wide material should visibly pull away from.
    g.setColour(dimColour.withAlpha(0.5f));
    g.drawLine(centreX, apexY, centreX, baselineY, 1.4f);

    // §12 Focus: marker sliding along the stem - never touches the L/R
    // geometry. A tick alone at 0.30 alpha proved hard to spot against the
    // stem/apex convergence, so this pairs a brighter tick with a small
    // filled dot at its centre.
    {
        const float focusY = juce::jmap(dispFocus, 0.0f, 1.0f, baselineY - 8.0f, apexY + 8.0f);
        g.setColour(accentColour.withAlpha(0.60f));
        g.drawLine(centreX - 9.0f, focusY, centreX + 9.0f, focusY, 2.0f);
        g.fillEllipse(centreX - 2.2f, focusY - 2.2f, 4.4f, 4.4f);
    }

    // BEFORE: input reference, its own colour (not just a dimmer accent -
    // see inputColour's comment for why: it must read as "the other trace"
    // even when it collapses near the centre stem, which is the common
    // case). Only meaningful for stereo material (§3's "0 = original/input
    // width" neutral point) - mono has no pre-existing image to preserve
    // (§2), so this fades out as the presentation settles into mono mode.
    strokeRayPair(dispInput, inputColour.withAlpha(0.85f * (1.0f - dispModeBlend)), 1.6f);

    // AFTER: actual measured output - the dominant, brightest element (§8).
    strokeRayPair(dispActual, accentColour.withAlpha(0.95f), 3.0f);

    // Target tick marks were here (round 1) but removed: they moved
    // whenever Width changed yet weren't in the legend and didn't match
    // any of the three labelled traces, reading as unexplained motion
    // (user feedback, round 6). Given target vs actual reliably diverges
    // for this engine (the whole point of §17's "report the discrepancy,
    // don't hide it"), they were effectively always-on noise rather than
    // an occasional annotation. dispTarget is still tracked/available if
    // a clearer treatment is wanted later - just not drawn for now.

    // §10/§11 Double + Tightness: small dots straddling the CENTRE STEM near
    // the baseline - deliberately NOT anchored to the width ray (an earlier
    // version rode dispActual, which made Double invisible whenever Width
    // was near 0, even though Double is fully independent of Width and
    // audible on its own - §9's "Width 0 / Double 100 should still create
    // audible thickening" needs a visible cue regardless of where the width
    // rays currently sit). Tightness sets how far the dots sit from the stem
    // (tight = close/coupled, loose = more independent); reads as "a second
    // performance is present", not "more width".
    //
    // STATIC positions (user feedback, 2026-08-07: continuous drift/jitter
    // here read as unexplained, distracting motion with no clear cause -
    // "why do the dots keep moving/drifting"). Position is now a pure
    // function of dispDouble/dispTightness, so the dots only move when
    // those controls actually change, not on a fixed decorative clock.
    if (dispDouble > 0.02f) {
        // Tightness knob convention inverted 2026-08-07 (0%=loose,
        // 100%=tight, matching the control's own name) - endpoints swapped
        // here so high Tightness still means "close/coupled" visually.
        const float offsetPx = juce::jmap(dispTightness, 0.0f, 1.0f, 0.22f, 0.05f) * halfWidth;
        const float dotY = baselineY - 10.0f;
        g.setColour(accentColour.withAlpha(0.40f * dispDouble));
        static constexpr float kDotPositions[] = { 0.70f, 1.0f };
        for (const bool leftSide : { true, false }) {
            for (size_t i = 0; i < 2; ++i) {
                const float x = centreX + (leftSide ? -1.0f : 1.0f) * offsetPx * kDotPositions[i];
                g.fillEllipse(x - 1.8f, dotY - 1.8f, 3.6f, 3.6f);
            }
        }
    }
}

} // namespace vxsuite
