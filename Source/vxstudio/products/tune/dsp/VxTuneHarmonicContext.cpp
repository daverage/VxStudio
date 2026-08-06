#include "VxTuneHarmonicContext.h"

#include <cmath>

namespace vxsuite::tune {

namespace {
// Below E1 (~41Hz) is sub-bass rumble/DC leakage, not a chord tone. Above
// ~4kHz a 2048-point FFT's bin spacing is wide enough in musical terms
// (and fundamentals that high are rare in instrumental harmony) that
// mapping it to a single pitch class adds more noise than signal.
constexpr float kMinTrustedHz = 41.0f;
constexpr float kMaxTrustedHz = 4000.0f;

int pitchClassForHz(const float hz) noexcept {
    const float midi = 69.0f + 12.0f * std::log2(hz / 440.0f);
    const int rounded = static_cast<int>(std::lround(midi));
    return ((rounded % 12) + 12) % 12;
}
} // namespace

void HarmonicContextAnalyzer::prepare(const double sampleRate, const int maxBlockSize) noexcept {
    sr = sampleRate > 1000.0 ? sampleRate : 48000.0;
    (void) maxBlockSize;

    ring.assign(static_cast<size_t>(kFftSize), 0.0f);
    frameScratch.assign(static_cast<size_t>(kFftSize), 0.0f);
    fftScratch.assign(static_cast<size_t>(kFftSize) * 2, 0.0f);

    binPitchClass.assign(static_cast<size_t>(kFftSize / 2 + 1), -1);
    for (int bin = 0; bin < kFftSize / 2 + 1; ++bin) {
        const float hz = static_cast<float>(bin) * static_cast<float>(sr) / static_cast<float>(kFftSize);
        if (hz < kMinTrustedHz || hz > kMaxTrustedHz)
            continue;
        binPitchClass[static_cast<size_t>(bin)] = pitchClassForHz(hz);
    }

    reset();
}

void HarmonicContextAnalyzer::reset() noexcept {
    std::fill(ring.begin(), ring.end(), 0.0f);
    ringWrite = 0;
    samplesSinceAnalysis = 0;
    chromaOut.fill(0.0f);
    presenceOut = 0.0f;
}

void HarmonicContextAnalyzer::process(const float* mono, const int numSamples) noexcept {
    if (ring.empty() || mono == nullptr)
        return;
    for (int i = 0; i < numSamples; ++i) {
        ring[static_cast<size_t>(ringWrite)] = mono[i];
        ringWrite = (ringWrite + 1) % kFftSize;
        if (++samplesSinceAnalysis >= kHopSize) {
            samplesSinceAnalysis = 0;
            runAnalysis();
        }
    }
}

void HarmonicContextAnalyzer::runAnalysis() noexcept {
    // Unwrap the ring into time order (oldest first) so the window function
    // is applied correctly regardless of where the write pointer currently
    // sits.
    for (int i = 0; i < kFftSize; ++i) {
        const int idx = (ringWrite + i) % kFftSize;
        frameScratch[static_cast<size_t>(i)] = ring[static_cast<size_t>(idx)];
    }

    double energy = 0.0;
    for (const float s : frameScratch)
        energy += static_cast<double>(s) * s;
    const float rms = static_cast<float>(std::sqrt(energy / static_cast<double>(kFftSize)));
    const float rmsDb = juce::Decibels::gainToDecibels(std::max(rms, 1.0e-6f), -120.0f);
    // -55dBFS -> 0 presence, -18dBFS -> full presence. Anything quieter than
    // -55dB is treated as noise floor, not a genuine harmonic signal.
    presenceOut = juce::jlimit(0.0f, 1.0f, (rmsDb + 55.0f) / 37.0f);

    window.multiplyWithWindowingTable(frameScratch.data(), static_cast<size_t>(kFftSize));

    std::fill(fftScratch.begin(), fftScratch.end(), 0.0f);
    std::copy(frameScratch.begin(), frameScratch.end(), fftScratch.begin());
    fft.performFrequencyOnlyForwardTransform(fftScratch.data());

    std::array<float, 12> accum {};
    for (int bin = 0; bin <= kFftSize / 2; ++bin) {
        const int pc = binPitchClass[static_cast<size_t>(bin)];
        if (pc < 0)
            continue;
        const float magnitude = fftScratch[static_cast<size_t>(bin)];
        accum[static_cast<size_t>(pc)] += magnitude * magnitude;
    }

    float total = 0.0f;
    for (const float v : accum)
        total += v;
    if (total > 1.0e-9f) {
        for (int i = 0; i < 12; ++i)
            chromaOut[static_cast<size_t>(i)] = accum[static_cast<size_t>(i)] / total;
    } else {
        chromaOut.fill(0.0f);
    }
}

} // namespace vxsuite::tune
