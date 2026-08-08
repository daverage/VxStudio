#pragma once

#include <juce_core/juce_core.h>
#include <algorithm>
#include <cmath>

// Full mono/phase-risk management (VXWIDTH_BUILD.md §11, 2026-08-08). The
// existing correlation-based restraints (monoRiskRestraint: broadband input
// correlation; MonoDownmixGuardrail: processed-vs-input downmix RMS, in
// VxWidthLoudnessCompensator.h) already cover §11.1's "preserve Mid, limit
// unstable Side, compare processed/unprocessed downmixes" at a broadband
// level. §11.2 asks for more: coherence BY PERCEPTUAL BAND, downmix spectral
// deviation (not just RMS), low-frequency-specific cancellation, centre-image
// displacement, and sustained negative-correlation duration (not just
// instantaneous depth). This class adds those, using cheap one-pole
// crossovers (no FFT, zero added latency - §13's Live-mode target stays
// intact) rather than a full analysis filterbank.
//
// §11.1 explicitly forbids claiming to CORRECT phase - this only ever
// RESTRAINS (multiplies existing-Side/decorrelated/DoubleSide amounts
// toward zero), same role as the existing restraints it supplements.
//
// Accumulation shape matches the rest of VxWidthProcessor.cpp's per-block
// measurements (e.g. the lag-correlation sums feeding ContentPredictability
// Restraint): accumulateSample() is called once per sample from INSIDE the
// existing main loop (the original pre-processing L/R and final post-
// processing L/R are both live there; by the time a whole-block pass could
// run afterward, the buffer has already been overwritten in place), then
// updateForNextBlock() converts the block's accumulated sums into the
// restraint applied to the NEXT block.
namespace vxsuite::width {

// Two cascaded one-pole crossovers split a signal into low/mid/high bands.
// Fixed crossover points (150Hz, 2500Hz) rather than user-configurable -
// this is a safety measurement, not a spectral shaping tool.
class ThreeBandSplitter {
public:
    void prepare(const double sampleRate) noexcept {
        const float sr = static_cast<float>(sampleRate > 1000.0 ? sampleRate : 48000.0);
        lowAlpha = std::exp(-2.0f * juce::MathConstants<float>::pi * 150.0f / sr);
        highAlpha = std::exp(-2.0f * juce::MathConstants<float>::pi * 2500.0f / sr);
        reset();
    }
    void reset() noexcept { lowState = 0.0f; highLowpassState = 0.0f; }

    struct Bands { float low, mid, high; };

    Bands process(const float x) noexcept {
        lowState += (x - lowState) * (1.0f - lowAlpha);
        const float low = lowState;
        highLowpassState += (x - highLowpassState) * (1.0f - highAlpha);
        const float high = x - highLowpassState;
        const float mid = x - low - high;
        return { low, mid, high };
    }

private:
    float lowAlpha = 0.99f;
    float highAlpha = 0.9f;
    float lowState = 0.0f;
    float highLowpassState = 0.0f;
};

class PhaseRiskGuardrail {
public:
    void prepare(const double sampleRate) noexcept {
        sr = sampleRate > 1000.0 ? sampleRate : 48000.0;
        splitterL.prepare(sr);
        splitterR.prepare(sr);
        splitterInMono.prepare(sr);
        splitterOutMono.prepare(sr);
        // ~400ms - distinct from the other restraints' own time constants
        // (deliberately not reused, same reasoning as every other restraint
        // in this file), smooths this restraint's own block-to-block motion.
        blockAlphaPerSample = std::exp(-1.0f / (0.4f * static_cast<float>(sr)));
        reset();
    }

    void reset() noexcept {
        splitterL.reset();
        splitterR.reset();
        splitterInMono.reset();
        splitterOutMono.reset();
        bandCoherenceRestraint = 1.0f;
        downmixSpectralRestraint = 1.0f;
        centreDisplacementRestraint = 1.0f;
        sustainedRiskRestraint = 1.0f;
        negativeCorrelationStreakBlocks = 0;
        clearAccumulators();
    }

    // Combined restraint (0=fully restrained, 1=unrestrained) - the MOST
    // restrictive of the four measurements below, matching how a safety
    // system should behave (any one serious issue restrains, they don't
    // average out against each other).
    [[nodiscard]] float currentRestraint() const noexcept {
        return juce::jmin(bandCoherenceRestraint, downmixSpectralRestraint,
            juce::jmin(centreDisplacementRestraint, sustainedRiskRestraint));
    }

