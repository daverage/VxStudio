// Regression tests for the VX Leveler ride logic (vocal rider + mix leveler).
// Drives vxsuite::leveler::Dsp directly with synthetic DetectorSnapshots so the
// tests target the ride behaviour, not the detector.

#include "../Source/vxstudio/products/leveler/VxLevelerProcessor.h"
#include "../Source/vxstudio/products/leveler/dsp/VxLevelerDsp.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_events/juce_events.h>

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
                            const double gapSeconds = 1.2,
                            const float floorDb = -70.0f) {
    BurstSignal signal;
    signal.burstSeconds = burstSeconds;
    signal.gapSeconds = gapSeconds;
    const int numSamples = static_cast<int>(totalSeconds * kSampleRate);
    signal.buffer.setSize(kNumChannels, numSamples);
    const float amp = juce::Decibels::decibelsToGain(burstDb);
    const float floorAmp = juce::Decibels::decibelsToGain(floorDb);
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
    expect(dsp.getRideGainDb() < -1.0f && dsp.getRideGainDb() > -12.0f,
           "rideGainReported", "ride gain after chorus " + std::to_string(dsp.getRideGainDb()) + " dB");
    const float inDiff = chorusDb - verseDb;   // 10 dB
    const float outDiff = outChorusDb - outVerseDb;
    expect(outDiff <= 0.55f * inDiff, "sectionLevellingPersists",
           "output section diff " + std::to_string(outDiff) + " dB vs input " + std::to_string(inDiff)
               + " dB (must be <= " + std::to_string(0.55f * inDiff) + ")");
}

// Fills a block with a steady tone loud enough to prime the general engine.
void fillTone(juce::AudioBuffer<float>& block, const std::int64_t startSample) {
    for (int i = 0; i < block.getNumSamples(); ++i) {
        const double t = static_cast<double>(startSample + i) / kSampleRate;
        const float s = 0.2f * static_cast<float>(std::sin(2.0 * juce::MathConstants<double>::pi * 220.0 * t));
        for (int ch = 0; ch < block.getNumChannels(); ++ch)
            block.setSample(ch, i, s);
    }
}

vxsuite::leveler::OfflineAnalysisResult makeSyntheticAnalysis(const std::int64_t startSample,
                                                              const int numBlocks) {
    vxsuite::leveler::OfflineAnalysisResult analysis {};
    analysis.sampleRate = kSampleRate;
    analysis.blockSize = kBlockSize;
    analysis.globalMedianDb = -24.0f;
    analysis.globalUpperDb = -18.0f;
    analysis.globalDynamicRangeDb = 6.0f;
    analysis.startSample = startSample;
    analysis.targetCurveDb.resize(static_cast<size_t>(numBlocks));
    for (int i = 0; i < numBlocks; ++i)
        analysis.targetCurveDb[static_cast<size_t>(i)] = -30.0f + 0.01f * static_cast<float>(i);
    return analysis;
}

// Task 4 acceptance: with a playhead position, the offline curve index must
// follow the timeline (seek/loop safe); outside the analysed range the fixed
// global-stats fallback engages.
void offlineCurveFollowsTimeline() {
    vxsuite::leveler::Dsp dsp;
    dsp.prepare(kSampleRate, kBlockSize, kNumChannels);
    vxsuite::leveler::Dsp::Params params {};
    params.level = 0.5f;
    params.control = 0.3f;
    params.voiceMode = false;
    params.analysisMode = vxsuite::leveler::Dsp::MixAnalysisMode::offline;
    dsp.setParams(params);

    const std::int64_t startSample = 48000;
    dsp.setOfflineAnalysis(makeSyntheticAnalysis(startSample, 200));

    juce::AudioBuffer<float> block(kNumChannels, kBlockSize);
    const vxsuite::leveler::DetectorSnapshot snapshot {};

    bool indexFollows = true;
    // Non-contiguous k values simulate seeks and loops.
    for (const int k : { 0, 5, 6, 120, 40, 41, 7, 199 }) {
        const std::int64_t timeline = startSample + static_cast<std::int64_t>(k) * kBlockSize;
        dsp.setTimelineSample(timeline);
        fillTone(block, timeline);
        dsp.process(block, snapshot);
        const auto debug = dsp.getDebugSnapshot();
        if (debug.offlineBlockIndex != k || debug.offlineWaiting)
            indexFollows = false;
    }
    expect(indexFollows, "offlineCurveFollowsTimeline", "curve index tracks timeline across seeks");
}

