#include "VxRebalanceModelRunner.h"

#include "../../../framework/VxSuiteSpectralHelpers.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace vxsuite::rebalance::ml {

namespace {

constexpr double kModelSampleRate = 44100.0;
constexpr float kEps = 1.0e-8f;
constexpr int kSpleeterMinWarmupFrames = 8;

inline float clamp01(const float value) noexcept {
    return juce::jlimit(0.0f, 1.0f, value);
}

inline void sharpenAndNormalize(float* values, const int count, const float exponent) noexcept {
    float total = 0.0f;
    for (int i = 0; i < count; ++i) {
        values[i] = std::pow(clamp01(values[i]), exponent);
        total += values[i];
    }
    total = std::max(kEps, total);
    for (int i = 0; i < count; ++i)
        values[i] /= total;
}

} // namespace

ModelRunner::ModelRunner()
    : juce::Thread("VXRebalanceML") {
    startThread();
}

ModelRunner::~ModelRunner() {
    signalThreadShouldExit();
    workerWakeEvent.signal();
    stopThread(4000);
    demucsModel.waitForPendingAsync();
    spleeterModel.waitForPendingAsync();
    onnxModel.waitForPendingAsync();
}

void ModelRunner::SampleFifo::reset(const int capacity) {
    buffer.assign(static_cast<size_t>(std::max(1, capacity)), 0.0f);
    clear();
}

void ModelRunner::SampleFifo::clear() {
    writePos = 0;
    available = 0;
}

void ModelRunner::SampleFifo::push(const float* data, const int count) {
    if (buffer.empty() || data == nullptr || count <= 0)
        return;

    const int capacity = static_cast<int>(buffer.size());
    for (int i = 0; i < count; ++i) {
        buffer[static_cast<size_t>(writePos)] = data[i];
        writePos = (writePos + 1) % capacity;
        available = std::min(available + 1, capacity);
    }
}

void ModelRunner::prepare(const double sampleRate, const int maxBlockSize,
                          const juce::File& demucsFile,
                          const juce::File& spleeterFile, const juce::File& umx4File,
                          const ModelPreference preference) {
    const juce::ScopedLock lock(stateLock);
    sampleRateHz = sampleRate > 1000.0 ? sampleRate : 48000.0;
    maxBlockSizePrepared = std::max(1, maxBlockSize);
    featureBuffer.prepare(sampleRateHz, maxBlockSizePrepared);
    confidenceTracker.prepare(sampleRateHz);

    const auto maxRatio = std::max(1.0, kModelSampleRate / sampleRateHz);
    const auto maxResampledSize = std::max(kMaxModelHopSize,
        static_cast<int>(std::ceil(maxBlockSizePrepared * maxRatio)) + 128);
    const int stftFifoCap = kMaxModelFftSize + maxResampledSize + kMaxModelHopSize;
    const int demucsFifoCap = OnnxDemucsModel::kChunkSamples + maxResampledSize + 1024;
    for (auto& channel : channels) {
        channel.inputFifo.reset(std::max(stftFifoCap, demucsFifoCap));
        channel.resampler = std::make_unique<Resampler<1, 1>>(sampleRateHz, kModelSampleRate);
        channel.sourceScratch.assign(static_cast<size_t>(maxBlockSizePrepared), 0.0f);
        channel.resampleIn.assign(static_cast<size_t>(maxResampledSize), 0.0f);
        channel.resampleOut.assign(static_cast<size_t>(maxResampledSize), 0.0f);
    }
    for (auto& buffer : fftBuffers)
        buffer.assign(static_cast<size_t>(kMaxModelFftSize * 2), 0.0f);
    modelInputMagnitudes.assign(static_cast<size_t>(2 * kMaxModelBins * kMaxModelFrames), 0.0f);
    frameHistory.assign(static_cast<size_t>(2 * kMaxModelBins * kMaxModelFrames), 0.0f);
    umxTemporalMagnitudes.assign(static_cast<size_t>(OnnxUmx4Model::kHeadCount * OnnxUmx4Model::kModelBins), 0.0f);
    frameWriteIndex = 0;
    frameCount = 0;
    demucsChunkL.assign(static_cast<size_t>(OnnxDemucsModel::kChunkSamples), 0.0f);
    demucsChunkR.assign(static_cast<size_t>(OnnxDemucsModel::kChunkSamples), 0.0f);
    demucsAnalysisFft.prepare(Dsp::kFftOrder);
    vxsuite::spectral::prepareSqrtHannWindow(demucsAnalysisWindow, Dsp::kFftSize);
    demucsWorkBuf.assign(static_cast<size_t>(Dsp::kFftSize * 2), 0.0f);
    for (auto& block : pendingBlockQueue) {
        block.audio.setSize(2, maxBlockSizePrepared, false, false, true);
        block.audio.clear();
        block.numSamples = 0;
        block.numChannels = 0;
        block.recordingType = Dsp::RecordingType::studio;
        block.signalQuality = {};
    }

    clearPendingBlocks();
    resetAnalysisState();
    loadModelFiles(demucsFile, spleeterFile, umx4File, preference);
    workerPrepared.store(true, std::memory_order_release);
    workerWakeEvent.signal();
}

