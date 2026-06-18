#include "VxStudioGainMeterView.h"

#include <cmath>
#include <array>

namespace vxsuite {

namespace {

constexpr float kDbFloor = -60.0f;
constexpr float kDbRange =  60.0f;

// Scale marks drawn alongside the bars (dBFS values).
constexpr std::array<int, 9> kScaleMarks {{ 0, -3, -6, -9, -12, -18, -24, -36, -48 }};

} // namespace

GainMeterView::GainMeterView(const ProductTheme& theme) {
    const auto& a = theme.accentRgb;
    const auto& t = theme.textRgb;
    accentColour = juce::Colour::fromFloatRGBA(a[0], a[1], a[2], 1.0f);
    inputColour  = juce::Colour::fromFloatRGBA(t[0], t[1], t[2], 0.55f);
    setInterceptsMouseClicks(false, false);
}

float GainMeterView::linearToNorm(const float linear) noexcept {
    if (linear <= 1.0e-6f) return 0.0f;
    const float db = 20.0f * std::log10(linear);
    return juce::jlimit(0.0f, 1.0f, (db - kDbFloor) / kDbRange);
}

void GainMeterView::updateChannel(ChannelState& ch, const float newLinear) noexcept {
    constexpr float kDecay    = 0.80f;
    constexpr int   kHoldTime = 24;

    ch.dispLevel = newLinear > ch.dispLevel ? newLinear : ch.dispLevel * kDecay;

    if (newLinear >= ch.holdLevel) {
        ch.holdLevel = newLinear;
        ch.holdTicks = kHoldTime;
    } else {
        if (ch.holdTicks > 0) {
            --ch.holdTicks;
        } else {
            ch.holdLevel *= 0.92f;
        }
    }

    ch.peakDb = ch.holdLevel > 1.0e-6f
        ? 20.0f * std::log10(ch.holdLevel)
        : -100.0f;
}

void GainMeterView::setLevels(const float inL, const float inR,
                               const float outL, const float outR) noexcept {
    updateChannel(chInL,  inL);
    updateChannel(chInR,  inR);
    updateChannel(chOutL, outL);
    updateChannel(chOutR, outR);
}

void GainMeterView::drawBar(juce::Graphics& g,
                             juce::Rectangle<float> slot,
                             const float normLevel,
                             const float normHold,
                             const juce::Colour colour) const noexcept {
    const float totalH   = slot.getHeight();
    const float segTotal = static_cast<float>(kSegments);
    const float segH     = (totalH - kSegGap * (segTotal - 1.0f)) / segTotal;
    if (segH <= 0.0f) return;

    const int activeSeg = juce::roundToInt(normLevel * (segTotal - 1.0f));
    const int holdSeg   = juce::roundToInt(normHold  * (segTotal - 1.0f));

    const auto green  = juce::Colour(0xff22cc44);
    const auto yellow = juce::Colour(0xffddcc00);
    const auto red    = juce::Colour(0xffee2200);
    constexpr float kDimAlpha = 0.11f;

    for (int i = 0; i < kSegments; ++i) {
        const float y  = slot.getBottom()
                       - (static_cast<float>(i) + 1.0f) * (segH + kSegGap)
                       + kSegGap;
        const juce::Rectangle<float> seg { slot.getX(), y, slot.getWidth(), segH };

        const bool isHold = (i == holdSeg && normHold > 0.0f);
        const bool isLit  = (i <= activeSeg && normLevel > 0.0f);

        juce::Colour base;
        if (i >= kRedStart)       base = red;
        else if (i >= kYellowStart) base = yellow;
        else                        base = green;

        juce::ignoreUnused(colour);

        if (isHold)
            g.setColour(base.interpolatedWith(juce::Colours::white, 0.55f));
        else if (isLit)
            g.setColour(base);
        else
            g.setColour(base.withAlpha(kDimAlpha));

        g.fillRect(seg);
    }
}

void GainMeterView::drawScale(juce::Graphics& g,
                               const juce::Rectangle<float> scaleSlot,
                               const juce::Rectangle<float> barSlot,
                               juce::Justification just) const noexcept {
    g.setFont(juce::FontOptions().withHeight(8.5f));
    const float totalH = barSlot.getHeight();

    for (const int db : kScaleMarks) {
        const float norm = (static_cast<float>(db) - kDbFloor) / kDbRange;
        const float y    = barSlot.getBottom() - norm * totalH;

        g.setColour(juce::Colours::white.withAlpha(0.38f));
        g.drawLine(barSlot.getX() - 1.0f, y, barSlot.getRight() + 1.0f, y, 0.5f);

        g.setColour(juce::Colours::white.withAlpha(0.45f));
        g.drawText(juce::String(db),
                   juce::Rectangle<float>(scaleSlot.getX(), y - 5.0f,
                                          scaleSlot.getWidth(), 10.0f),
                   just, false);
    }
}

void GainMeterView::paint(juce::Graphics& g) {
    const auto bounds = getLocalBounds().toFloat();
    const float w = bounds.getWidth();

    // Background
    g.setColour(juce::Colour(0xff101018));
    g.fillRoundedRectangle(bounds, 5.0f);
    g.setColour(juce::Colour(0xff2a3142));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 5.0f, 1.0f);