void offlineFallbackOutOfRange() {
    vxsuite::leveler::Dsp dsp;
    dsp.prepare(kSampleRate, kBlockSize, kNumChannels);
    vxsuite::leveler::Dsp::Params params {};
    params.level = 0.5f;
    params.control = 0.3f;
    params.voiceMode = false;
    params.analysisMode = vxsuite::leveler::Dsp::MixAnalysisMode::offline;
    dsp.setParams(params);

    const std::int64_t startSample = 48000;
    dsp.setOfflineAnalysis(makeSyntheticAnalysis(startSample, 100));

    juce::AudioBuffer<float> block(kNumChannels, kBlockSize);
    const vxsuite::leveler::DetectorSnapshot snapshot {};

    // Before the analysed region.
    dsp.setTimelineSample(0);
    fillTone(block, 0);
    dsp.process(block, snapshot);
    const auto before = dsp.getDebugSnapshot();
    expect(before.offlineWaiting && before.offlineBlockIndex < 0,
           "offlineFallbackOutOfRange.before", "global-stats fallback before map start");

    // After the analysed region.
    const std::int64_t past = startSample + static_cast<std::int64_t>(150) * kBlockSize;
    dsp.setTimelineSample(past);
    fillTone(block, past);
    dsp.process(block, snapshot);
    const auto after = dsp.getDebugSnapshot();
    expect(after.offlineWaiting && after.offlineBlockIndex < 0,
           "offlineFallbackOutOfRange.after", "global-stats fallback past map end");

    // No playhead / old chunk (startSample = -1): free-running counter engages.
    dsp.setOfflineAnalysis(makeSyntheticAnalysis(-1, 100));
    dsp.reset();
    dsp.setTimelineSample(-1);
    bool freeRunning = true;
    for (int k = 0; k < 4; ++k) {
        fillTone(block, static_cast<std::int64_t>(k) * kBlockSize);
        dsp.process(block, snapshot);
        if (dsp.getDebugSnapshot().offlineBlockIndex != k || dsp.getDebugSnapshot().offlineWaiting)
            freeRunning = false;
    }
    expect(freeRunning, "offlineFallbackOutOfRange.freeRunning",
           "free-running index without playhead/startSample");
}

// State round-trip: startSample must survive save/load; loading a chunk
// without the attribute (older version) must yield -1.
void offlineStateRoundTrip() {
    VXLevelerAudioProcessor saver;
    saver.prepareToPlay(kSampleRate, kBlockSize);
    auto analysis = makeSyntheticAnalysis(96000, 50);
    saver.setOfflineAnalysis(analysis);

    juce::MemoryBlock state;
    saver.getStateInformation(state);

    VXLevelerAudioProcessor loader;
    loader.prepareToPlay(kSampleRate, kBlockSize);
    loader.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    const auto& loaded = loader.getOfflineAnalysis();
    expect(loaded.isValid() && loaded.startSample == 96000,
           "offlineStateRoundTrip.survives",
           "startSample " + std::to_string(loaded.startSample));

    // Old chunks carry no startSample attribute; the loader must default to -1.
    VXLevelerAudioProcessor oldSaver;
    oldSaver.prepareToPlay(kSampleRate, kBlockSize);
    auto oldAnalysis = makeSyntheticAnalysis(-1, 50);
    oldSaver.setOfflineAnalysis(oldAnalysis);
    juce::MemoryBlock oldState;
    oldSaver.getStateInformation(oldState);

    VXLevelerAudioProcessor oldLoader;
    oldLoader.prepareToPlay(kSampleRate, kBlockSize);
    oldLoader.setStateInformation(oldState.getData(), static_cast<int>(oldState.getSize()));
    expect(oldLoader.getOfflineAnalysis().isValid()
               && oldLoader.getOfflineAnalysis().startSample == -1,
           "offlineStateRoundTrip.oldChunkDefaults",
           "startSample " + std::to_string(oldLoader.getOfflineAnalysis().startSample));
}

