#pragma once

#include <array>
#include <vector>

#include <juce_dsp/juce_dsp.h>

namespace vxsuite::tune {

// Extracts a 12-bin pitch-class energy histogram ("chroma") from a mono
// audio stream via a sliding-window FFT, so a sidechain instrumental can
// contribute harmonic evidence to VX Tune's auto key/scale detector
// (VXTuneAudioProcessor::updateAutoKeyEstimate) - the exact same histogram
// + Krumhansl-Schmuckler profile match the vocal itself already feeds, just
// from a source that carries full chord content instead of one note at a
// time. Deliberately not a new fusion path into TargetEstimator/
// CorrectionEngine: this project's own history (see VxTuneTargetEstimator.h)
// shows that file is where well-intentioned scoring additions have caused
// real regressions, so sidechain evidence rides the already-tested
// histogram/hysteresis/correlation machinery instead of adding a new one.
//
// Realtime-safe: fixed-size ring buffer and FFT workspace, no allocation
// after prepare().
class HarmonicContextAnalyzer {
public:
    void prepare(double sampleRate, int maxBlockSize) noexcept;
    void reset() noexcept;

    // Feeds new mono samples (any block size, including hosts' very large
    // offline blocks). Runs an FFT internally every kHopSize new samples
    // (75% overlap); harmless to call with a block smaller than one hop -
    // it just accumulates until the next hop boundary.
    void process(const float* mono, int numSamples) noexcept;

    // Most recently analysed chroma, normalised to sum to 1. Meaningless
    // (all-zero) until at least one analysis window has completed.
    const std::array<float, 12>& chroma() const noexcept { return chromaOut; }

    // 0..1, RMS-based: how much real signal was in the last analysed
    // window. Callers should gate/scale their use of chroma() by this -
    // a near-silent window's chroma is measurement noise, not harmony.
    float presence() const noexcept { return presenceOut; }

private:
    static constexpr int kFftOrder = 11;             // 2048-point FFT
    static constexpr int kFftSize = 1 << kFftOrder;
    static constexpr int kHopSize = kFftSize / 4;    // 75% overlap

    void runAnalysis() noexcept;

    juce::dsp::FFT fft { kFftOrder };
    juce::dsp::WindowingFunction<float> window {
        static_cast<size_t>(kFftSize), juce::dsp::WindowingFunction<float>::hann
    };

    // Circular ring holding the most recent kFftSize samples.
    std::vector<float> ring;
    int ringWrite = 0;
    int samplesSinceAnalysis = 0;

    // Scratch reused every analysis (assembled time-order frame, then FFT
    // workspace: interleaved complex, 2x size per JUCE's FFT contract).
    std::vector<float> frameScratch;
    std::vector<float> fftScratch;

    // Precomputed per prepare() (depends on sample rate): pitch class for
    // each FFT bin, or -1 if the bin's frequency is outside the trusted
    // range (sub-bass rumble / above where fundamentals are meaningfully
    // resolvable bin-to-bin at this FFT size).
    std::vector<int> binPitchClass;

    double sr = 48000.0;
    std::array<float, 12> chromaOut {};
    float presenceOut = 0.0f;
};

} // namespace vxsuite::tune
