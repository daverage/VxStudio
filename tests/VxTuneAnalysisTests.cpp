// VX Tune Phase 1 analysis tests: pitch detector + performance decomposition.
// Pure DSP, no JUCE. Contracts under test come from
// docs/Task Based/VXTUNE_VISION_ARCHITECTURE.md §5.1, §5.2, §10.

#include "../Source/vxstudio/products/tune/dsp/VxTunePitchDetector.h"
#include "../Source/vxstudio/products/tune/dsp/VxTuneDecomposition.h"
#include "../Source/vxstudio/products/tune/dsp/VxTuneCorrectionEngine.h"
#include "../Source/vxstudio/products/tune/dsp/VxTunePitchShifter.h"
#include "../Source/vxstudio/products/tune/dsp/VxTunePsolaShifter.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <random>
#include <string>
#include <vector>

using vxsuite::tune::Behaviour;
using vxsuite::tune::CorrectionEngine;
using vxsuite::tune::EstimateReason;
using vxsuite::tune::PerformanceDecomposition;
using vxsuite::tune::PitchDetector;
using vxsuite::tune::PitchFrame;
using vxsuite::tune::PitchObservation;
using vxsuite::tune::PitchShifter;
using vxsuite::tune::PsolaShifter;

namespace {

int failures = 0;

void check(const bool condition, const std::string& label) {
    if (condition) {
        std::printf("  PASS  %s\n", label.c_str());
    } else {
        std::printf("  FAIL  %s\n", label.c_str());
        ++failures;
    }
}

constexpr float kTwoPi = 6.28318530717958647692f;

float hzToCents(const float hz) {
    return 1200.0f * std::log2(hz / 440.0f);
}

// Generates audio via a frequency function (Hz per sample index) so vibrato
// and note changes have a continuous phase.
std::vector<float> renderTone(const double sampleRate, const float seconds,
                              const float amplitude, const int numHarmonics,
                              const std::function<float(float)>& hzAtTime) {
    const int n = static_cast<int>(sampleRate * seconds);
    std::vector<float> out(static_cast<size_t>(n), 0.0f);
    double phase = 0.0;
    for (int i = 0; i < n; ++i) {
        const float t = static_cast<float>(i / sampleRate);
        phase += kTwoPi * hzAtTime(t) / sampleRate;
        float s = 0.0f;
        for (int h = 1; h <= numHarmonics; ++h)
            s += std::sin(static_cast<float>(phase) * h) / static_cast<float>(h);
        out[static_cast<size_t>(i)] = amplitude * s;
    }
    return out;
}

struct AnalysisRun {
    std::vector<PitchFrame> frames;
};

AnalysisRun analyse(const std::vector<float>& audio, const double sampleRate) {
    PitchDetector detector;
    detector.prepare(sampleRate, PitchDetector::Config {});
    PerformanceDecomposition decomposition;
    decomposition.prepare(sampleRate / detector.hopSamples(),
                          PerformanceDecomposition::Config {});

    AnalysisRun run;
    PitchObservation observations[64];
    const int block = 512;
    for (size_t offset = 0; offset < audio.size(); offset += block) {
        const int n = static_cast<int>(std::min<size_t>(block, audio.size() - offset));
        const int produced = detector.process(audio.data() + offset, n, observations, 64);
        for (int i = 0; i < produced; ++i)
            run.frames.push_back(decomposition.process(observations[i]));
    }
    return run;
}

// Voiced frames after a settle period.
std::vector<PitchFrame> settled(const AnalysisRun& run, const size_t skip = 40) {
    std::vector<PitchFrame> out;
    for (size_t i = skip; i < run.frames.size(); ++i)
        if (run.frames[i].f0Hz.value > 0.0f)
            out.push_back(run.frames[i]);
    return out;
}

float medianDetectedCents(const std::vector<PitchFrame>& frames) {
    std::vector<float> cents;
    cents.reserve(frames.size());
    for (const auto& f : frames)
        cents.push_back(hzToCents(f.f0Hz.value));
    std::sort(cents.begin(), cents.end());
    return cents.empty() ? 0.0f : cents[cents.size() / 2];
}

void testSteadyTone(const double sampleRate, const float hz, const int harmonics,
                    const float toleranceCents, const std::string& label) {
    const auto audio = renderTone(sampleRate, 1.5f, 0.25f, harmonics,
                                  [hz](float) { return hz; });
    const auto frames = settled(analyse(audio, sampleRate));
    check(frames.size() > 100, label + ": produced voiced frames");
    if (frames.empty())
        return;

    const float err = medianDetectedCents(frames) - hzToCents(hz);
    check(std::abs(err) < toleranceCents,
          label + ": median error " + std::to_string(err) + "c within +/-"
              + std::to_string(toleranceCents) + "c");

    int confident = 0;
    for (const auto& f : frames)
        if (f.f0Hz.confidence > 0.85f && f.f0Hz.reason == EstimateReason::nominal)
            ++confident;
    check(confident > static_cast<int>(frames.size() * 3 / 4),
          label + ": mostly high-confidence nominal frames");
}

void testSilence() {
    std::printf("Silence:\n");
    const std::vector<float> audio(48000, 0.0f);
    const auto run = analyse(audio, 48000.0);
    bool allUnvoiced = !run.frames.empty();
    for (const auto& f : run.frames)
        if (f.f0Hz.value != 0.0f || f.f0Hz.confidence != 0.0f
            || f.f0Hz.reason != EstimateReason::unvoiced || f.decompConfidence != 0.0f)
            allUnvoiced = false;
    check(allUnvoiced, "silence yields unvoiced frames with zero confidence");
}

void testNoise() {
    std::printf("White noise:\n");
    std::mt19937 rng(1234);
    std::uniform_real_distribution<float> dist(-0.1f, 0.1f);
    std::vector<float> audio(48000);
    for (auto& s : audio)
        s = dist(rng);
    const auto run = analyse(audio, 48000.0);
    bool neverConfidentPitch = true;
    for (size_t i = 40; i < run.frames.size(); ++i) {
        const auto& f = run.frames[i];
        if (f.f0Hz.confidence > 0.6f && f.f0Hz.reason == EstimateReason::nominal)
            neverConfidentPitch = false;
    }
    check(neverConfidentPitch, "noise never yields confident nominal pitch");
}

void testVibrato() {
    std::printf("Vibrato preservation (220 Hz +/-50c @ 5.5 Hz):\n");
    const double sr = 48000.0;
    const auto audio = renderTone(sr, 3.0f, 0.25f, 3, [](const float t) {
        const float cents = 50.0f * std::sin(kTwoPi * 5.5f * t);
        return 220.0f * std::exp2(cents / 1200.0f);
    });
    const auto frames = settled(analyse(audio, sr), 80);
    check(frames.size() > 300, "vibrato: produced voiced frames");
    if (frames.empty())
        return;

    float rawMin = 1.0e9f, rawMax = -1.0e9f;
    float centreMin = 1.0e9f, centreMax = -1.0e9f;
    float residualMin = 1.0e9f, residualMax = -1.0e9f;
    float worstIdentityError = 0.0f;
    for (const auto& f : frames) {
        const float raw = hzToCents(f.f0Hz.value);
        rawMin = std::min(rawMin, raw);           rawMax = std::max(rawMax, raw);
        centreMin = std::min(centreMin, f.centreCents);
        centreMax = std::max(centreMax, f.centreCents);
        residualMin = std::min(residualMin, f.residualCents);
        residualMax = std::max(residualMax, f.residualCents);
        worstIdentityError = std::max(worstIdentityError,
            std::abs(raw - (f.centreCents + f.residualCents)));
    }
    const float rawExtent = rawMax - rawMin;            // expect ~100c
    const float centreExtent = centreMax - centreMin;
    const float residualExtent = residualMax - residualMin;

    check(rawExtent > 70.0f && rawExtent < 130.0f,
          "detector tracks vibrato (raw extent " + std::to_string(rawExtent) + "c)");
    check(centreExtent < 30.0f,
          "centre line stays steady under vibrato (extent "
              + std::to_string(centreExtent) + "c)");
    check(residualExtent > 0.8f * rawExtent,
          "expression residual keeps >=80% of vibrato (extent "
              + std::to_string(residualExtent) + "c)");
    check(worstIdentityError < 1.0e-3f,
          "reconstruction identity centre+residual==raw (worst "
              + std::to_string(worstIdentityError) + "c)");

    const float centreError = 0.5f * (centreMin + centreMax) - hzToCents(220.0f);
    check(std::abs(centreError) < 15.0f,
          "centre mean near 220 Hz (error " + std::to_string(centreError) + "c)");
}

void testNoteChangeSnap() {
    std::printf("Note change re-anchors the centre (A3 -> C4):\n");
    const double sr = 48000.0;
    const auto audio = renderTone(sr, 2.0f, 0.25f, 3, [](const float t) {
        return t < 1.0f ? 220.0f : 261.63f;
    });
    const auto run = analyse(audio, sr);

    const int hop = 256;
    const auto samplesToFrameTime = [&](const PitchFrame& f) {
        return static_cast<double>(f.timeSamples) / sr;
    };
    // 150 ms after the switch the centre must sit near the new note.
    bool converged = true;
    bool sawLateFrames = false;
    for (const auto& f : run.frames) {
        const double t = samplesToFrameTime(f);
        if (t > 1.15 && f.f0Hz.value > 0.0f) {
            sawLateFrames = true;
            if (std::abs(f.centreCents - hzToCents(261.63f)) > 20.0f)
                converged = false;
        }
    }
    (void) hop;
    check(sawLateFrames && converged,
          "centre within 20c of C4 from 150 ms after the note change");
}

// ---- PSOLA shifter tests (build spec F1) ----

// Runs audio through detector-driven PSOLA at a fixed shift.
std::vector<float> runPsola(const std::vector<float>& audio, const double sr,
                            const float shiftCents, const int block = 512) {
    PitchDetector detector;
    detector.prepare(sr, PitchDetector::Config {});
    PsolaShifter shifter;
    shifter.prepare(sr, block, 1);
    shifter.setShiftCents(shiftCents);

    std::vector<float> out(audio);
    PitchObservation obs[64];
    for (size_t offset = 0; offset < audio.size();
         offset += static_cast<size_t>(block)) {
        const int n = static_cast<int>(std::min<size_t>(
            static_cast<size_t>(block), audio.size() - offset));
        const int produced = detector.process(audio.data() + offset, n, obs, 64);
        for (int i = 0; i < produced; ++i) {
            const auto& o = obs[i];
            shifter.setPeriodHint(
                o.f0Hz.value > 0.0f ? static_cast<float>(sr) / o.f0Hz.value : 0.0f,
                o.f0Hz.confidence);
        }
        float* chans[1] = { out.data() + offset };
        shifter.process(chans, 1, n);
    }
    return out;
}

// Envelope-weighted spectral centroid over a band, via direct DFT of a
// steady 8192-sample window — harmonic-quantisation-robust formant probe.
float bandCentroidHz(const std::vector<float>& audio, const double sr,
                     const float fromHz, const float toHz) {
    const int n = 8192;
    const size_t start = audio.size() > static_cast<size_t>(n) + 48000
        ? audio.size() - static_cast<size_t>(n) - 1000 : 0;
    double num = 0.0, den = 0.0;
    for (float hz = fromHz; hz <= toHz; hz += 12.0f) {
        double re = 0.0, im = 0.0;
        for (int i = 0; i < n; ++i) {
            const double w = 0.5 * (1.0 - std::cos(2.0 * 3.14159265358979 * i / n));
            const double ph = 2.0 * 3.14159265358979 * hz * i / sr;
            const double v = w * audio[start + static_cast<size_t>(i)];
            re += v * std::cos(ph);
            im += v * std::sin(ph);
        }
        const double mag = std::sqrt(re * re + im * im);
        num += mag * hz;
        den += mag;
    }
    return den > 0.0 ? static_cast<float>(num / den) : 0.0f;
}

void testPsolaRatioAccuracy() {
    std::printf("PSOLA ratio accuracy:\n");
    const double sr = 48000.0;
    const struct { float hz; int harmonics; float shift; } cases[] = {
        { 220.0f, 1, 100.0f }, { 220.0f, 1, -50.0f },
        { 196.0f, 10, 25.0f }, { 196.0f, 10, -100.0f },
        { 110.0f, 6, 50.0f },
    };
    for (const auto& c : cases) {
        const auto audio = renderTone(sr, 2.0f, 0.25f, c.harmonics,
                                      [&](float) { return c.hz; });
        const auto out = runPsola(audio, sr, c.shift);
        const auto frames = settled(analyse(out, sr), 120);
        const float got = frames.empty() ? -1.0e9f : medianDetectedCents(frames);
        const float want = hzToCents(c.hz) + c.shift;
        check(std::abs(got - want) < 5.0f,
              std::to_string(c.hz) + " Hz shifted " + std::to_string(c.shift)
                  + "c -> error " + std::to_string(got - want) + "c");
    }
}

void testPsolaFormantPreservation() {
    std::printf("PSOLA formant preservation (vowel-like, +100c):\n");
    const double sr = 48000.0;
    // 150 Hz voice with a fixed spectral-envelope peak near 1 kHz.
    const int harmonics = 14;
    std::vector<float> audio(static_cast<size_t>(sr * 2.5));
    double phase = 0.0;
    for (size_t i = 0; i < audio.size(); ++i) {
        phase += 2.0 * 3.14159265358979 * 150.0 / sr;
        float s = 0.0f;
        for (int h = 1; h <= harmonics; ++h) {
            const float hz = 150.0f * static_cast<float>(h);
            const float envelope = std::exp(-0.5f * std::pow((hz - 1000.0f) / 350.0f, 2.0f))
                                 + 0.05f;
            s += envelope * std::sin(static_cast<float>(phase) * h);
        }
        audio[i] = 0.12f * s;
    }

    const float inCentroid = bandCentroidHz(audio, sr, 500.0f, 1700.0f);

    const auto psolaOut = runPsola(audio, sr, 100.0f);
    const float psolaCentroid = bandCentroidHz(psolaOut, sr, 500.0f, 1700.0f);
    const float psolaShiftPct = 100.0f * (psolaCentroid / inCentroid - 1.0f);

    // Contrast: the dual-tap resampling shifter moves the envelope by the
    // full ratio (+5.9% at +100c).
    std::vector<float> dualOut(audio);
    {
        PitchShifter dual;
        dual.prepare(sr, 512);
        dual.setShiftCents(100.0f);
        for (size_t offset = 0; offset < dualOut.size(); offset += 512) {
            const int n = static_cast<int>(std::min<size_t>(512, dualOut.size() - offset));
            dual.process(dualOut.data() + offset, n);
        }
    }
    const float dualCentroid = bandCentroidHz(dualOut, sr, 500.0f, 1700.0f);
    const float dualShiftPct = 100.0f * (dualCentroid / inCentroid - 1.0f);

    check(std::abs(psolaShiftPct) < 2.0f,
          "PSOLA keeps the envelope (centroid moved "
              + std::to_string(psolaShiftPct) + "%)");
    check(dualShiftPct > 3.5f,
          "dual-tap moves the envelope as expected (contrast: "
              + std::to_string(dualShiftPct) + "%)");
}

void testPsolaParkAndUnvoiced() {
    std::printf("PSOLA park transparency and unvoiced passthrough:\n");
    const double sr = 48000.0;

    {
        const auto audio = renderTone(sr, 2.0f, 0.25f, 3, [](float) { return 220.0f; });
        const auto out = runPsola(audio, sr, 0.0f);
        const int d = 600;   // ceil(48000/80)
        double num = 0.0, den = 0.0;
        for (size_t i = 48000; i + d < audio.size(); ++i) {
            const float delta = out[i + d] - audio[i];
            num += delta * delta;
            den += audio[i] * audio[i];
        }
        const float db = 10.0f * std::log10(static_cast<float>(num / std::max(den, 1e-12)));
        check(db < -60.0f, "zero shift is a pure aligned delay ("
                               + std::to_string(db) + " dB)");
    }

    {
        std::mt19937 rng(77);
        std::uniform_real_distribution<float> dist(-0.1f, 0.1f);
        std::vector<float> noise(96000);
        for (auto& s : noise)
            s = dist(rng);
        const auto out = runPsola(noise, sr, 100.0f);
        const int d = 600;
        double num = 0.0, den = 0.0;
        for (size_t i = 24000; i + d < noise.size(); ++i) {
            const float delta = out[i + d] - noise[i];
            num += delta * delta;
            den += noise[i] * noise[i];
        }
        const float db = 10.0f * std::log10(static_cast<float>(num / std::max(den, 1e-12)));
        check(db < -60.0f, "unvoiced input passes through aligned ("
                               + std::to_string(db) + " dB)");
    }
}

void testPsolaDoesNotSmearVoiceIntoGaps() {
    std::printf("PSOLA voiced-to-gap cleanup:\n");
    const double sr = 48000.0;
    const int n = static_cast<int>(sr * 1.4);
    std::vector<float> audio(static_cast<size_t>(n), 0.0f);
    double phase = 0.0;
    for (int i = 0; i < n; ++i) {
        const float t = static_cast<float>(i / sr);
        phase += kTwoPi * 220.0 / sr;
        const bool voiced = t < 0.50f || (t >= 0.82f && t < 1.25f);
        if (voiced) {
            float s = 0.0f;
            for (int h = 1; h <= 3; ++h)
                s += std::sin(static_cast<float>(phase) * h) / static_cast<float>(h);
            audio[static_cast<size_t>(i)] = 0.25f * s;
        }
    }

    const auto out = runPsola(audio, sr, 50.0f);
    const int d = 600;
    double gapSq = 0.0;
    int gapSamples = 0;
    for (int i = static_cast<int>(0.58 * sr); i < static_cast<int>(0.74 * sr); ++i) {
        const float v = out[static_cast<size_t>(i + d)];
        gapSq += v * v;
        ++gapSamples;
    }
    const float gapRms = gapSamples > 0
        ? static_cast<float>(std::sqrt(gapSq / gapSamples)) : 1.0f;
    check(gapRms < 0.002f,
          "voiced epochs do not smear into the silent gap (rms "
              + std::to_string(gapRms) + ")");
}

void testPsolaTortureTransitions() {
    std::printf("PSOLA transition torture (tones/silence/jump/glide):\n");
    const double sr = 48000.0;
    const int n = static_cast<int>(sr * 2.3);

    // Phase-continuous multi-segment vocal stand-in with 10 ms fades so the
    // input itself is click-free; every discontinuity in the output is ours.
    const auto freqAt = [](const float t) {
        if (t < 0.62f) return 220.0f;
        if (t < 1.10f) return 261.63f;
        if (t < 1.60f) return 180.0f + 40.0f * (t - 1.10f) / 0.5f;   // glide
        return 220.0f;
    };
    const auto ampAt = [](const float t) {
        const auto fade = [](const float x) {   // 10 ms raised-cosine edge
            const float u = std::clamp(x / 0.010f, 0.0f, 1.0f);
            return 0.5f - 0.5f * std::cos(3.14159265f * u);
        };
        float a = 0.0f;
        if (t < 0.50f) a = fade(t) * fade(0.50f - t);
        else if (t >= 0.62f && t < 1.60f) a = fade(t - 0.62f) * fade(1.60f - t);
        else if (t >= 1.80f) a = fade(t - 1.80f) * fade(2.30f - t);
        return 0.25f * a;
    };
    std::vector<float> audio(static_cast<size_t>(n));
    double phase = 0.0;
    for (int i = 0; i < n; ++i) {
        const float t = static_cast<float>(i / sr);
        phase += kTwoPi * freqAt(t) / sr;
        float s = 0.0f;
        for (int h = 1; h <= 3; ++h)
            s += std::sin(static_cast<float>(phase) * h) / static_cast<float>(h);
        audio[static_cast<size_t>(i)] = ampAt(t) * s;
    }

    float inPeak = 0.0f, inMaxDiff = 0.0f;
    for (int i = 1; i < n; ++i) {
        inPeak = std::max(inPeak, std::abs(audio[static_cast<size_t>(i)]));
        inMaxDiff = std::max(inMaxDiff,
            std::abs(audio[static_cast<size_t>(i)] - audio[static_cast<size_t>(i - 1)]));
    }

    const struct { float shift; int block; } runs[] = {
        { 0.0f, 512 }, { 50.0f, 512 }, { 50.0f, 128 }, { -50.0f, 96 },
    };
    for (const auto& r : runs) {
        const float shift = r.shift;
        const auto out = runPsola(audio, sr, shift, r.block);
        bool finite = true;
        float outPeak = 0.0f, outMaxDiff = 0.0f;
        for (size_t i = 1; i < out.size(); ++i) {
            if (!std::isfinite(out[i]))
                finite = false;
            outPeak = std::max(outPeak, std::abs(out[i]));
            outMaxDiff = std::max(outMaxDiff, std::abs(out[i] - out[i - 1]));
        }
        const std::string tag = "shift " + std::to_string(shift) + "c block "
            + std::to_string(r.block) + ": ";
        check(finite, tag + "output finite");
        check(outPeak < 1.4f * inPeak,
              tag + "no level blowup (peak x" + std::to_string(outPeak / inPeak) + ")");
        check(outMaxDiff < 3.0f * inMaxDiff,
              tag + "no clicks (max diff x" + std::to_string(outMaxDiff / inMaxDiff) + ")");

        // Level sanity inside the second tone segment (0.75-1.05 s).
        double inSq = 0.0, outSq = 0.0;
        const int d = 600;
        for (int i = static_cast<int>(0.75 * sr); i < static_cast<int>(1.05 * sr); ++i) {
            inSq += audio[static_cast<size_t>(i)] * audio[static_cast<size_t>(i)];
            outSq += out[static_cast<size_t>(i + d)] * out[static_cast<size_t>(i + d)];
        }
        const float rmsDb = 10.0f * std::log10(static_cast<float>(outSq / std::max(inSq, 1e-12)));
        check(std::abs(rmsDb) < 3.5f,
              tag + "held-note level within 3.5 dB (" + std::to_string(rmsDb) + " dB)");
    }
}

// Vocal stand-in with the properties that break naive PSOLA on real voices:
// cycle-level pitch jitter, amplitude shimmer, vibrato, and breath noise.
std::vector<float> renderRealisticVoice(const double sr, const float seconds,
                                        const float baseHz, const unsigned seed) {
    std::mt19937 rng(seed);
    std::normal_distribution<float> gauss(0.0f, 1.0f);
    const int n = static_cast<int>(sr * seconds);
    std::vector<float> out(static_cast<size_t>(n));
    double phase = 0.0;
    float jitterCents = 0.0f, shimmerDb = 0.0f, breathLp = 0.0f;
    for (int i = 0; i < n; ++i) {
        const float t = static_cast<float>(i / sr);
        jitterCents = 0.999f * jitterCents + 0.06f * gauss(rng);
        jitterCents = std::clamp(jitterCents, -10.0f, 10.0f);
        const float cents = jitterCents + 20.0f * std::sin(kTwoPi * 4.5f * t);
        phase += kTwoPi * baseHz * std::exp2(cents / 1200.0f) / sr;
        shimmerDb = 0.9995f * shimmerDb + 0.01f * gauss(rng);
        shimmerDb = std::clamp(shimmerDb, -1.5f, 1.5f);
        float s = 0.0f;
        for (int h = 1; h <= 3; ++h)
            s += std::sin(static_cast<float>(phase) * h) / static_cast<float>(h);
        breathLp += 0.05f * (0.5f * gauss(rng) - breathLp);
        out[static_cast<size_t>(i)] =
            0.22f * s * std::pow(10.0f, shimmerDb / 20.0f) + 0.004f * breathLp;
    }
    return out;
}

// Bubble metric: modulation depth of the 10 ms RMS envelope. Grain-schedule
// irregularities read as envelope modulation at 5-50 Hz.
float envelopeModulationIndex(const std::vector<float>& x, const double sr,
                              const float fromSec, const float toSec) {
    const int win = static_cast<int>(0.010 * sr);
    std::vector<float> rms;
    for (int start = static_cast<int>(fromSec * sr);
         start + win < static_cast<int>(toSec * sr)
             && start + win < static_cast<int>(x.size()); start += win) {
        double sq = 0.0;
        for (int i = 0; i < win; ++i)
            sq += x[static_cast<size_t>(start + i)] * x[static_cast<size_t>(start + i)];
        rms.push_back(static_cast<float>(std::sqrt(sq / win)));
    }
    if (rms.size() < 8)
        return 0.0f;
    double mean = 0.0;
    for (const float v : rms)
        mean += v;
    mean /= static_cast<double>(rms.size());
    double var = 0.0;
    for (const float v : rms)
        var += (v - mean) * (v - mean);
    return mean > 1.0e-9
        ? static_cast<float>(std::sqrt(var / rms.size()) / mean) : 0.0f;
}

// High-harmonic preservation on a formant-rich voice, at 157 Hz (the
// fundamental measured on the user-reported "bubbles" vocal). A fixed 900 Hz
// epoch-search low-pass still passes ~6 harmonics at this pitch, so a
// vowel's formant structure gives the peak search multiple similar-height
// candidates per period; picking a different one cycle-to-cycle is a small
// epoch position error that is a large phase error at high harmonics on
// overlap-add. Regression guard for that mechanism (build spec F4 round 4).
void testPsolaHighHarmonicPreservation() {
    std::printf("PSOLA high-harmonic preservation (157 Hz formant-rich voice):\n");
    const double sr = 44100.0;
    const float f0 = 157.0f;
    const int harmonics = 18;
    std::vector<float> audio(static_cast<size_t>(sr * 1.0));
    double phase = 0.0;
    for (size_t i = 0; i < audio.size(); ++i) {
        phase += kTwoPi * f0 / sr;
        float s = 0.0f;
        for (int h = 1; h <= harmonics; ++h) {
            const float hz = f0 * static_cast<float>(h);
            // Two formant bumps (roughly /a/-ish) so a single period has
            // more than one locally-strong lobe, like a real vowel.
            const float env = std::exp(-0.5f * std::pow((hz - 800.0f) / 300.0f, 2.0f))
                            + 0.7f * std::exp(-0.5f * std::pow((hz - 1900.0f) / 400.0f, 2.0f))
                            + 0.05f;
            s += env * std::sin(static_cast<float>(phase) * h);
        }
        audio[i] = 0.15f * s;
    }

    const auto out = runPsola(audio, sr, 40.0f);   // realistic correction-sized shift

    const auto spectrumPeak = [&](const std::vector<float>& x, const float hz) {
        const int n = 4096;
        const size_t start = x.size() > static_cast<size_t>(n) + 4000
            ? x.size() - static_cast<size_t>(n) - 2000 : 0;
        double re = 0.0, im = 0.0;
        for (int i = 0; i < n; ++i) {
            const double w = 0.5 * (1.0 - std::cos(kTwoPi * i / n));
            const double v = w * x[start + static_cast<size_t>(i)];
            re += v * std::cos(kTwoPi * hz * i / sr);
            im += v * std::sin(kTwoPi * hz * i / sr);
        }
        return std::sqrt(re * re + im * im);
    };

    const float ratio = std::exp2(40.0f / 1200.0f);   // matches the shift above
    float worstLossDb = 0.0f;
    for (int h = 8; h <= 16; ++h) {
        const float hz = f0 * static_cast<float>(h);
        const float dryMag = spectrumPeak(audio, hz);
        const float wetMag = spectrumPeak(out, hz * ratio);   // harmonics moved with the shift
        const float lossDb = 20.0f * std::log10((wetMag + 1e-9f) / (dryMag + 1e-9f));
        worstLossDb = std::min(worstLossDb, lossDb);
    }
    check(worstLossDb > -2.0f,
          "harmonics 8-16 lose <2 dB from grain phase error (worst "
              + std::to_string(worstLossDb) + " dB)");
}

// ---- Correction-path tests: detector -> decomposition -> engine -> shifter,
// with the output pitch measured by a second, independent detector pass.

struct CorrectionRun {
    std::vector<float> output;
    float maxCorrectionStepCents = 0.0f;   // largest frame-to-frame change
    float maxAbsCorrectionCents = 0.0f;    // largest |correction| reached
    int latencySamples = 0;
};

CorrectionRun runCorrectionChain(const std::vector<float>& audio, const double sr,
                                 const float amount, const float natural,
                                 const std::uint16_t scaleMask =
                                     CorrectionEngine::kChromaticMask) {
    PitchDetector detector;
    detector.prepare(sr, PitchDetector::Config {});
    PerformanceDecomposition decomposition;
    decomposition.prepare(sr / detector.hopSamples(), PerformanceDecomposition::Config {});
    CorrectionEngine engine;
    engine.prepare(sr / detector.hopSamples(), CorrectionEngine::Config {});
    PsolaShifter shifter;
    shifter.prepare(sr, 512, 1);

    CorrectionRun run;
    run.output = audio;
    run.latencySamples = shifter.latencySamples();

    PitchObservation obs[64];
    float previousCorrection = 0.0f;
    const int block = 512;
    for (size_t offset = 0; offset < audio.size(); offset += block) {
        const int n = static_cast<int>(std::min<size_t>(block, audio.size() - offset));
        const int produced = detector.process(audio.data() + offset, n, obs, 64);
        for (int i = 0; i < produced; ++i) {
            const auto& o = obs[i];
            shifter.setPeriodHint(
                o.f0Hz.value > 0.0f ? static_cast<float>(sr) / o.f0Hz.value : 0.0f,
                o.f0Hz.confidence);
            const float c = engine.process(decomposition.process(o), amount, natural, scaleMask);
            run.maxCorrectionStepCents =
                std::max(run.maxCorrectionStepCents, std::abs(c - previousCorrection));
            run.maxAbsCorrectionCents = std::max(run.maxAbsCorrectionCents, std::abs(c));
            previousCorrection = c;
        }
        shifter.setShiftCents(engine.currentCorrectionCents());
        float* chans[1] = { run.output.data() + offset };
        shifter.process(chans, 1, n);
    }
    return run;
}

float medianCentsAfter(const std::vector<float>& audio, const double sr,
                       const float skipSeconds) {
    const auto frames = settled(analyse(audio, sr),
                                static_cast<size_t>(skipSeconds * sr / 256.0));
    return frames.empty() ? -10000.0f : medianDetectedCents(frames);
}

void testCorrectsSharpNote() {
    std::printf("Correction pulls a sharp held note to pitch (A3 +30c):\n");
    const double sr = 48000.0;
    const float inputHz = 220.0f * std::exp2(30.0f / 1200.0f);
    const auto audio = renderTone(sr, 3.5f, 0.25f, 3,
                                  [inputHz](float) { return inputHz; });

    const float inputCents = medianCentsAfter(audio, sr, 0.5f) - hzToCents(220.0f);
    check(std::abs(inputCents - 30.0f) < 3.0f,
          "input measures +30c sharp (got " + std::to_string(inputCents) + "c)");

    const auto full = runCorrectionChain(audio, sr, 1.0f, 0.5f);
    const float fullCents = medianCentsAfter(full.output, sr, 0.8f) - hzToCents(220.0f);
    check(std::abs(fullCents) < 8.0f,
          "amount=1 output within 8c of A3 (got " + std::to_string(fullCents) + "c)");

    const auto half = runCorrectionChain(audio, sr, 0.5f, 0.5f);
    const float halfCents = medianCentsAfter(half.output, sr, 0.8f) - hzToCents(220.0f);
    check(std::abs(halfCents - 15.0f) < 6.0f,
          "amount=0.5 removes about half the error (got " + std::to_string(halfCents) + "c)");

    const auto off = runCorrectionChain(audio, sr, 0.0f, 0.5f);
    const float offCents = medianCentsAfter(off.output, sr, 0.8f) - hzToCents(220.0f);
    check(std::abs(offCents - 30.0f) < 3.0f,
          "amount=0 leaves the note untouched (got " + std::to_string(offCents) + "c)");

    // No-jump proof: correction trajectory bounded by the engine slew
    // (600 c/s max at frame rate 187.5 -> 3.2 c/frame; allow margin).
    check(full.maxCorrectionStepCents < 4.0f,
          "correction curve never jumps (max step "
              + std::to_string(full.maxCorrectionStepCents) + "c/frame)");

    // Target-stability regression (F3 estimator): a held steady note must
    // not just converge once and then wander back off - it caught a real
    // bug where the target estimator's candidate pool used an absolute
    // score-floor prune that evicted the correctly-converged (and
    // therefore very negative log-score) target, letting a weaker,
    // freshly-reset candidate win for a few frames before it converged
    // back. Compare two well-separated, both-converged points.
    const float midCents = medianCentsAfter(full.output, sr, 1.0f) - hzToCents(220.0f);
    const float lateCents = medianCentsAfter(full.output, sr, 3.0f) - hzToCents(220.0f);
    check(std::abs(midCents - lateCents) < 3.0f,
          "converged correction stays put, no wander (mid "
              + std::to_string(midCents) + "c, late " + std::to_string(lateCents) + "c)");
}

void testIgnoresBriefFastLeap() {
    std::printf("Correction ignores a brief fast leap, not a held note (A3, "
                "60ms leap to A3+1200c):\n");
    // Real vocal material can have fast runs/ad-libs/register leaps that
    // pass through or near a chromatic note for only a handful of frames -
    // measured directly on a real track (dry vs. a reference corrector's
    // output vs. VXTune's) where those genuine leaps read almost identically
    // in both, but VXTune's correction was chasing them as if they were
    // held notes worth correcting to, producing audible mis-tracking. A
    // target must be the estimator's top pick for several consecutive
    // frames before correction engages on it (see CorrectionEngine's
    // targetHoldFrames).
    const double sr = 48000.0;
    const float baseHz = 220.0f;
    const float leapHz = baseHz * std::exp2(1200.0f / 1200.0f);   // one octave up
    const auto audio = renderTone(sr, 2.0f, 0.25f, 3, [&](const float t) {
        return (t >= 0.9f && t < 0.96f) ? leapHz : baseHz;   // 60 ms leap
    });

    const auto run = runCorrectionChain(audio, sr, 1.0f, 0.5f);

    // Sampled well after the leap has passed: correction must not have been
    // pulled toward the leap and must not still be unwinding from it.
    const float afterCents = medianCentsAfter(run.output, sr, 1.3f) - hzToCents(baseHz);
    check(std::abs(afterCents) < 3.0f,
          "held note unaffected by the brief leap (got "
              + std::to_string(afterCents) + "c)");
}

void testDivergenceGuardStopsChasingAGlide() {
    std::printf("Correction gives up chasing a sustained glide rather than "
                "fighting it (A3 held, then a 400ms glide up 4 semitones):\n");
    // Reproduces a real bug found on real material: a held note gives the
    // target estimator a strong, sticky evidence lock (by design - that is
    // what target-lock persistence is for); when the singer then glides
    // continuously away from it, the OLD lock can outlast the glide's own
    // duration, so the engine keeps computing an ever-growing error against
    // a target the singer has already left. Measured directly: a real
    // ~400ms glide produced a correction smoothly ramping to -73c, actively
    // pitch-bending the singer's real, correct movement in the wrong
    // direction. The divergence guard (CorrectionEngine) must give up on a
    // target once the error is clearly growing rather than closing, long
    // before maxErrorCents would eventually catch it.
    const double sr = 48000.0;
    const auto audio = renderTone(sr, 2.5f, 0.25f, 3, [](const float t) {
        if (t < 1.0f) return 220.0f;                              // sustain A3
        if (t < 1.4f) {                                            // 400ms glide
            const float cents = 400.0f * ((t - 1.0f) / 0.4f);
            return 220.0f * std::exp2(cents / 1200.0f);
        }
        return 220.0f * std::exp2(400.0f / 1200.0f);               // land + sustain
    });

    const auto run = runCorrectionChain(audio, sr, 1.0f, 0.5f);
    // 60c, not near-zero: the estimator fix (score floor + in-range bonus,
    // see VxTuneTargetEstimator.h) cut recovery from ~1s (a genuine,
    // severe bug - stale evidence from the held note outweighed fresh,
    // perfect evidence for the new one) down to ~50ms, but a real 400c/
    // 400ms glide still has a brief, bounded correction transient while
    // decomposition's centre line and the estimator both catch up - that
    // is expected lag, not a regression.
    check(run.maxAbsCorrectionCents < 60.0f,
          "correction transient during the glide stays bounded (got "
              + std::to_string(run.maxAbsCorrectionCents) + "c)");
}

void testDoesNotTouchInTune() {
    std::printf("Do-nothing proof (in-tune A3, amount=1):\n");
    const double sr = 48000.0;
    const auto audio = renderTone(sr, 2.0f, 0.25f, 3, [](float) { return 220.0f; });
    const auto run = runCorrectionChain(audio, sr, 1.0f, 0.5f);

    const float outCents = medianCentsAfter(run.output, sr, 0.8f) - hzToCents(220.0f);
    check(std::abs(outCents) < 3.0f,
          "in-tune pitch unchanged (got " + std::to_string(outCents) + "c)");

    // Parked shifter must be a pure delay: compare output against the
    // latency-shifted input from 0.5 s onward.
    double num = 0.0, den = 0.0;
    for (size_t i = 24000; i + static_cast<size_t>(run.latencySamples) < audio.size(); ++i) {
        const float dry = audio[i];
        const float wet = run.output[i + static_cast<size_t>(run.latencySamples)];
        num += (wet - dry) * (wet - dry);
        den += dry * dry;
    }
    const float deltaDb = den > 0.0 ? 10.0f * std::log10(static_cast<float>(num / den))
                                    : -200.0f;
    check(deltaDb < -60.0f,
          "zero correction is a pure aligned delay (delta "
              + std::to_string(deltaDb) + " dB)");
}

void testPsolaRealisticVoiceStability() {
    std::printf("PSOLA realistic-voice stability (jitter/shimmer/vibrato/breath):\n");
    const double sr = 48000.0;

    // Shifted render must not add significant envelope modulation (bubble).
    {
        const auto voice = renderRealisticVoice(sr, 3.0f, 220.0f, 42);
        const float inMod = envelopeModulationIndex(voice, sr, 1.0f, 2.8f);
        const auto out = runPsola(voice, sr, 30.0f);
        const float outMod = envelopeModulationIndex(out, sr, 1.0f, 2.8f);
        check(outMod < inMod * 1.4f + 0.02f,
              "shifted render adds no bubble (in " + std::to_string(inMod)
                  + ", out " + std::to_string(outMod) + ")");
    }

    // Full correction chain on a sharp realistic voice: same bound, plus the
    // correction itself must still land.
    {
        const float sharpHz = 220.0f * std::exp2(30.0f / 1200.0f);
        const auto voice = renderRealisticVoice(sr, 3.0f, sharpHz, 7);
        const float inMod = envelopeModulationIndex(voice, sr, 1.0f, 2.8f);
        const auto run = runCorrectionChain(voice, sr, 1.0f, 0.5f);
        const float outMod = envelopeModulationIndex(run.output, sr, 1.0f, 2.8f);
        check(outMod < inMod * 1.4f + 0.02f,
              "corrected render adds no bubble (in " + std::to_string(inMod)
                  + ", out " + std::to_string(outMod) + ")");
        const float outCents = medianCentsAfter(run.output, sr, 1.0f) - hzToCents(220.0f);
        check(std::abs(outCents) < 12.0f,
              "correction still lands near A3 (got " + std::to_string(outCents) + "c)");
    }
}

// Runs detector -> decomposition -> engine on `audio` and returns the
// dominant behaviour + its probability at the LAST analysed frame -
// i.e. after the whole clip has been seen, matching how a real-time
// listener would judge "what does this current moment look like".
struct BehaviourResult {
    Behaviour dominant = Behaviour::unvoiced;
    float probability = 0.0f;
    float maxFrameToFrameJump = 0.0f;   // distribution-continuity check
};

BehaviourResult analyseBehaviour(const std::vector<float>& audio, const double sr) {
    PitchDetector detector;
    detector.prepare(sr, PitchDetector::Config {});
    PerformanceDecomposition decomposition;
    decomposition.prepare(sr / detector.hopSamples(), PerformanceDecomposition::Config {});
    CorrectionEngine engine;
    engine.prepare(sr / detector.hopSamples(), CorrectionEngine::Config {});

    BehaviourResult result;
    float previous[static_cast<int>(Behaviour::count)] = {};
    bool havePrevious = false;
    PitchObservation obs[64];
    const int block = 512;
    for (size_t offset = 0; offset < audio.size(); offset += block) {
        const int n = static_cast<int>(std::min<size_t>(block, audio.size() - offset));
        const int produced = detector.process(audio.data() + offset, n, obs, 64);
        for (int i = 0; i < produced; ++i) {
            const auto frame = decomposition.process(obs[i]);
            engine.process(frame, 1.0f, 0.5f);
            const auto& seg = engine.currentSegment();

            float jump = 0.0f;
            if (havePrevious) {
                for (int b = 0; b < static_cast<int>(Behaviour::count); ++b)
                    jump += std::abs(seg.behaviourProb[b] - previous[b]);
            }
            result.maxFrameToFrameJump = std::max(result.maxFrameToFrameJump, jump);
            for (int b = 0; b < static_cast<int>(Behaviour::count); ++b)
                previous[b] = seg.behaviourProb[b];
            havePrevious = true;

            int best = 0;
            for (int b = 1; b < static_cast<int>(Behaviour::count); ++b)
                if (seg.behaviourProb[b] > seg.behaviourProb[best])
                    best = b;
            result.dominant = static_cast<Behaviour>(best);
            result.probability = seg.behaviourProb[best];
        }
    }
    return result;
}

void testBehaviourDistribution() {
    std::printf("Behaviour distribution (F2 segmenter) classifies gesture type:\n");
    const double sr = 48000.0;

    // Sustain: long held steady tone.
    {
        const auto audio = renderTone(sr, 1.5f, 0.25f, 3, [](float) { return 220.0f; });
        const auto r = analyseBehaviour(audio, sr);
        check(r.dominant == Behaviour::sustain && r.probability > 0.5f,
              "steady held tone reads as sustain (got behaviour index "
                  + std::to_string(static_cast<int>(r.dominant))
                  + " p=" + std::to_string(r.probability) + ")");
    }

    // Vibrato: steady centre with a clean 5.5 Hz +/-40c modulation.
    {
        const auto audio = renderTone(sr, 1.5f, 0.25f, 3, [](const float t) {
            const float vibrato = 40.0f * std::sin(kTwoPi * 5.5f * t);
            return 220.0f * std::exp2(vibrato / 1200.0f);
        });
        const auto r = analyseBehaviour(audio, sr);
        check(r.dominant == Behaviour::vibrato && r.probability > 0.5f,
              "5.5 Hz +/-40c modulation reads as vibrato (got behaviour index "
                  + std::to_string(static_cast<int>(r.dominant))
                  + " p=" + std::to_string(r.probability) + ")");
    }

    // Slide: a continuous glide from one note toward another, evaluated
    // mid-glide (not after it has settled).
    {
        const auto audio = renderTone(sr, 1.0f, 0.25f, 3, [](const float t) {
            const float cents = 400.0f * (t / 1.0f);   // ramps 0 -> 400c over 1s
            return 220.0f * std::exp2(cents / 1200.0f);
        });
        const auto r = analyseBehaviour(audio, sr);
        check(r.dominant == Behaviour::slide || r.dominant == Behaviour::bend,
              "continuous glide reads as slide/bend, not sustain (got behaviour index "
                  + std::to_string(static_cast<int>(r.dominant)) + ")");
    }

    // Distribution continuity (rule 5): no hard flip between adjacent
    // frames, even across the segmenter's own boundary transitions.
    {
        const auto audio = renderTone(sr, 2.0f, 0.25f, 3, [](const float t) {
            return (t >= 0.9f && t < 0.96f)
                ? 220.0f * std::exp2(1200.0f / 1200.0f)
                : 220.0f;
        });
        const auto r = analyseBehaviour(audio, sr);
        check(r.maxFrameToFrameJump < 1.5f,
              "behaviour distribution never flips hard frame-to-frame (max L1 jump "
                  + std::to_string(r.maxFrameToFrameJump) + ")");
    }
}

void testScaleAwareTargets() {
    std::printf("Key/Scale changes the target (F4+55c, C Major vs Chromatic):\n");
    const double sr = 48000.0;
    const float inputHz = 349.23f * std::exp2(55.0f / 1200.0f);   // F4 + 55c
    const auto audio = renderTone(sr, 2.0f, 0.25f, 3,
                                  [inputHz](float) { return inputHz; });

    // Chromatic: nearest note is F#4 (-45c away) -> pulled UP to F#4 (-300c).
    const auto chromatic = runCorrectionChain(audio, sr, 1.0f, 0.5f);
    const float chromaticCents = medianCentsAfter(chromatic.output, sr, 0.8f);
    check(std::abs(chromaticCents - (-300.0f)) < 8.0f,
          "chromatic pulls to F#4 (got " + std::to_string(chromaticCents) + "c)");

    // C Major (no F#): nearest allowed note is F4 -> pulled DOWN to F4 (-400c).
    constexpr std::uint16_t cMajor = 0x0AB5;   // C D E F G A B
    const auto scaled = runCorrectionChain(audio, sr, 1.0f, 0.5f, cMajor);
    const float scaledCents = medianCentsAfter(scaled.output, sr, 0.8f);
    check(std::abs(scaledCents - (-400.0f)) < 8.0f,
          "C Major pulls to F4 (got " + std::to_string(scaledCents) + "c)");
}

void testVibratoSurvivesCorrection() {
    std::printf("Vibrato survives correction (A3 +30c centre, +/-50c @ 5.5 Hz):\n");
    const double sr = 48000.0;
    const auto audio = renderTone(sr, 3.0f, 0.25f, 3, [](const float t) {
        const float cents = 30.0f + 50.0f * std::sin(kTwoPi * 5.5f * t);
        return 220.0f * std::exp2(cents / 1200.0f);
    });
    const auto run = runCorrectionChain(audio, sr, 1.0f, 0.5f);

    const auto inFrames = settled(analyse(audio, sr), 150);
    const auto outFrames = settled(analyse(run.output, sr), 150);
    check(!inFrames.empty() && !outFrames.empty(), "produced frames in and out");
    if (inFrames.empty() || outFrames.empty())
        return;

    const auto residualExtent = [](const std::vector<PitchFrame>& frames) {
        float lo = 1.0e9f, hi = -1.0e9f;
        for (const auto& f : frames) {
            lo = std::min(lo, f.residualCents);
            hi = std::max(hi, f.residualCents);
        }
        return hi - lo;
    };
    const auto centreMedian = [](std::vector<PitchFrame> frames) {
        std::vector<float> c;
        for (const auto& f : frames)
            c.push_back(f.centreCents);
        std::sort(c.begin(), c.end());
        return c[c.size() / 2];
    };

    const float outCentre = centreMedian(outFrames) - hzToCents(220.0f);
    check(std::abs(outCentre) < 10.0f,
          "corrected centre sits on A3 (got " + std::to_string(outCentre) + "c)");

    const float inExtent = residualExtent(inFrames);
    const float outExtent = residualExtent(outFrames);
    check(outExtent > 0.75f * inExtent,
          "vibrato extent preserved through correction (in "
              + std::to_string(inExtent) + "c, out " + std::to_string(outExtent) + "c)");
}

} // namespace

