#include "VxDeverbSpectralProcessor.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace vxsuite::deverb {

// ── Construction / destruction ────────────────────────────────────────────────

SpectralProcessor::SpectralProcessor()  = default;
SpectralProcessor::~SpectralProcessor() = default;

void SpectralProcessor::setChannelCount(const int numChannels) {
    preparedChannels = std::max(0, numChannels);
    if (fft.isReady())
        allocateChannels(preparedChannels);
}

void SpectralProcessor::setRt60PresetSeconds(const float rt60Seconds) {
    rt60PresetSeconds = std::max(0.0f, rt60Seconds);
    if (rt60PresetSeconds > 0.0f)
        rt60Estimator.setFixedRt60(rt60PresetSeconds);
    else
        rt60Estimator.clearFixedRt60();
}

void SpectralProcessor::clearRt60Preset() {
    rt60PresetSeconds = 0.0f;
    rt60Estimator.clearFixedRt60();
}

void SpectralProcessor::setDeterministicReset(const bool shouldUseDefaultRt60) noexcept {
    useDeterministicReset = shouldUseDefaultRt60;
}

void SpectralProcessor::setOverSubtract(const float newOverSubtract) noexcept {
    overSubtract = juce::jlimit(1.0f, 2.5f, newOverSubtract);
}

float SpectralProcessor::getTrackedRt60Seconds([[maybe_unused]] const int channel) const noexcept {
    return rt60Estimator.getEstimatedRt60();
}

// ── prepare ───────────────────────────────────────────────────────────────────

void SpectralProcessor::prepare(const double sampleRate, const int maxBlockSize) {
    sr = (sampleRate > 1000.0) ? sampleRate : 48000.0;

    // ── Choose FFT order so that the analysis window is ≥ 21 ms ──────────────
    // 1024 @ 44.1/48 kHz (21.3 ms), 2048 @ 88.2/96 kHz (21.3 ms).
    constexpr float kTargetWindowMs = 21.3f;
    const int targetSamples =
        static_cast<int>(static_cast<float>(sr) * kTargetWindowMs / 1000.0f);
    int order = 10; // minimum: N = 1024
    while ((1 << order) < targetSamples && order < 13) ++order;

    fftSize  = 1 << order;
    hopSize  = fftSize / 4;              // 75 % overlap (COLA with periodic Hann)
    numBins  = fftSize / 2 + 1;         // positive-frequency bins including DC + Nyquist

    // Number of history frames spanning the early/late boundary (~50 ms).
    // At 48 kHz, hop=256: tHistFrames = ceil(0.05 × 48000 / 256) = 10 frames (12.8 ms each).
    tHistFrames = std::max(2,
        static_cast<int>(std::ceil(kTBoundaryS * static_cast<float>(sr)
                                   / static_cast<float>(hopSize))));

    latencySamples = fftSize - hopSize;   // = 3 × hopSize (reported to host via PDC)

    // OLA accumulator sized to survive the largest possible host block without
    // Phase-1 writes reaching Phase-2 reads.
    // Maximum distance from olaReadPos to the final write:
    //   latency + floor(maxBlockSize/hopSize)×hopSize + fftSize − 1
    //   ≤ (fftSize − hopSize) + maxBlockSize + fftSize − 1  < maxBlockSize + 2×fftSize  ✓
    const int safeBlock = std::max(1, maxBlockSize);
    olaAccumSize = safeBlock + fftSize * 2;

    // ── Periodic (DFT-even) Hann window ──────────────────────────────────────
    // w(n) = 0.5 − 0.5·cos(2π n / N),  n = 0 .. N−1.
    // "Periodic" means w(N) ≡ w(0); this is the correct form for STFT analysis.
    window.resize(static_cast<size_t>(fftSize));
    for (int n = 0; n < fftSize; ++n)
        window[static_cast<size_t>(n)] =
            0.5f - 0.5f * std::cos(2.0f * juce::MathConstants<float>::pi
                                   * static_cast<float>(n)
                                   / static_cast<float>(fftSize));

    olaNorm.assign(static_cast<size_t>(fftSize), 0.0f);
    const int overlapCount = fftSize / hopSize;
    for (int n = 0; n < fftSize; ++n) {
        float norm = 0.0f;
        for (int overlap = 0; overlap < overlapCount; ++overlap) {
            const int idx = (n + overlap * hopSize) % fftSize;
            const float w = window[static_cast<size_t>(idx)];
            norm += w * w;
        }
        olaNorm[static_cast<size_t>(n)] = std::max(norm, 1.0e-6f);
    }

    fft.prepare(order);

    // Allocate WPE scratch buffers (pre-allocated, no audio-thread allocation)
    wpeReScratch.assign(static_cast<size_t>(numBins), 0.0f);
    wpeImScratch.assign(static_cast<size_t>(numBins), 0.0f);

    rt60Estimator.prepare(sr);
    if (rt60PresetSeconds > 0.0f)
        rt60Estimator.setFixedRt60(rt60PresetSeconds);

    // Pre-compute speech-range bin boundaries for voice protection.
    // kVoiceFloor is applied to these bins when voiceMode is active.
    speechBinLo = std::max(0, static_cast<int>(200.0f * static_cast<float>(fftSize) / static_cast<float>(sr)));
    speechBinHi = std::min(numBins - 1, static_cast<int>(4000.0f * static_cast<float>(fftSize) / static_cast<float>(sr)));

    allocateChannels(preparedChannels);
}

