// VX Tune plugin-level tests: Listen delta contract, latency reporting, and
// the pitch-trace feed. Listen must play only the changes being made:
// non-silent while correction is active, silent for an in-tune singer.

#include "../Source/vxstudio/products/tune/VxTuneProcessor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(const bool condition, const std::string& label) {
    std::printf("  %s  %s\n", condition ? "PASS" : "FAIL", label.c_str());
    if (!condition)
        ++failures;
}

constexpr double kSampleRate = 48000.0;
constexpr int kBlock = 512;
constexpr float kTwoPi = 6.28318530717958647692f;

void setParam(VXTuneAudioProcessor& p, const char* id, const float normalised) {
    if (auto* param = p.getValueTreeState().getParameter(id))
        param->setValueNotifyingHost(normalised);
}

// Feeds a tone for `seconds` and returns the output RMS over the final
// `measureSeconds` of that span.
float runToneAndMeasure(VXTuneAudioProcessor& p, const float hz,
                        const float seconds, const float measureSeconds,
                        double& phase) {
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> block(2, kBlock);
    const int totalBlocks = static_cast<int>(seconds * kSampleRate / kBlock);
    const int measureFromBlock = totalBlocks
        - static_cast<int>(measureSeconds * kSampleRate / kBlock);
    double sumSquares = 0.0;
    long counted = 0;
    for (int b = 0; b < totalBlocks; ++b) {
        for (int i = 0; i < kBlock; ++i) {
            phase += kTwoPi * hz / kSampleRate;
            float s = 0.0f;
            for (int h = 1; h <= 3; ++h)
                s += std::sin(static_cast<float>(phase) * h) / static_cast<float>(h);
            block.setSample(0, i, 0.25f * s);
            block.setSample(1, i, 0.25f * s);
        }
        p.processBlock(block, midi);
        if (b >= measureFromBlock) {
            for (int i = 0; i < kBlock; ++i) {
                const float v = block.getSample(0, i);
                sumSquares += v * v;
                ++counted;
            }
        }
    }
    return counted > 0 ? static_cast<float>(std::sqrt(sumSquares / counted)) : 0.0f;
}

void testLatencyReported() {
    std::printf("Latency reporting:\n");
    VXTuneAudioProcessor p;
    p.prepareToPlay(kSampleRate, kBlock);
    check(p.getLatencySamples() > 0 && p.getLatencySamples() < static_cast<int>(0.25 * kSampleRate),
          "reports renderer latency (got " + std::to_string(p.getLatencySamples()) + ")");
}

void testListenPlaysChangesOnly() {
    std::printf("Listen plays the changes being made:\n");
    const float sharpHz = 220.0f * std::exp2(30.0f / 1200.0f);

    // Sharp singer, correction active: listen delta must be clearly audible.
    {
        VXTuneAudioProcessor p;
        p.prepareToPlay(kSampleRate, kBlock);
        setParam(p, "amount", 1.0f);
        setParam(p, "natural", 0.5f);
        double phase = 0.0;
        const float wetRms = runToneAndMeasure(p, sharpHz, 2.0f, 0.5f, phase);
        setParam(p, "listen", 1.0f);
        const float listenRms = runToneAndMeasure(p, sharpHz, 1.0f, 0.5f, phase);
        check(wetRms > 0.05f, "wet path carries signal (rms "
                                  + std::to_string(wetRms) + ")");
        check(listenRms > 0.02f,
              "listen is non-silent while correcting (rms "
                  + std::to_string(listenRms) + ")");
    }

    // In-tune singer, no correction: listen delta must be near silence.
    {
        VXTuneAudioProcessor p;
        p.prepareToPlay(kSampleRate, kBlock);
        setParam(p, "amount", 1.0f);
        setParam(p, "natural", 0.5f);
        double phase = 0.0;
        const float wetRms = runToneAndMeasure(p, 220.0f, 2.0f, 0.5f, phase);
        setParam(p, "listen", 1.0f);
        const float listenRms = runToneAndMeasure(p, 220.0f, 1.0f, 0.5f, phase);
        const float relDb = 20.0f * std::log10(std::max(listenRms, 1.0e-9f)
                                               / std::max(wetRms, 1.0e-9f));
        check(relDb < -40.0f,
              "listen is near-silent when not intervening ("
                  + std::to_string(relDb) + " dB rel wet)");
    }
}

