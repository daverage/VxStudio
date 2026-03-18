#include "VxSpectrumEditor.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace {

constexpr float kMinDb = -78.0f;
constexpr float kMaxDb = 6.0f;
constexpr std::array<float, 5> kGridDb { -72.0f, -54.0f, -36.0f, -18.0f, 0.0f };
constexpr std::array<float, 4> kGridFreq { 50.0f, 200.0f, 1000.0f, 5000.0f };
constexpr float kChainMatchThreshold = 0.42f;
constexpr int kMaxMatchedStages = 8;
constexpr int kMaxCorrelationLag = 24;
constexpr double kSilentSnapshotHoldMs = 450.0;
constexpr float kSignalThreshold = 1.0e-6f;
constexpr float kSpectrumAttack = 0.34f;
constexpr float kSpectrumRelease = 0.12f;

juce::String labelFromChars(const auto& chars) {
    return juce::String(chars.data());
}

} // namespace

VXSpectrumEditor::VXSpectrumEditor(VXSpectrumAudioProcessor& owner)
    : juce::AudioProcessorEditor(&owner),
      processor(owner) {
    setResizable(true, false);
    setResizeLimits(920, 560, 1440, 960);
    setSize(1120, 700);

    fft.prepare(9);
    for (int i = 0; i < vxsuite::spectrum::kWaveformSamples; ++i) {
        const float phase = juce::MathConstants<float>::twoPi * static_cast<float>(i)
            / static_cast<float>(vxsuite::spectrum::kWaveformSamples - 1);
        window[static_cast<std::size_t>(i)] = 0.5f - 0.5f * std::cos(phase);
    }

    traceVisibility.fill(true);

    titleLabel.setText("VX Spectrum", juce::dontSendNotification);
    titleLabel.setFont(juce::FontOptions().withHeight(30.0f).withStyle("Bold"));
    addAndMakeVisible(titleLabel);

    subtitleLabel.setText("Dry foundation, final wet result, and stabilised live traces per VX processor.",
                          juce::dontSendNotification);
    subtitleLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.72f));
    subtitleLabel.setFont(juce::FontOptions().withHeight(14.0f));
    addAndMakeVisible(subtitleLabel);

    diagnosticsLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.68f));
    diagnosticsLabel.setFont(juce::FontOptions().withHeight(12.5f));
    diagnosticsLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(diagnosticsLabel);

    dryButton.setButtonText("Dry");
    dryButton.setClickingTogglesState(true);
    dryButton.setToggleState(showDryLayer, juce::dontSendNotification);
    dryButton.onClick = [this] {
        showDryLayer = dryButton.getToggleState();
        repaint();
    };
    addAndMakeVisible(dryButton);

    finalWetButton.setButtonText("Final Wet");
    finalWetButton.setClickingTogglesState(true);
    finalWetButton.setToggleState(showFinalWetLayer, juce::dontSendNotification);
    finalWetButton.onClick = [this] {
        showFinalWetLayer = finalWetButton.getToggleState();
        repaint();
    };
    addAndMakeVisible(finalWetButton);

    footerLabel.setText("Insert last in the chain. Traces use a short visual hold so real-time changes are easier to compare.",
                        juce::dontSendNotification);
    footerLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.56f));
    footerLabel.setFont(juce::FontOptions().withHeight(13.0f));
    footerLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(footerLabel);

    startTimerHz(20);
    refreshTelemetry();
}

