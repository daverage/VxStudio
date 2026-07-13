// Regression tests for the VX Leveler ride logic (vocal rider + mix leveler).
// Drives vxsuite::leveler::Dsp directly with synthetic DetectorSnapshots so the
// tests target the ride behaviour, not the detector.

#include "../Source/vxstudio/products/leveler/dsp/VxLevelerDsp.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>
#include <cstdint>
#include <iostream>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;
constexpr int kNumChannels = 2;

int failures = 0;

void expect(const bool condition, const std::string& name, const std::string& detail) {
    if (condition) {
        std::cout << "PASS: " << name << (detail.empty() ? "" : " (" + detail + ")") << "\n";
    } else {
        std::cout << "FAIL: " << name << (detail.empty() ? "" : " (" + detail + ")") << "\n";
        ++failures;
    }
}

struct BurstSignal {
    juce::AudioBuffer<float> buffer;
    double burstSeconds = 0.8;
    double gapSeconds = 1.2;
};

// 600 Hz tone bursts (speech band) with a -70 dB noise floor between them.
BurstSignal makeBurstSignal(const double totalSeconds,
                            const float burstDb,
                            const double burstSeconds = 0.8,
                            const double gapSeconds = 1.2) {
    BurstSignal signal;
    signal.burstSeconds = burstSeconds;
    signal.gapSeconds = gapSeconds;
    const int numSamples = static_cast<int>(totalSeconds * kSampleRate);
    signal.buffer.setSize(kNumChannels, numSamples);
    const float amp = juce::Decibels::decibelsToGain(burstDb);
    const float floorAmp = juce::Decibels::decibelsToGain(-70.0f);
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> noise(-1.0f, 1.0f);
    const double cycle = burstSeconds + gapSeconds;
    for (int i = 0; i < numSamples; ++i) {
        const double t = static_cast<double>(i) / kSampleRate;
        const bool on = std::fmod(t, cycle) < burstSeconds;
        float s = on ? amp * static_cast<float>(std::sin(2.0 * juce::MathConstants<double>::pi * 600.0 * t)) : 0.0f;
        s += floorAmp * noise(rng);
        for (int ch = 0; ch < kNumChannels; ++ch)
            signal.buffer.setSample(ch, i, s);
    }
    return signal;
}

// Runs the DSP over the input in fixed blocks with a synthetic detector
// snapshot per block (phraseActivity high during bursts, zero in gaps).
juce::AudioBuffer<float> renderVoice(const juce::AudioBuffer<float>& input,
                                     vxsuite::leveler::Dsp& dsp,
                                     const double burstSeconds,
                                     const double cycleSeconds) {
    juce::AudioBuffer<float> output(input.getNumChannels(), input.getNumSamples());
    juce::AudioBuffer<float> block(input.getNumChannels(), kBlockSize);
    for (int start = 0; start < input.getNumSamples(); start += kBlockSize) {
        const int len = std::min(kBlockSize, input.getNumSamples() - start);
        block.setSize(input.getNumChannels(), len, false, false, true);
        for (int ch = 0; ch < input.getNumChannels(); ++ch)
            block.copyFrom(ch, 0, input, ch, start, len);

        const double tMid = (static_cast<double>(start) + 0.5 * len) / kSampleRate;
        const double phase = std::fmod(tMid, cycleSeconds);
        const bool on = phase < burstSeconds;
        vxsuite::leveler::DetectorSnapshot snapshot {};
        snapshot.phraseActivity = on ? 1.0f : 0.0f;
        snapshot.speechPresence = on ? 0.7f : 0.0f;
        dsp.process(block, snapshot);

        for (int ch = 0; ch < input.getNumChannels(); ++ch)
            output.copyFrom(ch, start, block, ch, 0, len);
    }
    return output;
}

float rmsDb(const juce::AudioBuffer<float>& buffer, int start, int end) {
    start = juce::jlimit(0, buffer.getNumSamples(), start);
    end = juce::jlimit(0, buffer.getNumSamples(), end);
    if (end <= start)
        return -120.0f;
    double energy = 0.0;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
        const auto* data = buffer.getReadPointer(ch);
        for (int i = start; i < end; ++i)
            energy += static_cast<double>(data[i]) * data[i];
    }
    energy /= static_cast<double>((end - start) * buffer.getNumChannels());
    return static_cast<float>(10.0 * std::log10(std::max(energy, 1.0e-12)));
}

int atSeconds(const double t) {
    return static_cast<int>(t * kSampleRate);
}

