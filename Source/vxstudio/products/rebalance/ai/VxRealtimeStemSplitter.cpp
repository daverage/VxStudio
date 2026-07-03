#include "VxRealtimeStemSplitter.h"

#include "IStemSeparationBackend.h"
#include "OnnxStemgenBackend.h"
#include "../../../framework/VxStudioFft.h"
#include "../../../framework/VxStudioStreamingResampler.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace vxsuite::rebalance::ai {

namespace {

constexpr float kNormTargetRms = 0.251f;
constexpr float kNormMaxGain = 100.0f;
constexpr float kNormMinInputRms = 0.000251f;
constexpr int kMaskFftOrder = 10;
constexpr int kMaskFftSize = 1 << kMaskFftOrder;
constexpr int kMaskBins = kMaskFftSize / 2 + 1;
constexpr int kRequestQueueSize = 4;
constexpr float kMaskEps = 1.0e-8f;

float calculateRms(const std::array<std::vector<float>, 2>& chunk) noexcept {
    double sum = 0.0;
    int count = 0;
    for (const auto& channel : chunk) {
        for (const float sample : channel) {
            sum += static_cast<double>(sample) * static_cast<double>(sample);
            ++count;
        }
    }
    return count > 0 ? std::sqrt(static_cast<float>(sum / static_cast<double>(count))) : 0.0f;
}

float calculateNormalizationGain(const std::array<std::vector<float>, 2>& chunk) noexcept {
    const float rms = calculateRms(chunk);
    if (rms < kNormMinInputRms)
        return 1.0f;

    return juce::jlimit(1.0f, kNormMaxGain, kNormTargetRms / rms);
}

void applyGain(std::array<std::vector<float>, 2>& chunk, const float gain) noexcept {
    if (std::abs(gain - 1.0f) <= 1.0e-5f)
        return;

    for (auto& channel : chunk) {
        for (float& sample : channel)
            sample *= gain;
    }
}

float smoothBand(const float hz, const float lo, const float hi) noexcept {
    if (hz <= lo || hz >= hi)
        return 0.0f;

    const float centre = 0.5f * (lo + hi);
    if (hz <= centre) {
        const float x = juce::jlimit(0.0f, 1.0f, (hz - lo) / std::max(1.0f, centre - lo));
        return x * x * (3.0f - 2.0f * x);
    }

    const float x = juce::jlimit(0.0f, 1.0f, (hi - hz) / std::max(1.0f, hi - centre));
    return x * x * (3.0f - 2.0f * x);
}

float guitarShareFromOtherStem(const float hz) noexcept {
    const float guitarBody = smoothBand(hz, 110.0f, 950.0f);
    const float guitarCore = smoothBand(hz, 520.0f, 3600.0f);
    const float guitarPresence = smoothBand(hz, 1800.0f, 7200.0f);
    const float nonGuitarLow = hz < 105.0f ? 1.0f : smoothBand(hz, 45.0f, 130.0f);
    const float nonGuitarAir = hz > 7600.0f ? smoothBand(hz, 6200.0f, 16000.0f) : 0.0f;

    const float claim = 0.03f + 0.26f * guitarBody + 0.58f * guitarCore + 0.20f * guitarPresence
        - 0.32f * nonGuitarLow - 0.18f * nonGuitarAir;
    return juce::jlimit(0.0f, 0.92f, claim);
}

} // namespace

struct RealtimeStemSplitter::Impl {
    using StemBuffers = IStemSeparationBackend::StemBuffers;

    struct Request {
        IStemSeparationBackend::ChannelBuffers context;
        IStemSeparationBackend::ChannelBuffers input;
        IStemSeparationBackend::ChannelBuffers lowFreq;
        float normalizationGain = 1.0f;
    };

