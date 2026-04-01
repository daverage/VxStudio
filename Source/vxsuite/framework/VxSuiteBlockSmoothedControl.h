#pragma once

#include "VxSuiteBlockSmoothing.h"

namespace vxsuite {

/**
 * @brief Encapsulates block-based smoothing state and logic for one or two parameters.
 *
 * Eliminates the need for products to manage smoothed parameter state, primer flags,
 * and block smoothing calls individually. Uses exponential block-smoothing with
 * a primer flag to snap to the first target without ramping.
 */
class BlockSmoothedControlPair {
public:
    /** Return value from process() */
    struct Values {
        float primary = 0.5f;
        float secondary = 0.5f;
    };

    /**
     * Reset state and priming flag.
     * @param primaryDefault Starting value for primary control (typically 0.5f for neutral)
     * @param secondaryDefault Starting value for secondary control (typically 0.5f for neutral)
     */
    void reset(float primaryDefault = 0.5f, float secondaryDefault = 0.5f) noexcept {
        primary = primaryDefault;
        secondary = secondaryDefault;
        primed = false;
    }

    /**
     * Update smoothed values toward targets.
     *
     * On first call (before priming): snaps both values to targets without ramping.
     * On subsequent calls: exponential block smoothing toward targets.
     *
     * @param primaryTarget Target value for primary control
     * @param secondaryTarget Target value for secondary control
     * @param sampleRate Current sample rate (Hz)
     * @param numSamples Number of samples in the block
     * @param primaryTimeSeconds Smoothing time constant for primary (seconds, typically 0.050-0.070)
     * @param secondaryTimeSeconds Smoothing time constant for secondary (typically 0.060-0.080)
     * @return Struct containing current smoothed { primary, secondary } values
     */
    Values process(float primaryTarget, float secondaryTarget,
                   double sampleRate, int numSamples,
                   float primaryTimeSeconds, float secondaryTimeSeconds) noexcept {
        if (!primed) {
            primary = primaryTarget;
            secondary = secondaryTarget;
            primed = true;
        } else {
            primary = smoothBlockValue(primary, primaryTarget,
                                      sampleRate, numSamples, primaryTimeSeconds);
            secondary = smoothBlockValue(secondary, secondaryTarget,
                                        sampleRate, numSamples, secondaryTimeSeconds);
        }
        return { primary, secondary };
    }

    /** Get current primary value without updating. */
    float getPrimary() const noexcept { return primary; }

    /** Get current secondary value without updating. */
    float getSecondary() const noexcept { return secondary; }

private:
    float primary = 0.5f;
    float secondary = 0.5f;
    bool primed = false;
};

/**
 * @brief Simplified version for products with only one smoothed parameter.
 *
 * Returns just the smoothed float value instead of a struct.
 */
class BlockSmoothedControl {
public:
    void reset(float defaultValue = 0.5f) noexcept {
        value = defaultValue;
        primed = false;
    }

    float process(float target, double sampleRate, int numSamples,
                  float timeSeconds) noexcept {
        if (!primed) {
            value = target;
            primed = true;
        } else {
            value = smoothBlockValue(value, target, sampleRate, numSamples, timeSeconds);
        }
        return value;
    }

    float get() const noexcept { return value; }

private:
    float value = 0.5f;
    bool primed = false;
};

/**
 * @brief Three-parameter smoothing for products with three main controls (rare).
 */
class BlockSmoothedControlTriple {
public:
    struct Values {
        float primary = 0.5f;
        float secondary = 0.5f;
        float tertiary = 0.5f;
    };

    void reset(float primaryDefault = 0.5f, float secondaryDefault = 0.5f,
               float tertiaryDefault = 0.5f) noexcept {
        primary = primaryDefault;
        secondary = secondaryDefault;
        tertiary = tertiaryDefault;
        primed = false;
    }

    Values process(float primaryTarget, float secondaryTarget, float tertiaryTarget,
                   double sampleRate, int numSamples,
                   float primaryTimeSeconds, float secondaryTimeSeconds,
                   float tertiaryTimeSeconds) noexcept {
        if (!primed) {
            primary = primaryTarget;
            secondary = secondaryTarget;
            tertiary = tertiaryTarget;
            primed = true;
        } else {
            primary = smoothBlockValue(primary, primaryTarget,
                                      sampleRate, numSamples, primaryTimeSeconds);
            secondary = smoothBlockValue(secondary, secondaryTarget,
                                        sampleRate, numSamples, secondaryTimeSeconds);
            tertiary = smoothBlockValue(tertiary, tertiaryTarget,
                                       sampleRate, numSamples, tertiaryTimeSeconds);
        }
        return { primary, secondary, tertiary };
    }

    float getPrimary() const noexcept { return primary; }
    float getSecondary() const noexcept { return secondary; }
    float getTertiary() const noexcept { return tertiary; }

private:
    float primary = 0.5f;
    float secondary = 0.5f;
    float tertiary = 0.5f;
    bool primed = false;
};

} // namespace vxsuite