void ModelRunner::reset() {
    const juce::ScopedLock lock(stateLock);
    clearPendingBlocks();
    resetAnalysisState();
}

void ModelRunner::reloadModels(const juce::File& demucsFile,
                               const juce::File& spleeterFile, const juce::File& umx4File,
                               const ModelPreference preference) {
    const juce::ScopedLock lock(stateLock);
    loadModelFiles(demucsFile, spleeterFile, umx4File, preference);
    workerWakeEvent.signal();
}

void ModelRunner::analyseBlock(const juce::AudioBuffer<float>& buffer,
                               const Dsp::RecordingType recordingType,
                               const vxsuite::SignalQualitySnapshot& signalQuality) {
    const int numSamples = std::min(buffer.getNumSamples(), maxBlockSizePrepared);
    const int numChannels = std::min(2, buffer.getNumChannels());
    if (!workerPrepared.load(std::memory_order_acquire) || numSamples <= 0 || numChannels <= 0)
        return;

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    pendingBlocks.prepareToWrite(1, start1, size1, start2, size2);
    if (size1 + size2 <= 0)
        return;

    const int slotIndex = size1 > 0 ? start1 : start2;
    auto& block = pendingBlockQueue[static_cast<size_t>(slotIndex)];
    block.numSamples = numSamples;
    block.numChannels = numChannels;
    block.recordingType = recordingType;
    block.signalQuality = signalQuality;

    for (int ch = 0; ch < 2; ++ch) {
        const int sourceChannel = numChannels == 1 ? 0 : std::min(ch, numChannels - 1);
        block.audio.copyFrom(ch, 0, buffer, sourceChannel, 0, numSamples);
        if (numSamples < block.audio.getNumSamples())
            block.audio.clear(ch, numSamples, block.audio.getNumSamples() - numSamples);
    }

    pendingBlocks.finishedWrite(1);
    workerWakeEvent.signal();
}

Dsp::MlMaskSnapshot ModelRunner::latestMaskSnapshot() const noexcept {
    for (int attempt = 0; attempt < 3; ++attempt) {
        const uint32_t before = snapshotSequence.load(std::memory_order_acquire);
        if ((before & 1u) != 0u)
            continue;

        const auto snapshot = latestSnapshot;
        const uint32_t after = snapshotSequence.load(std::memory_order_acquire);
        if (before == after && (after & 1u) == 0u) {
            auto stable = snapshot;
            if (!stable.available)
                stable.derivedGuitarFromOther = true;
            return stable;
        }
    }

    Dsp::MlMaskSnapshot fallback;
    fallback.derivedGuitarFromOther = true;
    return fallback;
}

void ModelRunner::run() {
    while (!threadShouldExit()) {
        workerWakeEvent.wait(50);
        if (threadShouldExit())
            break;

        while (!threadShouldExit()) {
            if (!workerPrepared.load(std::memory_order_acquire))
                break;

            int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
            pendingBlocks.prepareToRead(1, start1, size1, start2, size2);
            if (size1 + size2 <= 0)
                break;

            while (pendingBlocks.getNumReady() > 1) {
                pendingBlocks.finishedRead(1);
                pendingBlocks.prepareToRead(1, start1, size1, start2, size2);
                if (size1 + size2 <= 0)
                    break;
            }

            if (size1 + size2 <= 0)
                break;

            const int slotIndex = size1 > 0 ? start1 : start2;
            {
                const juce::ScopedLock lock(stateLock);
                if (workerPrepared.load(std::memory_order_relaxed))
                    processPendingBlock(pendingBlockQueue[static_cast<size_t>(slotIndex)]);
            }
            pendingBlocks.finishedRead(1);
        }
    }
}

