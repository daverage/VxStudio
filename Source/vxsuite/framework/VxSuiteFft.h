#pragma once

#include <memory>

#include <juce_dsp/juce_dsp.h>

namespace vxsuite {

class RealFft {
public:
    void prepare(const int newOrder) {
        const int clampedOrder = juce::jmax(1, newOrder);
        if (fft != nullptr && orderValue == clampedOrder)
            return;

        fft = std::make_unique<juce::dsp::FFT>(clampedOrder);
        orderValue = clampedOrder;
    }

    void reset() noexcept {
        fft.reset();
        orderValue = -1;
    }

    [[nodiscard]] bool isReady() const noexcept { return fft != nullptr; }
    [[nodiscard]] int order() const noexcept { return orderValue; }
    [[nodiscard]] int size() const noexcept { return isReady() ? (1 << orderValue) : 0; }
    [[nodiscard]] int bins() const noexcept { return isReady() ? (size() / 2 + 1) : 0; }

    void performForward(float* data) const noexcept {
        jassert(fft != nullptr);
        if (fft != nullptr)
            fft->performRealOnlyForwardTransform(data);
    }

    void performFrequencyOnlyForward(float* data) const noexcept {
        jassert(fft != nullptr);
        if (fft != nullptr)
            fft->performFrequencyOnlyForwardTransform(data);
    }

    void performInverse(float* data) const noexcept {
        jassert(fft != nullptr);
        if (fft != nullptr)
            fft->performRealOnlyInverseTransform(data);
    }

private:
    std::unique_ptr<juce::dsp::FFT> fft;
    int orderValue = -1;
};

} // namespace vxsuite
