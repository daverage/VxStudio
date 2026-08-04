#include "VxStudioPitchTraceView.h"

#include <cmath>
#include <vector>

namespace vxsuite {

PitchTraceView::PitchTraceView(const ProductTheme& theme)
    : accent(juce::Colour::fromFloatRGBA(theme.accentRgb[0], theme.accentRgb[1],
                                         theme.accentRgb[2], 1.0f)),
      background(juce::Colour::fromFloatRGBA(theme.panelRgb[0], theme.panelRgb[1],
                                             theme.panelRgb[2], 1.0f)),
      text(juce::Colour::fromFloatRGBA(theme.textRgb[0], theme.textRgb[1],
                                       theme.textRgb[2], 1.0f)) {
    setInterceptsMouseClicks(false, false);
}

void PitchTraceView::pushSample(const float detectedCents, const float correctedCents,
                                const float confidence) {
    samples[static_cast<size_t>(writeIndex)] = {
        juce::Time::getMillisecondCounterHiRes(), detectedCents, correctedCents, confidence
    };
    writeIndex = (writeIndex + 1) % kCapacity;
    count = std::min(count + 1, kCapacity);
    repaint();
}

void PitchTraceView::setZoomSeconds(const float seconds) {
    zoomSecondsValue = juce::jlimit(1.0f, 30.0f, seconds);
    repaint();
}

juce::String PitchTraceView::noteNameForCents(const float cents) {
    static const char* names[12] = { "C", "C#", "D", "D#", "E", "F",
                                     "F#", "G", "G#", "A", "A#", "B" };
    const int midi = 69 + static_cast<int>(std::lround(cents / 100.0f));
    if (midi < 0 || midi > 127)
        return {};
    return juce::String(names[midi % 12]) + juce::String(midi / 12 - 1);
}

void PitchTraceView::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat();
    g.setColour(background);
    g.fillRoundedRectangle(bounds, 6.0f);

    const double nowMs = juce::Time::getMillisecondCounterHiRes();
    const double windowMs = 1000.0 * zoomSecondsValue;

    // Fixed-span window (+/-350c) whose centre follows the median of the
    // recent voiced pitch, slew-limited so it glides between notes and is
    // immune to octave-error spikes. Fitting min/max of a whole melody
    // makes cent-level detail (the entire point) invisible.
    std::vector<float> recent;
    recent.reserve(64);
    for (int i = 0; i < count; ++i) {
        const auto& s = samples[static_cast<size_t>(i)];
        if (s.confidence <= 0.0f || nowMs - s.timeMs > 1500.0)
            continue;
        recent.push_back(s.detectedCents);
    }
    if (!recent.empty()) {
        std::nth_element(recent.begin(), recent.begin() + recent.size() / 2, recent.end());
        const float median = recent[recent.size() / 2];
        if (!viewCentreValid || std::abs(median - viewCentreCents) > 600.0f) {
            viewCentreCents = median;
            viewCentreValid = true;
        } else {
            const float dtSec = lastPaintMs > 0.0
                ? static_cast<float>(std::min(0.2, (nowMs - lastPaintMs) / 1000.0)) : 0.033f;
            const float maxStep = 1200.0f * dtSec;   // glide at up to 1 octave/s
            const float delta = median - viewCentreCents;
            viewCentreCents += juce::jlimit(-maxStep, maxStep, delta);
        }
    }
    lastPaintMs = nowMs;

    bool anyVoiced = false;
    for (int i = 0; i < count && !anyVoiced; ++i) {
        const auto& s = samples[static_cast<size_t>(i)];
        anyVoiced = s.confidence > 0.0f && nowMs - s.timeMs <= windowMs;
    }
    if (!anyVoiced || !viewCentreValid) {
        g.setColour(text.withAlpha(0.35f));
        g.setFont(13.0f);
        g.drawText("listening for pitch...", getLocalBounds(), juce::Justification::centred);
        return;
    }
    const float halfSpan = 350.0f;
    const float top = viewCentreCents + halfSpan;
    const float bottom = viewCentreCents - halfSpan;

    const auto xForTime = [&](const double timeMs) {
        return bounds.getRight()
            - static_cast<float>((nowMs - timeMs) / windowMs) * bounds.getWidth();
    };
    const auto yForCents = [&](const float cents) {
        return bounds.getY()
            + (top - cents) / (top - bottom) * bounds.getHeight();
    };