    Impl() {
        maskFft.prepare(kMaskFftOrder);
        maskFftScratch.assign(static_cast<size_t>(kMaskFftSize * 2), 0.0f);
        maskWindow.assign(static_cast<size_t>(kOutputChunkSize), 0.0f);
        for (int i = 0; i < kOutputChunkSize; ++i)
            maskWindow[static_cast<size_t>(i)] =
                0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi * static_cast<float>(i)
                                       / static_cast<float>(std::max(1, kOutputChunkSize - 1)));

        for (auto& channel : accum)
            channel.assign(static_cast<size_t>(kOutputChunkSize), 0.0f);
        for (auto& channel : context)
            channel.assign(static_cast<size_t>(kContextSize), 0.0f);
        for (auto& request : requestQueue)
            allocateRequest(request);
        allocateRequest(workerRequest);
        allocateStemBuffers(outputChunks, kOutputChunkSize);
    }

    ~Impl() {
        stopWorker();
    }

    static void allocateRequest(Request& request) {
        for (auto& channel : request.context)
            channel.assign(static_cast<size_t>(kContextSize), 0.0f);
        for (auto& channel : request.input)
            channel.assign(static_cast<size_t>(kOutputChunkSize), 0.0f);
        for (auto& channel : request.lowFreq)
            channel.assign(static_cast<size_t>(kOutputChunkSize), 0.0f);
    }

    static void allocateStemBuffers(StemBuffers& buffers, const int sampleCount) {
        for (auto& stem : buffers) {
            for (auto& channel : stem)
                channel.assign(static_cast<size_t>(sampleCount), 0.0f);
        }
    }

    bool prepare(const double sampleRate, const int maxBlockSize, const int numChannels, juce::String& error) {
        hostSampleRate = sampleRate > 1000.0 ? sampleRate : kModelSampleRate;
        resamplingFromHostRate = std::abs(hostSampleRate - kModelSampleRate) > 1.0;
        resampler = std::make_unique<vxsuite::StreamingResampler<kStemBackendChannelCount, kStemBackendChannelCount>>(
            hostSampleRate, kModelSampleRate);
        const int sourceCapacity = std::max(1, maxBlockSize);
        const double targetRatio = std::max(1.0, kModelSampleRate / hostSampleRate);
        const int targetCapacity = std::max(kOutputChunkSize,
                                            static_cast<int>(std::ceil(static_cast<double>(sourceCapacity) * targetRatio)) + 128);
        for (auto& channel : sourceInputScratch)
            channel.assign(static_cast<size_t>(sourceCapacity), 0.0f);
        for (auto& channel : sourceOutputScratch)
            channel.assign(static_cast<size_t>(sourceCapacity), 0.0f);
        for (auto& channel : targetInputScratch)
            channel.assign(static_cast<size_t>(targetCapacity), 0.0f);
        for (auto& channel : targetOutputScratch)
            channel.assign(static_cast<size_t>(targetCapacity), 0.0f);

        backend = createBackend();
        if (!backend) {
            error = "No stem separation backend available";
            return false;
        }

        if (!backend->prepare(kModelSampleRate, kOutputChunkSize, kContextSize, numChannels, error))
            return false;

        startWorker();
        return true;
    }

    void reset() {
        accumCount = 0;
        for (auto& channel : accum)
            std::fill(channel.begin(), channel.end(), 0.0f);
        for (auto& channel : context)
            std::fill(channel.begin(), channel.end(), 0.0f);
        if (hostSampleRate > 1000.0)
            resampler = std::make_unique<vxsuite::StreamingResampler<kStemBackendChannelCount, kStemBackendChannelCount>>(
                hostSampleRate, kModelSampleRate);
        latestFrame = {};
        submittedFrames.store(0, std::memory_order_release);
        completedFrames.store(0, std::memory_order_release);
        failedFrames.store(0, std::memory_order_release);
        droppedFrames.store(0, std::memory_order_release);
        latestConfidence.store(0.0f, std::memory_order_release);
        hasLatestFrame.store(false, std::memory_order_release);
        if (std::unique_lock<std::mutex> lock { workMutex, std::try_to_lock }; lock.owns_lock()) {
            queueReadIndex = 0;
            queueWriteIndex = 0;
            queueCount = 0;
        }
    }

    bool processBlock(const juce::AudioBuffer<float>& input, Dsp::AiMaskFrame& maskFrame) {
        const int numSamples = input.getNumSamples();
        const int channels = std::min(input.getNumChannels(), kStemBackendChannelCount);

        if (resampler && !sourceInputScratch.front().empty()) {
            const int sourceCapacity = static_cast<int>(sourceInputScratch.front().size());
            for (int offset = 0; offset < numSamples; offset += sourceCapacity) {
                const int sliceSamples = std::min(sourceCapacity, numSamples - offset);
                for (int ch = 0; ch < kStemBackendChannelCount; ++ch) {
                    auto* scratch = sourceInputScratch[static_cast<size_t>(ch)].data();
                    if (ch < channels)
                        std::copy(input.getReadPointer(ch) + offset, input.getReadPointer(ch) + offset + sliceSamples, scratch);
                    else
                        std::fill(scratch, scratch + sliceSamples, 0.0f);
                }

                float* sourceInput[] = {
                    sourceInputScratch[0].data(),
                    sourceInputScratch[1].data()
                };
                float* sourceOutput[] = {
                    sourceOutputScratch[0].data(),
                    sourceOutputScratch[1].data()
                };
                float* targetInput[] = {
                    targetInputScratch[0].data(),
                    targetInputScratch[1].data()
                };
                float* targetOutput[] = {
                    targetOutputScratch[0].data(),
                    targetOutputScratch[1].data()
                };

                resampler->process(sourceInput,
                                   sourceOutput,
                                   targetInput,
                                   targetOutput,
                                   sliceSamples,
                                   [this](float* const* modelInput, float* const* modelOutput, const int modelSampleCount) {
                                       consumeModelBlock(modelInput, modelSampleCount);
                                       for (int ch = 0; ch < kStemBackendChannelCount; ++ch)
                                           juce::FloatVectorOperations::clear(modelOutput[ch], modelSampleCount);
                                   });
            }
        }

        if (std::unique_lock<std::mutex> lock { latestMutex, std::try_to_lock }; lock.owns_lock()) {
            if (latestFrame.available) {
                maskFrame = latestFrame;
                return true;
            }
        }

        return false;
    }

    juce::String statusText() const {
        if (backend)
            return backend->backendName() + (resamplingFromHostRate
                ? " resampling " + juce::String(hostSampleRate / 1000.0, 1) + " kHz to 44.1 kHz: "
                : ": ") + backend->statusText();

        return "No stem separation backend";
    }

    RealtimeStemSplitter::DebugSnapshot getDebugSnapshot() const noexcept {
        return {
            true,
            hasLatestFrame.load(std::memory_order_acquire),
            latestConfidence.load(std::memory_order_acquire),
            submittedFrames.load(std::memory_order_acquire),
            completedFrames.load(std::memory_order_acquire),
            failedFrames.load(std::memory_order_acquire),
            droppedFrames.load(std::memory_order_acquire)
        };
    }

    void startWorker() {
        if (worker.joinable())
            return;

        shouldStop.store(false, std::memory_order_release);
        worker = std::thread([this] { runWorker(); });
    }

    void stopWorker() {
        shouldStop.store(true, std::memory_order_release);
        workCv.notify_all();
        if (worker.joinable())
            worker.join();
    }

    void consumeModelBlock(float* const* modelInput, const int modelSampleCount) {
        for (int sample = 0; sample < modelSampleCount; ++sample) {
            for (int ch = 0; ch < kStemBackendChannelCount; ++ch)
                accum[static_cast<size_t>(ch)][static_cast<size_t>(accumCount)] = modelInput[ch][sample];

            ++accumCount;
            if (accumCount >= kOutputChunkSize)
                submitAccumulatedChunk();
        }
    }

    void submitAccumulatedChunk() {
        bool submitted = false;
        if (std::unique_lock<std::mutex> lock { workMutex, std::try_to_lock };
            lock.owns_lock() && queueCount < kRequestQueueSize) {
            auto& request = requestQueue[static_cast<size_t>(queueWriteIndex)];
            copyChunkToRequest(request);
            queueWriteIndex = (queueWriteIndex + 1) % kRequestQueueSize;
            ++queueCount;
            submitted = true;
            submittedFrames.fetch_add(1, std::memory_order_acq_rel);
            workCv.notify_one();
        }
        if (!submitted)
            droppedFrames.fetch_add(1, std::memory_order_acq_rel);

        for (int ch = 0; ch < kStemBackendChannelCount; ++ch) {
            auto& channelContext = context[static_cast<size_t>(ch)];
            std::move(channelContext.begin() + kOutputChunkSize, channelContext.end(), channelContext.begin());
            std::copy(accum[static_cast<size_t>(ch)].begin(),
                      accum[static_cast<size_t>(ch)].end(),
                      channelContext.end() - kOutputChunkSize);
        }

        accumCount = 0;
    }

    void copyChunkToRequest(Request& request) {
        for (int ch = 0; ch < kStemBackendChannelCount; ++ch) {
            std::copy(context[static_cast<size_t>(ch)].begin(),
                      context[static_cast<size_t>(ch)].end(),
                      request.context[static_cast<size_t>(ch)].begin());
            std::copy(accum[static_cast<size_t>(ch)].begin(),
                      accum[static_cast<size_t>(ch)].end(),
                      request.input[static_cast<size_t>(ch)].begin());
            std::fill(request.lowFreq[static_cast<size_t>(ch)].begin(),
                      request.lowFreq[static_cast<size_t>(ch)].end(),
                      0.0f);
        }
        request.normalizationGain = calculateNormalizationGain(request.input);
        applyGain(request.input, request.normalizationGain);
    }

    void runWorker() {
        while (!shouldStop.load(std::memory_order_acquire)) {
            {
                std::unique_lock<std::mutex> lock { workMutex };
                workCv.wait(lock, [this] {
                    return shouldStop.load(std::memory_order_acquire) || queueCount > 0;
                });

                if (shouldStop.load(std::memory_order_acquire))
                    break;

                copyRequest(requestQueue[static_cast<size_t>(queueReadIndex)], workerRequest);
                queueReadIndex = (queueReadIndex + 1) % kRequestQueueSize;
                --queueCount;
            }

            juce::String error;
            if (!backend || !backend->processFrame(workerRequest.context,
                                                   workerRequest.input,
                                                   workerRequest.lowFreq,
                                                   workerRequest.normalizationGain,
                                                   outputChunks,
                                                   error)) {
                failedFrames.fetch_add(1, std::memory_order_acq_rel);
                continue;
            }

            auto frame = buildSpectralMaskFrame();
            completedFrames.fetch_add(1, std::memory_order_acq_rel);
            latestConfidence.store(frame.confidence, std::memory_order_release);
            hasLatestFrame.store(true, std::memory_order_release);
            {
                std::lock_guard<std::mutex> lock { latestMutex };
                latestFrame = frame;
            }
        }
    }

    static void copyRequest(const Request& source, Request& destination) {
        for (int ch = 0; ch < kStemBackendChannelCount; ++ch) {
            std::copy(source.context[static_cast<size_t>(ch)].begin(),
                      source.context[static_cast<size_t>(ch)].end(),
                      destination.context[static_cast<size_t>(ch)].begin());
            std::copy(source.input[static_cast<size_t>(ch)].begin(),
                      source.input[static_cast<size_t>(ch)].end(),
                      destination.input[static_cast<size_t>(ch)].begin());
            std::copy(source.lowFreq[static_cast<size_t>(ch)].begin(),
                      source.lowFreq[static_cast<size_t>(ch)].end(),
                      destination.lowFreq[static_cast<size_t>(ch)].begin());
        }
        destination.normalizationGain = source.normalizationGain;
    }

    Dsp::AiMaskFrame buildSpectralMaskFrame() {
        std::array<std::array<float, kMaskBins>, kStemBackendStemCount> stemMagnitudes {};

        for (int stem = 0; stem < kStemBackendStemCount; ++stem)
            analyseStemSpectrum(stem, stemMagnitudes[static_cast<size_t>(stem)]);

        Dsp::AiMaskFrame frame;
        frame.available = true;

        float confidenceSum = 0.0f;
        float confidenceWeight = 0.0f;

        for (int bin = 0; bin < Dsp::kBins; ++bin) {
            if (bin >= kMaskBins) {
                for (int source = 0; source < Dsp::kSourceCount; ++source)
                    frame.masks[static_cast<size_t>(source)][static_cast<size_t>(bin)] = 0.0f;
                continue;
            }

            const float hz = static_cast<float>(bin) * static_cast<float>(kModelSampleRate)
                / static_cast<float>(kMaskFftSize);
            const float drums = stemMagnitudes[static_cast<size_t>(stemDrums)][static_cast<size_t>(bin)];
            const float bass = stemMagnitudes[static_cast<size_t>(stemBass)][static_cast<size_t>(bin)];
            const float vocals = stemMagnitudes[static_cast<size_t>(stemVocals)][static_cast<size_t>(bin)];
            const float other = stemMagnitudes[static_cast<size_t>(stemOther)][static_cast<size_t>(bin)];
            const float guitarShare = guitarShareFromOtherStem(hz);

            std::array<float, Dsp::kSourceCount> sourceEnergy {};
            sourceEnergy[static_cast<size_t>(Dsp::vocalsSource)] = vocals;
            sourceEnergy[static_cast<size_t>(Dsp::drumsSource)] = drums;
            sourceEnergy[static_cast<size_t>(Dsp::bassSource)] = bass;
            sourceEnergy[static_cast<size_t>(Dsp::guitarSource)] = other * guitarShare;
            sourceEnergy[static_cast<size_t>(Dsp::otherSource)] = other * (1.0f - guitarShare);

            float total = kMaskEps;
            float strongest = 0.0f;
            for (const float energy : sourceEnergy) {
                const float positive = std::max(0.0f, energy);
                total += positive;
                strongest = std::max(strongest, positive);
            }

            for (int source = 0; source < Dsp::kSourceCount; ++source)
                frame.masks[static_cast<size_t>(source)][static_cast<size_t>(bin)] =
                    juce::jlimit(0.0f, 1.0f, sourceEnergy[static_cast<size_t>(source)] / total);

            const float spectralWeight = juce::jlimit(0.0f, 1.0f, total * 0.25f);
            confidenceSum += spectralWeight * juce::jlimit(0.0f, 1.0f, strongest / total);
            confidenceWeight += spectralWeight;
        }

        const float meanDominance = confidenceWeight > kMaskEps ? confidenceSum / confidenceWeight : 0.0f;
        frame.confidence = juce::jlimit(0.30f, 0.72f, 0.34f + 0.46f * meanDominance);
        return frame;
    }

    void analyseStemSpectrum(const int stem, std::array<float, kMaskBins>& magnitudes) {
        std::fill(maskFftScratch.begin(), maskFftScratch.end(), 0.0f);

        const auto& stemBuffer = outputChunks[static_cast<size_t>(stem)];
        for (int i = 0; i < kOutputChunkSize; ++i) {
            float mono = 0.0f;
            for (int ch = 0; ch < kStemBackendChannelCount; ++ch)
                mono += stemBuffer[static_cast<size_t>(ch)][static_cast<size_t>(i)];
            mono *= 0.5f;
            maskFftScratch[static_cast<size_t>(i)] = mono * maskWindow[static_cast<size_t>(i)];
        }

        maskFft.performForward(maskFftScratch.data());

        for (int bin = 0; bin < kMaskBins; ++bin) {
            const float re = maskFftScratch[static_cast<size_t>(2 * bin)];
            const float im = (bin == 0 || bin == kMaskBins - 1)
                ? 0.0f
                : maskFftScratch[static_cast<size_t>(2 * bin + 1)];
            magnitudes[static_cast<size_t>(bin)] = std::sqrt(std::max(0.0f, re * re + im * im));
        }
    }

    static std::unique_ptr<IStemSeparationBackend> createBackend() {
        return std::make_unique<OnnxStemgenBackend>();
    }

    std::unique_ptr<IStemSeparationBackend> backend;
    vxsuite::RealFft maskFft;
    std::vector<float> maskFftScratch;
    std::vector<float> maskWindow;
    IStemSeparationBackend::ChannelBuffers accum;
    IStemSeparationBackend::ChannelBuffers context;
    int accumCount = 0;
    double hostSampleRate = kModelSampleRate;
    bool resamplingFromHostRate = false;
    std::unique_ptr<vxsuite::StreamingResampler<kStemBackendChannelCount, kStemBackendChannelCount>> resampler;
    IStemSeparationBackend::ChannelBuffers sourceInputScratch;
    IStemSeparationBackend::ChannelBuffers sourceOutputScratch;
    IStemSeparationBackend::ChannelBuffers targetInputScratch;
    IStemSeparationBackend::ChannelBuffers targetOutputScratch;

    std::array<Request, kRequestQueueSize> requestQueue;
    Request workerRequest;
    StemBuffers outputChunks;

    std::thread worker;
    std::atomic<bool> shouldStop { false };
    std::atomic<std::uint64_t> submittedFrames { 0 };
    std::atomic<std::uint64_t> completedFrames { 0 };
    std::atomic<std::uint64_t> failedFrames { 0 };
    std::atomic<std::uint64_t> droppedFrames { 0 };
    std::atomic<float> latestConfidence { 0.0f };
    std::atomic<bool> hasLatestFrame { false };
    std::mutex workMutex;
    std::condition_variable workCv;
    int queueReadIndex = 0;
    int queueWriteIndex = 0;
    int queueCount = 0;

    std::mutex latestMutex;
    Dsp::AiMaskFrame latestFrame {};
};

