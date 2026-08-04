#include "VxRepairAnalysis.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace vxsuite::repair {

static float buildHannWindow(std::vector<float>& w, int size) {
    w.resize(static_cast<size_t>(size));
    for (int i = 0; i < size; ++i)
        w[static_cast<size_t>(i)] = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi * static_cast<float>(i) / static_cast<float>(size));
    return 1.0f;
}

void RepairAnalyser::prepare(double sampleRate, int /*maxBlockSize*/) {
    sr = sampleRate > 1000.0 ? sampleRate : 48000.0;
    targetFrames = static_cast<int>(std::ceil(kCollectionSeconds * sr / static_cast<double>(kHop)));

    fft.prepare(kFftOrder);
    buildHannWindow(window, kFftSize);

    inFifo.assign(static_cast<size_t>(kFftSize), 0.0f);
    fftBuf.assign(static_cast<size_t>(kFftSize * 2), 0.0f);

    frameRms.reserve(static_cast<size_t>(targetFrames + 8));
    sibBandRatio.reserve(static_cast<size_t>(targetFrames + 8));
    frameCrestDb.reserve(static_cast<size_t>(targetFrames + 8));

    reset();
}

void RepairAnalyser::reset() {
    fifoWritePos    = 0;
    hopFill         = 0;
    framesCollected = 0;

    frameRms.clear();
    sibBandRatio.clear();
    frameCrestDb.clear();

    collecting.store(false, std::memory_order_relaxed);
    progress.store(0.0f, std::memory_order_relaxed);
    complete.store(false, std::memory_order_relaxed);
    finalisePending.store(false, std::memory_order_relaxed);
    lastNoiseScore.store(0.0f, std::memory_order_relaxed);
    noiseScoreOverride = -1.0f;

    std::lock_guard<std::mutex> lock(assessmentMutex);
    assessment = RepairAssessment{};
}

void RepairAnalyser::startCollection(const float phase2NoiseScoreOverride) {
    fifoWritePos    = 0;
    hopFill         = 0;
    framesCollected = 0;

    frameRms.clear();
    sibBandRatio.clear();
    frameCrestDb.clear();

    noiseScoreOverride = phase2NoiseScoreOverride;
    complete.store(false, std::memory_order_relaxed);
    finalisePending.store(false, std::memory_order_relaxed);
    progress.store(0.0f, std::memory_order_relaxed);
    collecting.store(true, std::memory_order_release);
}

void RepairAnalyser::process(const juce::AudioBuffer<float>& buffer, int numSamples) {
    if (!collecting.load(std::memory_order_acquire) || complete.load(std::memory_order_relaxed))
        return;

    const int numChannels = buffer.getNumChannels();
    if (numChannels == 0 || numSamples <= 0)
        return;

    for (int i = 0; i < numSamples; ++i) {
        // Mix to mono for analysis
        float mono = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            mono += buffer.getSample(ch, i);
        mono /= static_cast<float>(numChannels);

        inFifo[static_cast<size_t>(fifoWritePos)] = mono;
        fifoWritePos = (fifoWritePos + 1) % kFftSize;
        ++hopFill;

        if (hopFill >= kHop) {
            hopFill = 0;
            processFrame();

            if (framesCollected >= targetFrames) {
                // Collection done — hand the heavy scoring pass to the message
                // thread (finaliseIfPending). The release store pairs with the
                // acquire exchange there, publishing the collected vectors.
                collecting.store(false, std::memory_order_relaxed);
                progress.store(1.0f, std::memory_order_relaxed);
                finalisePending.store(true, std::memory_order_release);
                return;
            }

            progress.store(static_cast<float>(framesCollected) / static_cast<float>(std::max(1, targetFrames)),
                           std::memory_order_relaxed);
        }
    }
}

