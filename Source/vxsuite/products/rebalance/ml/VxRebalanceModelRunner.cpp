#include "VxRebalanceModelRunner.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace vxsuite::rebalance::ml {

namespace {

constexpr double kModelSampleRate = 44100.0;
constexpr int kModelFftOrder = 12;
constexpr int kModelFftSize = 1 << kModelFftOrder;
constexpr int kModelHopSize = kModelFftSize / 4;
constexpr int kModelBins = kModelFftSize / 2 + 1;
constexpr int kModelFrames = OnnxUmx4Model::kModelFrames;
constexpr float kEps = 1.0e-8f;

inline float clamp01(const float value) noexcept {
    return juce::jlimit(0.0f, 1.0f, value);
}

} // namespace

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

void ModelRunner::prepare(const double sampleRate, const int maxBlockSize, const juce::File& onnxFile) {
    sampleRateHz = sampleRate > 1000.0 ? sampleRate : 48000.0;
    maxBlockSizePrepared = std::max(1, maxBlockSize);
    featureBuffer.prepare(sampleRateHz, maxBlockSizePrepared);
    confidenceTracker.prepare(sampleRateHz);

    fft.prepare(kModelFftOrder);
    vxsuite::spectral::prepareSqrtHannWindow(window, kModelFftSize);
    const auto maxRatio = std::max(1.0, kModelSampleRate / sampleRateHz);
    const auto maxResampledSize = std::max(kModelHopSize,
        static_cast<int>(std::ceil(maxBlockSizePrepared * maxRatio)) + 128);
    for (auto& channel : channels) {
        channel.inputFifo.reset(kModelFftSize + maxResampledSize + kModelHopSize);
        channel.resampler = std::make_unique<Resampler<1, 1>>(sampleRateHz, kModelSampleRate);
        channel.sourceScratch.assign(static_cast<size_t>(maxBlockSizePrepared), 0.0f);
        channel.resampleIn.assign(static_cast<size_t>(maxResampledSize), 0.0f);
        channel.resampleOut.assign(static_cast<size_t>(maxResampledSize), 0.0f);
    }
    for (auto& buffer : fftBuffers)
        buffer.assign(static_cast<size_t>(kModelFftSize * 2), 0.0f);
    modelInputMagnitudes.assign(static_cast<size_t>(2 * kModelBins * kModelFrames), 0.0f);
    frameHistory.assign(static_cast<size_t>(2 * kModelBins * kModelFrames), 0.0f);
    frameWriteIndex = 0;
    frameCount = 0;

    const bool foundModel = onnxFile.existsAsFile();
    modelDiscovered.store(foundModel, std::memory_order_relaxed);

    bool modelReady = false;
    lastError.clear();
    if (foundModel)
        modelReady = onnxModel.prepare(onnxFile.getFullPathName().toStdString(), lastError);

    status.store(static_cast<int>(modelReady ? Status::mlMasksActive
                                             : (foundModel ? Status::umx4ModelDetectedRuntimePending
                                                           : Status::heuristicFallback)),
                 std::memory_order_relaxed);
    reset();
}

void ModelRunner::reset() {
    // Wait for any in-flight async inference before clearing shared state.
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
    }
    for (auto& buffer : fftBuffers)
        std::fill(buffer.begin(), buffer.end(), 0.0f);
    std::fill(modelInputMagnitudes.begin(), modelInputMagnitudes.end(), 0.0f);
    std::fill(frameHistory.begin(), frameHistory.end(), 0.0f);
    frameWriteIndex = 0;
    frameCount = 0;
    {
        juce::SpinLock::ScopedLockType lock(snapshotLock);
        latestSnapshot = {};
    }
    audioThreadSnapshot = {};
    latestBlendConfidence.store(0.0f, std::memory_order_relaxed);
}

