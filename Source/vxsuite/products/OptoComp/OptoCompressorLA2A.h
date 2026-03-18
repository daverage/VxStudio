#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <cmath>

namespace vxsuite::finish {

// Behavioural LA-2A style optical compressor:
// - Feedback detector (1-sample delay).
// - Soft knee gain computer.
// - Dual-stage release with program/frequency dependent slow tail ("optical memory").
//
// Realtime-safe: no allocations in process(), all vectors sized in prepare().
class OptoCompressorLA2A final {
public:
  enum class Mode : int { compress = 0, limit = 1 };

  struct Params final {
    // Normalised controls
    float peakReduction = 0.0f;   // 0..1, maps to sidechain drive (threshold/amount)
    float outputGainDb  = 0.0f;   // makeup/output trim in dB
    float body          = 0.5f;   // 0..1, optional post-comp low shelf, 0.5 neutral

    Mode mode           = Mode::compress;
    bool stereoLink     = true;
  };

  void prepare(double sampleRate, int maxBlockSize, int numChannels);
  void reset() noexcept;

  void setParams(const Params& p) noexcept { params = p; }
  void process(juce::AudioBuffer<float>& buffer) noexcept;

  // Telemetry
  float getGainReductionDb() const noexcept { return grDbSmoothed; }     // >= 0
  float getEnvelopeDb() const noexcept { return detectorDb; }           // dBFS-ish
  float getActivity01() const noexcept { return activity01; }           // 0..1

private:
  Params params {};
  double sr = 44100.0;
  int channels = 0;

  // Feedback taps (previous output sample per channel)
  std::vector<float> y1;

  // For a crude "frequency dependence" proxy: 1-pole lowpass of audio (per channel),
  // then rectified to estimate LF dominance.
  std::vector<float> lfLp1;

  // Body shelf state must be instance-owned so reset() is deterministic.
  std::vector<float> shelfLp1;

  // Detector + optical states (in dB domain for stability/interpretability)
  float fastDb = 0.0f;
  float slowDb = 0.0f;

  // "Memory" state: rises with sustained/deep GR, decays slowly
  float mem01 = 0.0f;

  // Metering outputs
  float detectorDb = -120.0f;
  float grDbSmoothed = 0.0f;
  float activity01 = 0.0f;

private:
  static inline float clamp01(float x) noexcept { return juce::jlimit(0.0f, 1.0f, x); }

  static inline float dbToGain(float db) noexcept {
    return juce::Decibels::decibelsToGain(db);
  }

  static inline float gainToDb(float g) noexcept {
    return juce::Decibels::gainToDecibels(g, -120.0f);
  }

  // Convert a “half-life” time to a 1-pole smoothing coefficient.
  // Using half-life matches published LA-2A wording for the fast release stage.
  float coeffFromHalfLifeSeconds(float halfLifeSeconds) const noexcept;

  // Soft-knee compressor curve in dB domain. Returns GR in dB (>= 0).
  static float gainReductionSoftKneeDb(float inDb,
                                      float thresholdDb,
                                      float ratio,
                                      float kneeWidthDb) noexcept;

  // Maps 0..1 Peak Reduction knob to a sidechain drive in dB.
  static float peakReductionToDriveDb(float peakReduction01, Mode mode) noexcept;

  // Very cheap post-comp “Body” shelf: a low-shelf implemented as lowpass split.
  // body = 0.5 => neutral. Range is intentionally small.
  void applyBodyShelf(juce::AudioBuffer<float>& buffer) noexcept;
};

} // namespace vxsuite::finish
