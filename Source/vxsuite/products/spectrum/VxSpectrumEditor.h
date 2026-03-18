#pragma once

#include "../../framework/VxSuiteFft.h"
#include "../../framework/VxSuiteSpectrumTelemetry.h"
#include "VxSpectrumProcessor.h"

#include <array>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

class VXSpectrumEditor final : public juce::AudioProcessorEditor,
                               private juce::Timer {
public:
    explicit VXSpectrumEditor(VXSpectrumAudioProcessor&);
    ~VXSpectrumEditor() override = default;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    struct TraceRenderData {
        vxsuite::spectrum::SnapshotView snapshot;
        std::array<float, vxsuite::spectrum::kWaveformSamples / 2 + 1> wetSpectrum {};
    };

    struct SnapshotCacheEntry {
        bool valid = false;
        double lastActiveMs = 0.0;
        vxsuite::spectrum::SnapshotView snapshot;
    };

    void timerCallback() override;
    void refreshTelemetry();
    void refreshToggleButtons();
    [[nodiscard]] bool snapshotHasSignal(const vxsuite::spectrum::SnapshotView& snapshot) const noexcept;
    [[nodiscard]] bool collectSnapshotForSlot(int slotIndex,
                                              double nowMs,
                                              vxsuite::spectrum::SnapshotView& snapshotOut);
    [[nodiscard]] float correlation(const std::array<float, vxsuite::spectrum::kWaveformSamples>& a,
                                    const std::array<float, vxsuite::spectrum::kWaveformSamples>& b) const noexcept;
    [[nodiscard]] float bestAlignedCorrelation(const std::array<float, vxsuite::spectrum::kWaveformSamples>& a,
                                               const std::array<float, vxsuite::spectrum::kWaveformSamples>& b,
                                               int maxLag) const noexcept;
    [[nodiscard]] float bestAlignedEnvelopeCorrelation(
        const std::array<float, vxsuite::spectrum::kWaveformSamples>& a,
        const std::array<float, vxsuite::spectrum::kWaveformSamples>& b,
        int maxLag) const noexcept;
    [[nodiscard]] float stageMatchScore(const vxsuite::spectrum::SnapshotView& candidate,
                                        const std::array<float, vxsuite::spectrum::kWaveformSamples>& targetWaveform,
                                        int currentMaxOrder,
                                        float& wetCorrelationOut,
                                        float& envelopeCorrelationOut) const noexcept;
    void computeSpectrum(const std::array<float, vxsuite::spectrum::kWaveformSamples>& waveform,
                         double sampleRate,
                         std::array<float, vxsuite::spectrum::kWaveformSamples / 2 + 1>& spectrumOut);
    void smoothSpectrum(std::array<float, vxsuite::spectrum::kWaveformSamples / 2 + 1>& current,
                        const std::array<float, vxsuite::spectrum::kWaveformSamples / 2 + 1>& target,
                        bool& hasState) noexcept;
    juce::Path makeSpectrumPath(const std::array<float, vxsuite::spectrum::kWaveformSamples / 2 + 1>& spectrum,
                                double sampleRate,
                                juce::Rectangle<float> bounds) const;
    juce::Path makeFilledSpectrumPath(const std::array<float, vxsuite::spectrum::kWaveformSamples / 2 + 1>& spectrum,
                                      double sampleRate,
                                      juce::Rectangle<float> bounds) const;
    float spectrumY(float dbValue, juce::Rectangle<float> bounds) const noexcept;
    float spectrumX(float frequency, double sampleRate, juce::Rectangle<float> bounds) const noexcept;
    juce::Colour colourFromRgb(const std::array<float, 3>& rgb, float alpha = 1.0f) const noexcept;

    VXSpectrumAudioProcessor& processor;
    vxsuite::RealFft fft;
    std::array<float, vxsuite::spectrum::kWaveformSamples> window {};
    std::array<float, vxsuite::spectrum::kWaveformSamples * 2> fftData {};
    std::array<float, vxsuite::spectrum::kWaveformSamples / 2 + 1> drySpectrum {};
    std::array<float, vxsuite::spectrum::kWaveformSamples / 2 + 1> finalWetSpectrum {};
    double drySampleRate = 48000.0;
    double wetSampleRate = 48000.0;
    bool hasDrySpectrum = false;
    bool hasWetSpectrum = false;
    bool showDryLayer = true;
    bool showFinalWetLayer = true;
    std::vector<TraceRenderData> traces;
    std::array<SnapshotCacheEntry, vxsuite::spectrum::kMaxTelemetrySlots> snapshotCache {};
    std::array<std::array<float, vxsuite::spectrum::kWaveformSamples / 2 + 1>,
               vxsuite::spectrum::kMaxTelemetrySlots> slotSpectrumState {};
    std::array<bool, vxsuite::spectrum::kMaxTelemetrySlots> slotSpectrumStateValid {};
    bool drySpectrumStateValid = false;
    bool finalWetSpectrumStateValid = false;
    std::array<bool, vxsuite::spectrum::kMaxTelemetrySlots> traceVisibility {};
    juce::ToggleButton dryButton;
    juce::ToggleButton finalWetButton;
    std::vector<std::unique_ptr<juce::ToggleButton>> traceButtons;
    juce::Label titleLabel;
    juce::Label subtitleLabel;
    juce::Label diagnosticsLabel;
    juce::Label footerLabel;
    juce::Rectangle<int> plotBounds;
    juce::Rectangle<int> legendBounds;
};