    // Call once per sample, from inside the main processing loop, with the
    // ORIGINAL (pre-processing) input L/R and the FINAL (post-processing)
    // output L/R for that same sample.
    void accumulateSample(const float inL, const float inR, const float outL, const float outR) noexcept {
        const auto bl = splitterL.process(inL);
        const auto br = splitterR.process(inR);
        sumLL[0] += static_cast<double>(bl.low) * bl.low;   sumRR[0] += static_cast<double>(br.low) * br.low;   sumLR[0] += static_cast<double>(bl.low) * br.low;
        sumLL[1] += static_cast<double>(bl.mid) * bl.mid;   sumRR[1] += static_cast<double>(br.mid) * br.mid;   sumLR[1] += static_cast<double>(bl.mid) * br.mid;
        sumLL[2] += static_cast<double>(bl.high) * bl.high; sumRR[2] += static_cast<double>(br.high) * br.high; sumLR[2] += static_cast<double>(bl.high) * br.high;

        // §11.2 "downmix spectral deviation": band-split the MONO DOWNMIX of
        // input vs output, compare per-band energy - catches processing that
        // reshapes the mono-compatible spectrum even when broadband RMS
        // (MonoDownmixGuardrail's own measure) looks fine.
        const auto inBands = splitterInMono.process(inL + inR);
        const auto outBands = splitterOutMono.process(outL + outR);
        inMonoLow += static_cast<double>(inBands.low) * inBands.low;
        inMonoMid += static_cast<double>(inBands.mid) * inBands.mid;
        inMonoHigh += static_cast<double>(inBands.high) * inBands.high;
        outMonoLow += static_cast<double>(outBands.low) * outBands.low;
        outMonoMid += static_cast<double>(outBands.mid) * outBands.mid;
        outMonoHigh += static_cast<double>(outBands.high) * outBands.high;

        inLRms += static_cast<double>(inL) * inL;
        inRRms += static_cast<double>(inR) * inR;
        outLRms += static_cast<double>(outL) * outL;
        outRRms += static_cast<double>(outR) * outR;
    }

