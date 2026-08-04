#include "../Source/vxstudio/products/analyser/VXStudioAnalyserProcessor.h"
#include "../Source/vxstudio/products/deepfilternet/VxDeepFilterNetProcessor.h"
#include "../Source/vxstudio/products/deverb/VxDeverbProcessor.h"
#include "../Source/vxstudio/products/denoiser/VxDenoiserProcessor.h"
#include "../Source/vxstudio/products/OptoComp/VxOptoCompProcessor.h"
#include "../Source/vxstudio/products/leveler/VxLevelerProcessor.h"
#include "../Source/vxstudio/products/finish/VxFinishProcessor.h"
#include "../Source/vxstudio/products/proximity/VxProximityProcessor.h"
#include "../Source/vxstudio/products/rebalance/VxRebalanceProcessor.h"
#include "../Source/vxstudio/products/speech_clarity/VxSpeechClarityProcessor.h"
#include "../Source/vxstudio/products/speech_clarity/dsp/VxDeClickDsp.h"
#include "../Source/vxstudio/products/subtract/VxSubtractProcessor.h"
#include "../Source/vxstudio/products/tone/VxToneProcessor.h"
#include "../Source/vxstudio/products/tone_refine/VxToneRefineProcessor.h"
#include "../Source/vxstudio/products/ProximityClassic/VxProximityClassicProcessor.h"
#include "../Source/vxstudio/products/repair/VxRepairProcessor.h"
#include "VxStudioProcessorTestUtils.h"

#include <atomic>
#include <array>
#include <cstdlib>
#include <iostream>
#include <new>
#include <thread>

namespace {

using namespace vxsuite::test;

std::atomic<bool> gAllocationTrackingEnabled { false };
std::atomic<int> gTrackedAllocations { 0 };

template <std::size_t Size>
juce::String fixedLabelToString(const std::array<char, Size>& value) {
    return juce::String(value.data());
}

struct AllocationScope {
    AllocationScope() {
        gTrackedAllocations.store(0, std::memory_order_relaxed);
        gAllocationTrackingEnabled.store(true, std::memory_order_relaxed);
    }
    ~AllocationScope() {
        gAllocationTrackingEnabled.store(false, std::memory_order_relaxed);
    }
    [[nodiscard]] int allocations() const noexcept {
        return gTrackedAllocations.load(std::memory_order_relaxed);
    }
};

template <typename Processor>
bool expectNoSteadyStateAllocations(const char* label,
                                    Processor& processor,
                                    const juce::AudioBuffer<float>& input) {
    {
        auto warmup = input;
        juce::MidiBuffer midi;
        processor.processBlock(warmup, midi);
    }

    auto testBlock = input;
    juce::MidiBuffer midi;
    AllocationScope allocationScope;
    processor.processBlock(testBlock, midi);
    if (allocationScope.allocations() != 0) {
        std::cerr << "[VXSuitePluginRegression] Audio-thread allocation detected during steady-state "
                  << label << " processing: count=" << allocationScope.allocations() << "\n";
        return false;
    }
    return true;
}

bool primeSubtractLearn(VXSubtractAudioProcessor& processor, double sr, int blockSize = 256);

bool ensureDeepFilterTestModelsInstalled() {
    const auto repoRoot = juce::File(__FILE__).getParentDirectory().getParentDirectory();
    const auto sourceDir = repoRoot.getChildFile("assets").getChildFile("deepfilternet").getChildFile("models");
    const auto cacheRoot = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("VX Suite")
        .getChildFile("Models");

    struct ModelFile {
        const char* packageId;
        const char* fileName;
    };

    for (const auto& model : std::array<ModelFile, 2> {{
             { "deepfilternet3", "DeepFilterNet3_onnx.tar.gz" },
             { "deepfilternet2", "DeepFilterNet2_onnx_ll.tar.gz" },
         }}) {
        const auto source = sourceDir.getChildFile(model.fileName);
        if (!source.existsAsFile()) {
            std::cerr << "[VXSuitePluginRegression] Missing DeepFilter test model asset: "
                      << source.getFullPathName() << "\n";
            return false;
        }

        const auto packageDir = cacheRoot.getChildFile(model.packageId);
        if (!packageDir.exists() && !packageDir.createDirectory()) {
            std::cerr << "[VXSuitePluginRegression] Could not create DeepFilter model cache directory: "
                      << packageDir.getFullPathName() << "\n";
            return false;
        }

        const auto destination = packageDir.getChildFile(model.fileName);
        if (!destination.existsAsFile() && !source.copyFileTo(destination)) {
            std::cerr << "[VXSuitePluginRegression] Could not install DeepFilter test model into cache: "
                      << destination.getFullPathName() << "\n";
            return false;
        }
    }

    return true;
}

juce::AudioBuffer<float> renderSubtractProximityFinishChain(const double sr,
                                                            const int blockSize,
                                                            const juce::AudioBuffer<float>& input) {
    VXSubtractAudioProcessor subtract;
    subtract.prepareToPlay(sr, blockSize);
    if (!primeSubtractLearn(subtract, sr, blockSize))
        return {};
    setParamNormalized(subtract, "subtract", 0.78f);
    setParamNormalized(subtract, "protect", 0.48f);
    auto afterSubtract = render(subtract, input, blockSize);

    VXProximityAudioProcessor proximity;
    proximity.prepareToPlay(sr, blockSize);
    setParamNormalized(proximity, "closer", 0.24f);
    setParamNormalized(proximity, "air", 0.18f);
    auto afterProximity = render(proximity, afterSubtract, blockSize);

    VXFinishAudioProcessor finish;
    finish.prepareToPlay(sr, blockSize);
    setParamNormalized(finish, "finish", 0.42f);
    setParamNormalized(finish, "body", 0.32f);
    setParamNormalized(finish, "gain", 0.28f);
    return render(finish, afterProximity, blockSize);
}

juce::AudioBuffer<float> renderProximityToneFinishChain(const double sr,
                                                        const int blockSize,
                                                        const juce::AudioBuffer<float>& input) {
    VXProximityAudioProcessor proximity;
    proximity.prepareToPlay(sr, blockSize);
    setParamNormalized(proximity, "closer", 0.82f);
    setParamNormalized(proximity, "air", 0.72f);
    auto afterProximity = render(proximity, input, blockSize);

    VXToneAudioProcessor tone;
    tone.prepareToPlay(sr, blockSize);
    setParamNormalized(tone, "bass", 0.84f);
    setParamNormalized(tone, "treble", 0.76f);
    auto afterTone = render(tone, afterProximity, blockSize);

    VXFinishAudioProcessor finish;
    finish.prepareToPlay(sr, blockSize);
    setParamNormalized(finish, "finish", 0.36f);
    setParamNormalized(finish, "body", 0.42f);
    setParamNormalized(finish, "gain", 0.50f);
    return render(finish, afterTone, blockSize);
}

juce::AudioBuffer<float> makeMonoBuffer(const juce::AudioBuffer<float>& stereo) {
    juce::AudioBuffer<float> mono(1, stereo.getNumSamples());
    for (int i = 0; i < stereo.getNumSamples(); ++i) {
        const float sample = 0.5f * (stereo.getSample(0, i) + stereo.getSample(std::min(1, stereo.getNumChannels() - 1), i));
        mono.setSample(0, i, sample);
    }
    return mono;
}

juce::AudioBuffer<float> makeDualMonoBuffer(const juce::AudioBuffer<float>& stereo) {
    juce::AudioBuffer<float> dualMono(2, stereo.getNumSamples());
    for (int i = 0; i < stereo.getNumSamples(); ++i) {
        const float sample = 0.5f * (stereo.getSample(0, i)
            + stereo.getSample(std::min(1, stereo.getNumChannels() - 1), i));
        dualMono.setSample(0, i, sample);
        dualMono.setSample(1, i, sample);
    }
    return dualMono;
}

juce::AudioBuffer<float> makeRebalanceStereoConfidenceInput(const double sr, const float seconds) {
    auto speech = makeSpeechLike(sr, seconds);
    auto lowTone = makeSine(sr, seconds, 110.0f, 0.11f);
    auto midTone = makeSine(sr, seconds, 820.0f, 0.08f);
    auto highTone = makeSine(sr, seconds, 4200.0f, 0.06f);
    auto air = makeNoise(sr, seconds, 0.018f);

    juce::AudioBuffer<float> buffer(2, speech.getNumSamples());
    buffer.clear();
    for (int i = 0; i < buffer.getNumSamples(); ++i) {
        const float left = 0.58f * speech.getSample(0, i)
            + lowTone.getSample(0, i)
            + 0.65f * midTone.getSample(0, i)
            + 0.15f * highTone.getSample(0, i);
        const float right = 0.58f * speech.getSample(1, i)
            + 0.20f * lowTone.getSample(1, i)
            + 0.30f * midTone.getSample(1, i)
            + highTone.getSample(1, i)
            + air.getSample(1, i);
        buffer.setSample(0, i, left);
        buffer.setSample(1, i, right);
    }

    const float peak = peakAbs(buffer);
    if (peak > 1.0e-6f)
        buffer.applyGain(0.92f / peak);
    return buffer;
}

float dotProduct(const juce::AudioBuffer<float>& a,
                 const juce::AudioBuffer<float>& b) {
    const int channels = std::min(a.getNumChannels(), b.getNumChannels());
    const int samples = std::min(a.getNumSamples(), b.getNumSamples());
    double dot = 0.0;
    for (int ch = 0; ch < channels; ++ch) {
        const auto* aa = a.getReadPointer(ch);
        const auto* bb = b.getReadPointer(ch);
        for (int i = 0; i < samples; ++i)
            dot += static_cast<double>(aa[i]) * static_cast<double>(bb[i]);
    }
    return static_cast<float>(dot);
}

float bufferEnergy(const juce::AudioBuffer<float>& buffer) {
    return dotProduct(buffer, buffer);
}

float correlation(const juce::AudioBuffer<float>& a,
                  const juce::AudioBuffer<float>& b) {
    const float denom = std::sqrt(std::max(1.0e-9f, bufferEnergy(a) * bufferEnergy(b)));
    return dotProduct(a, b) / denom;
}

juce::AudioBuffer<float> subtractBuffers(const juce::AudioBuffer<float>& a,
                                         const juce::AudioBuffer<float>& b) {
    const int channels = std::min(a.getNumChannels(), b.getNumChannels());
    const int samples = std::min(a.getNumSamples(), b.getNumSamples());
    juce::AudioBuffer<float> delta(channels, samples);
    delta.clear();
    for (int ch = 0; ch < channels; ++ch) {
        delta.copyFrom(ch, 0, a, ch, 0, samples);
        delta.addFrom(ch, 0, b, ch, 0, samples, -1.0f);
    }
    return delta;
}

juce::AudioBuffer<float> makeRebalanceSyntheticGuitarStem(const double sr, const float seconds) {
    const int samples = static_cast<int>(sr * seconds);
    juce::AudioBuffer<float> buffer(2, samples);
    buffer.clear();
    for (int i = 0; i < samples; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(sr);
        const float pulse = 0.55f + 0.45f * std::max(0.0f, std::sin(2.0f * juce::MathConstants<float>::pi * 4.2f * t));
        const float body = 0.18f * std::sin(2.0f * juce::MathConstants<float>::pi * 196.0f * t);
        const float mid = 0.13f * std::sin(2.0f * juce::MathConstants<float>::pi * 392.0f * t);
        const float upper = 0.09f * std::sin(2.0f * juce::MathConstants<float>::pi * 784.0f * t);
        const float pick = 0.04f * std::sin(2.0f * juce::MathConstants<float>::pi * 2350.0f * t)
            * (0.5f + 0.5f * std::sin(2.0f * juce::MathConstants<float>::pi * 8.4f * t));
        const float left = pulse * (body + 0.92f * mid + 0.70f * upper) + pick;
        const float right = pulse * (0.86f * body + 1.06f * mid + 0.84f * upper) - 0.75f * pick;
        buffer.setSample(0, i, left);
        buffer.setSample(1, i, right);
    }
    return buffer;
}

juce::AudioBuffer<float> makeRebalanceSyntheticOtherStem(const double sr, const float seconds) {
    const int samples = static_cast<int>(sr * seconds);
    juce::AudioBuffer<float> buffer(2, samples);
    buffer.clear();
    for (int i = 0; i < samples; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(sr);
        const float chordPulse = std::max(0.0f, std::sin(2.0f * juce::MathConstants<float>::pi * 2.3f * t));
        const float env = 0.34f + 0.66f * chordPulse;
        const float base = 0.16f * std::sin(2.0f * juce::MathConstants<float>::pi * 261.63f * t);
        const float octave = 0.12f * std::sin(2.0f * juce::MathConstants<float>::pi * 523.25f * t);
        const float upper = 0.08f * std::sin(2.0f * juce::MathConstants<float>::pi * 1046.5f * t);
        const float shimmer = 0.035f * std::sin(2.0f * juce::MathConstants<float>::pi * 3139.5f * t)
            * (0.5f + 0.5f * chordPulse);
        const float sample = env * (base + octave + upper + shimmer);
        buffer.setSample(0, i, sample);
        buffer.setSample(1, i, sample);
    }
    return buffer;
}

juce::AudioBuffer<float> makeHarshContaminatedInput(const double sr, const float seconds) {
    auto speech = makeSpeechLike(sr, seconds);
    auto harshA = makeSine(sr, seconds, 3600.0f, 0.045f);
    auto harshB = makeSine(sr, seconds, 6100.0f, 0.040f);
    auto harshNoise = makeNoise(sr, seconds, 0.016f);
    auto buffer = addBuffers(addBuffers(speech, harshA), addBuffers(harshB, harshNoise));

    for (int i = 0; i < buffer.getNumSamples(); ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(sr);
        const float rasp = 0.52f + 0.48f * std::max(0.0f, std::sin(2.0f * juce::MathConstants<float>::pi * 6.5f * t));
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.setSample(ch, i, buffer.getSample(ch, i) + rasp * harshNoise.getSample(ch, i));
    }

    const float peak = peakAbs(buffer);
    if (peak > 1.0e-6f)
        buffer.applyGain(0.92f / peak);
    return buffer;
}

juce::AudioBuffer<float> makePerformInstrumentInput(const double sr, const float seconds) {
    const int samples = static_cast<int>(sr * seconds);
    juce::AudioBuffer<float> buffer(2, samples);
    buffer.clear();
    for (int i = 0; i < samples; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(sr);
        float sectionEnv = 0.40f;
        if (t >= seconds * 0.20f && t < seconds * 0.42f)
            sectionEnv = 1.00f;
        else if (t >= seconds * 0.42f && t < seconds * 0.58f)
            sectionEnv = 0.55f;
        else if (t >= seconds * 0.58f && t < seconds * 0.82f)
            sectionEnv = 1.15f;
        else if (t >= seconds * 0.82f)
            sectionEnv = 0.50f;
        const float pulse = 0.5f + 0.5f * std::sin(2.0f * juce::MathConstants<float>::pi * 2.1f * t);
        const float body = 0.34f * std::sin(2.0f * juce::MathConstants<float>::pi * 110.0f * t)
                         + 0.22f * std::sin(2.0f * juce::MathConstants<float>::pi * 220.0f * t)
                         + 0.14f * std::sin(2.0f * juce::MathConstants<float>::pi * 330.0f * t);
        const float bite = 0.12f * std::sin(2.0f * juce::MathConstants<float>::pi * 2900.0f * t)
                         * (0.35f + 0.65f * pulse);
        const float transient = 0.10f * std::sin(2.0f * juce::MathConstants<float>::pi * 6100.0f * t)
                              * std::pow(pulse, 6.0f);
        const float sample = sectionEnv * (0.78f * body + bite + transient);
        buffer.setSample(0, i, sample);
        buffer.setSample(1, i, sample * 0.98f);
    }
    return buffer;
}

juce::AudioBuffer<float> makeDeverbSyntheticRoom(const juce::AudioBuffer<float>& dry,
                                                 const double sr,
                                                 const float rt60Seconds) {
    const float safeRt60 = juce::jlimit(0.1f, 4.0f, rt60Seconds);
    const int rirSamples = std::max(1, static_cast<int>(std::ceil(sr * safeRt60 * 1.5f)));
    std::vector<float> rir(static_cast<size_t>(rirSamples), 0.0f);
    std::mt19937 rng(12345);
    std::normal_distribution<float> dist(0.0f, 1.0f);

    rir[0] = 1.0f;
    for (int n = 1; n < rirSamples; ++n) {
        const float decay = std::exp(-6.908f * static_cast<float>(n)
                                     / (safeRt60 * static_cast<float>(sr)));
        rir[static_cast<size_t>(n)] = 0.35f * dist(rng) * decay;
    }

    juce::AudioBuffer<float> wet(dry.getNumChannels(), dry.getNumSamples());
    wet.clear();
    for (int ch = 0; ch < dry.getNumChannels(); ++ch) {
        const auto* src = dry.getReadPointer(ch);
        auto* dst = wet.getWritePointer(ch);
        for (int i = 0; i < dry.getNumSamples(); ++i) {
            double acc = 0.0;
            const int taps = std::min(i + 1, rirSamples);
            for (int n = 0; n < taps; ++n)
                acc += static_cast<double>(src[i - n]) * rir[static_cast<size_t>(n)];
            dst[i] = static_cast<float>(acc);
        }
    }
    return wet;
}

float tailRmsFromSample(const juce::AudioBuffer<float>& buffer, const int startSample) {
    double energy = 0.0;
    int count = 0;
    const int start = juce::jlimit(0, buffer.getNumSamples(), startSample);
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
        const auto* data = buffer.getReadPointer(ch);
        for (int i = start; i < buffer.getNumSamples(); ++i) {
            energy += static_cast<double>(data[i]) * data[i];
            ++count;
        }
    }
    return count > 0 ? static_cast<float>(std::sqrt(energy / static_cast<double>(count))) : 0.0f;
}

float windowRms(const juce::AudioBuffer<float>& buffer, const int startSample, const int windowSamples) {
    const int start = juce::jlimit(0, buffer.getNumSamples(), startSample);
    const int end = juce::jlimit(start, buffer.getNumSamples(), start + std::max(1, windowSamples));
    double energy = 0.0;
    int count = 0;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
        const auto* data = buffer.getReadPointer(ch);
        for (int i = start; i < end; ++i) {
            energy += static_cast<double>(data[i]) * data[i];
            ++count;
        }
    }
    return count > 0 ? static_cast<float>(std::sqrt(energy / static_cast<double>(count))) : 0.0f;
}

float windowedLevelSpreadDb(const juce::AudioBuffer<float>& buffer,
                            const double sr,
                            const float windowSeconds = 0.10f) {
    const int channels = buffer.getNumChannels();
    const int windowSamples = std::max(1, static_cast<int>(std::round(sr * windowSeconds)));
    std::vector<float> levels;
    for (int start = 0; start < buffer.getNumSamples(); start += windowSamples) {
        const int end = std::min(buffer.getNumSamples(), start + windowSamples);
        double energy = 0.0;
        int count = 0;
        for (int ch = 0; ch < channels; ++ch) {
            const auto* data = buffer.getReadPointer(ch);
            for (int i = start; i < end; ++i) {
                energy += static_cast<double>(data[i]) * data[i];
                ++count;
            }
        }
        const float rms = count > 0 ? static_cast<float>(std::sqrt(energy / static_cast<double>(count))) : 0.0f;
        levels.push_back(juce::Decibels::gainToDecibels(std::max(rms, 1.0e-5f), -100.0f));
    }
    if (levels.size() < 2)
        return 0.0f;
    double mean = 0.0;
    for (float v : levels)
        mean += v;
    mean /= static_cast<double>(levels.size());
    double variance = 0.0;
    for (float v : levels) {
        const double d = static_cast<double>(v) - mean;
        variance += d * d;
    }
    variance /= static_cast<double>(levels.size());
    return static_cast<float>(std::sqrt(variance));
}

float catmullRomSample(const float p0,
                       const float p1,
                       const float p2,
                       const float p3,
                       const float t) {
    const float t2 = t * t;
    const float t3 = t2 * t;
    return 0.5f * ((2.0f * p1)
                   + (-p0 + p2) * t
                   + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2
                   + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}

float estimatedTruePeak(const juce::AudioBuffer<float>& buffer) {
    constexpr std::array<float, 3> kProbePoints { 0.25f, 0.5f, 0.75f };
    float peak = 0.0f;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
        const auto* data = buffer.getReadPointer(ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i) {
            const float p1 = data[i];
            peak = std::max(peak, std::abs(p1));
            if (i >= buffer.getNumSamples() - 1)
                continue;

            const float p0 = data[i > 0 ? i - 1 : i];
            const float p2 = data[i + 1];
            const float p3 = data[i + 2 < buffer.getNumSamples() ? i + 2 : i + 1];
            for (const float t : kProbePoints)
                peak = std::max(peak, std::abs(catmullRomSample(p0, p1, p2, p3, t)));
        }
    }
    return peak;
}

bool primeSubtractLearn(VXSubtractAudioProcessor& processor, const double sr, const int blockSize) {
    const int learnBlockSize = std::max(1, blockSize);
    juce::AudioBuffer<float> warmup(2, learnBlockSize);
    warmup.clear();
    setParamNormalized(processor, "learn", 0.0f);
    processSingleBlock(processor, warmup);

    auto noise = makeNoise(sr, 0.8f, 0.10f);
    setParamNormalized(processor, "learn", 1.0f);

    float lastProgress = 0.0f;
    float lastObserved = 0.0f;
    juce::MidiBuffer midi;
    for (int start = 0; start < noise.getNumSamples(); start += learnBlockSize) {
        const int num = std::min(learnBlockSize, noise.getNumSamples() - start);
        juce::AudioBuffer<float> block(2, num);
        for (int ch = 0; ch < 2; ++ch)
            block.copyFrom(ch, 0, noise, ch, start, num);
        processor.processBlock(block, midi);
        const float progress = processor.getLearnProgress();
        const float observed = processor.getLearnObservedSeconds();
        if (progress + 1.0e-5f < lastProgress) {
            std::cerr << "[VXSuitePluginRegression] Subtract learn progress regressed\n";
            return false;
        }
        if (observed + 1.0e-5f < lastObserved) {
            std::cerr << "[VXSuitePluginRegression] Subtract observed learn seconds regressed\n";
            return false;
        }
        lastProgress = progress;
        lastObserved = observed;
    }

    if (!(processor.isLearnActive() && processor.getLearnProgress() > 0.20f && processor.getLearnObservedSeconds() > 0.10f)) {
        std::cerr << "[VXSuitePluginRegression] Subtract learn never accumulated sensible progress\n";
        return false;
    }

    setParamNormalized(processor, "learn", 0.0f);
    juce::AudioBuffer<float> stopBlock(2, 256);
    stopBlock.clear();
    processSingleBlock(processor, stopBlock);

    if (processor.isLearnActive() || !processor.isLearnReady()) {
        std::cerr << "[VXSuitePluginRegression] Subtract learn did not finalize into a ready profile: active="
                  << processor.isLearnActive() << " ready=" << processor.isLearnReady()
                  << " progress=" << processor.getLearnProgress()
                  << " confidence=" << processor.getLearnConfidence()
                  << " observed=" << processor.getLearnObservedSeconds() << "\n";
        return false;
    }
    if (processor.getLearnConfidence() < 0.0f || processor.getLearnConfidence() > 1.0f) {
        std::cerr << "[VXSuitePluginRegression] Subtract learn confidence out of range\n";
        return false;
    }
    return true;
}