// ── allocateAndResetChannel ───────────────────────────────────────────────────

void SpectralProcessor::allocateChannels(const int numChannels) {
    chans.resize(static_cast<size_t>(std::max(0, numChannels)));
    for (auto& ch : chans)
        allocateAndResetChannel(ch);
}

void SpectralProcessor::allocateAndResetChannel(ChannelState& ch) const {
    ch.inFifo.assign(static_cast<size_t>(fftSize),       0.0f);
    ch.olaAccum.assign(static_cast<size_t>(olaAccumSize), 0.0f);
    ch.fftBuf.assign(static_cast<size_t>(fftSize * 2),   0.0f);
    ch.magSqHist.assign(static_cast<size_t>(tHistFrames * numBins), 0.0f);
    ch.gainSmooth.assign(static_cast<size_t>(numBins),   1.0f);
    ch.logGain.assign(static_cast<size_t>(numBins), 0.0f);

    // K=5 taps: 4× less O(K²) RLS work vs K=10; speech-band bins only (~82 vs 513)
    // gives ~25× total WPE speedup with adequate quality for short vocal reverb.
    const int numSpeechBins = std::max(1, speechBinHi - speechBinLo + 1);
    ch.wpeStage.prepare(numSpeechBins, 5);

    ch.inFifoWritePos = 0;
    ch.hopFillCount   = 0;
    ch.histWriteIdx   = 0;

    // Pre-advance the OLA write position by latency so that the first
    // processed frame's output lands at olaReadPos + latency.
    // Invariant maintained throughout: olaWritePos − olaReadPos ≈ latency.
    ch.olaWritePos = latencySamples;
    ch.olaReadPos  = 0;
}

// ── reset ─────────────────────────────────────────────────────────────────────

void SpectralProcessor::reset() {
    rt60Estimator.reset();
    if (rt60PresetSeconds > 0.0f)
        rt60Estimator.setFixedRt60(rt60PresetSeconds);
    for (auto& ch : chans) allocateAndResetChannel(ch);
}

// ── processInPlace ────────────────────────────────────────────────────────────

bool SpectralProcessor::processInPlace(juce::AudioBuffer<float>& buffer,
                                       const float               amount,
                                       const ProcessOptions&     /*options*/) {
    const int numCh  = buffer.getNumChannels();
    const int numSmp = buffer.getNumSamples();
    if (numCh <= 0 || numSmp <= 0 || !fft.isReady()) return false;

    if (static_cast<int>(chans.size()) != numCh) {
        jassertfalse;
        return false;
    }

    const float amt = juce::jlimit(0.0f, 1.0f, amount);

    // Feed channel-0 input into the shared RT60 estimator before processing.
    rt60Estimator.pushSamples(buffer.getReadPointer(0), numSmp);

    // Compute shared LRSV coefficient from the estimator.
    const float lrsvCoeff = lrsvCoeffFromRt60(rt60Estimator.getEstimatedRt60());

    for (int c = 0; c < numCh; ++c) {
        auto& ch        = chans[static_cast<size_t>(c)];
        const float* in = buffer.getReadPointer(c);
        float*       out = buffer.getWritePointer(c);

        // ── Phase 1: push input into ring buffer, trigger frames every hop ───
        //
        // Input at sample i is written to inFifo BEFORE the frame is triggered
        // so that processFrame() always sees the freshest fftSize samples.
        //
        // The OLA accumulator is written by processFrame() ahead of the current
        // olaReadPos by at least latency samples, so Phase-2 reads are always
        // from data that has been fully overlap-added by all contributing frames.
        for (int i = 0; i < numSmp; ++i) {
            ch.inFifo[static_cast<size_t>(ch.inFifoWritePos)] = in[i];
            ch.inFifoWritePos = (ch.inFifoWritePos + 1) % fftSize;

            if (++ch.hopFillCount == hopSize) {
                ch.hopFillCount = 0;
                processFrame(ch, amt, lrsvCoeff);
            }
        }

        // ── Phase 2: read accumulated output ─────────────────────────────────
        //
        // All frames that can contribute to output positions [0..numSmp−1] have
        // been processed by Phase 1 (proved in design notes: for any numSmp,
        // the latest contributing frame was triggered during Phase 1).
        const auto accSz = static_cast<size_t>(olaAccumSize);
        for (int i = 0; i < numSmp; ++i) {
            const size_t idx = static_cast<size_t>(ch.olaReadPos) % accSz;
            const float v = ch.olaAccum[idx] * kOlaGain;
            out[i] = std::isfinite(v) ? v : 0.0f;
            ch.olaAccum[idx] = 0.0f; // clear slot after reading
            ++ch.olaReadPos;
        }
    }

    return true;
}