void RepairAnalyser::processFrame() noexcept {
    // Copy fifo into contiguous buffer (ring-buffer unroll)
    const int readStart = fifoWritePos;
    for (int i = 0; i < kFftSize; ++i) {
        const int src = (readStart + i) % kFftSize;
        fftBuf[static_cast<size_t>(i)] = inFifo[static_cast<size_t>(src)] * window[static_cast<size_t>(i)];
    }
    for (int i = kFftSize; i < kFftSize * 2; ++i)
        fftBuf[static_cast<size_t>(i)] = 0.0f;

    fft.performFrequencyOnlyForward(fftBuf.data());

    // Update 24-band display spectrum (non-blocking: skip if UI is reading).
    {
        const int maxBin = std::min(kBins - 1, static_cast<int>(20000.0 * kFftSize / sr));
        const float logMin = std::log(1.0f);
        const float logMax = std::log(static_cast<float>(std::max(2, maxBin)));
        std::array<float, kDisplayBands> bands {};
        for (int b = 0; b < kDisplayBands; ++b) {
            const int binLo = std::max(1, static_cast<int>(std::exp(logMin + (logMax - logMin) * b / kDisplayBands)));
            const int binHi = std::min(maxBin, static_cast<int>(std::exp(logMin + (logMax - logMin) * (b + 1) / kDisplayBands)));
            float sum = 0.0f;
            int   cnt = 0;
            for (int k = binLo; k <= binHi; ++k) { sum += fftBuf[static_cast<size_t>(k)]; ++cnt; }
            bands[static_cast<size_t>(b)] = cnt > 0 ? sum / cnt : 0.0f;
        }
        if (spectrumMutex.try_lock()) {
            spectrumPeakHold = std::max(spectrumPeakHold * 0.9985f, *std::max_element(bands.begin(), bands.end()));
            const float invPeak = spectrumPeakHold > 1.0e-9f ? 1.0f / spectrumPeakHold : 0.0f;
            for (int b = 0; b < kDisplayBands; ++b)
                displayBands[static_cast<size_t>(b)] = bands[static_cast<size_t>(b)] * invPeak;
            spectrumMutex.unlock();
        }
    }

    // RMS and peak from time-domain (before windowing divides energy slightly)
    float sumSq = 0.0f;
    float peak  = 0.0f;
    for (int i = 0; i < kFftSize; ++i) {
        const float s = inFifo[static_cast<size_t>((readStart + i) % kFftSize)];
        sumSq += s * s;
        peak = std::max(peak, std::abs(s));
    }
    const float rms = std::sqrt(sumSq / static_cast<float>(kFftSize));

    // Skip silent frames  -  don't count them toward the 5-second budget.
    // This means "Analyse" can be pressed before playback starts; collection
    // only advances once real audio arrives.
    static constexpr float kSilenceThresh = 3.16e-4f;  // −70 dBFS
    if (rms < kSilenceThresh)
        return;

    frameRms.push_back(rms);

    // Click detection over the newest kHop (512) samples, NOT the full 2048-sample frame.
    // A 5-sample click in 512 samples is diluted 4× less than in 2048, so subtle events
    // actually register.  Two complementary signals are combined:
    //   crestSig  – crest factor of the hop window (peak / hop-RMS), sensitive to amplitude spikes
    //   diffSig   – largest sample-to-sample jump / hop-RMS, sensitive to waveform discontinuities
    // Result is a per-hop click signature stored in frameCrestDb [0..1]; finalise() scores it.
    {
        constexpr int kHopBase = kFftSize - kHop;   // offset to newest hop in FIFO
        float hopSumSq = 0.0f, hopPeak = 0.0f, maxDiff = 0.0f;
        float prevS = inFifo[static_cast<size_t>((readStart + kHopBase) % kFftSize)];
        for (int i = 0; i < kHop; ++i) {
            const float s = inFifo[static_cast<size_t>((readStart + kHopBase + i) % kFftSize)];
            hopSumSq += s * s;
            hopPeak   = std::max(hopPeak, std::abs(s));
            if (i > 0) { maxDiff = std::max(maxDiff, std::abs(s - prevS)); }
            prevS = s;
        }
        const float hopRms = std::sqrt(hopSumSq / static_cast<float>(kHop));
        float clickSig = 0.0f;
        if (hopRms > 1.0e-7f) {
            const float hopCrestDb = 20.0f * std::log10(std::max(hopPeak, hopRms) / hopRms);
            const float diffRatio  = maxDiff / hopRms;
            // Ranges start low deliberately — the P95/P50 gap check in finalise() filters
            // out plosives and fricatives that raise many hops uniformly.
            const float crestSig = juce::jlimit(0.0f, 1.0f, (hopCrestDb - 8.0f)  / 10.0f);
            const float diffSig  = juce::jlimit(0.0f, 1.0f, (diffRatio  - 1.5f) /  5.0f);
            clickSig = std::max(crestSig, diffSig);
        }
        frameCrestDb.push_back(clickSig);
    }

    // Sibilance score: ratio of sibilance-band (4.5–9 kHz) to speech-band (1–4 kHz) energy.
    // Only meaningful on voiced frames (above silence floor)  -  quiet frames are skipped.
    float sibSum    = 0.0f;
    float speechSum = 0.0f;
    for (int k = kSpeechLoBin; k < kSpeechHiBin2; ++k)
        speechSum += fftBuf[static_cast<size_t>(k)];
    for (int k = kSibLoBin; k <= kSibHiBin; ++k)
        sibSum += fftBuf[static_cast<size_t>(k)];

    const float speechAvg = speechSum / static_cast<float>(kSpeechHiBin2 - kSpeechLoBin);
    const float sibAvg    = sibSum    / static_cast<float>(kSibHiBin - kSibLoBin + 1);
    sibBandRatio.push_back(sibAvg / std::max(1.0e-9f, speechAvg));

    ++framesCollected;
}

