#pragma once

#include "VxTuneTimeline.h"

#include <vector>

namespace vxsuite::tune {

// YIN-family probabilistic monophonic f0 detector (architecture doc §5.1).
// Hop-based: feed audio with process(); one PitchObservation per hop.
// Estimates carry EstimateReason so downstream stages know *why* confidence
// degraded (periodicity collapse vs octave ambiguity vs onset vs unvoiced).
//
// Realtime-safe after prepare(): no allocation, locks, or I/O in process().
// The analysis window trails the newest sample, so estimates lag by roughly
// half a window; this is analysis latency only and adds no audio-path delay.
class PitchDetector {
public:
    struct Config {
        float minHz = 70.0f;
        float maxHz = 1000.0f;
        int hopSamples = 256;
        float voicedThreshold = 0.15f;   // CMNDF absolute threshold
        float silenceFloorDb = -70.0f;
    };

    void prepare(double sampleRate, const Config& config);
    void reset();

    // Consumes mono input; appends up to maxOut observations for completed
    // hops. Returns the number of observations written.
    int process(const float* input, int numSamples,
                PitchObservation* out, int maxOut) noexcept;

    int hopSamples() const noexcept { return cfg.hopSamples; }
    double sampleRate() const noexcept { return sr; }

private:
    void analyseWindow(PitchObservation& obs) noexcept;

    Config cfg {};
    double sr = 48000.0;
    int windowSamples = 0;      // difference-function window W
    int tauMin = 0;
    int tauMax = 0;

    std::vector<float> ring;    // history: windowSamples + tauMax
    int ringSize = 0;
    int ringWrite = 0;
    int samplesSinceHop = 0;
    std::int64_t totalSamples = 0;

    std::vector<float> frame;   // linearised window + lags
    std::vector<float> cmndf;   // cumulative-mean-normalised difference

    float previousLevelDb = -120.0f;
    float previousCents = 0.0f;
    bool hadPreviousPitch = false;
};

} // namespace vxsuite::tune