    // Call once per block (after all accumulateSample() calls for that
    // block) with the framework's own broadband correlation (already
    // computed elsewhere for monoRiskRestraint - reused here, not
    // recomputed, purely for the sustained-negative-duration tracker).
    // Updates the restraint applied to the NEXT block.
    void updateForNextBlock(const float broadbandCorrelation, const int numSamples) noexcept {
        if (numSamples <= 0)
            return;

        // §11.2 "coherence by perceptual band", on the INPUT (proactive -
        // matches monoRiskRestraint's own input-only convention).
        float minBandCorrelation = 1.0f;
        for (int b = 0; b < 3; ++b) {
            const double denom = sumLL[b] * sumRR[b];
            if (denom < 1.0e-12)
                continue;
            const float corr = juce::jlimit(-1.0f, 1.0f, static_cast<float>(sumLR[b] / std::sqrt(denom)));
            const float bandRestraint = juce::jlimit(0.0f, 1.0f, (corr + 1.0f) * 0.5f);
            // Low band weighted: squaring its restraint reads the SAME
            // correlation as more restrictive in the low band specifically
            // (§11.1/§10.3's low-frequency emphasis), without a second
            // separate mechanism.
            const float weighted = b == 0 ? bandRestraint * bandRestraint : bandRestraint;
            minBandCorrelation = std::min(minBandCorrelation, weighted);
        }

        // Spectral deviation: log-ratio of processed vs input band energy,
        // worst band wins. Knee at 3dB (matches the "no obvious gain jump"
        // spirit of §12) - normal, expected level differences from Width/
        // Double don't trip this; only a genuinely reshaped downmix does.
        auto bandDeviationDb = [](const double inSq, const double outSq) {
            if (inSq < 1.0e-9)
                return 0.0f;
            const float ratio = static_cast<float>(std::sqrt(juce::jmax(1.0e-12, outSq) / inSq));
            return std::abs(juce::Decibels::gainToDecibels(ratio, -60.0f));
        };
        const float maxDeviationDb = std::max({ bandDeviationDb(inMonoLow, outMonoLow),
                                                  bandDeviationDb(inMonoMid, outMonoMid),
                                                  bandDeviationDb(inMonoHigh, outMonoHigh) });
        constexpr float kKneeDb = 3.0f, kFullRestraintDb = 12.0f;
        const float targetSpectralRestraint = 1.0f - juce::jlimit(0.0f, 1.0f,
            (maxDeviationDb - kKneeDb) / (kFullRestraintDb - kKneeDb));

        // §11.2 "centre-image displacement": pan centroid shift between
        // input and output. Small tolerance (knee) before restraining -
        // ordinary Width/Double processing legitimately moves this a little.
        auto centroid = [](const double lSq, const double rSq) {
            const double l = std::sqrt(lSq), r = std::sqrt(rSq);
            return (l + r) > 1.0e-9 ? static_cast<float>((l - r) / (l + r)) : 0.0f;
        };
        const float displacement = std::abs(centroid(outLRms, outRRms) - centroid(inLRms, inRRms));
        constexpr float kDisplacementKnee = 0.08f, kDisplacementFull = 0.30f;
        const float targetDisplacementRestraint = 1.0f - juce::jlimit(0.0f, 1.0f,
            (displacement - kDisplacementKnee) / (kDisplacementFull - kDisplacementKnee));

        // §11.2 "short-term negative-correlation duration": instantaneous
        // dips are already handled by monoRiskRestraint's own smoothing;
        // this specifically penalises SUSTAINED anti-phase content that
        // stays negative block after block, which a single smoothed scalar
        // can under-restrain if it oscillates around the threshold.
        if (broadbandCorrelation < 0.0f)
            negativeCorrelationStreakBlocks = std::min(negativeCorrelationStreakBlocks + 1, 1000);
        else
            negativeCorrelationStreakBlocks = 0;
        const float streakSeconds = negativeCorrelationStreakBlocks * static_cast<float>(numSamples) / static_cast<float>(sr);
        constexpr float kStreakKneeSeconds = 0.15f, kStreakFullSeconds = 1.0f;
        const float targetSustainedRestraint = 1.0f - juce::jlimit(0.0f, 1.0f,
            (streakSeconds - kStreakKneeSeconds) / (kStreakFullSeconds - kStreakKneeSeconds));

        const float blockAlpha = std::pow(blockAlphaPerSample, static_cast<float>(std::max(1, numSamples)));
        bandCoherenceRestraint = blockAlpha * bandCoherenceRestraint + (1.0f - blockAlpha) * minBandCorrelation;
        downmixSpectralRestraint = blockAlpha * downmixSpectralRestraint + (1.0f - blockAlpha) * targetSpectralRestraint;
        centreDisplacementRestraint = blockAlpha * centreDisplacementRestraint + (1.0f - blockAlpha) * targetDisplacementRestraint;
        sustainedRiskRestraint = blockAlpha * sustainedRiskRestraint + (1.0f - blockAlpha) * targetSustainedRestraint;

        clearAccumulators();
    }

    // Development visibility - which specific measurement is currently
    // driving the combined restraint, not just the final scalar.
    [[nodiscard]] float bandCoherence() const noexcept { return bandCoherenceRestraint; }
    [[nodiscard]] float downmixSpectral() const noexcept { return downmixSpectralRestraint; }
    [[nodiscard]] float centreDisplacement() const noexcept { return centreDisplacementRestraint; }
    [[nodiscard]] float sustainedRisk() const noexcept { return sustainedRiskRestraint; }

private:
    void clearAccumulators() noexcept {
        for (int b = 0; b < 3; ++b) { sumLL[b] = sumRR[b] = sumLR[b] = 0.0; }
        inMonoLow = inMonoMid = inMonoHigh = 0.0;
        outMonoLow = outMonoMid = outMonoHigh = 0.0;
        inLRms = inRRms = outLRms = outRRms = 0.0;
    }

    double sr = 48000.0;
    ThreeBandSplitter splitterL, splitterR, splitterInMono, splitterOutMono;
    float blockAlphaPerSample = 0.999999f;
    float bandCoherenceRestraint = 1.0f;
    float downmixSpectralRestraint = 1.0f;
    float centreDisplacementRestraint = 1.0f;
    float sustainedRiskRestraint = 1.0f;
    int negativeCorrelationStreakBlocks = 0;

    double sumLL[3] {}, sumRR[3] {}, sumLR[3] {};
    double inMonoLow = 0.0, inMonoMid = 0.0, inMonoHigh = 0.0;
    double outMonoLow = 0.0, outMonoMid = 0.0, outMonoHigh = 0.0;
    double inLRms = 0.0, inRRms = 0.0, outLRms = 0.0, outRRms = 0.0;
};

} // namespace vxsuite::width
