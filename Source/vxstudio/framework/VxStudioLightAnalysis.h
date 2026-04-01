#pragma once

#include <cmath>

#include <juce_audio_basics/juce_audio_basics.h>

namespace vxsuite {

/**
 * @brief Lightweight, zero-allocation audio analysis primitives.
 *
 * These helpers are realtime-safe and designed for per-block analysis.
 * They compute across all channels or a single channel without allocating memory.
 */
namespace analysis {

/**
 * @brief Compute RMS (root mean square) across all channels of a buffer.
 * @param buffer Audio buffer to analyze
 * @return RMS value, or 0.0f if buffer is empty
 */
inline float rms(const juce::AudioBuffer<float>& buffer) noexcept {
    const int channels = buffer.getNumChannels();
    const int samples = buffer.getNumSamples();
    if (channels <= 0 || samples <= 0)
        return 0.0f;

    double sumSquares = 0.0;
    int count = 0;
    for (int ch = 0; ch < channels; ++ch) {
        const auto* data = buffer.getReadPointer(ch);
        for (int i = 0; i < samples; ++i) {
            const double sample = data[i];
            sumSquares += sample * sample;
        }
        count += samples;
    }

    return count > 0 ? static_cast<float>(std::sqrt(sumSquares / static_cast<double>(count))) : 0.0f;
}

/**
 * @brief Compute RMS for a single channel.
 * @param buffer Audio buffer to analyze
 * @param channel Channel index (must be valid)
 * @return RMS value for that channel, or 0.0f if invalid
 */
inline float rmsChannel(const juce::AudioBuffer<float>& buffer, int channel) noexcept {
    if (channel < 0 || channel >= buffer.getNumChannels() || buffer.getNumSamples() <= 0)
        return 0.0f;

    double sumSquares = 0.0;
    const auto* data = buffer.getReadPointer(channel);
    for (int i = 0; i < buffer.getNumSamples(); ++i) {
        const double sample = data[i];
        sumSquares += sample * sample;
    }

    return static_cast<float>(std::sqrt(sumSquares / static_cast<double>(buffer.getNumSamples())));
}

/**
 * @brief Compute peak (maximum absolute sample value) across all channels.
 * @param buffer Audio buffer to analyze
 * @return Peak value, or 0.0f if buffer is empty
 */
inline float peak(const juce::AudioBuffer<float>& buffer) noexcept {
    const int channels = buffer.getNumChannels();
    const int samples = buffer.getNumSamples();
    if (channels <= 0 || samples <= 0)
        return 0.0f;

    float maxPeak = 0.0f;
    for (int ch = 0; ch < channels; ++ch)
        maxPeak = juce::jmax(maxPeak, buffer.getMagnitude(ch, 0, samples));
    return maxPeak;
}

/**
 * @brief Compute peak for a single channel.
 * @param buffer Audio buffer to analyze
 * @param channel Channel index (must be valid)
 * @return Peak value for that channel, or 0.0f if invalid
 */
inline float peakChannel(const juce::AudioBuffer<float>& buffer, int channel) noexcept {
    if (channel < 0 || channel >= buffer.getNumChannels() || buffer.getNumSamples() <= 0)
        return 0.0f;
    return buffer.getMagnitude(channel, 0, buffer.getNumSamples());
}

} // namespace analysis

} // namespace vxsuite