// New user controls: Target offsets the ride reference; Gate shifts how
// eagerly the fader freezes in pauses. Defaults (0 dB) are covered by every
// other test in this file.
float lateBurstLevelDb(const juce::AudioBuffer<float>& output, const int delay,
                       const double cycle, const double burstSeconds) {
    double sum = 0.0;
    int count = 0;
    for (int k = 4; k < 6; ++k) {
        sum += rmsDb(output,
                     atSeconds(k * cycle + 0.3) + delay,
                     atSeconds(k * cycle + burstSeconds - 0.1) + delay);
        ++count;
    }
    return static_cast<float>(sum / count);
}

void targetOffsetShiftsRide() {
    auto signal = makeBurstSignal(14.0, -18.0f);
    const double cycle = signal.burstSeconds + signal.gapSeconds;

    const auto runWithOffset = [&](const float offsetDb) {
        vxsuite::leveler::Dsp dsp;
        dsp.prepare(kSampleRate, kBlockSize, kNumChannels);
        vxsuite::leveler::Dsp::Params params {};
        params.level = 0.8f;
        params.control = 0.3f;
        params.voiceMode = true;
        params.targetOffsetDb = offsetDb;
        dsp.setParams(params);
        const auto output = renderVoice(signal.buffer, dsp, signal.burstSeconds, cycle);
        return lateBurstLevelDb(output, dsp.latencySamples(), cycle, signal.burstSeconds);
    };

    const float baseDb = runWithOffset(0.0f);
    const float hotDb = runWithOffset(4.0f);
    const float quietDb = runWithOffset(-4.0f);
    expect(std::abs((hotDb - baseDb) - 4.0f) < 1.5f, "targetOffset.up",
           "+4 dB target moved output by " + std::to_string(hotDb - baseDb) + " dB");
    expect(std::abs((quietDb - baseDb) + 4.0f) < 1.5f, "targetOffset.down",
           "-4 dB target moved output by " + std::to_string(quietDb - baseDb) + " dB");
}

void gateSenseShiftsFreeze() {
    // A realistic pause floor (16 dB below the bursts, e.g. heavy room tone)
    // sits inside the trimmed gate band, so the knob decides whether the
    // rider freezes there or keeps riding it.
    auto signal = makeBurstSignal(14.0, -18.0f, 0.8, 1.2, -34.0f);
    const double cycle = signal.burstSeconds + signal.gapSeconds;

    // Worst gap floor rise for a given gate-sensitivity trim.
    const auto gapRiseWith = [&](const float gateSenseDb) {
        vxsuite::leveler::Dsp dsp;
        dsp.prepare(kSampleRate, kBlockSize, kNumChannels);
        vxsuite::leveler::Dsp::Params params {};
        params.level = 0.8f;
        params.control = 0.3f;
        params.voiceMode = true;
        params.gateSenseDb = gateSenseDb;
        dsp.setParams(params);
        const auto output = renderVoice(signal.buffer, dsp, signal.burstSeconds, cycle);
        const int delay = dsp.latencySamples();
        float worst = -120.0f;
        for (int k = 1; k < 6; ++k) {
            const int gapA = atSeconds(k * cycle + signal.burstSeconds + 0.6);
            const int gapB = atSeconds((k + 1) * cycle) - 64;
            worst = std::max(worst, rmsDb(output, gapA + delay, gapB + delay)
                                        - rmsDb(signal.buffer, gapA, gapB));
        }
        return worst;
    };

    const float tightRise = gapRiseWith(-8.0f);
    const float openRise = gapRiseWith(8.0f);
    expect(tightRise < 1.5f, "gateSense.tightStillFreezes",
           "gap rise at -8 dB trim " + std::to_string(tightRise) + " dB");
    expect(openRise > tightRise + 0.5f, "gateSense.openRidesDeeper",
           "gap rise " + std::to_string(tightRise) + " dB (tight) vs "
               + std::to_string(openRise) + " dB (open)");
}

// Renders continuous (non-bursty) material through general mode.
juce::AudioBuffer<float> renderGeneral(const juce::AudioBuffer<float>& input,
                                       vxsuite::leveler::Dsp& dsp) {
    juce::AudioBuffer<float> output(input.getNumChannels(), input.getNumSamples());
    juce::AudioBuffer<float> block(input.getNumChannels(), kBlockSize);
    const vxsuite::leveler::DetectorSnapshot snapshot {};
    for (int start = 0; start < input.getNumSamples(); start += kBlockSize) {
        const int len = std::min(kBlockSize, input.getNumSamples() - start);
        block.setSize(input.getNumChannels(), len, false, false, true);
        for (int ch = 0; ch < input.getNumChannels(); ++ch)
            block.copyFrom(ch, 0, input, ch, start, len);
        dsp.process(block, snapshot);
        for (int ch = 0; ch < input.getNumChannels(); ++ch)
            output.copyFrom(ch, start, block, ch, 0, len);
    }
    return output;
}