void ModelRunner::clearPendingBlocks() noexcept {
    pendingBlocks.reset();
    for (auto& block : pendingBlockQueue)
        block.numSamples = 0;
}

void ModelRunner::primeCenteredPadding(SampleFifo& fifo) const {
    if (currentModelFftSize <= 1)
        return;
    std::vector<float> zeros(static_cast<size_t>(currentModelFftSize / 2), 0.0f);
    fifo.push(zeros.data(), static_cast<int>(zeros.size()));
}

void ModelRunner::resetAnalysisState() {
    demucsModel.waitForPendingAsync();
    spleeterModel.waitForPendingAsync();
    onnxModel.waitForPendingAsync();
    inferenceInFlight.store(false, std::memory_order_relaxed);

    featureBuffer.reset();
    confidenceTracker.reset();
    for (auto& channel : channels) {
        channel.inputFifo.clear();
        channel.resampler = std::make_unique<Resampler<1, 1>>(sampleRateHz, kModelSampleRate);
        std::fill(channel.sourceScratch.begin(), channel.sourceScratch.end(), 0.0f);
        std::fill(channel.resampleIn.begin(), channel.resampleIn.end(), 0.0f);
        std::fill(channel.resampleOut.begin(), channel.resampleOut.end(), 0.0f);
        primeCenteredPadding(channel.inputFifo);
    }
    for (auto& buffer : fftBuffers)
        std::fill(buffer.begin(), buffer.end(), 0.0f);
    std::fill(modelInputMagnitudes.begin(), modelInputMagnitudes.end(), 0.0f);
    std::fill(frameHistory.begin(), frameHistory.end(), 0.0f);
    std::fill(umxTemporalMagnitudes.begin(), umxTemporalMagnitudes.end(), 0.0f);
    frameWriteIndex = 0;
    frameCount = 0;
    umxTemporalMagnitudesPrimed = false;
    snapshotSequence.fetch_add(1, std::memory_order_acq_rel);
    latestSnapshot = {};
    snapshotSequence.fetch_add(1, std::memory_order_release);
    latestBlendConfidence.store(0.0f, std::memory_order_relaxed);
}

void ModelRunner::loadModelFiles(const juce::File& demucsFile,
                                 const juce::File& spleeterFile, const juce::File& umx4File,
                                 const ModelPreference preference) {
    lastError.clear();

    const bool wantDemucs   = (preference == ModelPreference::any || preference == ModelPreference::demucs6);
    const bool wantSpleeter = (preference == ModelPreference::spleeter);
    const bool wantUmx4     = (preference == ModelPreference::umx4);

    // Try Demucs first (explicit guitar head).
    bool demucsReady = false;
    if (wantDemucs && demucsFile.existsAsFile())
        demucsReady = demucsModel.prepare(demucsFile.getFullPathName().toStdString(), lastError);
    else
        demucsModel.reset();

    bool spleeterReady = false;
    if (!demucsReady && wantSpleeter && spleeterFile.existsAsFile())
        spleeterReady = spleeterModel.prepare(spleeterFile.getFullPathName().toStdString(), lastError);
    else
        spleeterModel.reset();

    bool umx4Ready = false;
    if (!demucsReady && !spleeterReady && (wantUmx4 || wantDemucs) && umx4File.existsAsFile())
        umx4Ready = onnxModel.prepare(umx4File.getFullPathName().toStdString(), lastError);
    else
        onnxModel.reset();

    const bool anyFound = demucsFile.existsAsFile()
                        || (wantSpleeter && spleeterFile.existsAsFile())
                        || umx4File.existsAsFile();
    modelDiscovered.store(anyFound, std::memory_order_relaxed);

    ActiveModel selected = ActiveModel::none;
    if (demucsReady) {
        selected = ActiveModel::demucs6;
        // Demucs uses the DSP-resolution analysis FFT (set up in prepare()).
        // The STFT frame accumulation path is still active but unused for Demucs.
        currentModelFftOrder = Dsp::kFftOrder;
        currentModelFftSize  = Dsp::kFftSize;
        currentModelHopSize  = Dsp::kHopSize;
        currentModelFrames   = OnnxUmx4Model::kModelFrames;
        currentModelBins     = Dsp::kBins;
        currentModelHasNyquist = true;
    } else if (spleeterReady) {
        selected = ActiveModel::spleeter;
        currentModelFftOrder = 11;
        currentModelFftSize = 1 << currentModelFftOrder;
        currentModelHopSize = currentModelFftSize / 4;
        currentModelFrames = OnnxSpleeterModel::kModelFrames;
        currentModelBins   = OnnxSpleeterModel::kModelBins;
        currentModelHasNyquist = false;
    } else if (umx4Ready) {
        selected = ActiveModel::umx4;
        currentModelFftOrder = 12;
        currentModelFftSize = 1 << currentModelFftOrder;
        currentModelHopSize = currentModelFftSize / 4;
        currentModelFrames = OnnxUmx4Model::kModelFrames;
        currentModelBins   = OnnxUmx4Model::kModelBins;
        currentModelHasNyquist = true;
    } else {
        currentModelFftOrder = 12;
        currentModelFftSize = 1 << currentModelFftOrder;
        currentModelHopSize = currentModelFftSize / 4;
        currentModelFrames = OnnxUmx4Model::kModelFrames;
        currentModelBins   = OnnxUmx4Model::kModelBins;
        currentModelHasNyquist = true;
    }
    activeModel.store(static_cast<int>(selected), std::memory_order_relaxed);
    fft.prepare(currentModelFftOrder);
    vxsuite::spectral::prepareSqrtHannWindow(window, currentModelFftSize);
    for (auto& channel : channels) {
        channel.inputFifo.clear();
        primeCenteredPadding(channel.inputFifo);
    }

    const bool anyReady = spleeterReady || umx4Ready;
    status.store(static_cast<int>(anyReady || anyFound ? Status::modelDetectedRuntimePending
                                                        : Status::heuristicFallback),
                 std::memory_order_relaxed);
}

