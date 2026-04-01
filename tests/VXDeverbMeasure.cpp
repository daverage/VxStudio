#include "../Source/vxstudio/products/deverb/VxDeverbProcessor.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <random>
#include <vector>

namespace {

float clamp01(const float x) {
    return juce::jlimit(0.0f, 1.0f, x);
}

double tailRms(const juce::AudioBuffer<float>& buffer, int startSample);

juce::AudioBuffer<float> makeSpeechLike(const double sampleRate, const float seconds) {
    const int samples = static_cast<int>(sampleRate * seconds);
    juce::AudioBuffer<float> buffer(2, samples);
    for (int i = 0; i < samples; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(sampleRate);
        const float env = 0.50f + 0.35f * std::sin(2.0f * juce::MathConstants<float>::pi * 2.8f * t);
        const float voiced = 0.45f * std::sin(2.0f * juce::MathConstants<float>::pi * 160.0f * t)
                           + 0.20f * std::sin(2.0f * juce::MathConstants<float>::pi * 920.0f * t)
                           + 0.10f * std::sin(2.0f * juce::MathConstants<float>::pi * 2600.0f * t);
        const float sample = env * voiced;
        buffer.setSample(0, i, sample);
        buffer.setSample(1, i, sample * 0.995f);
    }
    return buffer;
}

juce::AudioBuffer<float> addRoomTail(const juce::AudioBuffer<float>& dry, const double sampleRate) {
    juce::AudioBuffer<float> wet(dry);
    const std::array<int, 5> delays {
        static_cast<int>(0.018 * sampleRate),
        static_cast<int>(0.033 * sampleRate),
        static_cast<int>(0.055 * sampleRate),
        static_cast<int>(0.082 * sampleRate),
        static_cast<int>(0.117 * sampleRate)
    };
    const std::array<float, 5> gains { 0.44f, 0.31f, 0.24f, 0.18f, 0.12f };
    for (int ch = 0; ch < wet.getNumChannels(); ++ch) {
        auto* dst = wet.getWritePointer(ch);
        for (int i = 0; i < wet.getNumSamples(); ++i) {
            float acc = dst[i];
            for (size_t tap = 0; tap < delays.size(); ++tap) {
                const int idx = i - delays[tap];
                if (idx >= 0)
                    acc += dry.getSample(ch, idx) * gains[tap];
            }
            dst[i] = acc;
        }
    }
    return wet;
}

juce::AudioBuffer<float> addSyntheticRt60Room(const juce::AudioBuffer<float>& dry,
                                              const double sampleRate,
                                              const float rt60Seconds) {
    const float safeRt60 = juce::jlimit(0.1f, 4.0f, rt60Seconds);
    const int rirSamples = std::max(1, static_cast<int>(std::ceil(sampleRate * safeRt60 * 1.5f)));
    std::vector<float> rir(static_cast<size_t>(rirSamples), 0.0f);
    std::mt19937 rng(12345);
    std::normal_distribution<float> dist(0.0f, 1.0f);

    rir[0] = 1.0f;
    for (int n = 1; n < rirSamples; ++n) {
        const float decay = std::exp(-6.908f * static_cast<float>(n)
                                     / (safeRt60 * static_cast<float>(sampleRate)));
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

juce::AudioBuffer<float> render(VXDeverbAudioProcessor& processor,
                                const juce::AudioBuffer<float>& input,
                                const float reduce,
                                const float body) {
    if (auto* p = processor.getValueTreeState().getParameter("reduce"))
        p->setValueNotifyingHost(clamp01(reduce));
    if (auto* p = processor.getValueTreeState().getParameter("body"))
        p->setValueNotifyingHost(clamp01(body));
    const int latency = std::max(0, processor.getLatencySamples());
    juce::AudioBuffer<float> staged(input.getNumChannels(), input.getNumSamples() + latency);
    staged.clear();
    for (int ch = 0; ch < input.getNumChannels(); ++ch)
        staged.copyFrom(ch, 0, input, ch, 0, input.getNumSamples());

    juce::AudioBuffer<float> rendered(input.getNumChannels(), staged.getNumSamples());
    juce::MidiBuffer midi;
    constexpr int blockSize = 256;
    for (int start = 0; start < staged.getNumSamples(); start += blockSize) {
        const int num = std::min(blockSize, staged.getNumSamples() - start);
        juce::AudioBuffer<float> block(staged.getNumChannels(), num);
        for (int ch = 0; ch < staged.getNumChannels(); ++ch)
            block.copyFrom(ch, 0, staged, ch, start, num);
        processor.processBlock(block, midi);
        for (int ch = 0; ch < rendered.getNumChannels(); ++ch)
            rendered.copyFrom(ch, start, block, ch, 0, num);
    }

    juce::AudioBuffer<float> output(input.getNumChannels(), input.getNumSamples());
    for (int ch = 0; ch < output.getNumChannels(); ++ch)
        output.copyFrom(ch, 0, rendered, ch, latency, input.getNumSamples());
    return output;
}

struct Rt60Snapshot {
    int blockIndex = 0;
    float trackedRt60 = 0.0f;
    double rollingTailRatio = 0.0;
};

juce::AudioBuffer<float> renderWithTelemetry(VXDeverbAudioProcessor& processor,
                                             const juce::AudioBuffer<float>& input,
                                             const float reduce,
                                             const float body,
                                             const bool printRt60,
                                             std::vector<Rt60Snapshot>* snapshots) {
    if (!printRt60)
        return render(processor, input, reduce, body);

    if (auto* p = processor.getValueTreeState().getParameter("reduce"))
        p->setValueNotifyingHost(clamp01(reduce));
    if (auto* p = processor.getValueTreeState().getParameter("body"))
        p->setValueNotifyingHost(clamp01(body));
    const int latency = std::max(0, processor.getLatencySamples());
    juce::AudioBuffer<float> staged(input.getNumChannels(), input.getNumSamples() + latency);
    staged.clear();
    for (int ch = 0; ch < input.getNumChannels(); ++ch)
        staged.copyFrom(ch, 0, input, ch, 0, input.getNumSamples());

    juce::AudioBuffer<float> rendered(input.getNumChannels(), staged.getNumSamples());
    juce::MidiBuffer midi;
    constexpr int blockSize = 256;
    for (int start = 0, blockIndex = 0; start < staged.getNumSamples(); start += blockSize, ++blockIndex) {
        const int num = std::min(blockSize, staged.getNumSamples() - start);
        juce::AudioBuffer<float> block(staged.getNumChannels(), num);
        for (int ch = 0; ch < staged.getNumChannels(); ++ch)
            block.copyFrom(ch, 0, staged, ch, start, num);
        processor.processBlock(block, midi);
        for (int ch = 0; ch < rendered.getNumChannels(); ++ch)
            rendered.copyFrom(ch, start, block, ch, 0, num);

        if (snapshots != nullptr && (blockIndex == 0 || (blockIndex % 50) == 0)) {
            Rt60Snapshot snapshot;
            snapshot.blockIndex = blockIndex;
            snapshot.trackedRt60 = processor.getDebugTrackedRt60Seconds(0);
            if (start + num > latency) {
                juce::AudioBuffer<float> rolling(rendered.getNumChannels(), start + num - latency);
                for (int ch = 0; ch < rolling.getNumChannels(); ++ch)
                    rolling.copyFrom(ch, 0, rendered, ch, latency, rolling.getNumSamples());
                snapshot.rollingTailRatio =
                    tailRms(rolling, std::max(0, rolling.getNumSamples() / 2))
                    / std::max(1.0e-12, tailRms(input, std::max(0, rolling.getNumSamples() / 2)));
            } else {
                snapshot.rollingTailRatio = std::numeric_limits<double>::quiet_NaN();
            }
            snapshots->push_back(snapshot);
        }
    }

    juce::AudioBuffer<float> output(input.getNumChannels(), input.getNumSamples());
    for (int ch = 0; ch < output.getNumChannels(); ++ch)
        output.copyFrom(ch, 0, rendered, ch, latency, input.getNumSamples());
    return output;
}

double rms(const juce::AudioBuffer<float>& buffer) {
    double energy = 0.0;
    int count = 0;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
        const auto* data = buffer.getReadPointer(ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i) {
            energy += static_cast<double>(data[i]) * data[i];
            ++count;
        }
    }
    return std::sqrt(energy / std::max(1, count));
}

double diffRms(const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b) {
    double energy = 0.0;
    int count = 0;
    const int channels = std::min(a.getNumChannels(), b.getNumChannels());
    const int samples = std::min(a.getNumSamples(), b.getNumSamples());
    for (int ch = 0; ch < channels; ++ch) {
        const auto* aa = a.getReadPointer(ch);
        const auto* bb = b.getReadPointer(ch);
        for (int i = 0; i < samples; ++i) {
            const double d = static_cast<double>(aa[i]) - bb[i];
            energy += d * d;
            ++count;
        }
    }
    return std::sqrt(energy / std::max(1, count));
}

double correlation(const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b) {
    const int channels = std::min(a.getNumChannels(), b.getNumChannels());
    const int samples = std::min(a.getNumSamples(), b.getNumSamples());
    double dot = 0.0;
    double aEnergy = 0.0;
    double bEnergy = 0.0;
    for (int ch = 0; ch < channels; ++ch) {
        const auto* aa = a.getReadPointer(ch);
        const auto* bb = b.getReadPointer(ch);
        for (int i = 0; i < samples; ++i) {
            dot += static_cast<double>(aa[i]) * bb[i];
            aEnergy += static_cast<double>(aa[i]) * aa[i];
            bEnergy += static_cast<double>(bb[i]) * bb[i];
        }
    }
    return dot / std::sqrt(std::max(1.0e-12, aEnergy * bEnergy));
}

double tailRms(const juce::AudioBuffer<float>& buffer, const int startSample) {
    double energy = 0.0;
    int count = 0;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
        const auto* data = buffer.getReadPointer(ch);
        for (int i = startSample; i < buffer.getNumSamples(); ++i) {
            energy += static_cast<double>(data[i]) * data[i];
            ++count;
        }
    }
    return std::sqrt(energy / std::max(1, count));
}

int tailStartSample(const double sampleRate, const float synthRt60, const int totalSamples) {
    const int defaultStart = static_cast<int>(sampleRate * 1.1);
    if (synthRt60 <= 0.0f)
        return std::min(totalSamples - 1, std::max(0, defaultStart));
    const int lateStart = static_cast<int>(std::ceil(0.060 * sampleRate));
    return std::min(totalSamples - 1, std::max(0, lateStart));
}

juce::AudioBuffer<float> delayBuffer(const juce::AudioBuffer<float>& input, const int delaySamples) {
    juce::AudioBuffer<float> delayed(input.getNumChannels(), input.getNumSamples());
    delayed.clear();
    if (delaySamples <= 0) {
        delayed.makeCopyOf(input);
        return delayed;
    }
    const int count = std::max(0, input.getNumSamples() - delaySamples);
    for (int ch = 0; ch < input.getNumChannels(); ++ch)
        delayed.copyFrom(ch, delaySamples, input, ch, 0, count);
    return delayed;
}

} // namespace

