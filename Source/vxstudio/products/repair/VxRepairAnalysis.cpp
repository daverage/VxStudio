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
    humBandEnergy.reserve(static_cast<size_t>(targetFrames + 8));
    speechBandEnergy.reserve(static_cast<size_t>(targetFrames + 8));

    reset();
}

void RepairAnalyser::reset() {
    fifoWritePos    = 0;
    hopFill         = 0;
    framesCollected = 0;

    frameRms.clear();
    humBandEnergy.clear();
    speechBandEnergy.clear();

    collecting.store(false, std::memory_order_relaxed);
    progress.store(0.0f, std::memory_order_relaxed);
    complete.store(false, std::memory_order_relaxed);

    std::lock_guard<std::mutex> lock(assessmentMutex);
    assessment = RepairAssessment{};
}

void RepairAnalyser::startCollection() {
    fifoWritePos    = 0;
    hopFill         = 0;
    framesCollected = 0;

    frameRms.clear();
    humBandEnergy.clear();
    speechBandEnergy.clear();

    complete.store(false, std::memory_order_relaxed);
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
                collecting.store(false, std::memory_order_relaxed);
                finalise();
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

    // RMS from time-domain (before windowing divides energy slightly)
    float sumSq = 0.0f;
    for (int i = 0; i < kFftSize; ++i) {
        const float s = inFifo[static_cast<size_t>((readStart + i) % kFftSize)];
        sumSq += s * s;
    }
    const float rms = std::sqrt(sumSq / static_cast<float>(kFftSize));

    // Skip silent frames — don't count them toward the 5-second budget.
    // This means "Analyse" can be pressed before playback starts; collection
    // only advances once real audio arrives.
    static constexpr float kSilenceThresh = 3.16e-4f;  // −70 dBFS
    if (rms < kSilenceThresh)
        return;

    frameRms.push_back(rms);

    // Band energies from magnitude spectrum
    float humSum    = 0.0f;
    float speechSum = 0.0f;
    for (int k = 1; k < kHumHiBin; ++k)
        humSum += fftBuf[static_cast<size_t>(k)];
    for (int k = kLowMidHiBin; k < kSpeechHiBin; ++k)
        speechSum += fftBuf[static_cast<size_t>(k)];

    const float humRange    = static_cast<float>(kHumHiBin - 1);
    const float speechRange = static_cast<float>(kSpeechHiBin - kLowMidHiBin);

    humBandEnergy.push_back(humSum / std::max(1.0f, humRange));
    speechBandEnergy.push_back(speechSum / std::max(1.0f, speechRange));

    ++framesCollected;
}

void RepairAnalyser::finalise() noexcept {
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

    // ── Hum / mud score ───────────────────────────────────────────────────────
    // Compare hum-band energy to speech-band energy during quieter frames.
    // Strong low-frequency dominance in quiet passages = hum or tonal mud.
    std::vector<float> quietHumRatios;
    const float medianRms = sortedRms[static_cast<size_t>(n / 2)];
    for (int i = 0; i < n; ++i) {
        if (frameRms[static_cast<size_t>(i)] < medianRms * 0.5f) {
            const float h = humBandEnergy[static_cast<size_t>(i)];
            const float s = std::max(speechBandEnergy[static_cast<size_t>(i)], 1.0e-9f);
            quietHumRatios.push_back(h / s);
        }
    }
    float humMudScore = 0.0f;
    if (!quietHumRatios.empty()) {
        const float meanRatio = std::accumulate(quietHumRatios.begin(), quietHumRatios.end(), 0.0f)
                                / static_cast<float>(quietHumRatios.size());
        // Ratio ~1.0 = balanced; ratio > 3.0 = strong hum/mud presence.
        humMudScore = juce::jlimit(0.0f, 1.0f, (meanRatio - 1.0f) / 2.5f);
    }

    // ── Build assessment ──────────────────────────────────────────────────────
    RepairAssessment a;
    a.noiseScore   = noiseScore;
    a.reverbScore  = reverbScore;
    a.humMudScore  = humMudScore;
    a.confidence   = juce::jlimit(0.0f, 1.0f, static_cast<float>(n) / static_cast<float>(std::max(1, targetFrames)));

    a.noiseActive   = noiseScore   >= kActiveThreshold;
    a.reverbActive  = reverbScore  >= kActiveThreshold;
    a.cleanupActive = humMudScore  >= kActiveThreshold;

    a.suggestedNoiseStrength   = scoreToStrength(noiseScore);
    a.suggestedReverbStrength  = scoreToStrength(reverbScore);
    a.suggestedCleanupStrength = scoreToStrength(humMudScore);

    {
        std::lock_guard<std::mutex> lock(assessmentMutex);
        assessment = a;
    }

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
    collecting.store(false, std::memory_order_relaxed);
    progress.store(1.0f,    std::memory_order_relaxed);
    complete.store(true,    std::memory_order_release);
}

float RepairAnalyser::scoreToStrengthStatic(float score) noexcept { return scoreToStrength(score); }

float RepairAnalyser::scoreToStrength(float score) noexcept {
    if (score < kActiveThreshold) return 0.0f;
    // Map [0.10 .. 1.0] → [0.30 .. 0.85]: starts at a solid working level,
    // reaches 85% at maximum — bolder defaults, user can still pull back.
    return 0.30f + (score - kActiveThreshold) / (1.0f - kActiveThreshold) * 0.55f;
}

} // namespace vxsuite::repair