void VXSpectrumEditor::paint(juce::Graphics& g) {
    const auto& theme = processor.theme();
    const auto bg = colourFromRgb(theme.backgroundRgb);
    const auto panel = colourFromRgb(theme.panelRgb);
    const auto accent = colourFromRgb(theme.accentRgb);
    const auto text = colourFromRgb(theme.textRgb);

    g.fillAll(bg);

    auto local = getLocalBounds().toFloat();
    juce::ColourGradient wash(bg.brighter(0.08f), local.getTopLeft(),
                              accent.withAlpha(0.10f), local.getBottomRight(), false);
    g.setGradientFill(wash);
    g.fillRect(local);

    g.setColour(panel.withAlpha(0.94f));
    g.fillRoundedRectangle(plotBounds.toFloat(), 24.0f);
    g.setColour(text.withAlpha(0.08f));
    g.drawRoundedRectangle(plotBounds.toFloat(), 24.0f, 1.0f);

    g.setColour(panel.brighter(0.06f).withAlpha(0.94f));
    g.fillRoundedRectangle(legendBounds.toFloat(), 20.0f);
    g.setColour(text.withAlpha(0.08f));
    g.drawRoundedRectangle(legendBounds.toFloat(), 20.0f, 1.0f);

    const auto plot = plotBounds.toFloat().reduced(24.0f, 22.0f);
    g.setColour(text.withAlpha(0.06f));
    for (float db : kGridDb) {
        const float y = spectrumY(db, plot);
        g.drawHorizontalLine(juce::roundToInt(y), plot.getX(), plot.getRight());
        g.setColour(text.withAlpha(0.42f));
        g.setFont(juce::FontOptions().withHeight(11.0f));
        g.drawText(juce::String(static_cast<int>(db)) + " dB",
                   juce::Rectangle<float>(plot.getX(), y - 10.0f, 52.0f, 16.0f),
                   juce::Justification::left, false);
        g.setColour(text.withAlpha(0.06f));
    }

    for (float freq : kGridFreq) {
        const float x = spectrumX(freq, wetSampleRate, plot);
        g.drawVerticalLine(juce::roundToInt(x), plot.getY(), plot.getBottom());
        g.setColour(text.withAlpha(0.42f));
        const juce::String label = freq >= 1000.0f ? juce::String(freq / 1000.0f, 1) + " kHz"
                                                   : juce::String(static_cast<int>(freq)) + " Hz";
        g.drawText(label,
                   juce::Rectangle<float>(x - 30.0f, plot.getBottom() - 18.0f, 60.0f, 14.0f),
                   juce::Justification::centred, false);
        g.setColour(text.withAlpha(0.06f));
    }

    if (hasDrySpectrum && showDryLayer) {
        auto dryPath = makeFilledSpectrumPath(drySpectrum, drySampleRate, plot);
        g.setColour(juce::Colour(0xff80a3bd).withAlpha(0.18f));
        g.fillPath(dryPath);
        g.setColour(juce::Colour(0xffb9d3e4).withAlpha(0.28f));
        g.strokePath(makeSpectrumPath(drySpectrum, drySampleRate, plot),
                     juce::PathStrokeType(1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    if (hasWetSpectrum && showFinalWetLayer) {
        auto wetPath = makeFilledSpectrumPath(finalWetSpectrum, wetSampleRate, plot);
        g.setColour(accent.withAlpha(0.13f));
        g.fillPath(wetPath);
        g.setColour(accent.withAlpha(0.38f));
        g.strokePath(makeSpectrumPath(finalWetSpectrum, wetSampleRate, plot),
                     juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    for (const auto& trace : traces) {
        if (!traceVisibility[static_cast<std::size_t>(trace.snapshot.slotIndex)])
            continue;

        const auto colour = colourFromRgb(trace.snapshot.accentRgb, 0.96f);
        g.setColour(colour);
        g.strokePath(makeSpectrumPath(trace.wetSpectrum, trace.snapshot.sampleRate, plot),
                     juce::PathStrokeType(1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    if (!hasWetSpectrum && traces.empty()) {
        g.setColour(text.withAlpha(0.72f));
        g.setFont(juce::FontOptions().withHeight(20.0f).withStyle("Bold"));
        g.drawFittedText("Waiting for VX telemetry",
                         plotBounds.reduced(40, 90),
                         juce::Justification::centred,
                         1);
        g.setFont(juce::FontOptions().withHeight(14.0f));
        g.setColour(text.withAlpha(0.52f));
        g.drawFittedText("Open other VX plugins in the same host session and insert VX Spectrum last in the chain.",
                         plotBounds.reduced(72, 130),
                         juce::Justification::centred,
                         2);
    } else if (hasWetSpectrum && traces.empty()) {
        auto infoBounds = plot.withTrimmedBottom(plot.getHeight() - 44.0f)
                             .withTrimmedRight(std::max(0.0f, plot.getWidth() - 480.0f));
        g.setColour(panel.brighter(0.08f).withAlpha(0.94f));
        g.fillRoundedRectangle(infoBounds, 12.0f);
        g.setColour(text.withAlpha(0.12f));
        g.drawRoundedRectangle(infoBounds, 12.0f, 1.0f);
        g.setColour(text.withAlpha(0.74f));
        g.setFont(juce::FontOptions().withHeight(13.5f).withStyle("Bold"));
        g.drawText("No upstream VX chain matched yet", infoBounds.reduced(14.0f, 6.0f), juce::Justification::centredLeft, false);
    }
}

void VXSpectrumEditor::resized() {
    auto area = getLocalBounds().reduced(20, 18);
    auto header = area.removeFromTop(74);
    titleLabel.setBounds(header.removeFromTop(34));
    subtitleLabel.setBounds(header.removeFromTop(22));
    auto diagRow = header.removeFromTop(20);
    diagnosticsLabel.setBounds(diagRow);
    footerLabel.setBounds(area.removeFromBottom(24));

    area.removeFromTop(10);
    legendBounds = area.removeFromRight(250);
    area.removeFromRight(14);
    plotBounds = area;

    auto toggleArea = legendBounds.reduced(18, 18);
    dryButton.setBounds(toggleArea.removeFromTop(26));
    toggleArea.removeFromTop(6);
    finalWetButton.setBounds(toggleArea.removeFromTop(26));
    toggleArea.removeFromTop(6);
    for (auto& button : traceButtons) {
        if (!button->isVisible())
            continue;
        button->setBounds(toggleArea.removeFromTop(26));
        toggleArea.removeFromTop(6);
    }
}

void VXSpectrumEditor::timerCallback() {
    refreshTelemetry();
}

bool VXSpectrumEditor::snapshotHasSignal(const vxsuite::spectrum::SnapshotView& snapshot) const noexcept {
    return !snapshot.silent
        && (snapshot.dryRms > kSignalThreshold || snapshot.wetRms > kSignalThreshold);
}

bool VXSpectrumEditor::collectSnapshotForSlot(const int slotIndex,
                                              const double nowMs,
                                              vxsuite::spectrum::SnapshotView& snapshotOut) {
    vxsuite::spectrum::SnapshotView currentSnapshot;
    if (!vxsuite::spectrum::SnapshotRegistry::instance().readSlot(slotIndex, currentSnapshot)) {
        snapshotCache[static_cast<std::size_t>(slotIndex)].valid = false;
        return false;
    }

    auto& cache = snapshotCache[static_cast<std::size_t>(slotIndex)];
    if (snapshotHasSignal(currentSnapshot)) {
        cache.valid = true;
        cache.lastActiveMs = nowMs;
        cache.snapshot = currentSnapshot;
        snapshotOut = currentSnapshot;
        return true;
    }

    if (cache.valid && (nowMs - cache.lastActiveMs) <= kSilentSnapshotHoldMs) {
        snapshotOut = cache.snapshot;
        snapshotOut.active = currentSnapshot.active;
        snapshotOut.slotIndex = slotIndex;
        snapshotOut.order = currentSnapshot.order;
        snapshotOut.instanceId = currentSnapshot.instanceId;
        snapshotOut.showTrace = currentSnapshot.showTrace;
        snapshotOut.lastPublishMs = currentSnapshot.lastPublishMs;
        snapshotOut.silent = false;
        return true;
    }

    cache.valid = false;
    snapshotOut = currentSnapshot;
    return true;
}

void VXSpectrumEditor::refreshTelemetry() {
    std::vector<vxsuite::spectrum::SnapshotView> snapshots;
    const double nowMs = juce::Time::getMillisecondCounterHiRes();
    for (int slotIndex = 0; slotIndex < vxsuite::spectrum::SnapshotRegistry::instance().maxSlots(); ++slotIndex) {
        vxsuite::spectrum::SnapshotView snapshot;
        if (collectSnapshotForSlot(slotIndex, nowMs, snapshot))
            snapshots.push_back(snapshot);
    }

    std::sort(snapshots.begin(), snapshots.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.order < rhs.order;
    });

    traces.clear();
    hasDrySpectrum = false;
    hasWetSpectrum = false;

    const auto selfId = processor.telemetryInstanceId();
    std::optional<vxsuite::spectrum::SnapshotView> selfSnapshot;
    std::vector<vxsuite::spectrum::SnapshotView> candidates;
    for (const auto& snapshot : snapshots) {
        if (snapshot.instanceId == selfId) {
            selfSnapshot = snapshot;
            continue;
        }

        if (!snapshot.showTrace || snapshot.silent)
            continue;
        candidates.push_back(snapshot);
    }

    if (selfSnapshot.has_value()) {
        wetSampleRate = selfSnapshot->sampleRate;
        std::array<float, vxsuite::spectrum::kWaveformSamples / 2 + 1> targetSpectrum {};
        computeSpectrum(selfSnapshot->wetWaveform, selfSnapshot->sampleRate, targetSpectrum);
        smoothSpectrum(finalWetSpectrum, targetSpectrum, finalWetSpectrumStateValid);
        hasWetSpectrum = true;

        std::array<float, vxsuite::spectrum::kWaveformSamples> targetWaveform = selfSnapshot->dryWaveform;
        std::vector<bool> used(candidates.size(), false);
        std::vector<TraceRenderData> reversedChain;
        int currentMaxOrder = selfSnapshot->order;

        while (static_cast<int>(reversedChain.size()) < kMaxMatchedStages) {
            float bestScore = 0.0f;
            float bestCorrelation = 0.0f;
            float bestEnvelopeCorrelation = 0.0f;
            int bestIndex = -1;
            for (int index = 0; index < static_cast<int>(candidates.size()); ++index) {
                if (used[static_cast<std::size_t>(index)])
                    continue;

                float wetCorrelation = 0.0f;
                float envelopeCorrelation = 0.0f;
                const float score = stageMatchScore(candidates[static_cast<std::size_t>(index)],
                                                    targetWaveform,
                                                    currentMaxOrder,
                                                    wetCorrelation,
                                                    envelopeCorrelation);
                if (score > bestScore) {
                    bestScore = score;
                    bestCorrelation = wetCorrelation;
                    bestEnvelopeCorrelation = envelopeCorrelation;
                    bestIndex = index;
                }
            }

            if (bestIndex < 0 || std::max(bestCorrelation, bestEnvelopeCorrelation) < kChainMatchThreshold)
                break;

            used[static_cast<std::size_t>(bestIndex)] = true;
            TraceRenderData trace;
            trace.snapshot = candidates[static_cast<std::size_t>(bestIndex)];
            std::array<float, vxsuite::spectrum::kWaveformSamples / 2 + 1> targetSpectrum {};
            computeSpectrum(trace.snapshot.wetWaveform, trace.snapshot.sampleRate, targetSpectrum);
            smoothSpectrum(slotSpectrumState[static_cast<std::size_t>(trace.snapshot.slotIndex)],
                           targetSpectrum,
                           slotSpectrumStateValid[static_cast<std::size_t>(trace.snapshot.slotIndex)]);
            trace.wetSpectrum = slotSpectrumState[static_cast<std::size_t>(trace.snapshot.slotIndex)];
            targetWaveform = trace.snapshot.dryWaveform;
            currentMaxOrder = trace.snapshot.order;
            reversedChain.push_back(std::move(trace));
        }

        if (!reversedChain.empty()) {
            const auto& earliest = reversedChain.back().snapshot;
            drySampleRate = earliest.sampleRate;
            std::array<float, vxsuite::spectrum::kWaveformSamples / 2 + 1> targetSpectrum {};
            computeSpectrum(earliest.dryWaveform, earliest.sampleRate, targetSpectrum);
            smoothSpectrum(drySpectrum, targetSpectrum, drySpectrumStateValid);
            hasDrySpectrum = true;

            traces.assign(reversedChain.rbegin(), reversedChain.rend());
        } else {
            drySampleRate = selfSnapshot->sampleRate;
            std::array<float, vxsuite::spectrum::kWaveformSamples / 2 + 1> targetSpectrum {};
            computeSpectrum(selfSnapshot->dryWaveform, selfSnapshot->sampleRate, targetSpectrum);
            smoothSpectrum(drySpectrum, targetSpectrum, drySpectrumStateValid);
            hasDrySpectrum = true;
        }
    }

    auto debug = vxsuite::spectrum::SnapshotRegistry::instance().debugInfo();
    juce::StringArray detectedNames;
    for (const auto& snapshot : snapshots)
        detectedNames.add(labelFromChars(snapshot.productName));
    diagnosticsLabel.setText(
        "Telemetry: " + juce::String(debug.available ? "ready" : "unavailable")
            + " | active publishers: " + juce::String(debug.activeSlots)
            + " | trace publishers: " + juce::String(debug.traceSlots)
            + " | matched chain: " + juce::String(static_cast<int>(traces.size()))
            + " | backend: " + debug.backendName
            + (detectedNames.isEmpty() ? " | detected: none" : " | detected: " + detectedNames.joinIntoString(", ")),
        juce::dontSendNotification);

    refreshToggleButtons();
    repaint();
}

float VXSpectrumEditor::correlation(
    const std::array<float, vxsuite::spectrum::kWaveformSamples>& a,
    const std::array<float, vxsuite::spectrum::kWaveformSamples>& b) const noexcept {
    double dot = 0.0;
    double energyA = 0.0;
    double energyB = 0.0;

    for (int i = 0; i < vxsuite::spectrum::kWaveformSamples; ++i) {
        const double av = static_cast<double>(a[static_cast<std::size_t>(i)]);
        const double bv = static_cast<double>(b[static_cast<std::size_t>(i)]);
        dot += av * bv;
        energyA += av * av;
        energyB += bv * bv;
    }

    if (energyA <= 1.0e-12 || energyB <= 1.0e-12)
        return 0.0f;

    return static_cast<float>(std::abs(dot) / std::sqrt(energyA * energyB));
}

float VXSpectrumEditor::bestAlignedCorrelation(
    const std::array<float, vxsuite::spectrum::kWaveformSamples>& a,
    const std::array<float, vxsuite::spectrum::kWaveformSamples>& b,
    const int maxLag) const noexcept {
    float best = 0.0f;

    for (int lag = -maxLag; lag <= maxLag; ++lag) {
        double dot = 0.0;
        double energyA = 0.0;
        double energyB = 0.0;

        for (int i = 0; i < vxsuite::spectrum::kWaveformSamples; ++i) {
            const int j = i + lag;
            if (j < 0 || j >= vxsuite::spectrum::kWaveformSamples)
                continue;

            const double av = static_cast<double>(a[static_cast<std::size_t>(i)]);
            const double bv = static_cast<double>(b[static_cast<std::size_t>(j)]);
            dot += av * bv;
            energyA += av * av;
            energyB += bv * bv;
        }

        if (energyA <= 1.0e-12 || energyB <= 1.0e-12)
            continue;

        best = std::max(best, static_cast<float>(std::abs(dot) / std::sqrt(energyA * energyB)));
    }

    return best;
}

float VXSpectrumEditor::bestAlignedEnvelopeCorrelation(
    const std::array<float, vxsuite::spectrum::kWaveformSamples>& a,
    const std::array<float, vxsuite::spectrum::kWaveformSamples>& b,
    const int maxLag) const noexcept {
    float best = 0.0f;

    for (int lag = -maxLag; lag <= maxLag; ++lag) {
        double dot = 0.0;
        double energyA = 0.0;
        double energyB = 0.0;

        for (int i = 0; i < vxsuite::spectrum::kWaveformSamples; ++i) {
            const int j = i + lag;
            if (j < 0 || j >= vxsuite::spectrum::kWaveformSamples)
                continue;

            const double av = std::abs(static_cast<double>(a[static_cast<std::size_t>(i)]));
            const double bv = std::abs(static_cast<double>(b[static_cast<std::size_t>(j)]));
            dot += av * bv;
            energyA += av * av;
            energyB += bv * bv;
        }

        if (energyA <= 1.0e-12 || energyB <= 1.0e-12)
            continue;

        best = std::max(best, static_cast<float>(dot / std::sqrt(energyA * energyB)));
    }

    return best;
}

float VXSpectrumEditor::stageMatchScore(
    const vxsuite::spectrum::SnapshotView& candidate,
    const std::array<float, vxsuite::spectrum::kWaveformSamples>& targetWaveform,
    const int currentMaxOrder,
    float& wetCorrelationOut,
    float& envelopeCorrelationOut) const noexcept {
    wetCorrelationOut = 0.0f;
    envelopeCorrelationOut = 0.0f;
    wetCorrelationOut = bestAlignedCorrelation(candidate.wetWaveform, targetWaveform, kMaxCorrelationLag);
    envelopeCorrelationOut = bestAlignedEnvelopeCorrelation(candidate.wetWaveform, targetWaveform, kMaxCorrelationLag);
    const float dryCorrelation = bestAlignedCorrelation(candidate.dryWaveform, targetWaveform, kMaxCorrelationLag);

    const float orderDistance = static_cast<float>(std::abs(currentMaxOrder - candidate.order));
    const float proximity = 1.0f / (1.0f + orderDistance);
    const float changeReward = 1.0f - dryCorrelation;

    return 0.44f * wetCorrelationOut
        + 0.34f * envelopeCorrelationOut
        + 0.14f * proximity
        + 0.08f * changeReward;
}

void VXSpectrumEditor::refreshToggleButtons() {
    const int neededButtons = static_cast<int>(traces.size());
    while (static_cast<int>(traceButtons.size()) < neededButtons) {
        const int slot = static_cast<int>(traceButtons.size());
        auto button = std::make_unique<juce::ToggleButton>();
        button->setClickingTogglesState(true);
        button->onClick = [this, buttonPtr = button.get()] {
            for (const auto& trace : traces) {
                if (buttonPtr->getComponentID().getIntValue() == trace.snapshot.slotIndex) {
                    traceVisibility[static_cast<std::size_t>(trace.snapshot.slotIndex)] = buttonPtr->getToggleState();
                    repaint();
                    break;
                }
            }
        };
        addAndMakeVisible(*button);
        traceButtons.push_back(std::move(button));
        juce::ignoreUnused(slot);
    }

    for (auto& button : traceButtons)
        button->setVisible(false);

    for (int index = 0; index < neededButtons; ++index) {
        auto& button = traceButtons[static_cast<std::size_t>(index)];
        const auto& trace = traces[static_cast<std::size_t>(index)];
        const auto label = labelFromChars(trace.snapshot.productName);
        button->setButtonText(label);
        button->setComponentID(juce::String(trace.snapshot.slotIndex));
        button->setToggleState(traceVisibility[static_cast<std::size_t>(trace.snapshot.slotIndex)], juce::dontSendNotification);
        button->setColour(juce::ToggleButton::textColourId, colourFromRgb(trace.snapshot.accentRgb));
        button->setVisible(true);
    }

    resized();
}

void VXSpectrumEditor::computeSpectrum(
    const std::array<float, vxsuite::spectrum::kWaveformSamples>& waveform,
    const double sampleRate,
    std::array<float, vxsuite::spectrum::kWaveformSamples / 2 + 1>& spectrumOut) {
    fftData.fill(0.0f);
    for (int i = 0; i < vxsuite::spectrum::kWaveformSamples; ++i)
        fftData[static_cast<std::size_t>(i)] = waveform[static_cast<std::size_t>(i)] * window[static_cast<std::size_t>(i)];

    fft.performForward(fftData.data());

    const float fftScale = 2.0f / static_cast<float>(vxsuite::spectrum::kWaveformSamples);
    spectrumOut.fill(kMinDb);
    spectrumOut[0] = juce::Decibels::gainToDecibels(std::abs(fftData[0]) * fftScale + 1.0e-6f, kMinDb);
    for (int bin = 1; bin < vxsuite::spectrum::kWaveformSamples / 2; ++bin) {
        const float re = fftData[static_cast<std::size_t>(bin * 2)];
        const float im = fftData[static_cast<std::size_t>(bin * 2 + 1)];
        const float magnitude = std::sqrt(re * re + im * im) * fftScale;
        spectrumOut[static_cast<std::size_t>(bin)] = juce::Decibels::gainToDecibels(magnitude + 1.0e-6f, kMinDb);
    }
    spectrumOut.back() = juce::Decibels::gainToDecibels(std::abs(fftData[1]) * fftScale + 1.0e-6f, kMinDb);

    juce::ignoreUnused(sampleRate);
}

void VXSpectrumEditor::smoothSpectrum(
    std::array<float, vxsuite::spectrum::kWaveformSamples / 2 + 1>& current,
    const std::array<float, vxsuite::spectrum::kWaveformSamples / 2 + 1>& target,
    bool& hasState) noexcept {
    if (!hasState) {
        current = target;
        hasState = true;
        return;
    }

    for (std::size_t index = 0; index < current.size(); ++index) {
        const float source = current[index];
        const float destination = target[index];
        const float alpha = destination > source ? kSpectrumAttack : kSpectrumRelease;
        current[index] = source + (destination - source) * alpha;
    }
}

juce::Path VXSpectrumEditor::makeSpectrumPath(
    const std::array<float, vxsuite::spectrum::kWaveformSamples / 2 + 1>& spectrum,
    const double sampleRate,
    const juce::Rectangle<float> bounds) const {
    juce::Path path;
    bool started = false;
    for (int bin = 1; bin < static_cast<int>(spectrum.size()); ++bin) {
        const float frequency = static_cast<float>(sampleRate) * static_cast<float>(bin)
            / static_cast<float>(vxsuite::spectrum::kWaveformSamples);
        if (frequency < 20.0f)
            continue;

        const float x = spectrumX(frequency, sampleRate, bounds);
        const float y = spectrumY(spectrum[static_cast<std::size_t>(bin)], bounds);
        if (!started) {
            path.startNewSubPath(x, y);
            started = true;
        } else {
            path.lineTo(x, y);
        }
    }
    return path;
}

juce::Path VXSpectrumEditor::makeFilledSpectrumPath(
    const std::array<float, vxsuite::spectrum::kWaveformSamples / 2 + 1>& spectrum,
    const double sampleRate,
    const juce::Rectangle<float> bounds) const {
    auto path = makeSpectrumPath(spectrum, sampleRate, bounds);
    if (!path.isEmpty()) {
        path.lineTo(bounds.getRight(), bounds.getBottom());
        path.lineTo(bounds.getX(), bounds.getBottom());
        path.closeSubPath();
    }
    return path;
}

float VXSpectrumEditor::spectrumY(const float dbValue, const juce::Rectangle<float> bounds) const noexcept {
    const float norm = juce::jlimit(0.0f, 1.0f, (dbValue - kMinDb) / (kMaxDb - kMinDb));
    return bounds.getBottom() - norm * bounds.getHeight();
}

float VXSpectrumEditor::spectrumX(const float frequency,
                                  const double sampleRate,
                                  const juce::Rectangle<float> bounds) const noexcept {
    const double nyquist = std::max(40.0, sampleRate * 0.5);
    const double clamped = juce::jlimit(20.0, std::min(20000.0, nyquist), static_cast<double>(frequency));
    const double maxFreq = std::max(200.0, std::min(20000.0, nyquist));
    const double norm = (std::log10(clamped) - std::log10(20.0)) / (std::log10(maxFreq) - std::log10(20.0));
    return bounds.getX() + static_cast<float>(norm) * bounds.getWidth();
}

juce::Colour VXSpectrumEditor::colourFromRgb(const std::array<float, 3>& rgb, const float alpha) const noexcept {
    return juce::Colour::fromFloatRGBA(rgb[0], rgb[1], rgb[2], alpha);
}