void RepairAnalyser::finaliseIfPending() {
    if (finalisePending.exchange(false, std::memory_order_acquire))
        finalise();
}

void RepairAnalyser::finalise() {
    if (framesCollected == 0) return;

    const int n = framesCollected;

    // ── Noise score ───────────────────────────────────────────────────────────
    // Sort frame RMS values; the 10th percentile approximates the noise floor.
    std::vector<float> sortedRms = frameRms;
    std::sort(sortedRms.begin(), sortedRms.end());

    const int p10idx = std::max(0, static_cast<int>(0.10f * static_cast<float>(n)) - 1);
    const float noiseFloorRms = std::max(sortedRms[static_cast<size_t>(p10idx)], 1.0e-9f);
    const float noiseFloorDb  = 20.0f * std::log10(noiseFloorRms);

    // Map: −70 dBFS floor (inaudible) → 0.0,  −30 dBFS floor (clearly audible) → 0.5,
    //       −10 dBFS floor (dominant noise) → 1.0.
    // Previous formula was inverted (gave 1.0 for silence).
    const float noiseScore = juce::jlimit(0.0f, 1.0f, (noiseFloorDb + 70.0f) / 60.0f);

    // ── Reverb score ──────────────────────────────────────────────────────────
    // Measure average log energy decay rate during frames where signal is dropping.
    // Slow decay (ratio near 1.0) = long reverb tail; fast decay = dry.
    // Reverb: measure how slowly energy decays *above* the noise floor.
    // Using a fixed -42 dB threshold masked noise-sustained tails as reverb.
    // Now the threshold floats 12 dB above the estimated noise floor.
    float decaySum   = 0.0f;
    int   decayCount = 0;
    const float signalThresh = noiseFloorRms * 4.0f;  // 12 dB above noise floor

    for (int i = 1; i < n; ++i) {
        const float prev = frameRms[static_cast<size_t>(i - 1)];
        const float cur  = frameRms[static_cast<size_t>(i)];
        // Only measure frames where the PREVIOUS frame was clearly signal
        // and energy is now dropping toward (but still above) the noise floor.
        if (prev > signalThresh && cur < prev && cur > noiseFloorRms * 1.5f) {
            // Normalise out the noise floor so sustained noise doesn't inflate the ratio
            const float normPrev = prev - noiseFloorRms;
            const float normCur  = cur  - noiseFloorRms;
            if (normPrev > 1.0e-9f) {
                decaySum += normCur / normPrev;
                ++decayCount;
            }
        }
    }
    const float meanDecayRatio = decayCount > 4 ? decaySum / static_cast<float>(decayCount) : 0.65f;
    // Dry signal drops fast (ratio ~0.5); room reverb decays slowly (ratio > 0.85).
    // Map 0.65..0.95 → 0..1.
    const float reverbScore = juce::jlimit(0.0f, 1.0f, (meanDecayRatio - 0.65f) / 0.30f);

    // ── Speech clarity score ──────────────────────────────────────────────────
    // Measure sibilance-band / speech-band ratio across all voiced frames.
    // High ratio on a consistent basis indicates sibilance-heavy audio that will
    // benefit from de-essing. Use the upper quartile (P75) to catch persistent
    // harshness without being skewed by occasional transient frames.
    float clarityScore = 0.0f;
    if (!sibBandRatio.empty()) {
        std::vector<float> sortedRatios = sibBandRatio;
        std::sort(sortedRatios.begin(), sortedRatios.end());
        const int p75idx = std::min(static_cast<int>(sortedRatios.size()) - 1,
                                    static_cast<int>(0.75f * static_cast<float>(sortedRatios.size())));
        const float p75Ratio = sortedRatios[static_cast<size_t>(p75idx)];
        // Typical speech: ratio ~0.25. Harsh/sibilant: ratio > 0.55. Extreme: > 0.80.
        clarityScore = juce::jlimit(0.0f, 1.0f, (p75Ratio - 0.25f) / 0.55f);
    }
    const float humMudScore = clarityScore;

    // ── Click score ───────────────────────────────────────────────────────────
    // frameCrestDb holds per-hop click signatures [0..1] from processFrame().
    // Strategy: use the P95–P50 gap to find isolated outlier hops (clicks) without
    // being fooled by plosives or fricatives that raise many consecutive hops uniformly.
    // A pervasive-click recording (whole distribution shifted up) is caught by a high
    // absolute P95.  Both signals are combined.
    float clickScore = 0.0f;
    if (!frameCrestDb.empty()) {
        std::vector<float> sortedSig = frameCrestDb;
        std::sort(sortedSig.begin(), sortedSig.end());
        const int ns  = static_cast<int>(sortedSig.size());
        const float p50 = sortedSig[static_cast<size_t>(ns / 2)];
        const float p95 = sortedSig[static_cast<size_t>(std::min(ns - 1, static_cast<int>(0.95f * ns)))];

        // P95−P50 gap: isolated spikes (clicks) push P95 way above the median.
        // Normal speech / plosives → small gap even if the median is elevated.
        const float gapScore = juce::jlimit(0.0f, 1.0f, (p95 - p50 - 0.12f) / 0.20f);
        // Absolute P95: catches recordings where clicks are so frequent the whole
        // distribution shifts upward and the gap alone underestimates severity.
        const float absScore = juce::jlimit(0.0f, 1.0f, (p95 - 0.30f) / 0.30f);
        clickScore = std::max(gapScore, absScore);
    }

    // ── Build assessment ──────────────────────────────────────────────────────
    // Phase 2 runs on denoised audio, so its own noise measurement is
    // meaningless — merge the Phase 1 score back in instead.
    const float effectiveNoiseScore = noiseScoreOverride >= 0.0f ? noiseScoreOverride : noiseScore;
    RepairAssessment a;
    a.noiseScore   = effectiveNoiseScore;
    a.reverbScore  = reverbScore;
    a.humMudScore  = humMudScore;
    a.clickScore   = clickScore;
    a.confidence   = juce::jlimit(0.0f, 1.0f, static_cast<float>(n) / static_cast<float>(std::max(1, targetFrames)));

    a.noiseActive   = effectiveNoiseScore >= kActiveThreshold;
    a.reverbActive  = reverbScore  >= kActiveThreshold;
    a.cleanupActive = humMudScore  >= kActiveThreshold;
    a.clickActive   = clickScore   >= kActiveThreshold;

    a.suggestedNoiseStrength   = scoreToStrength(effectiveNoiseScore);
    a.suggestedReverbStrength  = scoreToStrength(reverbScore);
    a.suggestedCleanupStrength = scoreToStrength(humMudScore);
    a.suggestedClickStrength   = scoreToStrength(clickScore);

    {
        std::lock_guard<std::mutex> lock(assessmentMutex);
        assessment = a;
    }

    lastNoiseScore.store(effectiveNoiseScore, std::memory_order_relaxed);
    progress.store(1.0f, std::memory_order_relaxed);
    complete.store(true, std::memory_order_release);
}