// Task 2 acceptance: the fader must freeze during pauses instead of pumping
// the noise floor up, and gain must be continuous from burst to burst.
void gateFreezesInSilence() {
    const double totalSeconds = 14.0;
    auto signal = makeBurstSignal(totalSeconds, -18.0f);

    vxsuite::leveler::Dsp dsp;
    dsp.prepare(kSampleRate, kBlockSize, kNumChannels);
    vxsuite::leveler::Dsp::Params params {};
    params.level = 0.8f;
    params.control = 0.3f;
    params.voiceMode = true;
    dsp.setParams(params);

    const auto output = renderVoice(signal.buffer, dsp, signal.burstSeconds,
                                    signal.burstSeconds + signal.gapSeconds);
    const int delay = dsp.latencySamples();
    const double cycle = signal.burstSeconds + signal.gapSeconds;

    float worstRise = -120.0f;
    float worstContinuity = 0.0f;
    float prevEndGain = 0.0f;
    bool havePrev = false;
    for (int k = 1; k < 6; ++k) {
        const double burstStart = k * cycle;
        // Last 0.6 s of the gap preceding no burst: gap runs [burstStart+0.8, burstStart+2.0).
        const int gapA = atSeconds(burstStart + signal.burstSeconds + 0.6);
        const int gapB = atSeconds(burstStart + cycle) - 64;
        const float inFloor = rmsDb(signal.buffer, gapA, gapB);
        const float outFloor = rmsDb(output, gapA + delay, gapB + delay);
        worstRise = std::max(worstRise, outFloor - inFloor);

        const int startA = atSeconds(burstStart + 0.05);
        const int startB = atSeconds(burstStart + 0.20);
        const int endA = atSeconds(burstStart + signal.burstSeconds - 0.20);
        const int endB = atSeconds(burstStart + signal.burstSeconds - 0.05);
        const float startGain = rmsDb(output, startA + delay, startB + delay) - rmsDb(signal.buffer, startA, startB);
        const float endGain = rmsDb(output, endA + delay, endB + delay) - rmsDb(signal.buffer, endA, endB);
        if (havePrev)
            worstContinuity = std::max(worstContinuity, std::abs(startGain - prevEndGain));
        prevEndGain = endGain;
        havePrev = true;
    }

    expect(worstRise < 1.5f, "gateFreezesInSilence.floorRise",
           "worst gap floor rise " + std::to_string(worstRise) + " dB (limit 1.5)");
    expect(worstContinuity < 1.5f, "gateFreezesInSilence.gainContinuity",
           "worst burst-to-burst gain jump " + std::to_string(worstContinuity) + " dB (limit 1.5)");
}

// Task 3 acceptance: verse/chorus imbalance correction must persist instead
// of collapsing once the anchor normalises to the louder section.
void sectionLevellingPersists() {
    const double sectionSeconds = 20.0;
    const double burstSeconds = 0.8;
    const double gapSeconds = 1.2;
    const double cycle = burstSeconds + gapSeconds;
    const float verseDb = -26.0f;
    const float chorusDb = -16.0f;

    auto verse = makeBurstSignal(sectionSeconds, verseDb, burstSeconds, gapSeconds);
    auto chorus = makeBurstSignal(sectionSeconds, chorusDb, burstSeconds, gapSeconds);
    juce::AudioBuffer<float> input(kNumChannels, verse.buffer.getNumSamples() + chorus.buffer.getNumSamples());
    for (int ch = 0; ch < kNumChannels; ++ch) {
        input.copyFrom(ch, 0, verse.buffer, ch, 0, verse.buffer.getNumSamples());
        input.copyFrom(ch, verse.buffer.getNumSamples(), chorus.buffer, ch, 0, chorus.buffer.getNumSamples());
    }

    vxsuite::leveler::Dsp dsp;
    dsp.prepare(kSampleRate, kBlockSize, kNumChannels);
    vxsuite::leveler::Dsp::Params params {};
    params.level = 0.8f;
    params.control = 0.3f;
    params.voiceMode = true;
    dsp.setParams(params);

    const auto output = renderVoice(input, dsp, burstSeconds, cycle);
    const int delay = dsp.latencySamples();

    // Average burst level over each section's second half (bursts only,
    // trimmed to the settled core of each burst).
    const auto sectionBurstDb = [&](const double sectionStart) {
        double sum = 0.0;
        int count = 0;
        for (int k = 0; k < static_cast<int>(sectionSeconds / cycle); ++k) {
            const double burstStart = sectionStart + k * cycle;
            if (burstStart - sectionStart < sectionSeconds * 0.5)
                continue;
            sum += rmsDb(output,
                         atSeconds(burstStart + 0.15) + delay,
                         atSeconds(burstStart + burstSeconds - 0.10) + delay);
            ++count;
        }
        return static_cast<float>(sum / std::max(1, count));
    };

    const float outVerseDb = sectionBurstDb(0.0);
    const float outChorusDb = sectionBurstDb(sectionSeconds);
    const float inDiff = chorusDb - verseDb;   // 10 dB
    const float outDiff = outChorusDb - outVerseDb;
    expect(outDiff <= 0.55f * inDiff, "sectionLevellingPersists",
           "output section diff " + std::to_string(outDiff) + " dB vs input " + std::to_string(inDiff)
               + " dB (must be <= " + std::to_string(0.55f * inDiff) + ")");
}

// level=0, control=0 must be a pure 10 ms delay in both modes.
void neutralIsTransparent() {
    auto signal = makeBurstSignal(6.0, -18.0f);
    for (const bool voice : { true, false }) {
        vxsuite::leveler::Dsp dsp;
        dsp.prepare(kSampleRate, kBlockSize, kNumChannels);
        vxsuite::leveler::Dsp::Params params {};
        params.level = 0.0f;
        params.control = 0.0f;
        params.voiceMode = voice;
        dsp.setParams(params);
        const auto output = renderVoice(signal.buffer, dsp, signal.burstSeconds,
                                        signal.burstSeconds + signal.gapSeconds);
        const int delay = dsp.latencySamples();
        float maxErr = 0.0f;
        for (int ch = 0; ch < kNumChannels; ++ch)
            for (int i = delay; i < signal.buffer.getNumSamples(); ++i)
                maxErr = std::max(maxErr,
                                  std::abs(output.getSample(ch, i) - signal.buffer.getSample(ch, i - delay)));
        expect(maxErr < 1.0e-6f,
               std::string("neutralIsTransparent.") + (voice ? "voice" : "general"),
               "max error " + std::to_string(maxErr));
    }
}

} // namespace

int main() {
    gateFreezesInSilence();
    sectionLevellingPersists();
    neutralIsTransparent();

    if (failures == 0) {
        std::cout << "All leveler rider tests passed\n";
        return 0;
    }
    std::cout << failures << " leveler rider test(s) failed\n";
    return 1;
}
