#include "VxStudioOptoCompressorLA2A.h"

#include <algorithm>

namespace vxsuite::finish {

void OptoCompressorLA2A::prepare(const double sampleRate, int /*maxBlockSize*/, const int numChannels) {
  sr = (sampleRate > 1000.0 ? sampleRate : 48000.0);
  channels = std::max(0, numChannels);

  y1.assign(static_cast<size_t>(channels), 0.0f);
  lfLp1.assign(static_cast<size_t>(channels), 0.0f);
  shelfLp1.assign(static_cast<size_t>(channels), 0.0f);
  fastDbCh.assign(static_cast<size_t>(channels), 0.0f);
  slowDbCh.assign(static_cast<size_t>(channels), 0.0f);
  mem01Ch.assign(static_cast<size_t>(channels), 0.0f);
  detectorDbCh.assign(static_cast<size_t>(channels), -120.0f);

  reset();
}

void OptoCompressorLA2A::reset() noexcept {
  std::fill(y1.begin(), y1.end(), 0.0f);
  std::fill(lfLp1.begin(), lfLp1.end(), 0.0f);
  std::fill(shelfLp1.begin(), shelfLp1.end(), 0.0f);
  std::fill(fastDbCh.begin(), fastDbCh.end(), 0.0f);
  std::fill(slowDbCh.begin(), slowDbCh.end(), 0.0f);
  std::fill(mem01Ch.begin(), mem01Ch.end(), 0.0f);
  std::fill(detectorDbCh.begin(), detectorDbCh.end(), -120.0f);

  detectorDb = -120.0f;
  grDbSmoothed = 0.0f;
  activity01 = 0.0f;
}

float OptoCompressorLA2A::coeffFromHalfLifeSeconds(const float halfLifeSeconds) const noexcept {
  if (halfLifeSeconds <= 0.0f || sr <= 1000.0) return 0.0f;
  return std::exp(std::log(0.5f) / (static_cast<float>(sr) * halfLifeSeconds));
}

float OptoCompressorLA2A::gainReductionSoftKneeDb(const float inDb,
                                                  const float thresholdDb,
                                                  const float ratio,
                                                  const float kneeWidthDb) noexcept {
  const float r = std::max(1.0f, ratio);
  const float knee = std::max(0.0f, kneeWidthDb);

  const float x = inDb - thresholdDb;

  if (knee <= 1.0e-6f) {
    if (x <= 0.0f) return 0.0f;
    return (1.0f - 1.0f / r) * x;
  }

  const float halfKnee = 0.5f * knee;

  if (x <= -halfKnee) return 0.0f;
  if (x >=  halfKnee) return (1.0f - 1.0f / r) * x;

  const float t = (x + halfKnee) / knee;
  const float y = t * t;
  const float effectiveOver = y * (x + halfKnee);
  return (1.0f - 1.0f / r) * effectiveOver;
}

float OptoCompressorLA2A::peakReductionToDriveDb(const float peakReduction01, const Mode mode) noexcept {
  const float pr = clamp01(peakReduction01);
  const float tapered = std::pow(pr, mode == Mode::limit ? 0.92f : 0.98f);
  const float maxDriveDb = (mode == Mode::limit) ? 58.0f : 52.0f;
  return tapered * maxDriveDb;
}

void OptoCompressorLA2A::process(juce::AudioBuffer<float>& buffer) noexcept {
  const int numChannels = std::min(channels, buffer.getNumChannels());
  const int numSamples  = buffer.getNumSamples();
  if (numChannels <= 0 || numSamples <= 0) return;

  const Params p = params;

  const Mode mode = p.mode;
  const float pr01 = clamp01(p.peakReduction);
  const bool compressionEnabled = pr01 > 1.0e-4f;

  const float atkSeconds = 0.010f;
  const float fastHalfLife = (mode == Mode::limit) ? 0.110f : 0.060f;
  const float ratio = (mode == Mode::limit) ? 12.0f : 3.0f;
  const float kneeDb = (mode == Mode::limit) ? 7.0f : 9.0f;
  const float baseThresholdDb = -24.0f;
  const float driveDb = peakReductionToDriveDb(pr01, mode);
  const float atkA = std::exp(-1.0f / (atkSeconds * static_cast<float>(sr)));
  const float relFastA = coeffFromHalfLifeSeconds(fastHalfLife);
  const float lfCut = 200.0f;
  const float lfA = std::exp(-2.0f * juce::MathConstants<float>::pi * lfCut / static_cast<float>(sr));
  const float memRiseA = std::exp(-1.0f / (0.40f * static_cast<float>(sr)));
  const float memFallA = std::exp(-1.0f / (18.0f * static_cast<float>(sr)));

  float grAcc = 0.0f;
  float detectorAcc = 0.0f;

  for (int i = 0; i < numSamples; ++i) {
    if (p.stereoLink) {
      float scAbs = 0.0f;
      float lfAbs = 0.0f;
      for (int ch = 0; ch < numChannels; ++ch) {
        const float yPrev = y1[static_cast<size_t>(ch)];
        scAbs += std::abs(yPrev);

        float lf = lfLp1[static_cast<size_t>(ch)];
        lf = lfA * lf + (1.0f - lfA) * yPrev;
        lfLp1[static_cast<size_t>(ch)] = lf;
        lfAbs += std::abs(lf);
      }
      scAbs /= static_cast<float>(numChannels);
      lfAbs /= static_cast<float>(numChannels);

      const float slowMin = (mode == Mode::limit) ? 1.0f : 0.50f;
      const float slowMax = (mode == Mode::limit) ? 15.0f : 5.0f;
      const float lfRatioEstimate = lfAbs / (scAbs + 1.0e-6f);
      const float lfSlowFactor = 1.0f + 0.70f * clamp01(lfRatioEstimate);
      const float slowSeconds = (slowMin + (slowMax - slowMin) * mem01Ch[0]) * lfSlowFactor;
      const float relSlowA = std::exp(-1.0f / (slowSeconds * static_cast<float>(sr)));

      const float scDb = gainToDb(std::max(scAbs, 1.0e-9f));
      detectorDbCh[0] = scDb;
      const float drivenDb = scDb + driveDb;
      const float targetGrDb = compressionEnabled
          ? std::max(0.0f, gainReductionSoftKneeDb(drivenDb, baseThresholdDb, ratio, kneeDb))
          : 0.0f;

      const float memTarget = clamp01(targetGrDb / 25.0f);
      if (memTarget > mem01Ch[0])
        mem01Ch[0] = memRiseA * mem01Ch[0] + (1.0f - memRiseA) * memTarget;
      else
        mem01Ch[0] = memFallA * mem01Ch[0] + (1.0f - memFallA) * memTarget;

      const float aFast = (targetGrDb > fastDbCh[0]) ? atkA : relFastA;
      const float aSlow = (targetGrDb > slowDbCh[0]) ? atkA : relSlowA;

      fastDbCh[0] = aFast * fastDbCh[0] + (1.0f - aFast) * targetGrDb;
      slowDbCh[0] = aSlow * slowDbCh[0] + (1.0f - aSlow) * targetGrDb;

      const float grDb = 0.5f * (fastDbCh[0] + slowDbCh[0]);
      const float grGain = dbToGain(-grDb);
      const float makeup = dbToGain(p.outputGainDb);

      grAcc += grDb;
      detectorAcc += scDb;

      for (int ch = 0; ch < numChannels; ++ch) {
        auto* d = buffer.getWritePointer(ch);
        const float x = d[i] * grGain * makeup;
        d[i] = x;
        y1[static_cast<size_t>(ch)] = x;
      }
    } else {
      for (int ch = 0; ch < numChannels; ++ch) {
        const size_t idx = static_cast<size_t>(ch);
        const float yPrev = y1[idx];
        const float scAbs = std::abs(yPrev);

        float lf = lfLp1[idx];
        lf = lfA * lf + (1.0f - lfA) * yPrev;
        lfLp1[idx] = lf;

        const float slowMin = (mode == Mode::limit) ? 1.0f : 0.50f;
        const float slowMax = (mode == Mode::limit) ? 15.0f : 5.0f;
        const float lfRatioEstimate = std::abs(lf) / (scAbs + 1.0e-6f);
        const float lfSlowFactor = 1.0f + 0.70f * clamp01(lfRatioEstimate);
        const float slowSeconds = (slowMin + (slowMax - slowMin) * mem01Ch[idx]) * lfSlowFactor;
        const float relSlowA = std::exp(-1.0f / (slowSeconds * static_cast<float>(sr)));

        const float scDb = gainToDb(std::max(scAbs, 1.0e-9f));
        detectorDbCh[idx] = scDb;
        const float drivenDb = scDb + driveDb;
        const float targetGrDb = compressionEnabled
            ? std::max(0.0f, gainReductionSoftKneeDb(drivenDb, baseThresholdDb, ratio, kneeDb))
            : 0.0f;

        const float memTarget = clamp01(targetGrDb / 25.0f);
        if (memTarget > mem01Ch[idx])
          mem01Ch[idx] = memRiseA * mem01Ch[idx] + (1.0f - memRiseA) * memTarget;
        else
          mem01Ch[idx] = memFallA * mem01Ch[idx] + (1.0f - memFallA) * memTarget;

        const float aFast = (targetGrDb > fastDbCh[idx]) ? atkA : relFastA;
        const float aSlow = (targetGrDb > slowDbCh[idx]) ? atkA : relSlowA;

        fastDbCh[idx] = aFast * fastDbCh[idx] + (1.0f - aFast) * targetGrDb;
        slowDbCh[idx] = aSlow * slowDbCh[idx] + (1.0f - aSlow) * targetGrDb;

        const float grDb = 0.5f * (fastDbCh[idx] + slowDbCh[idx]);
        const float grGain = dbToGain(-grDb);
        const float makeup = dbToGain(p.outputGainDb);

        grAcc += grDb;
        detectorAcc += scDb;

        auto* d = buffer.getWritePointer(ch);
        const float x = d[i] * grGain * makeup;
        d[i] = x;
        y1[idx] = x;
      }
    }
  }

  const float meterDivisor = static_cast<float>(std::max(1, (p.stereoLink ? numSamples : numSamples * numChannels)));
  const float grAvgDb = grAcc / meterDivisor;
  detectorDb = detectorAcc / meterDivisor;
  // Gate metering display on compressionEnabled: when Peak Reduction = 0, the slow
  // LA2A release (up to 5 s) would otherwise keep grDbSmoothed > 0 for seconds after
  // the knob is turned down, making the GR LED appear "always lit."
  const bool inputPresent = detectorDb > -60.0f;
  grDbSmoothed = (compressionEnabled && inputPresent) ? grAvgDb : 0.0f;
  activity01 = (compressionEnabled && inputPresent) ? clamp01(grAvgDb / 12.0f) : 0.0f;

  applyBodyShelf(buffer);
}

void OptoCompressorLA2A::applyBodyShelf(juce::AudioBuffer<float>& buffer) noexcept {
  const int numChannels = std::min(channels, buffer.getNumChannels());
  const int numSamples  = buffer.getNumSamples();
  if (numChannels <= 0 || numSamples <= 0) return;

  const float b = clamp01(params.body);
  const float delta = (b - 0.5f) * 2.0f;
  if (std::abs(delta) <= 1.0e-4f)
    return;

  const float lowGainDb = 4.5f * delta;
  const float lowGain = dbToGain(lowGainDb);
  const float fc = 180.0f;
  const float a = std::exp(-2.0f * juce::MathConstants<float>::pi * fc / static_cast<float>(sr));

  for (int ch = 0; ch < numChannels; ++ch) {
    auto* d = buffer.getWritePointer(ch);
    float lp = shelfLp1[static_cast<size_t>(ch)];
    for (int i = 0; i < numSamples; ++i) {
      const float x = d[i];
      lp = a * lp + (1.0f - a) * x;
      const float low = lp;
      const float high = x - lp;
      d[i] = low * lowGain + high;
    }
    shelfLp1[static_cast<size_t>(ch)] = lp;
  }
}

} // namespace vxsuite::finish