bool primeSubtractLearnRightOnly(VXSubtractAudioProcessor& processor, const double sr) {
    juce::AudioBuffer<float> warmup(2, 256);
    warmup.clear();
    setParamNormalized(processor, "learn", 0.0f);
    processSingleBlock(processor, warmup);

    auto noise = makeNoise(sr, 0.8f, 0.10f);
    for (int i = 0; i < noise.getNumSamples(); ++i)
        noise.setSample(0, i, 0.0f);

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

bool testSubtractLearnLifecycleMakesSense() {
    constexpr double sr = 48000.0;
    VXSubtractAudioProcessor processor;
    processor.prepareToPlay(sr, 256);
    return primeSubtractLearn(processor, sr);
}

bool testSubtractLearnStartsOnFirstPress() {
    constexpr double sr = 48000.0;
    VXSubtractAudioProcessor processor;
    processor.prepareToPlay(sr, 256);

    auto noise = makeNoise(sr, 0.2f, 0.08f);
    setParamNormalized(processor, "learn", 1.0f);
    juce::AudioBuffer<float> firstBlock(2, 256);
    for (int ch = 0; ch < 2; ++ch)
        firstBlock.copyFrom(ch, 0, noise, ch, 0, 256);
    processSingleBlock(processor, firstBlock);

    if (!processor.isLearnActive()) {
        std::cerr << "[VXSuitePluginRegression] Subtract learn did not start on the first press after prepare/reset\n";
        return false;
    }
    return true;
}

bool testSubtractSilentLearnDoesNotCreateProfileOrMuteOutput() {
    constexpr double sr = 48000.0;
    constexpr int blockSize = 256;
    VXSubtractAudioProcessor processor;
    processor.prepareToPlay(sr, blockSize);
    setParamNormalized(processor, "subtract", 1.0f);
    setParamNormalized(processor, "protect", 0.5f);

    setParamNormalized(processor, "learn", 1.0f);
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> silentBlock(2, blockSize);
    silentBlock.clear();
    for (int i = 0; i < 420; ++i) {
        auto block = silentBlock;
        processor.processBlock(block, midi);
        if (rms(block) > 3.0e-6f) {
            std::cerr << "[VXSuitePluginRegression] Subtract silent learn generated output: rms="
                      << rms(block) << "\n";
            return false;
        }
    }

    if (!processor.isLearnActive()) {
        std::cerr << "[VXSuitePluginRegression] Subtract silent learn did not stay active while armed\n";
        return false;
    }
    if (processor.getLearnProgress() > 1.0e-4f || processor.getLearnObservedSeconds() > 1.0e-4f) {
        std::cerr << "[VXSuitePluginRegression] Subtract silent learn accumulated progress: progress="
                  << processor.getLearnProgress() << " observed=" << processor.getLearnObservedSeconds() << "\n";
        return false;
    }
    if (processor.getLearnConfidence() > 1.0e-4f) {
        std::cerr << "[VXSuitePluginRegression] Subtract silent learn accumulated confidence: confidence="
                  << processor.getLearnConfidence() << "\n";
        return false;
    }

    setParamNormalized(processor, "learn", 0.0f);
    auto stopBlock = silentBlock;
    processor.processBlock(stopBlock, midi);
    if (processor.isLearnReady()) {
        std::cerr << "[VXSuitePluginRegression] Subtract finalized silence into a ready learned profile\n";
        return false;
    }

    auto speech = makeSpeechLike(sr, 1.0f);
    const float inputRms = rmsSkip(speech, 4096);
    auto out = render(processor, speech, blockSize);
    const float outputRms = rmsSkip(out, 4096);
    if (outputRms < inputRms * 0.25f) {
        std::cerr << "[VXSuitePluginRegression] Subtract muted output after failed silent learn: input="
                  << inputRms << " output=" << outputRms << "\n";
        return false;
    }
    return true;
}

bool testSubtractListenOutputsMeaningfulRemovedDelta() {
    constexpr double sr = 48000.0;
    VXSubtractAudioProcessor processor;
    processor.prepareToPlay(sr, 256);
    if (!primeSubtractLearn(processor, sr))
        return false;

    auto speech = makeSpeechLike(sr, 1.0f);
    auto noise = makeNoise(sr, 1.0f, 0.08f);
    auto noisy = addBuffers(speech, noise);

    processor.reset();
    setParamNormalized(processor, "subtract", 0.82f);
    setParamNormalized(processor, "protect", 0.45f);
    setParamNormalized(processor, "listen", 0.0f);
    auto wet = render(processor, noisy);

    processor.reset();
    setParamNormalized(processor, "subtract", 0.82f);
    setParamNormalized(processor, "protect", 0.45f);
    setParamNormalized(processor, "listen", 1.0f);
    auto listen = render(processor, noisy);

    const float listenRms = rms(listen);
    const auto recombined = addBuffers(wet, listen);
    const float recombineResidual = bestGainResidualRatioSkip(noisy, recombined, 4096);
    if (!(listenRms > 1.0e-4f)) {
        std::cerr << "[VXSuitePluginRegression] Subtract listen output was unexpectedly empty\n";
        return false;
    }
    if (recombineResidual > 0.60f) {
        std::cerr << "[VXSuitePluginRegression] Subtract wet/listen steady-state no longer recombines close to dry input: residualRatio="
                  << recombineResidual << "\n";
        return false;
    }
    return true;
}

bool testSubtractLearnedProfileSurvivesResetAndStillActs() {
    constexpr double sr = 48000.0;
    VXSubtractAudioProcessor processor;
    processor.prepareToPlay(sr, 256);
    if (!primeSubtractLearn(processor, sr))
        return false;

    auto speech = makeSpeechLike(sr, 1.0f);
    auto noise = makeNoise(sr, 1.0f, 0.08f);
    auto noisy = addBuffers(speech, noise);

    setParamNormalized(processor, "mode", 1.0f);
    setParamNormalized(processor, "subtract", 0.86f);
    setParamNormalized(processor, "protect", 0.22f);
    const auto first = render(processor, noisy, 256);
    const float firstResidual = bestGainResidualRatioSkip(noisy, first, 4096);

    processor.reset();
    setParamNormalized(processor, "mode", 1.0f);
    setParamNormalized(processor, "subtract", 0.86f);
    setParamNormalized(processor, "protect", 0.22f);
    const auto second = render(processor, noisy, 256);
    const float secondResidual = bestGainResidualRatioSkip(noisy, second, 4096);

    if (firstResidual > 0.94f || secondResidual > 0.94f) {
        std::cerr << "[VXSuitePluginRegression] Subtract forgot or stopped applying its learned profile after reset: firstResidual="
                  << firstResidual << " secondResidual=" << secondResidual << "\n";
        return false;
    }
    return true;
}

bool testSubtractActsAtStartOfRenderedAudio() {
    constexpr double sr = 48000.0;
    constexpr int blockSize = 256;
    VXSubtractAudioProcessor processor;
    processor.prepareToPlay(sr, blockSize);
    if (!primeSubtractLearn(processor, sr, blockSize))
        return false;

    auto speech = makeSpeechLike(sr, 1.0f);
    auto noise = makeNoise(sr, 1.0f, 0.10f);
    auto noisy = addBuffers(speech, noise);

    processor.reset();
    setParamNormalized(processor, "mode", 1.0f);
    setParamNormalized(processor, "subtract", 1.0f);
    setParamNormalized(processor, "protect", 0.20f);
    const auto out = render(processor, noisy, blockSize);

    const auto windowRmsDiff = [](const juce::AudioBuffer<float>& a,
                                  const juce::AudioBuffer<float>& b,
                                  const int startSample,
                                  const int endSample) {
        const int channels = std::min(a.getNumChannels(), b.getNumChannels());
        const int start = juce::jlimit(0, std::min(a.getNumSamples(), b.getNumSamples()), startSample);
        const int end = juce::jlimit(start, std::min(a.getNumSamples(), b.getNumSamples()), endSample);
        double energy = 0.0;
        int count = 0;
        for (int ch = 0; ch < channels; ++ch) {
            const auto* aa = a.getReadPointer(ch);
            const auto* bb = b.getReadPointer(ch);
            for (int i = start; i < end; ++i) {
                const double d = static_cast<double>(aa[i] - bb[i]);
                energy += d * d;
                ++count;
            }
        }
        return count > 0 ? static_cast<float>(std::sqrt(energy / static_cast<double>(count))) : 0.0f;
    };

    const int earlyStart = static_cast<int>(0.04 * sr);
    const int earlyEnd = static_cast<int>(0.20 * sr);
    const int lateStart = static_cast<int>(0.55 * sr);
    const int lateEnd = static_cast<int>(0.75 * sr);
    const float earlyDiff = windowRmsDiff(noisy, out, earlyStart, earlyEnd);
    const float lateDiff = windowRmsDiff(noisy, out, lateStart, lateEnd);

    if (earlyDiff < 0.55f * lateDiff || earlyDiff < 1.0e-3f) {
        std::cerr << "[VXSuitePluginRegression] Subtract did not act strongly enough at render start: earlyDiff="
                  << earlyDiff << " lateDiff=" << lateDiff << "\n";
        return false;
    }
    return true;
}

bool testSubtractGeneralModeStaysUsefulAndNonSilent() {
    constexpr double sr = 48000.0;
    VXSubtractAudioProcessor processor;
    processor.prepareToPlay(sr, 256);
    if (!primeSubtractLearn(processor, sr))
        return false;

    auto speech = makeSpeechLike(sr, 1.0f);
    auto noise = makeNoise(sr, 1.0f, 0.10f);
    auto noisy = addBuffers(speech, noise);

    setParamNormalized(processor, "mode", 1.0f);
    setParamNormalized(processor, "subtract", 1.0f);
    setParamNormalized(processor, "protect", 0.15f);
    const auto generalOut = render(processor, noisy, 256);
    const float inputRms = rmsSkip(noisy, 4096);
    const float outputRms = rmsSkip(generalOut, 4096);
    const float residual = bestGainResidualRatioSkip(noisy, generalOut, 4096);
    const float diff = maxAbsDiffSkip(noisy, generalOut, 4096);

    if (outputRms < inputRms * 0.16f) {
        std::cerr << "[VXSuitePluginRegression] Subtract general mode collapsed toward silence: input="
                  << inputRms << " output=" << outputRms << "\n";
        return false;
    }
    if (residual > 0.97f || diff < 0.01f) {
        std::cerr << "[VXSuitePluginRegression] Subtract general mode no longer does enough useful work: residual="
                  << residual << " diff=" << diff << "\n";
        return false;
    }
    return true;
}

bool testSubtractBlindProtectAndModeAreAudible() {
    constexpr double sr = 48000.0;
    constexpr int blockSize = 256;
    auto noisy = addBuffers(makeSpeechLike(sr, 1.0f), makeNoise(sr, 1.0f, 0.08f));

    VXSubtractAudioProcessor vocalLowProtect;
    vocalLowProtect.prepareToPlay(sr, blockSize);
    setParamNormalized(vocalLowProtect, "mode", 0.0f);
    setParamNormalized(vocalLowProtect, "subtract", 1.0f);
    setParamNormalized(vocalLowProtect, "protect", 0.0f);
    const auto vocalLow = render(vocalLowProtect, noisy, blockSize);

    VXSubtractAudioProcessor vocalHighProtect;
    vocalHighProtect.prepareToPlay(sr, blockSize);
    setParamNormalized(vocalHighProtect, "mode", 0.0f);
    setParamNormalized(vocalHighProtect, "subtract", 1.0f);
    setParamNormalized(vocalHighProtect, "protect", 1.0f);
    const auto vocalHigh = render(vocalHighProtect, noisy, blockSize);

    VXSubtractAudioProcessor general;
    general.prepareToPlay(sr, blockSize);
    setParamNormalized(general, "mode", 1.0f);
    setParamNormalized(general, "subtract", 1.0f);
    setParamNormalized(general, "protect", 0.0f);
    const auto generalOut = render(general, noisy, blockSize);

    const auto diffRmsSkip = [](const juce::AudioBuffer<float>& a,
                                const juce::AudioBuffer<float>& b,
                                const int skipSamples) {
        double energy = 0.0;
        int count = 0;
        const int channels = std::min(a.getNumChannels(), b.getNumChannels());
        const int samples = std::min(a.getNumSamples(), b.getNumSamples());
        const int start = juce::jlimit(0, samples, skipSamples);
        for (int ch = 0; ch < channels; ++ch) {
            const auto* aa = a.getReadPointer(ch);
            const auto* bb = b.getReadPointer(ch);
            for (int i = start; i < samples; ++i) {
                const double d = static_cast<double>(aa[i] - bb[i]);
                energy += d * d;
                ++count;
            }
        }
        return count > 0 ? static_cast<float>(std::sqrt(energy / static_cast<double>(count))) : 0.0f;
    };

    const float lowProtectDelta = diffRmsSkip(noisy, vocalLow, 4096);
    const float highProtectDelta = diffRmsSkip(noisy, vocalHigh, 4096);
    const float modeDiff = maxAbsDiffSkip(vocalLow, generalOut, 4096);

    if (!(highProtectDelta < lowProtectDelta * 0.82f)) {
        std::cerr << "[VXSuitePluginRegression] Subtract blind Protect did not reduce removal enough: low="
                  << lowProtectDelta << " high=" << highProtectDelta << "\n";
        return false;
    }
    if (modeDiff < 0.006f) {
        std::cerr << "[VXSuitePluginRegression] Subtract blind Vocal/General modes are still too similar: diff="
                  << modeDiff << "\n";
        return false;
    }
    return true;
}

bool testSubtractListenIsSilentWhenNoRemoval() {
    constexpr double sr = 48000.0;
    constexpr int blockSize = 256;
    auto noisy = addBuffers(makeSpeechLike(sr, 0.6f), makeNoise(sr, 0.6f, 0.05f));

    VXSubtractAudioProcessor processor;
    processor.prepareToPlay(sr, blockSize);
    setParamNormalized(processor, "subtract", 0.0f);
    setParamNormalized(processor, "protect", 0.5f);
    setParamNormalized(processor, "listen", 1.0f);
    const auto listen = render(processor, noisy, blockSize);

    const float listenLevel = rmsSkip(listen, 4096);
    if (listenLevel > 1.0e-5f) {
        std::cerr << "[VXSuitePluginRegression] Subtract Listen leaked audio when no removal happened: rms="
                  << listenLevel << "\n";
        return false;
    }
    return true;
}

bool testSubtractStaleLearnedProfileBacksOffOnMismatch() {
    constexpr double sr = 48000.0;
    constexpr int blockSize = 256;

    VXSubtractAudioProcessor processor;
    processor.prepareToPlay(sr, blockSize);
    if (!primeSubtractLearn(processor, sr, blockSize))
        return false;

    auto speech = makeSpeechLike(sr, 1.0f);

    setParamNormalized(processor, "mode", 1.0f);
    setParamNormalized(processor, "subtract", 1.0f);
    setParamNormalized(processor, "protect", 0.18f);
    render(processor, speech, blockSize);
    const float trust = processor.getProfileTrust();

    if (!(trust < 0.98f)) {
        std::cerr << "[VXSuitePluginRegression] Subtract stale learned profile trust stayed too high on speech-dominant material: trust="
                  << trust << "\n";
        return false;
    }
    return true;
}

bool testRebalanceInstancesStayTrackLocal() {
    constexpr double sr = 48000.0;
    constexpr int blockSize = 256;

    VXRebalanceAudioProcessor silentTrack;
    VXRebalanceAudioProcessor loudTrack;
    silentTrack.prepareToPlay(sr, blockSize);
    loudTrack.prepareToPlay(sr, blockSize);

    setParamNormalized(silentTrack, "vocals", 1.0f);
    setParamNormalized(silentTrack, "drums", 0.0f);
    setParamNormalized(silentTrack, "strength", 1.0f);
    setParamNormalized(loudTrack, "bass", 0.0f);
    setParamNormalized(loudTrack, "guitar", 1.0f);
    setParamNormalized(loudTrack, "strength", 1.0f);

    juce::AudioBuffer<float> silent(2, static_cast<int>(sr * 1.0));
    silent.clear();
    auto loud = addBuffers(makeSpeechLike(sr, 1.0f), makeNoise(sr, 1.0f, 0.12f));

    auto loudOut = render(loudTrack, loud, blockSize);
    auto silentOut = render(silentTrack, silent, blockSize);

    if (rms(loudOut) <= 1.0e-5f) {
        std::cerr << "[VXSuitePluginRegression] Rebalance loud reference unexpectedly rendered silent\n";
        return false;
    }

    if (rms(silentOut) > 1.0e-6f) {
        std::cerr << "[VXSuitePluginRegression] Rebalance silent instance picked up another instance/track: rms="
                  << rms(silentOut) << "\n";
        return false;
    }
    return true;
}

bool testRebalanceBacksOffOnLowConfidenceMaterial() {
    constexpr double sr = 48000.0;
    constexpr int blockSize = 256;

    const auto stereoInput = makeRebalanceStereoConfidenceInput(sr, 1.4f);
    const auto monoInput = makeDualMonoBuffer(stereoInput);

    VXRebalanceAudioProcessor stereoProcessor;
    stereoProcessor.prepareToPlay(sr, blockSize);
    setParamNormalized(stereoProcessor, "vocals", 1.0f);
    setParamNormalized(stereoProcessor, "drums", 0.0f);
    setParamNormalized(stereoProcessor, "bass", 0.0f);
    setParamNormalized(stereoProcessor, "guitar", 0.15f);
    setParamNormalized(stereoProcessor, "other", 0.0f);
    setParamNormalized(stereoProcessor, "strength", 1.0f);

    VXRebalanceAudioProcessor monoProcessor;
    monoProcessor.prepareToPlay(sr, blockSize);
    setParamNormalized(monoProcessor, "vocals", 1.0f);
    setParamNormalized(monoProcessor, "drums", 0.0f);
    setParamNormalized(monoProcessor, "bass", 0.0f);
    setParamNormalized(monoProcessor, "guitar", 0.15f);
    setParamNormalized(monoProcessor, "other", 0.0f);
    setParamNormalized(monoProcessor, "strength", 1.0f);

    const auto stereoOut = render(stereoProcessor, stereoInput, blockSize);
    const auto monoOut = render(monoProcessor, monoInput, blockSize);

    const float stereoResidual = bestGainResidualRatioSkip(stereoInput, stereoOut, 4096);
    const float monoResidual = bestGainResidualRatioSkip(monoInput, monoOut, 4096);

    if (stereoResidual < 0.14f) {
        std::cerr << "[VXSuitePluginRegression] Rebalance confident stereo path became too timid: residual="
                  << stereoResidual << "\n";
        return false;
    }

    if (!(monoResidual + 0.025f < stereoResidual)) {
        std::cerr << "[VXSuitePluginRegression] Rebalance no longer backs off enough on low-confidence dual-mono material: stereoResidual="
                  << stereoResidual << " monoResidual=" << monoResidual << "\n";
        return false;
    }

    return true;
}

bool testRebalanceSeparatesGuitarFromOtherMoreClearly() {
    constexpr double sr = 48000.0;
    constexpr int blockSize = 256;
    const auto guitarStem = makeRebalanceSyntheticGuitarStem(sr, 1.4f);
    const auto otherStem = makeRebalanceSyntheticOtherStem(sr, 1.4f);
    auto mix = addBuffers(guitarStem, otherStem);
    const float mixPeak = peakAbs(mix);
    if (mixPeak > 1.0e-6f)
        mix.applyGain(0.9f / mixPeak);

    VXRebalanceAudioProcessor guitarProcessor;
    guitarProcessor.prepareToPlay(sr, blockSize);
    setParamNormalized(guitarProcessor, "guitar", 1.0f);
    setParamNormalized(guitarProcessor, "other", 0.5f);
    setParamNormalized(guitarProcessor, "strength", 1.0f);

    const auto guitarOut = render(guitarProcessor, mix, blockSize);
    const auto guitarDelta = subtractBuffers(guitarOut, mix);
    const float guitarToGuitar = correlation(guitarDelta, guitarStem);
    const float guitarToOther = correlation(guitarDelta, otherStem);

    if (!(guitarToGuitar > guitarToOther + 0.06f)) {
        std::cerr << "[VXSuitePluginRegression] Rebalance guitar lane still overlaps 'Other' too much: guitarCorr="
                  << guitarToGuitar << " otherCorr=" << guitarToOther << "\n";
        return false;
    }

    return true;
}

bool testRebalancePhoneModeStillActsOnRoughMaterial() {
    constexpr double sr = 48000.0;
    constexpr int blockSize = 256;

    const auto roughInput = makeDualMonoBuffer(makeRebalanceStereoConfidenceInput(sr, 1.4f));

    VXRebalanceAudioProcessor processor;
    processor.prepareToPlay(sr, blockSize);
    setParamNormalized(processor, "recordingType", 1.0f);
    setParamNormalized(processor, "vocals", 1.0f);
    setParamNormalized(processor, "drums", 0.0f);
    setParamNormalized(processor, "bass", 0.0f);
    setParamNormalized(processor, "guitar", 0.10f);
    setParamNormalized(processor, "other", 0.0f);
    setParamNormalized(processor, "strength", 1.0f);

    const auto out = render(processor, roughInput, blockSize);
    const float residual = bestGainResidualRatioSkip(roughInput, out, 4096);
    const float diff = maxAbsDiffSkip(roughInput, out, 4096);

    if (residual < 0.035f || diff < 0.004f) {
        std::cerr << "[VXSuitePluginRegression] Rebalance phone/rough mode became too close to unity on rough low-confidence material: residual="
                  << residual << " diff=" << diff << "\n";
        return false;
    }

    return true;
}

bool testRebalanceStrongSettingsStayHeadroomSafeAcrossRecordingTypes() {
    constexpr double sr = 48000.0;
    constexpr int blockSize = 256;

    auto input = makeRebalanceStereoConfidenceInput(sr, 1.4f);
    input = addBuffers(input, makeNoise(sr, 1.4f, 0.02f));
    const float inputPeak = peakAbs(input);
    if (inputPeak > 1.0e-6f)
        input.applyGain(0.95f / inputPeak);

    const auto runCase = [&](const float recordingType) {
        VXRebalanceAudioProcessor processor;
        processor.prepareToPlay(sr, blockSize);
        setParamNormalized(processor, "recordingType", recordingType);
        setParamNormalized(processor, "vocals", 1.0f);
        setParamNormalized(processor, "drums", 0.0f);
        setParamNormalized(processor, "bass", 1.0f);
        setParamNormalized(processor, "guitar", 0.0f);
        setParamNormalized(processor, "other", 0.0f);
        setParamNormalized(processor, "strength", 1.0f);
        return render(processor, input, blockSize);
    };

    const auto studioOut = runCase(0.0f);
    const auto roughOut = runCase(1.0f);

    for (const auto* candidate : { &studioOut, &roughOut }) {
        if (!allFinite(*candidate) || peakAbs(*candidate) > 0.995f) {
            std::cerr << "[VXSuitePluginRegression] Rebalance strong setting exceeded safe output headroom: peak="
                      << peakAbs(*candidate) << "\n";
            return false;
        }
    }

    if (maxAbsDiffSkip(input, studioOut, 4096) < 0.008f) {
        std::cerr << "[VXSuitePluginRegression] Rebalance studio strong setting became too subtle under headroom protection\n";
        return false;
    }
    if (maxAbsDiffSkip(input, roughOut, 4096) < 0.006f) {
        std::cerr << "[VXSuitePluginRegression] Rebalance rough strong setting became too subtle under headroom protection\n";
        return false;
    }

    return true;
}

bool testSubtractStereoLearnTreatsChannelsIndependently() {
    constexpr double sr = 48000.0;
    VXSubtractAudioProcessor processor;
    processor.prepareToPlay(sr, 256);
    if (!primeSubtractLearnRightOnly(processor, sr))
        return false;

    auto speech = makeSpeechLike(sr, 1.0f);
    auto noise = makeNoise(sr, 1.0f, 0.08f);
    juce::AudioBuffer<float> input(2, speech.getNumSamples());
    input.copyFrom(0, 0, speech, 0, 0, speech.getNumSamples());
    input.copyFrom(1, 0, speech, 1, 0, speech.getNumSamples());
    input.addFrom(1, 0, noise, 1, 0, noise.getNumSamples());

    setParamNormalized(processor, "subtract", 0.82f);
    setParamNormalized(processor, "protect", 0.45f);
    const auto out = render(processor, input, 256);

    juce::AudioBuffer<float> inLeft(1, input.getNumSamples()), inRight(1, input.getNumSamples());
    juce::AudioBuffer<float> outLeft(1, out.getNumSamples()), outRight(1, out.getNumSamples());
    for (int i = 0; i < input.getNumSamples(); ++i) {
        inLeft.setSample(0, i, input.getSample(0, i));
        inRight.setSample(0, i, input.getSample(1, i));
        outLeft.setSample(0, i, out.getSample(0, i));
        outRight.setSample(0, i, out.getSample(1, i));
    }

    const float leftResidual = bestGainResidualRatioSkip(inLeft, outLeft, 4096);
    const float rightResidual = bestGainResidualRatioSkip(inRight, outRight, 4096);
    if (!(rightResidual > leftResidual + 0.03f)) {
        std::cerr << "[VXSuitePluginRegression] Subtract stereo learn is still not channel-aware enough: leftResidual="
                  << leftResidual << " rightResidual=" << rightResidual << "\n";
        return false;
    }
    return true;
}

bool testSubtractProximityFinishChainStaysStable() {
    constexpr double sr = 48000.0;
    auto speech = makeSpeechLike(sr, 1.2f);
    auto noise = makeNoise(sr, 1.2f, 0.07f);
    auto noisy = addBuffers(speech, noise);
    auto finalOut = renderSubtractProximityFinishChain(sr, 256, noisy);

    if (!allFinite(finalOut)) {
        std::cerr << "[VXSuitePluginRegression] Combined chain produced non-finite samples\n";
        return false;
    }
    if (peakAbs(finalOut) > 1.05f) {
        std::cerr << "[VXSuitePluginRegression] Combined chain peak too high: peak=" << peakAbs(finalOut) << "\n";
        return false;
    }
    const float corr = std::abs(speechBandCorrelation(speech, finalOut, sr));
    if (corr < 0.45f) {
        std::cerr << "[VXSuitePluginRegression] Combined chain damaged speech coherence too much: |corr|=" << corr << "\n";
        return false;
    }
    return true;
}

bool testDeverbExtremeBlendStaysStable() {
    constexpr double sr = 48000.0;
    constexpr int tailSamples = static_cast<int>(sr * 0.35);

    auto speech = makeSpeechLike(sr, 0.35f);
    juce::AudioBuffer<float> input(2, speech.getNumSamples() + tailSamples);
    input.clear();
    for (int ch = 0; ch < std::min(2, speech.getNumChannels()); ++ch)
        input.copyFrom(ch, 0, speech, ch, 0, speech.getNumSamples());

    VXDeverbAudioProcessor deverb;
    deverb.prepareToPlay(sr, 256);
    setParamNormalized(deverb, "reduce", 1.0f);
    setParamNormalized(deverb, "body", 1.0f);
    auto out = render(deverb, input, 256);

    const float outPeak = peakAbs(out);
    if (!allFinite(out) || outPeak > 1.05f) {
        std::cerr << "[VXSuitePluginRegression] Deverb extreme settings produced unstable output: finite="
                  << allFinite(out) << " peak=" << outPeak << "\n";
        return false;
    }

    juce::AudioBuffer<float> tail(2, tailSamples);
    for (int ch = 0; ch < 2; ++ch)
        tail.copyFrom(ch, 0, out, ch, out.getNumSamples() - tailSamples, tailSamples);
    if (rms(tail) > 0.02f) {
        std::cerr << "[VXSuitePluginRegression] Deverb extreme Blend left too much sustained tail / buzzing: rms="
                  << rms(tail) << "\n";
        return false;
    }
    return true;
}

bool testDeverbStrongSettingActuallyReducesSyntheticRoomTail() {
    constexpr double sr = 48000.0;
    constexpr int blockSize = 256;
    const int tailStart = static_cast<int>(std::ceil(0.060 * sr));

    auto dry = makeSpeechLike(sr, 1.6f);
    auto room = makeDeverbSyntheticRoom(dry, sr, 1.0f);

    VXDeverbAudioProcessor deverb;
    deverb.prepareToPlay(sr, blockSize);
    setParamNormalized(deverb, "mode", 1.0f);
    setParamNormalized(deverb, "reduce", 1.0f);
    setParamNormalized(deverb, "body", 0.0f);
    auto out = render(deverb, room, blockSize);

    const float tailIn = tailRmsFromSample(room, tailStart);
    const float tailOut = tailRmsFromSample(out, tailStart);
    const float tailRatio = tailOut / std::max(tailIn, 1.0e-6f);
    const float residual = bestGainResidualRatioSkip(room, out, 4096);

    if (!allFinite(out) || peakAbs(out) > 1.05f) {
        std::cerr << "[VXSuitePluginRegression] Deverb strong synthetic-room case became unstable\n";
        return false;
    }
    if (tailRatio > 0.22f) {
        std::cerr << "[VXSuitePluginRegression] Deverb still leaves too much synthetic room tail at strong settings: ratio="
                  << tailRatio << "\n";
        return false;
    }
    if (residual > 0.97f) {
        std::cerr << "[VXSuitePluginRegression] Deverb strong synthetic-room case no longer changes the input enough: residual="
                  << residual << "\n";
        return false;
    }
    return true;
}

bool testDeverbLateTailIsReducedMoreThanEarlyBody() {
    constexpr double sr = 48000.0;
    constexpr int blockSize = 256;
    const int earlyWindow = static_cast<int>(std::ceil(0.040 * sr));
    const int tailStart = static_cast<int>(std::ceil(0.120 * sr));
    const int tailWindow = static_cast<int>(std::ceil(0.180 * sr));

    auto dry = makeSpeechLike(sr, 1.6f);
    auto room = makeDeverbSyntheticRoom(dry, sr, 1.2f);

    VXDeverbAudioProcessor deverb;
    deverb.prepareToPlay(sr, blockSize);
    setParamNormalized(deverb, "mode", 1.0f);
    setParamNormalized(deverb, "reduce", 0.90f);
    setParamNormalized(deverb, "body", 0.0f);
    const auto out = render(deverb, room, blockSize);

    const float earlyRatio = windowRms(out, 0, earlyWindow) / std::max(windowRms(room, 0, earlyWindow), 1.0e-6f);
    const float tailRatio = windowRms(out, tailStart, tailWindow) / std::max(windowRms(room, tailStart, tailWindow), 1.0e-6f);

    if (!(tailRatio < earlyRatio * 0.78f)) {
        std::cerr << "[VXSuitePluginRegression] Deverb no longer reduces late synthetic tail more strongly than early body: earlyRatio="
                  << earlyRatio << " tailRatio=" << tailRatio << "\n";
        return false;
    }

    return true;
}

bool testFullChainBlockSizeInvariance() {
    constexpr double sr = 48000.0;
    auto speech = makeSpeechLike(sr, 0.6f);
    auto noise = makeNoise(sr, 0.6f, 0.07f);
    auto noisy = addBuffers(speech, noise);

    const auto out64 = renderSubtractProximityFinishChain(sr, 64, noisy);
    if (out64.getNumSamples() <= 0)
        return false;
    const auto out512 = renderSubtractProximityFinishChain(sr, 512, noisy);
    if (out512.getNumSamples() <= 0)
        return false;

    const float corr = bufferCorrelationSkip(out64, out512, 2048);
    if (corr < 0.88f) {
        std::cerr << "[VXSuitePluginRegression] Full chain changed too much across host block sizes: corr=" << corr << "\n";
        return false;
    }
    return true;
}

bool testCombinedChainKeepsSilenceSilent() {
    constexpr double sr = 48000.0;
    juce::AudioBuffer<float> silence(2, static_cast<int>(sr * 0.75f));
    silence.clear();

    VXSubtractAudioProcessor subtract;
    subtract.prepareToPlay(sr, 256);
    setParamNormalized(subtract, "subtract", 0.80f);
    setParamNormalized(subtract, "protect", 0.40f);
    auto afterSubtract = render(subtract, silence);

    VXFinishAudioProcessor finish;
    finish.prepareToPlay(sr, 256);
    setParamNormalized(finish, "finish", 0.45f);
    setParamNormalized(finish, "body", 0.25f);
    setParamNormalized(finish, "gain", 0.20f);
    auto finalOut = render(finish, afterSubtract);

    if (rms(finalOut) > 1.0e-5f) {
        std::cerr << "[VXSuitePluginRegression] Combined chain raised silence too far above zero: rms=" << rms(finalOut) << "\n";
        return false;
    }
    return true;
}

bool testSpeechClarityDeclickDoesNotAmplifyClicks() {
    constexpr double sr = 48000.0;
    constexpr int blockSize = 256;
    auto input = makeSpeechLike(sr, 0.9f);
    const int clickSample = static_cast<int>(0.36 * sr);
    for (int ch = 0; ch < input.getNumChannels(); ++ch) {
        input.setSample(ch, clickSample - 1, -0.20f);
        input.setSample(ch, clickSample, 0.55f);
        input.setSample(ch, clickSample + 1, -0.18f);
    }
    const auto original = input;

    const auto windowPeak = [](const juce::AudioBuffer<float>& buffer, const int center, const int radius) {
        float peak = 0.0f;
        const int start = juce::jlimit(0, buffer.getNumSamples(), center - radius);
        const int end = juce::jlimit(start, buffer.getNumSamples(), center + radius + 1);
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
            const auto* data = buffer.getReadPointer(ch);
            for (int i = start; i < end; ++i)
                peak = std::max(peak, std::abs(data[i]));
        }
        return peak;
    };

    vxsuite::speech_clarity::DeClickDsp declick;
    declick.prepare(sr, input.getNumSamples(), input.getNumChannels());
    declick.process(input, { 1.0f, 1.0f });

    const float inPeak = windowPeak(original, clickSample, 12);
    const float outPeak = windowPeak(input, clickSample + declick.getLatencySamples(), 16);
    if (outPeak > inPeak * 1.02f) {
        std::cerr << "[VXSuitePluginRegression] Speech Clarity declick amplified a click: inPeak="
                  << inPeak << " outPeak=" << outPeak << "\n";
        return false;
    }
    return true;
}

