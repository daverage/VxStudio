#include "VxSubtractDsp.h"
#include "../../../framework/VxStudioSpectralHelpers.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numeric>

namespace vxsuite::subtract {

namespace {
constexpr float kEps = 1.0e-12f;
constexpr float kMinimumLearnFramePower = 1.0e-6f;
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
} // namespace

// ---------------------------------------------------------------------------
// FrameCtx — scalar parameters derived from options, passed to per-frame stages
// ---------------------------------------------------------------------------
struct SubtractDsp::FrameCtx {
    float wetCore;
    float globalFloor;
    float strengthBaseGlobal;
    float subtractAlpha;
    float subtractFloor;
    bool  subtractEnabled;
    bool  labRaw;
    bool  voiceMode;
    float guardStrictness;
    float sourceProtect;
    float speechFocus;
};

// ---------------------------------------------------------------------------
// Utility
// ---------------------------------------------------------------------------
float SubtractDsp::safe(float x) {
    if (!std::isfinite(x))                   return 0.0f;
    if (std::fpclassify(x) == FP_SUBNORMAL)  return 0.0f;
    return x;
}

float SubtractDsp::activeNoise(size_t k) const {
    if (learnedProfileReady)
        return std::max(1.0e-10f, noisePowFrozen[k]);
    return std::max(1.0e-10f, noisePowBlind[k]);
}

float SubtractDsp::learnedSubtractNoise(size_t k) const {
    return std::max(1.0e-10f, noisePowFrozen[k]);
}

void SubtractDsp::updateMinStats(size_t k, float p, float presenceHint) {
    noisePowBlind[k] = vxsuite::spectral::updateMinStats(
        msState[k], p, presenceHint, minStatsL, minStatsD, MS_alpha, Bmin, 1.0e-12f);
}

// ---------------------------------------------------------------------------
// Ring FIFO helpers
// ---------------------------------------------------------------------------
void SubtractDsp::pushInputSample(float x) {
    if (!inQueueCap) return;
    inQueue[inWrite] = safe(x);
    inWrite = (inWrite + 1u) % inQueueCap;
    if (inCount < inQueueCap) ++inCount;
    else inRead = (inRead + 1u) % inQueueCap;
}
bool SubtractDsp::popInputSample(float& x) {
    if (!inCount) return false;
    x = inQueue[inRead];
    inRead = (inRead + 1u) % inQueueCap;
    --inCount; return true;
}
void SubtractDsp::pushOutputSample(float x) {
    if (!outQueueCap) return;
    outQueue[outWrite] = safe(x);
    outWrite = (outWrite + 1u) % outQueueCap;
    if (outCount < outQueueCap) ++outCount;
    else outRead = (outRead + 1u) % outQueueCap;
}
bool SubtractDsp::popOutputSample(float& x) {
    if (!outCount) return false;
    x = outQueue[outRead];
    outRead = (outRead + 1u) % outQueueCap;
    --outCount; return true;
}

// ---------------------------------------------------------------------------
// Setup helpers
// ---------------------------------------------------------------------------
void SubtractDsp::setQueueSizes(int maxBlockSize) {
    const size_t blockCap = static_cast<size_t>(std::max(1, maxBlockSize));
    maxBlockSizePrepared  = static_cast<int>(blockCap);

    inQueueCap   = fftSize + blockCap + hop + 16u;
    outQueueCap  = blockCap + fftSize + hop + 16u;
    const size_t sideLatency = fftSize - hop;
    sideDelayCap    = sideLatency + blockCap + 16u;
    midDryDelayCap  = sideLatency + blockCap + 16u;

    inQueue   .assign(inQueueCap,   0.0f);
    outQueue  .assign(outQueueCap,  0.0f);
    sideDelay .assign(sideDelayCap, 0.0f);
    midDryDelay.assign(midDryDelayCap, 0.0f);
    for (auto& ed : extraChannelDelays)
        ed.buffer.assign(sideDelayCap, 0.0f);

    inRead  = inWrite  = inCount  = 0;
    outRead = outWrite = outCount = 0;
    frameBufferWritePos = 0;
    olaReadPos = olaWritePos = 0;
    sideDelayRead    = sideDelayWrite    = sideDelayCount    = 0;
    midDryDelayRead  = midDryDelayWrite  = midDryDelayCount  = 0;
    for (auto& ed : extraChannelDelays)
        ed.readPos = ed.writePos = ed.available = 0;
}

void SubtractDsp::updateSmoothingCoeffs() {
    const float hopSec = static_cast<float>(hop) /
                         std::max(1000.0f, static_cast<float>(sr));
    attackCoeff  = std::exp(-hopSec / 0.050f);
    releaseCoeff = std::exp(-hopSec / 0.260f);
}

// ---------------------------------------------------------------------------
// Reset helpers
// ---------------------------------------------------------------------------
void SubtractDsp::clearLearnSessionState() {
    learnedProfileTrust    = 1.0f;
    liveLearnConfidence    = 0.0f;
    learnQualityAccum      = 0.0f;
    learnInputEnergyAccum  = 0.0f;
    learnQualityFrames     = 0;
    learnFrames            = 0;
    learnTargetFrames      = 0;
    std::fill(learnAccum  .begin(), learnAccum  .end(), 0.0f);
    std::fill(learnAccumSq.begin(), learnAccumSq.end(), 0.0f);
    for (auto& h : learnHistory) h.clear();
}

void SubtractDsp::clearStreamingState() {
    std::fill(frameBuffer     .begin(), frameBuffer     .end(), 0.0f);
    std::fill(olaAcc          .begin(), olaAcc          .end(), 0.0f);
    std::fill(currPow         .begin(), currPow         .end(), 1.0e-8f);
    std::fill(prevMag         .begin(), prevMag         .end(), 0.0f);
    std::fill(tonalnessByBin  .begin(), tonalnessByBin  .end(), 0.0f);
    std::fill(barkMaskFloor   .begin(), barkMaskFloor   .end(), 0.0f);
    std::fill(harmonicFloor   .begin(), harmonicFloor   .end(), 0.0f);
    barkFluxAvg.fill(0.0f);
    barkTransientHold.fill(0);

    vxsuite::spectral::resetMinStats(msState, 1.0e-8f);
    std::fill(noisePowBlind.begin(), noisePowBlind.end(), 1.0e-8f);

    std::fill(xiDD        .begin(), xiDD        .end(), 1.0f);
    std::fill(presenceProb.begin(), presenceProb.end(), 0.5f);
    std::fill(cleanPowPrev.begin(), cleanPowPrev.end(), 1.0e-8f);
    std::fill(binSuppressCount.begin(), binSuppressCount.end(), 0);

    std::fill(gainTarget      .begin(), gainTarget      .end(), 1.0f);
    std::fill(gainSmooth      .begin(), gainSmooth      .end(), 1.0f);
    std::fill(gainSmoothedFreq.begin(), gainSmoothedFreq.end(), 1.0f);

    inRead  = inWrite  = inCount  = 0;
    outRead = outWrite = outCount = 0;
    frameBufferWritePos = 0;
    olaReadPos = olaWritePos = 0;
    sideDelayRead    = sideDelayWrite    = sideDelayCount    = 0;
    midDryDelayRead  = midDryDelayWrite  = midDryDelayCount  = 0;
    std::fill(sideDelay  .begin(), sideDelay  .end(), 0.0f);
    std::fill(midDryDelay.begin(), midDryDelay.end(), 0.0f);
    for (auto& ed : extraChannelDelays) {
        std::fill(ed.buffer.begin(), ed.buffer.end(), 0.0f);
        ed.readPos = ed.writePos = ed.available = 0;
    }

    prevFrameEnergy = 1.0e-8f;
    suppressionRamp = 0.0f;

    updateSmoothingCoeffs();

    // Pre-warm delay lines with silence so the side/mid-dry drain conditions
    // are satisfied from sample 0, preventing stereo collapse on the first block.
    if (outQueueCap > 0 && sideDelayCap > 0) {
        const size_t preLatency = fftSize - hop;
        for (size_t i = 0; i < preLatency; ++i)
            pushOutputSample(0.0f);
        sideDelayWrite   = 0;
        sideDelayRead    = (sideDelayCap - preLatency) % sideDelayCap;
        sideDelayCount   = preLatency;
        midDryDelayWrite = 0;
        midDryDelayRead  = (midDryDelayCap - preLatency) % midDryDelayCap;
        midDryDelayCount = preLatency;
        for (auto& ed : extraChannelDelays) {
            if (!ed.buffer.empty()) {
                ed.writePos  = 0;
                ed.readPos   = ed.buffer.size() - preLatency;
                ed.available = preLatency;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Public lifecycle
// ---------------------------------------------------------------------------
void SubtractDsp::prepare(double sampleRate, int maxBlockSize) {
    sr = sampleRate > 1000.0 ? sampleRate : 48000.0;
    fft.prepare(static_cast<int>(fftOrder));

    const size_t blockCap = static_cast<size_t>(
        std::max<int>(static_cast<int>(hop), std::max(1, maxBlockSize)));
    const float hopSec = static_cast<float>(hop) / std::max(1000.0f, static_cast<float>(sr));
    minStatsL = std::max(2, static_cast<int>(std::round(0.040f / hopSec)));
    minStatsD = std::max(4, static_cast<int>(std::round(1.5f / (hopSec * static_cast<float>(minStatsL)))));

    monoIn       .assign(blockCap,           0.0f);
    monoOut      .assign(blockCap,           0.0f);
    alignedMidDry.assign(blockCap,           0.0f);
    frame        .assign(fftSize * 2u,       0.0f);
    frameBuffer  .assign(fftSize,            0.0f);
    window       .assign(fftSize,            0.0f);
    olaAcc       .assign(blockCap + fftSize * 2u, 0.0f);

    vxsuite::spectral::prepareSqrtHannWindow(window, static_cast<int>(fftSize));

    currPow        .assign(bins, 1.0e-8f);
    prevMag        .assign(bins, 0.0f);
    tonalnessByBin .assign(bins, 0.0f);
    barkMaskFloor  .assign(bins, 0.0f);
    harmonicFloor  .assign(bins, 0.0f);
    lowBandStability.assign(bins, 0.0f);
    binToBark      .assign(bins, 0);
    erbFloor.clear();
    erbFloor.reserve(bins);

    // Phase-advance vector is needed only by prepareBarkScaleLayout
    std::vector<float> phaseAdvanceTmp(bins, 0.0f);
    vxsuite::spectral::prepareBarkScaleLayout<size_t>(
        sr, static_cast<int>(fftSize), binToBark, phaseAdvanceTmp, barkBandBins);

    for (size_t k = 0; k < bins; ++k) {
        const float hz = static_cast<float>(k) * static_cast<float>(sr) / static_cast<float>(fftSize);
        lowBandStability[k] = vxsuite::clamp01((700.0f - hz) / 450.0f);

        float erbW = 1.0f;
        if      (hz < 200.0f)  erbW = 0.5f;
        else if (hz < 1000.0f) erbW = 0.8f;
        else if (hz < 4000.0f) erbW = 1.0f;
        else if (hz < 8000.0f) erbW = 0.85f;
        else                   erbW = 0.6f;
        erbFloor.push_back(erbW);
    }

    noisePowFrozen.assign(bins, 1.0e-8f);
    learnAccum    .assign(bins, 0.0f);
    learnAccumSq  .assign(bins, 0.0f);
    learnHistory  .assign(bins, {});

    vxsuite::spectral::prepareMinStats(msState, bins, minStatsD, 1.0e-8f);
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

void SubtractDsp::reset() {
    signalPresenceAvg      = 0.5f;
    learningPrev           = learning;
    learnedProfileReady    = false;
    learnedSensitivity     = 0.0f;
    learnedProfileConfidence = 0.0f;
    std::fill(noisePowFrozen.begin(), noisePowFrozen.end(), 1.0e-8f);
    clearLearnSessionState();
    clearStreamingState();
}

void SubtractDsp::resetStreamingState() {
    signalPresenceAvg = 0.5f;
    learningPrev      = false;  // force re-detect of learn edge on next block
    clearLearnSessionState();
    clearStreamingState();
}

// ---------------------------------------------------------------------------
// Learn profile management
// ---------------------------------------------------------------------------
void SubtractDsp::clearLearnedProfile() {
    learning       = false;
    learningPrev   = false;
    learnedProfileReady    = false;
    learnedSensitivity     = 0.0f;
    learnedProfileConfidence = 0.0f;
    std::fill(noisePowFrozen.begin(), noisePowFrozen.end(), 1.0e-8f);
    clearLearnSessionState();
}

bool SubtractDsp::getLearnedProfileData(std::vector<float>& outProfile, float& outConfidence) const {
    if (!learnedProfileReady || noisePowFrozen.size() != bins) return false;
    outProfile   = noisePowFrozen;
    outConfidence = learnedProfileConfidence;
    return true;
}

void SubtractDsp::restoreLearnedProfile(const std::vector<float>& profile, float confidence) {
    if (profile.size() != bins) return;
    noisePowFrozen           = profile;
    learnedProfileConfidence  = confidence;
    learnedProfileTrust       = 1.0f;
    liveLearnConfidence       = 0.0f;
    learnedProfileReady       = true;
}

bool SubtractDsp::finalizeLearnedProfile() {
    if (learnFrames <= 0) {
        learningPrev = learning;
        return learnedProfileReady;
    }
    const float avgPower = learnInputEnergyAccum / static_cast<float>(learnFrames);
    if (avgPower < kMinimumLearnFramePower) {
        learningPrev = learning;
        return learnedProfileReady;
    }

    const float invN = 1.0f / static_cast<float>(learnFrames);
    for (size_t k = 0; k < bins; ++k) {
        const float mean   = std::max(1.0e-10f, learnAccum[k] * invN);
        const float meanSq = std::max(1.0e-10f, learnAccumSq[k] * invN);
        const float var    = std::max(0.0f, meanSq - mean * mean);
        noisePowFrozen[k]  = std::max(1.0e-10f, mean + (0.40f + 0.45f * learnedSensitivity) * std::sqrt(var));
    }
    learnedProfileReady = true;

    const float progress = getLearnProgress();
    const float quality  = learnQualityFrames > 0
        ? vxsuite::clamp01(learnQualityAccum / static_cast<float>(learnQualityFrames))
        : 0.0f;
    learnedProfileConfidence = vxsuite::clamp01(
        0.55f * progress + 0.45f * (std::isfinite(quality) ? quality : 0.0f));
    learnedProfileTrust  = 1.0f;
    liveLearnConfidence  = learnedProfileConfidence;
    learningPrev         = learning;
    return true;
}

float SubtractDsp::getLearnProgress() const {
    if (learnTargetFrames <= 0) return 0.0f;
    return vxsuite::clamp01(static_cast<float>(learnFrames) / static_cast<float>(learnTargetFrames));
}

float SubtractDsp::getLearnObservedSeconds() const {
    return static_cast<float>(learnFrames) * static_cast<float>(hop)
         / static_cast<float>(std::max(1000.0, sr));
}

// ---------------------------------------------------------------------------
// Per-frame stage: compute gainTarget[] via OM-LSA + spectral subtraction
// ---------------------------------------------------------------------------
void SubtractDsp::computeGainTargets(const FrameCtx& ctx,
                                     float energyRatio,
                                     bool  learnFrameHasSignal) {
    for (size_t k = 0; k < bins; ++k) {
        const float p = currPow[k];

        updateMinStats(k, p, presenceProb[k]);

        if (learning && learnFrameHasSignal) {
            learnAccum[k]   += p;
            learnAccumSq[k] += p * p;
        }

        const float n = activeNoise(k);

        // OM-LSA decision-directed SNR estimate
        const float Gamma    = p / n;
        const float xiInst   = std::max(0.0f, Gamma - 1.0f);
        const float xiCand   = std::max(0.0f, 0.97f * (cleanPowPrev[k] / n) + 0.03f * xiInst);
        xiDD[k] = std::isfinite(xiCand) ? xiCand : 0.0f;

        const float gH1     = xiDD[k] / (xiDD[k] + 1.0f);
        const float vk      = Gamma * gH1;
        const float LR      = (1.0f + xiDD[k]) * std::exp(-std::min(vk, 30.0f));
        const float p_H1Raw = 1.0f / (1.0f + (q_absence / (1.0f - q_absence)) * LR);
        const float p_H1    = std::isfinite(p_H1Raw) ? vxsuite::clamp01(p_H1Raw) : 0.0f;
        const float pCand   = 0.90f * presenceProb[k] + 0.10f * p_H1;
        presenceProb[k]     = std::isfinite(pCand) ? vxsuite::clamp01(pCand) : 0.5f;

        const float pSm = presenceProb[k];
        const float lnG = pSm         * std::log(std::max(kEps, gH1))
                        + (1.0f - pSm) * std::log(gH0_val);
        float g = std::exp(lnG);

        // Tonalness from neighbours
        const float left      = currPow[(k > 0u) ? k - 1u : k];
        const float right     = currPow[(k + 1u < bins) ? k + 1u : k];
        const float tonalness = vxsuite::spectral::tonalnessFromNeighbors(p, left, right);
        tonalnessByBin[k]     = tonalness;

        // SNR-weighted strength
        const float localSNR_dB  = 10.0f * std::log10(std::max(1.0f, Gamma));
        const float betaBin       = juce::jlimit(0.30f, 2.5f,
                                        ctx.strengthBaseGlobal / (1.0f + 0.045f * localSNR_dB));
        const bool  binInTransient = barkTransientHold[static_cast<size_t>(binToBark[k])] > 0
                                  || energyRatio > 1.38f;
        const float transientProt  = (ctx.labRaw || !binInTransient) ? 1.0f : 0.55f;
        const float strength       = std::max(0.20f, (betaBin * transientProt) - 0.40f * tonalness);
        g = std::pow(std::max(0.0f, g), strength);
        g = lerp(1.0f, g, ctx.wetCore);
        if (!ctx.labRaw && tonalness > 0.0f)
            g = lerp(g, 1.0f, 0.30f * tonalness);
        if (!ctx.labRaw && binInTransient)
            g = lerp(g, 1.0f, 0.40f);

        const float maskHead = vxsuite::clamp01(std::log10(std::max(1.0f, Gamma)) / 3.5f);
        const float minGain  = ctx.labRaw
            ? juce::jlimit(1.0e-4f, 0.10f, lerp(1.0e-3f, 0.05f, maskHead))
            : juce::jlimit(0.015f, 0.18f,  lerp(0.03f, 0.11f, maskHead) * 0.85f);
        const float binFloor = ctx.globalFloor * erbFloor[k];
        g = std::max(std::max(minGain, binFloor), vxsuite::clamp01(g));

        if (g <= minGain + 0.05f)
            binSuppressCount[k] = std::min(binSuppressCount[k] + 1, 24);
        else
            binSuppressCount[k] = std::max(0, binSuppressCount[k] - 1);

        if (g > gainSmooth[k]) {
            const float conf = vxsuite::clamp01(binSuppressCount[k] / 8.0f);
            g = lerp(g, gainSmooth[k], 0.55f * conf);
        }

        if (ctx.subtractEnabled) {
            const float mag       = std::sqrt(std::max(kEps, p));
            const float noiseMag  = std::sqrt(std::max(kEps, learnedSubtractNoise(k)));
            const float hz        = static_cast<float>(k) * static_cast<float>(sr)
                                  / static_cast<float>(fftSize);
            const float sBandRise = vxsuite::clamp01((hz - 120.0f) / 320.0f);
            const float sBandFall = vxsuite::clamp01((4600.0f - hz) / 1400.0f);
            const float sBandW    = sBandRise * sBandFall;
            const float steadyV   = binInTransient ? 0.0f : 1.0f;
            const float speechProt = ctx.voiceMode
                ? juce::jlimit(0.0f, 1.0f,
                      ctx.sourceProtect * sBandW * steadyV
                    * std::pow(vxsuite::clamp01(presenceProb[k]), 1.35f)
                    * (0.35f + 0.65f * tonalnessByBin[k])
                    * (0.55f + 0.45f * ctx.speechFocus))
                : 0.0f;
            const float rawMask = 0.52f * presenceProb[k]
                                + 0.28f * tonalnessByBin[k]
                                + 0.20f * (binInTransient ? 1.0f : 0.0f);
            const float protectMask = juce::jlimit(0.0f, 1.0f,
                ctx.guardStrictness * rawMask * (ctx.voiceMode ? 1.0f : 0.55f));
            const float effAlpha = ctx.subtractAlpha
                * (1.0f - (ctx.voiceMode ? 0.48f : 0.32f) * protectMask
                        - (ctx.voiceMode ? 0.34f : 0.0f) * speechProt);
            const float speechFloor  = ctx.voiceMode ? lerp(0.01f, 0.14f, speechProt) : 1.0e-4f;
            const float effFloor     = juce::jlimit(1.0e-4f, 0.12f,
                std::max(lerp(1.0e-4f, ctx.subtractFloor, protectMask), speechFloor));
            float gSub = juce::jlimit(effFloor, 1.0f,
                (mag - effAlpha * noiseMag) / std::max(kEps, mag));
            if (ctx.voiceMode)
                gSub = lerp(gSub, 1.0f, juce::jlimit(0.0f, 0.24f, 0.20f * speechProt));
            g = std::max(g * gSub, effFloor);
        }

        gainTarget[k] = std::isfinite(g) ? vxsuite::clamp01(g) : 1.0f;
        const float cleanPow = p * gainTarget[k] * gainTarget[k];
        cleanPowPrev[k] = std::isfinite(cleanPow) ? std::max(1.0e-10f, cleanPow) : 1.0e-10f;
    }
}

// ---------------------------------------------------------------------------
// Per-frame stage: Bark mask floor + frequency smoothing + harmonic comb + temporal smoothing
// ---------------------------------------------------------------------------
void SubtractDsp::smoothGains(bool subtractEnabled, float wetCore) {
    // Bark masking floor — protect tonal peaks from over-suppression
    std::fill(barkMaskFloor.begin(), barkMaskFloor.end(), 0.0f);
    for (size_t k = 0; k < bins; ++k) {
        const float n     = std::max(kEps, activeNoise(k));
        const float gamma = currPow[k] / n;
        if (tonalnessByBin[k] < 0.60f || gamma < 3.0f) continue;
        const int centerBand = binToBark[k];
        for (int band = std::max(0, centerBand - 3); band <= std::min(23, centerBand + 3); ++band) {
            const float dist   = static_cast<float>(std::abs(centerBand - band));
            const float spread = 0.05f * (1.0f - dist / 4.0f) * wetCore;
            for (size_t idx : barkBandBins[static_cast<size_t>(band)])
                barkMaskFloor[idx] = std::max(barkMaskFloor[idx], spread);
        }
    }
    for (size_t k = 0; k < bins; ++k)
        gainTarget[k] = std::max(gainTarget[k], barkMaskFloor[k]);

    // Frequency smoothing — SNR-weighted 3-tap
    for (size_t k = 0; k < bins; ++k) {
        const float localGamma = currPow[k] / std::max(kEps, activeNoise(k));
        const float snrWeight  = vxsuite::clamp01((localGamma - 1.0f) / 15.0f);
        const float wCenter    = lerp(0.50f, 0.90f, snrWeight);
        const float wSide      = 0.5f * (1.0f - wCenter);
        const size_t km        = (k > 0u) ? k - 1u : k;
        const size_t kp        = (k + 1u < bins) ? k + 1u : k;
        gainSmoothedFreq[k]    = wSide * gainTarget[km]
                               + wCenter * gainTarget[k]
                               + wSide * gainTarget[kp];
    }

    // Harmonic comb — lock gain valleys across harmonic series to prevent chirpy residuals
    std::fill(harmonicFloor.begin(), harmonicFloor.end(), 0.0f);
    const size_t maxF0Bin = std::max<size_t>(8u, bins / 5u);
    for (size_t k = 8; k < maxF0Bin; ++k) {
        const float n     = std::max(kEps, activeNoise(k));
        const float gamma = currPow[k] / n;
        if (gamma < 4.0f || tonalnessByBin[k] < 0.62f) continue;
        if (!(gainSmoothedFreq[k] > gainSmoothedFreq[k - 1]
           && gainSmoothedFreq[k] >= gainSmoothedFreq[k + 1])) continue;

        float harmonicMean  = 0.0f;
        int   harmonicCount = 0;
        for (int h = 1;; ++h) {
            const size_t hk = static_cast<size_t>(
                std::lround(static_cast<double>(h) * static_cast<double>(k)));
            if (hk >= bins || h > 10) break;
            harmonicMean += gainSmoothedFreq[hk];
            ++harmonicCount;
        }
        if (harmonicCount < 3) continue;

        harmonicMean /= static_cast<float>(harmonicCount);
        const float clampWidth = lerp(0.18f, 0.10f, wetCore);
        const float floorBoost = juce::jlimit(0.0f, 1.0f, harmonicMean - clampWidth);
        for (int h = 1;; ++h) {
            const size_t hk = static_cast<size_t>(
                std::lround(static_cast<double>(h) * static_cast<double>(k)));
            if (hk >= bins || h > 10) break;
            harmonicFloor[hk] = std::max(harmonicFloor[hk], floorBoost);
        }
    }
    for (size_t k = 0; k < bins; ++k)
        gainSmoothedFreq[k] = std::max(gainSmoothedFreq[k], harmonicFloor[k]);

    // Temporal smoothing with low-frequency stability guard
    for (size_t k = 0; k < bins; ++k) {
        float coeff = (gainSmoothedFreq[k] < gainSmooth[k]) ? attackCoeff : releaseCoeff;
        if (!subtractEnabled) {
            const float lfStab = lowBandStability[k];
            if (lfStab > 0.0f)
                coeff = std::max(coeff, lerp(0.93f, 0.992f, lfStab));
        }
        const float cand = coeff * gainSmooth[k] + (1.0f - coeff) * gainSmoothedFreq[k];
        gainSmooth[k] = std::isfinite(cand) ? cand : 1.0f;
    }
}

// ---------------------------------------------------------------------------
// Per-frame stage: apply gain to spectrum (input phase), IFFT, overlap-add
// ---------------------------------------------------------------------------
void SubtractDsp::applyGainAndOLA() {
    float presenceSum = 0.0f;

    for (size_t k = 0; k < bins; ++k) {
        const float gk   = lerp(1.0f, gainSmooth[k], suppressionRamp);
        const float reIn = frame[2u * k];
        const float imIn = (k == 0u || k == bins - 1u) ? 0.0f : frame[2u * k + 1u];
        const float mag  = std::sqrt(std::max(kEps, reIn * reIn + imIn * imIn)) * gk;

        if (k == 0u || k == bins - 1u) {
            frame[2u * k] = safe(reIn >= 0.0f ? mag : -mag);
        } else {
            // Keep input phase — standard spectral subtraction, no phase vocoder.
            const float phaseIn     = std::atan2(imIn, reIn);
            frame[2u * k]           = safe(mag * std::cos(phaseIn));
            frame[2u * k + 1u]      = safe(mag * std::sin(phaseIn));
        }

        presenceSum += std::isfinite(presenceProb[k]) ? presenceProb[k] : 0.5f;
    }

    const float presenceCand = 0.94f * signalPresenceAvg
                             + 0.06f * (presenceSum / static_cast<float>(bins));
    signalPresenceAvg = std::isfinite(presenceCand) ? vxsuite::clamp01(presenceCand) : 0.5f;

    fft.performInverse(frame.data());

    const size_t olaAccSize = olaAcc.size();
    for (size_t i = 0; i < fftSize; ++i)
        olaAcc[(olaWritePos + i) % olaAccSize] += frame[i] * window[i];
    for (size_t i = 0; i < hop; ++i) {
        pushOutputSample(safe(olaAcc[(olaReadPos + i) % olaAccSize]));
        olaAcc[(olaReadPos + i) % olaAccSize] = 0.0f;
    }
    olaWritePos = (olaWritePos + hop) % olaAccSize;
    olaReadPos  = (olaReadPos  + hop) % olaAccSize;
}

// ---------------------------------------------------------------------------
// Per-block stage: M/S stereo reconstruction with latency-aligned side channel
// ---------------------------------------------------------------------------
void SubtractDsp::reconstructStereo(juce::AudioBuffer<float>& buffer,
                                    int processed, int n, float wet) {
    float* l = buffer.getWritePointer(0);
    float* r = buffer.getWritePointer(1);
    const size_t sideLatency = fftSize - hop;

    // Push mid and side into their respective delay lines
    for (int i = 0; i < n; ++i) {
        const float leftDry  = l[processed + i];
        const float rightDry = r[processed + i];
        const float midDry   = 0.5f * (leftDry + rightDry);
        const float side     = 0.5f * (leftDry - rightDry);

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

    // Drain aligned dry mid (needed for extra-channel reconstruction)
    for (int i = 0; i < n; ++i) {
        float dryMidAligned = 0.0f;
        if (midDryDelayCount >= sideLatency) {
            dryMidAligned = midDryDelay[midDryDelayRead];
            midDryDelayRead = (midDryDelayRead + 1u) % midDryDelayCap;
            --midDryDelayCount;
        }
        alignedMidDry[static_cast<size_t>(i)] = dryMidAligned;
    }

    // Recombine denoised mid with latency-aligned side (unity side pass-through)
    for (int i = 0; i < n; ++i) {
        const float midOut = monoOut[static_cast<size_t>(i)];
        float sideDelayed  = 0.0f;
        if (sideDelayCount >= sideLatency) {
            sideDelayed = sideDelay[sideDelayRead];
            sideDelayRead = (sideDelayRead + 1u) % sideDelayCap;
            --sideDelayCount;
        }
        l[processed + i] = safe(midOut + sideDelayed);
        r[processed + i] = safe(midOut - sideDelayed);
    }

    // Additional channels: apply mono denoise delta
    const int channels = buffer.getNumChannels();
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
                if (ed.available >= sideLatency) {
                    dryAligned = ed.buffer[ed.readPos];
                    ed.readPos = (ed.readPos + 1) % ed.buffer.size();
                    --ed.available;
                } else {
                    dryAligned = 0.0f;
                }
            }
            const float dryMidAligned = alignedMidDry[static_cast<size_t>(i)];
            const float wetMono       = monoOut[static_cast<size_t>(i)];
            d[processed + i] = safe(dryAligned + (wetMono - dryMidAligned) * wet);
        }
    }
}

// ---------------------------------------------------------------------------
// processInPlace — orchestration
// ---------------------------------------------------------------------------
bool SubtractDsp::processInPlace(juce::AudioBuffer<float>& buffer,
                                 float amount,
                                 const vxsuite::ProcessOptions& options) {
    const int channels = buffer.getNumChannels();
    const int samples  = buffer.getNumSamples();
    if (channels <= 0 || samples <= 0) return false;

    const float wet     = vxsuite::clamp01(amount);
    const float subtract    = juce::jlimit(0.0f, 6.5f, options.subtract);
    const float sensitivity = juce::jlimit(0.0f, 2.0f, options.sensitivity);
    learnedSensitivity = sensitivity;

    const float subMixGlobal    = vxsuite::clamp01(subtract / 5.0f);
    const bool  labRaw          = options.labRawMode;
    const float wetCore         = labRaw ? wet : vxsuite::clamp01(wet * (1.0f - 0.10f * wet));
    const bool  subtractEnabled = subMixGlobal > 1.0e-4f && learnedProfileReady;

    // Learn phase transitions
    if (learning && !learningPrev) {
        clearLearnSessionState();
        learnTargetFrames = std::max(2, static_cast<int>(
            std::ceil(1.6f * static_cast<float>(sr) / static_cast<float>(hop))));
    } else if (!learning && learningPrev) {
        finalizeLearnedProfile();
    }
    learningPrev = learning;

    // Build frame-level context from options and wet amount
    FrameCtx ctx;
    ctx.wetCore            = wetCore;
    ctx.globalFloor        = labRaw ? 1.0e-4f
                                    : juce::jlimit(0.015f, 0.14f,
                                          lerp(0.05f, 0.015f, wetCore) * 0.85f);
    ctx.strengthBaseGlobal = lerp(0.38f, 1.12f, wetCore);
    ctx.subtractEnabled    = subtractEnabled;
    ctx.labRaw             = labRaw;
    ctx.voiceMode          = options.isVoiceMode;
    ctx.guardStrictness    = juce::jlimit(0.0f, 1.0f, options.guardStrictness);
    ctx.sourceProtect      = options.sourceProtect;
    ctx.speechFocus        = options.speechFocus;

    const float subtractMix = subtractEnabled ? vxsuite::clamp01(subMixGlobal) : 0.0f;
    const float profileAuthority = subtractEnabled
        ? juce::jlimit(options.isVoiceMode ? 0.52f : 0.64f, 1.0f,
              (options.isVoiceMode ? 0.52f : 0.64f)
                + (options.isVoiceMode ? 0.48f : 0.36f) * learnedProfileConfidence)
        : 0.0f;
    const float alphaMin = options.isVoiceMode ? 3.0f : 3.4f;
    const float alphaMax = options.isVoiceMode ? 4.8f : 6.0f;
    ctx.subtractAlpha = lerp(0.0f, lerp(alphaMin, alphaMax, profileAuthority), subtractMix);
    const float floorStart = options.isVoiceMode ? 0.06f : 0.03f;
    const float floorMin   = options.isVoiceMode ? 0.006f : 0.0018f;
    const float floorBest  = options.isVoiceMode ? 1.5e-4f : 5.0e-5f;
    ctx.subtractFloor = lerp(floorStart, lerp(floorMin, floorBest, profileAuthority), subtractMix);

    // Profile trust — how well the frozen profile still matches the current noise floor.
    // Uses noisePowBlind from the previous frame so this runs once per block, not per-bin.
    if (subtractEnabled) {
        float weightedMismatch = 0.0f, mismatchWeightSum = 0.0f;
        float floorCoverageSum = 0.0f,  floorCoverageWeightSum = 0.0f;
        for (size_t k = 1; k + 1 < bins; ++k) {
            const float frozen     = std::max(kEps, noisePowFrozen[k]);
            const float blind      = std::max(kEps, noisePowBlind[k]);
            const float hz         = static_cast<float>(k) * static_cast<float>(sr)
                                   / static_cast<float>(fftSize);
            const float lowMidFocus = vxsuite::clamp01((2400.0f - hz) / 1800.0f);
            const float bandFall    = vxsuite::clamp01((9000.0f - hz) / 2600.0f);
            const float bandWeight  = std::max(0.14f, lowMidFocus * bandFall);
            const float quietWeight = 1.0f - 0.80f * vxsuite::clamp01(presenceProb[k]);
            const float nearFloor   = vxsuite::clamp01(blind / std::max(kEps, currPow[k]));
            const float weight      = bandWeight * quietWeight * nearFloor;
            floorCoverageSum       += bandWeight * nearFloor;
            floorCoverageWeightSum += bandWeight;
            const float mismatch    = std::max(0.0f, std::log(blind / frozen));
            if (mismatch > 0.0f) {
                weightedMismatch    += weight * mismatch;
                mismatchWeightSum   += weight;
            }
        }
        const float confidenceFloor = lerp(0.22f, 0.44f, learnedProfileConfidence);
        const float floorCoverage   = floorCoverageSum / std::max(1.0e-4f, floorCoverageWeightSum);
        if (mismatchWeightSum > 0.08f) {
            const float mismatchAvg   = weightedMismatch / mismatchWeightSum;
            const float mismatchPenalty = vxsuite::clamp01((mismatchAvg - 0.16f) / 0.70f);
            const float trustTarget   = lerp(1.0f, confidenceFloor, mismatchPenalty);
            const float trustCoeff    = (trustTarget < learnedProfileTrust) ? 0.82f : 0.95f;
            const float trustCand     = trustCoeff * learnedProfileTrust
                                      + (1.0f - trustCoeff) * trustTarget;
            learnedProfileTrust = std::isfinite(trustCand)
                ? juce::jlimit(confidenceFloor, 1.0f, trustCand) : confidenceFloor;
        } else {
            const float speechDom   = std::isfinite(signalPresenceAvg)
                ? vxsuite::clamp01(signalPresenceAvg) : 0.5f;
            const float evidTarget  = juce::jlimit(0.0f, 1.0f,
                floorCoverage * (1.0f - 0.32f * speechDom) + 0.16f * (1.0f - speechDom));
            const float trustTarget = lerp(confidenceFloor, 1.0f, evidTarget);
            const float trustCand   = 0.90f * learnedProfileTrust + 0.10f * trustTarget;
            learnedProfileTrust = std::isfinite(trustCand)
                ? juce::jlimit(confidenceFloor, 1.0f, trustCand) : trustTarget;
        }
    } else {
        learnedProfileTrust = 1.0f;
    }

    const int chunkSize = std::max(1, maxBlockSizePrepared);
    int processed = 0;

    while (processed < samples) {
        const int n = std::min(chunkSize, samples - processed);

        // Downmix to mono, push into input queue
        const float invCh = 1.0f / static_cast<float>(channels);
        for (int i = 0; i < n; ++i) {
            float m = 0.0f;
            for (int ch = 0; ch < channels; ++ch)
                m += buffer.getReadPointer(ch)[processed + i];
            monoIn[static_cast<size_t>(i)] = safe(m * invCh);
            pushInputSample(monoIn[static_cast<size_t>(i)]);
        }

        // Process one FFT frame per hop
        while (inCount >= hop) {
            for (size_t i = 0; i < hop; ++i) {
                float s = 0.0f; popInputSample(s);
                frameBuffer[frameBufferWritePos] = safe(s);
                frameBufferWritePos = (frameBufferWritePos + 1u) % fftSize;
            }

            // Frame energy
            float frameEnergy = 1.0e-8f;
            for (size_t i = 0; i < fftSize; ++i)
                frameEnergy += frameBuffer[i] * frameBuffer[i];
            const float frameMeanPower     = frameEnergy / static_cast<float>(fftSize);
            const bool  learnFrameHasSignal = frameMeanPower >= kMinimumLearnFramePower;

            // Window and forward FFT
            std::fill(frame.begin(), frame.end(), 0.0f);
            for (size_t i = 0; i < fftSize; ++i)
                frame[i] = frameBuffer[(frameBufferWritePos + i) % fftSize] * window[i];
            fft.performForward(frame.data());

            // Bark-band flux transient detection
            std::array<float, 24> barkFlux {};
            for (size_t k = 0; k < bins; ++k) {
                const float re = frame[2u * k];
                const float im = (k == 0u || k == bins - 1u) ? 0.0f : frame[2u * k + 1u];
                const float p  = std::max(kEps, re * re + im * im);
                const float m  = std::sqrt(p);
                currPow[k] = p;
                barkFlux[static_cast<size_t>(binToBark[k])] += std::max(0.0f, m - prevMag[k]);
                prevMag[k] = m;
            }
            for (size_t b = 0; b < barkFluxAvg.size(); ++b) {
                barkFluxAvg[b] = 0.94f * barkFluxAvg[b] + 0.06f * barkFlux[b];
                const float fluxRatio = barkFlux[b] / std::max(1.0e-6f, barkFluxAvg[b] + 1.0e-6f);
                if (fluxRatio > 1.65f) barkTransientHold[b] = 3;
                else if (barkTransientHold[b] > 0) --barkTransientHold[b];
            }

            const float energyRatio = frameEnergy / std::max(1.0e-8f, prevFrameEnergy);
            prevFrameEnergy = frameEnergy;

            // Spectral flatness — for learn quality only
            float logPowMean = 0.0f, linPowMean = 0.0f;
            for (size_t k = 0; k < bins; ++k) {
                logPowMean += std::log(std::max(kEps, currPow[k]));
                linPowMean += currPow[k];
            }
            logPowMean /= static_cast<float>(bins);
            linPowMean /= static_cast<float>(bins);
            const float spectralFlatness = std::exp(logPowMean) / std::max(kEps, linPowMean);

            computeGainTargets(ctx, energyRatio, learnFrameHasSignal);
            smoothGains(subtractEnabled, wetCore);

            suppressionRamp = std::min(1.0f, suppressionRamp
                + static_cast<float>(hop) / std::max(1000.0f, static_cast<float>(sr)) / 0.70f);


            applyGainAndOLA();

            // Learn quality accumulation
            if (learning && learnFrameHasSignal) {
                ++learnFrames;
                learnInputEnergyAccum += frameMeanPower;
                const float noiseLike  = vxsuite::clamp01((spectralFlatness - 0.12f) / 0.38f);
                const float steady     = vxsuite::clamp01(1.0f - std::abs(
                    std::log(std::max(0.25f, std::min(4.0f, energyRatio)))) / 1.2f);
                const float quietSpeech = 1.0f - vxsuite::clamp01(
                    std::isfinite(signalPresenceAvg) ? signalPresenceAvg : 0.5f);
                const float qualityRaw = 0.15f * noiseLike + 0.45f * steady + 0.40f * quietSpeech
                                       - (energyRatio > 1.38f ? 0.12f : 0.0f);
                const float quality    = std::isfinite(qualityRaw)
                    ? juce::jlimit(0.0f, 1.0f, qualityRaw) : 0.0f;
                learnQualityAccum += quality;
                ++learnQualityFrames;
                const float cumQuality = learnQualityAccum / static_cast<float>(learnQualityFrames);
                liveLearnConfidence    = vxsuite::clamp01(
                    0.55f * getLearnProgress()
                  + 0.45f * (std::isfinite(cumQuality) ? cumQuality : 0.0f));
            }
        }

        // Drain output queue to monoOut
        float out = 0.0f;
        for (int i = 0; i < n; ++i) {
            if (!popOutputSample(out))
                out = (i > 0) ? monoOut[static_cast<size_t>(i - 1)] : 0.0f;
            monoOut[static_cast<size_t>(i)] = safe(out);
        }

        // Stereo output: M/S reconstruction; mono: write directly
        if (channels >= 2)
            reconstructStereo(buffer, processed, n, wet);
        else {
            float* d = buffer.getWritePointer(0);
            for (int i = 0; i < n; ++i)
                d[processed + i] = safe(monoOut[static_cast<size_t>(i)]);
        }

        processed += n;
    }
    return true;
}

} // namespace vxsuite::subtract