// Feeds `melodyHz`, cycling one note per `noteSeconds`, for a total of
// `totalSeconds`. After every note, records the auto-key state so callers
// can check convergence speed and (in)stability over the run.
std::vector<VXTuneAudioProcessor::AutoKeyStateForTests> runMelodyAndTraceAutoKey(
        VXTuneAudioProcessor& p, const std::vector<float>& melodyHz,
        const float noteSeconds, const float totalSeconds) {
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> block(2, kBlock);
    const int blocksPerNote = std::max(1, static_cast<int>(noteSeconds * kSampleRate / kBlock));
    const int totalBlocks = static_cast<int>(totalSeconds * kSampleRate / kBlock);
    std::vector<VXTuneAudioProcessor::AutoKeyStateForTests> trace;
    double phase = 0.0;
    for (int b = 0; b < totalBlocks; ++b) {
        const size_t noteIndex = static_cast<size_t>(b / blocksPerNote) % melodyHz.size();
        const float hz = melodyHz[noteIndex];
        for (int i = 0; i < kBlock; ++i) {
            phase += kTwoPi * hz / kSampleRate;
            float s = 0.0f;
            for (int h = 1; h <= 3; ++h)
                s += std::sin(static_cast<float>(phase) * h) / static_cast<float>(h);
            block.setSample(0, i, 0.25f * s);
            block.setSample(1, i, 0.25f * s);
        }
        p.processBlock(block, midi);
        if (b % blocksPerNote == blocksPerNote - 1)
            trace.push_back(p.autoKeyStateForTests());
    }
    return trace;
}

int countKeyTransitions(const std::vector<VXTuneAudioProcessor::AutoKeyStateForTests>& trace) {
    int transitions = 0;
    for (size_t i = 1; i < trace.size(); ++i)
        if (trace[i].root != trace[i - 1].root || trace[i].scale != trace[i - 1].scale)
            ++transitions;
    return transitions;
}

// Regression for two bugs found on real material: the confidence metric
// used to compare major/minor "rivals" that always shared the same 7 notes
// (near-zero margin by construction), so it almost never cleared the
// commit threshold; and the major/minor tie-break had no hysteresis, so it
// flickered on noise even once the right note collection was found (see a
// ~4 minute vocal take that correctly found the G/Em note collection 86% of
// the time but whose major/minor label flipped 24 times).
void testAutoKeyLocksOntoScaleAndHolds() {
    std::printf("Auto-key: converges and holds on an unambiguous melody:\n");
    // All 7 scale degrees of C major (C D E F G A B), tonic/dominant
    // emphasised. Touching every degree is required to uniquely identify
    // the note collection - a melody using only e.g. C/E/G/A leaves the
    // scoring genuinely tied between C major, F major and G major (all
    // three contain that subset), which is correct behaviour, not a bug,
    // but not what this test is checking.
    const std::vector<float> cMajorMelody = {
        261.63f, 293.66f, 329.63f, 349.23f, 392.00f,
        261.63f, 440.00f, 392.00f, 493.88f, 261.63f,
    };
    VXTuneAudioProcessor p;
    p.prepareToPlay(kSampleRate, kBlock);
    const auto trace = runMelodyAndTraceAutoKey(p, cMajorMelody, 0.35f, 20.0f);

    const auto convergedAt = std::find_if(trace.begin(), trace.end(),
        [](const VXTuneAudioProcessor::AutoKeyStateForTests& s) {
            return s.confidence >= 0.15f && s.root == 0 && s.scale == 2;
        });
    check(convergedAt != trace.end(), "commits to C major within the run");

    if (convergedAt != trace.end()) {
        const int transitionsAfterLock = countKeyTransitions(
            { convergedAt, trace.end() });
        check(transitionsAfterLock == 0,
              "holds C major with no further flip-flopping (got "
                  + std::to_string(transitionsAfterLock) + " transition(s))");
    }
}

