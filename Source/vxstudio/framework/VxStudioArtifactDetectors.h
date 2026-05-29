#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <algorithm>
#include <cmath>
#include <array>
#include <vector>

namespace vxsuite {
namespace detectors {

// ============================================================================
// Utility Functions
// ============================================================================

inline float clamp01(float value) noexcept {
    return std::max(0.0f, std::min(value, 1.0f));
}

inline float safeDiv(float numerator, float denominator, float defaultVal = 0.0f) noexcept {
    return std::abs(denominator) > 1.0e-9f ? numerator / denominator : defaultVal;
}

// ============================================================================
// One-Pole Low-Pass Filter (for envelope following)
// ============================================================================

class OnePoleLowpass {
public:
    void setCutoff(double sampleRate, float cutoffHz) noexcept {
        const float omega = 2.0f * juce::MathConstants<float>::pi * cutoffHz / static_cast<float>(sampleRate);
        coeff = std::clamp(omega / (1.0f + omega), 0.0f, 1.0f);
    }

    float process(float sample) noexcept {
        z = sample * coeff + z * (1.0f - coeff);
        return z;
    }

    void reset() noexcept { z = 0.0f; }

private:
    float coeff = 0.0f;
    float z = 0.0f;
};

// ============================================================================
// Fast Attack / Slow Release Envelope Follower
// ============================================================================

class EnvelopeFollower {
public:
    void setSampleRate(double sr) noexcept {
        sampleRate = sr;
    }

    void setTimings(float attackMs, float releaseMs) noexcept {
        const float attackSamples = attackMs * static_cast<float>(sampleRate) / 1000.0f;
        const float releaseSamples = releaseMs * static_cast<float>(sampleRate) / 1000.0f;
        attackCoeff = std::exp(-1.0f / std::max(attackSamples, 1.0f));
        releaseCoeff = std::exp(-1.0f / std::max(releaseSamples, 1.0f));
    }

    float process(float sample) noexcept {
        const float absVal = std::abs(sample);
        if (absVal > envelope) {
            envelope = attackCoeff * envelope + (1.0f - attackCoeff) * absVal;
        } else {
            envelope = releaseCoeff * envelope + (1.0f - releaseCoeff) * absVal;
        }
        return envelope;
    }

    void reset() noexcept { envelope = 0.0f; }

private:
    double sampleRate = 48000.0;
    float envelope = 0.0f;
    float attackCoeff = 0.9f;
    float releaseCoeff = 0.99f;
};

// ============================================================================
// Simple Biquad Filter (for band-pass)
// ============================================================================

class BiquadFilter {
public:
    enum class Type { HighPass, LowPass, BandPass };

    void setCoefficients(Type type, float centerHz, float qFactor, double sampleRate) noexcept {
        const float omega = 2.0f * juce::MathConstants<float>::pi * centerHz / static_cast<float>(sampleRate);
        const float sinOmega = std::sin(omega);
        const float cosOmega = std::cos(omega);
        const float alpha = sinOmega / (2.0f * qFactor);

        const float a0 = 1.0f + alpha;
        float b0 = 0.0f, b1 = 0.0f, b2 = 0.0f;

        switch (type) {
            case Type::HighPass:
                b0 = (1.0f + cosOmega) / 2.0f;
                b1 = -(1.0f + cosOmega);
                b2 = (1.0f + cosOmega) / 2.0f;
                break;
            case Type::LowPass:
                b0 = (1.0f - cosOmega) / 2.0f;
                b1 = 1.0f - cosOmega;
                b2 = (1.0f - cosOmega) / 2.0f;
                break;
            case Type::BandPass:
                b0 = alpha;
                b1 = 0.0f;
                b2 = -alpha;
                break;
        }

        this->b0 = b0 / a0;
        this->b1 = b1 / a0;
        this->b2 = b2 / a0;
        this->a1 = -2.0f * cosOmega / a0;
        this->a2 = (1.0f - alpha) / a0;
    }

    float process(float sample) noexcept {
        const float out = b0 * sample + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1;
        x1 = sample;
        y2 = y1;
        y1 = out;
        return out;
    }

    void reset() noexcept {
        x1 = x2 = y1 = y2 = 0.0f;
    }

private:
    float b0 = 0.0f, b1 = 0.0f, b2 = 0.0f;
    float a1 = 0.0f, a2 = 0.0f;
    float x1 = 0.0f, x2 = 0.0f;
    float y1 = 0.0f, y2 = 0.0f;
};

// ============================================================================
// Spectral Analysis Helpers
// ============================================================================

class SpectralAnalyzer {
public:
    void prepare(int fftSize) noexcept {
        this->fftSize = fftSize;
        bins = fftSize / 2 + 1;
        spectrum.resize(bins);
    }

    // Compute spectral flatness (0=peaked, 1=flat/noise-like)
    float computeSpectralFlatness(const std::vector<float>& magnitudes) const noexcept {
        if (magnitudes.empty()) return 0.5f;

        float logSum = 0.0f;
        float linearSum = 0.0f;
        for (const auto& mag : magnitudes) {
            const float safeMag = std::max(mag, 1.0e-12f);
            logSum += std::log(safeMag);
            linearSum += safeMag;
        }

        const float geometricMean = std::exp(logSum / static_cast<float>(magnitudes.size()));
        const float arithmeticMean = linearSum / static_cast<float>(magnitudes.size());
        return clamp01(safeDiv(geometricMean, arithmeticMean, 0.5f) * 2.0f);
    }

