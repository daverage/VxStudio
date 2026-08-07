// Offline ADT voice-seed optimiser for VX Width (2026-08-07 imbalance
// investigation). NOT part of the regression suite or CI - a manual
// research tool, run on demand, that searches for a (voiceA seed, voiceB
// seed) pair with low long-term Mid<->generated-Side correlation across a
// diverse procedural + real-anchor corpus, then validates the winner
// against a disjoint HOLDOUT corpus (different seeds, different sample
// rate) so the result isn't just "this seed happened to work on the one
// fixture we already knew about."
//
// Runtime state (`AdtVoice`) is exercised directly, matching exactly how
// VXWidthProcessor.cpp drives it (same `mid = (l+r)*invSqrt2` scaling,
// same default Tightness=0.40, plus the two other named bands for
// robustness). Current production DSP is left untouched while this search
// runs - see VxWidthProcessor.cpp for whichever seed pair this run
// recommended actually being applied.
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include "../Source/vxstudio/products/width/dsp/VxWidthAdtVoice.h"
#include "../Source/vxstudio/products/width/dsp/VxWidthDecorrelator.h"
#include "VxWidthCorpusGenerator.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <random>
#include <vector>

using namespace vxsuite::width;
using namespace vxsuite::width::corpus;

namespace {

struct CorrAccum {
    double sumXY = 0.0, sumXX = 0.0, sumYY = 0.0;
    void add(const float x, const float y) {
        sumXY += static_cast<double>(x) * y;
        sumXX += static_cast<double>(x) * x;
        sumYY += static_cast<double>(y) * y;
    }
    [[nodiscard]] double corr() const {
        const double denom = sumXX * sumYY;
        return denom > 1e-12 ? sumXY / std::sqrt(denom) : 0.0;
    }
};

constexpr float kLagsMs[] = { 0.0f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f };
constexpr float kInvSqrt2 = 0.70710678f;

// Evaluates one (seedA, seedB) candidate against one corpus item at one
// tightness setting. Returns {lag0Corr (proportional to L/R energy bias),
// maxLagCorr}.
struct ItemResult { double lag0Corr, maxLagCorr; };

ItemResult evaluateItem(const std::uint32_t seedA, const std::uint32_t seedB,
                         const juce::AudioBuffer<float>& mono, const double sr,
                         const float tightness01) {
    AdtVoice va, vb;
    va.prepare(sr, seedA);
    vb.prepare(sr, seedB);

    const int maxLagSamples = static_cast<int>(std::ceil(16.0f * 0.001f * sr)) + 1;
    std::vector<float> midHistory(static_cast<size_t>(maxLagSamples + 1), 0.0f);
    int writePos = 0;
    int lagSamples[6];
    for (size_t k = 0; k < 6; ++k)
        lagSamples[k] = static_cast<int>(std::round(kLagsMs[k] * 0.001f * sr));

    CorrAccum lagAccum[6];
    const int n = mono.getNumSamples();
    const int historySize = static_cast<int>(midHistory.size());
    for (int i = 0; i < n; ++i) {
        const float mid = mono.getSample(0, i) * 1.41421356f;
        const float a = va.process(mid, tightness01, 0.0f);
        const float b = vb.process(mid, tightness01, 0.0f);
        const float side = 0.5f * (a - b);

        midHistory[static_cast<size_t>(writePos)] = mid;
        for (size_t k = 0; k < 6; ++k) {
            int idx = writePos - lagSamples[k];
            if (idx < 0) idx += historySize;
            lagAccum[k].add(midHistory[static_cast<size_t>(idx)], side);
        }
        writePos = (writePos + 1) % historySize;
    }
    double maxLagCorr = 0.0;
    for (auto& acc : lagAccum)
        maxLagCorr = std::max(maxLagCorr, std::abs(acc.corr()));
    return { lagAccum[0].corr(), maxLagCorr };
}

double compositeScore(const std::uint32_t seedA, const std::uint32_t seedB,
                       const std::vector<CorpusItem>& items, const std::vector<float>& tightnessBands) {
    double sumAbsLag0 = 0.0, sumMaxLag = 0.0;
    int count = 0;
    for (const auto& item : items) {
        for (const auto tightness : tightnessBands) {
            const auto r = evaluateItem(seedA, seedB, item.audio, item.sampleRate, tightness);
            sumAbsLag0 += std::abs(r.lag0Corr);
            sumMaxLag += r.maxLagCorr;
            ++count;
        }
    }
    if (count == 0) return 1e9;
    // Weighted: imbalance-proxy (lag0, directly proportional to actual L/R
    // energy bias) weighted higher than the general max-lag correlation
    // (already independently restrained at runtime by
    // ContentPredictabilityRestraint - this search targets the STRUCTURAL
    // bias that restraint can only partially compensate for).
    return 0.6 * (sumAbsLag0 / count) + 0.4 * (sumMaxLag / count);
}

// Decorrelator (Region C / Width path) is NOT searched this pass - scope
// decision: measure the EXISTING fixed taps' correlation against the same
// diverse corpus as a validation baseline. Width's own imbalance was
// already passing threshold (5.4%) before this investigation; only pursue
// a tap redesign if this measurement says otherwise.
ItemResult evaluateDecorrelatorItem(const juce::AudioBuffer<float>& mono, const double sr) {
    VelvetDecorrelator decA, decB;
    decA.prepare(sr, 1.2f, kDecorrelatorTapsA);
    decB.prepare(sr, 1.2f, kDecorrelatorTapsB);
    const int maxLagSamples = static_cast<int>(std::ceil(16.0f * 0.001f * sr)) + 1;
    std::vector<float> midHistory(static_cast<size_t>(maxLagSamples + 1), 0.0f);
    int writePos = 0;
    int lagSamples[6];
    for (size_t k = 0; k < 6; ++k)
        lagSamples[k] = static_cast<int>(std::round(kLagsMs[k] * 0.001f * sr));
    CorrAccum lagAccum[6];
    const int n = mono.getNumSamples();
    const int historySize = static_cast<int>(midHistory.size());
    for (int i = 0; i < n; ++i) {
        const float mid = mono.getSample(0, i) * 1.41421356f;
        const float decorSource = 0.7f * mid; // side=0 for true-mono input
        const float side = decA.process(decorSource) - decB.process(decorSource);
        midHistory[static_cast<size_t>(writePos)] = mid;
        for (size_t k = 0; k < 6; ++k) {
            int idx = writePos - lagSamples[k];
            if (idx < 0) idx += historySize;
            lagAccum[k].add(midHistory[static_cast<size_t>(idx)], side);
        }
        writePos = (writePos + 1) % historySize;
    }
    double maxLagCorr = 0.0;
    for (auto& acc : lagAccum)
        maxLagCorr = std::max(maxLagCorr, std::abs(acc.corr()));
    return { lagAccum[0].corr(), maxLagCorr };
}

// --- Phase 2: decorrelator tap-set search (added after the ADT seed search
// revealed the EXISTING fixed decorrelator taps correlate with Mid far
// worse than the ADT path across the diverse corpus: mean|lag0|=0.37,
// mean(maxLag)=0.55, vs ADT's 0.08-0.17. That crosses from "acceptable
// residual" into "materially bad" - the review that started this
// investigation specifically flagged these taps as arbitrary/hand-picked,
// never validated against real content, and this measurement confirms it. ---

std::array<VelvetTap, 14> randomTapPattern(std::mt19937& rng) {
    std::array<VelvetTap, 14> pattern{};
    std::uniform_real_distribution<float> jitter(-0.045f, 0.045f);
    std::bernoulli_distribution signDist(0.5);
    for (size_t i = 0; i < 14; ++i) {
        const float center = (static_cast<float>(i) + 0.5f) / 14.0f;
        pattern[i].fractionalDelay = juce::jlimit(0.01f, 0.99f, center + jitter(rng));
        pattern[i].sign = signDist(rng) ? 1.0f : -1.0f;
    }
    return pattern;
}

struct DecorItemResult { double lag0Corr, maxLagCorr, rms; };

DecorItemResult evaluateDecorrelatorItemWithTaps(const juce::AudioBuffer<float>& mono, const double sr,
                                                  const std::array<VelvetTap, 14>& tapsA,
                                                  const std::array<VelvetTap, 14>& tapsB) {
    VelvetDecorrelator decA, decB;
    decA.prepare(sr, 1.2f, tapsA);
    decB.prepare(sr, 1.2f, tapsB);
    const int maxLagSamples = static_cast<int>(std::ceil(16.0f * 0.001f * sr)) + 1;
    std::vector<float> midHistory(static_cast<size_t>(maxLagSamples + 1), 0.0f);
    int writePos = 0;
    int lagSamples[6];
    for (size_t k = 0; k < 6; ++k)
        lagSamples[k] = static_cast<int>(std::round(kLagsMs[k] * 0.001f * sr));
    CorrAccum lagAccum[6];
    double sumSideSq = 0.0;
    const int n = mono.getNumSamples();
    const int historySize = static_cast<int>(midHistory.size());
    for (int i = 0; i < n; ++i) {
        const float mid = mono.getSample(0, i) * 1.41421356f;
        const float decorSource = 0.7f * mid;
        const float side = decA.process(decorSource) - decB.process(decorSource);
        sumSideSq += static_cast<double>(side) * side;
        midHistory[static_cast<size_t>(writePos)] = mid;
        for (size_t k = 0; k < 6; ++k) {
            int idx = writePos - lagSamples[k];
            if (idx < 0) idx += historySize;
            lagAccum[k].add(midHistory[static_cast<size_t>(idx)], side);
        }
        writePos = (writePos + 1) % historySize;
    }
    double maxLagCorr = 0.0;
    for (auto& acc : lagAccum)
        maxLagCorr = std::max(maxLagCorr, std::abs(acc.corr()));
    return { lagAccum[0].corr(), maxLagCorr, std::sqrt(sumSideSq / std::max(1, n)) };
}

// Frequency-response flatness of the (decA - decB) system: impulse response
// -> zero-padded FFT -> magnitude in dB across a representative audio band
// (150Hz-15kHz at 48kHz), std-dev and peak deviation from the mean. Uses a
// plain FIR impulse response since VelvetDecorrelator IS a plain FIR (no
// feedback) - one impulse fully characterises it.
struct SpectralFlatness { double stdDevDb, peakDevDb; };

SpectralFlatness measureSpectralFlatness(const std::array<VelvetTap, 14>& tapsA,
                                          const std::array<VelvetTap, 14>& tapsB,
                                          const double sr) {
    VelvetDecorrelator decA, decB;
    decA.prepare(sr, 1.2f, tapsA);
    decB.prepare(sr, 1.2f, tapsB);
    constexpr int kFftOrder = 11; // 2048
    constexpr int kFftSize = 1 << kFftOrder;
    std::vector<float> ir(kFftSize, 0.0f);
    for (int i = 0; i < kFftSize; ++i) {
        const float impulse = (i == 0) ? 1.0f : 0.0f;
        ir[static_cast<size_t>(i)] = decA.process(impulse) - decB.process(impulse);
    }
    juce::dsp::FFT fft(kFftOrder);
    std::vector<float> fftBuf(static_cast<size_t>(kFftSize) * 2, 0.0f);
    std::copy(ir.begin(), ir.end(), fftBuf.begin());
    fft.performRealOnlyForwardTransform(fftBuf.data());

    const double loHz = 150.0, hiHz = 15000.0;
    const int loBin = std::max(1, static_cast<int>(loHz * kFftSize / sr));
    const int hiBin = std::min(kFftSize / 2 - 1, static_cast<int>(hiHz * kFftSize / sr));
    std::vector<double> magDb;
    for (int b = loBin; b <= hiBin; ++b) {
        const float re = fftBuf[static_cast<size_t>(b) * 2];
        const float im = fftBuf[static_cast<size_t>(b) * 2 + 1];
        const float mag = std::sqrt(re * re + im * im) + 1.0e-9f;
        magDb.push_back(20.0 * std::log10(mag));
    }
    double mean = 0.0;
    for (const auto v : magDb) mean += v;
    mean /= std::max<size_t>(1, magDb.size());
    double variance = 0.0, peakDev = 0.0;
    for (const auto v : magDb) {
        variance += (v - mean) * (v - mean);
        peakDev = std::max(peakDev, std::abs(v - mean));
    }
    variance /= std::max<size_t>(1, magDb.size());
    return { std::sqrt(variance), peakDev };
}

double decorCompositeScore(const std::array<VelvetTap, 14>& tapsA, const std::array<VelvetTap, 14>& tapsB,
                            const std::vector<CorpusItem>& items) {
    double sumAbsLag0 = 0.0, sumMaxLag = 0.0, sumRms = 0.0;
    int count = 0;
    for (const auto& item : items) {
        const auto r = evaluateDecorrelatorItemWithTaps(item.audio, item.sampleRate, tapsA, tapsB);
        sumAbsLag0 += std::abs(r.lag0Corr);
        sumMaxLag += r.maxLagCorr;
        sumRms += r.rms;
        ++count;
    }
    const double corrScore = 0.6 * (sumAbsLag0 / count) + 0.4 * (sumMaxLag / count);
    const auto flat = measureSpectralFlatness(tapsA, tapsB, 48000.0);
    // Normalise spectral terms to roughly the same O(1) scale as corrScore
    // (dividing by 6dB - a modest, not tiny, deviation budget).
    const double spectralScore = juce::jlimit(0.0, 2.0, flat.stdDevDb / 6.0);
    const double peakScore = juce::jlimit(0.0, 2.0, flat.peakDevDb / 6.0);
    // Non-degeneracy guard: a pattern where A and B happen to cancel (or
    // nearly cancel) would score a fake-perfect low correlation just by
    // producing near-silence, not by actually decorrelating anything.
    // Existing taps' RMS on true-mono content is the reference floor.
    const double avgRms = sumRms / count;
    const double rmsPenalty = avgRms < 0.03 ? (0.03 - avgRms) * 20.0 : 0.0;
    return 0.5 * corrScore + 0.25 * spectralScore + 0.15 * peakScore + 0.1 * rmsPenalty;
}

} // namespace

