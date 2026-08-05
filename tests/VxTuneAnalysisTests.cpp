// VX Tune Phase 1 analysis tests: pitch detector + performance decomposition.
// Pure DSP, no JUCE. Contracts under test come from
// docs/Task Based/VXTUNE_VISION_ARCHITECTURE.md §5.1, §5.2, §10.

#include "../Source/vxstudio/products/tune/dsp/VxTunePitchDetector.h"
#include "../Source/vxstudio/products/tune/dsp/VxTuneDecomposition.h"
#include "../Source/vxstudio/products/tune/dsp/VxTuneCorrectionEngine.h"
#include "../Source/vxstudio/products/tune/dsp/VxTunePitchRenderer.h"

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
using vxsuite::tune::VxTunePitchRenderer;
using vxsuite::tune::createPitchRenderer;

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

// ---- Correction-path tests: detector -> decomposition -> engine -> renderer,
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
    auto renderer = createPitchRenderer(VxTunePitchRenderer::Backend::Signalsmith);
    renderer->prepare(VxTunePitchRenderer::PrepareSpec { sr, 512, 1 });

    CorrectionRun run;
    run.output.assign(audio.size(), 0.0f);
    run.latencySamples = renderer->latencySamples();

    PitchObservation obs[64];
    float previousCorrection = 0.0f;
    float blockStartCents = 0.0f;
    float blockEndCents = 0.0f;
    const int block = 512;
    for (size_t offset = 0; offset < audio.size(); offset += block) {
        const int n = static_cast<int>(std::min<size_t>(block, audio.size() - offset));
        const int produced = detector.process(audio.data() + offset, n, obs, 64);
        for (int i = 0; i < produced; ++i) {
            const auto frame = decomposition.process(obs[i]);
            const float c = engine.process(frame, amount, natural, scaleMask);
            renderer->setFrameInfo(VxTunePitchRenderer::FrameInfo {
                c,
                frame.f0Hz.value,
                frame.f0Hz.confidence,
                frame.f0Hz.value > 0.0f,
            });
            run.maxCorrectionStepCents =
                std::max(run.maxCorrectionStepCents, std::abs(c - previousCorrection));
            run.maxAbsCorrectionCents = std::max(run.maxAbsCorrectionCents, std::abs(c));
            previousCorrection = c;
            blockEndCents = c;
        }
        const float* inChans[1] = { audio.data() + offset };
        float* outChans[1] = { run.output.data() + offset };
        renderer->process(inChans, outChans, 1, n, blockStartCents, blockEndCents);
        blockStartCents = blockEndCents;
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

    const auto full = runCorrectionChain(audio, sr, 0.5f, 0.5f);
    const float fullCents = medianCentsAfter(full.output, sr, 0.8f) - hzToCents(220.0f);
    check(std::abs(fullCents) < 8.0f,
          "amount=0.5 output within 8c of A3 (got " + std::to_string(fullCents) + "c)");

    const auto half = runCorrectionChain(audio, sr, 0.25f, 0.5f);
    const float halfCents = medianCentsAfter(half.output, sr, 0.8f) - hzToCents(220.0f);
    check(std::abs(halfCents - 15.0f) < 6.0f,
          "amount=0.25 removes about half the error (got " + std::to_string(halfCents) + "c)");

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

PitchFrame syntheticFrame(const float hz, const float offsetCents,
                          const float confidence,
                          const EstimateReason reason = EstimateReason::nominal) {
    PitchFrame frame;
    frame.f0Hz.value = hz * std::exp2(offsetCents / 1200.0f);
    frame.f0Hz.confidence = confidence;
    frame.f0Hz.reason = reason;
    frame.f0Hz.stability = reason == EstimateReason::nominal ? 1.0f : 0.3f;
    frame.voicedProb = confidence;
    frame.centreHz = frame.f0Hz.value;
    frame.centreCents = hzToCents(frame.centreHz);
    frame.decompConfidence = confidence;
    frame.levelDb = -18.0f;
    return frame;
}

float runDirectEngine(const float offsetCents, const float confidence,
                      const EstimateReason reason, const int frames,
                      const float natural = 0.5f) {
    constexpr double frameRate = 48000.0 / 256.0;
    CorrectionEngine engine;
    engine.prepare(frameRate, CorrectionEngine::Config {});
    float correction = 0.0f;
    for (int i = 0; i < frames; ++i) {
        auto frame = syntheticFrame(220.0f, offsetCents, confidence, reason);
        frame.timeSamples = static_cast<std::int64_t>(i * 256);
        correction = engine.process(frame, 1.0f, natural);
    }
    return correction;
}

void testMusicalAuthorityProtectsUnstableFrames() {
    std::printf("Musical authority backs off uncertain/onset frames:\n");

    const float settled = std::abs(runDirectEngine(35.0f, 0.95f,
                                                   EstimateReason::nominal, 140));
    check(settled > 24.0f,
          "confident held note still earns strong correction (got "
              + std::to_string(settled) + "c)");

    const float onset = std::abs(runDirectEngine(35.0f, 0.95f,
                                                 EstimateReason::onsetTransient, 24));
    check(onset < settled * 0.45f,
          "onset transient is protected from hard correction (onset "
              + std::to_string(onset) + "c vs settled "
              + std::to_string(settled) + "c)");

    const float lowConfidence = std::abs(runDirectEngine(35.0f, 0.45f,
                                                        EstimateReason::sparseEvidence, 140));
    check(lowConfidence < settled * 0.65f,
          "low-confidence frames receive less correction authority (low-confidence "
              + std::to_string(lowConfidence) + "c vs settled "
              + std::to_string(settled) + "c)");
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
    const auto chromatic = runCorrectionChain(audio, sr, 0.5f, 0.5f);
    const float chromaticCents = medianCentsAfter(chromatic.output, sr, 0.8f);
    check(std::abs(chromaticCents - (-300.0f)) < 8.0f,
          "chromatic pulls to F#4 (got " + std::to_string(chromaticCents) + "c)");

    // C Major (no F#): nearest allowed note is F4 -> pulled DOWN to F4 (-400c).
    constexpr std::uint16_t cMajor = 0x0AB5;   // C D E F G A B
    const auto scaled = runCorrectionChain(audio, sr, 0.5f, 0.5f, cMajor);
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
    const auto run = runCorrectionChain(audio, sr, 0.5f, 0.5f);

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

    testCorrectsSharpNote();
    testIgnoresBriefFastLeap();
    testDivergenceGuardStopsChasingAGlide();
    testDoesNotTouchInTune();
    testMusicalAuthorityProtectsUnstableFrames();
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