// Measures how long a genuine, permanent key change takes to be recognised:
// lock onto C major, then modulate to F# major (a tritone away - the
// maximally distant key, sharing zero pitch classes) and time how much new
// material it takes before the display updates. This is governed by the
// histogram's 45s half-life plus the hysteresis margin, not any fixed
// "refresh rate" - the estimate recomputes every audio frame, but a stale
// key only flips once fresh evidence clearly outweighs the old key's
// decaying-but-still-present histogram mass.
void testAutoKeyAdaptsToModulationWithinBoundedLag() {
    std::printf("Auto-key: adapts to a real modulation within a bounded lag:\n");
    const std::vector<float> cMajorMelody = {
        261.63f, 293.66f, 329.63f, 349.23f, 392.00f,
        261.63f, 440.00f, 392.00f, 493.88f, 261.63f,
    };
    const std::vector<float> fSharpMajorMelody = {
        369.99f, 415.30f, 466.16f, 493.88f, 554.37f,
        369.99f, 622.25f, 554.37f, 698.46f, 369.99f,
    };
    VXTuneAudioProcessor p;
    p.prepareToPlay(kSampleRate, kBlock);

    runMelodyAndTraceAutoKey(p, cMajorMelody, 0.35f, 60.0f);
    const auto locked = p.autoKeyStateForTests();
    check(locked.root == 0 && locked.scale == 2, "locks onto C major before the modulation");

    const auto trace = runMelodyAndTraceAutoKey(p, fSharpMajorMelody, 0.35f, 90.0f);
    const auto adaptedAt = std::find_if(trace.begin(), trace.end(),
        [](const VXTuneAudioProcessor::AutoKeyStateForTests& s) {
            return s.root == 6 && s.scale == 2; // F# = pitch class 6
        });
    check(adaptedAt != trace.end(), "eventually recognises the modulation to F# major");
    if (adaptedAt != trace.end()) {
        const auto index = std::distance(trace.begin(), adaptedAt);
        const float lagSeconds = static_cast<float>(index) * 0.35f;
        std::printf("    modulation recognised after ~%.1fs of new material\n", lagSeconds);
        check(lagSeconds < 60.0f, "adapts within a reasonable time (<60s) of the modulation");
    }
}

// A melody balanced between a major tonic (G) and its relative minor tonic
// (E) pushes the mode tie-break toward 50/50 - exactly the regime that used
// to flip on every frame. The hysteresis band should keep transitions rare
// even under this adversarial balance, not eliminate them outright.
void testAutoKeyModeHysteresisBoundsFlicker() {
    std::printf("Auto-key: mode hysteresis bounds flicker near a 50/50 tie:\n");
    const std::vector<float> gMajEMinBalanced = {
        392.00f, 329.63f, 392.00f, 329.63f, 293.66f, 246.94f, 392.00f, 329.63f,
    };
    VXTuneAudioProcessor p;
    p.prepareToPlay(kSampleRate, kBlock);
    const auto trace = runMelodyAndTraceAutoKey(p, gMajEMinBalanced, 0.35f, 24.0f);

    const int transitions = countKeyTransitions(trace);
    check(transitions <= 4,
          "stays bounded under a near-tied mode (got " + std::to_string(transitions)
              + " transition(s) over " + std::to_string(trace.size()) + " notes)");
}