int main() {
    std::printf("VX Tune analysis tests\n");

    std::printf("Steady sine A3 @48k:\n");
    testSteadyTone(48000.0, 220.0f, 1, 3.0f, "sine 220");
    std::printf("Steady sine A3 @44.1k:\n");
    testSteadyTone(44100.0, 220.0f, 1, 3.0f, "sine 220 @44.1k");
    std::printf("Steady sine A3 @96k:\n");
    testSteadyTone(96000.0, 220.0f, 1, 3.0f, "sine 220 @96k");
    std::printf("Low male E2 sine @48k:\n");
    testSteadyTone(48000.0, 82.41f, 1, 5.0f, "sine 82.41");
    std::printf("Harmonic-rich G3 (octave trap) @48k:\n");
    testSteadyTone(48000.0, 196.0f, 10, 5.0f, "saw-ish 196");

    testSilence();
    testNoise();
    testVibrato();
    testNoteChangeSnap();

    testPsolaRatioAccuracy();
    testPsolaFormantPreservation();
    testPsolaParkAndUnvoiced();
    testPsolaDoesNotSmearVoiceIntoGaps();
    testPsolaTortureTransitions();
    testPsolaRealisticVoiceStability();
    testPsolaHighHarmonicPreservation();

    testCorrectsSharpNote();
    testIgnoresBriefFastLeap();
    testDivergenceGuardStopsChasingAGlide();
    testDoesNotTouchInTune();
    testBehaviourDistribution();
    testScaleAwareTargets();
    testVibratoSurvivesCorrection();

    if (failures == 0) {
        std::printf("All VX Tune analysis tests passed\n");
        return 0;
    }
    std::printf("%d VX Tune analysis test(s) FAILED\n", failures);
    return 1;
}