// ── processFrame ──────────────────────────────────────────────────────────────
//
// Called every hopSize input samples.  Zero heap allocations.
// All working memory was pre-allocated by prepare() / allocateAndResetChannel().

void SpectralProcessor::processFrame(ChannelState& ch,
                                     const float   amount,
                                     const float   lrsvCoeff) noexcept {
    const auto fftSz = static_cast<size_t>(fftSize);
    const auto nbins = static_cast<size_t>(numBins);

    // ── 1. Extract the last fftSize input samples and apply analysis window ───
    //
    // inFifoWritePos is the NEXT write position = the OLDEST sample position.
    // Iterating [inFifoWritePos .. inFifoWritePos + fftSize − 1] (mod fftSize)
    // gives chronological order: oldest → newest.
    for (size_t n = 0; n < fftSz; ++n) {
        const size_t ringIdx = (static_cast<size_t>(ch.inFifoWritePos) + n) % fftSz;
        ch.fftBuf[n]          = ch.inFifo[ringIdx] * window[n];
        ch.fftBuf[n + fftSz]  = 0.0f; // zero-pad upper half (required by JUCE FFT)
    }

    // ── 2. Forward real FFT ───────────────────────────────────────────────────
    //
    // Input:  fftBuf[0..N−1] = windowed signal,  fftBuf[N..2N−1] = 0
    // Output: fftBuf[2k] = Re(X[k]),  fftBuf[2k+1] = Im(X[k]),  k = 0..N−1
    fft.performForward(ch.fftBuf.data());

    // ── 3. LRSV Wiener gain per bin ───────────────────────────────────────────
    //
    // History ring semantics (see header documentation):
    //   ch.magSqHist[ histWriteIdx × numBins + k ]  contains the power from
    //   tHistFrames frames ago — exactly T_boundary seconds of delay.
    //   We READ it first, then OVERWRITE it with the current frame's power.
    //   This avoids a separate "delayed index" calculation.
    const size_t histBase = static_cast<size_t>(ch.histWriteIdx) * nbins;

    for (size_t k = 0; k < nbins; ++k) {
        const float re = ch.fftBuf[2 * k];
        const float im = ch.fftBuf[2 * k + 1];
        const float curPow     = re * re + im * im;
        const float delayedPow = ch.magSqHist[histBase + k]; // T_boundary ago

        // Late-reverberant spectral variance (Habets 2009, eq. 11):
        //   Γ_late(m,k) = κ · exp(−2δT) · |Y(m − T_frames, k)|²
        const float lrsv = kKappa * lrsvCoeff * delayedPow * overSubtract;

        // Wiener power gain, floored to prevent full bin nulling.
        // suppressRatio is clamped to [0,1] so that LRSV noise exceeding the
        // current bin power cannot drive the subtraction past 100% and cause
        // catastrophic over-suppression across the entire spectrum.
        //   G²(m,k) = max( 1 − amount·min(Γ_late/|Y|², 1) , floor² )
        // In voice mode, bins in the 200–4000 Hz speech range use kVoiceFloor
        // (~−9 dB) to protect vocal fundamentals and formants from over-suppression,
        // consistent with Polish's voice-preserve policy.
        const float suppressRatio = std::min(1.0f, lrsv / std::max(curPow, 1.0e-20f));
        const bool  inSpeechBand  = voiceMode && (static_cast<int>(k) >= speechBinLo)
                                               && (static_cast<int>(k) <= speechBinHi);
        const float binFloor      = inSpeechBand ? kVoiceFloor : kFloor;
        const float wienerPow = std::max(binFloor * binFloor,
                                         1.0f - amount * suppressRatio);
        const float gainTarget = std::sqrt(wienerPow);

        // Per-bin IIR temporal smoothing suppresses musical noise:
        //   G_smooth(m,k) = α·G_smooth(m−1,k) + (1−α)·G(m,k)
        ch.gainSmooth[k] = kGainAlpha * ch.gainSmooth[k]
                           + (1.0f - kGainAlpha) * gainTarget;

        // Overwrite history slot with current frame power.
        ch.magSqHist[histBase + k] = curPow;
    }

    if (!debugNoCepstral) {
        // Log-domain smoothing across neighboring frequency bins reduces isolated
        // spectral spikes without relying solely on temporal averaging.
        for (size_t k = 0; k < nbins; ++k)
            ch.logGain[k] = std::log(ch.gainSmooth[k] + 1.0e-10f);

        for (size_t k = 0; k < nbins; ++k) {
            float sum = 0.0f;
            int count = 0;
            for (int d = -kCepLifter; d <= kCepLifter; ++d) {
                const int idx = static_cast<int>(k) + d;
                if (idx >= 0 && idx < numBins) {
                    sum += ch.logGain[static_cast<size_t>(idx)];
                    ++count;
                }
            }
            ch.gainSmooth[k] = std::exp(sum / static_cast<float>(count));
        }
    }

    for (size_t k = 0; k < nbins; ++k) {
        ch.fftBuf[2 * k]     *= ch.gainSmooth[k];
        ch.fftBuf[2 * k + 1] *= ch.gainSmooth[k];
    }

    // ── 4. WPE voice-mode stage (optional) ───────────────────────────────────
    //
    // Only active when voiceMode is enabled.  WpeStage operates on the
    // positive-frequency bins of fftBuf (interleaved re/im).
    if (voiceMode) {
        // WPE is restricted to the speech band (200–4 kHz, ~82 bins).
        // LRSV Wiener gains already cover the full spectrum; WPE adds
        // speech-targeted prediction only where it helps vocals.
        const int numSpeechBins = std::max(1, speechBinHi - speechBinLo + 1);
        for (int k = 0; k < numSpeechBins; ++k) {
            wpeReScratch[static_cast<size_t>(k)] = ch.fftBuf[2 * (speechBinLo + k)];
            wpeImScratch[static_cast<size_t>(k)] = ch.fftBuf[2 * (speechBinLo + k) + 1];
        }
        // Scale WPE with the same amount as the Wiener gains.  At amount=0 this
        // triggers WPE's bypass path (history updated, no filter application), so
        // the output is clean even before the RLS filter has converged.
        ch.wpeStage.processSpectrum(wpeReScratch.data(), wpeImScratch.data(), wpeAmount * amount);
        for (int k = 0; k < numSpeechBins; ++k) {
            ch.fftBuf[2 * (speechBinLo + k)]     = wpeReScratch[static_cast<size_t>(k)];
            ch.fftBuf[2 * (speechBinLo + k) + 1] = wpeImScratch[static_cast<size_t>(k)];
        }
    }

    // ── 5. Inverse real FFT ───────────────────────────────────────────────────
    //
    // Input:  fftBuf[2k], fftBuf[2k+1] = Re, Im of full N-point spectrum
    // Output: fftBuf[0..N−1] = real signal (JUCE normalises by 1/N internally)
    fft.performInverse(ch.fftBuf.data());

    // ── 6. Overlap-add into the OLA output accumulator ────────────────────────
    //
    // The IFFT output is added to olaAccum starting at olaWritePos.
    // olaWritePos was initialised to latency = fftSize − hopSize at reset, so
    // at any point olaWritePos − olaReadPos ≈ latency.  The ring size
    // (olaAccumSize = maxBlockSize + 2×fftSize) guarantees no overrun.
    const auto accSz = static_cast<size_t>(olaAccumSize);
    for (size_t n = 0; n < fftSz; ++n) {
        const size_t pos =
            static_cast<size_t>(ch.olaWritePos + static_cast<int>(n)) % accSz;
        ch.olaAccum[pos] += ch.fftBuf[n] * window[n] / olaNorm[n];
    }
    ch.olaWritePos += hopSize;

    // ── 7. Advance history ring write index ───────────────────────────────────
    ch.histWriteIdx = (ch.histWriteIdx + 1) % tHistFrames;
}

// ── lrsvCoeffFromRt60 ─────────────────────────────────────────────────────────

float SpectralProcessor::lrsvCoeffFromRt60(const float rt60Seconds) noexcept {
    // Derivation:
    //   δ = ln(1000) / RT60 = 6.908 / RT60          [decay rate, s⁻¹]
    //   coefficient = exp(−2δ · T_boundary)
    //               = exp(−2 × 6.908 / RT60 × 0.05)
    //               = exp(−0.6908 / RT60)
    //
    // Representative values:
    //   RT60 = 0.3 s  →  coeff ≈ 0.10   (dry room)
    //   RT60 = 0.5 s  →  coeff ≈ 0.25   (typical small room)
    //   RT60 = 1.0 s  →  coeff ≈ 0.50   (medium room)
    //   RT60 = 2.0 s  →  coeff ≈ 0.71   (large reverberant space)
    const float rt60 = std::max(0.05f, rt60Seconds);
    return std::exp(-0.6908f / rt60);
}

} // namespace vxsuite::deverb