    // Semitone grid with note names on whole-semitone lines.
    const int firstLine = static_cast<int>(std::ceil(bottom / 100.0f));
    const int lastLine = static_cast<int>(std::floor(top / 100.0f));
    g.setFont(10.0f);
    for (int line = firstLine; line <= lastLine; ++line) {
        const float cents = 100.0f * static_cast<float>(line);
        const float y = yForCents(cents);
        const bool isC = ((69 + line) % 12 + 12) % 12 == 0;   // midi % 12 == 0
        g.setColour(text.withAlpha(isC ? 0.22f : 0.10f));
        g.drawHorizontalLine(static_cast<int>(y), bounds.getX() + 34.0f, bounds.getRight() - 4.0f);
        if (halfSpan < 900.0f || isC) {
            g.setColour(text.withAlpha(0.45f));
            g.drawText(noteNameForCents(cents),
                       static_cast<int>(bounds.getX()) + 4, static_cast<int>(y) - 7, 30, 14,
                       juce::Justification::centredLeft);
        }
    }

    // Collect visible points into gap-separated segments, oldest to newest.
    struct Point {
        float x, ySung, yTuned;
    };
    std::vector<std::vector<Point>> segments;
    segments.emplace_back();
    const int start = (writeIndex - count + 2 * kCapacity) % kCapacity;
    for (int i = 0; i < count; ++i) {
        const int index = (start + i) % kCapacity;
        const auto& s = samples[static_cast<size_t>(index)];
        if (s.timeMs <= 0.0 || nowMs - s.timeMs > windowMs || s.confidence <= 0.0f) {
            if (!segments.back().empty())
                segments.emplace_back();
            continue;
        }
        segments.back().push_back({ xForTime(s.timeMs),
                                    yForCents(s.detectedCents),
                                    yForCents(s.correctedCents) });
    }

    // Correction ribbon: shaded area between sung and tuned wherever the
    // engine changed the pitch — the amount of correction, at a glance.
    for (const auto& segment : segments) {
        if (segment.size() < 2)
            continue;
        juce::Path ribbon;
        ribbon.startNewSubPath(segment.front().x, segment.front().ySung);
        for (size_t i = 1; i < segment.size(); ++i)
            ribbon.lineTo(segment[i].x, segment[i].ySung);
        for (size_t i = segment.size(); i-- > 0;)
            ribbon.lineTo(segment[i].x, segment[i].yTuned);
        ribbon.closeSubPath();
        g.setColour(accent.withAlpha(0.16f));
        g.fillPath(ribbon);
    }

    // Sung line: dashed, so it stays identifiable even where the tuned line
    // sits exactly on top of it (no correction happening).
    {
        juce::Path sung;
        for (const auto& segment : segments) {
            if (segment.empty())
                continue;
            sung.startNewSubPath(segment.front().x, segment.front().ySung);
            for (size_t i = 1; i < segment.size(); ++i)
                sung.lineTo(segment[i].x, segment[i].ySung);
        }
        juce::Path dashed;
        const float dashes[] = { 5.0f, 4.0f };
        juce::PathStrokeType(1.6f).createDashedStroke(dashed, sung, dashes, 2);
        g.setColour(text.withAlpha(0.60f));
        g.fillPath(dashed);
    }

    // Tuned line: solid accent — what is actually rendered.
    {
        juce::Path tuned;
        for (const auto& segment : segments) {
            if (segment.empty())
                continue;
            tuned.startNewSubPath(segment.front().x, segment.front().yTuned);
            for (size_t i = 1; i < segment.size(); ++i)
                tuned.lineTo(segment[i].x, segment[i].yTuned);
        }
        g.setColour(accent);
        g.strokePath(tuned, juce::PathStrokeType(2.0f));
    }

    g.setColour(text.withAlpha(0.5f));
    g.setFont(10.0f);
    g.drawText("sung", bounds.getRight() - 92.0f, bounds.getY() + 4.0f, 40.0f, 12.0f,
               juce::Justification::centredRight);
    g.setColour(accent.withAlpha(0.9f));
    g.drawText("tuned", bounds.getRight() - 48.0f, bounds.getY() + 4.0f, 44.0f, 12.0f,
               juce::Justification::centredRight);
}

} // namespace vxsuite