RealtimeStemSplitter::RealtimeStemSplitter() = default;

RealtimeStemSplitter::~RealtimeStemSplitter() {
    release();
}

void RealtimeStemSplitter::release() {
    impl.reset();
}

void RealtimeStemSplitter::prepare(const double sampleRate, const int maxBlockSize, const int numChannels) {
    currentSampleRate = sampleRate;
    currentMaxBlockSize = maxBlockSize;
    currentChannelCount = numChannels;

    release();
    impl = std::make_unique<Impl>();

    available = impl->prepare(currentSampleRate, currentMaxBlockSize, currentChannelCount, unavailableReason);
    if (!available)
        release();

    juce::ignoreUnused(currentMaxBlockSize, currentChannelCount);
}

void RealtimeStemSplitter::reset() {
    if (impl)
        impl->reset();
}

juce::String RealtimeStemSplitter::statusText() const {
    if (available && impl)
        return "AI splitter active: " + impl->statusText();

    return "AI splitter standby: " + unavailableReason;
}

RealtimeStemSplitter::DebugSnapshot RealtimeStemSplitter::getDebugSnapshot() const noexcept {
    if (available && impl)
        return impl->getDebugSnapshot();

    return {};
}

bool RealtimeStemSplitter::processBlock(const juce::AudioBuffer<float>& input,
                                        Dsp::AiMaskFrame& maskFrame) noexcept {
    if (!available || !impl)
        return false;

    return impl->processBlock(input, maskFrame);
}

} // namespace vxsuite::rebalance::ai