bool testSpeechClarityWetPathAndListenDeltaAreAudible() {
    constexpr double sr = 48000.0;
    constexpr int blockSize = 256;
    auto input = makeSpeechLike(sr, 1.1f);

    for (int i = 0; i < input.getNumSamples(); ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(sr);
        const float sibEnv = (t > 0.32f && t < 0.44f) || (t > 0.68f && t < 0.80f) ? 1.0f : 0.0f;
        const float sibilance = 0.13f * sibEnv * std::sin(2.0f * juce::MathConstants<float>::pi * 7600.0f * t);
        for (int ch = 0; ch < input.getNumChannels(); ++ch)
            input.addSample(ch, i, sibilance);
    }

    for (const int clickSample : { static_cast<int>(0.52 * sr), static_cast<int>(0.87 * sr) }) {
        for (int ch = 0; ch < input.getNumChannels(); ++ch) {
            input.setSample(ch, clickSample - 1, -0.18f);
            input.setSample(ch, clickSample, 0.48f);
            input.setSample(ch, clickSample + 1, -0.16f);
        }
    }

    auto configure = [=](VXSpeechClarityAudioProcessor& processor, const bool listen) {
        processor.prepareToPlay(sr, blockSize);
        setParamNormalized(processor, "sibilance", 1.0f);
        setParamNormalized(processor, "plosive", 0.8f);
        setParamNormalized(processor, "breath", 0.0f);
        setParamNormalized(processor, "click", 1.0f);
        setParamNormalized(processor, "listen", listen ? 1.0f : 0.0f);
    };

    VXSpeechClarityAudioProcessor wetProcessor;
    configure(wetProcessor, false);
    const auto wet = render(wetProcessor, input, blockSize);

    VXSpeechClarityAudioProcessor listenProcessor;
    configure(listenProcessor, true);
    const auto listen = render(listenProcessor, input, blockSize);

    const auto diffRmsSkip = [](const juce::AudioBuffer<float>& a,
                                const juce::AudioBuffer<float>& b,
                                const int skipSamples) {
        double energy = 0.0;
        int count = 0;
        const int channels = std::min(a.getNumChannels(), b.getNumChannels());
        const int samples = std::min(a.getNumSamples(), b.getNumSamples());
        const int start = juce::jlimit(0, samples, skipSamples);
        for (int ch = 0; ch < channels; ++ch) {
            const auto* aa = a.getReadPointer(ch);
            const auto* bb = b.getReadPointer(ch);
            for (int i = start; i < samples; ++i) {
                const float diff = aa[i] - bb[i];
                energy += static_cast<double>(diff) * diff;
                ++count;
            }
        }
        return count > 0 ? static_cast<float>(std::sqrt(energy / static_cast<double>(count))) : 0.0f;
    };

    const int skip = wetProcessor.getLatencySamples() + 2048;
    const float wetDeltaRms = diffRmsSkip(wet, input, skip);
    const float wetDeltaPeak = maxAbsDiffSkip(input, wet, skip);
    const float listenRms = rmsSkip(listen, skip);
    auto reconstructed = addBuffers(input, listen);
    const float reconstructionError = maxAbsDiffSkip(wet, reconstructed, skip);

    if (!allFinite(wet)
        || !allFinite(listen)
        || wetDeltaRms <= 2.0e-4f
        || wetDeltaPeak <= 2.0e-3f
        || listenRms <= wetDeltaRms * 0.45f
        || reconstructionError > 0.035f) {
        std::cerr << "[VXSuitePluginRegression] Speech Clarity wet path/listen delta is not measurably active: wetDeltaRms="
                  << wetDeltaRms << " wetDeltaPeak=" << wetDeltaPeak
                  << " listenRms=" << listenRms
                  << " reconstructionError=" << reconstructionError << "\n";
        return false;
    }

    return true;
}

bool testFinishStrongSettingsAreAudibleButBounded() {
    constexpr double sr = 48000.0;
    auto input = makeSpeechLike(sr, 1.0f);

    VXFinishAudioProcessor finishLow;
    finishLow.prepareToPlay(sr, 256);
    setParamNormalized(finishLow, "finish", 0.15f);
    setParamNormalized(finishLow, "body", 0.15f);
    setParamNormalized(finishLow, "gain", 0.15f);
    const auto lowOut = render(finishLow, input, 256);

    VXFinishAudioProcessor finishHigh;
    finishHigh.prepareToPlay(sr, 256);
    setParamNormalized(finishHigh, "finish", 0.80f);
    setParamNormalized(finishHigh, "body", 0.80f);
    setParamNormalized(finishHigh, "gain", 0.70f);
    const auto highOut = render(finishHigh, input, 256);

    const float diff = maxAbsDiffSkip(lowOut, highOut, 512);
    if (diff < 0.01f) {
        std::cerr << "[VXSuitePluginRegression] Finish controls still do not move enough between low and high settings\n";
        return false;
    }
    if (!allFinite(highOut) || peakAbs(highOut) > 1.02f) {
        std::cerr << "[VXSuitePluginRegression] Finish strong settings clipped or became unstable\n";
        return false;
    }
    return true;
}

bool testFinishGainIsBipolarAroundCenter() {
    constexpr double sr = 48000.0;
    auto input = makeSpeechLike(sr, 1.0f);

    VXFinishAudioProcessor finishCut;
    finishCut.prepareToPlay(sr, 256);
    setParamNormalized(finishCut, "finish", 0.40f);
    setParamNormalized(finishCut, "body", 0.35f);
    setParamNormalized(finishCut, "gain", 0.20f);
    const auto cutOut = render(finishCut, input, 256);

    VXFinishAudioProcessor finishMid;
    finishMid.prepareToPlay(sr, 256);
    setParamNormalized(finishMid, "finish", 0.40f);
    setParamNormalized(finishMid, "body", 0.35f);
    setParamNormalized(finishMid, "gain", 0.50f);
    const auto midOut = render(finishMid, input, 256);

    VXFinishAudioProcessor finishBoost;
    finishBoost.prepareToPlay(sr, 256);
    setParamNormalized(finishBoost, "finish", 0.40f);
    setParamNormalized(finishBoost, "body", 0.35f);
    setParamNormalized(finishBoost, "gain", 0.80f);
    const auto boostOut = render(finishBoost, input, 256);

    const float cutRms = rms(cutOut);
    const float midRms = rms(midOut);
    const float boostRms = rms(boostOut);
    if (!(cutRms + 1.0e-4f < midRms && midRms + 1.0e-4f < boostRms)) {
        std::cerr << "[VXSuitePluginRegression] Finish gain is not behaving like a centered bipolar control\n";
        return false;
    }
    if (!allFinite(boostOut) || peakAbs(boostOut) > 1.02f) {
        std::cerr << "[VXSuitePluginRegression] Finish gain boost clipped or became unstable\n";
        return false;
    }
    return true;
}

bool testFinishResetIsDeterministic() {
    constexpr double sr = 48000.0;
    auto input = makeSpeechLike(sr, 1.0f);

    VXFinishAudioProcessor finish;
    finish.prepareToPlay(sr, 256);
    setParamNormalized(finish, "finish", 0.55f);
    setParamNormalized(finish, "body", 0.72f);
    setParamNormalized(finish, "gain", 0.5f);
    const auto first = render(finish, input, 256);

    finish.reset();
    setParamNormalized(finish, "finish", 0.55f);
    setParamNormalized(finish, "body", 0.72f);
    setParamNormalized(finish, "gain", 0.5f);
    const auto second = render(finish, input, 256);

    if (maxAbsDiffSkip(first, second, 512) > 1.0e-4f) {
        std::cerr << "[VXSuitePluginRegression] Finish reset no longer restores deterministic compressor state\n";
        return false;
    }
    if (!allFinite(second) || peakAbs(second) > 1.02f) {
        std::cerr << "[VXSuitePluginRegression] Finish reset path became unstable\n";
        return false;
    }
    return true;
}

bool testFinishZeroAmountIsIdleAndTransparent() {
    constexpr double sr = 48000.0;
    auto input = makeSpeechLike(sr, 1.0f);
    input.applyGain(0.18f);

    VXFinishAudioProcessor finish;
    finish.prepareToPlay(sr, 256);
    if (auto* finishParam = finish.getValueTreeState().getParameter("finish")) {
        if (std::abs(finishParam->getValue()) > 1.0e-6f) {
            std::cerr << "[VXSuitePluginRegression] Finish default amount is no longer zero\n";
            return false;
        }
    }
    const auto out = render(finish, input, 256);

    const float diff = maxAbsDiff(input, out);
    if (diff > 1.0e-5f) {
        std::cerr << "[VXSuitePluginRegression] Finish default state is no longer transparent at zero amount: diff="
                  << diff << "\n";
        return false;
    }
    if (finish.getActivityLight(0) > 1.0e-4f || finish.getActivityLight(1) > 1.0e-4f) {
        std::cerr << "[VXSuitePluginRegression] Finish opto telemetry stayed active at zero amount\n";
        return false;
    }
    return true;
}

bool testOptoCompZeroAmountIsIdleAndTransparent() {
    constexpr double sr = 48000.0;
    auto input = makeSpeechLike(sr, 1.0f);
    input.applyGain(0.18f);

    VXOptoCompAudioProcessor opto;
    opto.prepareToPlay(sr, 256);
    if (auto* peakReductionParam = opto.getValueTreeState().getParameter("peak_reduction")) {
        if (std::abs(peakReductionParam->getValue()) > 1.0e-6f) {
            std::cerr << "[VXSuitePluginRegression] OptoComp default amount is no longer zero\n";
            return false;
        }
    }
    const auto out = render(opto, input, 256);

    const float diff = maxAbsDiff(input, out);
    if (diff > 1.0e-5f) {
        std::cerr << "[VXSuitePluginRegression] OptoComp default state is no longer transparent at zero amount: diff="
                  << diff << "\n";
        return false;
    }
    if (opto.getActivityLight(0) > 1.0e-4f || opto.getActivityLight(1) > 1.0e-4f) {
        std::cerr << "[VXSuitePluginRegression] OptoComp telemetry stayed active at zero amount\n";
        return false;
    }
    return true;
}

