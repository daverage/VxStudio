#include "VxDeClickDsp.h"
#include <algorithm>

namespace vxsuite {
namespace speech_clarity {

// ─── Static helpers ──────────────────────────────────────────────────────────

float DeClickDsp::clamp01(float x) noexcept {
    return std::min(1.0f, std::max(0.0f, x));
}

float DeClickDsp::smoothStep(float edge0, float edge1, float x) noexcept {
    if (edge0 == edge1)
        return x >= edge1 ? 1.0f : 0.0f;
    const float t = clamp01((x - edge0) / (edge1 - edge0));
    return t * t * (3.0f - 2.0f * t);
}

void DeClickDsp::designHighPass(double sr, float hz, float q, BiquadState& s) noexcept {
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

void DeClickDsp::designLowPass(double sr, float hz, float q, BiquadState& s) noexcept {
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

float DeClickDsp::processBiquad(float x, BiquadState& s) noexcept {
    const float y = s.b0 * x + s.b1 * s.x1 + s.b2 * s.x2
                  - s.a1 * s.y1 - s.a2 * s.y2;
    s.x2 = s.x1;  s.x1 = x;
    s.y2 = s.y1;  s.y1 = y;
    return y;
}

float DeClickDsp::boundedRepairInterp(float y0, float y1, float y2, float y3, float t) noexcept {
    juce::ignoreUnused(y0, y3);
    const float linear = y1 + (y2 - y1) * clamp01(t);
    const float limit = std::max({ std::abs(y0), std::abs(y1), std::abs(y2), std::abs(y3), 1.0e-5f });
    return juce::jlimit(-limit, limit, linear);
}

float DeClickDsp::median9(std::array<float, 9>& v) noexcept {
    for (size_t i = 1; i < v.size(); ++i) {
        const float key = v[i];
        size_t j = i;
        while (j > 0 && v[j - 1] > key) { v[j] = v[j - 1]; --j; }
        v[j] = key;
    }
    return v[4];
}

// ─── Ring buffer helpers ──────────────────────────────────────────────────────

float DeClickDsp::getRingSample(const ChannelState& c, int64_t idx) const noexcept {
    if (ringSize <= 0) return 0.0f;
    int i = static_cast<int>(idx % ringSize);
    if (i < 0) i += ringSize;
    return c.ring[static_cast<size_t>(i)];
}

void DeClickDsp::setRingSample(ChannelState& c, int64_t idx, float value) noexcept {
    if (ringSize <= 0) return;
    int i = static_cast<int>(idx % ringSize);
    if (i < 0) i += ringSize;
    c.ring[static_cast<size_t>(i)] = value;
}

// ─── Repair ──────────────────────────────────────────────────────────────────

bool DeClickDsp::repairRegion(ChannelState& c, int64_t start, int64_t end,
                               float amount, bool longBlend) noexcept {
    if (end < start || amount < 0.001f) return false;
    if (start < 8) return false;

    const float y0 = getRingSample(c, start - 2);
    const float y1 = getRingSample(c, start - 1);
    const float y2 = getRingSample(c, end + 1);
    const float y3 = getRingSample(c, end + 2);

    const int64_t span = end - start + 1;
    const int64_t fadeSamples = juce::jlimit<int64_t>(2, longBlend ? 48 : 16, span / 2 + 1);

    const auto editBlend = [amount, fadeSamples, span](int64_t i) noexcept {
        if (span <= 1)
            return 0.0f;

        const float fadeIn = smoothStep(0.0f, static_cast<float>(fadeSamples),
                                        static_cast<float>(i));
        const float fadeOut = smoothStep(0.0f, static_cast<float>(fadeSamples),
                                         static_cast<float>(span - 1 - i));
        return amount * std::min(fadeIn, fadeOut);
    };

    float ambientStep = 0.0f;
    for (int64_t k = 1; k <= 8; ++k) {
        ambientStep += std::abs(getRingSample(c, start - k) - getRingSample(c, start - k - 1));
        ambientStep += std::abs(getRingSample(c, end + k + 1) - getRingSample(c, end + k));
    }
    ambientStep *= 1.0f / 16.0f;
    const float anchorStep = std::max(std::abs(y1 - y0), std::abs(y3 - y2));
    const float anchorLevel = std::max({ std::abs(y0), std::abs(y1), std::abs(y2), std::abs(y3), 1.0e-5f });
    const float residualLimit = std::max({ ambientStep * (longBlend ? 3.0f : 2.0f),
                                           anchorStep * (longBlend ? 2.4f : 1.8f),
                                           anchorLevel * (longBlend ? 0.20f : 0.14f),
                                           0.0015f });

    const auto localMedianAt = [this, &c](int64_t idx) noexcept {
        std::array<float, 9> values;
        for (int k = 0; k < 9; ++k)
            values[static_cast<size_t>(k)] = getRingSample(c, idx - 4 + k);
        return median9(values);
    };

    const auto candidateAt = [this, &c, start, end, span, y0, y1, y2, y3,
                              editBlend, localMedianAt, residualLimit](int64_t i) noexcept {
        const float tLinear = span > 1
            ? static_cast<float>(i) / static_cast<float>(span - 1)
            : 0.5f;
        const float original = getRingSample(c, start + i);
        const float trend = boundedRepairInterp(y0, y1, y2, y3, tLinear);
        const float median = localMedianAt(start + i);
        const float baseline = 0.72f * median + 0.28f * trend;
        const float repaired = baseline + juce::jlimit(-residualLimit, residualLimit, original - baseline);
        const float blend = editBlend(i);
        const float candidate = original * (1.0f - blend) + repaired * blend;
        const float localLimit = std::max({ std::abs(original),
                                            std::abs(repaired),
                                            std::abs(y0),
                                            std::abs(y1),
                                            std::abs(y2),
                                            std::abs(y3),
                                            1.0e-5f });
        juce::ignoreUnused(end);
        return juce::jlimit(-localLimit, localLimit, candidate);
    };

    float originalMaxStep = 0.0f;
    float candidateMaxStep = 0.0f;
    float previousOriginal = y1;
    float previousCandidate = y1;
    for (int64_t i = 0; i < span; ++i) {
        const float original = getRingSample(c, start + i);
        const float candidate = candidateAt(i);
        originalMaxStep = std::max(originalMaxStep, std::abs(original - previousOriginal));
        candidateMaxStep = std::max(candidateMaxStep, std::abs(candidate - previousCandidate));
        previousOriginal = original;
        previousCandidate = candidate;
    }
    originalMaxStep = std::max(originalMaxStep, std::abs(y2 - previousOriginal));
    candidateMaxStep = std::max(candidateMaxStep, std::abs(y2 - previousCandidate));

    const float allowedStep = std::max(originalMaxStep * 1.03f,
                                       anchorStep * 2.0f + 1.0e-4f);
    if (candidateMaxStep > allowedStep)
        return false;
    if (originalMaxStep > 0.002f) {
        const float requiredImprovement = longBlend ? 0.98f : 0.92f;
        if (candidateMaxStep > originalMaxStep * requiredImprovement)
            return false;
    }

    for (int64_t i = 0; i < span; ++i) {
        setRingSample(c, start + i, candidateAt(i));
    }

    return true;
}

// ─── Prepare / reset / mode ───────────────────────────────────────────────────

void DeClickDsp::prepare(double sr, int maxBlockSize, int numChannels) {
    sampleRate = sr > 1000.0 ? sr : 48000.0;

    updateModeCoefficients();

    const int safety = maxBlockSize + lookaheadSamples
                     + std::max(hardMaxSamples, mouthMaxSamples)
                     + std::max(hardPostMargin, mouthPostMargin) + 64;

    ringSize = juce::jmax(4096, safety);

    channels.resize(static_cast<size_t>(numChannels));
    redesignFilters();
    reset();
}

void DeClickDsp::reset() noexcept {
    for (auto& c : channels) {
        std::fill(c.ring.begin(), c.ring.end(), 0.0f);

        c.hardHp.x1 = c.hardHp.x2 = c.hardHp.y1 = c.hardHp.y2 = 0.0f;
        c.mouthHp.x1 = c.mouthHp.x2 = c.mouthHp.y1 = c.mouthHp.y2 = 0.0f;
        c.lowLp.x1 = c.lowLp.x2 = c.lowLp.y1 = c.lowLp.y2 = 0.0f;

        c.hardFastEnv = c.hardSlowEnv = 0.0f;
        c.hardSlopeFast = c.hardSlopeSlow = 0.0f;
        c.hardPrevX = 0.0f;

        c.mouthFastEnv = c.mouthSlowEnv = 0.0f;
        c.lowEnv = c.fullEnv = 0.0f;

        c.hard = {};
        c.mouth = {};

        c.writeIndex = 0;
        c.absoluteIndex = 0;
        c.lastRepairStart = -1;
        c.lastRepairEnd = -1;
    }

    lastHardClickRepair  = 0.0f;
    lastMouthClickRepair = 0.0f;
    recentRepairCount    = 0;
}

void DeClickDsp::setMode(CleanupMode newMode) noexcept {
    if (mode == newMode) return;
    mode = newMode;
    updateModeCoefficients();
    redesignFilters();
}

int DeClickDsp::getLatencySamples() const noexcept {
    return lookaheadSamples;
}

// ─── Mode coefficient computation ─────────────────────────────────────────────

void DeClickDsp::updateModeCoefficients() {
    const float fs = static_cast<float>(sampleRate);

    if (mode == CleanupMode::Speech) {
        // Lookahead: 15 ms
        lookaheadSamples = juce::jlimit(128, 1024,
            static_cast<int>(std::round(sampleRate * 0.015)));

        // Hard-click lane (0.3–5 ms)
        hardHpCutoffHz = 2200.0f;
        hardMaxSamples = juce::jlimit(16, 512,
            static_cast<int>(std::round(sampleRate * 0.005)));  // 5 ms
        hardPreMargin = juce::jlimit(2, 64,
            static_cast<int>(std::round(sampleRate * 0.00035)));  // 0.35 ms
        hardPostMargin = juce::jlimit(4, 96,
            static_cast<int>(std::round(sampleRate * 0.00070)));  // 0.70 ms
        hardCooldown = juce::jlimit(32, 512,
            static_cast<int>(std::round(sampleRate * 0.004)));   // 4 ms

        // Mouth-click lane (1.5–30 ms)
        mouthHpCutoffHz = 1200.0f;
        mouthMinSamples = juce::jlimit(4, 256,
            static_cast<int>(std::round(sampleRate * 0.0015)));  // 1.5 ms
        mouthMaxSamples = juce::jlimit(64, 2048,
            static_cast<int>(std::round(sampleRate * 0.015)));   // 15 ms (bounded by lookahead)
        mouthPreMargin = juce::jlimit(4, 96,
            static_cast<int>(std::round(sampleRate * 0.001)));
        mouthPostMargin = juce::jlimit(8, 128,
            static_cast<int>(std::round(sampleRate * 0.002)));
        mouthCooldown = juce::jlimit(64, 1024,
            static_cast<int>(std::round(sampleRate * 0.008)));   // 8 ms
    } else {
        // Lookahead: 8 ms
        lookaheadSamples = juce::jlimit(128, 1024,
            static_cast<int>(std::round(sampleRate * 0.008)));

        // Hard-click lane (0.3–3.5 ms, stricter)
        hardHpCutoffHz = 3000.0f;
        hardMaxSamples = juce::jlimit(16, 384,
            static_cast<int>(std::round(sampleRate * 0.0035)));  // 3.5 ms
        hardPreMargin = juce::jlimit(2, 48,
            static_cast<int>(std::round(sampleRate * 0.00035)));
        hardPostMargin = juce::jlimit(4, 64,
            static_cast<int>(std::round(sampleRate * 0.00070)));
        hardCooldown = juce::jlimit(32, 512,
            static_cast<int>(std::round(sampleRate * 0.004)));

        // Mouth-click lane (1.5–10 ms, stricter)
        mouthHpCutoffHz = 2200.0f;
        mouthMinSamples = juce::jlimit(4, 256,
            static_cast<int>(std::round(sampleRate * 0.0015)));
        mouthMaxSamples = juce::jlimit(32, 1024,
            static_cast<int>(std::round(sampleRate * 0.010)));   // 10 ms
        mouthPreMargin = juce::jlimit(4, 64,
            static_cast<int>(std::round(sampleRate * 0.001)));
        mouthPostMargin = juce::jlimit(8, 96,
            static_cast<int>(std::round(sampleRate * 0.002)));
        mouthCooldown = juce::jlimit(64, 1024,
            static_cast<int>(std::round(sampleRate * 0.008)));
    }

    // Envelope time constants
    hardFastCoeff  = std::exp(-1.0f / (fs * 0.00025f));  // 0.25 ms
    hardSlowCoeff  = std::exp(-1.0f / (fs * 0.0120f));   // 12 ms
    hardSlopeFast  = std::exp(-1.0f / (fs * 0.00025f));
    hardSlopeSlow  = std::exp(-1.0f / (fs * 0.0120f));

    mouthFastCoeff = std::exp(-1.0f / (fs * 0.00080f));  // 0.8 ms
    mouthSlowCoeff = std::exp(-1.0f / (fs * 0.0300f));   // 30 ms
}

void DeClickDsp::redesignFilters() {
    for (auto& c : channels) {
        if (c.ring.size() != static_cast<size_t>(ringSize))
            c.ring.assign(static_cast<size_t>(ringSize), 0.0f);

        c.hardHp = {};
        c.mouthHp = {};
        c.lowLp = {};
        c.hardFastEnv = c.hardSlowEnv = 0.0f;
        c.hardSlopeFast = c.hardSlopeSlow = 0.0f;
        c.hardPrevX = 0.0f;
        c.mouthFastEnv = c.mouthSlowEnv = 0.0f;
        c.lowEnv = c.fullEnv = 0.0f;
        c.hard = {};
        c.mouth = {};
        c.lastRepairStart = -1;
        c.lastRepairEnd = -1;
        designHighPass(sampleRate, hardHpCutoffHz,  0.707f, c.hardHp);
        designHighPass(sampleRate, mouthHpCutoffHz, 0.707f, c.mouthHp);
        designLowPass (sampleRate, lowLpCutoffHz,   0.707f, c.lowLp);
    }
}

// ─── Process ──────────────────────────────────────────────────────────────────

void DeClickDsp::process(juce::AudioBuffer<float>& buffer, const Params& params) {
    const float strength  = clamp01(params.strength);
    const float upstream  = clamp01(params.detectionIntensity);

    const int numCh = buffer.getNumChannels();
    const int n     = buffer.getNumSamples();

    // Repair blends (Speech mode keeps a guaranteed floor when enabled)
    float hardRepairBlend, mouthRepairBlend;
    if (mode == CleanupMode::Speech) {
        hardRepairBlend  = strength > 0.001f ? 0.35f + 0.65f * strength : 0.0f;
        mouthRepairBlend = strength > 0.001f ? 0.25f + 0.65f * strength : 0.0f;
    } else {
        hardRepairBlend  = strength;
        mouthRepairBlend = strength * 0.60f;
    }

    // Threshold sensitivity driven by upstream hint
    const float sensitivity = juce::jlimit(0.60f, 1.25f, 0.75f + 0.50f * upstream);

    // Hard-click score thresholds
    constexpr float kHardStartScore    = 0.70f;
    constexpr float kHardContinueScore = 0.35f;

    // Mouth-click score thresholds
    constexpr float kMouthStartScore    = 0.62f;
    constexpr float kMouthContinueScore = 0.30f;

    // Short-region threshold for simple vs cosine-blend repair
    const int mouthShortSamples = juce::jlimit(4, mouthMaxSamples,
        static_cast<int>(std::round(sampleRate * 0.008)));  // 8 ms boundary

    // Block-worth decay for metering: 100 ms time constant
    const float meterDecay = std::exp(-static_cast<float>(n) / (static_cast<float>(sampleRate) * 0.1f));
    const auto overlapsRecentRepair = [](const ChannelState& c, int64_t start, int64_t end) noexcept {
        if (c.lastRepairStart < 0 || c.lastRepairEnd < c.lastRepairStart)
            return false;
        return start <= c.lastRepairEnd + 8 && end >= c.lastRepairStart - 8;
    };

    for (int ch = 0; ch < std::min(numCh, static_cast<int>(channels.size())); ++ch) {
        auto& c   = channels[static_cast<size_t>(ch)];
        float* buf = buffer.getWritePointer(ch);

        for (int i = 0; i < n; ++i) {
            const float x = buf[i];
            const int64_t curAbs = c.absoluteIndex;

            setRingSample(c, curAbs, x);

            const bool repairEnabled = strength > 0.001f;
            if (!repairEnabled) {
                c.hard = {};
                c.mouth = {};
            }

            {
                // ── Low-band guard (protects against plosive-masking as click) ──
                const float lpOut  = processBiquad(x, c.lowLp);
                const float lpRect = std::abs(lpOut);
                const float xRect  = std::abs(x);
                c.lowEnv  = lpRect > c.lowEnv  ? 0.995f * c.lowEnv  + 0.005f * lpRect
                                               : 0.990f * c.lowEnv  + 0.010f * lpRect;
                c.fullEnv = xRect  > c.fullEnv ? 0.995f * c.fullEnv + 0.005f * xRect
                                               : 0.990f * c.fullEnv + 0.010f * xRect;
                const float lowBandRatio = c.lowEnv / std::max(c.fullEnv, 1.0e-6f);
                // Dominant low-band → probably a plosive or low-freq event, not a click
                const float lowGuardFactor = lowBandRatio > 0.55f ? 0.30f : 1.0f;

                // ── Hard-click lane ───────────────────────────────────────────
                const float hardHpOut  = processBiquad(x, c.hardHp);
                const float hardRect   = std::abs(hardHpOut);
                const float hardSlope  = std::abs(x - c.hardPrevX);
                c.hardPrevX = x;

                c.hardFastEnv   = hardCoeff(c.hardFastEnv,   hardRect, hardFastCoeff);
                c.hardSlowEnv   = hardCoeff(c.hardSlowEnv,   hardRect, hardSlowCoeff);
                c.hardSlopeFast = hardCoeff(c.hardSlopeFast, hardSlope, hardFastCoeff);
                c.hardSlopeSlow = hardCoeff(c.hardSlopeSlow, hardSlope, hardSlowCoeff);

                const float hardRR   = c.hardFastEnv   / std::max(c.hardSlowEnv,   1.0e-6f);
                const float hardSR   = c.hardSlopeFast / std::max(c.hardSlopeSlow, 1.0e-6f);

                std::array<float, 9> hardRawMedian;
                std::array<float, 9> hardAbsMedian;
                for (int k = 0; k < 9; ++k) {
                    const float s = getRingSample(c, curAbs - (8 - k));
                    hardRawMedian[static_cast<size_t>(k)] = s;
                    hardAbsMedian[static_cast<size_t>(k)] = std::abs(s);
                }
                const float hardMed    = median9(hardRawMedian);
                const float hardAbsMed = median9(hardAbsMedian);
                const float hardMR = std::abs(x - hardMed) / std::max(hardAbsMed, 1.0e-6f);

                float hardScore =
                      0.45f * smoothStep(2.2f / sensitivity, 5.0f / sensitivity, hardRR)
                    + 0.35f * smoothStep(2.5f / sensitivity, 6.0f / sensitivity, hardSR)
                    + 0.20f * smoothStep(2.5f / sensitivity, 7.0f / sensitivity, hardMR);
                hardScore = clamp01(hardScore * lowGuardFactor);

                if (repairEnabled) {
                    processLane(c.hard, curAbs, hardScore, kHardStartScore, kHardContinueScore,
                                hardMaxSamples, 2, hardCooldown);
                }

                // ── Mouth-click lane ──────────────────────────────────────────
                const float mouthHpOut = processBiquad(x, c.mouthHp);
                const float mouthRect  = std::abs(mouthHpOut);

                c.mouthFastEnv = hardCoeff(c.mouthFastEnv, mouthRect, mouthFastCoeff);
                c.mouthSlowEnv = hardCoeff(c.mouthSlowEnv, mouthRect, mouthSlowCoeff);

                const float mouthRR = c.mouthFastEnv / std::max(c.mouthSlowEnv, 1.0e-6f);

                // Roughness: short windowed deviation from median
                std::array<float, 9> mouthMedian;
                for (int k = 0; k < 9; ++k)
                    mouthMedian[static_cast<size_t>(k)] = std::abs(getRingSample(c, curAbs - (8 - k)));
                const float mouthAbsMed = median9(mouthMedian);
                const float mouthMR = mouthRect / std::max(mouthAbsMed, 1.0e-6f);

                float mouthScore =
                      0.55f * smoothStep(1.6f / sensitivity, 4.5f / sensitivity, mouthRR)
                    + 0.45f * smoothStep(1.5f / sensitivity, 4.0f / sensitivity, mouthMR);
                mouthScore = clamp01(mouthScore * lowGuardFactor);

                if (repairEnabled) {
                    processLane(c.mouth, curAbs, mouthScore, kMouthStartScore, kMouthContinueScore,
                                mouthMaxSamples, mouthMinSamples, mouthCooldown);
                }

                // ── Execute pending repairs ───────────────────────────────────
                // Fire only after the post-margin samples have been written so the
                // post anchors read real audio instead of stale ring-buffer data.
                if (repairEnabled && c.hard.pendingRepair && curAbs >= c.hard.pendingEnd + hardPostMargin + 2) {
                    const int64_t repairStart = c.hard.pendingStart - hardPreMargin;
                    const int64_t repairEnd   = c.hard.pendingEnd   + hardPostMargin;
                    const int64_t oldestSafe  = curAbs - lookaheadSamples;
                    if (repairStart > oldestSafe && !overlapsRecentRepair(c, repairStart, repairEnd)) {
                        if (repairRegion(c, repairStart, repairEnd, hardRepairBlend, false)) {
                            c.lastRepairStart = repairStart;
                            c.lastRepairEnd = repairEnd;
                            lastHardClickRepair = 1.0f;
                            ++recentRepairCount;
                        }
                    }
                    c.hard.pendingRepair = false;
                }

                if (repairEnabled && c.mouth.pendingRepair && curAbs >= c.mouth.pendingEnd + mouthPostMargin + 2) {
                    const int64_t repairStart = c.mouth.pendingStart - mouthPreMargin;
                    const int64_t repairEnd   = c.mouth.pendingEnd   + mouthPostMargin;
                    const int64_t span        = repairEnd - repairStart + 1;
                    const bool longRegion     = span > mouthShortSamples;
                    const int64_t oldestSafe  = curAbs - lookaheadSamples;
                    if (repairStart > oldestSafe && !overlapsRecentRepair(c, repairStart, repairEnd)) {
                        if (repairRegion(c, repairStart, repairEnd, mouthRepairBlend, longRegion)) {
                            c.lastRepairStart = repairStart;
                            c.lastRepairEnd = repairEnd;
                            lastMouthClickRepair = 1.0f;
                            ++recentRepairCount;
                        }
                    }
                    c.mouth.pendingRepair = false;
                }
            }

            // Output lookahead-delayed audio (repaired in-place before output)
            const int64_t outAbs = curAbs - lookaheadSamples;
            buf[i] = getRingSample(c, outAbs);

            ++c.absoluteIndex;
        }
    }

    // Decay metering values
    lastHardClickRepair  *= meterDecay;
    lastMouthClickRepair *= meterDecay;
}

// ─── Lane state machine helper ────────────────────────────────────────────────

void DeClickDsp::processLane(LaneState& lane, int64_t curAbs, float score,
                              float startThresh, float continueThresh,
                              int maxSamples, int minSamples, int cooldownSamp) noexcept {
    if (lane.cooldown > 0) {
        --lane.cooldown;
        return;
    }

    if (!lane.candidateActive) {
        if (score > startThresh) {
            lane.candidateActive = true;
            lane.candidateStart  = curAbs;
            lane.candidateLength = 1;
        }
    } else {
        if (score > continueThresh) {
            ++lane.candidateLength;

            if (lane.candidateLength > maxSamples) {
                lane.candidateActive = false;
                lane.candidateStart  = -1;
                lane.candidateLength = 0;
                lane.cooldown        = cooldownSamp;
            }
        } else {
            const int len = lane.candidateLength;

            if (len >= minSamples && len <= maxSamples && !lane.pendingRepair) {
                lane.pendingRepair = true;
                lane.pendingStart  = lane.candidateStart;
                lane.pendingEnd    = curAbs - 1;
                lane.cooldown      = cooldownSamp;
            }

            lane.candidateActive = false;
            lane.candidateStart  = -1;
            lane.candidateLength = 0;
        }
    }
}

} // namespace speech_clarity
} // namespace vxsuite
