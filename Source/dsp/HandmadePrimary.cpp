#include "HandmadePrimary.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numeric>

namespace vxcleaner::dsp {

namespace {
constexpr float kEps = 1.0e-12f;
inline float clamp01(float x) { return juce::jlimit(0.0f, 1.0f, x); }
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
inline float wrapPi(float x) {
    while (x > juce::MathConstants<float>::pi)  x -= 2.0f * juce::MathConstants<float>::pi;
    while (x < -juce::MathConstants<float>::pi) x += 2.0f * juce::MathConstants<float>::pi;
    return x;
}
inline float hzToBark(float hz) {
    const float term1 = 13.0f * std::atan(0.00076f * hz);
    const float ratio = hz / 7500.0f;
    const float term2 = 3.5f * std::atan(ratio * ratio);
    return term1 + term2;
}
}

float HandmadePrimary::safe(float x) {
    if (!std::isfinite(x))                  return 0.0f;
    if (std::fpclassify(x) == FP_SUBNORMAL) return 0.0f;
    return x;
}

// activeNoise: returns frozen profile if learned, else blind min-stats.
// The blind estimator always runs so it is current if the profile is reset.
float HandmadePrimary::activeNoise(size_t k) const {
    if (learnedProfileReady)
        return std::max(1.0e-10f, noisePowFrozen[k]);
    return std::max(1.0e-10f, noisePowBlind[k]);
}

void HandmadePrimary::updateMinStats(size_t k, float p, float presenceHint) {
    MinStatsBin& ms = msState[k];

    const float alphaIMCRA = presenceHint > 0.55f ? 0.96f : MS_alpha;
    ms.smoothPow = alphaIMCRA * ms.smoothPow + (1.0f - alphaIMCRA) * p;

    // Seed: on the first frame with a real signal, pre-fill the ring so
    // noisePowBlind is a reasonable estimate immediately rather than after
    // MS_D * MS_L frames of near-zero values causing zero suppression.
    if (!ms.subWindows.empty()
        && ms.subWindows[0] <= 1.0e-7f
        && ms.smoothPow > 1.0e-7f) {
        std::fill(ms.subWindows.begin(), ms.subWindows.end(), ms.smoothPow);
        ms.globalMin = ms.smoothPow;
        ms.subWinMin = ms.smoothPow;
        noisePowBlind[k] = Bmin * ms.smoothPow;
    }

    ms.subWinMin = std::min(ms.subWinMin, ms.smoothPow);

    if (++ms.frameCount >= minStatsL) {
        ms.subWindows[ms.subWinIdx] = ms.subWinMin;
        ms.subWinIdx  = (ms.subWinIdx + 1) % minStatsD;
        ms.subWinMin  = ms.smoothPow;
        ms.frameCount = 0;
        ms.globalMin  = *std::min_element(ms.subWindows.begin(), ms.subWindows.end());
    }

    noisePowBlind[k] = Bmin * std::max(1.0e-12f, ms.globalMin);
}

void HandmadePrimary::setQueueSizes(int maxBlockSize) {
    const size_t blockCap = static_cast<size_t>(std::max(1, maxBlockSize));
    maxBlockSizePrepared  = static_cast<int>(blockCap);

    inQueueCap   = fftSize + blockCap + hop + 16u;
    outQueueCap  = blockCap + fftSize + hop + 16u;
    const size_t sideDelaySamples = fftSize - hop;
    sideDelayCap = sideDelaySamples + blockCap + 16u;
    midDryDelayCap = sideDelaySamples + blockCap + 16u;

    inQueue  .assign(inQueueCap,   0.0f);
    outQueue .assign(outQueueCap,  0.0f);
    sideDelay.assign(sideDelayCap, 0.0f);
    midDryDelay.assign(midDryDelayCap, 0.0f);
    for (auto& ed : extraChannelDelays) {
        ed.buffer.assign(sideDelayCap, 0.0f);
        ed.readPos = ed.writePos = ed.available = 0;
    }

    inRead   = inWrite   = inCount   = 0;
    outRead  = outWrite  = outCount  = 0;
    sideDelayRead = sideDelayWrite = sideDelayCount = 0;
    midDryDelayRead = midDryDelayWrite = midDryDelayCount = 0;
}

void HandmadePrimary::updateSmoothingCoeffs() {
    const float hopSec = static_cast<float>(hop) /
                         std::max(1000.0f, static_cast<float>(sr));
    attackCoeff  = std::exp(-hopSec / 0.026f);
    releaseCoeff = std::exp(-hopSec / 0.260f);
}

void HandmadePrimary::prepare(double sampleRate, int maxBlockSize) {
    sr = sampleRate > 1000.0 ? sampleRate : 48000.0;
    const size_t blockCap = static_cast<size_t>(std::max(1, maxBlockSize));
    const float hopSec = static_cast<float>(hop) / std::max(1000.0f, static_cast<float>(sr));
    minStatsL = std::max(2, static_cast<int>(std::round(0.040f / hopSec)));
    minStatsD = std::max(4, static_cast<int>(std::round(1.5f / (hopSec * static_cast<float>(minStatsL)))));

    monoIn .assign(blockCap,      0.0f);
    monoOut.assign(blockCap,      0.0f);
    alignedMidDry.assign(blockCap, 0.0f);
    frame      .assign(fftSize * 2u, 0.0f);
    frameBuffer.assign(fftSize,      0.0f);
    window     .assign(fftSize,      0.0f);
    olaAcc     .assign(fftSize,      0.0f);

    for (size_t i = 0; i < fftSize; ++i)
        window[i] = std::sqrt(0.5f - 0.5f * std::cos(2.0f * juce::MathConstants<float>::pi
                                                     * static_cast<float>(i)
                                                     / static_cast<float>(fftSize)));

    currPow.assign(bins, 1.0e-8f);
    prevMag.assign(bins, 0.0f);
    tonalnessByBin.assign(bins, 0.0f);
    barkMaskFloor.assign(bins, 0.0f);
    harmonicFloor.assign(bins, 0.0f);
    lowBandStability.assign(bins, 0.0f);
    prevInputPhase.assign(bins, 0.0f);
    prevOutputPhase.assign(bins, 0.0f);
    phaseAdvance.assign(bins, 0.0f);
    binToBark.assign(bins, 0);
    erbFloor.clear();
    erbFloor.reserve(bins);
    for (auto& band : barkBandBins)
        band.clear();

    for (size_t k = 0; k < bins; ++k) {
        const float hz = static_cast<float>(k) * static_cast<float>(sr) / static_cast<float>(fftSize);
        const int bark = juce::jlimit(0, 23, static_cast<int>(std::floor(hzToBark(hz))));
        binToBark[k] = bark;
        barkBandBins[static_cast<size_t>(bark)].push_back(k);
        lowBandStability[k] = clamp01((700.0f - hz) / 450.0f);
        phaseAdvance[k] = 2.0f * juce::MathConstants<float>::pi
                        * static_cast<float>(hop) * static_cast<float>(k)
                        / static_cast<float>(fftSize);

        float erbW = 1.0f;
        if      (hz < 200.0f)  erbW = 0.5f;
        else if (hz < 1000.0f) erbW = 0.8f;
        else if (hz < 4000.0f) erbW = 1.0f;
        else if (hz < 8000.0f) erbW = 0.85f;
        else                   erbW = 0.6f;
        erbFloor.push_back(erbW);
    }

    noisePowFrozen.assign(bins, 1.0e-8f);
    learnAccum.assign(bins, 0.0f);
    learnAccumSq.assign(bins, 0.0f);
    learnHistory.assign(bins, {});

    msState.resize(bins);
    for (auto& ms : msState) {
        ms.smoothPow = ms.subWinMin = ms.globalMin = 1.0e-8f;
        ms.frameCount = ms.subWinIdx = 0;
        ms.subWindows.assign(static_cast<size_t>(minStatsD), 1.0e-8f);
    }
    noisePowBlind.assign(bins, 1.0e-8f);

    xiDD        .assign(bins, 1.0f);
    presenceProb.assign(bins, 0.5f);
    cleanPowPrev.assign(bins, 1.0e-8f);

    binSuppressCount.assign(bins, 0);

    gainTarget      .assign(bins, 1.0f);
    gainSmooth      .assign(bins, 1.0f);
    gainSmoothedFreq.assign(bins, 1.0f);

    setQueueSizes(static_cast<int>(blockCap));
    updateSmoothingCoeffs();
    reset();
}

void HandmadePrimary::reset() {
    signalPresenceAvg   = 0.5f;
    learningPrev        = learning;
    learnedProfileReady = false;
    learnedSensitivity  = 0.0f;
    learnedProfileConfidence = 0.0f;
    learnQualityAccum = 0.0f;
    learnQualityFrames = 0;

    std::fill(frameBuffer.begin(), frameBuffer.end(), 0.0f);
    std::fill(olaAcc     .begin(), olaAcc     .end(), 0.0f);
    std::fill(currPow    .begin(), currPow    .end(), 1.0e-8f);
    std::fill(prevMag    .begin(), prevMag    .end(), 0.0f);
    std::fill(tonalnessByBin.begin(), tonalnessByBin.end(), 0.0f);
    std::fill(barkMaskFloor.begin(), barkMaskFloor.end(), 0.0f);
    std::fill(harmonicFloor.begin(), harmonicFloor.end(), 0.0f);
    std::fill(prevInputPhase.begin(), prevInputPhase.end(), 0.0f);
    std::fill(prevOutputPhase.begin(), prevOutputPhase.end(), 0.0f);
    barkFluxAvg.fill(0.0f);
    barkTransientHold.fill(0);

    std::fill(noisePowFrozen.begin(), noisePowFrozen.end(), 1.0e-8f);
    std::fill(learnAccum.begin(), learnAccum.end(), 0.0f);
    std::fill(learnAccumSq.begin(), learnAccumSq.end(), 0.0f);
    for (auto& history : learnHistory)
        history.clear();
    std::fill(noisePowBlind .begin(), noisePowBlind .end(), 1.0e-8f);

    for (auto& ms : msState) {
        ms.smoothPow = ms.subWinMin = ms.globalMin = 1.0e-8f;
        ms.frameCount = ms.subWinIdx = 0;
        ms.subWindows.assign(static_cast<size_t>(minStatsD), 1.0e-8f);
    }

    std::fill(xiDD        .begin(), xiDD        .end(), 1.0f);
    std::fill(presenceProb.begin(), presenceProb.end(), 0.5f);
    std::fill(cleanPowPrev.begin(), cleanPowPrev.end(), 1.0e-8f);
    std::fill(binSuppressCount.begin(), binSuppressCount.end(), 0);

    std::fill(gainTarget      .begin(), gainTarget      .end(), 1.0f);
    std::fill(gainSmooth      .begin(), gainSmooth      .end(), 1.0f);
    std::fill(gainSmoothedFreq.begin(), gainSmoothedFreq.end(), 1.0f);

    inRead   = inWrite   = inCount   = 0;
    outRead  = outWrite  = outCount  = 0;
    sideDelayRead = sideDelayWrite = sideDelayCount = 0;
    std::fill(sideDelay.begin(), sideDelay.end(), 0.0f);
    midDryDelayRead = midDryDelayWrite = midDryDelayCount = 0;
    std::fill(midDryDelay.begin(), midDryDelay.end(), 0.0f);
    for (auto& ed : extraChannelDelays) {
        std::fill(ed.buffer.begin(), ed.buffer.end(), 0.0f);
        ed.readPos = ed.writePos = ed.available = 0;
    }

    prevFrameEnergy = 1.0e-8f;
    phaseHistoryReady = false;
    learnFrames = learnTargetFrames = 0;

    updateSmoothingCoeffs();
}

void HandmadePrimary::clearLearnedProfile() {
    learning = false;
    learningPrev = false;
    learnedProfileReady = false;
    learnedSensitivity = 0.0f;
    learnedProfileConfidence = 0.0f;
    learnQualityAccum = 0.0f;
    learnQualityFrames = 0;
    learnFrames = 0;
    learnTargetFrames = 0;
    std::fill(noisePowFrozen.begin(), noisePowFrozen.end(), 1.0e-8f);
    std::fill(learnAccum.begin(), learnAccum.end(), 0.0f);
    std::fill(learnAccumSq.begin(), learnAccumSq.end(), 0.0f);
    for (auto& history : learnHistory)
        history.clear();
}

bool HandmadePrimary::getLearnedProfileData(std::vector<float>& outProfile, float& outConfidence) const {
    if (!learnedProfileReady || noisePowFrozen.size() != bins)
        return false;
    outProfile = noisePowFrozen;
    outConfidence = learnedProfileConfidence;
    return true;
}

void HandmadePrimary::restoreLearnedProfile(const std::vector<float>& profile, float confidence) {
    if (profile.size() != bins)
        return;
    noisePowFrozen = profile;
    learnedProfileConfidence = confidence;
    learnedProfileReady = true;
}

void HandmadePrimary::resetStreamingState() {
    signalPresenceAvg = 0.5f;

    std::fill(frameBuffer.begin(), frameBuffer.end(), 0.0f);
    std::fill(olaAcc.begin(), olaAcc.end(), 0.0f);
    std::fill(currPow.begin(), currPow.end(), 1.0e-8f);
    std::fill(prevMag.begin(), prevMag.end(), 0.0f);
    std::fill(tonalnessByBin.begin(), tonalnessByBin.end(), 0.0f);
    std::fill(barkMaskFloor.begin(), barkMaskFloor.end(), 0.0f);
    std::fill(harmonicFloor.begin(), harmonicFloor.end(), 0.0f);
    std::fill(prevInputPhase.begin(), prevInputPhase.end(), 0.0f);
    std::fill(prevOutputPhase.begin(), prevOutputPhase.end(), 0.0f);
    barkFluxAvg.fill(0.0f);
    barkTransientHold.fill(0);

    std::fill(noisePowBlind.begin(), noisePowBlind.end(), 1.0e-8f);
    for (auto& ms : msState) {
        ms.smoothPow = ms.subWinMin = ms.globalMin = 1.0e-8f;
        ms.frameCount = ms.subWinIdx = 0;
        ms.subWindows.assign(static_cast<size_t>(minStatsD), 1.0e-8f);
    }

    std::fill(xiDD.begin(), xiDD.end(), 1.0f);
    std::fill(presenceProb.begin(), presenceProb.end(), 0.5f);
    std::fill(cleanPowPrev.begin(), cleanPowPrev.end(), 1.0e-8f);
    std::fill(binSuppressCount.begin(), binSuppressCount.end(), 0);
    std::fill(gainTarget.begin(), gainTarget.end(), 1.0f);
    std::fill(gainSmooth.begin(), gainSmooth.end(), 1.0f);
    std::fill(gainSmoothedFreq.begin(), gainSmoothedFreq.end(), 1.0f);

    inRead = inWrite = inCount = 0;
    outRead = outWrite = outCount = 0;
    sideDelayRead = sideDelayWrite = sideDelayCount = 0;
    std::fill(sideDelay.begin(), sideDelay.end(), 0.0f);
    midDryDelayRead = midDryDelayWrite = midDryDelayCount = 0;
    std::fill(midDryDelay.begin(), midDryDelay.end(), 0.0f);
    for (auto& ed : extraChannelDelays) {
        std::fill(ed.buffer.begin(), ed.buffer.end(), 0.0f);
        ed.readPos = ed.writePos = ed.available = 0;
    }

    prevFrameEnergy = 1.0e-8f;
    phaseHistoryReady = false;
    learningPrev = learning;
    updateSmoothingCoeffs();
}

bool HandmadePrimary::finalizeLearnedProfile() {
    if (learnFrames <= 0) {
        learnedProfileReady = false;
        learningPrev = learning;
        return false;
    }

    const float invN = 1.0f / static_cast<float>(learnFrames);
    float weightedRelStd = 0.0f;
    float weightSum = 0.0f;
    for (size_t k = 0; k < bins; ++k) {
        const float mean = std::max(1.0e-10f, learnAccum[k] * invN);
        const float meanSq = std::max(1.0e-10f, learnAccumSq[k] * invN);
        const float var = std::max(0.0f, meanSq - mean * mean);
        const float stddev = std::sqrt(var);
        const float relStd = stddev / std::max(1.0e-7f, mean);
        const float weight = std::sqrt(mean);
        weightedRelStd += relStd * weight;
        weightSum += weight;
        noisePowFrozen[k] = std::max(1.0e-10f, mean + (0.75f + 0.75f * learnedSensitivity) * stddev);
    }
    learnedProfileReady = true;
    const float progress = getLearnProgress();
    const float quality = learnQualityFrames > 0
        ? juce::jlimit(0.0f, 1.0f, learnQualityAccum / static_cast<float>(learnQualityFrames))
        : 0.0f;
    const float relStdAvg = weightSum > 1.0e-6f ? (weightedRelStd / weightSum) : 1.0f;
    const float stability = juce::jlimit(0.0f, 1.0f, 1.0f - relStdAvg / 1.10f);
    learnedProfileConfidence = juce::jlimit(0.0f, 1.0f,
                                            0.20f * progress
                                          + 0.35f * quality
                                          + 0.45f * stability);
    learningPrev = learning;
    return true;
}

float HandmadePrimary::getLearnProgress() const {
    if (learnTargetFrames <= 0)
        return 0.0f;
    return juce::jlimit(0.0f, 1.0f, static_cast<float>(learnFrames) / static_cast<float>(learnTargetFrames));
}

float HandmadePrimary::getLearnObservedSeconds() const {
    return static_cast<float>(learnFrames) * static_cast<float>(hop) / static_cast<float>(std::max(1000.0, sr));
}

// ---------------------------------------------------------------------------
// Ring FIFO helpers
// ---------------------------------------------------------------------------
void HandmadePrimary::pushInputSample(float x) {
    if (!inQueueCap) return;
    inQueue[inWrite] = safe(x);
    inWrite = (inWrite + 1u) % inQueueCap;
    if (inCount < inQueueCap) ++inCount;
    else inRead = (inRead + 1u) % inQueueCap;
}
bool HandmadePrimary::popInputSample(float& x) {
    if (!inCount) return false;
    x = inQueue[inRead];
    inRead = (inRead + 1u) % inQueueCap;
    --inCount; return true;
}
void HandmadePrimary::pushOutputSample(float x) {
    if (!outQueueCap) return;
    outQueue[outWrite] = safe(x);
    outWrite = (outWrite + 1u) % outQueueCap;
    if (outCount < outQueueCap) ++outCount;
    else outRead = (outRead + 1u) % outQueueCap;
}
bool HandmadePrimary::popOutputSample(float& x) {
    if (!outCount) return false;
    x = outQueue[outRead];
    outRead = (outRead + 1u) % outQueueCap;
    --outCount; return true;
}

// ---------------------------------------------------------------------------
// processInPlace
// ---------------------------------------------------------------------------
bool HandmadePrimary::processInPlace(juce::AudioBuffer<float>& buffer,
                                     float amount,
                                     const DenoiseOptions& options) {
    const int channels = buffer.getNumChannels();
    const int samples  = buffer.getNumSamples();
    if (channels <= 0 || samples <= 0) return false;

    const float wet = clamp01(amount);

    // User semantics:
    // - amount (wet) controls primary denoise intensity
    // - subtract controls how strongly the learned profile is removed (when learned)
    // - sensitivity controls learned profile variance weighting
    const float subtract = juce::jlimit(0.0f, 5.0f, options.subtract);
    const float sensitivity = juce::jlimit(0.0f, 2.0f, options.sensitivity);
    learnedSensitivity = sensitivity;
    const float subMixGlobal = clamp01(subtract / 5.0f);
    const bool labRaw = options.labRawMode;
    const float wetCore  = labRaw ? wet : juce::jlimit(0.0f, 1.0f, wet * (1.0f - 0.10f * wet));
    const bool subtractEnabled = subMixGlobal > 1.0e-4f && learnedProfileReady;
    // Allow one transition block when learning just stopped so the frozen
    // profile can finalize even if wet amount is zero (subtract-only workflows).
    if (wet <= 0.0f && !learning && !subtractEnabled && !learningPrev)
        return false;

    // Learn phase transitions
    if (learning && !learningPrev) {
        learnedProfileReady = false;
        learnedProfileConfidence = 0.0f;
        std::fill(noisePowFrozen.begin(), noisePowFrozen.end(), 1.0e-8f);
        std::fill(learnAccum.begin(), learnAccum.end(), 0.0f);
        std::fill(learnAccumSq.begin(), learnAccumSq.end(), 0.0f);
        for (auto& history : learnHistory)
            history.clear();
        learnFrames = 0;
        learnQualityAccum = 0.0f;
        learnQualityFrames = 0;
        learnTargetFrames = std::max(2, static_cast<int>(
            std::ceil(1.2f * static_cast<float>(sr) / static_cast<float>(hop))));
    } else if (!learning && learningPrev) {
        // Finalize frozen learn profile from a robust percentile plus variance spread.
        finalizeLearnedProfile();
    }
    learningPrev = learning;

    const int chunkSize = std::max(1, maxBlockSizePrepared);
    int processed = 0;

    while (processed < samples) {
        const int n = std::min(chunkSize, samples - processed);

        const float invCh = 1.0f / static_cast<float>(channels);
        for (int i = 0; i < n; ++i) {
            float m = 0.0f;
            for (int ch = 0; ch < channels; ++ch)
                m += buffer.getReadPointer(ch)[processed + i];
            monoIn[static_cast<size_t>(i)] = safe(m * invCh);
            pushInputSample(monoIn[static_cast<size_t>(i)]);
        }

        while (inCount >= hop) {

            std::move(frameBuffer.begin() + static_cast<long>(hop),
                      frameBuffer.end(), frameBuffer.begin());
            for (size_t i = 0; i < hop; ++i) {
                float s = 0.0f; popInputSample(s);
                frameBuffer[fftSize - hop + i] = safe(s);
            }

            float frameEnergy = 1.0e-8f;
            for (size_t i = 0; i < fftSize; ++i)
                frameEnergy += frameBuffer[i] * frameBuffer[i];

            std::fill(frame.begin(), frame.end(), 0.0f);
            for (size_t i = 0; i < fftSize; ++i)
                frame[i] = frameBuffer[i] * window[i];
            fft.performRealOnlyForwardTransform(frame.data());

            std::array<float, 24> barkFlux {};
            float totalPow = 1.0e-8f;
            for (size_t k = 0; k < bins; ++k) {
                const float re = frame[2u * k];
                const float im = (k == 0u || k == bins - 1u) ? 0.0f : frame[2u * k + 1u];
                const float p  = std::max(kEps, re * re + im * im);
                const float m  = std::sqrt(p);
                currPow[k]  = p;
                totalPow   += p;
                barkFlux[static_cast<size_t>(binToBark[k])] += std::max(0.0f, m - prevMag[k]);
                prevMag[k]  = m;
            }

            for (size_t b = 0; b < barkFluxAvg.size(); ++b) {
                barkFluxAvg[b] = 0.94f * barkFluxAvg[b] + 0.06f * barkFlux[b];
                const float fluxRatio = barkFlux[b] / std::max(1.0e-6f, barkFluxAvg[b] + 1.0e-6f);
                if (fluxRatio > 1.65f)
                    barkTransientHold[b] = 3;
                else if (barkTransientHold[b] > 0)
                    --barkTransientHold[b];
            }

            const float energyRatio = frameEnergy / std::max(1.0e-8f, prevFrameEnergy);
            prevFrameEnergy = frameEnergy;
            float logPowMean = 0.0f;
            float linPowMean = 0.0f;
            for (size_t k = 0; k < bins; ++k) {
                logPowMean += std::log(std::max(kEps, currPow[k]));
                linPowMean += currPow[k];
            }
            logPowMean /= static_cast<float>(bins);
            linPowMean /= static_cast<float>(bins);
            const float spectralFlatness = std::exp(logPowMean) / std::max(kEps, linPowMean);

            const float strengthBaseGlobal = lerp(0.38f, 1.12f, wetCore);
            const float globalFloor      = labRaw
                                              ? 1.0e-4f
                                              : juce::jlimit(0.015f, 0.14f,
                                                   lerp(0.05f, 0.015f, wetCore) * 0.85f);
            const float subtractMix = subtractEnabled ? juce::jlimit(0.0f, 1.0f, subMixGlobal) : 0.0f;
            const float profileAuthority = subtractEnabled
                                             ? juce::jlimit(0.35f, 1.0f, 0.35f + 0.65f * learnedProfileConfidence)
                                             : 0.0f;
            const float subtractAlpha = lerp(0.0f, lerp(2.1f, 3.8f, profileAuthority), subtractMix);
            const float subtractFloor = lerp(0.12f, lerp(0.02f, 2.5e-4f, profileAuthority), subtractMix);

            for (size_t k = 0; k < bins; ++k) {
                const float p = currPow[k];

                // Blind estimator always updates
                updateMinStats(k, p, presenceProb[k]);

                // Manual learn accumulator (true mean estimator, Audacity-like learn semantics).
                if (learning) {
                    learnAccum[k] += p;
                    learnAccumSq[k] += p * p;
                }

                const float n = std::max(1.0e-10f, activeNoise(k));

                // OM-LSA
                const float Gamma  = p / n;
                const float xiInst = std::max(0.0f, Gamma - 1.0f);
                xiDD[k] = std::max(0.0f, 0.97f * (cleanPowPrev[k] / n) + 0.03f * xiInst);

                const float gH1  = xiDD[k] / (xiDD[k] + 1.0f);
                const float vk   = Gamma * gH1;
                const float LR   = (1.0f + xiDD[k]) * std::exp(-std::min(vk, 30.0f));
                const float p_H1 = 1.0f / (1.0f + (q_absence / (1.0f - q_absence)) * LR);
                presenceProb[k]  = 0.90f * presenceProb[k] + 0.10f * p_H1;

                const float pSm = presenceProb[k];
                const float lnG = pSm         * std::log(std::max(kEps, gH1))
                                + (1.0f - pSm) * std::log(gH0_val);
                float g = std::exp(lnG);

                const float left       = currPow[(k > 0u) ? k - 1u : k];
                const float right      = currPow[(k + 1u < bins) ? k + 1u : k];
                const float tonalRatio = p / std::max(kEps, 0.5f * (left + right) + 0.15f * p);
                const float tonalness  = clamp01((tonalRatio - 1.25f) / 2.8f);
                tonalnessByBin[k] = tonalness;

                const float localSNR_dB = 10.0f * std::log10(std::max(1.0f, Gamma));
                const float betaBin = juce::jlimit(0.30f, 2.5f,
                                                   strengthBaseGlobal / (1.0f + 0.045f * localSNR_dB));
                const bool binInTransient = barkTransientHold[static_cast<size_t>(binToBark[k])] > 0
                                         || energyRatio > 1.38f;
                const float transientProtect = (labRaw || !binInTransient) ? 1.0f : 0.55f;
                const float strength = std::max(0.20f, (betaBin * transientProtect) - 0.40f * tonalness);
                g = std::pow(std::max(0.0f, g), strength);
                g = lerp(1.0f, g, wetCore);
                if (!labRaw && tonalness > 0.0f)
                    g = lerp(g, 1.0f, 0.30f * tonalness);
                if (!labRaw && binInTransient)
                    g = lerp(g, 1.0f, 0.40f);

                const float maskHead = clamp01(std::log10(std::max(1.0f, Gamma)) / 3.5f);
                const float minGain  = labRaw
                                          ? juce::jlimit(1.0e-4f, 0.10f, lerp(1.0e-3f, 0.05f, maskHead))
                                          : juce::jlimit(0.015f, 0.18f,
                                               lerp(0.03f, 0.11f, maskHead) * 0.85f);
                const float binFloor = globalFloor * erbFloor[k];
                g = std::max(std::max(minGain, binFloor), clamp01(g));

                if (g <= minGain + 0.05f)
                    binSuppressCount[k] = std::min(binSuppressCount[k] + 1, 24);
                else
                    binSuppressCount[k] = std::max(0, binSuppressCount[k] - 1);

                if (g > gainSmooth[k]) {
                    const float conf = clamp01(binSuppressCount[k] / 8.0f);
                    g = lerp(g, gainSmooth[k], 0.55f * conf);
                }

                if (subtractEnabled) {
                    const float mag = std::sqrt(std::max(kEps, p));
                    const float noiseMag = std::sqrt(std::max(kEps, noisePowFrozen[k]));
                    const float protectMask = juce::jlimit(0.0f, 1.0f,
                                                           0.52f * presenceProb[k]
                                                         + 0.28f * tonalnessByBin[k]
                                                         + 0.20f * (binInTransient ? 1.0f : 0.0f));
                    const float effectiveAlpha = subtractAlpha * (1.0f - 0.72f * protectMask);
                    const float effectiveFloor = juce::jlimit(1.0e-4f, 0.12f,
                                                              lerp(1.0e-4f, subtractFloor, protectMask));
                    const float gSub = juce::jlimit(effectiveFloor, 1.0f,
                                                    (mag - effectiveAlpha * noiseMag)
                                                    / std::max(kEps, mag));
                    g = std::max(g * gSub, effectiveFloor);
                }

                gainTarget[k] = clamp01(g);
                cleanPowPrev[k] = std::max(1.0e-10f, p * gainTarget[k] * gainTarget[k]);
            }

            std::fill(barkMaskFloor.begin(), barkMaskFloor.end(), 0.0f);
            for (size_t k = 0; k < bins; ++k) {
                const float n = std::max(kEps, activeNoise(k));
                const float gamma = currPow[k] / n;
                if (tonalnessByBin[k] < 0.60f || gamma < 3.0f)
                    continue;
                const int centerBand = binToBark[k];
                for (int band = std::max(0, centerBand - 3); band <= std::min(23, centerBand + 3); ++band) {
                    const float dist = static_cast<float>(std::abs(centerBand - band));
                    const float spread = 0.05f * (1.0f - dist / 4.0f) * wetCore;
                    for (size_t idx : barkBandBins[static_cast<size_t>(band)])
                        barkMaskFloor[idx] = std::max(barkMaskFloor[idx], spread);
                }
            }
            for (size_t k = 0; k < bins; ++k)
                gainTarget[k] = std::max(gainTarget[k], barkMaskFloor[k]);

            if (learning) {
                ++learnFrames;
                const float noiseLike = clamp01((spectralFlatness - 0.12f) / 0.38f);
                const float steady = clamp01(1.0f - std::abs(std::log(std::max(0.25f, std::min(4.0f, energyRatio)))) / 1.2f);
                const float quietSpeech = 1.0f - clamp01(signalPresenceAvg);
                const float quality = juce::jlimit(0.0f, 1.0f,
                                                   0.50f * noiseLike
                                                 + 0.30f * steady
                                                 + 0.20f * quietSpeech
                                                 - (energyRatio > 1.38f ? 0.12f : 0.0f));
                learnQualityAccum += quality;
                ++learnQualityFrames;
                learnedProfileConfidence = juce::jlimit(0.0f, 1.0f,
                                                        0.55f * getLearnProgress()
                                                      + 0.45f * quality);
            }

            // Frequency smoothing
            for (size_t k = 0; k < bins; ++k) {
                const float localGamma = currPow[k] / std::max(kEps, activeNoise(k));
                const float snrWeight = clamp01((localGamma - 1.0f) / 15.0f);
                const float wCenter = lerp(0.50f, 0.90f, snrWeight);
                const float wSide = 0.5f * (1.0f - wCenter);
                const size_t km = (k > 0u) ? (k - 1u) : k;
                const size_t kp = (k + 1u < bins) ? (k + 1u) : k;
                gainSmoothedFreq[k] = wSide * gainTarget[km]
                                    + wCenter * gainTarget[k]
                                    + wSide * gainTarget[kp];
            }

            // Harmonic comb protection: lock gains across harmonic series to
            // avoid isolated dips that create chirpy/robotic residuals.
            std::fill(harmonicFloor.begin(), harmonicFloor.end(), 0.0f);
            const size_t maxF0Bin = std::max<size_t>(8u, bins / 5u);
            for (size_t k = 8; k < maxF0Bin; ++k) {
                const float n = std::max(kEps, activeNoise(k));
                const float gamma = currPow[k] / n;
                if (gamma < 4.0f || tonalnessByBin[k] < 0.62f)
                    continue;
                if (!(gainSmoothedFreq[k] > gainSmoothedFreq[k - 1] && gainSmoothedFreq[k] >= gainSmoothedFreq[k + 1]))
                    continue;

                float harmonicMean = 0.0f;
                int harmonicCount = 0;
                for (int h = 1;; ++h) {
                    const size_t hk = static_cast<size_t>(std::lround(static_cast<double>(h) * static_cast<double>(k)));
                    if (hk >= bins || h > 10)
                        break;
                    harmonicMean += gainSmoothedFreq[hk];
                    ++harmonicCount;
                }
                if (harmonicCount < 3)
                    continue;

                harmonicMean /= static_cast<float>(harmonicCount);
                const float clampWidth = lerp(0.18f, 0.10f, wetCore);
                const float floorBoost = juce::jlimit(0.0f, 1.0f, harmonicMean - clampWidth);
                for (int h = 1;; ++h) {
                    const size_t hk = static_cast<size_t>(std::lround(static_cast<double>(h) * static_cast<double>(k)));
                    if (hk >= bins || h > 10)
                        break;
                    harmonicFloor[hk] = std::max(harmonicFloor[hk], floorBoost);
                }
            }
            for (size_t k = 0; k < bins; ++k)
                gainSmoothedFreq[k] = std::max(gainSmoothedFreq[k], harmonicFloor[k]);

            // Temporal smoothing
            for (size_t k = 0; k < bins; ++k) {
                float coeff = (gainSmoothedFreq[k] < gainSmooth[k]) ? attackCoeff
                                                                     : releaseCoeff;
                const float lfStab = lowBandStability[k];
                if (lfStab > 0.0f)
                    coeff = std::max(coeff, lerp(0.93f, 0.992f, lfStab));
                gainSmooth[k] = coeff * gainSmooth[k] + (1.0f - coeff) * gainSmoothedFreq[k];
            }

            // Apply spectral gain with phase propagation to maintain continuity
            // under heavy suppression (reduces hollow/phasy artifacts).
            float presenceSum = 0.0f;
            for (size_t k = 0; k < bins; ++k) {
                float gk = gainSmooth[k];
                const float reIn = frame[2u * k];
                const float imIn = (k == 0u || k == bins - 1u) ? 0.0f : frame[2u * k + 1u];
                const float mag = std::sqrt(std::max(kEps, reIn * reIn + imIn * imIn)) * gk;

                if (k == 0u || k == bins - 1u) {
                    frame[2u * k] = safe((reIn >= 0.0f ? mag : -mag));
                } else {
                    const float phaseIn = std::atan2(imIn, reIn);
                    float phaseOut = phaseIn;
                    if (phaseHistoryReady) {
                        const float dphi = wrapPi((phaseIn - prevInputPhase[k]) - phaseAdvance[k]);
                        phaseOut = prevOutputPhase[k] + phaseAdvance[k] + dphi;
                        phaseOut = wrapPi(phaseOut);
                    }

                    frame[2u * k] = safe(mag * std::cos(phaseOut));
                    frame[2u * k + 1u] = safe(mag * std::sin(phaseOut));
                    prevInputPhase[k] = phaseIn;
                    prevOutputPhase[k] = phaseOut;
                }

                presenceSum += presenceProb[k];
            }
            phaseHistoryReady = true;

            signalPresenceAvg = 0.94f * signalPresenceAvg
                              + 0.06f * (presenceSum / static_cast<float>(bins));

            // IFFT + OLA
            fft.performRealOnlyInverseTransform(frame.data());
            for (size_t i = 0; i < fftSize; ++i) {
                olaAcc [i] += frame[i] * window[i];
            }
            for (size_t i = 0; i < hop; ++i)
                pushOutputSample(safe(olaAcc[i]));

            std::move(olaAcc .begin() + static_cast<long>(hop), olaAcc .end(), olaAcc .begin());
            std::fill(olaAcc .end()   - static_cast<long>(hop), olaAcc .end(), 0.0f);
        }

        // Drain audio output FIFO
        float out = 0.0f;
        for (int i = 0; i < n; ++i) {
            if (!popOutputSample(out))
                out = (i > 0) ? monoOut[static_cast<size_t>(i-1)] : 0.0f;
            monoOut[static_cast<size_t>(i)] = safe(out);
        }

        // Stereo-safe output: apply mono denoise delta equally to all channels.
        // This preserves stereo image while applying true reconstructed denoise.
        if (channels >= 2) {
            float* l = buffer.getWritePointer(0);
            float* r = buffer.getWritePointer(1);
            const size_t sideLatency = fftSize - hop;

            // Push current mid/side into delay lines
            for (int i = 0; i < n; ++i) {
                const float leftDry = l[processed + i];
                const float rightDry = r[processed + i];
                const float midDry = 0.5f * (leftDry + rightDry);
                const float side = 0.5f * (leftDry - rightDry);

                if (midDryDelayCap > 0u) {
                    midDryDelay[midDryDelayWrite] = safe(midDry);
                    midDryDelayWrite = (midDryDelayWrite + 1u) % midDryDelayCap;
                    if (midDryDelayCount < midDryDelayCap) ++midDryDelayCount;
                    else midDryDelayRead = (midDryDelayRead + 1u) % midDryDelayCap;
                }
                if (sideDelayCap > 0u) {
                    sideDelay[sideDelayWrite] = safe(side);
                    sideDelayWrite = (sideDelayWrite + 1u) % sideDelayCap;
                    if (sideDelayCount < sideDelayCap) ++sideDelayCount;
                    else sideDelayRead = (sideDelayRead + 1u) % sideDelayCap;
                }
            }

            float midDryEnergy = 1.0e-9f;
            float midWetEnergy = 1.0e-9f;

            // Extract aligned dry and compute energy for side scaling
            for (int i = 0; i < n; ++i) {
                float dryMidAligned = 0.0f;
                if (midDryDelayCount > sideLatency) {
                    dryMidAligned = midDryDelay[midDryDelayRead];
                    midDryDelayRead = (midDryDelayRead + 1u) % midDryDelayCap;
                    --midDryDelayCount;
                }
                alignedMidDry[static_cast<size_t>(i)] = dryMidAligned;
                midDryEnergy += dryMidAligned * dryMidAligned;
                midWetEnergy += monoOut[static_cast<size_t>(i)] * monoOut[static_cast<size_t>(i)];
            }

            const float midRatio = juce::jlimit(0.0f, 1.5f, std::sqrt(midWetEnergy / midDryEnergy));
            float sideScale = labRaw ? 1.0f : juce::jlimit(0.50f, 1.0f, lerp(1.0f, midRatio, 0.35f * wet));

            for (int i = 0; i < n; ++i) {
                const float midOut = monoOut[static_cast<size_t>(i)];
                float sideDelayed = 0.0f;
                if (sideDelayCount > sideLatency) {
                    sideDelayed = sideDelay[sideDelayRead];
                    sideDelayRead = (sideDelayRead + 1u) % sideDelayCap;
                    --sideDelayCount;
                }

                l[processed + i] = safe(midOut + sideDelayed * sideScale);
                r[processed + i] = safe(midOut - sideDelayed * sideScale);
            }

            // For additional channels (if present), apply a conservative mono delta.
            for (int ch = 2; ch < channels; ++ch) {
                const int ecIdx = ch - 2;
                float* d = buffer.getWritePointer(ch);
                for (int i = 0; i < n; ++i) {
                    float dryAligned = d[processed + i];
                    if (ecIdx < maxExtraChannels) {
                        auto& ed = extraChannelDelays[static_cast<size_t>(ecIdx)];
                        ed.buffer[ed.writePos] = safe(d[processed + i]);
                        ed.writePos = (ed.writePos + 1) % ed.buffer.size();
                        if (ed.available < ed.buffer.size()) ++ed.available;
                        else ed.readPos = (ed.readPos + 1) % ed.buffer.size();

                        if (ed.available > sideLatency) {
                            dryAligned = ed.buffer[ed.readPos];
                            ed.readPos = (ed.readPos + 1) % ed.buffer.size();
                            --ed.available;
                        } else {
                            dryAligned = 0.0f;
                        }
                    }
                    const float dryMidAligned = alignedMidDry[static_cast<size_t>(i)];
                    const float wetMono = monoOut[static_cast<size_t>(i)];
                    d[processed + i] = safe(dryAligned + (wetMono - dryMidAligned) * wet);
                }
            }
        } else {
            float* d = buffer.getWritePointer(0);
            for (int i = 0; i < n; ++i) {
                const float wetMono = monoOut[static_cast<size_t>(i)];
                d[processed + i] = safe(wetMono);
            }
        }

        processed += n;
    }

    return true;
}

} // namespace vxcleaner::dsp
