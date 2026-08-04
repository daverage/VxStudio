// VX Tune plugin-level tests: Listen delta contract, latency reporting, and
// the pitch-trace feed. Listen must play only the changes being made:
// non-silent while correction is active, silent for an in-tune singer.

#include "../Source/vxstudio/products/tune/VxTuneProcessor.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(const bool condition, const std::string& label) {
    std::printf("  %s  %s\n", condition ? "PASS" : "FAIL", label.c_str());
    if (!condition)
        ++failures;
}

constexpr double kSampleRate = 48000.0;
constexpr int kBlock = 512;
constexpr float kTwoPi = 6.28318530717958647692f;

void setParam(VXTuneAudioProcessor& p, const char* id, const float normalised) {
    if (auto* param = p.getValueTreeState().getParameter(id))
        param->setValueNotifyingHost(normalised);
}

// Feeds a tone for `seconds` and returns the output RMS over the final
// `measureSeconds` of that span.
float runToneAndMeasure(VXTuneAudioProcessor& p, const float hz,
                        const float seconds, const float measureSeconds,
                        double& phase) {
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> block(2, kBlock);
    const int totalBlocks = static_cast<int>(seconds * kSampleRate / kBlock);
    const int measureFromBlock = totalBlocks
        - static_cast<int>(measureSeconds * kSampleRate / kBlock);
    double sumSquares = 0.0;
    long counted = 0;
    for (int b = 0; b < totalBlocks; ++b) {
        for (int i = 0; i < kBlock; ++i) {
            phase += kTwoPi * hz / kSampleRate;
            float s = 0.0f;
            for (int h = 1; h <= 3; ++h)
                s += std::sin(static_cast<float>(phase) * h) / static_cast<float>(h);
            block.setSample(0, i, 0.25f * s);
            block.setSample(1, i, 0.25f * s);
        }
        p.processBlock(block, midi);
        if (b >= measureFromBlock) {
            for (int i = 0; i < kBlock; ++i) {
                const float v = block.getSample(0, i);
                sumSquares += v * v;
                ++counted;
            }
        }
    }
    return counted > 0 ? static_cast<float>(std::sqrt(sumSquares / counted)) : 0.0f;
}

void testLatencyReported() {
    std::printf("Latency reporting:\n");
    VXTuneAudioProcessor p;
    p.prepareToPlay(kSampleRate, kBlock);
    check(p.getLatencySamples() == 600,   // ceil(48000 / 80), PSOLA budget
          "reports shifter latency (got " + std::to_string(p.getLatencySamples()) + ")");
}

void testListenPlaysChangesOnly() {
    std::printf("Listen plays the changes being made:\n");
    const float sharpHz = 220.0f * std::exp2(30.0f / 1200.0f);

    // Sharp singer, correction active: listen delta must be clearly audible.
    {
        VXTuneAudioProcessor p;
        p.prepareToPlay(kSampleRate, kBlock);
        setParam(p, "amount", 1.0f);
        setParam(p, "natural", 0.5f);
        double phase = 0.0;
        const float wetRms = runToneAndMeasure(p, sharpHz, 2.0f, 0.5f, phase);
        setParam(p, "listen", 1.0f);
        const float listenRms = runToneAndMeasure(p, sharpHz, 1.0f, 0.5f, phase);
        check(wetRms > 0.05f, "wet path carries signal (rms "
                                  + std::to_string(wetRms) + ")");
        check(listenRms > 0.02f,
              "listen is non-silent while correcting (rms "
                  + std::to_string(listenRms) + ")");
    }

    // In-tune singer, no correction: listen delta must be near silence.
    {
        VXTuneAudioProcessor p;
        p.prepareToPlay(kSampleRate, kBlock);
        setParam(p, "amount", 1.0f);
        setParam(p, "natural", 0.5f);
        double phase = 0.0;
        const float wetRms = runToneAndMeasure(p, 220.0f, 2.0f, 0.5f, phase);
        setParam(p, "listen", 1.0f);
        const float listenRms = runToneAndMeasure(p, 220.0f, 1.0f, 0.5f, phase);
        const float relDb = 20.0f * std::log10(std::max(listenRms, 1.0e-9f)
                                               / std::max(wetRms, 1.0e-9f));
        check(relDb < -40.0f,
              "listen is near-silent when not intervening ("
                  + std::to_string(relDb) + " dB rel wet)");
    }
}

void testPitchTraceFeed() {
    std::printf("Pitch trace feed:\n");
    const float sharpHz = 220.0f * std::exp2(30.0f / 1200.0f);
    VXTuneAudioProcessor p;
    p.prepareToPlay(kSampleRate, kBlock);
    setParam(p, "amount", 1.0f);
    setParam(p, "natural", 0.5f);
    double phase = 0.0;
    runToneAndMeasure(p, sharpHz, 2.0f, 0.1f, phase);

    check(p.getPitchTraceConfidence() > 0.8f, "voiced input yields confident trace");
    const float detected = p.getPitchTraceDetectedCents();
    const float corrected = p.getPitchTraceCorrectedCents();
    // A3 = -1200c vs A440; input +30c sharp, corrected back toward the note.
    check(std::abs(detected - (-1170.0f)) < 12.0f,
          "detected trace near A3+30c (got " + std::to_string(detected) + "c)");
    check(std::abs(corrected - (-1200.0f)) < 12.0f,
          "corrected trace near A3 (got " + std::to_string(corrected) + "c)");

    VXTuneAudioProcessor silent;
    silent.prepareToPlay(kSampleRate, kBlock);
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> block(2, kBlock);
    block.clear();
    for (int b = 0; b < 40; ++b)
        silent.processBlock(block, midi);
    check(silent.getPitchTraceConfidence() <= 0.0f, "silence yields zero-confidence trace");
}

} // namespace

int main() {
    std::printf("VX Tune plugin tests\n");
    testLatencyReported();
    testListenPlaysChangesOnly();
    testPitchTraceFeed();
    if (failures == 0) {
        std::printf("All VX Tune plugin tests passed\n");
        return 0;
    }
    std::printf("%d VX Tune plugin test(s) FAILED\n", failures);
    return 1;
}
