#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <functional>
#include <memory>

namespace vxsuite {

/**
 * Thin wrapper around juce::dsp::Oversampling for use in VX Suite processors.
 *
 * Upsample → call user DSP lambda → downsample.  Factor 1 is a true no-op
 * (zero added latency, zero CPU overhead) so the same code path works for
 * all quality modes.
 *
 * Usage:
 *
 *   // In prepare():
 *   wrapper.prepare(sampleRate, maxBlockSize, numChannels, factor, mode);
 *
 *   // In processBlock():
 *   wrapper.process(buffer, [&](juce::AudioBuffer<float>& up) {
 *       limiterDsp.process(up);
 *   });
 *
 *   // Report extra latency to host:
 *   setReportedLatencySamples(baseLat + wrapper.addedLatencySamples());
 */
class OversamplingWrapper {
public:
    enum class Mode {
        minPhase,    // lower latency, phase shift — for dynamics / tracking
        linearPhase  // zero phase, pre-ringing — for mastering / export
    };

    /**
     * Prepare for streaming.
     *
     * @param sampleRate    Host sample rate.
     * @param maxBlockSize  Maximum host block size.
     * @param numChannels   Channel count.
     * @param factor        Oversampling factor: 1 (off), 2, 4, 8.
     *                      Values other than powers-of-two are rounded up.
     * @param mode          Filter type (default minPhase).
     */
    void prepare(double sampleRate,
                 int    maxBlockSize,
                 int    numChannels,
                 int    factor = 1,
                 Mode   mode   = Mode::minPhase) {
        factor_ = std::max(1, factor);

        if (factor_ == 1) {
            oversampler.reset();
            return;
        }

        const auto filterType = (mode == Mode::linearPhase)
            ? juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple
            : juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR;

        const int order = orderForFactor(factor_);

        oversampler = std::make_unique<juce::dsp::Oversampling<float>>(
            numChannels,
            order,
            filterType,
            /*isMaxQuality=*/true);

        juce::dsp::ProcessSpec spec;
        spec.sampleRate       = sampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32>(maxBlockSize);
        spec.numChannels      = static_cast<juce::uint32>(numChannels);
        oversampler->initProcessing(static_cast<size_t>(maxBlockSize));
        (void)spec;
    }

    void reset() noexcept {
        if (oversampler)
            oversampler->reset();
    }

    /**
     * Latency added by the oversampling filters (in host-rate samples).
     * 0 when factor == 1.
     */
    int addedLatencySamples() const noexcept {
        if (!oversampler || factor_ == 1)
            return 0;
        return static_cast<int>(oversampler->getLatencyInSamples());
    }

    int getFactor() const noexcept { return factor_; }

    /**
     * Process buffer with oversampling.
     *
     * @param buffer  Audio buffer, modified in-place.
     * @param fn      DSP callable: void(juce::AudioBuffer<float>&).
     *                Receives the upsampled buffer, must process it in-place.
     */
    template <typename Fn>
    void process(juce::AudioBuffer<float>& buffer, Fn&& fn) {
        if (factor_ == 1 || !oversampler) {
            fn(buffer);
            return;
        }

        juce::dsp::AudioBlock<float> block(buffer);
        auto upBlock = oversampler->processSamplesUp(block);

        // Build a temporary AudioBuffer view over the upsampled block
        const int upChannels = static_cast<int>(upBlock.getNumChannels());
        const int upSamples  = static_cast<int>(upBlock.getNumSamples());
        juce::AudioBuffer<float> upBuffer(upChannels, upSamples);
        for (int ch = 0; ch < upChannels; ++ch)
            upBuffer.copyFrom(ch, 0, upBlock.getChannelPointer(ch), upSamples);

        fn(upBuffer);

        for (int ch = 0; ch < upChannels; ++ch)
            upBlock.getSingleChannelBlock(static_cast<size_t>(ch))
                   .copyFrom(juce::dsp::AudioBlock<float>(upBuffer)
                                 .getSingleChannelBlock(static_cast<size_t>(ch)));

        oversampler->processSamplesDown(block);
    }

private:
    static int orderForFactor(int factor) noexcept {
        int order = 0;
        int f = factor;
        while (f > 1) { f >>= 1; ++order; }
        return std::max(1, order);
    }

    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    int factor_ = 1;
};

} // namespace vxsuite
