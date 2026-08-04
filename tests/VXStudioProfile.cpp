#include "../Source/vxstudio/products/deepfilternet/VxDeepFilterNetProcessor.h"
#include "../Source/vxstudio/products/denoiser/VxDenoiserProcessor.h"
#include "../Source/vxstudio/products/deverb/VxDeverbProcessor.h"
#include "../Source/vxstudio/products/finish/VxFinishProcessor.h"
#include "../Source/vxstudio/products/leveler/VxLevelerProcessor.h"
#include "../Source/vxstudio/products/OptoComp/VxOptoCompProcessor.h"
#include "../Source/vxstudio/products/proximity/VxProximityProcessor.h"
#include "../Source/vxstudio/products/ProximityClassic/VxProximityClassicProcessor.h"
#include "../Source/vxstudio/products/rebalance/VxRebalanceProcessor.h"
#include "../Source/vxstudio/products/repair/VxRepairProcessor.h"
#include "../Source/vxstudio/products/speech_clarity/VxSpeechClarityProcessor.h"
#include "../Source/vxstudio/products/subtract/VxSubtractProcessor.h"
#include "../Source/vxstudio/products/tone/VxToneProcessor.h"
#include "../Source/vxstudio/products/tone_refine/VxToneRefineProcessor.h"
#include "../Source/vxstudio/products/tune/VxTuneProcessor.h"
#include "../Source/vxstudio/products/analyser/VXStudioAnalyserProcessor.h"
#include "VxStudioProcessorTestUtils.h"

#include <chrono>
#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace vxsuite::test;

struct ProfileResult {
    std::string name;
    double sampleRate = 0.0;
    int blockSize = 0;
    double realtimeFactor = 0.0;
};

std::vector<ProfileResult> allResults;

template <typename Processor>
double profileProcessor(Processor& processor,
                        const juce::AudioBuffer<float>& input,
                        const int blockSize,
                        const int passes) {
    juce::MidiBuffer midi;
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

void recordProfileRow(const char* label,
                      const double milliseconds,
                      const double secondsAudio,
                      const double sampleRate,
                      const int blockSize) {
    const double realtimeFactor = secondsAudio > 0.0 ? (milliseconds / 1000.0) / secondsAudio : 0.0;
    std::cout << "  " << label << ": " << milliseconds << " ms total, x" << realtimeFactor << " realtime\n";
    allResults.push_back({ label, sampleRate, blockSize, realtimeFactor });
}

// Constructs, prepares, configures, and profiles one processor.
template <typename Processor, typename Setup>
void runProfile(const char* label,
                const juce::AudioBuffer<float>& input,
                const double sr,
                const int blockSize,
                const int passes,
                Setup&& setup) {
    Processor processor;
    processor.prepareToPlay(sr, blockSize);
    setup(processor);
    const double seconds = passes * (input.getNumSamples() / sr);
    recordProfileRow(label, profileProcessor(processor, input, blockSize, passes), seconds, sr, blockSize);
}

} // namespace