void ModelRunner::processPendingBlock(const PendingBlock& block) {
    const auto features = featureBuffer.analyseBlock(block.audio, block.numSamples);
    const float blendConfidence = confidenceTracker.update(features, block.recordingType, block.signalQuality);
    latestBlendConfidence.store(blendConfidence, std::memory_order_relaxed);

    for (int ch = 0; ch < 2; ++ch) {
        auto& channel = channels[static_cast<size_t>(ch)];
        if (channel.resampler == nullptr)
            continue;

        float* sourceInput[] = { const_cast<float*>(block.audio.getReadPointer(ch)) };
        float* sourceOutput[] = { channel.sourceScratch.data() };
        float* targetInput[] = { channel.resampleIn.data() };
        float* targetOutput[] = { channel.resampleOut.data() };

        channel.resampler->process(
            sourceInput,
            sourceOutput,
            targetInput,
            targetOutput,
            block.numSamples,
            [&](float* const* inputBuffers, float* const* outputBuffers, int sampleCount44k) {
                juce::ignoreUnused(outputBuffers);
                channel.inputFifo.push(inputBuffers[0], sampleCount44k);
            });
    }

    const auto am = static_cast<ActiveModel>(activeModel.load(std::memory_order_relaxed));
    if (am == ActiveModel::demucs6) {
        while (channels[0].inputFifo.available >= OnnxDemucsModel::kChunkSamples
            && channels[1].inputFifo.available >= OnnxDemucsModel::kChunkSamples)
            processDemucsChunk(block.signalQuality);
    } else if (am != ActiveModel::none) {
        while (channels[0].inputFifo.available >= currentModelFftSize
            && channels[1].inputFifo.available >= currentModelFftSize)
            processModelFrame(block.signalQuality);
    }
}