RepairAssessment RepairAnalyser::getAssessment() const noexcept {
    std::lock_guard<std::mutex> lock(assessmentMutex);
    return assessment;
}

void RepairAnalyser::restoreAssessment(const RepairAssessment& a) noexcept {
    {
        std::lock_guard<std::mutex> lock(assessmentMutex);
        assessment = a;
    }
    lastNoiseScore.store(a.noiseScore, std::memory_order_relaxed);
    collecting.store(false, std::memory_order_relaxed);
    progress.store(1.0f,    std::memory_order_relaxed);
    complete.store(true,    std::memory_order_release);
}

float RepairAnalyser::scoreToStrengthStatic(float score) noexcept { return scoreToStrength(score); }

void RepairAnalyser::getDisplayBands(std::array<float, kDisplayBands>& out) const noexcept {
    std::lock_guard<std::mutex> lock(spectrumMutex);
    out = displayBands;
}

float RepairAnalyser::scoreToStrength(float score) noexcept {
    if (score < kActiveThreshold) return 0.0f;
    // Map [0.10 .. 1.0] → [0.30 .. 0.95]: starts at a solid working level,
    // reaches 95% at maximum score  -  heavy problems get heavy treatment.
    return 0.30f + (score - kActiveThreshold) / (1.0f - kActiveThreshold) * 0.65f;
}

} // namespace vxsuite::repair