// Two-level continuous tone: quiet section then loud section.
juce::AudioBuffer<float> makeStepTone(const double secondsPerSection,
                                      const float quietDb, const float loudDb) {
    const int sectionSamples = static_cast<int>(secondsPerSection * kSampleRate);
    juce::AudioBuffer<float> buffer(kNumChannels, 2 * sectionSamples);
    for (int i = 0; i < buffer.getNumSamples(); ++i) {
        const float amp = juce::Decibels::decibelsToGain(i < sectionSamples ? quietDb : loudDb);
        const float sample = amp * static_cast<float>(
            std::sin(2.0 * juce::MathConstants<double>::pi * 220.0 * (i / kSampleRate)));
        for (int ch = 0; ch < kNumChannels; ++ch)
            buffer.setSample(ch, i, sample);
    }
    return buffer;
}

vxsuite::leveler::Dsp::Params generalParams(const vxsuite::leveler::Dsp::MixAnalysisMode mode) {
    vxsuite::leveler::Dsp::Params params {};
    params.level = 0.8f;
    params.control = 0.3f;
    params.voiceMode = false;
    params.analysisMode = mode;
    return params;
}

// Regression for the Realtime-mode dead ride: makeMixTargetFrame returned
// confidence 0 in Realtime mode, which multiplied the ride target to zero.
void realtimeModeActuallyLevels() {
    const auto input = makeStepTone(10.0, -30.0f, -18.0f);
    vxsuite::leveler::Dsp dsp;
    dsp.prepare(kSampleRate, kBlockSize, kNumChannels);
    dsp.setParams(generalParams(vxsuite::leveler::Dsp::MixAnalysisMode::realtime));
    const auto output = renderGeneral(input, dsp);
    const int delay = dsp.latencySamples();

    // Loud section, shortly after the step (before the local target has fully
    // re-normalised): the ride must be pulling the level down.
    const float inDb = rmsDb(input, atSeconds(12.0), atSeconds(14.0));
    const float outDb = rmsDb(output, atSeconds(12.0) + delay, atSeconds(14.0) + delay);
    expect(outDb < inDb - 1.0f, "realtimeModeActuallyLevels",
           "ride pulled the loud section by " + std::to_string(outDb - inDb) + " dB (needs < -1)");
}

// Target knob in mix mode: offsets the ride target.
void mixTargetOffsetShifts() {
    const auto runWithOffset = [](const float offsetDb) {
        const auto input = makeStepTone(8.0, -24.0f, -24.0f);   // steady tone
        vxsuite::leveler::Dsp dsp;
        dsp.prepare(kSampleRate, kBlockSize, kNumChannels);
        auto params = generalParams(vxsuite::leveler::Dsp::MixAnalysisMode::realtime);
        params.targetOffsetDb = offsetDb;
        dsp.setParams(params);
        const auto output = renderGeneral(input, dsp);
        return rmsDb(output, atSeconds(12.0), atSeconds(15.5));
    };
    const float hotDb = runWithOffset(4.0f);
    const float quietDb = runWithOffset(-4.0f);
    expect(hotDb > quietDb + 1.5f, "mixTargetOffsetShifts",
           "+4 vs -4 dB target: " + std::to_string(hotDb - quietDb) + " dB apart (needs > 1.5)");
}