bool testFinishAndOptoCompBrightStressStayTruePeakSafe() {
    constexpr double sr = 48000.0;
    auto bright = addBuffers(makeSine(sr, 1.0f, 4200.0f, 0.16f),
                             makeSine(sr, 1.0f, 9800.0f, 0.14f));
    bright = addBuffers(bright, makeNoise(sr, 1.0f, 0.02f));
    for (int i = 0; i < bright.getNumSamples(); ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(sr);
        const float burst = std::max(0.0f, std::sin(2.0f * juce::MathConstants<float>::pi * 5.5f * t));
        const float click = std::sin(2.0f * juce::MathConstants<float>::pi * 23.0f * t) > 0.985f ? 0.24f : 0.0f;
        const float env = 0.50f + 0.50f * burst;
        for (int ch = 0; ch < bright.getNumChannels(); ++ch)
            bright.setSample(ch, i, bright.getSample(ch, i) * env + click);
    }
    const float inputPeak = peakAbs(bright);
    if (inputPeak > 1.0e-6f)
        bright.applyGain(0.90f / inputPeak);

    VXFinishAudioProcessor finish;
    finish.prepareToPlay(sr, 256);
    setParamNormalized(finish, "finish", 0.82f);
    setParamNormalized(finish, "body", 0.68f);
    setParamNormalized(finish, "gain", 0.62f);
    const auto finishOut = render(finish, bright, 256);

    VXOptoCompAudioProcessor opto;
    opto.prepareToPlay(sr, 256);
    setParamNormalized(opto, "peak_reduction", 0.82f);
    setParamNormalized(opto, "body", 0.60f);
    setParamNormalized(opto, "gain", 0.62f);
    const auto optoOut = render(opto, bright, 256);

    const float finishTruePeak = estimatedTruePeak(finishOut);
    const float optoTruePeak = estimatedTruePeak(optoOut);
    if (!allFinite(finishOut) || finishTruePeak > 1.02f) {
        std::cerr << "[VXSuitePluginRegression] Finish no longer keeps bright transient stress input true-peak safe: truePeak="
                  << finishTruePeak << "\n";
        return false;
    }
    if (!allFinite(optoOut) || optoTruePeak > 1.02f) {
        std::cerr << "[VXSuitePluginRegression] OptoComp no longer keeps bright transient stress input true-peak safe: truePeak="
                  << optoTruePeak << "\n";
        return false;
    }
    return true;
}

bool testToneCenterIsIdentityAndExtremesStayBounded() {
    constexpr double sr = 48000.0;
    auto input = makeSpeechLike(sr, 1.0f);

    VXToneAudioProcessor toneFlat;
    toneFlat.prepareToPlay(sr, 256);
    setParamNormalized(toneFlat, "bass", 0.5f);
    setParamNormalized(toneFlat, "treble", 0.5f);
    const auto flatOut = render(toneFlat, input, 256);
    if (maxAbsDiff(input, flatOut) > 1.0e-6f) {
        std::cerr << "[VXSuitePluginRegression] Tone center should be identity\n";
        return false;
    }

    VXToneAudioProcessor toneExtreme;
    toneExtreme.prepareToPlay(sr, 256);
    setParamNormalized(toneExtreme, "bass", 1.0f);
    setParamNormalized(toneExtreme, "treble", 1.0f);
    const auto boostedOut = render(toneExtreme, input, 256);
    if (!allFinite(boostedOut) || peakAbs(boostedOut) > 1.02f) {
        std::cerr << "[VXSuitePluginRegression] Tone extreme boost clipped or became unstable\n";
        return false;
    }
    if (maxAbsDiffSkip(input, boostedOut, 128) < 0.01f) {
        std::cerr << "[VXSuitePluginRegression] Tone extreme boost was too subtle\n";
        return false;
    }
    return true;
}

bool testStackedProximityToneFinishChainKeepsHeadroom() {
    constexpr double sr = 48000.0;
    auto input = addBuffers(makeSpeechLike(sr, 1.0f),
                            makeSine(sr, 1.0f, 120.0f, 0.11f));
    input = addBuffers(input, makeSine(sr, 1.0f, 3400.0f, 0.05f));
    input = addBuffers(input, makeNoise(sr, 1.0f, 0.015f));

    const float inputPeak = peakAbs(input);
    if (inputPeak > 1.0e-6f)
        input.applyGain(0.94f / inputPeak);

    const auto out = renderProximityToneFinishChain(sr, 256, input);
    if (!allFinite(out) || peakAbs(out) > 0.995f) {
        std::cerr << "[VXSuitePluginRegression] Stacked proximity/tone/finish chain exceeded safe output headroom: peak="
                  << peakAbs(out) << "\n";
        return false;
    }
    if (maxAbsDiffSkip(input, out, 256) < 0.015f) {
        std::cerr << "[VXSuitePluginRegression] Stacked proximity/tone/finish chain became too subtle\n";
        return false;
    }
    return true;
}

bool testFrameworkOutputTrimmerStaysMostlyIdleOnNominalStrongSettings() {
    constexpr double sr = 48000.0;

    auto shapedInput = addBuffers(makeSpeechLike(sr, 1.0f), makeNoise(sr, 1.0f, 0.015f));
    const float shapedPeak = peakAbs(shapedInput);
    if (shapedPeak > 1.0e-6f)
        shapedInput.applyGain(0.92f / shapedPeak);

    VXToneAudioProcessor tone;
    tone.prepareToPlay(sr, 256);
    setParamNormalized(tone, "bass", 0.78f);
    setParamNormalized(tone, "treble", 0.70f);
    const auto toneOut = render(tone, shapedInput, 256);
    juce::ignoreUnused(toneOut);
    if (tone.getOutputSafetyTrimMaxReductionDb() > 0.45f) {
        std::cerr << "[VXSuitePluginRegression] Tone leaned too hard on framework output trimming: reductionDb="
                  << tone.getOutputSafetyTrimMaxReductionDb() << "\n";
        return false;
    }

    VXProximityAudioProcessor proximity;
    proximity.prepareToPlay(sr, 256);
    setParamNormalized(proximity, "closer", 0.76f);
    setParamNormalized(proximity, "air", 0.66f);
    const auto proximityOut = render(proximity, shapedInput, 256);
    juce::ignoreUnused(proximityOut);
    if (proximity.getOutputSafetyTrimMaxReductionDb() > 0.45f) {
        std::cerr << "[VXSuitePluginRegression] Proximity leaned too hard on framework output trimming: reductionDb="
                  << proximity.getOutputSafetyTrimMaxReductionDb() << "\n";
        return false;
    }

    auto bright = addBuffers(makeSine(sr, 1.0f, 4200.0f, 0.16f),
                             makeSine(sr, 1.0f, 9800.0f, 0.14f));
    bright = addBuffers(bright, makeNoise(sr, 1.0f, 0.02f));
    const float brightPeak = peakAbs(bright);
    if (brightPeak > 1.0e-6f)
        bright.applyGain(0.90f / brightPeak);

    VXFinishAudioProcessor finish;
    finish.prepareToPlay(sr, 256);
    setParamNormalized(finish, "finish", 0.82f);
    setParamNormalized(finish, "body", 0.68f);
    setParamNormalized(finish, "gain", 0.62f);
    const auto finishOut = render(finish, bright, 256);
    juce::ignoreUnused(finishOut);
    if (finish.getOutputSafetyTrimMaxReductionDb() > 0.30f) {
        std::cerr << "[VXSuitePluginRegression] Finish relied on framework output trimming instead of its own gain staging: reductionDb="
                  << finish.getOutputSafetyTrimMaxReductionDb() << "\n";
        return false;
    }

    auto rebalanceInput = makeRebalanceStereoConfidenceInput(sr, 1.4f);
    rebalanceInput = addBuffers(rebalanceInput, makeNoise(sr, 1.4f, 0.02f));
    const float rebalancePeak = peakAbs(rebalanceInput);
    if (rebalancePeak > 1.0e-6f)
        rebalanceInput.applyGain(0.95f / rebalancePeak);

    VXRebalanceAudioProcessor rebalance;
    rebalance.prepareToPlay(sr, 256);
    setParamNormalized(rebalance, "recordingType", 0.0f);
    setParamNormalized(rebalance, "vocals", 0.90f);
    setParamNormalized(rebalance, "drums", 0.0f);
    setParamNormalized(rebalance, "bass", 0.72f);
    setParamNormalized(rebalance, "guitar", 0.0f);
    setParamNormalized(rebalance, "other", 0.0f);
    setParamNormalized(rebalance, "strength", 0.85f);
    const auto rebalanceOut = render(rebalance, rebalanceInput, 256);
    juce::ignoreUnused(rebalanceOut);
    if (rebalance.getOutputSafetyTrimMaxReductionDb() > 0.60f) {
        std::cerr << "[VXSuitePluginRegression] Rebalance still leaned too hard on framework output trimming: reductionDb="
                  << rebalance.getOutputSafetyTrimMaxReductionDb() << "\n";
        return false;
    }

    return true;
}

bool testProductLocalOutputTrimmersStayMostlyIdleOnNominalStrongSettings() {
    constexpr double sr = 48000.0;

    auto shapedInput = addBuffers(makeSpeechLike(sr, 1.0f), makeNoise(sr, 1.0f, 0.015f));
    const float shapedPeak = peakAbs(shapedInput);
    if (shapedPeak > 1.0e-6f)
        shapedInput.applyGain(0.92f / shapedPeak);

    auto bright = addBuffers(makeSine(sr, 1.0f, 4200.0f, 0.16f),
                             makeSine(sr, 1.0f, 9800.0f, 0.14f));
    bright = addBuffers(bright, makeNoise(sr, 1.0f, 0.02f));
    const float brightPeak = peakAbs(bright);
    if (brightPeak > 1.0e-6f)
        bright.applyGain(0.90f / brightPeak);

    VXToneAudioProcessor tone;
    tone.prepareToPlay(sr, 256);
    setParamNormalized(tone, "bass", 0.78f);
    setParamNormalized(tone, "treble", 0.70f);
    const auto toneOut = render(tone, shapedInput, 256);
    juce::ignoreUnused(toneOut);
    if (tone.getLocalOutputTrimMaxReductionDb() > 1.35f) {
        std::cerr << "[VXSuitePluginRegression] Tone local output trimmer engaged too heavily: reductionDb="
                  << tone.getLocalOutputTrimMaxReductionDb() << "\n";
        return false;
    }

    VXProximityAudioProcessor proximity;
    proximity.prepareToPlay(sr, 256);
    setParamNormalized(proximity, "closer", 0.76f);
    setParamNormalized(proximity, "air", 0.66f);
    const auto proximityOut = render(proximity, shapedInput, 256);
    juce::ignoreUnused(proximityOut);
    if (proximity.getLocalOutputTrimMaxReductionDb() > 1.35f) {
        std::cerr << "[VXSuitePluginRegression] Proximity local output trimmer engaged too heavily: reductionDb="
                  << proximity.getLocalOutputTrimMaxReductionDb() << "\n";
        return false;
    }

    VXFinishAudioProcessor finish;
    finish.prepareToPlay(sr, 256);
    setParamNormalized(finish, "finish", 0.82f);
    setParamNormalized(finish, "body", 0.68f);
    setParamNormalized(finish, "gain", 0.62f);
    const auto finishOut = render(finish, bright, 256);
    juce::ignoreUnused(finishOut);
    if (finish.getLocalOutputTrimMaxReductionDb() > 0.45f) {
        std::cerr << "[VXSuitePluginRegression] Finish local output trimmer engaged too heavily: reductionDb="
                  << finish.getLocalOutputTrimMaxReductionDb() << "\n";
        return false;
    }

    VXOptoCompAudioProcessor opto;
    opto.prepareToPlay(sr, 256);
    setParamNormalized(opto, "peak_reduction", 0.82f);
    setParamNormalized(opto, "body", 0.60f);
    setParamNormalized(opto, "gain", 0.62f);
    const auto optoOut = render(opto, bright, 256);
    juce::ignoreUnused(optoOut);
    if (opto.getLocalOutputTrimMaxReductionDb() > 0.45f) {
        std::cerr << "[VXSuitePluginRegression] OptoComp local output trimmer engaged too heavily: reductionDb="
                  << opto.getLocalOutputTrimMaxReductionDb() << "\n";
        return false;
    }

    auto noisy = addBuffers(shapedInput, makeNoise(sr, 1.0f, 0.05f));
    const float noisyPeak = peakAbs(noisy);
    if (noisyPeak > 1.0e-6f)
        noisy.applyGain(0.92f / noisyPeak);

    VXDenoiserAudioProcessor denoiser;
    denoiser.prepareToPlay(sr, 256);
    setParamNormalized(denoiser, "clean", 0.90f);
    setParamNormalized(denoiser, "guard", 0.65f);
    const auto denoiserOut = render(denoiser, noisy, 256);
    juce::ignoreUnused(denoiserOut);


    return true;
}

bool testLevelerZeroIsTransparentAndIdle() {
    constexpr double sr = 48000.0;
    auto input = makeSpeechLike(sr, 1.0f);
    input.applyGain(0.22f);

    VXLevelerAudioProcessor leveler;
    leveler.prepareToPlay(sr, 256);
    const auto out = render(leveler, input, 256);

    const float diff = maxAbsDiff(input, out);
    if (diff > 1.0e-5f) {
        std::cerr << "[VXSuitePluginRegression] Perform default state is no longer transparent at zero settings: diff="
                  << diff << "\n";
        return false;
    }
    if (leveler.getActivityLight(0) > 1.0e-4f
        || leveler.getActivityLight(1) > 1.0e-4f
        || leveler.getActivityLight(2) > 1.0e-4f) {
        std::cerr << "[VXSuitePluginRegression] Leveler telemetry stayed active at zero settings\n";
        return false;
    }
    return true;
}

bool testLevelerImprovesLevelConsistencyOnHotInstrumentMix() {
    constexpr double sr = 48000.0;
    auto speech = makeSpeechLike(sr, 1.2f);
    speech.applyGain(0.42f);
    auto instrument = makePerformInstrumentInput(sr, 1.2f);
    instrument.applyGain(0.92f);
    auto mix = addBuffers(speech, instrument);

    const float peak = peakAbs(mix);
    if (peak > 1.0e-6f)
        mix.applyGain(0.88f / peak);

    VXLevelerAudioProcessor leveler;
    leveler.prepareToPlay(sr, 256);
    setParamNormalized(leveler, "mode", 1.0f);
    setParamNormalized(leveler, "analysisMode", 0.0f);
    setParamNormalized(leveler, "level", 1.0f);
    setParamNormalized(leveler, "control", 0.85f);
    const auto out = render(leveler, mix, 256);

    if (!allFinite(out) || peakAbs(out) > 1.02f) {
        std::cerr << "[VXSuitePluginRegression] Leveler hot-mix recovery path became unstable\n";
        return false;
    }

    const float drySpread = windowedLevelSpreadDb(mix, sr);
    const float wetSpread = windowedLevelSpreadDb(out, sr);
    if (!(wetSpread < drySpread * 0.98f)) {
        std::cerr << "[VXSuitePluginRegression] Leveler no longer improves level consistency on a hot instrument mix: drySpread="
                  << drySpread << " wetSpread=" << wetSpread << "\n";
        return false;
    }

    return true;
}

bool testLevelerGeneralTracksPresenceMoreThanBassAtEqualDrive() {
    constexpr double sr = 48000.0;
    constexpr int skip = 4096;

    auto bass = addBuffers(makeSine(sr, 1.0f, 90.0f, 0.14f),
                           makeSine(sr, 1.0f, 220.0f, 0.11f));
    auto presence = addBuffers(makeSine(sr, 1.0f, 700.0f, 0.11f),
                               makeSine(sr, 1.0f, 2200.0f, 0.12f));
    presence = addBuffers(presence, makeNoise(sr, 1.0f, 0.01f));

    VXLevelerAudioProcessor leveler;
    leveler.prepareToPlay(sr, 256);
    setParamNormalized(leveler, "mode", 1.0f);
    setParamNormalized(leveler, "analysisMode", 0.0f);
    setParamNormalized(leveler, "level", 1.0f);
    setParamNormalized(leveler, "control", 0.85f);

    const float bassRatio = rmsSkip(render(leveler, bass, 256), skip)
        / std::max(rmsSkip(bass, skip), 1.0e-6f);

    leveler.reset();
    setParamNormalized(leveler, "mode", 1.0f);
    setParamNormalized(leveler, "analysisMode", 0.0f);
    setParamNormalized(leveler, "level", 1.0f);
    setParamNormalized(leveler, "control", 0.85f);

    const float presenceRatio = rmsSkip(render(leveler, presence, 256), skip)
        / std::max(rmsSkip(presence, skip), 1.0e-6f);

    if (!(presenceRatio < bassRatio * 0.99f)) {
        std::cerr << "[VXSuitePluginRegression] Leveler mix target no longer rides upper-mid dense material at least slightly harder than bass-heavy material: bassRatio="
                  << bassRatio << " presenceRatio=" << presenceRatio << "\n";
        return false;
    }

    return true;
}

bool testLevelerOfflineAnalysisPersistsAcrossStateRestore() {
    constexpr double sr = 48000.0;
    auto input = addBuffers(makeSpeechLike(sr, 1.0f), makeNoise(sr, 1.0f, 0.02f));

    VXLevelerAudioProcessor original;
    original.prepareToPlay(sr, 256);
    setParamNormalized(original, "mode", 1.0f);
    setParamNormalized(original, "analysisMode", 1.0f);
    setParamNormalized(original, "level", 0.78f);
    setParamNormalized(original, "control", 0.64f);

    vxsuite::leveler::OfflineAnalysisResult analysis {};
    analysis.sampleRate = sr;
    analysis.blockSize = 256;
    analysis.globalMedianDb = -23.5f;
    analysis.globalUpperDb = -18.2f;
    analysis.globalDynamicRangeDb = 7.4f;
    analysis.targetCurveDb = { -25.0f, -24.5f, -23.9f, -22.8f, -22.4f, -21.9f, -21.6f, -21.2f };
    original.setOfflineAnalysis(analysis);

    juce::MemoryBlock state;
    original.getStateInformation(state);
    const auto originalOut = render(original, input, 256);

    VXLevelerAudioProcessor restored;
    restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    restored.prepareToPlay(sr, 256);

    if (!restored.isLearnReady()) {
        std::cerr << "[VXSuitePluginRegression] Leveler lost offline analysis readiness after state restore\n";
        return false;
    }

    const auto restoredOut = render(restored, input, 256);
    if (!allFinite(restoredOut)) {
        std::cerr << "[VXSuitePluginRegression] Leveler restored offline analysis produced non-finite output\n";
        return false;
    }

    const float diff = maxAbsDiff(originalOut, restoredOut);
    if (diff > 1.0e-4f) {
        std::cerr << "[VXSuitePluginRegression] Leveler offline analysis restore changed output too much: diff="
                  << diff << "\n";
        return false;
    }

    return true;
}

bool testProximityExtremeIsBoundedAndAdditive() {
    constexpr double sr = 48000.0;
    auto input = makeSpeechLike(sr, 1.0f);

    VXProximityAudioProcessor proximity;
    proximity.prepareToPlay(sr, 256);
    setParamNormalized(proximity, "closer", 1.0f);
    setParamNormalized(proximity, "air", 1.0f);
    const auto out = render(proximity, input, 256);

    if (!allFinite(out) || peakAbs(out) > 1.02f) {
        std::cerr << "[VXSuitePluginRegression] Proximity extreme setting clipped or became unstable\n";
        return false;
    }
    if (maxAbsDiffSkip(input, out, 128) < 0.01f) {
        std::cerr << "[VXSuitePluginRegression] Proximity extreme setting was too subtle\n";
        return false;
    }
    return true;
}

bool testProximityDefaultsAreNearNeutral() {
    constexpr double sr = 48000.0;
    auto input = makeSpeechLike(sr, 1.0f);

    VXProximityAudioProcessor proximity;
    proximity.prepareToPlay(sr, 256);
    const auto out = render(proximity, input, 256);

    if (maxAbsDiffSkip(input, out, 128) > 2.0e-3f) {
        std::cerr << "[VXSuitePluginRegression] Proximity defaults are no longer near-neutral\n";
        return false;
    }
    return true;
}

bool testProximityCloserKeepsLoudnessSteadierAcrossSources() {
    constexpr double sr = 48000.0;
    constexpr int skip = 4096;

    auto bassHeavy = addBuffers(makeSine(sr, 1.0f, 110.0f, 0.12f),
                                makeSine(sr, 1.0f, 260.0f, 0.05f));
    auto presenceHeavy = addBuffers(makeSine(sr, 1.0f, 3200.0f, 0.08f),
                                    makeSine(sr, 1.0f, 7800.0f, 0.04f));

    VXProximityAudioProcessor proximityBass;
    proximityBass.prepareToPlay(sr, 256);
    setParamNormalized(proximityBass, "closer", 1.0f);
    setParamNormalized(proximityBass, "air", 0.25f);
    const auto bassOut = render(proximityBass, bassHeavy, 256);

    VXProximityAudioProcessor proximityPresence;
    proximityPresence.prepareToPlay(sr, 256);
    setParamNormalized(proximityPresence, "closer", 1.0f);
    setParamNormalized(proximityPresence, "air", 0.25f);
    const auto presenceOut = render(proximityPresence, presenceHeavy, 256);

    const float bassRatio = rmsSkip(bassOut, skip) / std::max(rmsSkip(bassHeavy, skip), 1.0e-6f);
    const float presenceRatio = rmsSkip(presenceOut, skip) / std::max(rmsSkip(presenceHeavy, skip), 1.0e-6f);

    if (std::abs(bassRatio - presenceRatio) > 0.18f) {
        std::cerr << "[VXSuitePluginRegression] Proximity closer loudness is no longer steady enough across source balances: bassRatio="
                  << bassRatio << " presenceRatio=" << presenceRatio << "\n";
        return false;
    }
    return true;
}

