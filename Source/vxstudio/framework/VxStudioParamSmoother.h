#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

namespace vxsuite {

/**
 * Sample-accurate scalar parameter ramp.
 *
 * Wraps juce::SmoothedValue to provide a consistent interface for smoothing
 * DSP parameters that feed filter coefficient calculations. Unlike
 * BlockSmoothedControl (block-rate), this advances sample-by-sample inside
 * a DSP loop so coefficient jumps cannot occur mid-block.
 *
 * Typical use:
 *   - In prepare(): call prepare(sampleRate, rampSeconds)
 *   - At start of processBlock(): call setTarget(newValue)
 *   - Inside sample loop: float v = smoother.getNext()
 */
class ParamSmoother {
public:
    /**
     * Must be called before streaming.
     * @param sampleRate  Host sample rate.
     * @param rampSeconds Time to ramp from current to target (seconds).
     *                    Typical: 0.020–0.050 s for filter params.
     */
    void prepare(double sampleRate, float rampSeconds) noexcept {
        const int rampSamples = std::max(1,
            static_cast<int>(static_cast<double>(rampSeconds) * sampleRate));
        smoother.reset(static_cast<int>(rampSamples));
        primed = false;
    }

    /**
     * Snap to value instantly with no ramp. Use on first prepare / reset so
     * the plugin doesn't ramp from 0 on load.
     */
    void snapTo(float value) noexcept {
        smoother.setCurrentAndTargetValue(value);
        primed = true;
    }

    /**
     * Arm a ramp toward value. If this is the first call (not yet primed),
     * snaps immediately instead of ramping.
     */
    void setTarget(float value) noexcept {
        if (!primed) {
            snapTo(value);
        } else {
            smoother.setTargetValue(value);
        }
    }

    /** Advance one sample and return current smoothed value. */
    float getNext() noexcept {
        return smoother.getNextValue();
    }

    /** Current smoothed value without advancing. */
    float getCurrentValue() const noexcept {
        return smoother.getCurrentValue();
    }

    /** True while the ramp has not reached its target. */
    bool isSmoothing() const noexcept {
        return smoother.isSmoothing();
    }

    void reset() noexcept {
        primed = false;
        smoother.reset(smoother.getTargetValue());
    }

private:
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoother;
    bool primed = false;
};

} // namespace vxsuite
