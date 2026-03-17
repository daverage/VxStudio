#pragma once

#include <cmath>
#include <random>

#include <juce_audio_processors/juce_audio_processors.h>

namespace vxsuite::test {

inline float clamp01(const float x) {
    return juce::jlimit(0.0f, 1.0f, x);
}

template <typename Processor>
inline void setParamNormalized(Processor& processor, const char* paramId, const float value) {
    if (auto* p = processor.getValueTreeState().getParameter(paramId))
        p->setValueNotifyingHost(clamp01(value));
}

inline juce::AudioBuffer<float> makeSpeechLike(const double sampleRate, const float seconds) {
    const int samples = static_cast<int>(sampleRate * seconds);
    juce::AudioBuffer<float> buffer(2, samples);
    for (int i = 0; i < samples; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(sampleRate);
        float env = 0.45f + 0.35f * std::sin(2.0f * juce::MathConstants<float>::pi * 3.1f * t);
        if (t > seconds * 0.72f) {
            const float fade = juce::jlimit(0.0f, 1.0f, 1.0f - (t - seconds * 0.72f) / (seconds * 0.20f));
            env *= fade;
        }
        const float voiced = 0.42f * std::sin(2.0f * juce::MathConstants<float>::pi * 140.0f * t)
                           + 0.18f * std::sin(2.0f * juce::MathConstants<float>::pi * 840.0f * t)
                           + 0.11f * std::sin(2.0f * juce::MathConstants<float>::pi * 2300.0f * t);
        const float consonant = 0.08f * std::sin(2.0f * juce::MathConstants<float>::pi * 4100.0f * t)
                              * (0.5f + 0.5f * std::sin(2.0f * juce::MathConstants<float>::pi * 9.0f * t));
        const float sample = env * (voiced + consonant);
        buffer.setSample(0, i, sample);
        buffer.setSample(1, i, sample * (0.98f + 0.02f * std::sin(2.0f * juce::MathConstants<float>::pi * 0.7f * t)));
    }
    return buffer;
}

inline juce::AudioBuffer<float> makeNoise(const double sampleRate, const float seconds, const float gain = 0.08f) {
    const int samples = static_cast<int>(sampleRate * seconds);
    juce::AudioBuffer<float> buffer(2, samples);
    std::mt19937 rng(1337);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    for (int i = 0; i < samples; ++i) {
        const float n = gain * dist(rng);
        buffer.setSample(0, i, n);
        buffer.setSample(1, i, 0.97f * n);
    }
    return buffer;
}

inline juce::AudioBuffer<float> addBuffers(const juce::AudioBuffer<float>& a,
                                           const juce::AudioBuffer<float>& b) {
    const int channels = std::min(a.getNumChannels(), b.getNumChannels());
    const int samples = std::min(a.getNumSamples(), b.getNumSamples());
    juce::AudioBuffer<float> mixed(channels, samples);
    mixed.clear();
    for (int ch = 0; ch < channels; ++ch) {
        mixed.copyFrom(ch, 0, a, ch, 0, samples);
        mixed.addFrom(ch, 0, b, ch, 0, samples);
    }
    return mixed;
}

template <typename Processor>
inline juce::AudioBuffer<float> render(Processor& processor,
                                       const juce::AudioBuffer<float>& input,
                                       const int blockSize = 256) {
    const int latency = std::max(0, processor.getLatencySamples());
    juce::AudioBuffer<float> staged(input.getNumChannels(), input.getNumSamples() + latency);
    staged.clear();
    for (int ch = 0; ch < input.getNumChannels(); ++ch)
        staged.copyFrom(ch, 0, input, ch, 0, input.getNumSamples());

    juce::AudioBuffer<float> rendered(input.getNumChannels(), staged.getNumSamples());
    juce::MidiBuffer midi;
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

template <typename Processor>
inline void processSingleBlock(Processor& processor, juce::AudioBuffer<float>& block) {
    juce::MidiBuffer midi;
    processor.processBlock(block, midi);
}

inline float rms(const juce::AudioBuffer<float>& buffer) {
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

inline float peakAbs(const juce::AudioBuffer<float>& buffer) {
    float peak = 0.0f;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
        const auto* data = buffer.getReadPointer(ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            peak = std::max(peak, std::abs(data[i]));
    }
    return peak;
}

inline float maxAbsDiff(const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b) {
    float maxDiff = 0.0f;
    const int channels = std::min(a.getNumChannels(), b.getNumChannels());
    const int samples = std::min(a.getNumSamples(), b.getNumSamples());
    for (int ch = 0; ch < channels; ++ch) {
        const auto* aa = a.getReadPointer(ch);
        const auto* bb = b.getReadPointer(ch);
        for (int i = 0; i < samples; ++i)
            maxDiff = std::max(maxDiff, std::abs(aa[i] - bb[i]));
    }
    return maxDiff;
}

inline float maxAbsDiffSkip(const juce::AudioBuffer<float>& a,
                            const juce::AudioBuffer<float>& b,
                            const int skipSamples) {
    float maxDiff = 0.0f;
    const int channels = std::min(a.getNumChannels(), b.getNumChannels());
    const int samples = std::min(a.getNumSamples(), b.getNumSamples());
    const int start = juce::jlimit(0, samples, skipSamples);
    for (int ch = 0; ch < channels; ++ch) {
        const auto* aa = a.getReadPointer(ch);
        const auto* bb = b.getReadPointer(ch);
        for (int i = start; i < samples; ++i)
            maxDiff = std::max(maxDiff, std::abs(aa[i] - bb[i]));
    }
    return maxDiff;
}

inline bool allFinite(const juce::AudioBuffer<float>& buffer) {
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
        const auto* data = buffer.getReadPointer(ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i) {
            if (!std::isfinite(data[i]))
                return false;
        }
    }
    return true;
}

inline float speechBandCorrelation(const juce::AudioBuffer<float>& ref,
                                   const juce::AudioBuffer<float>& test,
                                   const double sampleRate) {
    const int channels = std::min(ref.getNumChannels(), test.getNumChannels());
    const int samples = std::min(ref.getNumSamples(), test.getNumSamples());
    const float hpA = std::exp(-2.0f * juce::MathConstants<float>::pi * 120.0f / static_cast<float>(sampleRate));
    const float lpA = std::exp(-2.0f * juce::MathConstants<float>::pi * 4200.0f / static_cast<float>(sampleRate));
    float refHp = 0.0f, testHp = 0.0f;
    float refBp = 0.0f, testBp = 0.0f;
    double dot = 0.0;
    double refEnergy = 0.0;
    double testEnergy = 0.0;
    for (int i = 0; i < samples; ++i) {
        float refMono = 0.0f;
        float testMono = 0.0f;
        for (int ch = 0; ch < channels; ++ch) {
            refMono += ref.getSample(ch, i);
            testMono += test.getSample(ch, i);
        }
        refMono /= static_cast<float>(channels);
        testMono /= static_cast<float>(channels);
        refHp = hpA * refHp + (1.0f - hpA) * refMono;
        testHp = hpA * testHp + (1.0f - hpA) * testMono;
        refBp = lpA * refBp + (1.0f - lpA) * (refMono - refHp);
        testBp = lpA * testBp + (1.0f - lpA) * (testMono - testHp);
        dot += static_cast<double>(refBp) * testBp;
        refEnergy += static_cast<double>(refBp) * refBp;
        testEnergy += static_cast<double>(testBp) * testBp;
    }
    return static_cast<float>(dot / std::sqrt(std::max(1.0e-12, refEnergy * testEnergy)));
}

inline float bufferCorrelationSkip(const juce::AudioBuffer<float>& a,
                                   const juce::AudioBuffer<float>& b,
                                   const int skipSamples = 0) {
    const int channels = std::min(a.getNumChannels(), b.getNumChannels());
    const int samples = std::min(a.getNumSamples(), b.getNumSamples());
    const int start = juce::jlimit(0, samples, skipSamples);
    double dot = 0.0;
    double aEnergy = 0.0;
    double bEnergy = 0.0;
    for (int i = start; i < samples; ++i) {
        float aMono = 0.0f;
        float bMono = 0.0f;
        for (int ch = 0; ch < channels; ++ch) {
            aMono += a.getSample(ch, i);
            bMono += b.getSample(ch, i);
        }
        aMono /= static_cast<float>(channels);
        bMono /= static_cast<float>(channels);
        dot += static_cast<double>(aMono) * bMono;
        aEnergy += static_cast<double>(aMono) * aMono;
        bEnergy += static_cast<double>(bMono) * bMono;
    }
    return static_cast<float>(dot / std::sqrt(std::max(1.0e-12, aEnergy * bEnergy)));
}

} // namespace vxsuite::test