int main() {
    const juce::ScopedJuceInitialiser_GUI juceInit;

    constexpr std::array<double, 2> sampleRates { 48000.0, 96000.0 };
    constexpr std::array<int, 3> blockSizes { 64, 256, 512 };
    constexpr int passes = 12;

    for (const double sr : sampleRates) {
        auto speech = makeSpeechLike(sr, 1.0f);
        auto noisy = addBuffers(speech, makeNoise(sr, 1.0f, 0.06f));
        std::cout << "\nSample rate: " << sr << "\n";
        for (const int blockSize : blockSizes) {
            std::cout << "Block size: " << blockSize << "\n";

            runProfile<VXProximityAudioProcessor>("Proximity", speech, sr, blockSize, passes, [](auto& p) {
                setParamNormalized(p, "closer", 0.24f);
                setParamNormalized(p, "air", 0.18f);
            });

            runProfile<VXProximityClassicAudioProcessor>("ProximityClassic", speech, sr, blockSize, passes, [](auto& p) {
                setParamNormalized(p, "closer", 0.24f);
                setParamNormalized(p, "air", 0.18f);
            });

            runProfile<VXToneAudioProcessor>("Tone", speech, sr, blockSize, passes, [](auto& p) {
                setParamNormalized(p, "bass", 0.68f);
                setParamNormalized(p, "mid", 0.42f);
                setParamNormalized(p, "treble", 0.64f);
            });

            runProfile<VXToneRefineAudioProcessor>("ToneRefine", speech, sr, blockSize, passes, [](auto& p) {
                setParamNormalized(p, "mud", 0.60f);
                setParamNormalized(p, "harshness", 0.60f);
                setParamNormalized(p, "smooth", 0.50f);
            });

            runProfile<VXFinishAudioProcessor>("Finish", speech, sr, blockSize, passes, [](auto& p) {
                setParamNormalized(p, "finish", 0.44f);
                setParamNormalized(p, "body", 0.30f);
                setParamNormalized(p, "gain", 0.32f);
            });

            runProfile<VXOptoCompAudioProcessor>("OptoComp", speech, sr, blockSize, passes, [](auto& p) {
                setParamNormalized(p, "peak_reduction", 0.60f);
                setParamNormalized(p, "gain", 0.55f);
            });

            runProfile<VXSpeechClarityAudioProcessor>("SpeechClarity", speech, sr, blockSize, passes, [](auto& p) {
                setParamNormalized(p, "sibilance", 0.55f);
                setParamNormalized(p, "plosive", 0.55f);
                setParamNormalized(p, "breath", 0.55f);
                setParamNormalized(p, "click", 0.55f);
            });

            runProfile<VXLevelerAudioProcessor>("Leveler", speech, sr, blockSize, passes, [](auto& p) {
                setParamNormalized(p, "level", 0.60f);
                setParamNormalized(p, "control", 0.50f);
            });

            runProfile<VXTuneAudioProcessor>("Tune", speech, sr, blockSize, passes, [](auto& p) {
                setParamNormalized(p, "amount", 0.60f);
                setParamNormalized(p, "natural", 0.50f);
            });

            runProfile<VXDenoiserAudioProcessor>("Denoiser", noisy, sr, blockSize, passes, [](auto& p) {
                setParamNormalized(p, "clean", 0.60f);
                setParamNormalized(p, "guard", 0.50f);
            });

            // Vocal mode engages the RLS-WPE stage — the heaviest deverb path.
            runProfile<VXDeverbAudioProcessor>("Deverb+WPE", noisy, sr, blockSize, passes, [](auto& p) {
                setParamNormalized(p, "reduce", 0.70f);
                setParamNormalized(p, "mode", 0.0f);
            });

            runProfile<VXDeepFilterNetAudioProcessor>("DeepFilterNet", noisy, sr, blockSize, passes, [](auto& p) {
                setParamNormalized(p, "clean", 0.60f);
                setParamNormalized(p, "guard", 0.40f);
            });

            runProfile<VXRebalanceAudioProcessor>("Rebalance", noisy, sr, blockSize, passes, [](auto& p) {
                setParamNormalized(p, "vocals", 0.80f);
                setParamNormalized(p, "drums", 0.30f);
                setParamNormalized(p, "strength", 0.70f);
            });

            runProfile<VXRepairAudioProcessor>("Repair", noisy, sr, blockSize, passes, [](auto& p) {
                setParamNormalized(p, "noise_on", 1.0f);
                setParamNormalized(p, "reverb_on", 1.0f);
                setParamNormalized(p, "clarity_on", 1.0f);
                setParamNormalized(p, "click_on", 1.0f);
                setParamNormalized(p, "noise_strength", 0.60f);
                setParamNormalized(p, "reverb_strength", 0.60f);
                setParamNormalized(p, "clarity_strength", 0.60f);
                setParamNormalized(p, "click_strength", 0.60f);
            });

            runProfile<VXStudioAnalyserAudioProcessor>("Analyser", speech, sr, blockSize, passes, [](auto&) {});

            {
                VXSubtractAudioProcessor subtract;
                subtract.prepareToPlay(sr, blockSize);
                if (primeSubtractLearn(subtract, sr)) {
                    setParamNormalized(subtract, "subtract", 0.78f);
                    setParamNormalized(subtract, "protect", 0.46f);
                    const double seconds = passes * (noisy.getNumSamples() / sr);
                    recordProfileRow("Subtract", profileProcessor(subtract, noisy, blockSize, passes), seconds, sr, blockSize);
                } else {
                    std::cout << "  Subtract: skipped (learn profile unavailable)\n";
                }
            }
        }
    }

#if defined(NDEBUG)
    // CI regression threshold: no single plugin may exceed this fraction of a
    // core at 48 kHz / 512-sample blocks in an optimized build. Generous on
    // purpose — it exists to catch order-of-magnitude regressions, not tuning
    // noise across machines.
    constexpr double kMaxRealtimeFactor48k512 = 0.50;
    bool thresholdBreached = false;
    for (const auto& r : allResults) {
        if (r.sampleRate == 48000.0 && r.blockSize == 512 && r.realtimeFactor > kMaxRealtimeFactor48k512) {
            std::cout << "THRESHOLD BREACH: " << r.name << " at x" << r.realtimeFactor
                      << " realtime (limit x" << kMaxRealtimeFactor48k512 << ")\n";
            thresholdBreached = true;
        }
    }
    if (thresholdBreached)
        return 1;
    std::cout << "\nAll plugins within x" << kMaxRealtimeFactor48k512 << " realtime at 48k/512.\n";
#else
    std::cout << "\n(unoptimized build - CI threshold check skipped)\n";
#endif

    return 0;
}