void ModelRunner::analyseBlock(const juce::AudioBuffer<float>& buffer,
                               const Dsp::RecordingType recordingType,
                               const vxsuite::SignalQualitySnapshot& signalQuality) {
    const auto features = featureBuffer.analyseBlock(buffer);
    const float blendConfidence = confidenceTracker.update(features, recordingType, signalQuality);
    latestBlendConfidence.store(blendConfidence, std::memory_order_relaxed);

    const int numChannels = std::min(2, buffer.getNumChannels());
    if (numChannels <= 0)
        return;

    for (int ch = 0; ch < 2; ++ch) {
        const int sourceChannel = numChannels == 1 ? 0 : ch;
        auto& channel = channels[static_cast<size_t>(ch)];
        if (channel.resampler == nullptr)
            continue;

        float* sourceInput[] = { const_cast<float*>(buffer.getReadPointer(sourceChannel)) };
        float* sourceOutput[] = { channel.sourceScratch.data() };
        float* targetInput[] = { channel.resampleIn.data() };
        float* targetOutput[] = { channel.resampleOut.data() };

        channel.resampler->process(
            sourceInput,
            sourceOutput,
            targetInput,
            targetOutput,
            buffer.getNumSamples(),
            [&](float* const* inputBuffers, float* const* outputBuffers, int sampleCount44k) {
                juce::ignoreUnused(outputBuffers);
                channel.inputFifo.push(inputBuffers[0], sampleCount44k);
            });
    }

    if (status.load(std::memory_order_relaxed) == static_cast<int>(Status::mlMasksActive)) {
        while (channels[0].inputFifo.available >= kModelFftSize && channels[1].inputFifo.available >= kModelFftSize)
            processModelFrame(signalQuality);
    }

    // Copy the latest snapshot (written by ORT callback thread) to the audio-thread copy.
    {
        juce::SpinLock::ScopedLockType lock(snapshotLock);
        audioThreadSnapshot = latestSnapshot;
    }
    if (!audioThreadSnapshot.available)
        audioThreadSnapshot.derivedGuitarFromOther = true;
}