void ModelRunner::processDemucsChunk(const vxsuite::SignalQualitySnapshot& signalQuality) {
    static constexpr int kChunk = OnnxDemucsModel::kChunkSamples;

    // Extract kChunk samples from each channel FIFO (oldest first).
    for (int ch = 0; ch < 2; ++ch) {
        auto& fifo = channels[static_cast<size_t>(ch)].inputFifo;
        auto& chunkBuf = (ch == 0) ? demucsChunkL : demucsChunkR;
        const int fifoSize = static_cast<int>(fifo.buffer.size());
        const int startPos = (fifo.writePos - fifo.available + fifoSize) % fifoSize;
        for (int i = 0; i < kChunk; ++i)
            chunkBuf[static_cast<size_t>(i)] = fifo.buffer[static_cast<size_t>((startPos + i) % fifoSize)];
        fifo.available -= kChunk;
    }

    if (inferenceInFlight.load(std::memory_order_acquire))
        return;

    const float blendConf = latestBlendConfidence.load(std::memory_order_relaxed);
    const float sepConf   = signalQuality.separationConfidence;
    inferenceInFlight.store(true, std::memory_order_release);

    auto buildAndPublishDemucs = [this, blendConf, sepConf](const float* outputData, int /*totalElements*/) {
        // outputData layout: [kHeadCount][2][kChunkSamples]
        // Demucs stem order: 0=drums  1=bass  2=other  3=vocals  4=guitar  5=piano
        static constexpr int kChunkSz  = OnnxDemucsModel::kChunkSamples;
        static constexpr int kStems    = OnnxDemucsModel::kHeadCount;
        static constexpr int kNumWins  = 5;
        static constexpr int kWinSize  = Dsp::kFftSize;   // 1024
        static constexpr int kWinHop   = kWinSize / 4;

        // Centre-windowed analysis: start at middle of chunk, step backward/forward.
        const int centre = kChunkSz / 2 - kWinSize / 2;

        // Accumulate per-stem magnitude spectra over kNumWins overlapping windows.
        std::array<std::array<float, Dsp::kBins>, kStems> stemMags {};
        for (auto& m : stemMags) m.fill(0.0f);

        for (int stem = 0; stem < kStems; ++stem) {
            const float* leftPtr  = outputData + (stem * 2 + 0) * kChunkSz;
            const float* rightPtr = outputData + (stem * 2 + 1) * kChunkSz;
            for (int win = 0; win < kNumWins; ++win) {
                const int offset = centre + win * kWinHop - (kNumWins / 2) * kWinHop;
                std::fill(demucsWorkBuf.begin(), demucsWorkBuf.end(), 0.0f);
                for (int i = 0; i < kWinSize; ++i) {
                    const int srcIdx = offset + i;
                    if (srcIdx >= 0 && srcIdx < kChunkSz) {
                        const float sample = 0.5f * (leftPtr[srcIdx] + rightPtr[srcIdx]);
                        demucsWorkBuf[static_cast<size_t>(i)] = sample * demucsAnalysisWindow[static_cast<size_t>(i)];
                    }
                }
                demucsAnalysisFft.performForward(demucsWorkBuf.data());
                for (int k = 0; k < Dsp::kBins; ++k) {
                    const float re = demucsWorkBuf[static_cast<size_t>(2 * k)];
                    const float im = (k == 0 || k == Dsp::kBins - 1) ? 0.0f
                                     : demucsWorkBuf[static_cast<size_t>(2 * k + 1)];
                    stemMags[stem][static_cast<size_t>(k)] += std::sqrt(re * re + im * im + kEps);
                }
            }
            for (auto& v : stemMags[stem])
                v /= static_cast<float>(kNumWins);
        }

        Dsp::MlMaskSnapshot snap;
        snap.derivedGuitarFromOther = false;
        snap.directStemMix = true;

        for (int k = 0; k < Dsp::kBins; ++k) {
            float sources[Dsp::kSourceCount];
            sources[Dsp::vocalsSource] = stemMags[3][static_cast<size_t>(k)];  // vocals
            sources[Dsp::drumsSource]  = stemMags[0][static_cast<size_t>(k)];  // drums
            sources[Dsp::bassSource]   = stemMags[1][static_cast<size_t>(k)];  // bass
            sources[Dsp::guitarSource] = stemMags[4][static_cast<size_t>(k)];  // guitar
            // other + piano merged into residual lane
            sources[Dsp::otherSource]  = stemMags[2][static_cast<size_t>(k)] + stemMags[5][static_cast<size_t>(k)];

            sharpenAndNormalize(sources, Dsp::kSourceCount, 2.0f);

            snap.masks[Dsp::vocalsSource][static_cast<size_t>(k)] = sources[Dsp::vocalsSource];
            snap.masks[Dsp::drumsSource] [static_cast<size_t>(k)] = sources[Dsp::drumsSource];
            snap.masks[Dsp::bassSource]  [static_cast<size_t>(k)] = sources[Dsp::bassSource];
            snap.masks[Dsp::guitarSource][static_cast<size_t>(k)] = sources[Dsp::guitarSource];
            snap.masks[Dsp::otherSource] [static_cast<size_t>(k)] = sources[Dsp::otherSource];
        }
        snap.available  = true;
        snap.confidence = clamp01(blendConf * sepConf);
        snap.revision = latestSnapshot.revision + 1;
        snapshotSequence.fetch_add(1, std::memory_order_acq_rel);
        latestSnapshot = snap;
        snapshotSequence.fetch_add(1, std::memory_order_release);
        status.store(static_cast<int>(Status::mlMasksActive), std::memory_order_relaxed);
        inferenceInFlight.store(false, std::memory_order_release);
    };

    std::string error;
    const bool dispatched = demucsModel.run(
        demucsChunkL.data(), demucsChunkR.data(),
        buildAndPublishDemucs, error);

    if (!dispatched) {
        status.store(static_cast<int>(Status::modelDetectedRuntimePending), std::memory_order_relaxed);
        lastError = error;
        inferenceInFlight.store(false, std::memory_order_release);
    }
}

