#include "VxRebalanceFeatureBuffer.h"

#include <algorithm>
#include <cmath>

namespace vxsuite::rebalance::ml {

void FeatureBuffer::prepare(const double sampleRate, const int maxBlockSize) noexcept {
    sampleRateHz = sampleRate > 1000.0 ? sampleRate : 48000.0;
    blockSize = std::max(1, maxBlockSize);
}

void FeatureBuffer::reset() noexcept {
    blockSize = std::max(1, blockSize);
}

FeatureSnapshot FeatureBuffer::analyseBlock(const juce::AudioBuffer<float>& buffer, const int requestedSamples) noexcept {
    FeatureSnapshot snapshot {};
    const int numChannels = std::max(1, buffer.getNumChannels());
    const int numSamples = std::min(buffer.getNumSamples(), std::max(0, requestedSamples));
    if (numSamples <= 0)
        return snapshot;

    double peak = 0.0;
    double energy = 0.0;
    double midAbs = 0.0;
    double sideAbs = 0.0;

    for (int i = 0; i < numSamples; ++i) {
        const float left = buffer.getSample(0, i);
        const float right = numChannels > 1 ? buffer.getSample(1, i) : left;
        const float mono = 0.5f * (left + right);
        const float side = 0.5f * (left - right);
        peak = std::max(peak, static_cast<double>(std::abs(mono)));
        energy += static_cast<double>(mono) * static_cast<double>(mono);
        midAbs += std::abs(mono);
        sideAbs += std::abs(side);
    }

    snapshot.rms = static_cast<float>(std::sqrt(energy / static_cast<double>(numSamples)));
    snapshot.crest = snapshot.rms > 1.0e-6f ? static_cast<float>(peak) / snapshot.rms : 1.0f;
    snapshot.monoScore = juce::jlimit(0.0f, 1.0f,
                                      1.0f - static_cast<float>(sideAbs / std::max(1.0, midAbs * 0.08)));

    const int bands = static_cast<int>(snapshot.bandEnergy.size());
    const int chunk = std::max(1, numSamples / bands);
    for (int band = 0; band < bands; ++band) {
        const int start = band * chunk;
        const int end = band == bands - 1 ? numSamples : std::min(numSamples, start + chunk);
        double bandAccum = 0.0;
        for (int i = start; i < end; ++i) {
            const float left = buffer.getSample(0, i);
            const float right = numChannels > 1 ? buffer.getSample(1, i) : left;
            const float mono = 0.5f * (left + right);
            bandAccum += std::abs(mono);
        }
        snapshot.bandEnergy[static_cast<size_t>(band)] = static_cast<float>(bandAccum / std::max(1, end - start));
    }

    return snapshot;
}

} // namespace vxsuite::rebalance::ml
