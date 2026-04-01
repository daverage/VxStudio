#include "../Source/vxsuite/products/cleanup/VxCleanupProcessor.h"
#include "../Source/vxsuite/products/finish/VxFinishProcessor.h"
#include "../Source/vxsuite/products/proximity/VxProximityProcessor.h"
#include "../Source/vxsuite/products/subtract/VxSubtractProcessor.h"
#include "VxStudioProcessorTestUtils.h"

#include <chrono>
#include <iostream>

namespace {

using namespace vxsuite::test;

template <typename Processor>
double profileProcessor(Processor& processor,
                        const juce::AudioBuffer<float>& input,
                        const int blockSize,
                        const int passes) {
    juce::MidiBuffer midi;
    auto block = input;
    const auto start = std::chrono::steady_clock::now();
    for (int pass = 0; pass < passes; ++pass) {
        processor.reset();
        for (int offset = 0; offset < input.getNumSamples(); offset += blockSize) {
            const int num = std::min(blockSize, input.getNumSamples() - offset);
            juce::AudioBuffer<float> chunk(input.getNumChannels(), num);
            for (int ch = 0; ch < input.getNumChannels(); ++ch)
                chunk.copyFrom(ch, 0, input, ch, offset, num);
            processor.processBlock(chunk, midi);
        }
    }
    const auto end = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count();
}

bool primeSubtractLearn(VXSubtractAudioProcessor& processor, double sr) {
    juce::AudioBuffer<float> warmup(2, 256);
    warmup.clear();
    setParamNormalized(processor, "learn", 0.0f);
    processSingleBlock(processor, warmup);

    auto noise = makeNoise(sr, 0.8f, 0.10f);
    setParamNormalized(processor, "learn", 1.0f);
    juce::MidiBuffer midi;
    constexpr int blockSize = 256;
    for (int start = 0; start < noise.getNumSamples(); start += blockSize) {
        const int num = std::min(blockSize, noise.getNumSamples() - start);
        juce::AudioBuffer<float> block(2, num);
        for (int ch = 0; ch < 2; ++ch)
            block.copyFrom(ch, 0, noise, ch, start, num);
        processor.processBlock(block, midi);
    }
    setParamNormalized(processor, "learn", 0.0f);
    juce::AudioBuffer<float> stopBlock(2, 256);
    stopBlock.clear();
    processSingleBlock(processor, stopBlock);
    return processor.isLearnReady();
}

void printProfileRow(const char* label, const double milliseconds, const int passes, const double secondsAudio) {
    const double realtimeFactor = secondsAudio > 0.0 ? (milliseconds / 1000.0) / secondsAudio : 0.0;
    std::cout << label << ": " << milliseconds << " ms total, x" << realtimeFactor << " realtime\n";
}

} // namespace

int main() {
    constexpr std::array<double, 3> sampleRates { 44100.0, 48000.0, 96000.0 };
    constexpr std::array<int, 4> blockSizes { 64, 128, 256, 512 };
    constexpr int passes = 12;

    for (const double sr : sampleRates) {
        auto speech = makeSpeechLike(sr, 1.0f);
        auto noisy = addBuffers(speech, makeNoise(sr, 1.0f, 0.06f));
        std::cout << "\nSample rate: " << sr << "\n";
        for (const int blockSize : blockSizes) {
            std::cout << "Block size: " << blockSize << "\n";

            VXCleanupAudioProcessor cleanup;
            cleanup.prepareToPlay(sr, blockSize);
            setParamNormalized(cleanup, "cleanup", 0.58f);
            setParamNormalized(cleanup, "body", 0.42f);
            setParamNormalized(cleanup, "focus", 0.57f);
            printProfileRow("  Cleanup", profileProcessor(cleanup, noisy, blockSize, passes), passes, passes * 1.0);

            VXProximityAudioProcessor proximity;
            proximity.prepareToPlay(sr, blockSize);
            setParamNormalized(proximity, "closer", 0.24f);
            setParamNormalized(proximity, "air", 0.18f);
            printProfileRow("  Proximity", profileProcessor(proximity, speech, blockSize, passes), passes, passes * 1.0);

            VXFinishAudioProcessor finish;
            finish.prepareToPlay(sr, blockSize);
            setParamNormalized(finish, "finish", 0.44f);
            setParamNormalized(finish, "body", 0.30f);
            setParamNormalized(finish, "gain", 0.32f);
            printProfileRow("  Finish", profileProcessor(finish, speech, blockSize, passes), passes, passes * 1.0);

            VXSubtractAudioProcessor subtract;
            subtract.prepareToPlay(sr, blockSize);
            const bool subtractReady = primeSubtractLearn(subtract, sr);
            if (subtractReady) {
                setParamNormalized(subtract, "subtract", 0.78f);
                setParamNormalized(subtract, "protect", 0.46f);
                printProfileRow("  Subtract", profileProcessor(subtract, noisy, blockSize, passes), passes, passes * 1.0);
            } else {
                std::cout << "  Subtract: skipped (learn profile unavailable)\n";
            }

            if (subtractReady) {
                const auto chainStart = std::chrono::steady_clock::now();
                for (int pass = 0; pass < passes; ++pass) {
                    auto afterSubtract = render(subtract, noisy, blockSize);
                    auto afterCleanup = render(cleanup, afterSubtract, blockSize);
                    auto afterProximity = render(proximity, afterCleanup, blockSize);
                    auto finalOut = render(finish, afterProximity, blockSize);
                    juce::ignoreUnused(finalOut);
                }
                const auto chainEnd = std::chrono::steady_clock::now();
                printProfileRow("  Full chain",
                                std::chrono::duration<double, std::milli>(chainEnd - chainStart).count(),
                                passes,
                                passes * 1.0);
            }
        }
    }

    return 0;
}
