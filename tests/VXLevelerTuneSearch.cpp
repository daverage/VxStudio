#include "../Source/vxsuite/products/leveler/VxLevelerProcessor.h"
#include "VxSuiteProcessorTestUtils.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <random>
#include <string>
#include <vector>

namespace {

using vxsuite::leveler::Dsp;
using vxsuite::test::render;
using vxsuite::test::setParamNormalized;

struct CandidateResult {
    Dsp::Tuning tuning;
    float spreadDb = 0.0f;
    float rmsDbFs = 0.0f;
    float peakDbFs = 0.0f;
    float corrDry = 0.0f;
    float corrRef = 0.0f;
    float score = -1.0e9f;
};

float toDb(const float gain) {
    return juce::Decibels::gainToDecibels(std::max(gain, 1.0e-12f), -120.0f);
}

juce::AudioBuffer<float> readWaveFile(const juce::File& file, double& sampleRate) {
    juce::AudioFormatManager manager;
    manager.registerBasicFormats();
    auto reader = std::unique_ptr<juce::AudioFormatReader>(manager.createReaderFor(file));
    if (reader == nullptr)
        throw std::runtime_error("Could not read input file");

    sampleRate = reader->sampleRate;
    juce::AudioBuffer<float> buffer(static_cast<int>(reader->numChannels),
                                    static_cast<int>(reader->lengthInSamples));
    reader->read(&buffer, 0, buffer.getNumSamples(), 0, true, true);
    return buffer;
}

float rms(const juce::AudioBuffer<float>& buffer) {
    double energy = 0.0;
    int count = 0;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
        const auto* data = buffer.getReadPointer(ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i) {
            energy += static_cast<double>(data[i]) * data[i];
            ++count;
        }
    }
    return static_cast<float>(std::sqrt(energy / std::max(1, count)));
}

float peakAbs(const juce::AudioBuffer<float>& buffer) {
    float peak = 0.0f;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        peak = std::max(peak, buffer.getMagnitude(ch, 0, buffer.getNumSamples()));
    return peak;
}

float windowedLevelSpreadDb(const juce::AudioBuffer<float>& buffer,
                            const double sr,
                            const float windowSeconds = 0.10f) {
    const int channels = buffer.getNumChannels();
    const int windowSamples = std::max(1, static_cast<int>(std::round(sr * windowSeconds)));
    std::vector<float> levels;
    for (int start = 0; start < buffer.getNumSamples(); start += windowSamples) {
        const int end = std::min(buffer.getNumSamples(), start + windowSamples);
        double energy = 0.0;
        int count = 0;
        for (int ch = 0; ch < channels; ++ch) {
            const auto* data = buffer.getReadPointer(ch);
            for (int i = start; i < end; ++i) {
                energy += static_cast<double>(data[i]) * data[i];
                ++count;
            }
        }
        const float r = count > 0 ? static_cast<float>(std::sqrt(energy / static_cast<double>(count))) : 0.0f;
        levels.push_back(juce::Decibels::gainToDecibels(std::max(r, 1.0e-5f), -100.0f));
    }
    if (levels.size() < 2)
        return 0.0f;

    double mean = 0.0;
    for (const float level : levels)
        mean += level;
    mean /= static_cast<double>(levels.size());

    double variance = 0.0;
    for (const float level : levels) {
        const double d = static_cast<double>(level) - mean;
        variance += d * d;
    }
    variance /= static_cast<double>(levels.size());
    return static_cast<float>(std::sqrt(variance));
}

std::vector<float> toMono(const juce::AudioBuffer<float>& buffer) {
    std::vector<float> mono(static_cast<std::size_t>(buffer.getNumSamples()), 0.0f);
    const float scale = 1.0f / static_cast<float>(std::max(1, buffer.getNumChannels()));
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
        const auto* data = buffer.getReadPointer(ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            mono[static_cast<std::size_t>(i)] += data[i] * scale;
    }
    return mono;
}

float correlation(const std::vector<float>& a, const std::vector<float>& b) {
    const std::size_t n = std::min(a.size(), b.size());
    if (n < 2)
        return 0.0f;

    double meanA = 0.0;
    double meanB = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        meanA += a[i];
        meanB += b[i];
    }
    meanA /= static_cast<double>(n);
    meanB /= static_cast<double>(n);

    double num = 0.0;
    double denA = 0.0;
    double denB = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double da = static_cast<double>(a[i]) - meanA;
        const double db = static_cast<double>(b[i]) - meanB;
        num += da * db;
        denA += da * da;
        denB += db * db;
    }
    if (denA <= 1.0e-12 || denB <= 1.0e-12)
        return 0.0f;
    return static_cast<float>(num / std::sqrt(denA * denB));
}

