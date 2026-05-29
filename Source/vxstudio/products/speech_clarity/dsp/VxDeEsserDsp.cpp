#include "VxDeEsserDsp.h"
#include <algorithm>
#include <cmath>

namespace vxsuite {
namespace speech_clarity {

void DeEsserDsp::prepare(double sampleRate, int maxBlockSize, int numChannels) {
    this->sampleRate = sampleRate > 1000.0 ? sampleRate : 48000.0;
    channels.clear();
    channels.resize(numChannels);

    // Design band-pass filter centered at 5.5 kHz (sibilance region center)
    // Q=2 gives a reasonably narrow band (roughly 4.5-8 kHz for vocal range)
    for (auto& ch : channels) {
        designBandPassCoefficients(this->sampleRate, 5500.0f, 2.0f, ch.bandPassFilter);
    }

    reset();
}

void DeEsserDsp::reset() noexcept {
    for (auto& ch : channels) {
        ch.bandPassFilter.x1 = ch.bandPassFilter.x2 = 0.0f;
        ch.bandPassFilter.y1 = ch.bandPassFilter.y2 = 0.0f;
        ch.envelopeDb = 0.0f;
    }
    lastReductionDb = 0.0f;
}

void DeEsserDsp::designBandPassCoefficients(
    double sampleRate,
    float centerHz,
    float qFactor,
    BiquadState& state) {
    // Design normalized biquad band-pass coefficients
    const float omega = 2.0f * 3.14159265359f * centerHz / static_cast<float>(sampleRate);
    const float sinOmega = std::sin(omega);
    const float cosOmega = std::cos(omega);
    const float alpha = sinOmega / (2.0f * qFactor);

    const float a0 = 1.0f + alpha;
    const float b0 = alpha;
    const float b1 = 0.0f;
    const float b2 = -alpha;
    const float a1 = -2.0f * cosOmega;
    const float a2 = 1.0f - alpha;

    // Normalize
    state.b0 = b0 / a0;
    state.b1 = b1 / a0;
    state.b2 = b2 / a0;
    state.a1 = a1 / a0;
    state.a2 = a2 / a0;
}

float DeEsserDsp::processBiquad(float sample, BiquadState& state) noexcept {
    const float out = state.b0 * sample + state.b1 * state.x1 + state.b2 * state.x2
                    - state.a1 * state.y1 - state.a2 * state.y2;
    state.x2 = state.x1;
    state.x1 = sample;
    state.y2 = state.y1;
    state.y1 = out;
    return out;
}

float DeEsserDsp::applySoftCompression(
    float inputDb,
    float thresholdDb,
    float ratio,
    float kneeDb) noexcept {
    // Soft-knee compressor for smooth, natural reduction
    // Returns gain reduction in dB (0 = no reduction, negative = reduce)

    if (inputDb < thresholdDb - kneeDb / 2.0f) {
        return 0.0f;  // Below knee: no reduction
    }

    if (inputDb > thresholdDb + kneeDb / 2.0f) {
        // Above knee: full compression ratio
        const float overDb = inputDb - thresholdDb;
        return -overDb * (1.0f - 1.0f / ratio);
    }

    // Inside knee: smooth interpolation
    const float kneeCenter = thresholdDb;
    const float kneeHalf = kneeDb / 2.0f;
    const float normalizedPos = (inputDb - (kneeCenter - kneeHalf)) / kneeDb;
    const float clampedPos = std::max(0.0f, std::min(1.0f, normalizedPos));

    // Quadratic ease for smooth knee
    const float easePos = clampedPos * clampedPos;
    const float overDb = (inputDb - kneeCenter) * easePos;
    return -overDb * (1.0f - 1.0f / ratio);
}

void DeEsserDsp::process(juce::AudioBuffer<float>& buffer, const Params& params) {
    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    if (numSamples <= 0 || numChannels <= 0)
        return;

    // Early exit if not active
    if (params.strength < 0.001f || params.detectionIntensity < 0.001f)
        return;

    // Single pass: measure and process simultaneously to avoid filter state corruption
    float maxSibilanceEnergy = 0.0f;
    std::vector<std::vector<float>> sibBands(numChannels, std::vector<float>(numSamples));

    // 1. MEASURE & EXTRACT sibilance band in single pass
    for (int ch = 0; ch < numChannels; ++ch) {
        const float* inputData = buffer.getReadPointer(ch);
        auto& chState = channels[ch];

        for (int i = 0; i < numSamples; ++i) {
            // Filter input through band-pass (sibilance region)
            float bandFiltered = processBiquad(inputData[i], chState.bandPassFilter);
            sibBands[ch][i] = bandFiltered;
            maxSibilanceEnergy = std::max(maxSibilanceEnergy, std::abs(bandFiltered));
        }
    }

    // 2. COMPUTE reduction amount based on sibilance intensity
    const float sibilanceDb = 20.0f * std::log10(std::max(maxSibilanceEnergy, 1.0e-6f));

    // Threshold at -40dB (soft knee of ±3dB)
    const float thresholdDb = -40.0f;
    const float kneeDb = 6.0f;

    // Compression ratio: higher strength = higher ratio (more aggressive)
    // strength 0.5 = 4:1, strength 1.0 = 8:1
    const float ratio = 2.0f + params.strength * 6.0f;

    // Calculate reduction in dB
    float targetReductionDb = applySoftCompression(sibilanceDb, thresholdDb, ratio, kneeDb);

    // Gate the reduction by detection intensity (only reduce when sibilance detected)
    targetReductionDb *= params.detectionIntensity;

    // Limit max reduction to -12dB (never too aggressive)
    targetReductionDb = std::max(targetReductionDb, -12.0f);

    // Smooth reduction amount (prevents pumping/artifacts)
    lastReductionDb = 0.95f * lastReductionDb + 0.05f * targetReductionDb;

    // 3. APPLY reduction to sibilance band only
    const float gainLinear = std::pow(10.0f, lastReductionDb / 20.0f);

    for (int ch = 0; ch < numChannels; ++ch) {
        float* audioData = buffer.getWritePointer(ch);

        for (int i = 0; i < numSamples; ++i) {
            // Get the pre-computed sibilance band
            const float sibBand = sibBands[ch][i];

            // Apply reduction to sibilance band only
            const float reduced = sibBand * gainLinear;

            // Blend: reduce only the sibilance, keep the rest of spectrum
            const float blendedBand = sibBand - reduced;  // Difference = amount reduced

            // Apply back to original (subtract the reduction from sibilance band)
            audioData[i] -= blendedBand;
        }
    }
}

} // namespace speech_clarity
} // namespace vxsuite