bool testProximityPhysicsBasedModelActsMonotonicallyWithCloser() {
    constexpr double sr = 48000.0;
    auto input = makeSpeechLike(sr, 1.0f);

    VXProximityAudioProcessor proximityFar;
    proximityFar.prepareToPlay(sr, 256);
    setParamNormalized(proximityFar, "closer", 0.0f);
    setParamNormalized(proximityFar, "air", 0.0f);
    const auto farOut = render(proximityFar, input, 256);

    VXProximityAudioProcessor proximityMid;
    proximityMid.prepareToPlay(sr, 256);
    setParamNormalized(proximityMid, "closer", 0.5f);
    setParamNormalized(proximityMid, "air", 0.0f);
    const auto midOut = render(proximityMid, input, 256);

    VXProximityAudioProcessor proximityClose;
    proximityClose.prepareToPlay(sr, 256);
    setParamNormalized(proximityClose, "closer", 1.0f);
    setParamNormalized(proximityClose, "air", 0.0f);
    const auto closeOut = render(proximityClose, input, 256);

    const float farBassEnergy = rmsSkip(farOut, 256);
    const float midBassEnergy = rmsSkip(midOut, 256);
    const float closeBassEnergy = rmsSkip(closeOut, 256);

    if (midBassEnergy <= farBassEnergy || closeBassEnergy <= midBassEnergy) {
        std::cerr << "[VXSuitePluginRegression] Proximity physics model does not increase bass monotonically with closer: "
                  << "far=" << farBassEnergy << " mid=" << midBassEnergy << " close=" << closeBassEnergy << "\n";
        return false;
    }
    return true;
}

bool testProximityBassBoostGrowthIsNonlinearWithCloser() {
    constexpr double sr = 48000.0;
    auto bassSignal = makeSine(sr, 1.0f, 150.0f, 0.10f);

    VXProximityAudioProcessor ProximityClassic5;
    ProximityClassic5.prepareToPlay(sr, 256);
    setParamNormalized(ProximityClassic5, "closer", 0.25f);
    setParamNormalized(ProximityClassic5, "air", 0.0f);
    const auto out25 = render(ProximityClassic5, bassSignal, 256);

    VXProximityAudioProcessor proximity50;
    proximity50.prepareToPlay(sr, 256);
    setParamNormalized(proximity50, "closer", 0.50f);
    setParamNormalized(proximity50, "air", 0.0f);
    const auto out50 = render(proximity50, bassSignal, 256);

    VXProximityAudioProcessor proximity75;
    proximity75.prepareToPlay(sr, 256);
    setParamNormalized(proximity75, "closer", 0.75f);
    setParamNormalized(proximity75, "air", 0.0f);
    const auto out75 = render(proximity75, bassSignal, 256);

    const float input_rms = rms(bassSignal);
    const float step1_gain = rms(out25) / std::max(input_rms, 1.0e-6f);
    const float step2_gain = rms(out50) / std::max(input_rms, 1.0e-6f);
    const float step3_gain = rms(out75) / std::max(input_rms, 1.0e-6f);

    const float step1to2_increase = step2_gain / std::max(step1_gain, 1.0e-6f);
    const float step2to3_increase = step3_gain / std::max(step2_gain, 1.0e-6f);

    if (step1to2_increase < 1.05f || step2to3_increase < 1.02f) {
        std::cerr << "[VXSuitePluginRegression] Proximity bass boost growth too weak for physics model: "
                  << "0->50% increase=" << step1to2_increase << "x 50->75% increase=" << step2to3_increase << "x\n";
        return false;
    }
    return true;
}

bool testProximityVoiceVsGeneralPatternFactorIsEvident() {
    constexpr double sr = 48000.0;
    auto bassSignal = makeSine(sr, 1.0f, 180.0f, 0.10f);

    VXProximityAudioProcessor proximityVoice;
    proximityVoice.prepareToPlay(sr, 256);
    setParamNormalized(proximityVoice, "mode", 0.0f);
    setParamNormalized(proximityVoice, "closer", 1.0f);
    setParamNormalized(proximityVoice, "air", 0.0f);
    const auto voiceOut = render(proximityVoice, bassSignal, 256);

    VXProximityAudioProcessor proximityGeneral;
    proximityGeneral.prepareToPlay(sr, 256);
    setParamNormalized(proximityGeneral, "mode", 1.0f);
    setParamNormalized(proximityGeneral, "closer", 1.0f);
    setParamNormalized(proximityGeneral, "air", 0.0f);
    const auto generalOut = render(proximityGeneral, bassSignal, 256);

    const float input_rms = rms(bassSignal);
    const float voiceGain = rms(voiceOut) / std::max(input_rms, 1.0e-6f);
    const float generalGain = rms(generalOut) / std::max(input_rms, 1.0e-6f);

    if (voiceGain > generalGain) {
        std::cerr << "[VXSuitePluginRegression] Proximity general mode should have higher bass gain (0.8x pattern vs 0.5x cardioid): "
                  << "voice=" << voiceGain << "x general=" << generalGain << "x\n";
        return false;
    }

    const float gainRatioDifference = std::abs(voiceGain - generalGain) / std::max(voiceGain, 1.0e-6f);
    if (gainRatioDifference < 0.08f) {
        std::cerr << "[VXSuitePluginRegression] Proximity voice/general gain difference too subtle for pattern factor: "
                  << "voice=" << voiceGain << "x general=" << generalGain << "x (diff=" << gainRatioDifference << ")\n";
        return false;
    }
    return true;
}

bool testToneAndProximityModesAreClearlyDifferentAtExtremes() {
    constexpr double sr = 48000.0;
    auto input = makeSpeechLike(sr, 1.0f);

    VXToneAudioProcessor toneVoice;
    toneVoice.prepareToPlay(sr, 256);
    setParamNormalized(toneVoice, "mode", 0.0f);
    setParamNormalized(toneVoice, "bass", 1.0f);
    setParamNormalized(toneVoice, "treble", 1.0f);
    const auto toneVoiceOut = render(toneVoice, input, 256);

    VXToneAudioProcessor toneGeneral;
    toneGeneral.prepareToPlay(sr, 256);
    setParamNormalized(toneGeneral, "mode", 1.0f);
    setParamNormalized(toneGeneral, "bass", 1.0f);
    setParamNormalized(toneGeneral, "treble", 1.0f);
    const auto toneGeneralOut = render(toneGeneral, input, 256);

    if (maxAbsDiffSkip(toneVoiceOut, toneGeneralOut, 128) < 0.02f) {
        std::cerr << "[VXSuitePluginRegression] Tone voice/general extremes are still too similar\n";
        return false;
    }

    VXProximityAudioProcessor proximityVoice;
    proximityVoice.prepareToPlay(sr, 256);
    setParamNormalized(proximityVoice, "mode", 0.0f);
    setParamNormalized(proximityVoice, "closer", 1.0f);
    setParamNormalized(proximityVoice, "air", 1.0f);
    const auto proximityVoiceOut = render(proximityVoice, input, 256);

    VXProximityAudioProcessor proximityGeneral;
    proximityGeneral.prepareToPlay(sr, 256);
    setParamNormalized(proximityGeneral, "mode", 1.0f);
    setParamNormalized(proximityGeneral, "closer", 1.0f);
    setParamNormalized(proximityGeneral, "air", 1.0f);
    const auto proximityGeneralOut = render(proximityGeneral, input, 256);

    if (maxAbsDiffSkip(proximityVoiceOut, proximityGeneralOut, 128) < 0.015f) {
        std::cerr << "[VXSuitePluginRegression] Proximity voice/general extremes are still too similar\n";
        return false;
    }

    return true;
}

bool testDenoiserStrongSettingStaysCoherentAndBounded() {
    constexpr double sr = 48000.0;
    auto speech = makeSpeechLike(sr, 1.0f);
    auto noisy = addBuffers(speech, makeNoise(sr, 1.0f, 0.07f));

    VXDenoiserAudioProcessor denoiser;
    denoiser.prepareToPlay(sr, 256);
    setParamNormalized(denoiser, "clean", 0.90f);
    setParamNormalized(denoiser, "guard", 0.65f);
    const auto out = render(denoiser, noisy, 256);

    if (!allFinite(out) || peakAbs(out) > 1.05f) {
        std::cerr << "[VXSuitePluginRegression] Denoiser strong setting clipped or became unstable\n";
        return false;
    }
    const float corr = std::abs(speechBandCorrelation(speech, out, sr));
    if (corr < 0.40f) {
        std::cerr << "[VXSuitePluginRegression] Denoiser strong setting damaged speech coherence too much: |corr|="
                  << corr << "\n";
        return false;
    }
    return true;
}

bool testDenoiserDoesNotAmplifyImpulsesOrLeakListenBuzz() {
    constexpr double sr = 48000.0;
    constexpr int blockSize = 256;
    auto input = addBuffers(makeSpeechLike(sr, 0.8f), makeNoise(sr, 0.8f, 0.025f));
    const int clickSample = static_cast<int>(0.32 * sr);
    for (int ch = 0; ch < input.getNumChannels(); ++ch) {
        input.setSample(ch, clickSample - 1, -0.18f);
        input.setSample(ch, clickSample, 0.42f);
        input.setSample(ch, clickSample + 1, -0.16f);
    }
    const float inputPeak = peakAbs(input);

    VXDenoiserAudioProcessor denoiser;
    denoiser.prepareToPlay(sr, blockSize);
    setParamNormalized(denoiser, "clean", 0.95f);
    setParamNormalized(denoiser, "guard", 0.85f);
    setParamNormalized(denoiser, "mode", 0.0f);
    const auto out = render(denoiser, input, blockSize);
    const float outputPeak = peakAbs(out);
    if (outputPeak > inputPeak * 1.04f) {
        std::cerr << "[VXSuitePluginRegression] Denoiser amplified an impulse: inPeak="
                  << inputPeak << " outPeak=" << outputPeak << "\n";
        return false;
    }

    juce::AudioBuffer<float> silence(2, static_cast<int>(0.35 * sr));
    silence.clear();
    VXDenoiserAudioProcessor listenDenoiser;
    listenDenoiser.prepareToPlay(sr, blockSize);
    setParamNormalized(listenDenoiser, "clean", 0.95f);
    setParamNormalized(listenDenoiser, "guard", 0.85f);
    setParamNormalized(listenDenoiser, "listen", 1.0f);
    const auto listen = render(listenDenoiser, silence, blockSize);
    if (rms(listen) > 1.0e-5f || peakAbs(listen) > 1.0e-4f) {
        std::cerr << "[VXSuitePluginRegression] Denoiser Listen leaked buzz on silence: rms="
                  << rms(listen) << " peak=" << peakAbs(listen) << "\n";
        return false;
    }
    return true;
}

bool testDenoiserAndLevelerModesAreClearlyDifferentAtStrongSettings() {
    constexpr double sr = 48000.0;
    auto speech = makeSpeechLike(sr, 1.0f);
    auto noisySpeech = addBuffers(speech, makeNoise(sr, 1.0f, 0.10f));

    VXDenoiserAudioProcessor denoiseVoice;
    denoiseVoice.prepareToPlay(sr, 256);
    setParamNormalized(denoiseVoice, "mode", 0.0f);
    setParamNormalized(denoiseVoice, "clean", 1.0f);
    setParamNormalized(denoiseVoice, "guard", 0.75f);
    const auto denoiseVoiceOut = render(denoiseVoice, noisySpeech, 256);

    VXDenoiserAudioProcessor denoiseGeneral;
    denoiseGeneral.prepareToPlay(sr, 256);
    setParamNormalized(denoiseGeneral, "mode", 1.0f);
    setParamNormalized(denoiseGeneral, "clean", 1.0f);
    setParamNormalized(denoiseGeneral, "guard", 0.75f);
    const auto denoiseGeneralOut = render(denoiseGeneral, noisySpeech, 256);

    if (maxAbsDiffSkip(denoiseVoiceOut, denoiseGeneralOut, 512) < 0.012f) {
        std::cerr << "[VXSuitePluginRegression] Denoiser voice/general strong settings are still too similar\n";
        return false;
    }

    auto instrument = makePerformInstrumentInput(sr, 1.0f);
    auto mixedInput = addBuffers(noisySpeech, instrument);
    const float peak = peakAbs(mixedInput);
    if (peak > 1.0e-6f)
        mixedInput.applyGain(0.88f / peak);

    VXLevelerAudioProcessor levelerVoice;
    levelerVoice.prepareToPlay(sr, 256);
    setParamNormalized(levelerVoice, "mode", 0.0f);
    setParamNormalized(levelerVoice, "level", 1.0f);
    setParamNormalized(levelerVoice, "control", 0.90f);
    const auto levelerVoiceOut = render(levelerVoice, mixedInput, 256);

    VXLevelerAudioProcessor levelerGeneral;
    levelerGeneral.prepareToPlay(sr, 256);
    setParamNormalized(levelerGeneral, "mode", 1.0f);
    setParamNormalized(levelerGeneral, "level", 1.0f);
    setParamNormalized(levelerGeneral, "control", 0.90f);
    const auto levelerGeneralOut = render(levelerGeneral, mixedInput, 256);

    if (maxAbsDiffSkip(levelerVoiceOut, levelerGeneralOut, 256) < 0.015f) {
        std::cerr << "[VXSuitePluginRegression] Leveler voice/general strong settings are still too similar\n";
        return false;
    }

    return true;
}

bool testDenoiserStrongSettingRetainsUsefulLevelInBothModes() {
    constexpr double sr = 48000.0;
    auto speech = makeSpeechLike(sr, 1.0f);
    auto noisy = addBuffers(speech, makeNoise(sr, 1.0f, 0.07f));
    const float inputRms = rms(noisy);

    VXDenoiserAudioProcessor vocal;
    vocal.prepareToPlay(sr, 256);
    setParamNormalized(vocal, "clean", 0.90f);
    setParamNormalized(vocal, "guard", 0.65f);
    setParamNormalized(vocal, "mode", 0.0f);
    const auto vocalOut = render(vocal, noisy, 256);

    VXDenoiserAudioProcessor general;
    general.prepareToPlay(sr, 256);
    setParamNormalized(general, "clean", 0.90f);
    setParamNormalized(general, "guard", 0.65f);
    setParamNormalized(general, "mode", 1.0f);
    const auto generalOut = render(general, noisy, 256);

    if (rms(vocalOut) < inputRms * 0.62f) {
        std::cerr << "[VXSuitePluginRegression] Denoiser vocal mode collapsed level too far: in="
                  << inputRms << " out=" << rms(vocalOut) << "\n";
        return false;
    }
    if (rms(generalOut) < inputRms * 0.56f) {
        std::cerr << "[VXSuitePluginRegression] Denoiser general mode collapsed level too far: in="
                  << inputRms << " out=" << rms(generalOut) << "\n";
        return false;
    }
    return true;
}

bool testDenoiserNoiseOnlyInputStillReducesNoiseInBothModes() {
    constexpr double sr = 48000.0;
    auto noise = makeNoise(sr, 1.0f, 0.20f);
    const float inputRms = rms(noise);

    VXDenoiserAudioProcessor vocal;
    vocal.prepareToPlay(sr, 256);
    setParamNormalized(vocal, "clean", 1.0f);
    setParamNormalized(vocal, "guard", 1.0f);
    setParamNormalized(vocal, "mode", 0.0f);
    const auto vocalOut = render(vocal, noise, 256);

    VXDenoiserAudioProcessor general;
    general.prepareToPlay(sr, 256);
    setParamNormalized(general, "clean", 1.0f);
    setParamNormalized(general, "guard", 1.0f);
    setParamNormalized(general, "mode", 1.0f);
    const auto generalOut = render(general, noise, 256);

    if (rms(vocalOut) > inputRms * 0.92f) {
        std::cerr << "[VXSuitePluginRegression] Denoiser vocal mode barely reduced noise-only input: in="
                  << inputRms << " out=" << rms(vocalOut) << "\n";
        return false;
    }
    if (rms(generalOut) > inputRms * 0.80f) {
        std::cerr << "[VXSuitePluginRegression] Denoiser general mode barely reduced noise-only input: in="
                  << inputRms << " out=" << rms(generalOut) << "\n";
        return false;
    }
    return true;
}

bool testDenoiserStereoTreatsChannelsIndependently() {
    constexpr double sr = 48000.0;
    auto speech = makeSpeechLike(sr, 1.0f);
    auto noise = makeNoise(sr, 1.0f, 0.24f);
    juce::AudioBuffer<float> input(2, speech.getNumSamples());
    input.copyFrom(0, 0, speech, 0, 0, speech.getNumSamples());
    input.copyFrom(1, 0, speech, 1, 0, speech.getNumSamples());
    input.addFrom(1, 0, noise, 1, 0, noise.getNumSamples());

    VXDenoiserAudioProcessor denoiser;
    denoiser.prepareToPlay(sr, 256);
    setParamNormalized(denoiser, "clean", 1.0f);
    setParamNormalized(denoiser, "guard", 0.10f);
    setParamNormalized(denoiser, "mode", 1.0f);
    const auto out = render(denoiser, input, 256);

    juce::AudioBuffer<float> inLeft(1, input.getNumSamples()), inRight(1, input.getNumSamples());
    juce::AudioBuffer<float> outLeft(1, out.getNumSamples()), outRight(1, out.getNumSamples());
    for (int i = 0; i < input.getNumSamples(); ++i) {
        inLeft.setSample(0, i, input.getSample(0, i));
        inRight.setSample(0, i, input.getSample(1, i));
        outLeft.setSample(0, i, out.getSample(0, i));
        outRight.setSample(0, i, out.getSample(1, i));
    }

    juce::AudioBuffer<float> leftDelta(1, input.getNumSamples()), rightDelta(1, input.getNumSamples());
    for (int i = 0; i < input.getNumSamples(); ++i) {
        leftDelta.setSample(0, i, outLeft.getSample(0, i) - inLeft.getSample(0, i));
        rightDelta.setSample(0, i, outRight.getSample(0, i) - inRight.getSample(0, i));
    }

    const float leftResidual = rmsSkip(leftDelta, 4096);
    const float rightResidual = rmsSkip(rightDelta, 4096);
    if (!(rightResidual > leftResidual + 0.006f)) {
        std::cerr << "[VXSuitePluginRegression] Denoiser stereo path is still not channel-aware enough: leftResidual="
                  << leftResidual << " rightResidual=" << rightResidual << "\n";
        return false;
    }
    return true;
}

bool testDenoiserHybridCleanupPrefersHarshResidualOverVoicedTone() {
    constexpr double sr = 48000.0;
    auto harsh = makeHarshContaminatedInput(sr, 1.0f);
    auto voiced = makeSpeechLike(sr, 1.0f);
    const auto highBandRatio = [sr](const juce::AudioBuffer<float>& input,
                                    const juce::AudioBuffer<float>& output) {
        const float coeff = std::exp(-2.0f * juce::MathConstants<float>::pi * 3400.0f / static_cast<float>(sr));
        auto bandRms = [coeff](const juce::AudioBuffer<float>& buffer) {
            double energy = 0.0;
            int count = 0;
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
                float lp = 0.0f;
                const auto* data = buffer.getReadPointer(ch);
                for (int i = 0; i < buffer.getNumSamples(); ++i) {
                    lp = coeff * lp + (1.0f - coeff) * data[i];
                    const float high = data[i] - lp;
                    energy += static_cast<double>(high) * static_cast<double>(high);
                    ++count;
                }
            }
            return count > 0 ? static_cast<float>(std::sqrt(energy / static_cast<double>(count))) : 0.0f;
        };
        return bandRms(output) / std::max(bandRms(input), 1.0e-6f);
    };

    VXDenoiserAudioProcessor denoiserHarsh;
    denoiserHarsh.prepareToPlay(sr, 256);
    setParamNormalized(denoiserHarsh, "mode", 0.0f);
    setParamNormalized(denoiserHarsh, "clean", 1.0f);
    setParamNormalized(denoiserHarsh, "guard", 0.55f);
    const auto harshOut = render(denoiserHarsh, harsh, 256);

    VXDenoiserAudioProcessor denoiserVoiced;
    denoiserVoiced.prepareToPlay(sr, 256);
    setParamNormalized(denoiserVoiced, "mode", 0.0f);
    setParamNormalized(denoiserVoiced, "clean", 1.0f);
    setParamNormalized(denoiserVoiced, "guard", 0.55f);
    const auto voicedOut = render(denoiserVoiced, voiced, 256);

    const float harshHighRatio = highBandRatio(harsh, harshOut);
    const float voicedHighRatio = highBandRatio(voiced, voicedOut);
    const float voicedCorr = std::abs(speechBandCorrelation(voiced, voicedOut, sr));

    if (!(harshHighRatio < voicedHighRatio * 0.94f)) {
        std::cerr << "[VXSuitePluginRegression] Denoiser hybrid cleanup no longer reduces harsh high-band residue more strongly than normal voiced material: harshHighRatio="
                  << harshHighRatio << " voicedHighRatio=" << voicedHighRatio << "\n";
        return false;
    }
    if (voicedCorr < 0.20f) {
        std::cerr << "[VXSuitePluginRegression] Denoiser hybrid cleanup now damages voiced material coherence too much: voicedCorr="
                  << voicedCorr << "\n";
        return false;
    }

    return true;
}

bool testDenoiserZeroCleanKeepsPdcAlignedIdentity() {
    constexpr double sr = 48000.0;
    auto input = addBuffers(makeSpeechLike(sr, 0.8f), makeNoise(sr, 0.8f, 0.04f));

    VXDenoiserAudioProcessor denoiser;
    denoiser.prepareToPlay(sr, 256);
    setParamNormalized(denoiser, "clean", 0.0f);
    setParamNormalized(denoiser, "guard", 0.65f);
    const auto out = render(denoiser, input, 256);

    const float diff = maxAbsDiff(input, out);
    if (diff > 1.0e-3f) {
        std::cerr << "[VXSuitePluginRegression] Denoiser clean=0 no longer returns a PDC-aligned dry path: diff="
                  << diff << "\n";
        return false;
    }
    return true;
}

bool testDenoiserResetAndReprepareStayFinite() {
    constexpr double srA = 48000.0;
    constexpr double srB = 44100.0;

    auto noisyA = addBuffers(makeSpeechLike(srA, 0.8f), makeNoise(srA, 0.8f, 0.05f));
    auto noisyB = addBuffers(makeSpeechLike(srB, 0.8f), makeNoise(srB, 0.8f, 0.05f));

    VXDenoiserAudioProcessor denoiser;
    denoiser.prepareToPlay(srA, 256);
    setParamNormalized(denoiser, "clean", 0.82f);
    setParamNormalized(denoiser, "guard", 0.58f);
    const auto first = render(denoiser, noisyA, 256);
    if (!allFinite(first)) {
        std::cerr << "[VXSuitePluginRegression] Denoiser first render produced non-finite output\n";
        return false;
    }

    denoiser.reset();
    const auto second = render(denoiser, noisyA, 256);
    if (!allFinite(second)) {
        std::cerr << "[VXSuitePluginRegression] Denoiser reset render produced non-finite output\n";
        return false;
    }

    denoiser.prepareToPlay(srB, 512);
    setParamNormalized(denoiser, "clean", 0.76f);
    setParamNormalized(denoiser, "guard", 0.44f);
    const auto third = render(denoiser, noisyB, 512);
    if (!allFinite(third) || peakAbs(third) > 1.05f) {
        std::cerr << "[VXSuitePluginRegression] Denoiser reprepare render became unstable\n";
        return false;
    }

    return true;
}

