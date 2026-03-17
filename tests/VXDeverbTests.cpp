#include "../Source/vxsuite/products/deverb/VxDeverbProcessor.h"
#include "../Source/vxsuite/products/deverb/dsp/VxDeverbRt60Estimator.h"
#include "../Source/vxsuite/products/deverb/dsp/VxDeverbSpectralProcessor.h"

#include <cmath>
#include <iostream>
#include <random>
#include <vector>

namespace {

float clamp01(const float x) {
    return juce::jlimit(0.0f, 1.0f, x);
}

float onePoleAlpha(const double sampleRate, const float cutoffHz) {
    return std::exp(-2.0f * juce::MathConstants<float>::pi * cutoffHz / static_cast<float>(sampleRate));
}

juce::AudioBuffer<float> makeSpeechLike(const double sampleRate, const float seconds) {
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

juce::AudioBuffer<float> addRoomTail(const juce::AudioBuffer<float>& dry, const double sampleRate) {
    juce::AudioBuffer<float> wet(dry);
    const std::array<int, 4> delays {
        static_cast<int>(0.017 * sampleRate),
        static_cast<int>(0.031 * sampleRate),
        static_cast<int>(0.053 * sampleRate),
        static_cast<int>(0.081 * sampleRate)
    };
    const std::array<float, 4> gains { 0.42f, 0.28f, 0.20f, 0.12f };
    for (int ch = 0; ch < wet.getNumChannels(); ++ch) {
        auto* d = wet.getWritePointer(ch);
        for (int i = 0; i < wet.getNumSamples(); ++i) {
            float acc = d[i];
            for (size_t tap = 0; tap < delays.size(); ++tap) {
                const int idx = i - delays[tap];
                if (idx >= 0)
                    acc += dry.getSample(ch, idx) * gains[tap];
            }
            d[i] = acc;
        }
    }
    return wet;
}

juce::AudioBuffer<float> makePathologicalBurst(const double sampleRate, const float seconds) {
    const int samples = static_cast<int>(sampleRate * seconds);
    juce::AudioBuffer<float> buffer(2, samples);
    for (int i = 0; i < samples; ++i) {
        const float burst = ((i % 37) == 0) ? (i % 74 == 0 ? 1.0f : -1.0f) : 0.0f;
        const float tail = (i > 0 ? buffer.getSample(0, i - 1) * 0.94f : 0.0f);
        const float sample = burst + tail;
        buffer.setSample(0, i, sample);
        buffer.setSample(1, i, -sample);
    }
    return buffer;
}

float speechBandCorrelation(const juce::AudioBuffer<float>& ref, const juce::AudioBuffer<float>& test, const double sampleRate) {
    const int channels = std::min(ref.getNumChannels(), test.getNumChannels());
    const int samples = std::min(ref.getNumSamples(), test.getNumSamples());
    const float hpA = onePoleAlpha(sampleRate, 120.0f);
    const float lpA = onePoleAlpha(sampleRate, 4200.0f);
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

float tailRms(const juce::AudioBuffer<float>& buffer, const int startSample) {
    double energy = 0.0;
    int count = 0;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
        const auto* s = buffer.getReadPointer(ch);
        for (int i = startSample; i < buffer.getNumSamples(); ++i) {
            energy += static_cast<double>(s[i]) * s[i];
            ++count;
        }
    }
    return std::sqrt(energy / std::max(1, count));
}

float lowBandRatio(const juce::AudioBuffer<float>& buffer, const double sampleRate) {
    float lp = 0.0f;
    const float alpha = onePoleAlpha(sampleRate, 190.0f);
    double lowEnergy = 0.0;
    double fullEnergy = 0.0;
    for (int i = 0; i < buffer.getNumSamples(); ++i) {
        float mono = 0.0f;
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            mono += buffer.getSample(ch, i);
        mono /= static_cast<float>(buffer.getNumChannels());
        lp = alpha * lp + (1.0f - alpha) * mono;
        lowEnergy += static_cast<double>(lp) * lp;
        fullEnergy += static_cast<double>(mono) * mono;
    }
    return static_cast<float>(std::sqrt(lowEnergy / std::max(1.0e-12, fullEnergy)));
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

float maxAbsDiff(const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b) {
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

float correlation(const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b) {
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
    return static_cast<float>(dot / std::sqrt(std::max(1.0e-12, aEnergy * bEnergy)));
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

bool testReduceChangesAudioAndLowersTail() {
    constexpr double sr = 48000.0;
    auto dry = makeSpeechLike(sr, 1.5f);
    auto room = addRoomTail(dry, sr);

    VXDeverbAudioProcessor processor;
    processor.prepareToPlay(sr, room.getNumSamples());
    auto out = render(processor, room, 0.82f, 0.0f);

    const float corr = speechBandCorrelation(dry, out, sr);
    const float tailIn = tailRms(room, static_cast<int>(sr * 1.1));
    const float tailOut = tailRms(out, static_cast<int>(sr * 1.1));
    if (corr < 0.48f) {
        std::cerr << "[VXDeverbTests] expected deverb to keep acceptable speech correlation: corr="
                  << corr << "\n";
        return false;
    }
    if (!(tailOut < tailIn)) {
        std::cerr << "[VXDeverbTests] expected reduce to lower tail energy: in="
                  << tailIn << " out=" << tailOut << "\n";
        return false;
    }
    return true;
}

bool testReduceZeroIsIdentity() {
    constexpr double sr = 48000.0;
    auto dry = makeSpeechLike(sr, 0.9f);

    VXDeverbAudioProcessor processor;
    processor.setDebugDeterministicReset(true);
    processor.prepareToPlay(sr, 256);
    auto out = render(processor, dry, 0.0f, 0.0f);

    const float diff = maxAbsDiff(dry, out);
    if (diff > 1.0e-6f) {
        std::cerr << "[VXDeverbTests] expected reduce=0 identity: max_abs_diff=" << diff << "\n";
        return false;
    }
    return true;
}

bool testFullyWetStaysCoherentWithDelayedDry() {
    constexpr double sr = 48000.0;
    auto dry = makeSpeechLike(sr, 1.0f);

    VXDeverbAudioProcessor processor;
    processor.setDebugDeterministicReset(true);
    processor.setDebugRt60PresetSeconds(0.5f);
    processor.setDebugOverSubtract(1.0f);
    processor.prepareToPlay(sr, 256);
    auto out = render(processor, dry, 1.0f, 0.0f);
    auto delayedDry = delayBuffer(dry, processor.getLatencySamples());
    const float undelayedCorr = correlation(dry, out);

    const float corr = correlation(delayedDry, out);
    if (!(undelayedCorr > corr + 0.15f)) {
        std::cerr << "[VXDeverbTests] expected wet path to stay host-latency aligned: delayed_corr="
                  << corr << " undelayed_corr=" << undelayedCorr << "\n";
        return false;
    }
    return true;
}

bool testBodyRestoreIsIntentional() {
    constexpr double sr = 48000.0;
    auto dry = makeSpeechLike(sr, 1.2f);
    auto room = addRoomTail(dry, sr);

    VXDeverbAudioProcessor processor;
    processor.setDebugDeterministicReset(true);
    processor.prepareToPlay(sr, room.getNumSamples());
    auto bodyOff = render(processor, room, 0.76f, 0.0f);
    processor.reset();
    auto bodyOn = render(processor, room, 0.76f, 1.0f);

    const float lowOff = lowBandRatio(bodyOff, sr);
    const float lowOn = lowBandRatio(bodyOn, sr);
    const float lowDry = lowBandRatio(dry, sr);
    if (!(lowOn > lowOff + 0.01f)) {
        std::cerr << "[VXDeverbTests] expected Body to restore low band: off=" << lowOff
                  << " on=" << lowOn << "\n";
        return false;
    }
    if (!(lowOn < lowDry * 1.18f)) {
        std::cerr << "[VXDeverbTests] Body over-restored low band: on=" << lowOn
                  << " dry=" << lowDry << "\n";
        return false;
    }
    return true;
}

bool testRt60Estimator() {
    using vxsuite::deverb::LollmannRt60Estimator;
    constexpr double sr = 48000.0;
    constexpr float  groundTruthRt60 = 0.8f;
    constexpr int    blockSize = 512;

    LollmannRt60Estimator est;
    est.prepare(sr);

    // Synthetic exponential-decay white noise: x[n] = N(0,1) * exp(-6.908 * n / (RT60 * sr))
    const int totalSamples = static_cast<int>(sr * 5.0);
    std::mt19937 rng(42);
    std::normal_distribution<float> dist(0.0f, 1.0f);
    std::vector<float> signal(static_cast<size_t>(totalSamples));
    for (int n = 0; n < totalSamples; ++n) {
        const float decay = std::exp(-6.908f * static_cast<float>(n)
                                     / (groundTruthRt60 * static_cast<float>(sr)));
        signal[static_cast<size_t>(n)] = dist(rng) * decay;
    }

    for (int start = 0; start < totalSamples; start += blockSize) {
        const int count = std::min(blockSize, totalSamples - start);
        est.pushSamples(signal.data() + start, count);
    }

    const float estimate = est.getEstimatedRt60();
    const float tolerance = 0.40f * groundTruthRt60;
    if (std::abs(estimate - groundTruthRt60) > tolerance) {
        std::cerr << "[VXDeverbTests] RT60 estimate out of range: got=" << estimate
                  << " expected=" << groundTruthRt60 << " ±" << tolerance << "\n";
        return false;
    }

    // After reset, should return default (0.5 s)
    est.reset();
    if (est.getEstimatedRt60() != 0.5f) {
        std::cerr << "[VXDeverbTests] RT60 not restored to default after reset: got="
                  << est.getEstimatedRt60() << "\n";
        return false;
    }

    // Fixed override
    est.setFixedRt60(1.2f);
    if (est.getEstimatedRt60() != 1.2f) {
        std::cerr << "[VXDeverbTests] setFixedRt60 not honoured\n";
        return false;
    }

    return true;
}

bool testWpeVoiceMode() {
    constexpr double sr   = 48000.0;
    constexpr int    fftBlockSize = 4096;
    constexpr float  synthRt60 = 0.6f;

    // Build a synthetic exponential-decay stereo signal
    const int totalSamples = static_cast<int>(sr * 3.0);
    std::mt19937 rng(99);
    std::normal_distribution<float> dist(0.0f, 1.0f);

    juce::AudioBuffer<float> input(2, totalSamples);
    for (int i = 0; i < totalSamples; ++i) {
        const float decay = std::exp(-6.908f * static_cast<float>(i)
                                     / (synthRt60 * static_cast<float>(sr)));
        const float s = dist(rng) * decay;
        input.setSample(0, i, s);
        input.setSample(1, i, s * 0.99f);
    }

    const auto computeTailPower = [&](const juce::AudioBuffer<float>& buf) {
        const int tailStart = static_cast<int>(sr * 0.5);
        double energy = 0.0;
        int count = 0;
        for (int ch = 0; ch < buf.getNumChannels(); ++ch) {
            const auto* d = buf.getReadPointer(ch);
            for (int i = tailStart; i < buf.getNumSamples(); ++i) {
                energy += static_cast<double>(d[i]) * d[i];
                ++count;
            }
        }
        return energy / std::max(1, count);
    };

    const auto runProcessor = [&](bool vm) -> double {
        VXDeverbAudioProcessor processor;
        processor.setDebugDeterministicReset(true);
        processor.setDebugRt60PresetSeconds(synthRt60);
        processor.setVoiceMode(vm);
        processor.prepareToPlay(sr, fftBlockSize);

        if (auto* p = processor.getValueTreeState().getParameter("reduce"))
            p->setValueNotifyingHost(0.82f);
        if (auto* p = processor.getValueTreeState().getParameter("body"))
            p->setValueNotifyingHost(0.0f);

        const int latency = std::max(0, processor.getLatencySamples());
        juce::AudioBuffer<float> staged(input.getNumChannels(), input.getNumSamples() + latency);
        staged.clear();
        for (int ch = 0; ch < input.getNumChannels(); ++ch)
            staged.copyFrom(ch, 0, input, ch, 0, input.getNumSamples());

        juce::AudioBuffer<float> rendered(input.getNumChannels(), staged.getNumSamples());
        juce::MidiBuffer midi;
        constexpr int bsz = 256;
        for (int start = 0; start < staged.getNumSamples(); start += bsz) {
            const int num = std::min(bsz, staged.getNumSamples() - start);
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
        return computeTailPower(output);
    };

    const double tailSpectral = runProcessor(false);
    const double tailWpe      = runProcessor(true);

    // WPE should not increase tail energy on synthetic noise
    if (tailWpe > tailSpectral * 1.20) { // 20% margin for measurement noise
        std::cerr << "[VXDeverbTests] WPE increased tail more than expected: "
                  << "spectral=" << tailSpectral << " wpe=" << tailWpe << "\n";
        return false;
    }

    // Voice mode must not change reported latency
    {
        VXDeverbAudioProcessor pNoVm, pVm;
        pNoVm.prepareToPlay(sr, 256);
        pVm.setVoiceMode(true);
        pVm.prepareToPlay(sr, 256);
        if (pNoVm.getLatencySamples() != pVm.getLatencySamples()) {
            std::cerr << "[VXDeverbTests] voice mode changed latency\n";
            return false;
        }
    }

    return true;
}

bool testResetAndSilenceRecovery() {
    constexpr double sr = 48000.0;
    auto dry = makeSpeechLike(sr, 0.5f);
    auto room = addRoomTail(dry, sr);
    juce::AudioBuffer<float> silence(2, static_cast<int>(sr * 0.3f));
    silence.clear();

    VXDeverbAudioProcessor processor;
    processor.setDebugDeterministicReset(true);
    processor.prepareToPlay(sr, room.getNumSamples());
    auto first = render(processor, room, 0.78f, 0.0f);
    render(processor, silence, 0.78f, 0.0f);
    processor.reset();
    auto second = render(processor, room, 0.78f, 0.0f);

    const float corr = speechBandCorrelation(first, second, sr);
    if (corr < 0.84f) {
        std::cerr << "[VXDeverbTests] reset/silence recovery diverged too much: corr=" << corr << "\n";
        return false;
    }
    return true;
}

} // namespace

int main() {
    bool ok = true;
    ok &= testReduceZeroIsIdentity();
    ok &= testFullyWetStaysCoherentWithDelayedDry();
    ok &= testReduceChangesAudioAndLowersTail();
    ok &= testBodyRestoreIsIntentional();
    ok &= testResetAndSilenceRecovery();
    ok &= testRt60Estimator();
    ok &= testWpeVoiceMode();
    return ok ? 0 : 1;
}