// Regression for a bug found live in a DAW: resetSuite() (reached from
// AudioProcessor::reset(), which hosts call on transport stop/rewind and on
// every loop-region wraparound) used to wipe the learned key histogram. A
// user looping a section while tuning would never accumulate the ~2s of
// continuous evidence needed to commit, because the loop kept erasing
// progress before it got there - the key would never settle, even though
// the same melody rendered offline in one continuous pass converged fine.
void testAutoKeySurvivesTransportResetBetweenLoopIterations() {
    std::printf("Auto-key: learned key survives reset() across loop iterations:\n");
    const std::vector<float> cMajorMelody = {
        261.63f, 293.66f, 329.63f, 349.23f, 392.00f,
        261.63f, 440.00f, 392.00f, 493.88f, 261.63f,
    };
    VXTuneAudioProcessor p;
    p.prepareToPlay(kSampleRate, kBlock);

    // Simulate a host looping a ~1.5s region: too short on its own to clear
    // the auto-key commit gate, with reset() firing between iterations the
    // way a DAW does on loop wraparound.
    bool committed = false;
    for (int loopIteration = 0; loopIteration < 12 && !committed; ++loopIteration) {
        const auto trace = runMelodyAndTraceAutoKey(p, cMajorMelody, 0.15f, 1.5f);
        if (!trace.empty()) {
            const auto& s = trace.back();
            committed = s.confidence >= 0.15f && s.root == 0 && s.scale == 2;
        }
        p.reset();
    }
    check(committed,
          "key commits across repeated short loop iterations despite reset() between them");
}

void setChoice(VXTuneAudioProcessor& p, const char* id, const int index) {
    if (auto* param = p.getValueTreeState().getParameter(id))
        param->setValueNotifyingHost(param->convertTo0to1(static_cast<float>(index)));
}

// Regression: pinning a root while leaving Scale on Auto (or vice versa)
// used to reuse the free joint pick's other half regardless of whether it
// applied to the pinned value - a pairing the algorithm never validated
// together. Lock the free pick onto G major (root=7), then pin a root
// whose correct mode genuinely differs from G major's: E is G major's
// relative minor tonic, so the SAME 7 notes correlate far better as E
// minor than E major. The old code would have blindly reported "major"
// (G major's mode) for root E; the fix must independently find minor.
void testFixedRootPicksItsOwnModeNotTheFreePicksMode() {
    std::printf("Auto-key: a pinned root gets its own mode, not the free pick's:\n");
    const std::vector<float> gMajorMelody = {
        392.00f, 440.00f, 493.88f, 523.25f, 587.33f,
        392.00f, 659.25f, 587.33f, 739.99f, 392.00f,
    };
    VXTuneAudioProcessor p;
    p.prepareToPlay(kSampleRate, kBlock);
    runMelodyAndTraceAutoKey(p, gMajorMelody, 0.35f, 20.0f);
    const auto free = p.autoKeyStateForTests();
    check(free.root == 7 && free.scale == 2, "free pick locks onto G major");

    const bool modeForE = p.bestModeForFixedRootForTests(4); // E = pitch class 4
    check(!modeForE, "root E is independently found to be minor, not G major's major");
}

// Mirror case: pinning the mode should pick its own best root, not reuse
// the free pick's root. Locked onto G major, pinning minor mode should
// find E (the relative minor tonic) rather than reusing root G.
void testFixedModePicksItsOwnRootNotTheFreePicksRoot() {
    std::printf("Auto-key: a pinned mode gets its own root, not the free pick's:\n");
    const std::vector<float> gMajorMelody = {
        392.00f, 440.00f, 493.88f, 523.25f, 587.33f,
        392.00f, 659.25f, 587.33f, 739.99f, 392.00f,
    };
    VXTuneAudioProcessor p;
    p.prepareToPlay(kSampleRate, kBlock);
    runMelodyAndTraceAutoKey(p, gMajorMelody, 0.35f, 20.0f);
    const auto free = p.autoKeyStateForTests();
    check(free.root == 7 && free.scale == 2, "free pick locks onto G major");

    const int rootForMinor = p.bestRootForFixedModeForTests(false);
    check(rootForMinor == 4, "minor mode is independently rooted at E, not G ("
              + std::to_string(rootForMinor) + ")");
}