bool testDenoiserOversizedHostBlocksStayConsistent() {
    constexpr double sr = 48000.0;
    auto noisy = addBuffers(makeSpeechLike(sr, 0.9f), makeNoise(sr, 0.9f, 0.06f));
    std::vector<int> oversizedBlocks { 1536, 3072, 4096 };

    VXDenoiserAudioProcessor reference;
    reference.prepareToPlay(sr, 256);
    setParamNormalized(reference, "clean", 0.78f);
    setParamNormalized(reference, "guard", 0.52f);
    const auto refOut = render(reference, noisy, 256);

    VXDenoiserAudioProcessor oversized;
    oversized.prepareToPlay(sr, 256);
    setParamNormalized(oversized, "clean", 0.78f);
    setParamNormalized(oversized, "guard", 0.52f);
    const auto oversizedOut = renderWithBlocks(oversized, noisy, oversizedBlocks);

    if (!allFinite(oversizedOut)) {
        std::cerr << "[VXSuitePluginRegression] Oversized host-block denoiser output became non-finite\n";
        return false;
    }

    const float corr = bufferCorrelationSkip(refOut, oversizedOut, 4096);
    if (corr < 0.985f) {
        std::cerr << "[VXSuitePluginRegression] Oversized host blocks changed denoiser behaviour too much: corr="
                  << corr << "\n";
        return false;
    }

    return true;
}

bool testDeepFilterOfflineRenderModeSwitchRecoversCleanly() {
    constexpr double sr = 48000.0;
    constexpr int blockSize = 256;

    if (!ensureDeepFilterTestModelsInstalled())
        return false;

    VXDeepFilterNetAudioProcessor processor;
    processor.prepareToPlay(sr, blockSize);
    setParamNormalized(processor, "clean", 1.0f);
    setParamNormalized(processor, "guard", 0.18f);
    setParamNormalized(processor, "model", 0.0f);

    const auto input = addBuffers(makeSpeechLike(sr, 3.2f), makeNoise(sr, 3.2f, 0.08f));
    const auto liveBefore = render(processor, input, blockSize);
    const int liveCompareSkip = processor.getLatencySamples() + 4096;
    const float liveBeforeRms = rmsSkip(liveBefore, liveCompareSkip);
    if (processor.getLatencySamples() <= 0
        || processor.getStatusText().containsIgnoreCase("not installed")
        || processor.getStatusText().containsIgnoreCase("init failed")
        || processor.getStatusText().containsIgnoreCase("fallback")
        || !allFinite(liveBefore)
        || liveBeforeRms <= 1.0e-6f) {
        std::cerr << "[VXSuitePluginRegression] DeepFilter did not reach a healthy live baseline before mode switch: latency="
                  << processor.getLatencySamples() << " status=" << processor.getStatusText()
                  << " rms=" << liveBeforeRms << "\n";
            return false;
        }

    processor.setNonRealtime(true);
    processor.prepareToPlay(sr, 2048);
    setParamNormalized(processor, "clean", 1.0f);
    setParamNormalized(processor, "guard", 0.18f);
    setParamNormalized(processor, "model", 0.0f);
    const auto offlineOut = render(processor, input, 2048);
    constexpr int modeSwitchCompareSkip = 12000;
    const float offlineVsLiveResidual = bestGainResidualRatioSkip(liveBefore, offlineOut, modeSwitchCompareSkip);
    const float offlineRms = rmsSkip(offlineOut, modeSwitchCompareSkip);
    if (processor.getStatusText().containsIgnoreCase("init failed")
        || processor.getStatusText().containsIgnoreCase("fallback")
        || !allFinite(offlineOut)
        || offlineRms <= liveBeforeRms * 0.35f) {
        std::cerr << "[VXSuitePluginRegression] DeepFilter offline render path broke after entering non-realtime mode: residual="
                  << offlineVsLiveResidual << " status=" << processor.getStatusText()
                  << " liveRms=" << liveBeforeRms << " offlineRms=" << offlineRms << "\n";
        return false;
    }

    processor.setNonRealtime(false);
    processor.prepareToPlay(sr, blockSize);
    setParamNormalized(processor, "clean", 1.0f);
    setParamNormalized(processor, "guard", 0.18f);
    setParamNormalized(processor, "model", 0.0f);
    const auto liveAfter = render(processor, input, blockSize);
    const int liveAfterCompareSkip = processor.getLatencySamples() + 4096;
    const float liveAfterVsBeforeResidual = bestGainResidualRatioSkip(liveBefore, liveAfter, liveAfterCompareSkip);
    const float liveAfterRms = rmsSkip(liveAfter, liveAfterCompareSkip);
    if (processor.getStatusText().containsIgnoreCase("init failed")
        || processor.getStatusText().containsIgnoreCase("fallback")
        || !allFinite(liveAfter)
        || liveAfterRms <= liveBeforeRms * 0.35f
        || liveAfterVsBeforeResidual > 0.45f) {
        std::cerr << "[VXSuitePluginRegression] DeepFilter stayed broken after leaving non-realtime mode: residual="
                  << liveAfterVsBeforeResidual << " status=" << processor.getStatusText()
                  << " liveRms=" << liveBeforeRms << " liveAfterRms=" << liveAfterRms << "\n";
        return false;
    }

    return true;
}

bool testDeepFilterStartupHoldbackReleasesValidProcessedAudio() {
    constexpr double sr = 48000.0;
    constexpr int blockSize = 256;

    if (!ensureDeepFilterTestModelsInstalled())
        return false;

    auto input = addBuffers(makeSpeechLike(sr, 0.9f), makeNoise(sr, 0.9f, 0.08f));

    auto rmsWindow = [](const juce::AudioBuffer<float>& buffer, const int start, const int length) {
        double energy = 0.0;
        int count = 0;
        const int begin = juce::jlimit(0, buffer.getNumSamples(), start);
        const int end = juce::jlimit(begin, buffer.getNumSamples(), begin + std::max(0, length));
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
            const auto* data = buffer.getReadPointer(ch);
            for (int i = begin; i < end; ++i) {
                energy += static_cast<double>(data[i]) * data[i];
                ++count;
            }
        }
        return count > 0 ? static_cast<float>(std::sqrt(energy / static_cast<double>(count))) : 0.0f;
    };
    auto firstAudibleSample = [](const juce::AudioBuffer<float>& buffer) {
        for (int i = 0; i < buffer.getNumSamples(); ++i) {
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
                if (std::abs(buffer.getSample(ch, i)) > 1.0e-5f)
                    return i;
            }
        }
        return -1;
    };

    VXDeepFilterNetAudioProcessor processor;
    processor.setNonRealtime(true);
    processor.prepareToPlay(sr, blockSize);
    setParamNormalized(processor, "clean", 1.0f);
    setParamNormalized(processor, "guard", 0.0f);
    setParamNormalized(processor, "model", 0.0f);

    const auto rendered = render(processor, input, blockSize);
    const float firstRms = rmsWindow(rendered, 0, 1024);
    const float laterRms = rmsWindow(rendered, 4096, 4096);
    if (processor.getLatencySamples() <= 0
        || !allFinite(rendered)
        || firstRms <= 1.0e-5f
        || laterRms <= 1.0e-5f
        || firstRms < laterRms * 0.12f) {
        std::cerr << "[VXSuitePluginRegression] DeepFilter startup holdback did not produce valid compensated output from the first audible block: latency="
                  << processor.getLatencySamples() << " firstRms=" << firstRms
                  << " laterRms=" << laterRms
                  << " firstAudible=" << firstAudibleSample(rendered) << "\n";
        return false;
    }

    const int leadingSilenceSamples = static_cast<int>(sr * 0.12);
    auto speechAfterSilence = makeSpeechLike(sr, 0.5f);
    juce::AudioBuffer<float> onsetInput(2, leadingSilenceSamples + speechAfterSilence.getNumSamples());
    onsetInput.clear();
    for (int ch = 0; ch < onsetInput.getNumChannels(); ++ch)
        onsetInput.copyFrom(ch, leadingSilenceSamples, speechAfterSilence, ch, 0, speechAfterSilence.getNumSamples());

    VXDeepFilterNetAudioProcessor onsetProcessor;
    onsetProcessor.setNonRealtime(true);
    onsetProcessor.prepareToPlay(sr, blockSize);
    setParamNormalized(onsetProcessor, "clean", 1.0f);
    setParamNormalized(onsetProcessor, "guard", 0.0f);
    setParamNormalized(onsetProcessor, "model", 0.0f);

    const auto onsetRendered = render(onsetProcessor, onsetInput, blockSize);
    const int onsetFirstAudible = firstAudibleSample(onsetRendered);
    const int expectedCompensatedOnset = std::max(0, leadingSilenceSamples - onsetProcessor.getLatencySamples());
    const float preVoiceRms = rmsWindow(onsetRendered, expectedCompensatedOnset - 1800, 1800);
    const float firstVoiceRms = onsetFirstAudible >= 0 ? rmsWindow(onsetRendered, onsetFirstAudible, 1024) : 0.0f;
    const float laterVoiceRms = rmsWindow(onsetRendered, expectedCompensatedOnset + 8192, 4096);
    if (!allFinite(onsetRendered)
        || std::abs(onsetFirstAudible - expectedCompensatedOnset) > 1024
        || preVoiceRms > 1.0e-5f
        || firstVoiceRms <= 1.0e-5f
        || laterVoiceRms <= 1.0e-5f
        || firstVoiceRms < laterVoiceRms * 0.50f) {
        std::cerr << "[VXSuitePluginRegression] DeepFilter silence-to-voice holdback leaked chatter or weakened the first voiced frames: firstAudible="
                  << onsetFirstAudible << " expectedOnset=" << expectedCompensatedOnset
                  << " preVoiceRms=" << preVoiceRms
                  << " firstVoiceRms=" << firstVoiceRms
                  << " laterVoiceRms=" << laterVoiceRms << "\n";
        return false;
    }

    VXDeepFilterNetAudioProcessor irregular;
    irregular.setNonRealtime(true);
    irregular.prepareToPlay(sr, 1024);
    setParamNormalized(irregular, "clean", 1.0f);
    setParamNormalized(irregular, "guard", 0.0f);
    setParamNormalized(irregular, "model", 0.0f);

    const auto irregularOut = renderWithBlocks(irregular, input, { 32, 64, 128, 256, 512, 1024, 96, 384 });
    const float irregularFirstRms = rmsWindow(irregularOut, 0, 1024);
    const float irregularLaterRms = rmsWindow(irregularOut, 4096, 4096);
    if (!allFinite(irregularOut)
        || irregularFirstRms <= 1.0e-5f
        || irregularLaterRms <= 1.0e-5f
        || irregularFirstRms < irregularLaterRms * 0.08f) {
        std::cerr << "[VXSuitePluginRegression] DeepFilter startup holdback failed with irregular block sizes: firstRms="
                  << irregularFirstRms << " laterRms=" << irregularLaterRms << "\n";
        return false;
    }

    return true;
}

bool testDeepFilterGuardRespondsMoreOnArtifactHeavyInput() {
    constexpr double sr = 48000.0;
    constexpr int blockSize = 256;

    if (!ensureDeepFilterTestModelsInstalled())
        return false;

    auto lightInput = addBuffers(makeSpeechLike(sr, 1.0f), makeNoise(sr, 1.0f, 0.02f));
    auto harshInput = addBuffers(makeSpeechLike(sr, 1.0f), makeNoise(sr, 1.0f, 0.10f));

    VXDeepFilterNetAudioProcessor lightLowGuard;
    lightLowGuard.setNonRealtime(true);
    lightLowGuard.prepareToPlay(sr, blockSize);
    setParamNormalized(lightLowGuard, "clean", 1.0f);
    setParamNormalized(lightLowGuard, "guard", 0.15f);
    setParamNormalized(lightLowGuard, "model", 0.0f);
    const auto lightLowOut = render(lightLowGuard, lightInput, blockSize);

    VXDeepFilterNetAudioProcessor lightHighGuard;
    lightHighGuard.setNonRealtime(true);
    lightHighGuard.prepareToPlay(sr, blockSize);
    setParamNormalized(lightHighGuard, "clean", 1.0f);
    setParamNormalized(lightHighGuard, "guard", 0.85f);
    setParamNormalized(lightHighGuard, "model", 0.0f);
    const auto lightHighOut = render(lightHighGuard, lightInput, blockSize);

    VXDeepFilterNetAudioProcessor harshLowGuard;
    harshLowGuard.setNonRealtime(true);
    harshLowGuard.prepareToPlay(sr, blockSize);
    setParamNormalized(harshLowGuard, "clean", 1.0f);
    setParamNormalized(harshLowGuard, "guard", 0.15f);
    setParamNormalized(harshLowGuard, "model", 0.0f);
    const auto harshLowOut = render(harshLowGuard, harshInput, blockSize);

    VXDeepFilterNetAudioProcessor harshHighGuard;
    harshHighGuard.setNonRealtime(true);
    harshHighGuard.prepareToPlay(sr, blockSize);
    setParamNormalized(harshHighGuard, "clean", 1.0f);
    setParamNormalized(harshHighGuard, "guard", 0.85f);
    setParamNormalized(harshHighGuard, "model", 0.0f);
    const auto harshHighOut = render(harshHighGuard, harshInput, blockSize);

    if (!allFinite(lightLowOut) || !allFinite(lightHighOut) || !allFinite(harshLowOut) || !allFinite(harshHighOut)) {
        std::cerr << "[VXSuitePluginRegression] DeepFilter guard confidence pass produced non-finite output\n";
        return false;
    }

    const float lightGuardDelta = maxAbsDiffSkip(lightLowOut, lightHighOut, 4096);
    const float harshGuardDelta = maxAbsDiffSkip(harshLowOut, harshHighOut, 4096);
    if (!(harshGuardDelta > lightGuardDelta * 1.10f)) {
        std::cerr << "[VXSuitePluginRegression] DeepFilter guard is no longer materially more active on artifact-heavy input: lightDelta="
                  << lightGuardDelta << " harshDelta=" << harshGuardDelta << "\n";
        return false;
    }

    return true;
}

bool testDeepFilterFullGuardIsPdcAlignedDry() {
    constexpr double sr = 48000.0;
    constexpr int blockSize = 256;

    const auto input = addBuffers(makeSpeechLike(sr, 1.0f), makeNoise(sr, 1.0f, 0.04f));

    VXDeepFilterNetAudioProcessor processor;
    processor.setNonRealtime(true);
    processor.prepareToPlay(sr, blockSize);
    setParamNormalized(processor, "clean", 1.0f);
    setParamNormalized(processor, "guard", 1.0f);
    setParamNormalized(processor, "model", 0.0f);

    const auto rendered = render(processor, input, blockSize);
    const float diff = maxAbsDiff(input, rendered);
    if (processor.getLatencySamples() <= 0
        || !allFinite(rendered)
        || diff > 1.0e-5f) {
        std::cerr << "[VXSuitePluginRegression] DeepFilter full Guard is not aligned dry: latency="
                  << processor.getLatencySamples() << " maxDiff=" << diff
                  << " status=" << processor.getStatusText() << "\n";
        return false;
    }

    return true;
}

bool testDeepFilterListenIsRemovedContentAndGuardListenIsSilent() {
    constexpr double sr = 48000.0;
    constexpr int blockSize = 256;

    if (!ensureDeepFilterTestModelsInstalled())
        return false;

    const auto input = addBuffers(makeSpeechLike(sr, 1.2f), makeNoise(sr, 1.2f, 0.09f));

    auto configure = [=](VXDeepFilterNetAudioProcessor& processor, const float guard, const bool listen) {
        processor.setNonRealtime(true);
        processor.prepareToPlay(sr, blockSize);
        setParamNormalized(processor, "clean", 1.0f);
        setParamNormalized(processor, "guard", guard);
        setParamNormalized(processor, "model", 0.0f);
        setParamNormalized(processor, "listen", listen ? 1.0f : 0.0f);
    };

    VXDeepFilterNetAudioProcessor wetProcessor;
    configure(wetProcessor, 0.20f, false);
    const auto wet = render(wetProcessor, input, blockSize);

    VXDeepFilterNetAudioProcessor listenProcessor;
    configure(listenProcessor, 0.20f, true);
    const auto listen = render(listenProcessor, input, blockSize);

    const int skip = wetProcessor.getLatencySamples() + 4096;
    const auto recombined = addBuffers(wet, listen);
    const float listenRms = rmsSkip(listen, skip);
    const float recombinedError = maxAbsDiffSkip(input, recombined, skip);
    if (!allFinite(wet)
        || !allFinite(listen)
        || listenRms <= 1.0e-5f
        || recombinedError > 0.035f) {
        std::cerr << "[VXSuitePluginRegression] DeepFilter listen is not removed-content audition: listenRms="
                  << listenRms << " recombinedError=" << recombinedError << "\n";
        return false;
    }

    VXDeepFilterNetAudioProcessor guardedListenProcessor;
    configure(guardedListenProcessor, 1.0f, true);
    const auto guardedListen = render(guardedListenProcessor, input, blockSize);
    if (!allFinite(guardedListen)
        || rmsSkip(guardedListen, skip) > 1.0e-5f
        || peakAbs(guardedListen) > 1.0e-4f) {
        std::cerr << "[VXSuitePluginRegression] DeepFilter full-Guard Listen leaked delayed dry/delta: rms="
                  << rmsSkip(guardedListen, skip) << " peak=" << peakAbs(guardedListen) << "\n";
        return false;
    }

    return true;
}

bool testDeepFilterConcurrentInstancesBothProcess() {
    constexpr double sr = 48000.0;
    constexpr int blockSize = 256;

    if (!ensureDeepFilterTestModelsInstalled())
        return false;

    const auto inputA = addBuffers(makeSpeechLike(sr, 1.2f), makeNoise(sr, 1.2f, 0.07f));
    const auto inputB = addBuffers(makeSpeechLike(sr, 1.2f), makeNoise(sr, 1.2f, 0.11f));

    auto configure = [=](VXDeepFilterNetAudioProcessor& processor) {
        processor.setNonRealtime(true);
        processor.prepareToPlay(sr, blockSize);
        setParamNormalized(processor, "clean", 1.0f);
        setParamNormalized(processor, "guard", 0.10f);
        setParamNormalized(processor, "model", 0.0f);
    };

    VXDeepFilterNetAudioProcessor reference;
    configure(reference);
    const auto referenceOut = render(reference, inputB, blockSize);

    VXDeepFilterNetAudioProcessor processorA;
    VXDeepFilterNetAudioProcessor processorB;
    configure(processorA);
    configure(processorB);

    juce::AudioBuffer<float> outA;
    juce::AudioBuffer<float> outB;
    std::thread renderA([&] { outA = render(processorA, inputA, blockSize); });
    std::thread renderB([&] { outB = render(processorB, inputB, blockSize); });
    renderA.join();
    renderB.join();

    const int skip = std::max(processorB.getLatencySamples() + 4096, 8192);
    const float processedRms = rmsSkip(outB, skip);
    const float dryResidual = bestGainResidualRatioSkip(inputB, outB, skip);
    const float referenceResidual = bestGainResidualRatioSkip(referenceOut, outB, skip);
    if (!allFinite(outA)
        || !allFinite(outB)
        || processedRms <= 1.0e-6f
        || dryResidual < 0.02f
        || referenceResidual > 0.35f
        || processorA.getStatusText().containsIgnoreCase("init failed")
        || processorB.getStatusText().containsIgnoreCase("init failed")
        || processorA.getStatusText().containsIgnoreCase("fallback")
        || processorB.getStatusText().containsIgnoreCase("fallback")) {
        std::cerr << "[VXSuitePluginRegression] Concurrent DeepFilter instances did not both process: rms="
                  << processedRms << " dryResidual=" << dryResidual
                  << " referenceResidual=" << referenceResidual
                  << " statusA=" << processorA.getStatusText()
                  << " statusB=" << processorB.getStatusText() << "\n";
        return false;
    }

    return true;
}

bool testAnalyserDomainBindingSurvivesMultipleDomains() {
    constexpr double sr = 48000.0;
    constexpr int blockSize = 4096;
    const auto input = addBuffers(makeSpeechLike(sr, 0.20f), makeNoise(sr, 0.20f, 0.03f));

    bool deverbFoundInNewestAnalyserDomain = false;
    juce::String seenDeverbStages;
    std::uint64_t expectedDomainId = 0;
    {
        VXStudioAnalyserAudioProcessor analyserA;
        analyserA.prepareToPlay(sr, blockSize);
        juce::AudioProcessor::TrackProperties trackA;
        trackA.channelUID = "regression-analyser-track-a";
        analyserA.updateTrackProperties(trackA);

        VXStudioAnalyserAudioProcessor analyserB;
        analyserB.prepareToPlay(sr, blockSize);
        juce::AudioProcessor::TrackProperties trackB;
        trackB.channelUID = "regression-analyser-track-b";
        analyserB.updateTrackProperties(trackB);
        expectedDomainId = analyserB.analysisDomainId();

        VXDeverbAudioProcessor deverb;
        deverb.prepareToPlay(sr, blockSize);
        deverb.updateTrackProperties(trackB);

        render(analyserA, input, blockSize);
        render(analyserB, input, blockSize);
        render(deverb, input, blockSize);

        for (int slotIndex = 0; slotIndex < vxsuite::analysis::StageRegistry::instance().maxSlots(); ++slotIndex) {
            vxsuite::analysis::StageView stage;
            if (!vxsuite::analysis::StageRegistry::instance().readStage(slotIndex, stage))
                continue;
            if (!stage.active)
                continue;
            const auto stageName = fixedLabelToString(stage.telemetry.identity.stageName);
            if (stageName.containsIgnoreCase("Deverb")) {
                seenDeverbStages << "domain=" << juce::String(static_cast<juce::int64>(stage.analysisDomainId))
                                 << " stageId=" << fixedLabelToString(stage.telemetry.identity.stageId)
                                 << " instance=" << juce::String(static_cast<juce::int64>(stage.telemetry.identity.instanceId))
                                 << "; ";
            }
            if (stage.analysisDomainId != expectedDomainId)
                continue;
            if (stageName.containsIgnoreCase("Deverb")) {
                deverbFoundInNewestAnalyserDomain = true;
                break;
            }
        }
    }

    if (!deverbFoundInNewestAnalyserDomain) {
        std::cerr << "[VXSuitePluginRegression] Deverb stage telemetry did not bind to the active analyser domain when multiple domains existed in the host process\n";
        std::cerr << "  expected domain=" << expectedDomainId
                  << " seen deverb stages: " << seenDeverbStages << "\n";
        return false;
    }

    return true;
}

