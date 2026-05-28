#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace vxsuite {

namespace analysis {
struct AnalysisSummary;
}

struct SignalQualitySnapshot {
    float monoScore = 0.0f;              // 0 = healthy stereo, 1 = near mono
    float compressionScore = 0.0f;       // 0 = dynamic, 1 = heavily crushed
    float tiltScore = 0.0f;              // 0 = balanced, 1 = strong low tilt
    float separationConfidence = 1.0f;   // 1 = trust source-specific heuristics, 0 = blend toward unity
};

class SignalQualityState {
public:
    void prepare(double sampleRate, int maxSamplesPerBlock);
    void reset();
    void update(const juce::AudioBuffer<float>& input, int numSamples);
    void updateFromPublishedSummaries(const analysis::AnalysisSummary& drySummary,
                                      const analysis::AnalysisSummary& wetSummary) noexcept;
    [[nodiscard]] SignalQualitySnapshot snapshot() const noexcept { return current; }

private:
    static float timeAlpha(double sampleRate, float seconds) noexcept;
    static float analyzeMonoFromSummary(const analysis::AnalysisSummary& summary) noexcept;
    static float analyzeCompressionFromSummary(const analysis::AnalysisSummary& summary) noexcept;
    static float analyzeTiltFromSummary(const analysis::AnalysisSummary& summary) noexcept;

    double sr = 48000.0;
    float low500L = 0.0f;
    float low500R = 0.0f;
    float low4kL = 0.0f;
    float low4kR = 0.0f;
    float smoothedPeak = 0.0f;
    float smoothedRms = 0.0f;
    float alpha500 = 0.0f;
    float alpha4k = 0.0f;
    SignalQualitySnapshot current;
};

} // namespace vxsuite
