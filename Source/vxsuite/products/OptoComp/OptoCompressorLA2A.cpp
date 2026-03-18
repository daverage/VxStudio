#include "OptoCompressorLA2A.h"

namespace vxsuite::finish {

void OptoCompressorLA2A::prepare(const double sampleRate, int /*maxBlockSize*/, const int numChannels) {
  sr = (sampleRate > 1000.0 ? sampleRate : 48000.0);
  channels = std::max(0, numChannels);

  y1.assign(static_cast<size_t>(channels), 0.0f);
  lfLp1.assign(static_cast<size_t>(channels), 0.0f);
  shelfLp1.assign(static_cast<size_t>(channels), 0.0f);

  reset();
}

void OptoCompressorLA2A::reset() noexcept {
  std::fill(y1.begin(), y1.end(), 0.0f);
  std::fill(lfLp1.begin(), lfLp1.end(), 0.0f);
  std::fill(shelfLp1.begin(), shelfLp1.end(), 0.0f);

  fastDb = 0.0f;
  slowDb = 0.0f;
  mem01 = 0.0f;

  detectorDb = -120.0f;
  grDbSmoothed = 0.0f;
  activity01 = 0.0f;
}

float OptoCompressorLA2A::coeffFromHalfLifeSeconds(const float halfLifeSeconds) const noexcept {
  if (halfLifeSeconds <= 0.0f || sr <= 1000.0) return 0.0f;
  // y[n] = a*y[n-1] + (1-a)*x[n]
  // half-life => a^(sr*t) = 0.5 => a = exp(ln(0.5)/(sr*t))
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

  // Standard quadratic soft-knee region
  const float halfKnee = 0.5f * knee;

  if (x <= -halfKnee) return 0.0f;
  if (x >=  halfKnee) return (1.0f - 1.0f / r) * x;

  // x in [-halfKnee, +halfKnee]
  const float t = (x + halfKnee) / knee; // 0..1
  // Smoothly transition from 0 to full slope
  const float y = t * t; // quadratic
  const float effectiveOver = y * (x + halfKnee); // shaped
  return (1.0f - 1.0f / r) * effectiveOver;
}

float OptoCompressorLA2A::peakReductionToDriveDb(const float peakReduction01, const Mode mode) noexcept {
  // LA-2A Peak Reduction is non-linear in practice. Use a gentle taper so early travel is usable.
  const float pr = clamp01(peakReduction01);
  const float tapered = std::pow(pr, 1.8f);

  // In limit mode users typically expect “more grab” at the same knob value.
  const float maxDriveDb = (mode == Mode::limit) ? 44.0f : 40.0f;
  return tapered * maxDriveDb;
}

void OptoCompressorLA2A::process(juce::AudioBuffer<float>& buffer) noexcept {
  const int numChannels = std::min(channels, buffer.getNumChannels());
  const int numSamples  = buffer.getNumSamples();
  if (numChannels <= 0 || numSamples <= 0) return;

  const Params p = params; // local copy for thread-safety

  const Mode mode = p.mode;
  const float pr01 = clamp01(p.peakReduction);
  const bool compressionEnabled = pr01 > 1.0e-4f;

  // Targets from documentation:
  // - Attack effectively fixed-ish. Use ~10ms (time constant-ish).
  // - Fast release half-life ~60ms.
  // - Slow release is program-dependent, ~0.5..15s typical.
  //
  // We interpret “50% release at 60ms” literally as half-life for the fast stage.
  const float atkSeconds = 0.010f;  // effective ballistics target
  const float fastHalfLife = 0.060f;

  // Mode-dependent baseline ratios.
  // Compressor slope around 3:1 is described in Teletronix documentation; limit is much stronger.
  const float ratio = (mode == Mode::limit) ? 12.0f : 3.0f;

  // Soft knee: keep it wide. LA-2A knee is not sharp.
  const float kneeDb = (mode == Mode::limit) ? 7.0f : 9.0f;

  // Threshold: set for typical DAW levels; "Peak Reduction" shifts effectively via sidechain drive.
  const float baseThresholdDb = -24.0f;

  // Sidechain drive from Peak Reduction knob.
  const float driveDb = peakReductionToDriveDb(pr01, mode);

  // Convert attack into a 1-pole coefficient (time constant).
  const float atkA = std::exp(-1.0f / (atkSeconds * static_cast<float>(sr)));

  // Fast stage uses half-life.
  const float relFastA = coeffFromHalfLifeSeconds(fastHalfLife);

  // Frequency-dependence proxy: measure LF dominance.
  // 1-pole LP cutoff for LF estimate ~200 Hz.
  const float lfCut = 200.0f;
  const float lfA = std::exp(-2.0f * juce::MathConstants<float>::pi * lfCut / static_cast<float>(sr));

  // Memory dynamics:
  // - rises relatively quickly when GR is sustained
  // - decays slowly (many seconds)
  const float memRiseA = std::exp(-1.0f / (0.40f * static_cast<float>(sr)));   // ~0.4s TC
  const float memFallA = std::exp(-1.0f / (18.0f * static_cast<float>(sr)));   // ~18s TC

  float grAcc = 0.0f;

  for (int i = 0; i < numSamples; ++i) {
    // Stereo-linked feedback detector
    float scAbs = 0.0f;
    float lfAbs = 0.0f;

    if (p.stereoLink) {
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
    } else {
      // If not linked (rare): use channel 0 detector
      const float yPrev = y1[0];
      scAbs = std::abs(yPrev);
      float lf = lfLp1[0];
      lf = lfA * lf + (1.0f - lfA) * yPrev;
      lfLp1[0] = lf;
      lfAbs = std::abs(lf);
    }

    // Avoid log blow-ups
    const float scDb = gainToDb(std::max(scAbs, 1.0e-9f));
    detectorDb = scDb;

    // Effective input to gain computer
    const float drivenDb = scDb + driveDb;

    // When Peak Reduction is at zero, the opto stage should behave like a transparent
    // makeup/body wrapper rather than a still-live fixed-threshold compressor.
    const float targetGrDb = compressionEnabled
        ? std::max(0.0f, gainReductionSoftKneeDb(drivenDb, baseThresholdDb, ratio, kneeDb))
        : 0.0f;

    // Update memory state:
    // target is proportional to GR; decay slower than rise.
    const float memTarget = clamp01(targetGrDb / 25.0f); // 25dB ~ "strong"
    if (memTarget > mem01)
      mem01 = memRiseA * mem01 + (1.0f - memRiseA) * memTarget;
    else
      mem01 = memFallA * mem01 + (1.0f - memFallA) * memTarget;

    // Frequency dependence factor: more LF dominance => slower tail.
    const float lfRatio = lfAbs / (scAbs + 1.0e-6f); // ~0..1
    const float lfSlowFactor = 1.0f + 0.70f * clamp01(lfRatio);

    // Slow release time range:
    // Compress: 0.5..5s
    // Limit:    1..15s
    const float slowMin = (mode == Mode::limit) ? 1.0f : 0.50f;
    const float slowMax = (mode == Mode::limit) ? 15.0f : 5.0f;
    const float slowSeconds = (slowMin + (slowMax - slowMin) * mem01) * lfSlowFactor;

    const float relSlowA = std::exp(-1.0f / (slowSeconds * static_cast<float>(sr)));

    // Dual ballistics on targetGrDb (in dB):
    // Attack: both use atkA.
    // Release: fast uses relFastA, slow uses relSlowA.
    const float aFast = (targetGrDb > fastDb) ? atkA : relFastA;
    const float aSlow = (targetGrDb > slowDb) ? atkA : relSlowA;

    fastDb = aFast * fastDb + (1.0f - aFast) * targetGrDb;
    slowDb = aSlow * slowDb + (1.0f - aSlow) * targetGrDb;

    const float grDb = 0.5f * (fastDb + slowDb);

    // Telemetry
    grDbSmoothed = grDb;
    grAcc += grDb;

    // Convert to gain and apply
    const float grGain = dbToGain(-grDb);
    const float makeup = dbToGain(p.outputGainDb);

    for (int ch = 0; ch < numChannels; ++ch) {
      auto* d = buffer.getWritePointer(ch);
      const float x = d[i] * grGain * makeup;
      d[i] = x;

      // Update feedback tap with *post-GR, post-makeup* output
      // (keeps behaviour stable and closer to "the device output drives sidechain").
      y1[static_cast<size_t>(ch)] = x;
    }
  }

  // Activity: normalise GR amount for UI
  const float grAvgDb = grAcc / static_cast<float>(numSamples);
  activity01 = compressionEnabled ? clamp01(grAvgDb / 12.0f) : 0.0f; // 12dB -> “full” activity

  // Optional gentle tone shaping after compression
  applyBodyShelf(buffer);
}

void OptoCompressorLA2A::applyBodyShelf(juce::AudioBuffer<float>& buffer) noexcept {
  const int numChannels = std::min(channels, buffer.getNumChannels());
  const int numSamples  = buffer.getNumSamples();
  if (numChannels <= 0 || numSamples <= 0) return;

  const float b = clamp01(params.body);
  const float delta = (b - 0.5f) * 2.0f; // -1..+1

  // Keep it subtle: +/- 1.5 dB max
  const float lowGainDb = 1.5f * delta;
  const float lowGain = dbToGain(lowGainDb);

  // Low split around 180 Hz
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