    // Measure harmonicity: how much energy is in harmonic series vs. noise
    float computeHarmonicity(const std::vector<float>& magnitudes, float fundamentalHz, float sampleRate) const noexcept {
        if (magnitudes.empty() || fundamentalHz < 20.0f) return 0.0f;

        const float binHz = static_cast<float>(sampleRate) / static_cast<float>(fftSize);
        const int fundamentalBin = static_cast<int>(fundamentalHz / binHz + 0.5f);

        if (fundamentalBin <= 0 || fundamentalBin >= static_cast<int>(magnitudes.size())) return 0.0f;

        float harmonicEnergy = 0.0f;
        float totalEnergy = 1.0e-12f;

        for (int h = 1; h <= 10; ++h) {
            const int bin = fundamentalBin * h;
            if (bin < static_cast<int>(magnitudes.size())) {
                harmonicEnergy += magnitudes[bin];
            }
            totalEnergy += magnitudes[bin];
        }

        return clamp01(safeDiv(harmonicEnergy, totalEnergy, 0.0f));
    }

    // Measure energy in a frequency band
    float measureBandEnergy(const std::vector<float>& magnitudes, float lowHz, float highHz, float sampleRate) const noexcept {
        if (magnitudes.empty()) return 0.0f;

        const float binHz = static_cast<float>(sampleRate) / static_cast<float>(fftSize);
        const int lowBin = static_cast<int>(lowHz / binHz + 0.5f);
        const int highBin = static_cast<int>(highHz / binHz + 0.5f);

        float energy = 0.0f;
        for (int b = std::max(0, lowBin); b <= std::min(static_cast<int>(magnitudes.size()) - 1, highBin); ++b) {
            energy += magnitudes[b] * magnitudes[b];
        }

        return std::sqrt(energy / static_cast<float>(std::max(1, highBin - lowBin + 1)));
    }

    // Find the most prominent peak in a frequency band
    float findPeakFrequency(const std::vector<float>& magnitudes, float lowHz, float highHz, float sampleRate) const noexcept {
        if (magnitudes.empty()) return lowHz;

        const float binHz = static_cast<float>(sampleRate) / static_cast<float>(fftSize);
        const int lowBin = static_cast<int>(lowHz / binHz + 0.5f);
        const int highBin = static_cast<int>(highHz / binHz + 0.5f);

        int peakBin = std::max(0, lowBin);
        float peakMag = magnitudes[peakBin];

        for (int b = lowBin; b <= std::min(static_cast<int>(magnitudes.size()) - 1, highBin); ++b) {
            if (magnitudes[b] > peakMag) {
                peakMag = magnitudes[b];
                peakBin = b;
            }
        }

        return static_cast<float>(peakBin) * binHz;
    }

private:
    int fftSize = 2048;
    int bins = 1025;
    std::vector<float> spectrum;
};

// ============================================================================
// Onset Detector (for plosive bursts)
// ============================================================================

class OnsetDetector {
public:
    void setSampleRate(double sr) noexcept {
        sampleRate = sr;
    }

    // Detect fast rise in signal (plosive/attack characteristic)
    float detectOnsetSlope(float currentSample, float& previousSample, float& previousEnvelope) noexcept {
        const float absVal = std::abs(currentSample);
        const float envelope = 0.9f * previousEnvelope + 0.1f * absVal;
        const float slope = envelope - previousEnvelope;

        previousSample = currentSample;
        previousEnvelope = envelope;

        // Return slope as normalized value (0-1)
        return clamp01(slope / std::max(envelope, 0.01f));
    }

    // Measure burst characteristics: peak followed by decay
    float measureBurstDecay(const float* data, int numSamples) noexcept {
        if (numSamples < 2) return 0.0f;

        // Find peak in first quarter
        float peakVal = 0.0f;
        int peakIdx = 0;
        const int searchEnd = std::min(numSamples / 4, numSamples);

        for (int i = 0; i < searchEnd; ++i) {
            const float val = std::abs(data[i]);
            if (val > peakVal) {
                peakVal = val;
                peakIdx = i;
            }
        }

        if (peakVal < 1.0e-6f) return 0.0f;

        // Measure decay slope after peak
        const int decayStart = peakIdx + 1;
        const int decayEnd = std::min(peakIdx + numSamples / 8, numSamples);

        if (decayStart >= decayEnd) return 0.0f;

        float decayEnergy = 0.0f;
        for (int i = decayStart; i < decayEnd; ++i) {
            decayEnergy += std::abs(data[i]);
        }

        const float decayAvg = decayEnergy / static_cast<float>(decayEnd - decayStart);
        return clamp01((peakVal - decayAvg) / peakVal);
    }

private:
    double sampleRate = 48000.0;
};

// ============================================================================
// RMS Energy Measurement
// ============================================================================

inline float computeRMS(const float* data, int numSamples) noexcept {
    if (numSamples <= 0) return 0.0f;
    double sumSq = 0.0;
    for (int i = 0; i < numSamples; ++i) {
        sumSq += static_cast<double>(data[i]) * data[i];
    }
    return static_cast<float>(std::sqrt(sumSq / static_cast<double>(numSamples)));
}

inline float computePeak(const float* data, int numSamples) noexcept {
    if (numSamples <= 0) return 0.0f;
    float peak = 0.0f;
    for (int i = 0; i < numSamples; ++i) {
        peak = std::max(peak, std::abs(data[i]));
    }
    return peak;
}

// ============================================================================
// Statistical Helpers
// ============================================================================

inline float computePercentile(const std::vector<float>& values, float percentile) noexcept {
    if (values.empty()) return 0.0f;
    auto sorted = values;
    std::sort(sorted.begin(), sorted.end());
    const size_t idx = static_cast<size_t>(percentile * sorted.size());
    return sorted[std::min(idx, sorted.size() - 1)];
}

inline float computeMedian(const std::vector<float>& values) noexcept {
    return computePercentile(values, 0.5f);
}

} // namespace detectors
} // namespace vxsuite
