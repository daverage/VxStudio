// Verifies VX Width build-order step 1 (docs/Task Based/VXWIDTH_BUILD.md §28):
// exact null at neutral settings (§5.2, §26.1) and stable mono collapse
// at Width=-100 (§26.2).
#include "../Source/vxstudio/products/width/VxWidthProcessor.h"
#include "../Source/vxstudio/framework/VxStudioEditorBase.h"
#include "VxStudioProcessorTestUtils.h"
#include "VxWidthCorpusGenerator.h"
#include <atomic>
#include <cmath>
#include <iostream>
#include <memory>
#include <new>

using namespace vxsuite::test;

namespace {
// Anti-phase stereo (R = -L): broadband correlation ~ -1, the mono-risk
// case §7.2 says Region B expansion must restrain against.
juce::AudioBuffer<float> makeAntiPhase(const double sampleRate, const float seconds) {
    auto buf = makeSpeechLike(sampleRate, seconds);
    for (int i = 0; i < buf.getNumSamples(); ++i)
        buf.setSample(1, i, -buf.getSample(0, i));
    return buf;
}

// Audio-thread allocation tracking for the EXPERIMENTAL micro-pitch path
// (mirrors the pattern in tests/VXStudioPluginRegressionTests.cpp - this
// executable doesn't otherwise have global new/delete overrides).
std::atomic<bool> gAllocationTrackingEnabled { false };
std::atomic<int> gTrackedAllocations { 0 };

struct AllocationScope {
    AllocationScope() {
        gTrackedAllocations.store(0, std::memory_order_relaxed);
        gAllocationTrackingEnabled.store(true, std::memory_order_relaxed);
    }
    ~AllocationScope() {
        gAllocationTrackingEnabled.store(false, std::memory_order_relaxed);
    }
    [[nodiscard]] int allocations() const noexcept {
        return gTrackedAllocations.load(std::memory_order_relaxed);
    }
};
} // namespace

void* operator new(std::size_t size) {
    if (gAllocationTrackingEnabled.load(std::memory_order_relaxed))
        gTrackedAllocations.fetch_add(1, std::memory_order_relaxed);
    if (void* p = std::malloc(size))
        return p;
    throw std::bad_alloc();
}
void operator delete(void* ptr) noexcept { std::free(ptr); }
void operator delete(void* ptr, std::size_t) noexcept { std::free(ptr); }
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete[](void* ptr) noexcept { std::free(ptr); }
void operator delete[](void* ptr, std::size_t) noexcept { std::free(ptr); }

