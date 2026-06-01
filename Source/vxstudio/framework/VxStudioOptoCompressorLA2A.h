#pragma once

#include <cmath>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>

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
    float peakReduction = 0.0f;
    float outputGainDb  = 0.0f;
    float body          = 0.5f;

    Mode mode           = Mode::compress;
    bool stereoLink     = true;
  };

  void prepare(double sampleRate, int maxBlockSize, int numChannels);
  void reset() noexcept;
  void setParams(const Params& p) noexcept { params = p; }
  void process(juce::AudioBuffer<float>& buffer) noexcept;

  float getGainReductionDb() const noexcept { return grDbSmoothed; }
  float getEnvelopeDb() const noexcept { return detectorDb; }
  float getActivity01() const noexcept { return activity01; }

private:
  Params params {};
  double sr = 44100.0;
  int channels = 0;
  std::vector<float> y1;
  std::vector<float> lfLp1;
  std::vector<float> shelfLp1;
  std::vector<float> fastDbCh;
  std::vector<float> slowDbCh;
  std::vector<float> mem01Ch;
  std::vector<float> detectorDbCh;
  float detectorDb = -120.0f;
  float grDbSmoothed = 0.0f;
  float activity01 = 0.0f;

  static inline float clamp01(float x) noexcept { return juce::jlimit(0.0f, 1.0f, x); }
  static inline float dbToGain(float db) noexcept { return juce::Decibels::decibelsToGain(db); }
  static inline float gainToDb(float g) noexcept { return juce::Decibels::gainToDecibels(g, -120.0f); }
  float coeffFromHalfLifeSeconds(float halfLifeSeconds) const noexcept;
  static float gainReductionSoftKneeDb(float inDb, float thresholdDb, float ratio, float kneeWidthDb) noexcept;
  static float peakReductionToDriveDb(float peakReduction01, Mode mode) noexcept;
  void applyBodyShelf(juce::AudioBuffer<float>& buffer) noexcept;
};

} // namespace vxsuite::finish
