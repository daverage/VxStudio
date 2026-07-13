#include "VxStudioLevelTraceView.h"

#include <algorithm>
#include <cmath>

namespace vxsuite {

LevelTraceView::LevelTraceView(const ProductTheme& theme)
    : accent(accentFromTheme(theme)) {}

void LevelTraceView::setSnapshot(const spectrum::SnapshotView& snapshot) {
    available = snapshot.active && snapshot.levelTraceCount > 1;
    totalSeconds = snapshot.levelTraceSeconds;
    traceCount = snapshot.levelTraceCount;
    dryRms = snapshot.dryRms;
    wetRms = snapshot.wetRms;
    dryTrace = snapshot.dryLevelTrace;
    wetTrace = snapshot.wetLevelTrace;
    repaint();
}

void LevelTraceView::setUnavailable() {
    available = false;
    totalSeconds = 0.0f;
    traceCount = 0;
    dryRms = 0.0f;
    wetRms = 0.0f;
    dryTrace.fill(0.0f);
    wetTrace.fill(0.0f);
    repaint();
}

void LevelTraceView::pushGainSample(const float gainDb, const float openness, const bool enabled) {
    gainTraceEnabled = enabled;
    if (!enabled) {
        gainTraceCount = 0;
        gainTraceWriteIndex = 0;
        return;
    }
    gainTrace[static_cast<size_t>(gainTraceWriteIndex)] = { juce::Time::getMillisecondCounterHiRes(), gainDb, openness };
    gainTraceWriteIndex = (gainTraceWriteIndex + 1) % kGainTraceSamples;
    gainTraceCount = std::min(gainTraceCount + 1, kGainTraceSamples);
}

void LevelTraceView::setReferenceDb(const float newReferenceDb, const bool enabled) {
    referenceEnabled = enabled;
    referenceDb = newReferenceDb;
}

void LevelTraceView::setZoomSeconds(const float seconds) {
    zoomSecondsValue = juce::jlimit(1.0f, 24.0f, seconds);
    repaint();
}

void LevelTraceView::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat();
    g.setColour(juce::Colours::black.withAlpha(0.16f));
    g.fillRoundedRectangle(bounds.translated(0.0f, 2.0f), 14.0f);
    g.setColour(juce::Colour(0xff11131a));
    g.fillRoundedRectangle(bounds, 14.0f);
    g.setColour(juce::Colours::white.withAlpha(0.08f));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 14.0f, 1.0f);

    auto content = bounds.reduced(12.0f, 10.0f);
    auto header = content.removeFromTop(20.0f);
    auto footer = content.removeFromBottom(18.0f);
    auto plot = content;

    g.setColour(juce::Colours::white.withAlpha(0.80f));
    g.setFont(juce::FontOptions().withHeight(13.0f).withStyle("Bold"));
    g.drawText("Level Trace", header.removeFromLeft(120.0f), juce::Justification::centredLeft, false);

    g.setFont(juce::FontOptions().withHeight(12.0f));
    g.setColour(juce::Colours::white.withAlpha(0.56f));
    const auto zoomText = "Zoom " + juce::String(zoomSecondsValue, zoomSecondsValue >= 10.0f ? 0 : 1) + "s";
    g.drawText(zoomText, header.removeFromRight(84.0f), juce::Justification::centredRight, false);

    if (!available || totalSeconds <= 0.0f || traceCount <= 1) {
        g.setColour(juce::Colours::white.withAlpha(0.45f));
        g.drawFittedText("Trace waiting for live audio",
                         plot.toNearestInt(),
                         juce::Justification::centred,
                         1);
        return;
    }

    for (float dbLine : { -36.0f, -24.0f, -12.0f, -6.0f }) {
        const float yNorm = juce::jlimit(0.0f, 1.0f, (dbLine + 42.0f) / 42.0f);
        const float y = plot.getBottom() - yNorm * plot.getHeight();
        g.setColour(juce::Colours::white.withAlpha(dbLine == -24.0f ? 0.12f : 0.06f));
        g.drawHorizontalLine(static_cast<int>(std::round(y)), plot.getX(), plot.getRight());
    }

    const float visibleSeconds = juce::jmin(totalSeconds, zoomSecondsValue);
    const int visibleSamples = juce::jlimit(2, traceCount,
        static_cast<int>(std::round(static_cast<float>(traceCount) * (visibleSeconds / totalSeconds))));
    const int startIndex = juce::jlimit(0, traceCount - visibleSamples, traceCount - visibleSamples);
    const float coverage = juce::jlimit(0.0f, 1.0f, visibleSeconds / juce::jmax(zoomSecondsValue, 1.0f));
    const float plotWidth = plot.getWidth() * coverage;
    const float plotStartX = plot.getRight() - plotWidth;

    juce::Path dryPath;
    juce::Path wetPath;
    for (int i = 0; i < visibleSamples; ++i) {
        const float x = plotStartX + (static_cast<float>(i) / static_cast<float>(std::max(1, visibleSamples - 1))) * plotWidth;
        const float dryDb = juce::jlimit(-42.0f, 0.0f, levelToDb(sampleAt(dryTrace, startIndex + i)));
        const float wetDb = juce::jlimit(-42.0f, 0.0f, levelToDb(sampleAt(wetTrace, startIndex + i)));
        const float dryY = plot.getBottom() - ((dryDb + 42.0f) / 42.0f) * plot.getHeight();
        const float wetY = plot.getBottom() - ((wetDb + 42.0f) / 42.0f) * plot.getHeight();
        if (i == 0) {
            dryPath.startNewSubPath(x, dryY);
            wetPath.startNewSubPath(x, wetY);
        } else {
            dryPath.lineTo(x, dryY);
            wetPath.lineTo(x, wetY);
        }
    }

    g.setColour(juce::Colours::white.withAlpha(0.36f));
    g.strokePath(dryPath, juce::PathStrokeType(1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(accent.withAlpha(0.92f));
    g.strokePath(wetPath, juce::PathStrokeType(1.9f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Optional reference line: the level the processing steers toward,
    // drawn on the same dBFS scale as the dry/wet traces.
    if (referenceEnabled && referenceDb > -60.0f) {
        const float refClamped = juce::jlimit(-42.0f, 0.0f, referenceDb);
        const float refY = plot.getBottom() - ((refClamped + 42.0f) / 42.0f) * plot.getHeight();
        g.setColour(accent.withAlpha(0.38f));
        const float dashes[] = { 5.0f, 4.0f };
        g.drawDashedLine(juce::Line<float>(plot.getX(), refY, plot.getRight(), refY),
                         dashes, 2, 1.0f);
    }

    // Optional gain-trace overlay: the processing gain trajectory (e.g. a
    // rider's fader), +/-12 dB mapped over the plot height, 0 dB centred.
    // Segments where the gate/hold is engaged (openness < 0.5) draw dimmed.
    if (gainTraceEnabled && gainTraceCount > 1) {
        const double nowMs = juce::Time::getMillisecondCounterHiRes();
        const double windowMs = static_cast<double>(visibleSeconds) * 1000.0;
        juce::Path openPath;
        juce::Path heldPath;
        bool openStarted = false;
        bool heldStarted = false;
        float prevX = 0.0f;
        float prevY = 0.0f;
        bool havePrev = false;
        for (int i = gainTraceCount - 1; i >= 0; --i) {
            const int index = (gainTraceWriteIndex - 1 - (gainTraceCount - 1 - i) + 2 * kGainTraceSamples) % kGainTraceSamples;
            const auto& sample = gainTrace[static_cast<size_t>(index)];
            const double ageMs = nowMs - sample.timeMs;
            if (ageMs > windowMs)
                break;
            const float x = plot.getRight() - static_cast<float>(ageMs / windowMs) * plotWidth;
            const float gainNorm = juce::jlimit(0.0f, 1.0f, (sample.gainDb + 12.0f) / 24.0f);
            const float y = plot.getBottom() - gainNorm * plot.getHeight();
            const bool open = sample.openness >= 0.5f;
            auto& path = open ? openPath : heldPath;
            bool& started = open ? openStarted : heldStarted;
            if (havePrev)
                { if (!started) path.startNewSubPath(prevX, prevY); else path.lineTo(prevX, prevY); path.lineTo(x, y); started = true; }
            else if (!started)
                { path.startNewSubPath(x, y); started = true; }
            prevX = x;
            prevY = y;
            havePrev = true;
        }
        if (heldStarted) {
            g.setColour(accent.brighter(0.9f).withAlpha(0.28f));
            g.strokePath(heldPath, juce::PathStrokeType(1.1f));
        }
        if (openStarted) {
            g.setColour(accent.brighter(0.9f).withAlpha(0.85f));
            g.strokePath(openPath, juce::PathStrokeType(1.1f));
        }
    }

    g.setFont(juce::FontOptions().withHeight(11.0f));
    g.setColour(juce::Colours::white.withAlpha(0.56f));
    g.drawText("Dry " + juce::String(levelToDb(dryRms), 1) + " dB",
               footer.removeFromLeft(110.0f),
               juce::Justification::centredLeft, false);
    g.drawText("Wet " + juce::String(levelToDb(wetRms), 1) + " dB",
               footer.removeFromLeft(110.0f),
               juce::Justification::centredLeft, false);
    g.drawText("Showing last " + juce::String(visibleSeconds, visibleSeconds >= 10.0f ? 0 : 1) + "s",
               footer,
               juce::Justification::centredRight, false);
}

juce::Colour LevelTraceView::accentFromTheme(const ProductTheme& theme) noexcept {
    return juce::Colour::fromFloatRGBA(theme.accentRgb[0], theme.accentRgb[1], theme.accentRgb[2], 1.0f);
}

float LevelTraceView::levelToDb(const float linear) noexcept {
    return juce::Decibels::gainToDecibels(std::max(linear, 1.0e-5f), -100.0f);
}

float LevelTraceView::sampleAt(const std::array<float, spectrum::kLevelTraceSamples>& values,
                               const int index) const noexcept {
    const int clamped = juce::jlimit(0, spectrum::kLevelTraceSamples - 1, index);
    return values[static_cast<std::size_t>(clamped)];
}

} // namespace vxsuite
