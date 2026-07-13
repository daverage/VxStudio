#include "../Source/vxstudio/products/rebalance/ai/VxRealtimeStemSplitter.h"
#include "../Source/vxstudio/products/rebalance/ai/VxAiStemRebalanceDsp.h"

#include <array>
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

double calculateRms(const juce::AudioBuffer<float>& buffer) {
    double sum = 0.0;
    int count = 0;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample) {
            const double value = buffer.getSample(ch, sample);
            sum += value * value;
            ++count;
        }
    }

    return count > 0 ? std::sqrt(sum / static_cast<double>(count)) : 0.0;
}

double calculateRmsDifference(const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b) {
    const int channels = std::min(a.getNumChannels(), b.getNumChannels());
    const int samples = std::min(a.getNumSamples(), b.getNumSamples());
    double sum = 0.0;
    int count = 0;
    for (int ch = 0; ch < channels; ++ch) {
        for (int sample = 0; sample < samples; ++sample) {
            const double delta = static_cast<double>(a.getSample(ch, sample)) - static_cast<double>(b.getSample(ch, sample));
            sum += delta * delta;
            ++count;
        }
    }

    return count > 0 ? std::sqrt(sum / static_cast<double>(count)) : 0.0;
}

juce::AudioBuffer<float> renderStemFrame(vxsuite::rebalance::StemFrame stemFrame,
                                         const double sampleRate,
                                         const std::array<float, vxsuite::rebalance::kControlCount>& targets) {
    vxsuite::rebalance::ai::StemRebalanceDsp mixer;
    mixer.prepare(sampleRate, vxsuite::rebalance::kStemFrameSamples, vxsuite::rebalance::kStemFrameChannels);
    mixer.setRecordingType(vxsuite::rebalance::RecordingType::phoneRough);
    mixer.setControlTargets(targets);

    juce::AudioBuffer<float> output(vxsuite::rebalance::kStemFrameChannels, vxsuite::rebalance::kStemFrameSamples);
    for (std::uint64_t sequence = 1; sequence <= 16; ++sequence) {
        stemFrame.sequenceNumber = sequence;
        mixer.setStemFrame(stemFrame);
        output.clear();
        mixer.process(output);
    }

    return output;
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

    vxsuite::rebalance::AiMaskFrame frame;
    vxsuite::rebalance::StemFrame stemFrame;
    constexpr int blockSize = vxsuite::rebalance::ai::RealtimeStemSplitter::kOutputChunkSize;
    for (int block = 0; block < 5; ++block) {
        auto input = makeSyntheticBlock(block * blockSize, blockSize, sampleRate);
        (void) splitter.processBlock(input, frame, stemFrame);
        std::this_thread::sleep_for(std::chrono::milliseconds(12));
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(12);
    juce::AudioBuffer<float> pollBuffer(2, 0);
    while (std::chrono::steady_clock::now() < deadline) {
        if (splitter.processBlock(pollBuffer, frame, stemFrame) && frame.available && stemFrame.available)
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

    if (!frame.available || !stemFrame.available || !debug.hasFrame || debug.completedFrames == 0) {
        std::cerr << "[VxRebalanceAISmokeTest] No AI stem/mask frame produced at "
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

    std::array<double, vxsuite::rebalance::kSourceCount> stemEnergy {};
    for (int source = 0; source < vxsuite::rebalance::kSourceCount; ++source) {
        for (int ch = 0; ch < vxsuite::rebalance::kStemFrameChannels; ++ch) {
            for (int sample = 0; sample < vxsuite::rebalance::kStemFrameSamples; ++sample) {
                const float value = stemFrame.stems[static_cast<size_t>(source)][static_cast<size_t>(ch)][static_cast<size_t>(sample)];
                stemEnergy[static_cast<size_t>(source)] += static_cast<double>(value) * static_cast<double>(value);
            }
        }
    }

    if (stemFrame.sequenceNumber == 0 || stemFrame.confidence <= 0.0f) {
        std::cerr << "[VxRebalanceAISmokeTest] Invalid stem frame metadata at "
                  << sampleRate << " Hz\n";
        return false;
    }

    if (stemEnergy[static_cast<size_t>(vxsuite::rebalance::vocalsSource)] <= 1.0e-9
        || stemEnergy[static_cast<size_t>(vxsuite::rebalance::bassSource)] <= 1.0e-9
        || stemEnergy[static_cast<size_t>(vxsuite::rebalance::guitarSource)] <= 1.0e-12
        || stemEnergy[static_cast<size_t>(vxsuite::rebalance::otherSource)] <= 1.0e-12) {
        std::cerr << "[VxRebalanceAISmokeTest] Stem frame missing expected source energy at "
                  << sampleRate << " Hz\n";
        return false;
    }

    std::array<float, vxsuite::rebalance::kControlCount> neutralTargets {
        0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 1.0f
    };
    auto isolateGuitarTargets = neutralTargets;
    isolateGuitarTargets[static_cast<size_t>(vxsuite::rebalance::vocalsSource)] = 0.0f;
    isolateGuitarTargets[static_cast<size_t>(vxsuite::rebalance::drumsSource)] = 0.0f;
    isolateGuitarTargets[static_cast<size_t>(vxsuite::rebalance::bassSource)] = 0.0f;
    isolateGuitarTargets[static_cast<size_t>(vxsuite::rebalance::guitarSource)] = 1.0f;

    stemFrame.confidence = 1.0f;
    const auto neutralRender = renderStemFrame(stemFrame, sampleRate, neutralTargets);
    const auto guitarRender = renderStemFrame(stemFrame, sampleRate, isolateGuitarTargets);
    const double neutralRms = calculateRms(neutralRender);
    const double deltaRms = calculateRmsDifference(neutralRender, guitarRender);

    if (neutralRms <= 1.0e-8 || deltaRms <= neutralRms * 0.035) {
        std::cerr << "[VxRebalanceAISmokeTest] AI stem mixer produced no meaningful audible change at "
                  << sampleRate << " Hz, neutralRms=" << neutralRms
                  << " deltaRms=" << deltaRms << "\n";
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