// Regression: the mask fed to CorrectionEngine used to update every frame,
// which could retarget TargetEstimator's held note mid-sustain purely
// because the key/scale changed, not because the singer's pitch moved.
// It should only update between notes.
void testAppliedScaleMaskHoldsThroughAnUninterruptedSustain() {
    std::printf("Applied scale mask: holds through a sustain, updates between notes:\n");
    VXTuneAudioProcessor p;
    p.prepareToPlay(kSampleRate, kBlock);
    setChoice(p, "keyroot", 1); // C
    setChoice(p, "scale", 2);   // Major

    juce::MidiBuffer midi;
    juce::AudioBuffer<float> block(2, kBlock);
    double phase = 0.0;
    const int blocksPerSecond = static_cast<int>(kSampleRate) / kBlock;
    const auto feedTone = [&](const float hz, const int blocks) {
        for (int b = 0; b < blocks; ++b) {
            for (int i = 0; i < kBlock; ++i) {
                phase += kTwoPi * hz / kSampleRate;
                float s = 0.0f;
                for (int h = 1; h <= 3; ++h)
                    s += std::sin(static_cast<float>(phase) * h) / static_cast<float>(h);
                block.setSample(0, i, 0.25f * s);
                block.setSample(1, i, 0.25f * s);
            }
            p.processBlock(block, midi);
        }
    };

    feedTone(261.63f, blocksPerSecond); // ~1s of C4, let the mask settle
    const std::uint16_t maskBeforeSwitch = p.appliedScaleMaskForTests();

    setChoice(p, "keyroot", 8); // switch to G mid-sustain, no gap
    feedTone(261.63f, blocksPerSecond);
    check(p.appliedScaleMaskForTests() == maskBeforeSwitch,
          "applied mask holds steady through an uninterrupted sustain");

    block.clear();
    for (int b = 0; b < 10; ++b)
        p.processBlock(block, midi);
    feedTone(261.63f, blocksPerSecond);
    check(p.appliedScaleMaskForTests() != maskBeforeSwitch,
          "applied mask updates once the note actually ends");
}

// Regression for a real bug found on real material: auto-key's major/minor
// mode can flip mid-note (the tie-break settling slightly after the note
// already started), and the mask captured at the note's own boundary can
// therefore exclude the pitch class the singer is actually on for the rest
// of that note - forcing correction onto the nearest note the stale mask
// still allows, a full semitone off, until the note ends. Manually pinning
// Key/Scale here (rather than waiting on auto-key's own hysteresis) to
// reproduce the exact mask-membership transition deterministically: Bb
// Natural Minor excludes D, Bb Major includes it.
void testAppliedScaleMaskRecoversMidNoteWhenPitchClassNewlyAllowed() {
    std::printf("Applied scale mask: recovers mid-note once the sung pitch "
                "class becomes newly allowed:\n");
    VXTuneAudioProcessor p;
    p.prepareToPlay(kSampleRate, kBlock);
    setChoice(p, "keyroot", 11); // Bb
    setChoice(p, "scale", 3);    // Natural Minor - excludes D

    juce::MidiBuffer midi;
    juce::AudioBuffer<float> block(2, kBlock);
    double phase = 0.0;
    const int blocksPerSecond = static_cast<int>(kSampleRate) / kBlock;
    const auto feedTone = [&](const float hz, const int blocks) {
        for (int b = 0; b < blocks; ++b) {
            for (int i = 0; i < kBlock; ++i) {
                phase += kTwoPi * hz / kSampleRate;
                float s = 0.0f;
                for (int h = 1; h <= 3; ++h)
                    s += std::sin(static_cast<float>(phase) * h) / static_cast<float>(h);
                block.setSample(0, i, 0.25f * s);
                block.setSample(1, i, 0.25f * s);
            }
            p.processBlock(block, midi);
        }
    };

    block.clear();
    for (int b = 0; b < 10; ++b)
        p.processBlock(block, midi); // genuine silence, lets the mask capture once
    const float d5Hz = 440.0f * std::exp2(5.0f / 12.0f); // D5, pitch class D
    feedTone(d5Hz, blocksPerSecond); // ~1s, settle on Bb Natural Minor
    const std::uint16_t maskBeforeSwitch = p.appliedScaleMaskForTests();
    check((maskBeforeSwitch & (1u << 2)) == 0,
          "Bb Natural Minor excludes D before the switch");

    setChoice(p, "scale", 2); // Major mid-sustain, no gap - now includes D
    feedTone(d5Hz, blocksPerSecond);
    check((p.appliedScaleMaskForTests() & (1u << 2)) != 0,
          "mask recovers D mid-note once the update would newly allow the "
              "pitch actually being sung, without waiting for the note to end");
}