void ModelRunner::processModelFrame(const vxsuite::SignalQualitySnapshot& signalQuality) {
    for (int ch = 0; ch < 2; ++ch) {
        auto& fifo = channels[static_cast<size_t>(ch)].inputFifo;
        auto& fftBuffer = fftBuffers[static_cast<size_t>(ch)];
        std::fill(fftBuffer.begin(), fftBuffer.end(), 0.0f);
        const int fifoSize = static_cast<int>(fifo.buffer.size());
        const int frameStart = (fifo.writePos - fifo.available + fifoSize) % fifoSize;
        for (int i = 0; i < kModelFftSize; ++i) {
            const int srcIndex = (frameStart + i) % fifoSize;
            fftBuffer[static_cast<size_t>(i)] = fifo.buffer[static_cast<size_t>(srcIndex)] * window[static_cast<size_t>(i)];
        }
        fft.performForward(fftBuffer.data());

        for (int k = 0; k < kModelBins; ++k) {
            const float re = fftBuffer[static_cast<size_t>(2 * k)];
            const float im = (k == 0 || k == kModelBins - 1) ? 0.0f : fftBuffer[static_cast<size_t>(2 * k + 1)];
            const float mag = std::sqrt(std::max(kEps, re * re + im * im));
            const size_t index = static_cast<size_t>(((ch * kModelBins) + k) * kModelFrames + frameWriteIndex);
            frameHistory[index] = mag;
        }

        fifo.available -= kModelHopSize;
    }

    frameWriteIndex = (frameWriteIndex + 1) % kModelFrames;
    frameCount = std::min(frameCount + 1, kModelFrames);

    if (frameCount < kModelFrames)
        return;

    for (int ch = 0; ch < 2; ++ch) {
        for (int k = 0; k < kModelBins; ++k) {
            for (int frame = 0; frame < kModelFrames; ++frame) {
                const int sourceFrame = (frameWriteIndex + frame) % kModelFrames;
                const size_t historyIndex = static_cast<size_t>(((ch * kModelBins) + k) * kModelFrames + sourceFrame);
                const size_t inputIndex = static_cast<size_t>(((ch * kModelBins) + k) * kModelFrames + frame);
                modelInputMagnitudes[inputIndex] = frameHistory[historyIndex];
            }
        }
    }

    // Skip if a previous inference is still running — use the last published masks.
    if (inferenceInFlight.load(std::memory_order_acquire))
        return;

    // Capture values needed in the callback (fired on an ORT thread).
    const float blendConf = latestBlendConfidence.load(std::memory_order_relaxed);
    const float sepConf   = signalQuality.separationConfidence;
    const double sr       = sampleRateHz;

    inferenceInFlight.store(true, std::memory_order_release);

    std::string error;
    const bool dispatched = onnxModel.runAsync(
        modelInputMagnitudes.data(), kModelFrames,
        [this, blendConf, sepConf, sr](const float* outputData, int /*size*/) {
            // Runs on an ORT thread — extract masks then publish atomically.
            const int latestFrame = kModelFrames - 1;
            Dsp::MlMaskSnapshot snap;
            snap.derivedGuitarFromOther = true;

            for (int k = 0; k < Dsp::kBins; ++k) {
                const float hz = static_cast<float>(k) * static_cast<float>(sr) / static_cast<float>(Dsp::kFftSize);
                const int modelBin = juce::jlimit(0, kModelBins - 1,
                    juce::roundToInt(hz * static_cast<float>(kModelFftSize) / static_cast<float>(kModelSampleRate)));

                float shares[OnnxUmx4Model::kHeadCount] {};
                float total = 0.0f;
                for (int head = 0; head < OnnxUmx4Model::kHeadCount; ++head) {
                    const size_t li = static_cast<size_t>((((head * 2) + 0) * kModelBins + modelBin) * kModelFrames + latestFrame);
                    const size_t ri = static_cast<size_t>((((head * 2) + 1) * kModelBins + modelBin) * kModelFrames + latestFrame);
                    shares[head] = 0.5f * (outputData[li] + outputData[ri]);
                    total += shares[head];
                }
                total = std::max(kEps, total);

                snap.masks[Dsp::vocalsSource][static_cast<size_t>(k)] = shares[0] / total;
                snap.masks[Dsp::drumsSource] [static_cast<size_t>(k)] = shares[1] / total;
                snap.masks[Dsp::bassSource]  [static_cast<size_t>(k)] = shares[2] / total;
                snap.masks[Dsp::guitarSource][static_cast<size_t>(k)] = 0.0f;
                snap.masks[Dsp::otherSource] [static_cast<size_t>(k)] = shares[3] / total;
            }
            snap.available  = true;
            snap.confidence = clamp01(blendConf * sepConf);

            {
                juce::SpinLock::ScopedLockType lock(snapshotLock);
                latestSnapshot = snap;
            }
            inferenceInFlight.store(false, std::memory_order_release);
        },
        error);

    if (!dispatched) {
        status.store(static_cast<int>(Status::umx4ModelDetectedRuntimePending), std::memory_order_relaxed);
        lastError = error;
        inferenceInFlight.store(false, std::memory_order_release);
    }
}

juce::String ModelRunner::statusText() const {
    const auto current = static_cast<Status>(status.load(std::memory_order_relaxed));
    switch (current) {
        case Status::mlMasksActive:
            return "V2.0 UMX4 masks active";
        case Status::umx4ModelDetectedRuntimePending:
            if (modelDiscovered.load(std::memory_order_relaxed)) {
                auto text = juce::String("V2.0 UMX4 model found - runtime pending");
                if (!lastError.empty())
                    text << " (" << juce::String(lastError) << ")";
                return text;
            }
            return "V2.0 heuristic fallback";
        case Status::heuristicFallback:
            break;
    }
    return "V2.0 heuristic fallback";
}


} // namespace vxsuite::rebalance::ml