int main() {
    constexpr double sr = 48000.0;
    std::cout << "=== VXWidth Shell Check ===\n";
    bool allPass = true;

    // --- Test 1: neutral null (Width=0, Double=0) ---
    {
        VXWidthAudioProcessor proc;
        proc.prepareToPlay(sr, 256);

        auto input = makeSpeechLike(sr, 3.0f);
        render(proc, input, 256); // warm up smoothing
        auto out = render(proc, input, 256);

        const float diff = maxAbsDiff(input, out);
        const bool ok = diff < 1e-4f;
        allPass &= ok;
        std::cout << "  [neutral null] maxAbsDiff=" << diff
                  << (ok ? "  PASS" : "  FAIL") << "\n";
    }

    // --- Test 2: mono collapse (Width=-100) ---
    {
        VXWidthAudioProcessor proc;
        proc.prepareToPlay(sr, 256);
        setParamNormalized(proc, "width", 0.0f); // 0.0 -> -100

        auto input = makeSpeechLike(sr, 3.0f);
        render(proc, input, 256);
        auto out = render(proc, input, 256);

        float maxLR = 0.0f;
        for (int i = 0; i < out.getNumSamples(); ++i)
            maxLR = std::max(maxLR, std::abs(out.getSample(0, i) - out.getSample(1, i)));
        const bool ok = maxLR < 1e-5f;
        allPass &= ok;
        std::cout << "  [mono collapse] maxL-R=" << maxLR
                  << (ok ? "  PASS" : "  FAIL") << "\n";
    }

    // --- Test 3: Region B expansion widens (Width=+20) ---
    {
        VXWidthAudioProcessor proc;
        proc.prepareToPlay(sr, 256);
        setParamNormalized(proc, "width", 0.5f + 20.0f / 200.0f); // -> +20

        auto input = makeSpeechLike(sr, 3.0f);
        render(proc, input, 256);
        auto out = render(proc, input, 256);

        auto sideEnergy = [](const juce::AudioBuffer<float>& b) {
            double e = 0.0;
            for (int i = 0; i < b.getNumSamples(); ++i) {
                const float s = b.getSample(0, i) - b.getSample(1, i);
                e += static_cast<double>(s) * s;
            }
            return e;
        };
        const double sideIn = sideEnergy(input);
        const double sideOut = sideEnergy(out);
        const bool ok = sideOut > sideIn * 1.05;
        allPass &= ok;
        std::cout << "  [Region B expand] sideEnergy in=" << sideIn << " out=" << sideOut
                  << (ok ? "  PASS (wider)" : "  FAIL") << "\n";
    }

    // --- Test 4: mono-risk restraint - anti-phase input widens less than
    // in-phase input at the same Width (§7.2, §11 preview) ---
    {
        auto sideEnergy = [](const juce::AudioBuffer<float>& b) {
            double e = 0.0;
            for (int i = 0; i < b.getNumSamples(); ++i) {
                const float s = b.getSample(0, i) - b.getSample(1, i);
                e += static_cast<double>(s) * s;
            }
            return e;
        };

        VXWidthAudioProcessor safeProc;
        safeProc.prepareToPlay(sr, 256);
        setParamNormalized(safeProc, "width", 0.5f + 20.0f / 200.0f);
        auto safeInput = makeSpeechLike(sr, 3.0f);
        render(safeProc, safeInput, 256);
        auto safeOut = render(safeProc, safeInput, 256);
        const double safeRatio = sideEnergy(safeOut) / std::max(1e-9, sideEnergy(safeInput));

        VXWidthAudioProcessor riskyProc;
        riskyProc.prepareToPlay(sr, 256);
        setParamNormalized(riskyProc, "width", 0.5f + 20.0f / 200.0f);
        auto riskyInput = makeAntiPhase(sr, 3.0f);
        render(riskyProc, riskyInput, 256); // prime the correlation smoother
        render(riskyProc, riskyInput, 256);
        auto riskyOut = render(riskyProc, riskyInput, 256);
        const double riskyRatio = sideEnergy(riskyOut) / std::max(1e-9, sideEnergy(riskyInput));

        // Ratio (not absolute delta), since anti-phase input already starts
        // with far more side energy than the near-mono-in-side speech signal -
        // what matters is how much EACH signal's own side content is boosted.
        const bool ok = riskyRatio < safeRatio - 0.05;
        allPass &= ok;
        std::cout << "  [mono-risk restraint] safeRatio=" << safeRatio << " riskyRatio=" << riskyRatio
                  << (ok ? "  PASS (restrained)" : "  FAIL") << "\n";
    }

    // --- Test 5: Region C widens beyond the Region B ceiling (§7.3) ---
    {
        auto sideEnergy = [](const juce::AudioBuffer<float>& b) {
            double e = 0.0;
            for (int i = 0; i < b.getNumSamples(); ++i) {
                const float s = b.getSample(0, i) - b.getSample(1, i);
                e += static_cast<double>(s) * s;
            }
            return e;
        };

        VXWidthAudioProcessor ceilingProc;
        ceilingProc.prepareToPlay(sr, 256);
        setParamNormalized(ceilingProc, "width", 0.5f + 35.0f / 200.0f); // Region B ceiling
        auto input = makeSpeechLike(sr, 3.0f);
        render(ceilingProc, input, 256);
        const double sideAtCeiling = sideEnergy(render(ceilingProc, input, 256));

        VXWidthAudioProcessor deepProc;
        deepProc.prepareToPlay(sr, 256);
        setParamNormalized(deepProc, "width", 1.0f); // +100, deep Region C
        render(deepProc, input, 256);
        auto deepOut = render(deepProc, input, 256);
        const double sideAtMax = sideEnergy(deepOut);

        const bool widensOk = sideAtMax > sideAtCeiling * 1.1;
        // §12: output safety stage must keep peak level bounded regardless of
        // how much synthetic width Region C adds from near-mono material.
        float peakOut = 0.0f;
        for (int ch = 0; ch < deepOut.getNumChannels(); ++ch)
            peakOut = std::max(peakOut, deepOut.getMagnitude(ch, 0, deepOut.getNumSamples()));
        const bool peakOk = peakOut <= 0.99f;
        const bool ok = widensOk && peakOk;
        allPass &= ok;
        std::cout << "  [Region C beyond ceiling] sideAtCeiling(+35)=" << sideAtCeiling
                  << " sideAtMax(+100)=" << sideAtMax << " peakOut=" << peakOut
                  << (ok ? "  PASS (Region C adds width, peak bounded)" : "  FAIL") << "\n";
    }

    // --- Test: Double engine (§8) adds audible content, stays finite/bounded
    // over a longer render (stochastic-walker + delay-line stability) ---
    {
        VXWidthAudioProcessor proc;
        proc.prepareToPlay(sr, 256);
        setParamNormalized(proc, "width", 0.5f);  // Width=0, isolate Double's own effect
        setParamNormalized(proc, "double", 1.0f); // Double=100

        auto input = makeSpeechLike(sr, 8.0f); // longer render for walker stability
        render(proc, input, 256);
        auto out = render(proc, input, 256);

        const float diff = maxAbsDiff(input, out);
        bool finiteOk = true;
        float peakOut = 0.0f;
        for (int ch = 0; ch < out.getNumChannels(); ++ch) {
            for (int i = 0; i < out.getNumSamples(); ++i) {
                const float s = out.getSample(ch, i);
                if (!std::isfinite(s))
                    finiteOk = false;
            }
            peakOut = std::max(peakOut, out.getMagnitude(ch, 0, out.getNumSamples()));
        }
        const bool audibleOk = diff > 0.01f;
        const bool boundedOk = peakOut <= 0.99f;
        const bool ok = finiteOk && audibleOk && boundedOk;
        allPass &= ok;
        std::cout << "  [Double engine] maxAbsDiff=" << diff << " peakOut=" << peakOut
                  << " finite=" << finiteOk
                  << (ok ? "  PASS (audible, finite, bounded)" : "  FAIL") << "\n";
    }

    // --- Test: ADT delay slew clamp keeps instantaneous Doppler pitch shift
    // inside each tightness band's intended budget (Tight ~3c, Natural ~6c,
    // Loose ~10c - see VxWidthAdtVoice.h's kMsPerSecPerCent derivation).
    // Measures the ACTUAL max via lastInstantaneousPitchShiftCents(), not
    // just trusting the clamp math - proves the fix, doesn't assume it. ---
    {
        struct Band { const char* name; float tightnessNorm; float maxBudgetCents; };
        const Band bands[] = {
            { "Tight",   0.0f, 3.0f },
            { "Natural", 0.5f, 6.0f },
            { "Loose",   1.0f, 10.0f },
        };
        bool ok = true;
        for (const auto& band : bands) {
            VXWidthAudioProcessor proc;
            proc.prepareToPlay(sr, 256);
            setParamNormalized(proc, "width", 0.5f);
            setParamNormalized(proc, "double", 1.0f);
            setParamNormalized(proc, "tightness", band.tightnessNorm);

            auto input = makeSpeechLike(sr, 10.0f); // long enough to hit several walker retargets
            render(proc, input, 256);
            render(proc, input, 256);

            const float observedCents = proc.getAdtMaxPitchShiftCentsObserved();
            // Small tolerance for float rounding in the slope-to-cents conversion.
            const bool bandOk = observedCents <= band.maxBudgetCents + 0.25f;
            ok &= bandOk;
            std::cout << "    " << band.name << " budget=" << band.maxBudgetCents
                      << "c observed=" << observedCents << "c"
                      << (bandOk ? "  PASS" : "  FAIL") << "\n";
        }
        allPass &= ok;
        std::cout << "  [ADT Doppler pitch-shift clamp] " << (ok ? "PASS" : "FAIL") << "\n";
    }

    // --- Test: Double is driven by centre-confidence, not mono-confidence
    // (user-requested spec refinement) - a centred source should get more
    // doubling than the same material hard-panned, even though hard-panned
    // material is just as "stereo" by any mono/side-energy measure. ---
    {
        auto makeHardPanned = [](double sampleRate, float seconds) {
            auto buf = makeSpeechLike(sampleRate, seconds);
            for (int i = 0; i < buf.getNumSamples(); ++i)
                buf.setSample(1, i, 0.0f); // R silent - fully panned left
            return buf;
        };
        auto makeCentred = [](double sampleRate, float seconds) {
            auto buf = makeSpeechLike(sampleRate, seconds);
            for (int i = 0; i < buf.getNumSamples(); ++i)
                buf.setSample(1, i, buf.getSample(0, i)); // R := L
            return buf;
        };

        VXWidthAudioProcessor pannedProc;
        pannedProc.prepareToPlay(sr, 256);
        setParamNormalized(pannedProc, "width", 0.5f);
        setParamNormalized(pannedProc, "double", 1.0f);
        auto pannedInput = makeHardPanned(sr, 6.0f);
        render(pannedProc, pannedInput, 256);
        auto pannedOut = render(pannedProc, pannedInput, 256);
        const float pannedDiff = maxAbsDiff(pannedInput, pannedOut);

        VXWidthAudioProcessor centredProc;
        centredProc.prepareToPlay(sr, 256);
        setParamNormalized(centredProc, "width", 0.5f);
        setParamNormalized(centredProc, "double", 1.0f);
        auto centredInput = makeCentred(sr, 6.0f);
        render(centredProc, centredInput, 256);
        auto centredOut = render(centredProc, centredInput, 256);
        const float centredDiff = maxAbsDiff(centredInput, centredOut);

        const bool ok = pannedDiff < centredDiff * 0.6f;
        allPass &= ok;
        std::cout << "  [Double centre-confidence gate] pannedDiff=" << pannedDiff
                  << " centredDiff=" << centredDiff
                  << (ok ? "  PASS (panned material doubled less)" : "  FAIL") << "\n";
    }

    // --- Test: L/R loudness balance (user-reported symptom: "left channel
    // significantly louder than right when it adds stereo width to mono").
    //
    // METHODOLOGY NOTE (2026-08-07): this test originally gated on ONE
    // narrow synthetic fixture (makeSpeechLike). Investigating a real
    // regression here proved that fixture was not representative - a
    // corpus-wide, holdout-validated ADT-seed/decorrelator-tap search
    // (tests/VXWidthAdtSeedOptimizer.cpp) materially improved the AVERAGE
    // imbalance across 11 diverse content categories, confirmed on a
    // disjoint holdout corpus, while making THIS ONE fixture's own number
    // worse - proof the old single-fixture gate was measuring the wrong
    // thing. Replaced with: average + worst-case imbalance across a small
    // REPRESENTATIVE corpus subset (deterministic procedural synthesis,
    // not the full optimizer corpus - kept small enough to run as a normal
    // regression check), gating on both so a good mean can't hide one badly
    // biased content type. The original makeSpeechLike fixture is still
    // measured and printed for historical visibility, but does NOT gate. ---
    {
        auto channelEnergy = [](const juce::AudioBuffer<float>& b, int ch) {
            double e = 0.0;
            for (int i = 0; i < b.getNumSamples(); ++i) {
                const float s = b.getSample(ch, i);
                e += static_cast<double>(s) * s;
            }
            return e;
        };
        auto imbalanceFor = [&](const juce::AudioBuffer<float>& trueMono, const float widthNorm, const float doubleNorm) {
            VXWidthAudioProcessor proc;
            proc.prepareToPlay(sr, 256);
            setParamNormalized(proc, "width", widthNorm);
            setParamNormalized(proc, "double", doubleNorm);
            render(proc, trueMono, 256);
            auto out = render(proc, trueMono, 256);
            const double l = channelEnergy(out, 0);
            const double r = channelEnergy(out, 1);
            return std::abs(l - r) / std::max(l, r);
        };

        // Representative subset: 6 of the 11 optimizer corpus categories,
        // spanning distinct spectral character (tonal speech, sustained
        // vibrato vocal, harmonic-transient guitar, percussive transients,
        // broadband noise) - deliberately not the same seed range as the
        // optimizer's own training (1000+) or holdout (9000+) pools, so
        // this regression check is independent of both.
        using namespace vxsuite::width::corpus;
        constexpr double repSr = 48000.0;
        constexpr float repSeconds = 6.0f;
        constexpr std::uint32_t repSeedBase = 5000u;
        struct RepItem { const char* label; juce::AudioBuffer<float> mono; };
        std::vector<RepItem> repItems;
        repItems.push_back({ "maleSpeech", makeMaleSpeech(repSr, repSeconds, repSeedBase + 0) });
        repItems.push_back({ "femaleSpeech", makeFemaleSpeech(repSr, repSeconds, repSeedBase + 1) });
        repItems.push_back({ "sungVocal", makeSungVocal(repSr, repSeconds, repSeedBase + 2) });
        repItems.push_back({ "guitarDist", makeGuitar(repSr, repSeconds, repSeedBase + 3, true) });
        repItems.push_back({ "drums", makeDrums(repSr, repSeconds, repSeedBase + 4) });
        repItems.push_back({ "pinkNoise", makePinkNoise(repSr, repSeconds, repSeedBase + 5) });

        double widthSum = 0.0, widthMax = 0.0, doubleSum = 0.0, doubleMax = 0.0;
        for (const auto& item : repItems) {
            const double wImb = imbalanceFor(item.mono, 1.0f, 0.0f);   // Width=+100, Double=0
            const double dImb = imbalanceFor(item.mono, 0.5f, 1.0f);   // Width=0, Double=100
            widthSum += wImb; widthMax = std::max(widthMax, wImb);
            doubleSum += dImb; doubleMax = std::max(doubleMax, dImb);
        }
        const double widthAvg = widthSum / repItems.size();
        const double doubleAvg = doubleSum / repItems.size();

        // Thresholds derived from measuring the actual production config
        // across this exact representative set (2026-08-07). Two
        // generations of evidence: (1) seeds/taps alone (0x1234/0x2222,
        // original decorrelator taps, no orthogonaliser): widthAvg=0.111,
        // widthMax=0.281, doubleAvg=0.046, doubleMax=0.151 - a corpus-wide
        // seed/tap search found an "improvement" that evaporated end-to-end
        // (see VxWidthProcessor.cpp's ADT-seed comment), so NOT used to
        // justify a looser threshold. (2) SideOrthogonalizer added (sparse
        // adaptive NLMS predictor + hard cancellation-fraction ceiling, see
        // VxWidthSideOrthogonalizer.h): widthAvg=0.074, widthMax=0.187,
        // doubleAvg=0.030, doubleMax=0.094 - a real, bounded improvement
        // (confirmed non-degenerate: 72% of generated-Side RMS retained,
        // 2.7dB energy reduction, not the 90%-removed/26dB failure mode an
        // earlier unconstrained version produced). Thresholds set with
        // headroom over THIS measured baseline, tight enough to catch a
        // real regression.
        constexpr double kAvgThreshold = 0.095;
        constexpr double kWorstCaseThreshold = 0.24;
        const bool ok = widthAvg < kAvgThreshold && doubleAvg < kAvgThreshold
                      && widthMax < kWorstCaseThreshold && doubleMax < kWorstCaseThreshold;
        allPass &= ok;
        std::cout << "  [L/R balance, representative corpus] widthAvg=" << widthAvg
                  << " widthMax=" << widthMax << " | doubleAvg=" << doubleAvg
                  << " doubleMax=" << doubleMax
                  << (ok ? "  PASS" : "  FAIL") << "\n";

        // --- Effect-strength check (user's explicit warning: "a system
        // producing 1% imbalance because it removed 80% of the generated
        // Side is not a success"). Measures orthogonalizer energy reduction
        // telemetry directly, plus generated-Side RMS with the
        // orthogonalizer bypassed vs active, on the same representative
        // corpus - reports honestly, does not gate (this is diagnostic
        // visibility for a prototype, not yet a pass/fail contract). ---
        {
            double sumReductionDb = 0.0;
            int reductionCount = 0;
            for (const auto& item : repItems) {
                VXWidthAudioProcessor proc;
                proc.prepareToPlay(repSr, 256);
                setParamNormalized(proc, "width", 0.5f);
                setParamNormalized(proc, "double", 1.0f);
                render(proc, item.mono, 256);
                render(proc, item.mono, 256);
                sumReductionDb += proc.getOrthogonalizerEnergyReductionDb();
                ++reductionCount;
            }
            const double avgReductionDb = sumReductionDb / reductionCount;

            auto sideRmsWithBypass = [&](const juce::AudioBuffer<float>& mono, const bool bypass) {
                VXWidthAudioProcessor proc;
                proc.prepareToPlay(repSr, 256);
                setParamNormalized(proc, "width", 0.5f);
                setParamNormalized(proc, "double", 1.0f);
                proc.setOrthogonalizerBypassed(bypass);
                render(proc, mono, 256);
                auto out = render(proc, mono, 256);
                double sumSq = 0.0;
                for (int i = 0; i < out.getNumSamples(); ++i) {
                    const float side = out.getSample(0, i) - out.getSample(1, i);
                    sumSq += static_cast<double>(side) * side;
                }
                return std::sqrt(sumSq / std::max(1, out.getNumSamples()));
            };
            const double sideRmsBypassed = sideRmsWithBypass(repItems[0].mono, true);
            const double sideRmsActive = sideRmsWithBypass(repItems[0].mono, false);
            const double effectRetainedRatio = sideRmsBypassed > 1e-12 ? sideRmsActive / sideRmsBypassed : 0.0;

            std::cout << "    [orthogonalizer effect-strength check] avgEnergyReductionDb=" << avgReductionDb
                      << " | sideRms bypassed=" << sideRmsBypassed << " active=" << sideRmsActive
                      << " retainedRatio=" << effectRetainedRatio
                      << (effectRetainedRatio < 0.5 ? "  WARNING: suppressing more than half the effect, not just the correlated part" : "") << "\n";
        }

        // Historical fixture, reported not gated - see methodology note above.
        auto legacyMono = makeSpeechLike(sr, 12.0f);
        for (int i = 0; i < legacyMono.getNumSamples(); ++i)
            legacyMono.setSample(1, i, legacyMono.getSample(0, i));
        const double legacyWidthImb = imbalanceFor(legacyMono, 1.0f, 0.0f);
        const double legacyDoubleImb = imbalanceFor(legacyMono, 0.5f, 1.0f);
        std::cout << "    [historical, non-gating] legacy makeSpeechLike fixture: widthImbalance="
                  << legacyWidthImb << " doubleImbalance=" << legacyDoubleImb << "\n";
    }

    // --- Test 6: silence stays silence ---
    {
        VXWidthAudioProcessor proc;
        proc.prepareToPlay(sr, 256);
        setParamNormalized(proc, "width", 1.0f); // max +100

        juce::AudioBuffer<float> silence(2, static_cast<int>(sr));
        silence.clear();
        render(proc, silence, 256);
        auto out = render(proc, silence, 256);

        float peak = 0.0f;
        for (int ch = 0; ch < out.getNumChannels(); ++ch)
            peak = std::max(peak, out.getMagnitude(ch, 0, out.getNumSamples()));
        const bool ok = peak < 1e-6f;
        allPass &= ok;
        std::cout << "  [silence] peak=" << peak
                  << (ok ? "  PASS" : "  FAIL") << "\n";
    }

    // --- Test: mono input bus -> stereo output bus (§15 required config) ---
    {
        VXWidthAudioProcessor proc;
        juce::AudioProcessor::BusesLayout layout;
        layout.inputBuses.add(juce::AudioChannelSet::mono());
        layout.outputBuses.add(juce::AudioChannelSet::stereo());
        const bool layoutOk = proc.setBusesLayout(layout);
        proc.prepareToPlay(sr, 256);
        setParamNormalized(proc, "width", 1.0f); // deep Region C: should synthesize real width

        const int n = static_cast<int>(sr * 3.0);
        juce::AudioBuffer<float> input(2, n); // buffer sized to output channel count (2)
        auto mono = makeSpeechLike(sr, 3.0f);
        input.copyFrom(0, 0, mono, 0, 0, n);
        // Deliberately fill channel 1 with UNRELATED noise, not silence/zero -
        // if the mono->stereo duplication is missing or buggy, this garbage
        // would leak straight into the output instead of being overwritten.
        auto noise = makeNoise(sr, 3.0f, 0.3f);
        input.copyFrom(1, 0, noise, 0, 0, n);

        auto out = render(proc, input, 256);
        // Compare against a genuinely-mono reference run at the same settings.
        VXWidthAudioProcessor refProc;
        refProc.prepareToPlay(sr, 256);
        setParamNormalized(refProc, "width", 1.0f);
        juce::AudioBuffer<float> trueMonoInput(2, n);
        trueMonoInput.copyFrom(0, 0, mono, 0, 0, n);
        trueMonoInput.copyFrom(1, 0, mono, 0, 0, n);
        auto refOut = render(refProc, trueMonoInput, 256);

        const float diffFromReference = maxAbsDiff(out, refOut);
        const bool ok = layoutOk && diffFromReference < 1e-4f;
        allPass &= ok;
        std::cout << "  [mono-in/stereo-out bus] layoutAccepted=" << layoutOk
                  << " diffFromTrueMonoReference=" << diffFromReference
                  << (ok ? "  PASS (channel 1 garbage correctly overwritten)" : "  FAIL") << "\n";
    }

    // --- Test: latency reporting (§13.2) - no lookahead anywhere in the
    // signal path, so reported latency must be exactly 0. ---
    {
        VXWidthAudioProcessor proc;
        proc.prepareToPlay(sr, 256);
        const int latency = proc.getLatencySamples();
        const bool ok = latency == 0;
        allPass &= ok;
        std::cout << "  [latency reporting] latencySamples=" << latency
                  << (ok ? "  PASS" : "  FAIL") << "\n";
    }

    // --- Test: sample-rate / block-size coverage (§14) - neutral null and
    // finite output must hold across the required SR/block-size matrix. ---
    {
        const double sampleRates[] = { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 };
        const int blockSizes[] = { 16, 32, 64, 128, 256, 512, 1024, 2048 };
        bool ok = true;
        for (const double testSr : sampleRates) {
            for (const int block : blockSizes) {
                VXWidthAudioProcessor proc;
                proc.prepareToPlay(testSr, block);
                auto input = makeSpeechLike(testSr, 0.5f);
                render(proc, input, block);
                auto out = render(proc, input, block);
                const float diff = maxAbsDiff(input, out);
                bool finiteOk = true;
                for (int ch = 0; ch < out.getNumChannels() && finiteOk; ++ch)
                    for (int i = 0; i < out.getNumSamples() && finiteOk; ++i)
                        if (!std::isfinite(out.getSample(ch, i)))
                            finiteOk = false;
                if (diff > 1e-3f || !finiteOk) {
                    ok = false;
                    std::cout << "    FAIL at sr=" << testSr << " block=" << block
                              << " diff=" << diff << " finite=" << finiteOk << "\n";
                }
            }
        }
        allPass &= ok;
        std::cout << "  [SR/block-size coverage] " << (ok ? "PASS (all combinations neutral-null + finite)" : "FAIL") << "\n";
    }

    // --- Test: editor (§28 step 7) constructs from ProductIdentity without
    // crashing, and exposes all four knobs (§3.2/§10 - Focus must not be
    // hidden as an "expert" control, it's one of the four headline knobs). ---
    {
        juce::ScopedJuceInitialiser_GUI guiInit;
        VXWidthAudioProcessor proc;
        proc.prepareToPlay(sr, 256);
        std::unique_ptr<juce::AudioProcessorEditor> editorBase(proc.createEditor());
        auto* editor = dynamic_cast<vxsuite::EditorBase*>(editorBase.get());
        const bool ok = editor != nullptr
            && proc.getValueTreeState().getParameter("width") != nullptr
            && proc.getValueTreeState().getParameter("double") != nullptr
            && proc.getValueTreeState().getParameter("tightness") != nullptr
            && proc.getValueTreeState().getParameter("focus") != nullptr;
        allPass &= ok;
        std::cout << "  [editor + 4 knobs] " << (ok ? "PASS" : "FAIL") << "\n";
    }

    // --- Test: factory presets (§20) apply only the four knobs, all indices
    // land on distinct, in-range values. ---
    {
        VXWidthAudioProcessor presetProc;
        const auto& identity = presetProc.getProductIdentity();
        bool ok = identity.supportsPresetSelector() && identity.presetChoiceCount == 6;
        for (int i = 0; i < identity.clampedPresetChoiceCount() && ok; ++i) {
            ok = !identity.presetChoiceLabel(static_cast<size_t>(i)).empty()
              && identity.presetPrimaryValues[i]   >= 0.0f && identity.presetPrimaryValues[i]   <= 1.0f
              && identity.presetSecondaryValues[i] >= 0.0f && identity.presetSecondaryValues[i] <= 1.0f
              && identity.presetTertiaryValues[i]  >= 0.0f && identity.presetTertiaryValues[i]  <= 1.0f
              && identity.presetQuaternaryValues[i] >= 0.0f && identity.presetQuaternaryValues[i] <= 1.0f;
        }
        allPass &= ok;
        std::cout << "  [factory presets] count=" << identity.presetChoiceCount
                  << (ok ? "  PASS" : "  FAIL") << "\n";
    }

    // --- Test: MonoDownmixGuardrail (§11.3) restrains on severe mono-energy
    // loss and recovers when the output stops degrading - a real audio
    // scenario that reliably drives the FINAL mono downmix down (as
    // opposed to just the input's own correlation, already covered by
    // monoRiskRestraint) is hard to construct synthetically, so this
    // exercises the guardrail's own decision logic directly. ---
    {
        vxsuite::width::MonoDownmixGuardrail guardrail;
        guardrail.prepare(sr);
        const int block = 256;

        // Severe, sustained mono-energy loss (30% of input) - restraint
        // should decay toward 0 over repeated blocks.
        for (int i = 0; i < 400; ++i)
            guardrail.updateForNextBlock(1.0, 0.09, block); // sqrt(0.09/1.0)=0.3 ratio
        const float restrainedGain = guardrail.currentRestraint();

        // Recovery: feed unchanged mono energy (ratio=1) for a while.
        for (int i = 0; i < 400; ++i)
            guardrail.updateForNextBlock(1.0, 1.0, block);
        const float recoveredGain = guardrail.currentRestraint();

        const bool ok = restrainedGain < 0.15f && recoveredGain > 0.85f;
        allPass &= ok;
        std::cout << "  [mono downmix guardrail] restrainedGain=" << restrainedGain
                  << " recoveredGain=" << recoveredGain
                  << (ok ? "  PASS" : "  FAIL") << "\n";
    }

    // =====================================================================
    // EXPERIMENTAL: ADT micro-pitch stage (2026-08-07, off by default -
    // vxsuite::width::AdtVoice::setMicroPitchEnabled(), default false, no
    // user-facing control). These tests exercise the feature explicitly
    // enabled via VXWidthAudioProcessor::setAdtMicroPitchEnabled() (a
    // developer/test-only API) - production code never calls this.
    // =====================================================================
    {
        using namespace vxsuite::width::corpus;

        // --- Disabled path is sample-identical to today's production
        // behaviour: one processor never touches the experimental API, the
        // other explicitly disables it - both must render byte-identical
        // output, proving the feature has zero effect unless opted into. ---
        {
            auto input = makeSpeechLike(sr, 6.0f);
            VXWidthAudioProcessor untouched;
            untouched.prepareToPlay(sr, 256);
            setParamNormalized(untouched, "width", 0.5f);
            setParamNormalized(untouched, "double", 1.0f);
            render(untouched, input, 256);
            auto untouchedOut = render(untouched, input, 256);

            VXWidthAudioProcessor explicitlyDisabled;
            explicitlyDisabled.prepareToPlay(sr, 256);
            setParamNormalized(explicitlyDisabled, "width", 0.5f);
            setParamNormalized(explicitlyDisabled, "double", 1.0f);
            setParamNormalized(explicitlyDisabled, "microPitchExperimental", 0.0f);
            render(explicitlyDisabled, input, 256);
            auto disabledOut = render(explicitlyDisabled, input, 256);

            const float diff = maxAbsDiff(untouchedOut, disabledOut);
            const bool ok = diff == 0.0f;
            allPass &= ok;
            std::cout << "  [EXPERIMENTAL micro-pitch: disabled == production] diff=" << diff
                      << (ok ? "  PASS" : "  FAIL") << "\n";
        }

        // --- Enabled: bounded actual pitch deviation per voice, measured via
        // telemetry, not assumed from the configured range. Voice A
        // configured -1.5c+/-1.5c (range -3..0c), Voice B +2.0c+/-2.0c
        // (range 0..+4c) - asymmetric, weighted-average-neutral per the
        // design brief. ---
        {
            VXWidthAudioProcessor proc;
            proc.prepareToPlay(sr, 256);
            setParamNormalized(proc, "width", 0.5f);
            setParamNormalized(proc, "double", 1.0f);
            setParamNormalized(proc, "microPitchExperimental", 1.0f);
            auto input = makeSpeechLike(sr, 10.0f);
            render(proc, input, 256);
            render(proc, input, 256);

            const float aCents = proc.getAdtVoiceALongTermMicroPitchCents();
            const float bCents = proc.getAdtVoiceBLongTermMicroPitchCents();
            constexpr float kTol = 0.25f; // smoothing settle tolerance
            // Middle-ground range (2026-08-07 round 6): Voice A -8..0c, Voice B +2..+8c.
            const bool aOk = aCents > (-8.0f - kTol) && aCents < (0.0f + kTol);
            const bool bOk = bCents > (2.0f - kTol) && bCents < (8.0f + kTol);
            const bool ok = aOk && bOk;
            allPass &= ok;
            std::cout << "  [EXPERIMENTAL micro-pitch: bounded deviation] voiceA longTermCents=" << aCents
                      << " (expect -8..0) voiceB longTermCents=" << bCents << " (expect 2..8)"
                      << (ok ? "  PASS" : "  FAIL") << "\n";
        }

        // --- No NaN/Inf, stable across sample rates and block sizes, with
        // the feature enabled (mirrors the production SR/block-size sweep
        // above, but with micro-pitch active). ---
        {
            const double sampleRates[] = { 44100.0, 48000.0, 96000.0, 192000.0 };
            const int blockSizes[] = { 32, 128, 512, 2048 };
            bool ok = true;
            for (const double testSr : sampleRates) {
                for (const int block : blockSizes) {
                    VXWidthAudioProcessor proc;
                    proc.prepareToPlay(testSr, block);
                    setParamNormalized(proc, "width", 0.5f);
                    setParamNormalized(proc, "double", 1.0f);
                    setParamNormalized(proc, "microPitchExperimental", 1.0f);
                    auto input = makeSpeechLike(testSr, 1.0f);
                    render(proc, input, block);
                    auto out = render(proc, input, block);
                    if (!allFinite(out)) {
                        ok = false;
                        std::cout << "    FAIL (non-finite) at sr=" << testSr << " block=" << block << "\n";
                    }
                }
            }
            allPass &= ok;
            std::cout << "  [EXPERIMENTAL micro-pitch: SR/block-size stability] "
                      << (ok ? "PASS (all combinations finite)" : "FAIL") << "\n";
        }

        // --- No audio-thread allocations with the feature enabled. ---
        {
            VXWidthAudioProcessor proc;
            proc.prepareToPlay(sr, 256);
            setParamNormalized(proc, "width", 0.5f);
            setParamNormalized(proc, "double", 1.0f);
            setParamNormalized(proc, "microPitchExperimental", 1.0f);
            auto input = makeSpeechLike(sr, 1.0f);
            juce::MidiBuffer midi;
            {
                auto warmup = input;
                proc.processBlock(warmup, midi);
            }
            auto testBlock = input;
            AllocationScope allocationScope;
            proc.processBlock(testBlock, midi);
            const bool ok = allocationScope.allocations() == 0;
            allPass &= ok;
            std::cout << "  [EXPERIMENTAL micro-pitch: no audio-thread allocations] count="
                      << allocationScope.allocations() << (ok ? "  PASS" : "  FAIL") << "\n";
        }

        // --- No regression in L/R imbalance / Side-retention with the
        // feature enabled, on the same representative corpus and thresholds
        // the production path is held to. ---
        {
            constexpr double repSr = 48000.0;
            constexpr float repSeconds = 6.0f;
            constexpr std::uint32_t repSeedBase = 5000u;
            std::vector<juce::AudioBuffer<float>> repMonos;
            repMonos.push_back(makeMaleSpeech(repSr, repSeconds, repSeedBase + 0));
            repMonos.push_back(makeFemaleSpeech(repSr, repSeconds, repSeedBase + 1));
            repMonos.push_back(makeSungVocal(repSr, repSeconds, repSeedBase + 2));
            repMonos.push_back(makeGuitar(repSr, repSeconds, repSeedBase + 3, true));
            repMonos.push_back(makeDrums(repSr, repSeconds, repSeedBase + 4));
            repMonos.push_back(makePinkNoise(repSr, repSeconds, repSeedBase + 5));

            auto channelEnergyLocal = [](const juce::AudioBuffer<float>& b, int ch) {
                double e = 0.0;
                for (int i = 0; i < b.getNumSamples(); ++i) {
                    const float s = b.getSample(ch, i);
                    e += static_cast<double>(s) * s;
                }
                return e;
            };
            auto imbalanceEnabled = [&](const juce::AudioBuffer<float>& mono, const float widthNorm, const float doubleNorm) {
                VXWidthAudioProcessor proc;
                proc.prepareToPlay(repSr, 256);
                setParamNormalized(proc, "width", widthNorm);
                setParamNormalized(proc, "double", doubleNorm);
                setParamNormalized(proc, "microPitchExperimental", 1.0f);
                render(proc, mono, 256);
                auto out = render(proc, mono, 256);
                const double l = channelEnergyLocal(out, 0), r = channelEnergyLocal(out, 1);
                return std::abs(l - r) / std::max(l, r);
            };

            double widthSum = 0.0, widthMax = 0.0, doubleSum = 0.0, doubleMax = 0.0;
            for (const auto& mono : repMonos) {
                const double w = imbalanceEnabled(mono, 1.0f, 0.0f);
                const double d = imbalanceEnabled(mono, 0.5f, 1.0f);
                widthSum += w; widthMax = std::max(widthMax, w);
                doubleSum += d; doubleMax = std::max(doubleMax, d);
            }
            const double widthAvg = widthSum / repMonos.size();
            const double doubleAvg = doubleSum / repMonos.size();
            // Same production thresholds - the experimental stage must not
            // push imbalance past what the production path is held to.
            constexpr double kAvgThreshold = 0.095;
            constexpr double kWorstCaseThreshold = 0.24;
            const bool ok = widthAvg < kAvgThreshold && doubleAvg < kAvgThreshold
                          && widthMax < kWorstCaseThreshold && doubleMax < kWorstCaseThreshold;
            allPass &= ok;
            std::cout << "  [EXPERIMENTAL micro-pitch: no imbalance regression] widthAvg=" << widthAvg
                      << " widthMax=" << widthMax << " doubleAvg=" << doubleAvg << " doubleMax=" << doubleMax
                      << (ok ? "  PASS" : "  FAIL") << "\n";
        }
    }

    std::cout << "\n=== " << (allPass ? "ALL PASS" : "SOME TESTS FAILED") << " ===\n";
    return allPass ? 0 : 1;
}