Dsp::Tuning makeCandidate(std::mt19937& rng) {
    std::uniform_real_distribution<float> targetBlendBase(0.20f, 0.32f);
    std::uniform_real_distribution<float> targetBlendLevel(0.10f, 0.26f);
    std::uniform_real_distribution<float> deadbandBase(0.85f, 1.10f);
    std::uniform_real_distribution<float> shortThreshBase(0.40f, 0.65f);
    std::uniform_real_distribution<float> baselineThreshBase(0.25f, 0.50f);
    std::uniform_real_distribution<float> shortScaleBase(0.50f, 0.78f);
    std::uniform_real_distribution<float> baselineScaleBase(0.76f, 1.02f);
    std::uniform_real_distribution<float> maxDb(4.0f, 5.6f);
    std::uniform_real_distribution<float> spikePenalty(0.25f, 0.50f);

    Dsp::Tuning t;
    t.mixTargetBlendBase = targetBlendBase(rng);
    t.mixTargetBlendLevelWeight = targetBlendLevel(rng);
    t.mixDeadbandBase = deadbandBase(rng);
    t.mixNormalizeShortThresholdBase = shortThreshBase(rng);
    t.mixNormalizeBaselineThresholdBase = baselineThreshBase(rng);
    t.mixNormalizeShortScaleBase = shortScaleBase(rng);
    t.mixNormalizeBaselineScaleBase = baselineScaleBase(rng);
    t.mixNormalizeMaxDb = maxDb(rng);
    t.mixNormalizeSpikePenalty = spikePenalty(rng);
    return t;
}

CandidateResult evaluateCandidate(const Dsp::Tuning& tuning,
                                  const juce::AudioBuffer<float>& dry,
                                  const std::vector<float>& dryMono,
                                  const std::vector<float>& refMono,
                                  const double sampleRate) {
    VXLevelerAudioProcessor leveler;
    leveler.prepareToPlay(sampleRate, 256);
    leveler.setDebugTuning(tuning);
    setParamNormalized(leveler, "mode", 1.0f);
    setParamNormalized(leveler, "level", 1.0f);
    setParamNormalized(leveler, "control", 1.0f);

    const auto out = render(leveler, dry, 256);
    const auto wetMono = toMono(out);

    CandidateResult result;
    result.tuning = tuning;
    result.spreadDb = windowedLevelSpreadDb(out, sampleRate);
    result.rmsDbFs = toDb(rms(out));
    result.peakDbFs = toDb(peakAbs(out));
    result.corrDry = correlation(dryMono, wetMono);
    result.corrRef = correlation(refMono, wetMono);

    const float drySpread = windowedLevelSpreadDb(dry, sampleRate);
    const float dryRmsDbFs = toDb(rms(dry));
    const float dryPeakDbFs = toDb(peakAbs(dry));
    const float spreadImprovement = drySpread - result.spreadDb;
    const float rmsLossDb = dryRmsDbFs - result.rmsDbFs;
    const float peakLossDb = dryPeakDbFs - result.peakDbFs;

    result.score = 100.0f * result.corrDry
        + 20.0f * result.corrRef
        + 4.0f * spreadImprovement
        - 5.0f * std::max(0.0f, rmsLossDb - 2.8f)
        - 3.0f * std::max(0.0f, peakLossDb - 2.0f)
        - 20.0f * std::max(0.0f, -spreadImprovement);

    return result;
}

void printResult(const CandidateResult& r, const std::string& label) {
    std::cout << label
              << " score=" << r.score
              << " spread=" << r.spreadDb
              << " rms=" << r.rmsDbFs
              << " peak=" << r.peakDbFs
              << " corrDry=" << r.corrDry
              << " corrRef=" << r.corrRef
              << " | blendBase=" << r.tuning.mixTargetBlendBase
              << " blendLevel=" << r.tuning.mixTargetBlendLevelWeight
              << " deadband=" << r.tuning.mixDeadbandBase
              << " shortThresh=" << r.tuning.mixNormalizeShortThresholdBase
              << " baselineThresh=" << r.tuning.mixNormalizeBaselineThresholdBase
              << " shortScale=" << r.tuning.mixNormalizeShortScaleBase
              << " baselineScale=" << r.tuning.mixNormalizeBaselineScaleBase
              << " maxDb=" << r.tuning.mixNormalizeMaxDb
              << " spikePenalty=" << r.tuning.mixNormalizeSpikePenalty
              << "\n";
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: VXLevelerTuneSearch <input.wav> <mix_reference.wav> [numCandidates]\n";
        return 1;
    }

    const juce::ScopedJuceInitialiser_GUI juceInit;
    const juce::File inputFile(argv[1]);
    const juce::File refFile(argv[2]);
    const int numCandidates = argc > 3 ? std::max(1, std::atoi(argv[3])) : 96;

    try {
        double inputSr = 48000.0;
        double refSr = 48000.0;
        const auto dry = readWaveFile(inputFile, inputSr);
        const auto ref = readWaveFile(refFile, refSr);
        if (std::abs(inputSr - refSr) > 1.0)
            throw std::runtime_error("Input and reference sample rates must match");

        const auto dryMono = toMono(dry);
        const auto refMono = toMono(ref);

        std::mt19937 rng(1337u);
        std::vector<CandidateResult> results;
        results.reserve(static_cast<std::size_t>(numCandidates + 1));

        Dsp::Tuning current;
        results.push_back(evaluateCandidate(current, dry, dryMono, refMono, inputSr));

        for (int i = 0; i < numCandidates; ++i)
            results.push_back(evaluateCandidate(makeCandidate(rng), dry, dryMono, refMono, inputSr));

        std::sort(results.begin(), results.end(), [](const CandidateResult& a, const CandidateResult& b) {
            return a.score > b.score;
        });

        std::cout << "Top candidates:\n";
        const int limit = std::min<int>(10, results.size());
        for (int i = 0; i < limit; ++i)
            printResult(results[static_cast<std::size_t>(i)], i == 0 ? "best" : ("cand" + std::to_string(i + 1)));

    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }

    return 0;
}