void ModelRunner::processModelFrame(const vxsuite::SignalQualitySnapshot& signalQuality) {
    for (int ch = 0; ch < 2; ++ch) {
        auto& fifo = channels[static_cast<size_t>(ch)].inputFifo;
        auto& fftBuffer = fftBuffers[static_cast<size_t>(ch)];
        std::fill(fftBuffer.begin(), fftBuffer.end(), 0.0f);
        const int fifoSize = static_cast<int>(fifo.buffer.size());
        const int frameStart = (fifo.writePos - fifo.available + fifoSize) % fifoSize;
        for (int i = 0; i < currentModelFftSize; ++i) {
            const int srcIndex = (frameStart + i) % fifoSize;
            fftBuffer[static_cast<size_t>(i)] = fifo.buffer[static_cast<size_t>(srcIndex)] * window[static_cast<size_t>(i)];
        }
        fft.performForward(fftBuffer.data());

        for (int k = 0; k < currentModelBins; ++k) {
            const float re = fftBuffer[static_cast<size_t>(2 * k)];
            const bool isNyquistBin = currentModelHasNyquist && (k == currentModelBins - 1);
            const float im = (k == 0 || isNyquistBin) ? 0.0f : fftBuffer[static_cast<size_t>(2 * k + 1)];
            const float mag = std::sqrt(std::max(kEps, re * re + im * im));
            const size_t index = static_cast<size_t>(((ch * currentModelBins) + k) * currentModelFrames + frameWriteIndex);
            frameHistory[index] = mag;
        }

        fifo.available -= currentModelHopSize;
    }

    frameWriteIndex = (frameWriteIndex + 1) % currentModelFrames;
    frameCount = std::min(frameCount + 1, currentModelFrames);

    const auto am = static_cast<ActiveModel>(activeModel.load(std::memory_order_relaxed));
    if (am == ActiveModel::none)
        return;

    const int minWarmupFrames = am == ActiveModel::spleeter
        ? std::min(currentModelFrames, kSpleeterMinWarmupFrames)
        : currentModelFrames;
    if (frameCount < minWarmupFrames)
        return;

    for (int ch = 0; ch < 2; ++ch) {
        const int padFrames = currentModelFrames - frameCount;
        const int historyStart = frameCount < currentModelFrames ? 0 : frameWriteIndex;
        for (int k = 0; k < currentModelBins; ++k) {
            for (int frame = 0; frame < currentModelFrames; ++frame) {
                const size_t inputIndex = static_cast<size_t>(((ch * currentModelBins) + k) * currentModelFrames + frame);
                if (frame < padFrames) {
                    modelInputMagnitudes[inputIndex] = 0.0f;
                    continue;
                }

                const int historyFrame = frame - padFrames;
                const int sourceFrame = (historyStart + historyFrame) % currentModelFrames;
                const size_t historyIndex = static_cast<size_t>(((ch * currentModelBins) + k) * currentModelFrames + sourceFrame);
                modelInputMagnitudes[inputIndex] = frameHistory[historyIndex];
            }
        }
    }

    // Skip if a previous inference is still running — use the last published masks.
    if (inferenceInFlight.load(std::memory_order_acquire))
        return;

    // Capture values needed while publishing the latest completed inference.
    const float blendConf  = latestBlendConfidence.load(std::memory_order_relaxed);
    const float sepConf    = signalQuality.separationConfidence;
    const double sr        = sampleRateHz;
    const int dispatchBins = currentModelBins;
    const int dispatchFrames = currentModelFrames;
    const float contextCoverage = juce::jlimit(0.0f, 1.0f,
        static_cast<float>(frameCount) / static_cast<float>(currentModelFrames));

    inferenceInFlight.store(true, std::memory_order_release);

    // Shared mask-builder used by both model outputs.
    // headCount: number of output stems.
    // derivedGuitar: guitar mask is derived in the DSP layer instead of coming from
    //                a dedicated model head.
    // mergeTailToOther: when true, the last two heads are merged into the residual
    //                   lane before DSP derives guitar from that combined residual.
    auto buildAndPublish = [this, blendConf, sepConf, sr, dispatchBins, dispatchFrames, contextCoverage](
            const float* outputData, int headCount, bool derivedGuitar, bool mergeTailToOther,
            float maskPower, float sharpenExponent, bool temporalAverage) {
        Dsp::MlMaskSnapshot snap;
        snap.derivedGuitarFromOther = derivedGuitar;
        snap.directStemMix = temporalAverage;

        if (temporalAverage && headCount == OnnxUmx4Model::kHeadCount && dispatchBins == OnnxUmx4Model::kModelBins) {
            for (int head = 0; head < headCount; ++head) {
                for (int modelBin = 0; modelBin < dispatchBins; ++modelBin) {
                    float weightedSum = 0.0f;
                    float weightTotal = 0.0f;
                    for (int frame = 0; frame < dispatchFrames; ++frame) {
                        const float weight = 0.15f + 0.85f * static_cast<float>(frame + 1)
                            / static_cast<float>(dispatchFrames);
                        const size_t li = static_cast<size_t>((((head * 2) + 0) * dispatchBins + modelBin) * dispatchFrames + frame);
                        const size_t ri = static_cast<size_t>((((head * 2) + 1) * dispatchBins + modelBin) * dispatchFrames + frame);
                        const float magnitude = 0.5f * (outputData[li] + outputData[ri]);
                        weightedSum += weight * std::pow(std::max(kEps, magnitude), maskPower);
                        weightTotal += weight;
                    }

                    const float averagedMagnitude = weightedSum / std::max(kEps, weightTotal);
                    const size_t temporalIndex = static_cast<size_t>(head * dispatchBins + modelBin);
                    if (!umxTemporalMagnitudesPrimed)
                        umxTemporalMagnitudes[temporalIndex] = averagedMagnitude;
                    else
                        umxTemporalMagnitudes[temporalIndex] = std::lerp(umxTemporalMagnitudes[temporalIndex],
                                                                         averagedMagnitude,
                                                                         0.18f);
                }
            }
            umxTemporalMagnitudesPrimed = true;
        }

        for (int k = 0; k < Dsp::kBins; ++k) {
            const float hz = static_cast<float>(k) * static_cast<float>(sr) / static_cast<float>(Dsp::kFftSize);
            const int modelBin = juce::jlimit(0, dispatchBins - 1,
                juce::roundToInt(hz * static_cast<float>(currentModelFftSize) / static_cast<float>(kModelSampleRate)));

            float shares[OnnxSpleeterModel::kHeadCount] {}; // sized for max (5)
            float total = 0.0f;
            for (int head = 0; head < headCount; ++head) {
                if (temporalAverage && headCount == OnnxUmx4Model::kHeadCount && dispatchBins == OnnxUmx4Model::kModelBins) {
                    const size_t temporalIndex = static_cast<size_t>(head * dispatchBins + modelBin);
                    shares[head] = umxTemporalMagnitudes[temporalIndex];
                } else {
                    const int latestFrame = dispatchFrames - 1;
                    const size_t li = static_cast<size_t>((((head * 2) + 0) * dispatchBins + modelBin) * dispatchFrames + latestFrame);
                    const size_t ri = static_cast<size_t>((((head * 2) + 1) * dispatchBins + modelBin) * dispatchFrames + latestFrame);
                    const float magnitude = 0.5f * (outputData[li] + outputData[ri]);
                    shares[head] = std::pow(std::max(kEps, magnitude), maskPower);
                }
                total += shares[head];
            }
            total = std::max(kEps, total);

            float normalized[Dsp::kSourceCount] {};
            normalized[Dsp::vocalsSource] = shares[0] / total;
            normalized[Dsp::drumsSource]  = shares[1] / total;
            normalized[Dsp::bassSource]   = shares[2] / total;
            normalized[Dsp::guitarSource] = derivedGuitar ? 0.0f : shares[3] / total;
            const float residualShare = mergeTailToOther
                ? (shares[3] + shares[4]) / total
                : shares[derivedGuitar ? 3 : 4] / total;
            normalized[Dsp::otherSource] = clamp01(residualShare);

            if (temporalAverage && !mergeTailToOther) {
                if (hz < 140.0f)
                    normalized[Dsp::vocalsSource] *= 0.16f;
                if (hz > 9000.0f)
                    normalized[Dsp::vocalsSource] *= 0.32f;
                if (hz >= 220.0f && hz <= 4200.0f) {
                    normalized[Dsp::vocalsSource] *= 1.22f;
                    normalized[Dsp::drumsSource] *= 0.90f;
                    normalized[Dsp::otherSource] *= 0.74f;
                }
                if (hz >= 500.0f && hz <= 2600.0f)
                    normalized[Dsp::otherSource] *= 0.82f;
                if (hz < 110.0f)
                    normalized[Dsp::otherSource] *= 0.42f;
            }

            sharpenAndNormalize(normalized, Dsp::kSourceCount, sharpenExponent);

            snap.masks[Dsp::vocalsSource][static_cast<size_t>(k)] = normalized[Dsp::vocalsSource];
            snap.masks[Dsp::drumsSource] [static_cast<size_t>(k)] = normalized[Dsp::drumsSource];
            snap.masks[Dsp::bassSource]  [static_cast<size_t>(k)] = normalized[Dsp::bassSource];
            snap.masks[Dsp::guitarSource][static_cast<size_t>(k)] = normalized[Dsp::guitarSource];
            snap.masks[Dsp::otherSource] [static_cast<size_t>(k)] = normalized[Dsp::otherSource];
        }
        snap.available  = true;
        snap.confidence = clamp01(blendConf * sepConf * std::sqrt(std::max(0.0f, contextCoverage)));
        snap.revision = latestSnapshot.revision + 1;
        snapshotSequence.fetch_add(1, std::memory_order_acq_rel);
        latestSnapshot = snap;
        snapshotSequence.fetch_add(1, std::memory_order_release);
        status.store(static_cast<int>(Status::mlMasksActive), std::memory_order_relaxed);
        inferenceInFlight.store(false, std::memory_order_release);
    };

    std::string error;
    bool dispatched = false;

    if (am == ActiveModel::spleeter) {
        dispatched = spleeterModel.run(
            modelInputMagnitudes.data(), dispatchFrames,
            [buildAndPublish](const float* data, int /*size*/) {
                buildAndPublish(data, OnnxSpleeterModel::kHeadCount, true, true, 1.0f, 1.90f, true);
            },
            error);
    } else {
        dispatched = onnxModel.run(
            modelInputMagnitudes.data(), dispatchFrames,
            [buildAndPublish](const float* data, int /*size*/) {
                buildAndPublish(data, OnnxUmx4Model::kHeadCount, true, false, 1.0f, 2.0f, true);
            },
            error);
    }

    if (!dispatched) {
        status.store(static_cast<int>(Status::modelDetectedRuntimePending), std::memory_order_relaxed);
        lastError = error;
        inferenceInFlight.store(false, std::memory_order_release);
    }
}