int main(int argc, char** argv) {
    constexpr double sampleRate = 48000.0;
    float reduce = 1.0f;
    float body = 0.0f;
    float rt60Preset = 0.0f;
    float synthRt60 = 0.0f;
    float renderSeconds = 1.6f;
    bool printRt60 = false;
    float overSubtract = 1.5f;
    bool noCepstral = false;
    bool voiceMode  = false;

    for (int i = 1; i < argc; ++i) {
        const juce::String arg(argv[i]);
        if (arg == "--reduce" && i + 1 < argc)
            reduce = std::stof(argv[++i]);
        else if (arg == "--body" && i + 1 < argc)
            body = std::stof(argv[++i]);
        else if (arg == "--rt60-preset" && i + 1 < argc)
            rt60Preset = std::stof(argv[++i]);
        else if (arg == "--synth-rt60" && i + 1 < argc)
            synthRt60 = std::stof(argv[++i]);
        else if (arg == "--render-seconds" && i + 1 < argc)
            renderSeconds = std::stof(argv[++i]);
        else if (arg == "--print-rt60")
            printRt60 = true;
        else if (arg == "--over-subtract" && i + 1 < argc)
            overSubtract = std::stof(argv[++i]);
        else if (arg == "--no-cepstral")
            noCepstral = true;
        else if (arg == "--voice")
            voiceMode = true;
    }

    auto dry = makeSpeechLike(sampleRate, std::max(0.2f, renderSeconds));
    auto room = synthRt60 > 0.0f
        ? addSyntheticRt60Room(dry, sampleRate, synthRt60)
        : addRoomTail(dry, sampleRate);

    VXDeverbAudioProcessor processor;
    processor.setDebugDeterministicReset(rt60Preset <= 0.0f);
    processor.prepareToPlay(sampleRate, 256);
    processor.setDebugOverSubtract(overSubtract);
    processor.setDebugNoCepstral(noCepstral);
    processor.setVoiceMode(voiceMode);
    if (rt60Preset > 0.0f)
        processor.setDebugRt60PresetSeconds(rt60Preset);
    std::vector<Rt60Snapshot> snapshots;
    auto out = renderWithTelemetry(processor, room, reduce, body, printRt60, &snapshots);
    auto delayedDry = delayBuffer(dry, processor.getLatencySamples());

    const auto inRms = rms(room);
    const auto outRms = rms(out);
    const auto deltaRms = diffRms(room, out);
    const auto corr = correlation(room, out);
    const auto delayedCorr = correlation(delayedDry, out);
    const int tailStart = tailStartSample(sampleRate, synthRt60, room.getNumSamples());
    const auto tailIn = tailRms(room, tailStart);
    const auto tailOut = tailRms(out, tailStart);

    std::cout << "reduce=" << reduce
              << " body=" << body
              << " rt60_preset=" << rt60Preset
              << " synth_rt60=" << synthRt60
              << " render_seconds=" << renderSeconds
              << " over_subtract=" << overSubtract
              << " no_cepstral=" << (noCepstral ? 1 : 0)
              << " voice_mode=" << (voiceMode ? 1 : 0) << "\n";
    std::cout << "status=" << processor.getStatusText() << "\n";
    std::cout << "latency_samples=" << processor.getLatencySamples() << "\n";
    std::cout << "input_rms=" << inRms << "\n";
    std::cout << "output_rms=" << outRms << "\n";
    std::cout << "delta_rms=" << deltaRms << "\n";
    std::cout << "input_output_corr=" << corr << "\n";
    std::cout << "delayed_input_output_corr=" << delayedCorr << "\n";
    std::cout << "tail_start_sample=" << tailStart << "\n";
    std::cout << "tail_in_rms=" << tailIn << "\n";
    std::cout << "tail_out_rms=" << tailOut << "\n";
    std::cout << "tail_ratio=" << (tailOut / std::max(1.0e-12, tailIn)) << "\n";
    std::cout << "tracked_rt60_final=" << processor.getDebugTrackedRt60Seconds(0) << "\n";
    std::cout << "over_subtract_effective=" << processor.getDebugOverSubtract() << "\n";

    if (printRt60) {
        std::cout << "rt60_trace_begin\n";
        std::cout << "block\ttracked_rt60\trolling_tail_ratio\n";
        for (const auto& snapshot : snapshots) {
            std::cout << snapshot.blockIndex << "\t"
                      << snapshot.trackedRt60 << "\t";
            if (std::isnan(snapshot.rollingTailRatio))
                std::cout << "-";
            else
                std::cout << snapshot.rollingTailRatio;
            std::cout << "\n";
        }
        std::cout << "rt60_trace_end\n";
    }

    return 0;
}
