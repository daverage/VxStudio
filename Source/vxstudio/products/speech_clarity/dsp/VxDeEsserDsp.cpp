#include "VxDeEsserDsp.h"
#include <algorithm>
#include <cmath>

namespace vxsuite {
namespace speech_clarity {

// ─── Static helpers ───────────────────────────────────────────────────────────

float DeEsserDsp::clamp01(float x) noexcept {
    return std::min(1.0f, std::max(0.0f, x));
}

float DeEsserDsp::smoothStep(float edge0, float edge1, float x) noexcept {
    if (edge0 == edge1)
        return x >= edge1 ? 1.0f : 0.0f;
    const float t = clamp01((x - edge0) / (edge1 - edge0));
    return t * t * (3.0f - 2.0f * t);
}

void DeEsserDsp::designHighPass(double sr, float hz, float q, BiquadState& s) noexcept {
    const float w0 = 2.0f * juce::MathConstants<float>::pi * hz / static_cast<float>(sr);
    const float cw = std::cos(w0);
    const float sw = std::sin(w0);
    const float alpha = sw / (2.0f * q);

    const float b0 =  (1.0f + cw) * 0.5f;
    const float b1 = -(1.0f + cw);
    const float b2 =  (1.0f + cw) * 0.5f;
    const float a0 =   1.0f + alpha;
    const float a1 =  -2.0f * cw;
    const float a2 =   1.0f - alpha;

    s.b0 = b0 / a0;  s.b1 = b1 / a0;  s.b2 = b2 / a0;
    s.a1 = a1 / a0;  s.a2 = a2 / a0;
}

void DeEsserDsp::designLowPass(double sr, float hz, float q, BiquadState& s) noexcept {
    const float w0 = 2.0f * juce::MathConstants<float>::pi * hz / static_cast<float>(sr);
    const float cw = std::cos(w0);
    const float sw = std::sin(w0);
    const float alpha = sw / (2.0f * q);

    const float b0 = (1.0f - cw) * 0.5f;
    const float b1 =  1.0f - cw;
    const float b2 = (1.0f - cw) * 0.5f;
    const float a0 =  1.0f + alpha;
    const float a1 = -2.0f * cw;
    const float a2 =  1.0f - alpha;

    s.b0 = b0 / a0;  s.b1 = b1 / a0;  s.b2 = b2 / a0;
    s.a1 = a1 / a0;  s.a2 = a2 / a0;
}

float DeEsserDsp::processBiquad(float x, BiquadState& s) noexcept {
    const float y = s.b0 * x + s.b1 * s.x1 + s.b2 * s.x2
                  - s.a1 * s.y1 - s.a2 * s.y2;
    s.x2 = s.x1;  s.x1 = x;
    s.y2 = s.y1;  s.y1 = y;
    return y;
}

// ─── Prepare / reset / mode ───────────────────────────────────────────────────

void DeEsserDsp::prepare(double sr, int /*maxBlockSize*/, int numChannels) {
    sampleRate = sr > 1000.0 ? sr : 48000.0;

    const float fs = static_cast<float>(sampleRate);

    envAttCoeff  = std::exp(-1.0f / (fs * 0.001f));   // 1 ms
    envRelCoeff  = std::exp(-1.0f / (fs * 0.055f));   // 55 ms (Speech default)

    gainAttCoeff = std::exp(-1.0f / (fs * 0.001f));   // 1 ms
    gainRelCoeff = std::exp(-1.0f / (fs * 0.055f));

    channels.resize(static_cast<size_t>(numChannels));

    updateModeCoefficients();
    redesignFilters();
    reset();
}

void DeEsserDsp::reset() noexcept {
    for (auto& c : channels) {
        c.bodyHp.x1  = c.bodyHp.x2  = c.bodyHp.y1  = c.bodyHp.y2  = 0.0f;
        c.bodyLp.x1  = c.bodyLp.x2  = c.bodyLp.y1  = c.bodyLp.y2  = 0.0f;
        c.lowerHp.x1 = c.lowerHp.x2 = c.lowerHp.y1 = c.lowerHp.y2 = 0.0f;
        c.lowerLp.x1 = c.lowerLp.x2 = c.lowerLp.y1 = c.lowerLp.y2 = 0.0f;
        c.upperHp.x1 = c.upperHp.x2 = c.upperHp.y1 = c.upperHp.y2 = 0.0f;
        c.upperLp.x1 = c.upperLp.x2 = c.upperLp.y1 = c.upperLp.y2 = 0.0f;

        c.bodyEnv  = 0.0f;
        c.lowerEnv = 0.0f;
        c.upperEnv = 0.0f;
        c.lowerGain = 1.0f;
        c.upperGain = 1.0f;
    }

    lastLowerReductionDb = 0.0f;
    lastUpperReductionDb = 0.0f;
}

void DeEsserDsp::setMode(CleanupMode newMode) noexcept {
    if (mode == newMode) return;
    mode = newMode;
    updateModeCoefficients();
}

void DeEsserDsp::updateModeCoefficients() {
    const float fs = static_cast<float>(sampleRate);

    if (mode == CleanupMode::Speech) {
        lowerMaxDb     = -7.0f;
        upperMaxDb     = -10.0f;
        lowerScoreLow  = 0.35f;
        lowerScoreHigh = 1.20f;
        upperScoreLow  = 0.30f;
        upperScoreHigh = 1.10f;
        lowerGenModeBias = 1.0f;   // balanced
        gainRelCoeff = std::exp(-1.0f / (fs * 0.055f));
    } else {
        lowerMaxDb     = -5.0f;    // -7.0 * 0.70 ≈ -5.0
        upperMaxDb     = -7.0f;    // -10.0 * 0.70 ≈ -7.0
        lowerScoreLow  = 0.55f;
        lowerScoreHigh = 1.60f;
        upperScoreLow  = 0.50f;
        upperScoreHigh = 1.50f;
        lowerGenModeBias = 0.75f;  // bias toward upper to avoid dulling music
        gainRelCoeff = std::exp(-1.0f / (fs * 0.035f));
    }
}

void DeEsserDsp::redesignFilters() {
    for (auto& c : channels) {
        // Body: 700–3500 Hz
        designHighPass(sampleRate, 700.0f,   0.707f, c.bodyHp);
        designLowPass (sampleRate, 3500.0f,  0.707f, c.bodyLp);

        // Lower sibilance: 4200–6500 Hz
        designHighPass(sampleRate, 4200.0f,  0.707f, c.lowerHp);
        designLowPass (sampleRate, 6500.0f,  0.707f, c.lowerLp);

        // Upper sibilance: 6500–10500 Hz
        designHighPass(sampleRate, 6500.0f,  0.707f, c.upperHp);
        designLowPass (sampleRate, 10500.0f, 0.707f, c.upperLp);
    }
}

// ─── Process ──────────────────────────────────────────────────────────────────

void DeEsserDsp::process(juce::AudioBuffer<float>& buffer, const Params& params) {
    const float strength = clamp01(params.strength);
    const float upstream = clamp01(params.detectionIntensity);

    if (strength < 0.001f)
        return;

    const int numCh = buffer.getNumChannels();
    const int n     = buffer.getNumSamples();

    const float lowerMax = lowerMaxDb * strength;
    const float upperMax = upperMaxDb * strength;

    for (int ch = 0; ch < std::min(numCh, static_cast<int>(channels.size())); ++ch) {
        auto& c    = channels[static_cast<size_t>(ch)];
        float* buf = buffer.getWritePointer(ch);

        for (int i = 0; i < n; ++i) {
            const float x = buf[i];

            // ── Band extraction ─────────────────────────────────────────────
            const float bodyBand  = processBiquad(processBiquad(x, c.bodyHp),  c.bodyLp);
            const float lowerBand = processBiquad(processBiquad(x, c.lowerHp), c.lowerLp);
            const float upperBand = processBiquad(processBiquad(x, c.upperHp), c.upperLp);

            const float bodyRect  = std::abs(bodyBand);
            const float lowerRect = std::abs(lowerBand);
            const float upperRect = std::abs(upperBand);

            // ── Envelopes ───────────────────────────────────────────────────
            c.bodyEnv = bodyRect > c.bodyEnv
                ? envAttCoeff * c.bodyEnv + (1.0f - envAttCoeff) * bodyRect
                : envRelCoeff * c.bodyEnv + (1.0f - envRelCoeff) * bodyRect;

            c.lowerEnv = lowerRect > c.lowerEnv
                ? envAttCoeff * c.lowerEnv + (1.0f - envAttCoeff) * lowerRect
                : envRelCoeff * c.lowerEnv + (1.0f - envRelCoeff) * lowerRect;

            c.upperEnv = upperRect > c.upperEnv
                ? envAttCoeff * c.upperEnv + (1.0f - envAttCoeff) * upperRect
                : envRelCoeff * c.upperEnv + (1.0f - envRelCoeff) * upperRect;

            // ── Detector (ratio against body reference) ─────────────────────
            const float bodyRef = std::max(c.bodyEnv, 1.0e-6f);

            float lowerRatio = c.lowerEnv / bodyRef;
            float upperRatio = c.upperEnv / bodyRef;

            float lowerScore = smoothStep(lowerScoreLow, lowerScoreHigh, lowerRatio) * upstream;
            float upperScore = smoothStep(upperScoreLow, upperScoreHigh, upperRatio) * upstream;

            lowerScore = clamp01(lowerScore * lowerGenModeBias);
            upperScore = clamp01(upperScore);

            // ── Per-band gain computation ────────────────────────────────────
            const float lowerTargetDb = lowerMax * lowerScore;
            const float upperTargetDb = upperMax * upperScore;

            const float lowerTargetGain = std::pow(10.0f, lowerTargetDb / 20.0f);
            const float upperTargetGain = std::pow(10.0f, upperTargetDb / 20.0f);

            c.lowerGain = lowerTargetGain < c.lowerGain
                ? gainAttCoeff * c.lowerGain + (1.0f - gainAttCoeff) * lowerTargetGain
                : gainRelCoeff * c.lowerGain + (1.0f - gainRelCoeff) * lowerTargetGain;

            c.upperGain = upperTargetGain < c.upperGain
                ? gainAttCoeff * c.upperGain + (1.0f - gainAttCoeff) * upperTargetGain
                : gainRelCoeff * c.upperGain + (1.0f - gainRelCoeff) * upperTargetGain;

            // ── Subtractive output ──────────────────────────────────────────
            buf[i] = x
                - lowerBand * (1.0f - c.lowerGain)
                - upperBand * (1.0f - c.upperGain);
        }

        // Update metering from last channel
        if (c.lowerGain < 0.999f)
            lastLowerReductionDb = 20.0f * std::log10(std::max(c.lowerGain, 1.0e-6f));
        else
            lastLowerReductionDb = 0.0f;

        if (c.upperGain < 0.999f)
            lastUpperReductionDb = 20.0f * std::log10(std::max(c.upperGain, 1.0e-6f));
        else
            lastUpperReductionDb = 0.0f;
    }
}

} // namespace speech_clarity
} // namespace vxsuite