juce::String ModelRunner::statusText() const {
    const auto current = static_cast<Status>(status.load(std::memory_order_relaxed));
    const auto am = static_cast<ActiveModel>(activeModel.load(std::memory_order_relaxed));
    switch (current) {
        case Status::mlMasksActive:
            if (am == ActiveModel::demucs6)
                return "htdemucs_6s direct stem remix active (dedicated guitar)";
            if (am == ActiveModel::spleeter)
                return "Spleeter 5-stem ML masks active";
            return "V2.0 UMX4 direct stem remix active (guitar derived)";
        case Status::modelDetectedRuntimePending:
            if (am == ActiveModel::none) {
                if (!lastError.empty())
                    return "ML model load failed - using DSP heuristic split (" + juce::String(lastError) + ")";
                return "ML model unavailable - using DSP heuristic split";
            }
            if (modelDiscovered.load(std::memory_order_relaxed)) {
                juce::String modelName = (am == ActiveModel::demucs6) ? "htdemucs_6s"
                                       : (am == ActiveModel::spleeter) ? "Spleeter" : "V2.0 UMX4";
                auto text = modelName + " model ready - warming up";
                if (!lastError.empty())
                    text << " (" << juce::String(lastError) << ")";
                return text;
            }
            return "ML mode selected - using DSP heuristic split";
        case Status::heuristicFallback:
            break;
    }
    return "ML mode selected - using DSP heuristic split";
}


} // namespace vxsuite::rebalance::ml