    // Layout: [scaleL | IN-L | gap | IN-R | divider | OUT-L | gap | OUT-R | scaleR]
    // Sizes chosen to fit in ~90px width after padding.
    constexpr float kPadX    = 5.0f;
    constexpr float kPadTop  = 18.0f; // room for IN/OUT labels
    constexpr float kPadBot  = 18.0f; // room for peak readout
    constexpr float kScaleW  = 18.0f;
    constexpr float kBarW    = 10.0f;
    constexpr float kBarGap  = 2.0f;
    constexpr float kDivW    = 5.0f;

    const float barH  = bounds.getHeight() - kPadTop - kPadBot;
    const float barY  = bounds.getY() + kPadTop;
    const float readY = barY + barH + 2.0f;

    // X positions: all bars packed between left and right scale columns
    const float x0 = bounds.getX() + kPadX + kScaleW; // IN-L bar
    const float x1 = x0 + kBarW + kBarGap;             // IN-R bar
    const float x2 = x1 + kBarW + kDivW;               // OUT-L bar
    const float x3 = x2 + kBarW + kBarGap;             // OUT-R bar

    // Scale column positions
    const juce::Rectangle<float> scaleL { bounds.getX() + kPadX, barY, kScaleW - 2.0f, barH };
    const juce::Rectangle<float> scaleR { x3 + kBarW + 2.0f, barY, kScaleW - 2.0f, barH };

    // Section bar areas (for grid lines in drawScale)
    const juce::Rectangle<float> inArea  { x0, barY, kBarW * 2.0f + kBarGap, barH };
    const juce::Rectangle<float> outArea { x2, barY, kBarW * 2.0f + kBarGap, barH };

    // "IN" / "OUT" section labels
    g.setFont(juce::FontOptions().withHeight(9.5f).withKerningFactor(0.08f));
    g.setColour(inputColour);
    g.drawText("IN",
               juce::Rectangle<float>(x0, bounds.getY() + 3.0f, kBarW * 2.0f + kBarGap, 12.0f),
               juce::Justification::centred, false);

    g.setColour(accentColour.withAlpha(0.85f));
    g.drawText("OUT",
               juce::Rectangle<float>(x2, bounds.getY() + 3.0f, kBarW * 2.0f + kBarGap, 12.0f),
               juce::Justification::centred, false);

    // L/R micro-labels
    g.setFont(juce::FontOptions().withHeight(7.5f));
    g.setColour(juce::Colours::white.withAlpha(0.25f));
    g.drawText("L", juce::Rectangle<float>(x0, bounds.getY() + 13.0f, kBarW, 6.0f), juce::Justification::centred, false);
    g.drawText("R", juce::Rectangle<float>(x1, bounds.getY() + 13.0f, kBarW, 6.0f), juce::Justification::centred, false);
    g.drawText("L", juce::Rectangle<float>(x2, bounds.getY() + 13.0f, kBarW, 6.0f), juce::Justification::centred, false);
    g.drawText("R", juce::Rectangle<float>(x3, bounds.getY() + 13.0f, kBarW, 6.0f), juce::Justification::centred, false);

    // Scale labels (left side, right-justified; right side, left-justified)
    drawScale(g, scaleL, inArea,  juce::Justification::centredRight);
    drawScale(g, scaleR, outArea, juce::Justification::centredLeft);

    // Draw the four bars
    drawBar(g, { x0, barY, kBarW, barH }, linearToNorm(chInL.dispLevel),  linearToNorm(chInL.holdLevel),  inputColour);
    drawBar(g, { x1, barY, kBarW, barH }, linearToNorm(chInR.dispLevel),  linearToNorm(chInR.holdLevel),  inputColour);
    drawBar(g, { x2, barY, kBarW, barH }, linearToNorm(chOutL.dispLevel), linearToNorm(chOutL.holdLevel), accentColour);
    drawBar(g, { x3, barY, kBarW, barH }, linearToNorm(chOutR.dispLevel), linearToNorm(chOutR.holdLevel), accentColour);

    // IN/OUT separator
    g.setColour(juce::Colour(0xff2a3142));
    const float divX = x1 + kBarW + kDivW * 0.5f;
    g.drawLine(divX, bounds.getY() + 6.0f, divX, bounds.getBottom() - 6.0f, 1.0f);

    // Peak readouts
    const auto fmtDb = [](const float db) -> juce::String {
        if (db <= -99.0f) return "-∞";
        if (db >= 0.0f)   return "+0.0";
        return juce::String(db, 1);
    };

    g.setFont(juce::FontOptions().withHeight(8.5f));
    g.setColour(inputColour.withAlpha(0.80f));
    // Show max of L/R for each section to keep it compact
    const float inPeak  = std::max(chInL.peakDb,  chInR.peakDb);
    const float outPeak = std::max(chOutL.peakDb, chOutR.peakDb);

    const juce::Colour inReadColour  = inPeak  >= -0.5f ? juce::Colour(0xffee4422) : inputColour.withAlpha(0.75f);
    const juce::Colour outReadColour = outPeak >= -0.5f ? juce::Colour(0xffee4422) : accentColour.withAlpha(0.85f);

    g.setColour(inReadColour);
    g.drawText(fmtDb(inPeak),
               juce::Rectangle<float>(x0, readY, kBarW * 2.0f + kBarGap, 12.0f),
               juce::Justification::centred, false);

    g.setColour(outReadColour);
    g.drawText(fmtDb(outPeak),
               juce::Rectangle<float>(x2, readY, kBarW * 2.0f + kBarGap, 12.0f),
               juce::Justification::centred, false);
}

} // namespace vxsuite