bool testSubtractZeroKeepsPdcAlignedIdentity() {
    constexpr double sr = 48000.0;
    auto input = addBuffers(makeSpeechLike(sr, 0.8f), makeNoise(sr, 0.8f, 0.04f));

    VXSubtractAudioProcessor subtract;
    subtract.prepareToPlay(sr, 256);
    setParamNormalized(subtract, "subtract", 0.0f);
    setParamNormalized(subtract, "protect", 0.5f);
    setParamNormalized(subtract, "learn", 0.0f);
    const auto out = render(subtract, input, 256);

    const float diff = maxAbsDiff(input, out);
    if (diff > 1.0e-3f) {
        std::cerr << "[VXSuitePluginRegression] Subtract subtract=0 no longer returns a PDC-aligned dry path: diff="
                  << diff << "\n";
        return false;
    }
    return true;
}

bool testLifecycleAndStateRestore() {
    constexpr double srA = 48000.0;
    constexpr double srB = 44100.0;

    VXSubtractAudioProcessor subtract;
    subtract.prepareToPlay(srA, 256);
    if (!primeSubtractLearn(subtract, srA))
        return false;

    juce::MemoryBlock state;
    subtract.getStateInformation(state);
    const float savedConfidence = subtract.getLearnConfidence();

    VXSubtractAudioProcessor restored;
    restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    restored.prepareToPlay(srA, 256);
    if (!restored.isLearnReady()) {
        std::cerr << "[VXSuitePluginRegression] Restored subtract state lost learned profile readiness\n";
        return false;
    }
    if (std::abs(restored.getLearnConfidence() - savedConfidence) > 0.08f) {
        std::cerr << "[VXSuitePluginRegression] Restored subtract confidence drifted too far\n";
        return false;
    }

    auto input = addBuffers(makeSpeechLike(srA, 0.7f), makeNoise(srA, 0.7f, 0.05f));
    auto first = render(restored, input, 128);
    restored.reset();
    auto second = render(restored, input, 128);
    if (!allFinite(first) || !allFinite(second)) {
        std::cerr << "[VXSuitePluginRegression] Reset lifecycle produced non-finite output\n";
        return false;
    }

    restored.prepareToPlay(srB, 512);
    auto third = render(restored, addBuffers(makeSpeechLike(srB, 0.7f), makeNoise(srB, 0.7f, 0.05f)), 512);
    if (!allFinite(third)) {
        std::cerr << "[VXSuitePluginRegression] Sample-rate reprepare produced non-finite output\n";
        return false;
    }
    return true;
}

bool testSubtractStateRestoreRejectsMismatchedProfileFormat() {
    constexpr double srSaved = 48000.0;
    constexpr double srRestore = 44100.0;

    VXSubtractAudioProcessor subtract;
    subtract.prepareToPlay(srSaved, 256);
    if (!primeSubtractLearn(subtract, srSaved))
        return false;

    juce::MemoryBlock state;
    subtract.getStateInformation(state);

    VXSubtractAudioProcessor restored;
    restored.prepareToPlay(srRestore, 256);
    restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));

    if (restored.isLearnReady()) {
        std::cerr << "[VXSuitePluginRegression] Subtract restored a learned profile across an incompatible sample-rate format\n";
        return false;
    }
    if (restored.getLearnConfidence() > 1.0e-4f) {
        std::cerr << "[VXSuitePluginRegression] Subtract kept non-zero confidence after rejecting a mismatched learned profile\n";
        return false;
    }
    return true;
}

bool testSubtractResetKeepsLearningArmed() {
    constexpr double sr = 48000.0;
    auto noise = makeNoise(sr, 0.2f, 0.08f);

    VXSubtractAudioProcessor subtract;
    subtract.prepareToPlay(sr, 256);
    setParamNormalized(subtract, "learn", 1.0f);
    subtract.reset();

    juce::AudioBuffer<float> block(2, 256);
    for (int ch = 0; ch < 2; ++ch)
        block.copyFrom(ch, 0, noise, ch, 0, 256);
    processSingleBlock(subtract, block);

    if (!subtract.isLearnActive()) {
        std::cerr << "[VXSuitePluginRegression] Subtract reset dropped an armed Learn state\n";
        return false;
    }
    return true;
}

bool testListenSemanticsAcrossPlugins() {
    constexpr double sr = 48000.0;
    auto speech = makeSpeechLike(sr, 0.9f);
    auto noise = makeNoise(sr, 0.9f, 0.06f);
    auto noisy = addBuffers(speech, noise);

    VXProximityAudioProcessor proximity;
    proximity.prepareToPlay(sr, 256);
    setParamNormalized(proximity, "closer", 0.30f);
    setParamNormalized(proximity, "air", 0.22f);
    setParamNormalized(proximity, "listen", 0.0f);
    const auto proxWet = render(proximity, speech);
    proximity.reset();
    setParamNormalized(proximity, "closer", 0.30f);
    setParamNormalized(proximity, "air", 0.22f);
    setParamNormalized(proximity, "listen", 1.0f);
    const auto proxListen = render(proximity, speech);
    if (maxAbsDiffSkip(proxWet, addBuffers(speech, proxListen), 128) > 1.0e-4f) {
        std::cerr << "[VXSuitePluginRegression] Proximity listen no longer behaves like additive delta audition\n";
        return false;
    }

    VXFinishAudioProcessor finish;
    finish.prepareToPlay(sr, 256);
    setParamNormalized(finish, "finish", 0.45f);
    setParamNormalized(finish, "body", 0.28f);
    setParamNormalized(finish, "gain", 0.35f);
    setParamNormalized(finish, "listen", 0.0f);
    const auto finishWet = render(finish, speech);
    finish.reset();
    setParamNormalized(finish, "finish", 0.45f);
    setParamNormalized(finish, "body", 0.28f);
    setParamNormalized(finish, "gain", 0.35f);
    setParamNormalized(finish, "listen", 1.0f);
    const auto finishListen = render(finish, speech);
    if (maxAbsDiffSkip(finishWet, addBuffers(speech, finishListen), 128) > 1.0e-3f) {
        std::cerr << "[VXSuitePluginRegression] Finish listen no longer behaves like finish-delta audition\n";
        return false;
    }

    VXToneAudioProcessor tone;
    tone.prepareToPlay(sr, 256);
    setParamNormalized(tone, "bass", 0.78f);
    setParamNormalized(tone, "treble", 0.28f);
    setParamNormalized(tone, "listen", 0.0f);
    const auto toneWet = render(tone, speech);
    tone.reset();
    setParamNormalized(tone, "bass", 0.78f);
    setParamNormalized(tone, "treble", 0.28f);
    setParamNormalized(tone, "listen", 1.0f);
    const auto toneListen = render(tone, speech);
    if (maxAbsDiffSkip(toneWet, addBuffers(speech, toneListen), 128) > 1.0e-4f) {
        std::cerr << "[VXSuitePluginRegression] Tone listen no longer behaves like additive tone-delta audition\n";
        return false;
    }
    return true;
}

bool testOversizedHostBlocksStayConsistent() {
    constexpr double sr = 48000.0;
    auto input = makeSpeechLike(sr, 1.0f);
    std::vector<int> oversizedBlocks { 2048, 1536, 3072, 1024 };

    VXDeverbAudioProcessor reference;
    reference.prepareToPlay(sr, 256);
    setParamNormalized(reference, "reduce", 0.72f);
    setParamNormalized(reference, "body", 0.35f);
    const auto refOut = render(reference, input, 256);

    VXDeverbAudioProcessor oversized;
    oversized.prepareToPlay(sr, 256);
    setParamNormalized(oversized, "reduce", 0.72f);
    setParamNormalized(oversized, "body", 0.35f);
    const auto oversizedOut = renderWithBlocks(oversized, input, oversizedBlocks);

    if (!allFinite(oversizedOut)) {
        std::cerr << "[VXSuitePluginRegression] Oversized host-block deverb output became non-finite\n";
        return false;
    }
    const float corr = bufferCorrelationSkip(refOut, oversizedOut, 4096);
    if (corr < 0.985f) {
        std::cerr << "[VXSuitePluginRegression] Oversized host blocks changed deverb behaviour too much: corr="
                  << corr << "\n";
        return false;
    }
    return true;
}

bool testMultiRateAndBufferCoverage() {
    constexpr std::array<double, 2> sampleRates { 44100.0, 48000.0 };
    constexpr std::array<int, 3> blockSizes { 64, 256, 512 };

    for (const double sr : sampleRates) {
        auto speech = makeSpeechLike(sr, 0.45f);
        auto noise = makeNoise(sr, 0.45f, 0.06f);
        auto noisy = addBuffers(speech, noise);
        for (const int blockSize : blockSizes) {
            auto out = renderSubtractProximityFinishChain(sr, blockSize, noisy);
            if (out.getNumSamples() <= 0 || !allFinite(out) || peakAbs(out) > 1.10f) {
                std::cerr << "[VXSuitePluginRegression] Chain failed sample-rate/buffer coverage at sr="
                          << sr << " block=" << blockSize << "\n";
                return false;
            }
        }
    }
    return true;
}

bool testMonoStereoConsistency() {
    constexpr double sr = 48000.0;
    auto stereoInput = makeSpeechLike(sr, 1.0f);
    auto monoInput = makeMonoBuffer(stereoInput);

    VXToneAudioProcessor toneStereo;
    toneStereo.prepareToPlay(sr, 256);
    setParamNormalized(toneStereo, "bass", 0.70f);
    setParamNormalized(toneStereo, "treble", 0.66f);
    const auto stereoOut = render(toneStereo, stereoInput, 256);

    VXToneAudioProcessor toneMono;
    toneMono.prepareToPlay(sr, 256);
    setParamNormalized(toneMono, "bass", 0.70f);
    setParamNormalized(toneMono, "treble", 0.66f);
    const auto monoOut = render(toneMono, monoInput, 256);

    juce::AudioBuffer<float> stereoMid(1, stereoOut.getNumSamples());
    for (int i = 0; i < stereoOut.getNumSamples(); ++i)
        stereoMid.setSample(0, i, 0.5f * (stereoOut.getSample(0, i) + stereoOut.getSample(1, i)));

    const float corr = bufferCorrelationSkip(monoOut, stereoMid, 1024);
    if (corr < 0.98f) {
        std::cerr << "[VXSuitePluginRegression] Mono/stereo tone paths diverged too far: corr=" << corr << "\n";
        return false;
    }
    return true;
}

bool testLatencyBearingProcessorsDoNotReportLatencyAsTail() {
    constexpr double sr = 48000.0;

    VXDeverbAudioProcessor deverb;
    deverb.prepareToPlay(sr, 256);
    if (!(deverb.getLatencySamples() > 0 && deverb.getTailLengthSeconds() == 0.0)) {
        std::cerr << "[VXSuitePluginRegression] Deverb should report latency without pretending it has post-input tail audio\n";
        return false;
    }

    VXDenoiserAudioProcessor denoiser;
    denoiser.prepareToPlay(sr, 256);
    if (!(denoiser.getLatencySamples() > 0 && denoiser.getTailLengthSeconds() == 0.0)) {
        std::cerr << "[VXSuitePluginRegression] Denoiser should report latency without pretending it has post-input tail audio\n";
        return false;
    }

    VXSubtractAudioProcessor subtract;
    subtract.prepareToPlay(sr, 256);
    if (!(subtract.getLatencySamples() > 0 && subtract.getTailLengthSeconds() == 0.0)) {
        std::cerr << "[VXSuitePluginRegression] Subtract should report latency without pretending it has post-input tail audio\n";
        return false;
    }

    return true;
}

bool testTailReportingMatchesRenderedCarryover() {
    constexpr double sr = 48000.0;
    constexpr float renderSeconds = 0.22f;

    auto speech = makeSpeechLike(sr, renderSeconds);
    auto noisy = addBuffers(speech, makeNoise(sr, renderSeconds, 0.05f));

    auto verifyTailWindow = [&](const char* label,
                                auto& processor,
                                const juce::AudioBuffer<float>& input,
                                const int blockSize,
                                const float minTailRms,
                                const float maxLateTailRms) {
        if (processor.getTailLengthSeconds() <= 0.0)
            return true;
        const int reportedTailSamples = std::max(1, juce::roundToInt(processor.getTailLengthSeconds() * sr));
        const int extraTail = reportedTailSamples + static_cast<int>(sr * 0.10f);
        const auto rendered = renderWithTail(processor, input, extraTail, blockSize);
        const int start = input.getNumSamples();
        const int activeTailSamples = std::min(reportedTailSamples, rendered.getNumSamples() - start);
        if (activeTailSamples <= 0) {
            std::cerr << "[VXSuitePluginRegression] " << label
                      << " rendered no samples inside its reported tail window\n";
            return false;
        }

        juce::AudioBuffer<float> tailWindow(rendered.getNumChannels(), activeTailSamples);
        for (int ch = 0; ch < tailWindow.getNumChannels(); ++ch)
            tailWindow.copyFrom(ch, 0, rendered, ch, start, activeTailSamples);

        const float activeTailRms = rms(tailWindow);
        if (activeTailRms < minTailRms) {
            std::cerr << "[VXSuitePluginRegression] " << label
                      << " tail window was unexpectedly empty: rms=" << activeTailRms << "\n";
            return false;
        }

        const int lateStart = start + reportedTailSamples;
        if (lateStart < rendered.getNumSamples()) {
            juce::AudioBuffer<float> lateTail(rendered.getNumChannels(), rendered.getNumSamples() - lateStart);
            for (int ch = 0; ch < lateTail.getNumChannels(); ++ch)
                lateTail.copyFrom(ch, 0, rendered, ch, lateStart, lateTail.getNumSamples());
            const float lateRms = rms(lateTail);
            if (lateRms > maxLateTailRms) {
                std::cerr << "[VXSuitePluginRegression] " << label
                          << " stayed too active after its reported tail window: rms=" << lateRms << "\n";
                return false;
            }
        }

        return true;
    };

    VXDeverbAudioProcessor deverb;
    deverb.prepareToPlay(sr, 256);
    setParamNormalized(deverb, "reduce", 0.75f);
    setParamNormalized(deverb, "body", 0.35f);
    if (!verifyTailWindow("Deverb", deverb, speech, 256, 1.0e-3f, 1.5e-2f))
        return false;

    VXDenoiserAudioProcessor denoiser;
    denoiser.prepareToPlay(sr, 256);
    setParamNormalized(denoiser, "clean", 0.85f);
    setParamNormalized(denoiser, "guard", 0.55f);
    if (!verifyTailWindow("Denoiser", denoiser, noisy, 256, 1.0e-3f, 8.0e-3f))
        return false;

    VXSubtractAudioProcessor subtract;
    subtract.prepareToPlay(sr, 256);
    if (!primeSubtractLearn(subtract, sr))
        return false;
    setParamNormalized(subtract, "subtract", 0.75f);
    setParamNormalized(subtract, "protect", 0.45f);
    if (!verifyTailWindow("Subtract", subtract, noisy, 256, 1.0e-3f, 8.0e-3f))
        return false;

    return true;
}

bool testToneFrequencyResponseRegression() {
    constexpr double sr = 48000.0;
    constexpr int skip = 4096;

    auto low = makeSine(sr, 0.8f, 80.0f, 0.08f);
    auto mid = makeSine(sr, 0.8f, 1000.0f, 0.08f);
    auto high = makeSine(sr, 0.8f, 10000.0f, 0.04f);

    VXToneAudioProcessor bassBoost;
    bassBoost.prepareToPlay(sr, 256);
    setParamNormalized(bassBoost, "bass", 1.0f);
    setParamNormalized(bassBoost, "treble", 0.5f);
    const float lowBoost = rmsSkip(render(bassBoost, low, 256), skip) / std::max(rmsSkip(low, skip), 1.0e-6f);
    bassBoost.reset();
    setParamNormalized(bassBoost, "bass", 1.0f);
    setParamNormalized(bassBoost, "treble", 0.5f);
    const float midBoost = rmsSkip(render(bassBoost, mid, 256), skip) / std::max(rmsSkip(mid, skip), 1.0e-6f);
    bassBoost.reset();
    setParamNormalized(bassBoost, "bass", 0.75f);
    setParamNormalized(bassBoost, "treble", 0.5f);
    const float lowHalfBoost = rmsSkip(render(bassBoost, low, 256), skip) / std::max(rmsSkip(low, skip), 1.0e-6f);
    if (!(lowBoost > midBoost * 1.35f && lowBoost > lowHalfBoost * 1.10f)) {
        std::cerr << "[VXSuitePluginRegression] Tone bass control no longer boosts low frequencies more than mids\n";
        return false;
    }

    VXToneAudioProcessor trebleBoost;
    trebleBoost.prepareToPlay(sr, 256);
    setParamNormalized(trebleBoost, "bass", 0.5f);
    setParamNormalized(trebleBoost, "treble", 1.0f);
    const float highBoost = rmsSkip(render(trebleBoost, high, 256), skip) / std::max(rmsSkip(high, skip), 1.0e-6f);
    trebleBoost.reset();
    setParamNormalized(trebleBoost, "bass", 0.5f);
    setParamNormalized(trebleBoost, "treble", 1.0f);
    const float midTrebleBoost = rmsSkip(render(trebleBoost, mid, 256), skip) / std::max(rmsSkip(mid, skip), 1.0e-6f);
    trebleBoost.reset();
    setParamNormalized(trebleBoost, "bass", 0.5f);
    setParamNormalized(trebleBoost, "treble", 0.75f);
    const float highHalfBoost = rmsSkip(render(trebleBoost, high, 256), skip) / std::max(rmsSkip(high, skip), 1.0e-6f);
    if (!(highBoost > midTrebleBoost * 1.25f && highBoost > highHalfBoost * 1.08f)) {
        std::cerr << "[VXSuitePluginRegression] Tone treble control no longer boosts highs more than mids\n";
        return false;
    }

    return true;
}

bool testToneRefineFrequencyResponseAndTransparency() {
    constexpr double sr = 48000.0;
    constexpr int skip = 4096;

    auto mud  = makeSine(sr, 0.8f, 300.0f, 0.12f);
    auto mid  = makeSine(sr, 0.8f, 1000.0f, 0.12f);
    // Loud enough that the presence-peakiness detector (sample-derivative
    // threshold 0.1) actually classifies the tone as harsh.
    auto harsh = makeSine(sr, 0.8f, 3500.0f, 0.40f);
    auto midLoud = makeSine(sr, 0.8f, 1000.0f, 0.40f);

    // All controls at zero must be near-transparent.
    {
        VXToneRefineAudioProcessor refine;
        refine.prepareToPlay(sr, 256);
        setParamNormalized(refine, "mud", 0.0f);
        setParamNormalized(refine, "harshness", 0.0f);
        setParamNormalized(refine, "smooth", 0.0f);
        const float ratio = rmsSkip(render(refine, mid, 256), skip) / std::max(rmsSkip(mid, skip), 1.0e-6f);
        if (!(ratio > 0.90f && ratio < 1.10f)) {
            std::cerr << "[VXSuitePluginRegression] ToneRefine is not transparent at zero settings: ratio="
                      << ratio << "\n";
            return false;
        }
    }

    auto bandRatio = [&](const juce::AudioBuffer<float>& input, const float mudAmt, const float harshAmt) {
        VXToneRefineAudioProcessor refine;
        refine.prepareToPlay(sr, 256);
        setParamNormalized(refine, "mud", mudAmt);
        setParamNormalized(refine, "harshness", harshAmt);
        setParamNormalized(refine, "smooth", 0.0f);
        return rmsSkip(render(refine, input, 256), skip) / std::max(rmsSkip(input, skip), 1.0e-6f);
    };

    // Full Mud must cut the low-mid band more than the 1 kHz reference.
    const float mudBand = bandRatio(mud, 1.0f, 0.0f);
    const float mudRef  = bandRatio(mid, 1.0f, 0.0f);
    if (!(mudBand < 0.97f && mudRef > mudBand * 1.02f)) {
        std::cerr << "[VXSuitePluginRegression] ToneRefine Mud no longer attenuates low-mids selectively: "
                  << "band=" << mudBand << " ref=" << mudRef << "\n";
        return false;
    }

    // Full Harshness must cut the presence band more than the 1 kHz reference.
    const float harshBand = bandRatio(harsh, 0.0f, 1.0f);
    const float harshRef  = bandRatio(midLoud, 0.0f, 1.0f);
    if (!(harshBand < 0.97f && harshRef > harshBand * 1.02f)) {
        std::cerr << "[VXSuitePluginRegression] ToneRefine Harshness no longer attenuates presence selectively: "
                  << "band=" << harshBand << " ref=" << harshRef << "\n";
        return false;
    }

    return true;
}

// Synthetic burst + exponentially-decaying tail (same construction pattern as
// VXDeverbTests' makePathologicalBurst): a click every 37 samples with a 0.94
// per-sample feedback tail approximates a reverberant decay the LRSV estimator
// can act on.
juce::AudioBuffer<float> makeBurstWithTail(const double sampleRate, const float seconds) {
    const int samples = static_cast<int>(sampleRate * seconds);
    juce::AudioBuffer<float> buffer(2, samples);
    for (int i = 0; i < samples; ++i) {
        const float burst = ((i % 37) == 0) ? (i % 74 == 0 ? 1.0f : -1.0f) : 0.0f;
        const float tail = (i > 0 ? buffer.getSample(0, i - 1) * 0.94f : 0.0f);
        const float sample = 0.15f * (burst + tail);
        buffer.setSample(0, i, sample);
        buffer.setSample(1, i, sample);
    }
    return buffer;
}

float tailRmsFrom(const juce::AudioBuffer<float>& buffer, const int startSample) {
    double energy = 0.0;
    int count = 0;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
        const auto* s = buffer.getReadPointer(ch);
        for (int i = startSample; i < buffer.getNumSamples(); ++i) {
            energy += static_cast<double>(s[i]) * s[i];
            ++count;
        }
    }
    return count > 0 ? static_cast<float>(std::sqrt(energy / count)) : 0.0f;
}