void testPitchTraceFeed() {
    std::printf("Pitch trace feed:\n");
    const float sharpHz = 220.0f * std::exp2(30.0f / 1200.0f);
    VXTuneAudioProcessor p;
    p.prepareToPlay(kSampleRate, kBlock);
    setParam(p, "amount", 0.5f);
    setParam(p, "natural", 0.5f);
    double phase = 0.0;
    runToneAndMeasure(p, sharpHz, 2.0f, 0.1f, phase);

    check(p.getPitchTraceConfidence() > 0.8f, "voiced input yields confident trace");
    const float detected = p.getPitchTraceDetectedCents();
    const float corrected = p.getPitchTraceCorrectedCents();
    // A3 = -1200c vs A440; input +30c sharp, corrected back toward the note.
    check(std::abs(detected - (-1170.0f)) < 12.0f,
          "detected trace near A3+30c (got " + std::to_string(detected) + "c)");
    check(std::abs(corrected - (-1200.0f)) < 12.0f,
          "corrected trace near A3 (got " + std::to_string(corrected) + "c)");

    VXTuneAudioProcessor silent;
    silent.prepareToPlay(kSampleRate, kBlock);
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> block(2, kBlock);
    block.clear();
    for (int b = 0; b < 40; ++b)
        silent.processBlock(block, midi);
    check(silent.getPitchTraceConfidence() <= 0.0f, "silence yields zero-confidence trace");
}

// Enables the (opt-in) sidechain bus and returns a 4-channel buffer
// (main L/R + sidechain L/R), mirroring VXLeveler's own sidechain-bus test
// pattern (tests/VxLevelerRiderTests.cpp::processorSidechainBus).
juce::AudioBuffer<float> prepareWithSidechainBus(VXTuneAudioProcessor& p) {
    auto layout = p.getBusesLayout();
    layout.inputBuses.set(1, juce::AudioChannelSet::stereo());
    const bool ok = p.setBusesLayout(layout);
    p.prepareToPlay(kSampleRate, kBlock);
    (void) ok;
    return juce::AudioBuffer<float>(4, kBlock);
}

void testSidechainBusExistsAndNeverLeaksToOutput() {
    std::printf("Sidechain: bus exists and is detection-only (never reaches output):\n");
    VXTuneAudioProcessor p;
    const bool hasScBus = p.getBusCount(true) > 1;
    check(hasScBus, "opts into a sidechain input bus");
    if (!hasScBus)
        return;

    auto block = prepareWithSidechainBus(p);
    juce::MidiBuffer midi;
    float maxMainOut = 0.0f;
    double phase = 0.0;
    for (int b = 0; b < 60; ++b) {
        block.clear();
        for (int i = 0; i < kBlock; ++i) {
            phase += kTwoPi * 261.63f / kSampleRate;
            const float sc = 0.3f * std::sin(static_cast<float>(phase));
            block.setSample(2, i, sc);
            block.setSample(3, i, sc);
        }
        p.processBlock(block, midi);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < kBlock; ++i)
                maxMainOut = std::max(maxMainOut, std::abs(block.getSample(ch, i)));
    }
    check(maxMainOut < 1.0e-6f,
          "silent main input stays silent with sidechain signal present (max "
              + std::to_string(maxMainOut) + ")");
    check(p.hasSidechainActive(), "reports sidechain active while fed a real signal");
}

