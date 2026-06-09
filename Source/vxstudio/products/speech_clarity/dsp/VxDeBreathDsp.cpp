#include "VxDeBreathDsp.h"
#include <algorithm>
#include <cmath>

namespace vxsuite {
namespace speech_clarity {

// ─── Static helpers ───────────────────────────────────────────────────────────

float DeBreathDsp::clamp01(float x) noexcept {
    return std::min(1.0f, std::max(0.0f, x));
}

float DeBreathDsp::smoothStep(float edge0, float edge1, float x) noexcept {
    if (edge0 == edge1)
        return x >= edge1 ? 1.0f : 0.0f;
    const float t = clamp01((x - edge0) / (edge1 - edge0));
    return t * t * (3.0f - 2.0f * t);
}

void DeBreathDsp::designHighPass(double sr, float hz, float q, BiquadState& s) noexcept {
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

void DeBreathDsp::designLowPass(double sr, float hz, float q, BiquadState& s) noexcept {
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

float DeBreathDsp::processBiquad(float x, BiquadState& s) noexcept {
    const float y = s.b0 * x + s.b1 * s.x1 + s.b2 * s.x2
                  - s.a1 * s.y1 - s.a2 * s.y2;
    s.x2 = s.x1;  s.x1 = x;
    s.y2 = s.y1;  s.y1 = y;
    return y;
}

// ─── Prepare / reset / mode ───────────────────────────────────────────────────

void DeBreathDsp::prepare(double sr, int /*maxBlockSize*/, int numChannels) {
    sampleRate = sr > 1000.0 ? sr : 48000.0;

    const float fs = static_cast<float>(sampleRate);

    // Envelope followers (shared across modes)
    envAttCoeff = std::exp(-1.0f / (fs * 0.005f));   // 5 ms
    envRelCoeff = std::exp(-1.0f / (fs * 0.080f));   // 80 ms

    // Speech reference tracker: fast attack (30 ms), slow release (1.2 s)
    speechRefAttCoeff = std::exp(-1.0f / (fs * 0.030f));  // fixed: was 3.0s
    speechRefRelCoeff = std::exp(-1.0f / (fs * 1.200f));

    channels.resize(static_cast<size_t>(numChannels));

    for (auto& c : channels) {
        designHighPass(sampleRate, 900.0f,   0.707f, c.breathHp);
        designLowPass (sampleRate, 10000.0f, 0.707f, c.breathLp);
        designLowPass (sampleRate, 250.0f,   0.707f, c.lowLp);
    }

    updateModeCoefficients();
    reset();
}

void DeBreathDsp::reset() noexcept {
    for (auto& c : channels) {
        c.breathHp.x1 = c.breathHp.x2 = c.breathHp.y1 = c.breathHp.y2 = 0.0f;
        c.breathLp.x1 = c.breathLp.x2 = c.breathLp.y1 = c.breathLp.y2 = 0.0f;
        c.lowLp.x1    = c.lowLp.x2    = c.lowLp.y1    = c.lowLp.y2    = 0.0f;

        c.fullEnv = c.breathEnv = c.lowEnv = c.speechRefEnv = 0.0f;

        c.state           = BreathState::Idle;
        c.candidateSamples = 0;
        c.activeSamples    = 0;
        c.releaseSamples   = 0;
        c.regionScore      = 0.0f;
        c.currentGain      = 1.0f;
    }

    lastReductionDb  = 0.0f;
    lastBreathScore  = 0.0f;
    anyChannelActive = false;
}

void DeBreathDsp::setMode(CleanupMode newMode) noexcept {
    if (mode == newMode) return;
    mode = newMode;
    updateModeCoefficients();
}

void DeBreathDsp::updateModeCoefficients() {
    const float fs = static_cast<float>(sampleRate);

    if (mode == CleanupMode::Speech) {
        minBreathSamples           = juce::jlimit(16, 8192,
            static_cast<int>(std::round(sampleRate * 0.090f)));   // 90 ms
        maxBreathSamples           = juce::jlimit(64, 65536,
            static_cast<int>(std::round(sampleRate * 1.600f)));   // 1600 ms
        candidateStartThreshold    = 0.55f;
        candidateContinueThreshold = 0.30f;
        maxReductionDb             = -15.0f;

        gainAttCoeff = std::exp(-1.0f / (fs * 0.080f));   // 80 ms attack
        gainRelCoeff = std::exp(-1.0f / (fs * 0.350f));   // 350 ms release
    } else {
        minBreathSamples           = juce::jlimit(16, 8192,
            static_cast<int>(std::round(sampleRate * 0.180f)));   // 180 ms
        maxBreathSamples           = juce::jlimit(64, 65536,
            static_cast<int>(std::round(sampleRate * 1.000f)));   // 1000 ms
        candidateStartThreshold    = 0.75f;
        candidateContinueThreshold = 0.45f;
        maxReductionDb             = -6.0f;

        gainAttCoeff = std::exp(-1.0f / (fs * 0.120f));   // 120 ms attack
        gainRelCoeff = std::exp(-1.0f / (fs * 0.500f));   // 500 ms release
    }
}

// ─── Process ──────────────────────────────────────────────────────────────────

void DeBreathDsp::process(juce::AudioBuffer<float>& buffer, const Params& params) {
    const float strength = clamp01(params.strength);
    const float upstream = clamp01(params.detectionIntensity);

    if (strength < 0.001f)
        return;

    const int numCh = buffer.getNumChannels();
    const int n     = buffer.getNumSamples();

    // Mode-scaled reduction limit
    const float reductionDb = maxReductionDb * strength;

    bool anyChanActive = false;

    for (int ch = 0; ch < std::min(numCh, static_cast<int>(channels.size())); ++ch) {
        auto& c    = channels[static_cast<size_t>(ch)];
        float* buf = buffer.getWritePointer(ch);

        for (int i = 0; i < n; ++i) {
            const float x = buf[i];

            // ── Feature extraction ──────────────────────────────────────────
            const float breathHpOut = processBiquad(x, c.breathHp);
            const float breathBand  = processBiquad(breathHpOut, c.breathLp);
            const float lowBand     = processBiquad(x, c.lowLp);

            const float fullRect   = std::abs(x);
            const float breathRect = std::abs(breathBand);
            const float lowRect    = std::abs(lowBand);

            c.fullEnv = fullRect > c.fullEnv
                ? envAttCoeff * c.fullEnv + (1.0f - envAttCoeff) * fullRect
                : envRelCoeff * c.fullEnv + (1.0f - envRelCoeff) * fullRect;

            c.breathEnv = breathRect > c.breathEnv
                ? envAttCoeff * c.breathEnv + (1.0f - envAttCoeff) * breathRect
                : envRelCoeff * c.breathEnv + (1.0f - envRelCoeff) * breathRect;

            c.lowEnv = lowRect > c.lowEnv
                ? envAttCoeff * c.lowEnv + (1.0f - envAttCoeff) * lowRect
                : envRelCoeff * c.lowEnv + (1.0f - envRelCoeff) * lowRect;

            // Speech reference: only rises when speech is present (fast att = 30 ms)
            c.speechRefEnv = c.fullEnv > c.speechRefEnv
                ? speechRefAttCoeff * c.speechRefEnv + (1.0f - speechRefAttCoeff) * c.fullEnv
                : speechRefRelCoeff * c.speechRefEnv + (1.0f - speechRefRelCoeff) * c.fullEnv;

            const float denom = std::max(c.fullEnv, 1.0e-6f);
            const float breathRatio = c.breathEnv / denom;
            const float lowRatio    = c.lowEnv    / denom;
            const float relativeLevel = c.fullEnv / std::max(c.speechRefEnv, 1.0e-6f);

            // quietComparedToSpeech: floor at 0.10 (was 0.25) so isolated breaths still fire
            const float quietComparedToSpeech =
                0.10f + 0.90f * (1.0f - smoothStep(0.45f, 0.90f, relativeLevel));

            const float breathBandScore = smoothStep(0.18f, 0.55f, breathRatio);
            const float lowPenalty      = smoothStep(0.30f, 0.70f, lowRatio);
            const float levelScore      = smoothStep(0.00003f, 0.0005f, c.fullEnv);

            // Phrase-boundary score: was speech recently active and now quieter?
            const float speechWasHigh    = smoothStep(0.0005f, 0.005f, c.speechRefEnv);
            const float currentBelowRef  = 1.0f - smoothStep(0.45f, 0.90f, relativeLevel);
            const float phraseScore      = speechWasHigh * currentBelowRef;

            float instantScore =
                  upstream
                * breathBandScore
                * quietComparedToSpeech
                * (1.0f - 0.85f * lowPenalty)
                * levelScore;
            instantScore = clamp01(instantScore);
            instantScore *= (0.70f + 0.30f * phraseScore);
            instantScore = clamp01(instantScore);

            // ── Region state machine ────────────────────────────────────────
            switch (c.state) {
                case BreathState::Idle:
                    if (instantScore > candidateStartThreshold) {
                        c.state            = BreathState::Candidate;
                        c.candidateSamples = 1;
                        c.regionScore      = instantScore;
                    }
                    break;

                case BreathState::Candidate:
                    if (instantScore > candidateContinueThreshold) {
                        ++c.candidateSamples;
                        c.regionScore = c.regionScore * 0.9f + instantScore * 0.1f;

                        if (c.candidateSamples >= minBreathSamples) {
                            c.state        = BreathState::Active;
                            c.activeSamples = 0;
                        } else if (c.candidateSamples > maxBreathSamples) {
                            c.state            = BreathState::Idle;
                            c.candidateSamples = 0;
                        }
                    } else {
                        // Candidate too short — discard, protect h/f/s/th
                        c.state            = BreathState::Idle;
                        c.candidateSamples = 0;
                        c.regionScore      = 0.0f;
                    }
                    break;

                case BreathState::Active:
                    ++c.activeSamples;
                    c.regionScore = c.regionScore * 0.98f + instantScore * 0.02f;

                    if (instantScore <= candidateContinueThreshold
                            || c.activeSamples > maxBreathSamples) {
                        c.state          = BreathState::Release;
                        c.releaseSamples = 0;
                    }
                    break;

                case BreathState::Release:
                    ++c.releaseSamples;
                    // Wait for gain to recover before returning to Idle
                    if (c.currentGain > 0.999f || c.releaseSamples > maxBreathSamples / 4) {
                        c.state            = BreathState::Idle;
                        c.candidateSamples = 0;
                        c.activeSamples    = 0;
                        c.releaseSamples   = 0;
                        c.regionScore      = 0.0f;
                    }
                    break;
            }

            // ── Gain computation ────────────────────────────────────────────
            float targetGain;
            if (c.state == BreathState::Active) {
                const float targetDb = reductionDb * c.regionScore;
                targetGain = std::pow(10.0f, targetDb / 20.0f);
                anyChanActive = true;
            } else {
                targetGain = 1.0f;
            }

            c.currentGain = targetGain < c.currentGain
                ? gainAttCoeff * c.currentGain + (1.0f - gainAttCoeff) * targetGain
                : gainRelCoeff * c.currentGain + (1.0f - gainRelCoeff) * targetGain;

            buf[i] = x * c.currentGain;
        }

        // Update metering from last channel
        lastBreathScore = channels[static_cast<size_t>(ch)].regionScore;
        if (c.currentGain < 0.999f)
            lastReductionDb = 20.0f * std::log10(std::max(c.currentGain, 1.0e-6f));
        else
            lastReductionDb = 0.0f;
    }

    anyChannelActive = anyChanActive;
}

} // namespace speech_clarity
} // namespace vxsuite
