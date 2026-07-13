#include "../Source/vxstudio/products/rebalance/ai/VxAiStemRebalanceDsp.h"

#include <cmath>
#include <iostream>

namespace {

constexpr int kBlockSize = vxsuite::rebalance::kStemFrameSamples;
constexpr double kModelSampleRate = 44100.0;

vxsuite::rebalance::StemFrame makeSyntheticStemFrame() {
    vxsuite::rebalance::StemFrame frame;
    frame.available = true;
    frame.confidence = 1.0f;
    frame.sequenceNumber = 1;

    for (int sample = 0; sample < kBlockSize; ++sample) {
        const auto t = static_cast<double>(sample) / kModelSampleRate;
        const float vocal = 0.16f * std::sin(static_cast<float>(2.0 * juce::MathConstants<double>::pi * 330.0 * t));
        const float bass = 0.14f * std::sin(static_cast<float>(2.0 * juce::MathConstants<double>::pi * 82.0 * t));
        const float guitar = 0.10f * std::sin(static_cast<float>(2.0 * juce::MathConstants<double>::pi * 880.0 * t));
        const float other = 0.05f * std::sin(static_cast<float>(2.0 * juce::MathConstants<double>::pi * 1800.0 * t));

        for (int ch = 0; ch < vxsuite::rebalance::kStemFrameChannels; ++ch) {
            frame.mixture[static_cast<size_t>(ch)][static_cast<size_t>(sample)] = vocal + bass + guitar + other;
            frame.stems[static_cast<size_t>(vxsuite::rebalance::vocalsSource)][static_cast<size_t>(ch)][static_cast<size_t>(sample)] = vocal;
            frame.stems[static_cast<size_t>(vxsuite::rebalance::bassSource)][static_cast<size_t>(ch)][static_cast<size_t>(sample)] = bass;
            frame.stems[static_cast<size_t>(vxsuite::rebalance::guitarSource)][static_cast<size_t>(ch)][static_cast<size_t>(sample)] = guitar;
            frame.stems[static_cast<size_t>(vxsuite::rebalance::otherSource)][static_cast<size_t>(ch)][static_cast<size_t>(sample)] = other;
        }
    }

    return frame;
}

vxsuite::rebalance::StemFrame makeSingleStemFrame(const int source) {
    vxsuite::rebalance::StemFrame frame;
    frame.available = true;
    frame.confidence = 1.0f;
    frame.sequenceNumber = 1;

    for (int sample = 0; sample < kBlockSize; ++sample) {
        const auto t = static_cast<double>(sample) / kModelSampleRate;
        const float value = 0.18f * std::sin(static_cast<float>(2.0 * juce::MathConstants<double>::pi * 440.0 * t));
        for (int ch = 0; ch < vxsuite::rebalance::kStemFrameChannels; ++ch) {
            frame.mixture[static_cast<size_t>(ch)][static_cast<size_t>(sample)] = value;
            frame.stems[static_cast<size_t>(source)][static_cast<size_t>(ch)][static_cast<size_t>(sample)] = value;
        }
    }

    return frame;
}

double rms(const juce::AudioBuffer<float>& buffer) {
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

double goertzelMagnitude(const juce::AudioBuffer<float>& buffer, const double sampleRate, const double frequency) {
    const int n = buffer.getNumSamples();
    const double omega = 2.0 * juce::MathConstants<double>::pi * frequency / sampleRate;
    const double coeff = 2.0 * std::cos(omega);
    double q0 = 0.0;
    double q1 = 0.0;
    double q2 = 0.0;

    for (int i = 0; i < n; ++i) {
        q0 = coeff * q1 - q2 + buffer.getSample(0, i);
        q2 = q1;
        q1 = q0;
    }

    return std::sqrt(q1 * q1 + q2 * q2 - q1 * q2 * coeff);
}

juce::AudioBuffer<float> renderWithTargets(const double sampleRate,
                                           const int hostBlockSize,
                                           const std::array<float, vxsuite::rebalance::kControlCount>& targets) {
    vxsuite::rebalance::ai::StemRebalanceDsp dsp;
    dsp.prepare(sampleRate, hostBlockSize, 2);
    dsp.setControlTargets(targets);

    juce::AudioBuffer<float> buffer(2, hostBlockSize);
    for (std::uint64_t frameIndex = 1; frameIndex <= 16; ++frameIndex) {
        auto frame = makeSyntheticStemFrame();
        frame.sequenceNumber = frameIndex;
        dsp.setStemFrame(frame);
        buffer.clear();
        dsp.process(buffer);
    }
    return buffer;
}

bool runMixerCase(const double sampleRate, const int hostBlockSize) {
    std::array<float, vxsuite::rebalance::kControlCount> neutral {
        0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 1.0f
    };

    auto vocalBoost = neutral;
    vocalBoost[static_cast<size_t>(vxsuite::rebalance::vocalsSource)] = 0.75f;

    auto bassCut = neutral;
    bassCut[static_cast<size_t>(vxsuite::rebalance::bassSource)] = 0.25f;

    const auto neutralBuffer = renderWithTargets(sampleRate, hostBlockSize, neutral);
    const auto vocalBoostBuffer = renderWithTargets(sampleRate, hostBlockSize, vocalBoost);
    const auto bassCutBuffer = renderWithTargets(sampleRate, hostBlockSize, bassCut);

    const double neutralVocal = goertzelMagnitude(neutralBuffer, sampleRate, 330.0);
    const double boostedVocal = goertzelMagnitude(vocalBoostBuffer, sampleRate, 330.0);
    const double neutralBass = goertzelMagnitude(neutralBuffer, sampleRate, 82.0);
    const double cutBass = goertzelMagnitude(bassCutBuffer, sampleRate, 82.0);

    if (boostedVocal <= neutralVocal * 1.20) {
        std::cerr << "[VxAiStemMixerTests] Vocal boost did not raise vocal component enough at "
                  << sampleRate << " Hz block " << hostBlockSize << "\n";
        return false;
    }

    if (cutBass >= neutralBass * 0.82) {
        std::cerr << "[VxAiStemMixerTests] Bass cut did not reduce bass component enough at "
                  << sampleRate << " Hz block " << hostBlockSize << "\n";
        return false;
    }

    return true;
}

bool runMuteCase() {
    std::array<float, vxsuite::rebalance::kControlCount> targets {
        0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 1.0f
    };
    targets[static_cast<size_t>(vxsuite::rebalance::vocalsSource)] = 0.0f;

    vxsuite::rebalance::ai::StemRebalanceDsp dsp;
    dsp.prepare(48000.0, 256, 2);
    dsp.setRecordingType(vxsuite::rebalance::RecordingType::phoneRough);
    dsp.setControlTargets(targets);

    juce::AudioBuffer<float> buffer(2, 256);
    for (std::uint64_t frameIndex = 1; frameIndex <= 80; ++frameIndex) {
        auto frame = makeSingleStemFrame(vxsuite::rebalance::vocalsSource);
        frame.sequenceNumber = frameIndex;
        dsp.setStemFrame(frame);
        buffer.clear();
        dsp.process(buffer);
    }

    const double outputRms = rms(buffer);
    if (outputRms >= 1.0e-4) {
        std::cerr << "[VxAiStemMixerTests] -100% did not mute isolated vocal stem, rms="
                  << outputRms << "\n";
        return false;
    }

    return true;
}

} // namespace

int main() {
    if (!runMixerCase(44100.0, 512))
        return 1;
    if (!runMixerCase(48000.0, 256))
        return 1;
    if (!runMixerCase(96000.0, 320))
        return 1;
    if (!runMuteCase())
        return 1;

    return 0;
}