// Regression for a real bug: a structurally-connected sidechain bus (the
// host negotiated the channels) is not the same as a genuinely wired
// sidechain send - some hosts reuse/copy a plugin's bus layout across
// instances, which can leave an instance's bus "connected" with only
// silence actually flowing into it. `hasSidechainActive()` must reflect
// real signal, matching VXLeveler's isSidechainSteering()/scConfidence
// pattern, not bus connectivity alone.
void testSidechainBusConnectedButSilentDoesNotReportActive() {
    std::printf("Sidechain: a connected-but-silent bus does not report active:\n");
    VXTuneAudioProcessor p;
    auto block = prepareWithSidechainBus(p);
    juce::MidiBuffer midi;
    for (int b = 0; b < 60; ++b) {
        block.clear(); // main AND sidechain channels both silent
        p.processBlock(block, midi);
    }
    check(!p.hasSidechainActive(),
          "a structurally-connected bus carrying only silence does not "
              "report sidechain active");
}

// Regression for the real mechanism behind a reported "phasing" bug: VST3
// hosts can pass channels beyond a plugin's declared output bus straight
// through unprocessed. Sidechain is detection-only by contract, so nothing
// in this plugin ever writes to those channels - meaning, without an
// explicit clear, the RAW pre-plugin sidechain audio (e.g. a receive
// feeding the sidechain pins, undelayed by this plugin's own latency)
// would still be sitting in the host buffer when processBlock returns. If
// a host then treats those channels as live track audio downstream, that
// raw copy can reach the mix alongside this plugin's (latency-delayed)
// output and phase against the real source. The framework must zero the
// sidechain region of the buffer in place, not just avoid reading it.
void testSidechainChannelsAreClearedInPlaceAfterProcessing() {
    std::printf("Sidechain: buffer channels are cleared in place after "
                "processing (host pass-through protection):\n");
    VXTuneAudioProcessor p;
    auto block = prepareWithSidechainBus(p);
    juce::MidiBuffer midi;
    double phase = 0.0;
    float maxSidechainAfter = 0.0f;
    for (int b = 0; b < 20; ++b) {
        block.clear();
        for (int i = 0; i < kBlock; ++i) {
            phase += kTwoPi * 261.63f / kSampleRate;
            const float sc = 0.4f * std::sin(static_cast<float>(phase));
            block.setSample(2, i, sc);
            block.setSample(3, i, sc);
        }
        p.processBlock(block, midi);
        for (int ch = 2; ch < 4; ++ch)
            for (int i = 0; i < kBlock; ++i)
                maxSidechainAfter = std::max(maxSidechainAfter, std::abs(block.getSample(ch, i)));
    }
    check(maxSidechainAfter < 1.0e-6f,
          "sidechain channels read 0.4 amplitude in, silent after processBlock "
              "returns (max " + std::to_string(maxSidechainAfter) + ")");
}

