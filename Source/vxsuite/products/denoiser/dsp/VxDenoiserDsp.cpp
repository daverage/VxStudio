#include "VxDenoiserDsp.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <numeric>

namespace vxsuite::denoiser {

// ── Helpers ───────────────────────────────────────────────────────────────────

float DenoiserDsp::safe(float x) noexcept {
    return std::isfinite(x) && std::fpclassify(x) != FP_SUBNORMAL ? x : 0.0f;
}

// ── prepare ───────────────────────────────────────────────────────────────────

void DenoiserDsp::prepare(const double sampleRate, const int maxBlockSize) {
    sr = (sampleRate > 1000.0) ? sampleRate : 48000.0;

    // Martin sub-window params: L ≈ 40 ms, D × L ≈ 1.5 s
    const float hopSec = static_cast<float>(kHop) / static_cast<float>(sr);
    msL = std::max(2, static_cast<int>(std::round(0.040f / hopSec)));
    msD = std::max(4, static_cast<int>(std::round(1.5f / (hopSec * static_cast<float>(msL)))));

    latencySamples = kFftSize - kHop;
    const int safeBlock = std::max(1, maxBlockSize);
    olaAccumSize   = safeBlock + kFftSize * 2;

    // ── sqrt-Hann analysis/synthesis window (periodic) ───────────────────────
    window.resize(kFftSize);
    for (int n = 0; n < kFftSize; ++n)
        window[n] = std::sqrt(0.5f - 0.5f * std::cos(
            2.0f * juce::MathConstants<float>::pi
            * static_cast<float>(n) / static_cast<float>(kFftSize)));

    fftObj     = std::make_unique<juce::dsp::FFT>(kFftOrder);
    olaAcc     .assign(olaAccumSize, 0.0f);
    inFifo     .assign(kFftSize,     0.0f);
    frameBuffer.assign(kFftSize,     0.0f);
    fftBuf     .assign(kFftSize * 2, 0.0f);
    monoOut    .assign(safeBlock,    0.0f);

    // ── Per-bin lookup tables ─────────────────────────────────────────────────
    const float binHz = static_cast<float>(sr) / static_cast<float>(kFftSize);
    binToBark   .resize(kBins);
    phaseAdv    .resize(kBins);
    erbKernelHW .resize(kBins);
    lfStab      .resize(kBins);
    for (auto& b : barkBins) b.clear();

    for (int k = 0; k < kBins; ++k) {
        const float hz   = static_cast<float>(k) * binHz;
        const int   bark = juce::jlimit(0, 23, static_cast<int>(std::floor(spectral::hzToBark(hz))));
        binToBark[k] = bark;
        barkBins[static_cast<size_t>(bark)].push_back(k);

        phaseAdv[k] = 2.0f * juce::MathConstants<float>::pi
                    * static_cast<float>(kHop) * static_cast<float>(k)
                    / static_cast<float>(kFftSize);

        // Moore & Glasberg (1990) ERB — drives smoother half-width
        const float erb = 24.7f * (4.37f * hz / 1000.0f + 1.0f);
        erbKernelHW[k] = juce::jlimit(1.0f, 10.0f, erb / binHz);

        // LF stability: boost smoother persistence below 700 Hz
        lfStab[k] = clamp01((700.0f - hz) / 450.0f);
    }

    // ── Resize all per-bin state vectors ─────────────────────────────────────
    auto resizeBin = [&](std::vector<float>& v, float init = 0.0f) {
        v.assign(kBins, init);
    };
    resizeBin(currPow,       kEps);
    resizeBin(prevMag);
    resizeBin(tonalness);
    resizeBin(prevPhaseIn);
    resizeBin(prevPhaseOut);
    resizeBin(gainTarget,    1.0f);
    resizeBin(gainSmooth,    1.0f);
    resizeBin(gainFreqSmooth,1.0f);
    resizeBin(harmonicFloor);
    resizeBin(barkMaskFloor);
    resizeBin(humTargetGain, 1.0f);
    resizeBin(narrowbandTargetGain, 1.0f);
    resizeBin(narrowbandConfidence);
    resizeBin(noisePow,      kEps);
    resizeBin(xiDD,          1.0f);
    resizeBin(presenceProb,  0.5f);
    resizeBin(cleanPowPrev,  kEps);

    suppressCount.assign(kBins, 0);

    msState.resize(kBins);
    for (auto& ms : msState) {
        ms.smoothPow = ms.subWinMin = ms.globalMin = kEps;
        ms.frameCount = ms.subWinIdx = 0;
        ms.subWindows.assign(static_cast<size_t>(msD), kEps);
    }

    // ── Stereo delay lines ────────────────────────────────────────────────────
    sideDelaySize    = latencySamples + safeBlock + 16;
    midDryDelaySize  = latencySamples + safeBlock + 16;
    sideDelayBuf   .assign(sideDelaySize,   0.0f);
    midDryDelayBuf .assign(midDryDelaySize, 0.0f);

    // ── Attack / release coefficients ────────────────────────────────────────
    attackCoeff  = std::exp(-hopSec / 0.026f);
    releaseCoeff = std::exp(-hopSec / 0.260f);

    reset();
}

// ── reset ─────────────────────────────────────────────────────────────────────

void DenoiserDsp::reset() {
    std::fill(inFifo     .begin(), inFifo     .end(), 0.0f);
    std::fill(frameBuffer.begin(), frameBuffer.end(), 0.0f);
    std::fill(olaAcc     .begin(), olaAcc     .end(), 0.0f);
    std::fill(fftBuf     .begin(), fftBuf     .end(), 0.0f);

    std::fill(currPow       .begin(), currPow       .end(), kEps);
    std::fill(prevMag       .begin(), prevMag       .end(), 0.0f);
    std::fill(tonalness     .begin(), tonalness     .end(), 0.0f);
    std::fill(prevPhaseIn   .begin(), prevPhaseIn   .end(), 0.0f);
    std::fill(prevPhaseOut  .begin(), prevPhaseOut  .end(), 0.0f);
    std::fill(gainTarget    .begin(), gainTarget    .end(), 1.0f);
    std::fill(gainSmooth    .begin(), gainSmooth    .end(), 1.0f);
    std::fill(gainFreqSmooth.begin(), gainFreqSmooth.end(), 1.0f);
    std::fill(harmonicFloor .begin(), harmonicFloor .end(), 0.0f);
    std::fill(barkMaskFloor .begin(), barkMaskFloor .end(), 0.0f);
    std::fill(humTargetGain.begin(), humTargetGain.end(), 1.0f);
    std::fill(narrowbandTargetGain.begin(), narrowbandTargetGain.end(), 1.0f);
    std::fill(narrowbandConfidence.begin(), narrowbandConfidence.end(), 0.0f);
    std::fill(noisePow      .begin(), noisePow      .end(), kEps);
    std::fill(xiDD          .begin(), xiDD          .end(), 1.0f);
    std::fill(presenceProb  .begin(), presenceProb  .end(), 0.5f);
    std::fill(cleanPowPrev  .begin(), cleanPowPrev  .end(), kEps);
    std::fill(suppressCount .begin(), suppressCount .end(), 0);

    for (auto& ms : msState) {
        ms.smoothPow = ms.subWinMin = ms.globalMin = kEps;
        ms.frameCount = ms.subWinIdx = 0;
        std::fill(ms.subWindows.begin(), ms.subWindows.end(), kEps);
    }

    barkFluxAvg.fill(0.0f);
    barkHold   .fill(0);
    humScores = { 0.0f, 0.0f };

    std::fill(sideDelayBuf  .begin(), sideDelayBuf  .end(), 0.0f);
    std::fill(midDryDelayBuf.begin(), midDryDelayBuf.end(), 0.0f);
    sideDelayWrite = sideDelayRead = sideDelayCount = 0;
    midDryDelayWrite = midDryDelayRead = midDryDelayCount = 0;
    smoothedSideRatio = 1.0f;
    prevSideScale     = 1.0f;
    stftFrameCount    = 0;

    inFifoWritePos = 0;
    hopFillCount   = 0;
    olaWritePos    = latencySamples;
    olaReadPos     = 0;

    phaseReady      = false;
    fifoLive        = false;
    firstFrame      = true;
    prevFrameEnergy = kEps;
    signalPresence  = 0.5f;
}

// ── updateMinStats ────────────────────────────────────────────────────────────

void DenoiserDsp::updateMinStats(const int k, const float p,
                                 const float presence) noexcept {
    MinStatsBin& ms = msState[static_cast<size_t>(k)];

    // IMCRA-style: use slower smoothing when signal is likely present
    const float alpha = (presence > 0.55f) ? 0.96f : kMsAlpha;
    ms.smoothPow = alpha * ms.smoothPow + (1.0f - alpha) * p;

    // Per-bin first-signal seed: pre-fill sub-windows so noisePow is valid
    // immediately rather than underestimating for the first D*L frames.
    if (!ms.subWindows.empty()
        && ms.subWindows[0] <= 1.0e-7f
        && ms.smoothPow > 1.0e-7f) {
        std::fill(ms.subWindows.begin(), ms.subWindows.end(), ms.smoothPow);
        ms.globalMin = ms.smoothPow;
        ms.subWinMin = ms.smoothPow;
        noisePow[k]  = kBmin * ms.smoothPow;
    }

    ms.subWinMin = std::min(ms.subWinMin, ms.smoothPow);
    if (++ms.frameCount >= msL) {
        ms.subWindows[static_cast<size_t>(ms.subWinIdx)] = ms.subWinMin;
        ms.subWinIdx  = (ms.subWinIdx + 1) % msD;
        ms.subWinMin  = ms.smoothPow;
        ms.frameCount = 0;
        ms.globalMin  = *std::min_element(ms.subWindows.begin(),
                                          ms.subWindows.end());
    }
    noisePow[k] = kBmin * std::max(kEps, ms.globalMin);
}

// ── processInPlace ────────────────────────────────────────────────────────────

bool DenoiserDsp::processInPlace(juce::AudioBuffer<float>& buffer,
                                 const float               amount,
                                 const ProcessOptions&     options) {
    const int numCh  = buffer.getNumChannels();
    const int numSmp = buffer.getNumSamples();
    if (numCh <= 0 || numSmp <= 0 || !fftObj) return false;

    const float wet = juce::jlimit(0.0f, 1.0f, amount);
    if (wet <= 0.0f) {
        fifoLive = false;  // mark FIFO as stale so next active call resets cleanly
        return false;
    }
    // Re-enable after bypass (wet was 0, FIFO/OLA frozen): reset STFT state to
    // avoid reading old frames that were accumulated before the bypass period.
    if (!fifoLive) {
        reset();
        fifoLive = true;
    }

    const int accSz = olaAccumSize;

    // ── Stereo M/S — push side & dry-mid into delay lines ────────────────────
    if (numCh >= 2) {
        const float* l = buffer.getReadPointer(0);
        const float* r = buffer.getReadPointer(1);
        for (int i = 0; i < numSmp; ++i) {
            const float side   = safe(0.5f * (l[i] - r[i]));
            const float midDry = safe(0.5f * (l[i] + r[i]));

            sideDelayBuf[sideDelayWrite] = side;
            sideDelayWrite = (sideDelayWrite + 1) % sideDelaySize;
            sideDelayCount = std::min(sideDelayCount + 1, sideDelaySize);

            midDryDelayBuf[midDryDelayWrite] = midDry;
            midDryDelayWrite = (midDryDelayWrite + 1) % midDryDelaySize;
            midDryDelayCount = std::min(midDryDelayCount + 1, midDryDelaySize);
        }
    }

    // ── Push mono mid into STFT FIFO, trigger frame every hop ─────────────────
    for (int i = 0; i < numSmp; ++i) {
        // Build mono mid from all channels
        float midIn = 0.0f;
        for (int ch = 0; ch < numCh; ++ch)
            midIn += buffer.getReadPointer(ch)[i];
        midIn = safe(midIn / static_cast<float>(numCh));

        inFifo[static_cast<size_t>(inFifoWritePos)] = midIn;
        inFifoWritePos = (inFifoWritePos + 1) % kFftSize;

        if (++hopFillCount == kHop) {
            hopFillCount = 0;
            processFrame(wet, options);
        }
    }

    // ── Drain OLA ring → monoOut ──────────────────────────────────────────────
    // sqrt-Hann applied at both analysis and synthesis (WOLA).
    // At 75% overlap the sum of (sqrt-Hann)² = Hann over 4 frames = 2.0 for all n,
    // so the raw OLA accumulator is 2× the desired output.  Multiply by 0.5 here.
    for (int i = 0; i < numSmp; ++i) {
        const int idx = olaReadPos % accSz;
        monoOut[static_cast<size_t>(i)] = safe(olaAcc[static_cast<size_t>(idx)] * 0.5f);
        olaAcc [static_cast<size_t>(idx)] = 0.0f;
        ++olaReadPos;
    }

    // ── Reconstruct stereo from mid + delayed side ────────────────────────────
    if (numCh >= 2) {
        float* l = buffer.getWritePointer(0);
        float* r = buffer.getWritePointer(1);

        // Per-block energy for side ratio (smoothed to avoid pumping)
        float midDryE = kEps, midWetE = kEps;
        for (int i = 0; i < numSmp; ++i) {
            const float mo = monoOut[static_cast<size_t>(i)];
            midWetE += mo * mo;

            float dryMid = 0.0f;
            if (midDryDelayCount > latencySamples) {
                dryMid = midDryDelayBuf[midDryDelayRead];
                midDryDelayRead  = (midDryDelayRead + 1) % midDryDelaySize;
                --midDryDelayCount;
            }
            midDryE += dryMid * dryMid;
        }
        const float rawRatio  = juce::jlimit(0.0f, 1.5f,
                                             std::sqrt(midWetE / midDryE));
        // 200 ms one-pole smoothing on the ratio — eliminates per-block pumping
        const float ratioAlpha = std::exp(
            -static_cast<float>(numSmp) / (0.200f * static_cast<float>(sr)));
        smoothedSideRatio = ratioAlpha * smoothedSideRatio
                          + (1.0f - ratioAlpha) * rawRatio;
        const float sideScale = juce::jlimit(0.50f, 1.0f,
            1.0f + (smoothedSideRatio - 1.0f) * 0.35f * wet);

        // Interpolate sideScale from prev block to current across samples to
        // avoid a step at the block boundary (block-rate step is otherwise
        // audible as a low-frequency thump when stereo image changes quickly).
        const float sideScaleStart = prevSideScale;
        const float sideScaleEnd   = sideScale;
        const float sideScaleInc   = (numSmp > 1)
            ? (sideScaleEnd - sideScaleStart) / static_cast<float>(numSmp - 1)
            : 0.0f;
        prevSideScale = sideScale;

        for (int i = 0; i < numSmp; ++i) {
            float sideD = 0.0f;
            if (sideDelayCount > latencySamples) {
                sideD = sideDelayBuf[sideDelayRead];
                sideDelayRead  = (sideDelayRead + 1) % sideDelaySize;
                --sideDelayCount;
            }
            const float mo   = monoOut[static_cast<size_t>(i)];
            const float ss   = sideScaleStart + sideScaleInc * static_cast<float>(i);
            l[i] = safe(mo + sideD * ss);
            r[i] = safe(mo - sideD * ss);
        }
    } else {
        float* d = buffer.getWritePointer(0);
        for (int i = 0; i < numSmp; ++i)
            d[i] = monoOut[static_cast<size_t>(i)];
    }

    return true;
}

void DenoiserDsp::applyHumAndNarrowbandSuppression(const float amount,
                                                   const ProcessOptions& options) noexcept {
    std::fill(humTargetGain.begin(), humTargetGain.end(), 1.0f);
    std::fill(narrowbandTargetGain.begin(), narrowbandTargetGain.end(), 1.0f);

    const bool voiceMode = options.isVoiceMode;
    const float binHz = static_cast<float>(sr) / static_cast<float>(kFftSize);
    const float guardLevel = juce::jlimit(0.0f, 1.0f, options.sourceProtect);
    const float humDepthBase = amount * (voiceMode ? 0.48f : 0.70f) * (1.0f - 0.20f * guardLevel);
    const float narrowbandDepthBase = amount * (voiceMode ? 0.42f : 0.58f) * (1.0f - 0.18f * guardLevel);

    for (int k = 2; k < kBins - 2; ++k) {
        const float hz = static_cast<float>(k) * binHz;
        const bool inTransient = barkHold[static_cast<size_t>(binToBark[k])] > 0;
        const float prominence = currPow[k] / std::max(kEps,
            0.25f * (currPow[k - 2] + currPow[k - 1] + currPow[k + 1] + currPow[k + 2]));
        const float gamma = currPow[k] / std::max(kEps, noisePow[k]);

        float candidate = spectral::clamp01((prominence - 2.6f) / 7.0f)
                        * spectral::clamp01((tonalness[k] - 0.45f) / 0.45f)
                        * spectral::clamp01((gamma - 1.6f) / 14.0f);
        if (inTransient)
            candidate *= 0.25f;
        if (voiceMode) {
            if (hz < 900.0f)
                candidate = 0.0f;
            else if (hz < 1600.0f)
                candidate *= spectral::clamp01((hz - 900.0f) / 700.0f);
        } else if (hz < 650.0f) {
            candidate *= spectral::clamp01((hz - 250.0f) / 400.0f);
        }
        candidate *= 1.0f - 0.45f * presenceProb[k];
        narrowbandConfidence[k] = 0.95f * narrowbandConfidence[k] + 0.05f * candidate;

        if (narrowbandConfidence[k] > 0.52f) {
            const float strength = spectral::clamp01((narrowbandConfidence[k] - 0.52f) / 0.48f);
            for (int d = -1; d <= 1; ++d) {
                const int idx = k + d;
                const float spread = (d == 0) ? 1.0f : 0.55f;
                const float minGain = juce::jlimit(0.26f, 1.0f,
                    1.0f - narrowbandDepthBase * strength * spread);
                narrowbandTargetGain[idx] = std::min(narrowbandTargetGain[idx], minGain);
            }
        }
    }

    constexpr std::array<float, 2> mainsFrequencies { 50.0f, 60.0f };
    for (size_t mainsIdx = 0; mainsIdx < mainsFrequencies.size(); ++mainsIdx) {
        const float mainsHz = mainsFrequencies[mainsIdx];
        float scoreSum = 0.0f;
        int scoreCount = 0;

        for (int harmonic = 1;; ++harmonic) {
            const float targetHz = mainsHz * static_cast<float>(harmonic);
            if (targetHz > 1800.0f)
                break;

            const int centerBin = juce::jlimit(2, kBins - 3,
                static_cast<int>(std::lround(targetHz / binHz)));
            int bestBin = centerBin;
            float bestPower = currPow[centerBin];
            for (int offset = -1; offset <= 1; ++offset) {
                const int idx = centerBin + offset;
                if (currPow[idx] > bestPower) {
                    bestPower = currPow[idx];
                    bestBin = idx;
                }
            }

            const float prominence = currPow[bestBin] / std::max(kEps,
                0.25f * (currPow[bestBin - 2] + currPow[bestBin - 1]
                       + currPow[bestBin + 1] + currPow[bestBin + 2]));
            const float gamma = currPow[bestBin] / std::max(kEps, noisePow[bestBin]);
            float candidate = spectral::clamp01((prominence - 2.2f) / 6.0f)
                            * spectral::clamp01((tonalness[bestBin] - 0.40f) / 0.60f)
                            * spectral::clamp01((gamma - 1.4f) / 10.0f);
            if (voiceMode && targetHz >= 80.0f && targetHz <= 320.0f)
                candidate *= 0.30f;
            if (barkHold[static_cast<size_t>(binToBark[bestBin])] > 0)
                candidate *= 0.40f;

            scoreSum += candidate;
            ++scoreCount;
        }

        const float frameScore = scoreCount > 0 ? (scoreSum / static_cast<float>(scoreCount)) : 0.0f;
        humScores[mainsIdx] = 0.93f * humScores[mainsIdx] + 0.07f * frameScore;
    }

    const size_t dominantHumIdx = (humScores[1] > humScores[0]) ? 1u : 0u;
    const float dominantScore = humScores[dominantHumIdx];
    if (dominantScore < 0.18f)
        return;

    const float mainsHz = mainsFrequencies[dominantHumIdx];
    const float humStrength = spectral::clamp01((dominantScore - 0.18f) / 0.60f);
    for (int harmonic = 1;; ++harmonic) {
        const float targetHz = mainsHz * static_cast<float>(harmonic);
        if (targetHz > 1800.0f)
            break;

        const int centerBin = juce::jlimit(2, kBins - 3,
            static_cast<int>(std::lround(targetHz / binHz)));
        int bestBin = centerBin;
        float bestPower = currPow[centerBin];
        for (int offset = -1; offset <= 1; ++offset) {
            const int idx = centerBin + offset;
            if (currPow[idx] > bestPower) {
                bestPower = currPow[idx];
                bestBin = idx;
            }
        }

        const float gamma = currPow[bestBin] / std::max(kEps, noisePow[bestBin]);
        const float lineStrength = spectral::clamp01((gamma - 1.2f) / 12.0f)
                                 * spectral::clamp01((tonalness[bestBin] - 0.35f) / 0.65f);
        const float voiceProtect = voiceMode
            ? juce::jlimit(0.35f, 1.0f, 1.0f - 0.45f * presenceProb[bestBin])
            : 1.0f;
        const float harmonicWeight = 1.0f / (1.0f + 0.08f * static_cast<float>(harmonic - 1));
        const float targetDepth = humDepthBase * humStrength * lineStrength * harmonicWeight * voiceProtect;

        for (int d = -1; d <= 1; ++d) {
            const int idx = bestBin + d;
            const float spread = (d == 0) ? 1.0f : 0.60f;
            const float minGain = juce::jlimit(0.16f, 1.0f, 1.0f - targetDepth * spread);
            humTargetGain[idx] = std::min(humTargetGain[idx], minGain);
        }
    }
}

// ── processFrame ──────────────────────────────────────────────────────────────

void DenoiserDsp::processFrame(const float amount,
                                const ProcessOptions& options) noexcept {
    ++stftFrameCount;

    // ── 1. Extract windowed frame ─────────────────────────────────────────────
    for (int n = 0; n < kFftSize; ++n) {
        const int ring = (inFifoWritePos + n) % kFftSize;
        fftBuf[static_cast<size_t>(n)]            = inFifo[static_cast<size_t>(ring)] * window[n];
        fftBuf[static_cast<size_t>(n + kFftSize)] = 0.0f;
    }

    // ── 2. Forward FFT ────────────────────────────────────────────────────────
    fftObj->performRealOnlyForwardTransform(fftBuf.data());

    // ── 3. Power + Bark flux ──────────────────────────────────────────────────
    std::array<float, 24> barkFlux {};
    float frameEnergy = kEps;
    float totalPow    = kEps;

    for (int k = 0; k < kBins; ++k) {
        const float re = fftBuf[static_cast<size_t>(2 * k)];
        const float im = (k == 0 || k == kBins - 1) ? 0.0f
                       : fftBuf[static_cast<size_t>(2 * k + 1)];
        const float p  = std::max(kEps, re * re + im * im);
        const float m  = std::sqrt(p);
        currPow[k]  = p;
        totalPow   += p;
        frameEnergy += p;
        barkFlux[static_cast<size_t>(binToBark[k])] += std::max(0.0f, m - prevMag[k]);
        prevMag[k]  = m;
    }

    // Per-bin seeding is handled inside updateMinStats on the first non-zero frame;
    // no global seed needed here — seeding at currPow caused immediate muting because
    // noisePow ≥ signal power → Γ < 1 → gains → floor for the first Martin-window
    // convergence period (~40 frames / 200 ms at 48 kHz).
    firstFrame = false;

    // ── 4. Bark transient detection ───────────────────────────────────────────
    for (size_t b = 0; b < 24; ++b) {
        barkFluxAvg[b] = 0.94f * barkFluxAvg[b] + 0.06f * barkFlux[b];
        const float ratio = barkFlux[b]
                          / std::max(1.0e-6f, barkFluxAvg[b] + 1.0e-6f);
        if (ratio > 1.65f)
            barkHold[b] = 3;
        else if (barkHold[b] > 0)
            --barkHold[b];
    }
    const float energyRatio  = frameEnergy / std::max(kEps, prevFrameEnergy);
    prevFrameEnergy = frameEnergy;

    // ── 5. OM-LSA per bin ─────────────────────────────────────────────────────
    const bool  voiceMode     = options.isVoiceMode;
    const float aggression    = juce::jlimit(0.3f, 2.5f,
                                    0.38f + amount * (options.lateTailAggression * 0.74f + 0.26f));
    const float guardLevel    = juce::jlimit(0.0f, 1.0f, options.sourceProtect);
    const float guardStrict   = juce::jlimit(0.0f, 1.0f, options.guardStrictness);
    const float globalFloor   = juce::jlimit(0.010f, 0.12f,
                                    0.06f - 0.045f * amount);

    for (int k = 0; k < kBins; ++k) {
        const float p = currPow[k];

        updateMinStats(k, p, presenceProb[k]);

        const float n    = std::max(kEps, noisePow[k]);
        const float Gamma = p / n;
        const float xiInst = std::max(0.0f, Gamma - 1.0f);

        // Decision-Directed a priori SNR
        xiDD[k] = std::max(0.0f, 0.97f * (cleanPowPrev[k] / n) + 0.03f * xiInst);

        // OM-LSA speech-presence probability & optimal gain
        const float gH1 = xiDD[k] / (xiDD[k] + 1.0f);
        const float vk  = Gamma * gH1;
        const float LR  = (1.0f + xiDD[k]) * std::exp(-std::min(vk, 30.0f));
        const float pH1 = 1.0f / (1.0f + (kQAbsence / (1.0f - kQAbsence)) * LR);
        presenceProb[k] = 0.90f * presenceProb[k] + 0.10f * pH1;

        const float pSm = presenceProb[k];
        const float lnG = pSm       * std::log(std::max(kEps, gH1))
                        + (1.0f - pSm) * std::log(kGH0);
        float g = std::exp(lnG);

        // Tonalness — protect spectral peaks from over-suppression
        const float left       = currPow[(k > 0) ? k - 1 : k];
        const float right      = currPow[(k + 1 < kBins) ? k + 1 : k];
        tonalness[k] = spectral::tonalnessFromNeighbors(p, left, right);

        // Suppression strength scaled by local SNR and user controls
        const float snr_dB  = 10.0f * std::log10(std::max(1.0f, Gamma));
        const float betaBin = juce::jlimit(0.30f, 2.5f,
                                           aggression / (1.0f + 0.045f * snr_dB));

        // Transient protection
        const bool  inTransient = barkHold[static_cast<size_t>(binToBark[k])] > 0
                                || energyRatio > 1.38f;
        const float transProtect = inTransient
                                 ? (1.0f - 0.45f * guardStrict)
                                 : 1.0f;
        const float strength = std::max(0.20f, (betaBin * transProtect) - 0.40f * tonalness[k]);

        g = std::pow(std::max(0.0f, g), strength);
        g = 1.0f + (g - 1.0f) * amount;  // wet blend

        // Tonal and transient restoration
        if (tonalness[k] > 0.0f)
            g = g + (1.0f - g) * (0.30f * tonalness[k]);
        if (inTransient)
            g = g + (1.0f - g) * (0.40f * guardStrict);

        // SNR-adaptive minimum gain floor
        const float maskHead = clamp01(snr_dB / 35.0f);
        const float minGain  = juce::jlimit(globalFloor, 0.18f,
                                            (0.03f + 0.08f * maskHead) * 0.85f);
        g = std::max(clamp01(g), minGain);

        // Anti-flicker: slow-release suppression counter
        // Increment fast when suppressed; decrement slowly (every 4 frames).
        if (g <= minGain + 0.05f) {
            suppressCount[k] = std::min(suppressCount[k] + 1, 24);
        } else {
            // Slow release: decrement every 4 frames only (gate on frame clock, not count value)
            if ((stftFrameCount & 3) == 0 && suppressCount[k] > 0)
                --suppressCount[k];
        }
        if (g > gainSmooth[k]) {
            const float conf = clamp01(static_cast<float>(suppressCount[k]) / 8.0f);
            g = g + (gainSmooth[k] - g) * (0.55f * conf);
        }

        gainTarget[k]   = clamp01(g);
        cleanPowPrev[k] = std::max(kEps, p * g * g);
    }

    applyHumAndNarrowbandSuppression(amount, options);
    for (int k = 0; k < kBins; ++k)
        gainTarget[k] = std::min(gainTarget[k], std::min(humTargetGain[k], narrowbandTargetGain[k]));

    // ── 6. Bark masking floor (protect tonal bins from creating hollow pockets) ──
    std::fill(barkMaskFloor.begin(), barkMaskFloor.end(), 0.0f);
    for (int k = 0; k < kBins; ++k) {
        if (tonalness[k] < 0.60f || currPow[k] / noisePow[k] < 3.0f)
            continue;
        const int cb = binToBark[k];
        for (int b = std::max(0, cb - 3); b <= std::min(23, cb + 3); ++b) {
            const float dist   = static_cast<float>(std::abs(cb - b));
            const float spread = 0.05f * (1.0f - dist / 4.0f) * amount;
            for (int idx : barkBins[static_cast<size_t>(b)])
                barkMaskFloor[idx] = std::max(barkMaskFloor[idx], spread);
        }
    }
    for (int k = 0; k < kBins; ++k)
        gainTarget[k] = std::max(gainTarget[k], barkMaskFloor[k]);

    // ── 7. ERB-adaptive frequency smoothing ───────────────────────────────────
    // Variable triangular kernel width per bin — eliminates high-frequency
    // graininess without over-smoothing low-frequency detail.
    for (int k = 0; k < kBins; ++k) {
        const int hw = static_cast<int>(erbKernelHW[k]);  // half-width in bins
        float gSum = 0.0f, wSum = 0.0f;
        for (int d = -hw; d <= hw; ++d) {
            const int idx = juce::jlimit(0, kBins - 1, k + d);
            const float w = static_cast<float>(hw + 1 - std::abs(d)); // triangular
            gSum += w * gainTarget[idx];
            wSum += w;
        }
        gainFreqSmooth[k] = (wSum > kEps) ? (gSum / wSum) : gainTarget[k];
    }

    // ── 8. Harmonic comb protection (voice mode only) ─────────────────────────
    // Locks gain across the harmonic series to prevent isolated dips that
    // create chirpy/robotic artefacts on pitched sources.
    if (voiceMode) {
        std::fill(harmonicFloor.begin(), harmonicFloor.end(), 0.0f);
        const int maxF0Bin = std::max(8, kBins / 5);
        for (int k = 8; k < maxF0Bin; ++k) {
            if (currPow[k] / noisePow[k] < 4.0f || tonalness[k] < 0.62f)
                continue;
            if (!(gainFreqSmooth[k] > gainFreqSmooth[k - 1]
               && gainFreqSmooth[k] >= gainFreqSmooth[k + 1]))
                continue;
            float hMean = 0.0f;
            int   hCnt  = 0;
            for (int h = 1; h <= 10; ++h) {
                const int hk = h * k;
                if (hk >= kBins) break;
                hMean += gainFreqSmooth[hk];
                ++hCnt;
            }
            if (hCnt < 3) continue;
            hMean /= static_cast<float>(hCnt);
            const float clampW    = 0.10f + 0.08f * (1.0f - amount);
            const float floorBoost = juce::jlimit(0.0f, 1.0f, hMean - clampW);
            const float protectW   = 0.10f + 0.15f * guardLevel;
            for (int h = 1; h <= 10; ++h) {
                const int hk = h * k;
                if (hk >= kBins) break;
                harmonicFloor[hk] = std::max(harmonicFloor[hk],
                                             floorBoost * (1.0f - protectW));
            }
        }
        for (int k = 0; k < kBins; ++k)
            gainFreqSmooth[k] = std::max(gainFreqSmooth[k], harmonicFloor[k]);
    }

    // ── 9. Temporal smoothing (attack/release + LF stability) ─────────────────
    for (int k = 0; k < kBins; ++k) {
        float coeff = (gainFreqSmooth[k] < gainSmooth[k]) ? attackCoeff
                                                           : releaseCoeff;
        const float lf = lfStab[k];
        if (lf > 0.0f && voiceMode)
            coeff = std::max(coeff, 0.93f + 0.062f * lf);
        gainSmooth[k] = coeff * gainSmooth[k]
                      + (1.0f - coeff) * gainFreqSmooth[k];
    }

    // ── 10. Gain application + phase-vocoder synthesis ────────────────────────
    float presSum = 0.0f;
    for (int k = 0; k < kBins; ++k) {
        const float gk   = gainSmooth[k];
        const float reIn = fftBuf[static_cast<size_t>(2 * k)];
        const float imIn = (k == 0 || k == kBins - 1) ? 0.0f
                         : fftBuf[static_cast<size_t>(2 * k + 1)];
        const float mag  = std::sqrt(std::max(kEps, reIn * reIn + imIn * imIn)) * gk;

        if (k == 0 || k == kBins - 1) {
            fftBuf[static_cast<size_t>(2 * k)] = safe(reIn >= 0.0f ? mag : -mag);
        } else {
            const float phIn = std::atan2(imIn, reIn);
            float phOut = phIn;
            if (phaseReady) {
                // std::remainder — no loop, handles arbitrarily large jumps
                const float dphi = spectral::wrapPi((phIn - prevPhaseIn[k]) - phaseAdv[k]);
                phOut = spectral::wrapPi(prevPhaseOut[k] + phaseAdv[k] + dphi);
            }
            fftBuf[static_cast<size_t>(2 * k)]     = safe(mag * std::cos(phOut));
            fftBuf[static_cast<size_t>(2 * k + 1)] = safe(mag * std::sin(phOut));
            prevPhaseIn [k] = phIn;
            prevPhaseOut[k] = phOut;
        }
        presSum += presenceProb[k];
    }
    phaseReady = true;
    signalPresence = 0.94f * signalPresence
                   + 0.06f * (presSum / static_cast<float>(kBins));

    // ── 11. Inverse FFT + overlap-add ─────────────────────────────────────────
    fftObj->performRealOnlyInverseTransform(fftBuf.data());

    const int accSz = olaAccumSize;
    for (int n = 0; n < kFftSize; ++n) {
        const int pos = (olaWritePos + n) % accSz;
        olaAcc[static_cast<size_t>(pos)] += fftBuf[static_cast<size_t>(n)] * window[n];
    }
    olaWritePos = (olaWritePos + kHop) % accSz;
}

} // namespace vxsuite::denoiser