bool testProximityDrynessReducesReverberantTail() {
    constexpr double sr = 48000.0;
    auto burst = makeBurstWithTail(sr, 1.2f);
    const int tailStart = static_cast<int>(sr * 1.0);

    VXProximityAudioProcessor far;
    far.prepareToPlay(sr, 256);
    setParamNormalized(far, "closer", 0.0f);
    setParamNormalized(far, "air", 0.0f);
    const float tailFar = tailRmsFrom(render(far, burst, 256), tailStart);

    VXProximityAudioProcessor close;
    close.prepareToPlay(sr, 256);
    setParamNormalized(close, "closer", 1.0f);
    setParamNormalized(close, "air", 0.0f);
    const float tailClose = tailRmsFrom(render(close, burst, 256), tailStart);

    if (!(tailClose < tailFar * 0.85f)) {
        std::cerr << "[VXSuitePluginRegression] Proximity 'closer' no longer reduces reverberant "
                     "tail energy: far=" << tailFar << " close=" << tailClose << "\n";
        return false;
    }

    // Latency must actually be reported so hosts PDC-align (render() above
    // relies on getLatencySamples() to line up the comparison correctly).
    if (close.getLatencySamples() <= 0) {
        std::cerr << "[VXSuitePluginRegression] Proximity reports zero latency despite the "
                     "dryness stage — PDC alignment would be wrong\n";
        return false;
    }

    return true;
}

bool testProximityAndFinishFrequencyResponseRegression() {
    constexpr double sr = 48000.0;
    constexpr int skip = 4096;

    auto low = makeSine(sr, 0.8f, 90.0f, 0.08f);
    auto presence = makeSine(sr, 0.8f, 3800.0f, 0.08f);
    auto mid = makeSine(sr, 0.8f, 1000.0f, 0.08f);
    auto high = makeSine(sr, 0.8f, 9000.0f, 0.04f);

    VXProximityAudioProcessor proximity;
    proximity.prepareToPlay(sr, 256);
    setParamNormalized(proximity, "closer", 1.0f);
    setParamNormalized(proximity, "air", 0.0f);
    const float lowCloser = rmsSkip(render(proximity, low, 256), skip) / std::max(rmsSkip(low, skip), 1.0e-6f);
    proximity.reset();
    setParamNormalized(proximity, "closer", 1.0f);
    setParamNormalized(proximity, "air", 0.0f);
    const float midCloser = rmsSkip(render(proximity, mid, 256), skip) / std::max(rmsSkip(mid, skip), 1.0e-6f);
    if (!(lowCloser > midCloser * 1.20f)) {
        std::cerr << "[VXSuitePluginRegression] Proximity closer control no longer favors low-frequency boost\n";
        return false;
    }

    proximity.reset();
    auto lowMid = makeSine(sr, 0.8f, 250.0f, 0.08f);
    setParamNormalized(proximity, "closer", 1.0f);
    setParamNormalized(proximity, "air", 0.0f);
    const float lowMidCloser = rmsSkip(render(proximity, lowMid, 256), skip) / std::max(rmsSkip(lowMid, skip), 1.0e-6f);
    if (!(lowCloser > lowMidCloser * 1.18f)) {
        std::cerr << "[VXSuitePluginRegression] Proximity closer control is still lifting low mids too much relative to true proximity bass\n";
        return false;
    }

    proximity.reset();
    setParamNormalized(proximity, "closer", 1.0f);
    setParamNormalized(proximity, "air", 0.0f);
    const float presenceCloser = rmsSkip(render(proximity, presence, 256), skip) / std::max(rmsSkip(presence, skip), 1.0e-6f);
    if (!(presenceCloser > midCloser * 1.04f)) {
        std::cerr << "[VXSuitePluginRegression] Proximity closer model no longer adds directness/presence beyond a broad mid boost\n";
        return false;
    }

    proximity.reset();
    setParamNormalized(proximity, "closer", 0.0f);
    setParamNormalized(proximity, "air", 1.0f);
    const float highAir = rmsSkip(render(proximity, high, 256), skip) / std::max(rmsSkip(high, skip), 1.0e-6f);
    proximity.reset();
    setParamNormalized(proximity, "closer", 0.0f);
    setParamNormalized(proximity, "air", 1.0f);
    const float midAir = rmsSkip(render(proximity, mid, 256), skip) / std::max(rmsSkip(mid, skip), 1.0e-6f);
    if (!(highAir > midAir * 1.12f)) {
        std::cerr << "[VXSuitePluginRegression] Proximity air control no longer favors high-frequency boost\n";
        return false;
    }

    VXFinishAudioProcessor finish;
    finish.prepareToPlay(sr, 256);
    setParamNormalized(finish, "finish", 0.0f);
    setParamNormalized(finish, "body", 1.0f);
    setParamNormalized(finish, "gain", 0.5f);
    const float lowBody = rmsSkip(render(finish, low, 256), skip) / std::max(rmsSkip(low, skip), 1.0e-6f);
    finish.reset();
    setParamNormalized(finish, "finish", 0.0f);
    setParamNormalized(finish, "body", 1.0f);
    setParamNormalized(finish, "gain", 0.5f);
    const float midBody = rmsSkip(render(finish, mid, 256), skip) / std::max(rmsSkip(mid, skip), 1.0e-6f);
    if (!(lowBody > midBody * 1.08f)) {
        std::cerr << "[VXSuitePluginRegression] Finish body control no longer favors low-frequency enhancement\n";
        return false;
    }

    return true;
}

bool testNoSteadyStateAllocationsOnAudioThread() {
    constexpr double sr = 48000.0;
    auto noisy = addBuffers(makeSpeechLike(sr, 0.4f), makeNoise(sr, 0.4f, 0.05f));
    auto speech = makeSpeechLike(sr, 0.4f);

    VXDenoiserAudioProcessor denoiser;
    denoiser.prepareToPlay(sr, 256);
    setParamNormalized(denoiser, "clean", 0.70f);
    setParamNormalized(denoiser, "guard", 0.55f);
    if (!expectNoSteadyStateAllocations("denoiser", denoiser, noisy))
        return false;

    VXDeverbAudioProcessor deverb;
    deverb.prepareToPlay(sr, 256);
    setParamNormalized(deverb, "reduce", 0.72f);
    setParamNormalized(deverb, "body", 0.35f);
    if (!expectNoSteadyStateAllocations("deverb", deverb, speech))
        return false;

    VXSubtractAudioProcessor subtract;
    subtract.prepareToPlay(sr, 256);
    setParamNormalized(subtract, "subtract", 0.55f);
    setParamNormalized(subtract, "protect", 0.45f);
    setParamNormalized(subtract, "learn", 0.0f);
    if (!expectNoSteadyStateAllocations("subtract", subtract, noisy))
        return false;

    VXFinishAudioProcessor finish;
    finish.prepareToPlay(sr, 256);
    setParamNormalized(finish, "finish", 0.45f);
    setParamNormalized(finish, "body", 0.35f);
    setParamNormalized(finish, "gain", 0.55f);
    if (!expectNoSteadyStateAllocations("finish", finish, speech))
        return false;

    VXOptoCompAudioProcessor opto;
    opto.prepareToPlay(sr, 256);
    setParamNormalized(opto, "peak_reduction", 0.45f);
    setParamNormalized(opto, "body", 0.55f);
    setParamNormalized(opto, "gain", 0.55f);
    if (!expectNoSteadyStateAllocations("optocomp", opto, speech))
        return false;

    VXProximityAudioProcessor proximity;
    proximity.prepareToPlay(sr, 256);
    setParamNormalized(proximity, "closer", 0.55f);
    setParamNormalized(proximity, "air", 0.35f);
    if (!expectNoSteadyStateAllocations("proximity", proximity, speech))
        return false;

    VXToneAudioProcessor tone;
    tone.prepareToPlay(sr, 256);
    setParamNormalized(tone, "bass", 0.72f);
    setParamNormalized(tone, "treble", 0.34f);
    if (!expectNoSteadyStateAllocations("tone", tone, speech))
        return false;

    VXLevelerAudioProcessor leveler;
    leveler.prepareToPlay(sr, 256);
    setParamNormalized(leveler, "mode", 1.0f);
    setParamNormalized(leveler, "level", 0.72f);
    setParamNormalized(leveler, "control", 0.58f);
    if (!expectNoSteadyStateAllocations("leveler", leveler, noisy))
        return false;

    VXProximityClassicAudioProcessor proximityClassic;
    proximityClassic.prepareToPlay(sr, 256);
    setParamNormalized(proximityClassic, "closer", 0.55f);
    setParamNormalized(proximityClassic, "air", 0.35f);
    if (!expectNoSteadyStateAllocations("proximityClassic", proximityClassic, speech))
        return false;

    VXToneRefineAudioProcessor toneRefine;
    toneRefine.prepareToPlay(sr, 256);
    setParamNormalized(toneRefine, "mud", 0.60f);
    setParamNormalized(toneRefine, "harshness", 0.60f);
    setParamNormalized(toneRefine, "smooth", 0.50f);
    if (!expectNoSteadyStateAllocations("toneRefine", toneRefine, speech))
        return false;

    VXSpeechClarityAudioProcessor speechClarity;
    speechClarity.prepareToPlay(sr, 256);
    setParamNormalized(speechClarity, "sibilance", 0.55f);
    setParamNormalized(speechClarity, "plosive", 0.55f);
    setParamNormalized(speechClarity, "breath", 0.55f);
    setParamNormalized(speechClarity, "click", 0.55f);
    if (!expectNoSteadyStateAllocations("speechClarity", speechClarity, speech))
        return false;

    VXRebalanceAudioProcessor rebalance;
    rebalance.prepareToPlay(sr, 256);
    setParamNormalized(rebalance, "vocals", 0.80f);
    setParamNormalized(rebalance, "drums", 0.30f);
    setParamNormalized(rebalance, "strength", 0.70f);
    if (!expectNoSteadyStateAllocations("rebalance", rebalance, noisy))
        return false;

    VXRepairAudioProcessor repair;
    repair.prepareToPlay(sr, 256);
    setParamNormalized(repair, "noise_on", 1.0f);
    setParamNormalized(repair, "reverb_on", 1.0f);
    setParamNormalized(repair, "clarity_on", 1.0f);
    setParamNormalized(repair, "click_on", 1.0f);
    setParamNormalized(repair, "noise_strength", 0.60f);
    setParamNormalized(repair, "reverb_strength", 0.60f);
    setParamNormalized(repair, "clarity_strength", 0.60f);
    setParamNormalized(repair, "click_strength", 0.60f);
    if (!expectNoSteadyStateAllocations("repair", repair, noisy))
        return false;

    VXDeepFilterNetAudioProcessor deepFilter;
    deepFilter.prepareToPlay(sr, 256);
    setParamNormalized(deepFilter, "clean", 0.60f);
    setParamNormalized(deepFilter, "guard", 0.40f);
    if (!expectNoSteadyStateAllocations("deepfilternet", deepFilter, noisy))
        return false;

    VXStudioAnalyserAudioProcessor analyser;
    analyser.prepareToPlay(sr, 256);
    if (!expectNoSteadyStateAllocations("analyser", analyser, speech))
        return false;

    return true;
}

} // namespace

void* operator new(std::size_t size) {
    if (gAllocationTrackingEnabled.load(std::memory_order_relaxed))
        gTrackedAllocations.fetch_add(1, std::memory_order_relaxed);
    if (void* ptr = std::malloc(size))
        return ptr;
    throw std::bad_alloc();
}

void operator delete(void* ptr) noexcept {
    std::free(ptr);
}

void operator delete(void* ptr, std::size_t) noexcept {
    std::free(ptr);
}

void* operator new[](std::size_t size) {
    if (gAllocationTrackingEnabled.load(std::memory_order_relaxed))
        gTrackedAllocations.fetch_add(1, std::memory_order_relaxed);
    if (void* ptr = std::malloc(size))
        return ptr;
    throw std::bad_alloc();
}

void operator delete[](void* ptr) noexcept {
    std::free(ptr);
}

void operator delete[](void* ptr, std::size_t) noexcept {
    std::free(ptr);
}

int main(int argc, char* argv[]) {
    bool ok = true;
    int passCount = 0;
    int failCount = 0;
    const juce::String filter = argc > 1 ? juce::String(argv[1]) : juce::String();
    const auto run = [&](const char* name, auto&& fn) {
        if (filter.isNotEmpty() && !juce::String(name).containsIgnoreCase(filter))
            return;

        const bool passed = fn();
        ok &= passed;
        if (passed) {
            ++passCount;
            std::cerr << "[VXSuitePluginRegression] PASS " << name << "\n";
        } else {
            ++failCount;
            std::cerr << "[VXSuitePluginRegression] FAIL " << name << "\n";
        }
    };
    run("testSubtractLearnStartsOnFirstPress", testSubtractLearnStartsOnFirstPress);
    run("testSubtractSilentLearnDoesNotCreateProfileOrMuteOutput", testSubtractSilentLearnDoesNotCreateProfileOrMuteOutput);
    run("testSubtractLearnLifecycleMakesSense", testSubtractLearnLifecycleMakesSense);
    run("testSubtractListenOutputsMeaningfulRemovedDelta", testSubtractListenOutputsMeaningfulRemovedDelta);
    run("testSubtractLearnedProfileSurvivesResetAndStillActs", testSubtractLearnedProfileSurvivesResetAndStillActs);
    run("testSubtractActsAtStartOfRenderedAudio", testSubtractActsAtStartOfRenderedAudio);
    run("testSubtractGeneralModeStaysUsefulAndNonSilent", testSubtractGeneralModeStaysUsefulAndNonSilent);
    run("testSubtractBlindProtectAndModeAreAudible", testSubtractBlindProtectAndModeAreAudible);
    run("testSubtractListenIsSilentWhenNoRemoval", testSubtractListenIsSilentWhenNoRemoval);
    run("testSubtractStaleLearnedProfileBacksOffOnMismatch", testSubtractStaleLearnedProfileBacksOffOnMismatch);
    run("testRebalanceInstancesStayTrackLocal", testRebalanceInstancesStayTrackLocal);
    run("testRebalanceBacksOffOnLowConfidenceMaterial", testRebalanceBacksOffOnLowConfidenceMaterial);
    run("testRebalanceSeparatesGuitarFromOtherMoreClearly", testRebalanceSeparatesGuitarFromOtherMoreClearly);
    run("testRebalancePhoneModeStillActsOnRoughMaterial", testRebalancePhoneModeStillActsOnRoughMaterial);
    run("testRebalanceStrongSettingsStayHeadroomSafeAcrossRecordingTypes", testRebalanceStrongSettingsStayHeadroomSafeAcrossRecordingTypes);
    run("testSubtractStereoLearnTreatsChannelsIndependently", testSubtractStereoLearnTreatsChannelsIndependently);
    run("testDeverbExtremeBlendStaysStable", testDeverbExtremeBlendStaysStable);
    run("testDeverbStrongSettingActuallyReducesSyntheticRoomTail", testDeverbStrongSettingActuallyReducesSyntheticRoomTail);
    run("testDeverbLateTailIsReducedMoreThanEarlyBody", testDeverbLateTailIsReducedMoreThanEarlyBody);
    run("testSubtractProximityFinishChainStaysStable", testSubtractProximityFinishChainStaysStable);
    run("testSpeechClarityDeclickDoesNotAmplifyClicks", testSpeechClarityDeclickDoesNotAmplifyClicks);
    run("testSpeechClarityWetPathAndListenDeltaAreAudible", testSpeechClarityWetPathAndListenDeltaAreAudible);
    run("testFinishStrongSettingsAreAudibleButBounded", testFinishStrongSettingsAreAudibleButBounded);
    run("testFinishGainIsBipolarAroundCenter", testFinishGainIsBipolarAroundCenter);
    run("testFinishResetIsDeterministic", testFinishResetIsDeterministic);
    run("testFinishZeroAmountIsIdleAndTransparent", testFinishZeroAmountIsIdleAndTransparent);
    run("testOptoCompZeroAmountIsIdleAndTransparent", testOptoCompZeroAmountIsIdleAndTransparent);
    run("testFinishAndOptoCompBrightStressStayTruePeakSafe", testFinishAndOptoCompBrightStressStayTruePeakSafe);
    run("testToneCenterIsIdentityAndExtremesStayBounded", testToneCenterIsIdentityAndExtremesStayBounded);
    run("testStackedProximityToneFinishChainKeepsHeadroom", testStackedProximityToneFinishChainKeepsHeadroom);
    run("testFrameworkOutputTrimmerStaysMostlyIdleOnNominalStrongSettings", testFrameworkOutputTrimmerStaysMostlyIdleOnNominalStrongSettings);
    run("testProductLocalOutputTrimmersStayMostlyIdleOnNominalStrongSettings", testProductLocalOutputTrimmersStayMostlyIdleOnNominalStrongSettings);
    run("testLevelerZeroIsTransparentAndIdle", testLevelerZeroIsTransparentAndIdle);
    run("testLevelerImprovesLevelConsistencyOnHotInstrumentMix", testLevelerImprovesLevelConsistencyOnHotInstrumentMix);
    run("testLevelerGeneralTracksPresenceMoreThanBassAtEqualDrive", testLevelerGeneralTracksPresenceMoreThanBassAtEqualDrive);
    run("testLevelerOfflineAnalysisPersistsAcrossStateRestore", testLevelerOfflineAnalysisPersistsAcrossStateRestore);
    run("testProximityExtremeIsBoundedAndAdditive", testProximityExtremeIsBoundedAndAdditive);
    run("testProximityDefaultsAreNearNeutral", testProximityDefaultsAreNearNeutral);
    run("testProximityCloserKeepsLoudnessSteadierAcrossSources", testProximityCloserKeepsLoudnessSteadierAcrossSources);
    run("testProximityPhysicsBasedModelActsMonotonicallyWithCloser", testProximityPhysicsBasedModelActsMonotonicallyWithCloser);
    run("testProximityBassBoostGrowthIsNonlinearWithCloser", testProximityBassBoostGrowthIsNonlinearWithCloser);
    run("testProximityVoiceVsGeneralPatternFactorIsEvident", testProximityVoiceVsGeneralPatternFactorIsEvident);
    run("testToneAndProximityModesAreClearlyDifferentAtExtremes", testToneAndProximityModesAreClearlyDifferentAtExtremes);
    run("testDenoiserStrongSettingStaysCoherentAndBounded", testDenoiserStrongSettingStaysCoherentAndBounded);
    run("testDenoiserDoesNotAmplifyImpulsesOrLeakListenBuzz", testDenoiserDoesNotAmplifyImpulsesOrLeakListenBuzz);
    run("testDenoiserStrongSettingRetainsUsefulLevelInBothModes", testDenoiserStrongSettingRetainsUsefulLevelInBothModes);
    run("testDenoiserNoiseOnlyInputStillReducesNoiseInBothModes", testDenoiserNoiseOnlyInputStillReducesNoiseInBothModes);
    run("testDenoiserHybridCleanupPrefersHarshResidualOverVoicedTone", testDenoiserHybridCleanupPrefersHarshResidualOverVoicedTone);
    run("testDenoiserAndLevelerModesAreClearlyDifferentAtStrongSettings", testDenoiserAndLevelerModesAreClearlyDifferentAtStrongSettings);
    run("testDenoiserStereoTreatsChannelsIndependently", testDenoiserStereoTreatsChannelsIndependently);
    run("testDenoiserZeroCleanKeepsPdcAlignedIdentity", testDenoiserZeroCleanKeepsPdcAlignedIdentity);
    run("testDenoiserResetAndReprepareStayFinite", testDenoiserResetAndReprepareStayFinite);
    run("testDenoiserOversizedHostBlocksStayConsistent", testDenoiserOversizedHostBlocksStayConsistent);
    run("testDeepFilterOfflineRenderModeSwitchRecoversCleanly", testDeepFilterOfflineRenderModeSwitchRecoversCleanly);
    run("testDeepFilterStartupHoldbackReleasesValidProcessedAudio", testDeepFilterStartupHoldbackReleasesValidProcessedAudio);
    run("testDeepFilterGuardRespondsMoreOnArtifactHeavyInput", testDeepFilterGuardRespondsMoreOnArtifactHeavyInput);
    run("testDeepFilterFullGuardIsPdcAlignedDry", testDeepFilterFullGuardIsPdcAlignedDry);
    run("testDeepFilterListenIsRemovedContentAndGuardListenIsSilent", testDeepFilterListenIsRemovedContentAndGuardListenIsSilent);
    run("testDeepFilterConcurrentInstancesBothProcess", testDeepFilterConcurrentInstancesBothProcess);
    run("testAnalyserDomainBindingSurvivesMultipleDomains", testAnalyserDomainBindingSurvivesMultipleDomains);
    run("testSubtractZeroKeepsPdcAlignedIdentity", testSubtractZeroKeepsPdcAlignedIdentity);
    run("testFullChainBlockSizeInvariance", testFullChainBlockSizeInvariance);
    run("testLifecycleAndStateRestore", testLifecycleAndStateRestore);
    run("testSubtractStateRestoreRejectsMismatchedProfileFormat", testSubtractStateRestoreRejectsMismatchedProfileFormat);
    run("testSubtractResetKeepsLearningArmed", testSubtractResetKeepsLearningArmed);
    run("testListenSemanticsAcrossPlugins", testListenSemanticsAcrossPlugins);
    run("testOversizedHostBlocksStayConsistent", testOversizedHostBlocksStayConsistent);
    run("testMultiRateAndBufferCoverage", testMultiRateAndBufferCoverage);
    run("testMonoStereoConsistency", testMonoStereoConsistency);
    run("testLatencyBearingProcessorsDoNotReportLatencyAsTail", testLatencyBearingProcessorsDoNotReportLatencyAsTail);
    run("testTailReportingMatchesRenderedCarryover", testTailReportingMatchesRenderedCarryover);
    run("testToneFrequencyResponseRegression", testToneFrequencyResponseRegression);
    run("testToneRefineFrequencyResponseAndTransparency", testToneRefineFrequencyResponseAndTransparency);
    run("testProximityDrynessReducesReverberantTail", testProximityDrynessReducesReverberantTail);
    run("testProximityAndFinishFrequencyResponseRegression", testProximityAndFinishFrequencyResponseRegression);
    run("testNoSteadyStateAllocationsOnAudioThread", testNoSteadyStateAllocationsOnAudioThread);
    run("testCombinedChainKeepsSilenceSilent", testCombinedChainKeepsSilenceSilent);
    std::cerr << "[VXSuitePluginRegression] summary: " << passCount << " passed, "
              << failCount << " failed";
    if (filter.isNotEmpty())
        std::cerr << " (filter: " << filter << ")";
    std::cerr << "\n";
    return ok ? 0 : 1;
}