// Feeds silence on the main (vocal) input and a repeating C-major melody
// on the sidechain input; traces auto-key state the same way
// runMelodyAndTraceAutoKey() does for the vocal-only path.
std::vector<VXTuneAudioProcessor::AutoKeyStateForTests> runSidechainMelodyAndTraceAutoKey(
        VXTuneAudioProcessor& p, juce::AudioBuffer<float>& block,
        const std::vector<float>& melodyHz, const float noteSeconds, const float totalSeconds) {
    juce::MidiBuffer midi;
    const int blocksPerNote = std::max(1, static_cast<int>(noteSeconds * kSampleRate / kBlock));
    const int totalBlocks = static_cast<int>(totalSeconds * kSampleRate / kBlock);
    std::vector<VXTuneAudioProcessor::AutoKeyStateForTests> trace;
    double phase = 0.0;
    for (int b = 0; b < totalBlocks; ++b) {
        const size_t noteIndex = static_cast<size_t>(b / blocksPerNote) % melodyHz.size();
        const float hz = melodyHz[noteIndex];
        block.clear();
        for (int i = 0; i < kBlock; ++i) {
            phase += kTwoPi * hz / kSampleRate;
            float s = 0.0f;
            for (int h = 1; h <= 3; ++h)
                s += std::sin(static_cast<float>(phase) * h) / static_cast<float>(h);
            const float sc = 0.3f * s;
            block.setSample(2, i, sc);
            block.setSample(3, i, sc);
        }
        p.processBlock(block, midi);
        if (b % blocksPerNote == blocksPerNote - 1)
            trace.push_back(p.autoKeyStateForTests());
    }
    return trace;
}

// The real acceptance test for the feature: a silent vocal (no melody at
// all - as if the instrumental is playing an intro before the singer
// enters) still lets VX Tune establish the song's key, purely from the
// sidechain's harmonic content, via the same histogram/profile-correlation
// machinery the vocal itself uses (VxTuneHarmonicContext.h feeds the
// identical autoKeyHistogram bins). Uses the same C-major content and the
// same 0.15 confidence gate as testAutoKeyLocksOntoScaleAndHolds() so the
// two are directly comparable: vocal-only needs a sung melody to reach
// this; here there is no vocal at all.
void testSidechainAloneEstablishesKeyWithNoVocal() {
    std::printf("Sidechain: establishes the key from the instrumental alone, no vocal singing:\n");
    const std::vector<float> cMajorMelody = {
        261.63f, 293.66f, 329.63f, 349.23f, 392.00f,
        261.63f, 440.00f, 392.00f, 493.88f, 261.63f,
    };
    VXTuneAudioProcessor p;
    auto block = prepareWithSidechainBus(p);

    const auto initial = p.autoKeyStateForTests();
    check(initial.confidence < 0.15f, "starts unconverged before any signal");

    const auto trace = runSidechainMelodyAndTraceAutoKey(p, block, cMajorMelody, 0.35f, 20.0f);
    const auto convergedAt = std::find_if(trace.begin(), trace.end(),
        [](const VXTuneAudioProcessor::AutoKeyStateForTests& s) {
            return s.confidence >= 0.15f && s.root == 0 && s.scale == 2;
        });
    check(convergedAt != trace.end(),
          "locks onto C major from sidechain content alone (main input silent throughout)");
}

} // namespace

int main() {
    std::printf("VX Tune plugin tests\n");
    testLatencyReported();
    testListenPlaysChangesOnly();
    testAutoKeyLocksOntoScaleAndHolds();
    testAutoKeyAdaptsToModulationWithinBoundedLag();
    testAutoKeyModeHysteresisBoundsFlicker();
    testAutoKeySurvivesTransportResetBetweenLoopIterations();
    testFixedRootPicksItsOwnModeNotTheFreePicksMode();
    testFixedModePicksItsOwnRootNotTheFreePicksRoot();
    testAppliedScaleMaskHoldsThroughAnUninterruptedSustain();
    testAppliedScaleMaskRecoversMidNoteWhenPitchClassNewlyAllowed();
    testPitchTraceFeed();
    testSidechainBusExistsAndNeverLeaksToOutput();
    testSidechainBusConnectedButSilentDoesNotReportActive();
    testSidechainChannelsAreClearedInPlaceAfterProcessing();
    testSidechainAloneEstablishesKeyWithNoVocal();
    if (failures == 0) {
        std::printf("All VX Tune plugin tests passed\n");
        return 0;
    }
    std::printf("%d VX Tune plugin test(s) FAILED\n", failures);
    return 1;
}