int main() {
    std::cout << "=== VX Width ADT Seed Optimizer ===\n";

    // --- Corpus: training (procedural, seedBase=1000, 44.1k/48k) + real
    // vocal anchors (training-only) + holdout (procedural, seedBase=9000,
    // DISJOINT sample rate 96k, never seen during search). ---
    const std::vector<double> trainSampleRates = { 44100.0, 48000.0 };
    const std::vector<float> trainDurations = { 4.0f, 8.0f };
    auto trainingItems = buildProceduralCorpus(trainSampleRates, trainDurations, 2, 1000u);
    juce::File repoRoot = juce::File::getCurrentWorkingDirectory();
    auto anchors = loadRealVocalAnchors(repoRoot);
    for (auto& a : anchors) trainingItems.push_back(std::move(a));

    const std::vector<double> holdoutSampleRates = { 96000.0 };
    const std::vector<float> holdoutDurations = { 6.0f };
    auto holdoutItems = buildProceduralCorpus(holdoutSampleRates, holdoutDurations, 2, 9000u);

    std::cout << "Training corpus: " << trainingItems.size() << " items ("
              << (trainingItems.size() - anchors.size()) << " procedural + "
              << anchors.size() << " real anchors)\n";
    std::cout << "Holdout corpus: " << holdoutItems.size() << " items (disjoint seeds + sample rate)\n\n";

    const std::vector<float> tightnessBands = { 0.0f, 0.40f, 1.0f };

    // --- Search: unordered pairs from a bounded candidate seed pool.
    // doubleSide=0.5*(A-B) is antisymmetric under A/B swap (sign flips,
    // |corr| unchanged), so only unordered pairs need evaluating. ---
    const std::uint32_t pool[] = { 0x1111u, 0x1234u, 0x2222u, 0x3333u, 0x4444u, 0x5678u,
                                    0x6666u, 0x7777u, 0x8888u, 0x9999u };
    const size_t poolSize = sizeof(pool) / sizeof(pool[0]);

    double bestScore = 1e18;
    std::uint32_t bestA = pool[0], bestB = pool[1];
    std::cout << "Searching " << (poolSize * (poolSize - 1) / 2) << " candidate pairs...\n";
    for (size_t i = 0; i < poolSize; ++i) {
        for (size_t j = i + 1; j < poolSize; ++j) {
            const double score = compositeScore(pool[i], pool[j], trainingItems, tightnessBands);
            std::cout << "  A=0x" << std::hex << pool[i] << " B=0x" << pool[j] << std::dec
                       << "  score=" << std::fixed << std::setprecision(5) << score << "\n";
            if (score < bestScore) {
                bestScore = score;
                bestA = pool[i];
                bestB = pool[j];
            }
        }
    }

    std::cout << "\nWINNER: A=0x" << std::hex << bestA << " B=0x" << bestB << std::dec
              << "  trainingScore=" << bestScore << "\n\n";

    // Baseline comparison: current production seeds (0x1234 / 0x2222, the
    // single-fixture seed-search result from earlier today).
    const double currentScore = compositeScore(0x1234u, 0x2222u, trainingItems, tightnessBands);
    std::cout << "Current production (0x1234/0x2222) trainingScore=" << currentScore << "\n\n";

    // --- Holdout validation of the winner ---
    const double holdoutScoreWinner = compositeScore(bestA, bestB, holdoutItems, tightnessBands);
    const double holdoutScoreCurrent = compositeScore(0x1234u, 0x2222u, holdoutItems, tightnessBands);
    std::cout << "Holdout validation (unseen seeds, 96kHz, never used in search):\n"
              << "  winner (0x" << std::hex << bestA << "/0x" << bestB << std::dec << ") holdoutScore="
              << holdoutScoreWinner << "\n"
              << "  current (0x1234/0x2222) holdoutScore=" << holdoutScoreCurrent << "\n\n";

    // --- Decorrelator (Width path) baseline measurement across the same
    // diverse corpus - NOT optimised this pass, see file header comment. ---
    double decorSumAbsLag0 = 0.0, decorSumMaxLag = 0.0;
    int decorCount = 0;
    for (const auto& item : trainingItems) {
        const auto r = evaluateDecorrelatorItem(item.audio, item.sampleRate);
        decorSumAbsLag0 += std::abs(r.lag0Corr);
        decorSumMaxLag += r.maxLagCorr;
        ++decorCount;
    }
    std::cout << "Decorrelator (Width path, EXISTING fixed taps, measured not optimised) on training corpus:\n"
              << "  mean|lag0Corr|=" << (decorSumAbsLag0 / decorCount)
              << "  mean(maxLagCorr)=" << (decorSumMaxLag / decorCount) << "\n";

    const double existingDecorScore = decorCompositeScore(kDecorrelatorTapsA, kDecorrelatorTapsB, trainingItems);
    std::cout << "  existing-taps compositeScore=" << existingDecorScore
              << " (0.37/0.55 correlation crossed from 'acceptable residual' into "
              << "'materially bad' per the corpus-wide measurement above - searching for a "
              << "better tap set)\n\n";

    // --- Phase 2 search: random candidate tap-pattern pairs, scored on the
    // SAME training corpus, spectral flatness via FFT, non-degeneracy RMS
    // guard. Bounded to 60 candidates (each candidate evaluates all 90
    // training items once - no tightness loop, decorrelator has none). ---
    std::cout << "Searching decorrelator tap-pattern candidates...\n";
    constexpr int kNumTapCandidates = 60;
    double bestDecorScore = existingDecorScore;
    std::array<VelvetTap, 14> bestTapsA = kDecorrelatorTapsA;
    std::array<VelvetTap, 14> bestTapsB = kDecorrelatorTapsB;
    bool foundBetter = false;
    for (int c = 0; c < kNumTapCandidates; ++c) {
        std::mt19937 rngA(20000u + static_cast<std::uint32_t>(c) * 2u);
        std::mt19937 rngB(20000u + static_cast<std::uint32_t>(c) * 2u + 1u);
        const auto candA = randomTapPattern(rngA);
        const auto candB = randomTapPattern(rngB);
        const double score = decorCompositeScore(candA, candB, trainingItems);
        if (score < bestDecorScore) {
            bestDecorScore = score;
            bestTapsA = candA;
            bestTapsB = candB;
            foundBetter = true;
            std::cout << "  candidate " << c << " NEW BEST score=" << std::fixed
                       << std::setprecision(5) << score << "\n";
        }
    }

    if (foundBetter) {
        const double holdoutExisting = decorCompositeScore(kDecorrelatorTapsA, kDecorrelatorTapsB, holdoutItems);
        const double holdoutWinner = decorCompositeScore(bestTapsA, bestTapsB, holdoutItems);
        std::cout << "\nDecorrelator WINNER trainingScore=" << bestDecorScore
                   << " (existing=" << existingDecorScore << ")\n"
                   << "Holdout: winner=" << holdoutWinner << " existing=" << holdoutExisting << "\n\n";
        std::cout << "Winning tap pattern A (fractionalDelay, sign):\n";
        for (const auto& t : bestTapsA)
            std::cout << "  {" << std::fixed << std::setprecision(4) << t.fractionalDelay
                       << "f, " << (t.sign > 0 ? "1.0f" : "-1.0f") << "},\n";
        std::cout << "Winning tap pattern B (fractionalDelay, sign):\n";
        for (const auto& t : bestTapsB)
            std::cout << "  {" << std::fixed << std::setprecision(4) << t.fractionalDelay
                       << "f, " << (t.sign > 0 ? "1.0f" : "-1.0f") << "},\n";
    } else {
        std::cout << "\nNo candidate beat the existing taps within " << kNumTapCandidates
                   << " random samples - existing taps kept as-is.\n";
    }

    return 0;
}