// Depth knob in mix mode: sensitivity trim on the ride deadband.
void mixDepthChangesSensitivity() {
    const auto activityWith = [](const float senseDb) {
        // Gentle wobble around the baseline - inside the calm deadband,
        // outside the sensitive one.
        const int n = static_cast<int>(16.0 * kSampleRate);
        juce::AudioBuffer<float> input(kNumChannels, n);
        for (int i = 0; i < n; ++i) {
            const double t = i / kSampleRate;
            const float levelDb = -24.0f + 1.6f * static_cast<float>(std::sin(2.0 * juce::MathConstants<double>::pi * t / 7.0));
            const float sample = juce::Decibels::decibelsToGain(levelDb)
                * static_cast<float>(std::sin(2.0 * juce::MathConstants<double>::pi * 220.0 * t));
            for (int ch = 0; ch < kNumChannels; ++ch)
                input.setSample(ch, i, sample);
        }
        vxsuite::leveler::Dsp dsp;
        dsp.prepare(kSampleRate, kBlockSize, kNumChannels);
        auto params = generalParams(vxsuite::leveler::Dsp::MixAnalysisMode::realtime);
        params.gateSenseDb = senseDb;
        dsp.setParams(params);
        renderGeneral(input, dsp);
        return dsp.getLevelActivity();
    };
    const float calm = activityWith(-8.0f);
    const float sensitive = activityWith(8.0f);
    expect(sensitive > calm, "mixDepthChangesSensitivity",
           "level activity calm " + std::to_string(calm) + " vs sensitive " + std::to_string(sensitive));
}

// Vocal offline: the analyzed take's active median becomes the ride
// reference, so quiet playback rides up toward it.
void vocalOfflineReferencePins() {
    auto signal = makeBurstSignal(14.0, -30.0f);
    const double cycle = signal.burstSeconds + signal.gapSeconds;

    const auto lateLevelWith = [&](const bool withAnalysis) {
        vxsuite::leveler::Dsp dsp;
        dsp.prepare(kSampleRate, kBlockSize, kNumChannels);
        vxsuite::leveler::Dsp::Params params {};
        params.level = 0.8f;
        params.control = 0.3f;
        params.voiceMode = true;
        params.analysisMode = withAnalysis
            ? vxsuite::leveler::Dsp::MixAnalysisMode::offline
            : vxsuite::leveler::Dsp::MixAnalysisMode::smartRealtime;
        dsp.setParams(params);
        if (withAnalysis) {
            auto analysis = makeSyntheticAnalysis(-1, 100);
            analysis.activeMedianDb = -22.0f;   // take is meant to sit ~8 dB hotter
            dsp.setOfflineAnalysis(analysis);
        }
        const auto output = renderVoice(signal.buffer, dsp, signal.burstSeconds, cycle);
        return lateBurstLevelDb(output, dsp.latencySamples(), cycle, signal.burstSeconds);
    };

    const float selfLearned = lateLevelWith(false);
    const float pinned = lateLevelWith(true);
    expect(pinned > selfLearned + 2.0f, "vocalOfflineReferencePins",
           "self-learned " + std::to_string(selfLearned) + " dB vs analyzed-ref "
               + std::to_string(pinned) + " dB");
}

// Analyzer: active median must ignore the silent gaps a sparse take is full of.
void analyzerActiveMedian() {
    std::vector<float> blockDb;
    for (int i = 0; i < 200; ++i)
        blockDb.push_back(i % 3 == 0 ? -20.0f : -100.0f);   // sparse take: 1/3 signal, 2/3 gaps
    const auto result = vxsuite::leveler::OfflineAnalyzer::analyse(blockDb.data(), blockDb.size(),
                                                                   kSampleRate, kBlockSize);
    expect(std::abs(result.activeMedianDb + 20.0f) < 1.0f && result.globalMedianDb < -60.0f,
           "analyzerActiveMedian",
           "active median " + std::to_string(result.activeMedianDb) + " dB while plain median sits in the gaps ("
               + std::to_string(result.globalMedianDb) + " dB)");
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
        expect(dsp.getRideGainDb() == 0.0f,
               std::string("neutralRideGainZero.") + (voice ? "voice" : "general"),
               std::to_string(dsp.getRideGainDb()));
    }
}

} // namespace

int main() {
    const juce::ScopedJuceInitialiser_GUI juceInit;
    gateFreezesInSilence();
    sectionLevellingPersists();
    offlineCurveFollowsTimeline();
    offlineFallbackOutOfRange();
    offlineStateRoundTrip();
    targetOffsetShiftsRide();
    gateSenseShiftsFreeze();
    realtimeModeActuallyLevels();
    mixTargetOffsetShifts();
    mixDepthChangesSensitivity();
    vocalOfflineReferencePins();
    analyzerActiveMedian();
    neutralIsTransparent();

    if (failures == 0) {
        std::cout << "All leveler rider tests passed\n";
        return 0;
    }
    std::cout << failures << " leveler rider test(s) failed\n";
    return 1;
}
