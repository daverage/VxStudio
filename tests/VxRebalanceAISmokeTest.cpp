#include "../Source/vxstudio/products/rebalance/ai/VxRealtimeStemSplitter.h"

#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>

namespace {

juce::AudioBuffer<float> makeSyntheticBlock(const int sampleOffset, const int blockSize, const double sampleRate) {
    juce::AudioBuffer<float> buffer(2, blockSize);

    for (int i = 0; i < blockSize; ++i) {
        const auto t = static_cast<double>(sampleOffset + i) / sampleRate;
        const auto vocalLike = 0.16f * std::sin(static_cast<float>(2.0 * juce::MathConstants<double>::pi * 220.0 * t));
        const auto bassLike = 0.08f * std::sin(static_cast<float>(2.0 * juce::MathConstants<double>::pi * 82.0 * t));
        const auto brightLike = 0.035f * std::sin(static_cast<float>(2.0 * juce::MathConstants<double>::pi * 2200.0 * t));
        const auto clickLike = (sampleOffset + i) % 4096 < 32 ? 0.11f : 0.0f;
        const auto left = vocalLike + bassLike + brightLike + clickLike;
        const auto right = 0.9f * vocalLike + 0.7f * bassLike - brightLike + clickLike;
        buffer.setSample(0, i, left);
        buffer.setSample(1, i, right);
    }

    return buffer;
}

bool runSplitterAtSampleRate(const double sampleRate) {
    vxsuite::rebalance::ai::RealtimeStemSplitter splitter;
    splitter.prepare(sampleRate,
                     vxsuite::rebalance::ai::RealtimeStemSplitter::kOutputChunkSize,
                     2);

    if (!splitter.isAvailable()) {
        std::cerr << "[VxRebalanceAISmokeTest] Splitter unavailable at "
                  << sampleRate << " Hz: " << splitter.statusText() << "\n";
        return false;
    }

    vxsuite::rebalance::Dsp::AiMaskFrame frame;
    constexpr int blockSize = vxsuite::rebalance::ai::RealtimeStemSplitter::kOutputChunkSize;
    for (int block = 0; block < 5; ++block) {
        auto input = makeSyntheticBlock(block * blockSize, blockSize, sampleRate);
        (void) splitter.processBlock(input, frame);
        std::this_thread::sleep_for(std::chrono::milliseconds(12));
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(12);
    juce::AudioBuffer<float> pollBuffer(2, 0);
    while (std::chrono::steady_clock::now() < deadline) {
        if (splitter.processBlock(pollBuffer, frame) && frame.available)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    const auto debug = splitter.getDebugSnapshot();
    const auto statusText = splitter.statusText();
    std::cout << "[VxRebalanceAISmokeTest] sampleRate=" << sampleRate
              << " provider=" << splitter.statusText()
              << " submitted=" << debug.submittedFrames
              << " completed=" << debug.completedFrames
              << " failed=" << debug.failedFrames
              << " dropped=" << debug.droppedFrames
              << " confidence=" << debug.latestConfidence << "\n";

    if (!frame.available || !debug.hasFrame || debug.completedFrames == 0) {
        std::cerr << "[VxRebalanceAISmokeTest] No AI mask frame produced at "
                  << sampleRate << " Hz\n";
        return false;
    }

    if (debug.failedFrames > 0) {
        std::cerr << "[VxRebalanceAISmokeTest] Inference failures detected at "
                  << sampleRate << " Hz\n";
        return false;
    }

    if (frame.confidence <= 0.0f) {
        std::cerr << "[VxRebalanceAISmokeTest] Invalid confidence at "
                  << sampleRate << " Hz\n";
        return false;
    }

    const auto requireGpu = juce::SystemStats::getEnvironmentVariable("VXSTUDIO_REBALANCE_AI_REQUIRE_GPU", "0")
        .trim()
        .toLowerCase();
    if ((requireGpu == "1" || requireGpu == "true" || requireGpu == "gpu")
        && statusText.containsIgnoreCase("(CPU,")) {
        std::cerr << "[VxRebalanceAISmokeTest] GPU provider required but splitter used CPU\n";
        return false;
    }

    return true;
}

} // namespace

int main() {
    if (!runSplitterAtSampleRate(vxsuite::rebalance::ai::RealtimeStemSplitter::kModelSampleRate))
        return 1;

    if (!runSplitterAtSampleRate(48000.0))
        return 1;

    if (!runSplitterAtSampleRate(96000.0))
        return 1;

    return 0;
}
