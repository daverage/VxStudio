#include "VxDePolosiveDsp.h"
#include <algorithm>
#include <cmath>

namespace vxsuite {
namespace speech_clarity {

// ─── Static helpers ───────────────────────────────────────────────────────────

float DePolosiveDsp::clamp01(float x) noexcept {
    return std::min(1.0f, std::max(0.0f, x));
}

float DePolosiveDsp::smoothStep(float edge0, float edge1, float x) noexcept {
    if (edge0 == edge1)
        return x >= edge1 ? 1.0f : 0.0f;
    const float t = clamp01((x - edge0) / (edge1 - edge0));
    return t * t * (3.0f - 2.0f * t);
}

void DePolosiveDsp::designHighPass(double sr, float hz, float q, BiquadState& s) noexcept {
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

void DePolosiveDsp::designLowPass(double sr, float hz, float q, BiquadState& s) noexcept {
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

float DePolosiveDsp::processBiquad(float x, BiquadState& s) noexcept {
    const float y = s.b0 * x + s.b1 * s.x1 + s.b2 * s.x2
                  - s.a1 * s.y1 - s.a2 * s.y2;
    s.x2 = s.x1;  s.x1 = x;
    s.y2 = s.y1;  s.y1 = y;
    return y;
}

// ─── Prepare / reset / mode ───────────────────────────────────────────────────

void DePolosiveDsp::prepare(double sr, int /*maxBlockSize*/, int numChannels) {
    sampleRate = sr > 1000.0 ? sr : 48000.0;

    const float fs = static_cast<float>(sampleRate);

    // Detector envelope timing
    attCoeff     = std::exp(-1.0f / (fs * 0.0015f));  // 1.5 ms
    relCoeff     = std::exp(-1.0f / (fs * 0.1200f));  // 120 ms

    // Fast onset tracker (compares against relCoeff baseline)
    onsetAttCoeff = std::exp(-1.0f / (fs * 0.0005f)); // 0.5 ms

    // Crossfade smoothing
    xfadeAttCoeff = std::exp(-1.0f / (fs * 0.0015f)); // 1.5 ms attack
    xfadeRelCoeff = std::exp(-1.0f / (fs * 0.1200f)); // 120 ms release

    channels.resize(static_cast<size_t>(numChannels));

    updateModeCoefficients();
    redesignProcessingFilters();
    reset();
}

void DePolosiveDsp::reset() noexcept {
    for (auto& c : channels) {
        c.detLp.x1   = c.detLp.x2   = c.detLp.y1   = c.detLp.y2   = 0.0f;
        c.detBpHp.x1 = c.detBpHp.x2 = c.detBpHp.y1 = c.detBpHp.y2 = 0.0f;
        c.detBpLp.x1 = c.detBpLp.x2 = c.detBpLp.y1 = c.detBpLp.y2 = 0.0f;
        c.normalHpf.x1 = c.normalHpf.x2 = c.normalHpf.y1 = c.normalHpf.y2 = 0.0f;
        c.strongHpf.x1 = c.strongHpf.x2 = c.strongHpf.y1 = c.strongHpf.y2 = 0.0f;

        c.lowEnv = c.midEnv = c.lowOnsetEnv = 0.0f;
        c.score  = c.xfade  = 0.0f;
        c.holdCounter    = 0;
        c.currentStrongHz = strongHpfMinHz;
    }

    lastReductionDb  = 0.0f;
    lastPlosiveScore = 0.0f;
}

void DePolosiveDsp::setMode(CleanupMode newMode) noexcept {
    if (mode == newMode) return;
    mode = newMode;
    updateModeCoefficients();
    redesignProcessingFilters();
}

void DePolosiveDsp::updateModeCoefficients() {
    const float fs = static_cast<float>(sampleRate);

    if (mode == CleanupMode::Speech) {
        normalHpfHz    = 70.0f;
        strongHpfMinHz = 140.0f;
        strongHpfMaxHz = 260.0f;

        holdSamples = juce::jlimit(16, 4096,
            static_cast<int>(std::round(sampleRate * 0.035)));  // 35 ms

        xfadeAttCoeff = std::exp(-1.0f / (fs * 0.001f));   // 1 ms attack
        xfadeRelCoeff = std::exp(-1.0f / (fs * 0.060f));   // 60 ms release
    } else {
        normalHpfHz    = 50.0f;
        strongHpfMinHz = 100.0f;
        strongHpfMaxHz = 180.0f;

        holdSamples = juce::jlimit(16, 4096,
            static_cast<int>(std::round(sampleRate * 0.020)));  // 20 ms

        xfadeAttCoeff = std::exp(-1.0f / (fs * 0.002f));   // 2 ms attack
        xfadeRelCoeff = std::exp(-1.0f / (fs * 0.040f));   // 40 ms release
    }
}

void DePolosiveDsp::redesignProcessingFilters() {
    for (auto& c : channels) {
        // Detector: low band at 180 Hz
        designLowPass(sampleRate, 180.0f, 0.707f, c.detLp);

        // Detector: mid band 250–1500 Hz (two-pole cascade)
        designHighPass(sampleRate, 250.0f,  0.707f, c.detBpHp);
        designLowPass (sampleRate, 1500.0f, 0.707f, c.detBpLp);

        // Processing: normal HPF
        designHighPass(sampleRate, normalHpfHz, 0.707f, c.normalHpf);

        // Processing: strong HPF, start at min
        designHighPass(sampleRate, strongHpfMinHz, 0.707f, c.strongHpf);

        c.currentStrongHz = strongHpfMinHz;
    }
}

// ─── Process ──────────────────────────────────────────────────────────────────

void DePolosiveDsp::process(juce::AudioBuffer<float>& buffer, const Params& params) {
    const float strength = clamp01(params.strength);
    const float upstream = clamp01(params.detectionIntensity);

    if (strength < 0.001f) {
        // Still run processing filters to keep them warm
        return;
    }

    const int numCh = buffer.getNumChannels();
    const int n     = buffer.getNumSamples();

    const float modeStrength = (mode == CleanupMode::Speech) ? strength : strength * 0.65f;

    // Allow internal ratio/onset detection even when the external hint is weak.
    // The hint scales sensitivity but a floor ensures genuine low/mid bursts still register.
    const float effectiveUpstream = juce::jmax(0.20f, upstream);

    // Control block size for coefficient recalculation (every 64 samples is cheap)
    constexpr int kCtrlBlock = 64;

    for (int ch = 0; ch < std::min(numCh, static_cast<int>(channels.size())); ++ch) {
        auto& c    = channels[static_cast<size_t>(ch)];
        float* buf = buffer.getWritePointer(ch);

        int samplesDone = 0;

        while (samplesDone < n) {
            const int blockEnd = std::min(samplesDone + kCtrlBlock, n);

            // Update strong HPF coefficients once per control block
            const float targetStrongHz = strongHpfMinHz
                + (strongHpfMaxHz - strongHpfMinHz) * modeStrength * c.score;

            // Smooth the cutoff to avoid zipper noise
            c.currentStrongHz = c.currentStrongHz * 0.90f + targetStrongHz * 0.10f;
            designHighPass(sampleRate, c.currentStrongHz, 0.707f, c.strongHpf);

            for (int i = samplesDone; i < blockEnd; ++i) {
                const float x = buf[i];

                // ── Detector ─────────────────────────────────────────────────
                const float lowOut = processBiquad(x, c.detLp);
                const float midOut = processBiquad(processBiquad(x, c.detBpHp), c.detBpLp);

                const float lowRect = std::abs(lowOut);
                const float midRect = std::abs(midOut);

                c.lowEnv = lowRect > c.lowEnv
                    ? attCoeff * c.lowEnv + (1.0f - attCoeff) * lowRect
                    : relCoeff * c.lowEnv + (1.0f - relCoeff) * lowRect;

                c.midEnv = midRect > c.midEnv
                    ? attCoeff * c.midEnv + (1.0f - attCoeff) * midRect
                    : relCoeff * c.midEnv + (1.0f - relCoeff) * midRect;

                // Fast onset: track how quickly low-band energy rose
                c.lowOnsetEnv = lowRect > c.lowOnsetEnv
                    ? onsetAttCoeff * c.lowOnsetEnv + (1.0f - onsetAttCoeff) * lowRect
                    : relCoeff      * c.lowOnsetEnv + (1.0f - relCoeff)      * lowRect;

                const float lowMidRatio  = c.lowEnv     / std::max(c.midEnv,     1.0e-6f);
                const float lowOnsetRatio = c.lowOnsetEnv / std::max(c.lowEnv + 1.0e-9f, 1.0e-6f);

                float ratioScore, onsetScore;
                if (mode == CleanupMode::Speech) {
                    ratioScore = smoothStep(1.8f, 5.0f, lowMidRatio);
                    onsetScore = smoothStep(1.5f, 4.0f, lowOnsetRatio);
                } else {
                    ratioScore = smoothStep(2.5f, 7.0f, lowMidRatio);
                    onsetScore = smoothStep(2.0f, 5.5f, lowOnsetRatio);
                }

                const float rawScore = clamp01(effectiveUpstream * (0.75f * ratioScore + 0.25f * onsetScore));

                // Smooth score and apply hold
                c.score = rawScore > c.score
                    ? xfadeAttCoeff * c.score + (1.0f - xfadeAttCoeff) * rawScore
                    : xfadeRelCoeff * c.score + (1.0f - xfadeRelCoeff) * rawScore;

                if (rawScore > 0.3f)
                    c.holdCounter = holdSamples;
                else if (c.holdCounter > 0)
                    --c.holdCounter;

                const float effectiveScore = (c.holdCounter > 0) ? std::max(c.score, 0.1f) : c.score;
                const float xfade = clamp01(effectiveScore * modeStrength);

                // Smooth xfade
                c.xfade = xfade > c.xfade
                    ? xfadeAttCoeff * c.xfade + (1.0f - xfadeAttCoeff) * xfade
                    : xfadeRelCoeff * c.xfade + (1.0f - xfadeRelCoeff) * xfade;

                // ── Processing: crossfade dry ↔ strong HPF ───────────────────
                // Dry is deliberately used as the baseline so the module does not constantly thin the vocal.
                processBiquad(x, c.normalHpf);
                const float strongOut = processBiquad(x, c.strongHpf);

                buf[i] = x * (1.0f - c.xfade) + strongOut * c.xfade;
            }

            samplesDone = blockEnd;
        }

        // Update metering from last sample
        lastPlosiveScore = c.score;
        // Approximate reduction in dB: at full xfade the low end is cut by the HPF difference
        lastReductionDb = c.xfade > 0.001f
            ? -6.0f * c.xfade * strength   // rough estimate
            : 0.0f;
    }
}

} // namespace speech_clarity
} // namespace vxsuite
