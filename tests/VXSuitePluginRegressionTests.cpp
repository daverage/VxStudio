#include "../Source/vxsuite/products/cleanup/VxCleanupProcessor.h"
#include "../Source/vxsuite/products/deverb/VxDeverbProcessor.h"
#include "../Source/vxsuite/products/denoiser/VxDenoiserProcessor.h"
#include "../Source/vxsuite/products/finish/VxFinishProcessor.h"
#include "../Source/vxsuite/products/proximity/VxProximityProcessor.h"
#include "../Source/vxsuite/products/subtract/VxSubtractProcessor.h"
#include "../Source/vxsuite/products/tone/VxToneProcessor.h"
#include "VxSuiteProcessorTestUtils.h"

#include <atomic>
#include <array>
#include <cstdlib>
#include <iostream>
#include <new>

namespace {

using namespace vxsuite::test;

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

bool primeSubtractLearn(VXSubtractAudioProcessor& processor, double sr);

juce::AudioBuffer<float> renderSubtractCleanupProximityFinishChain(const double sr,
                                                                   const int blockSize,
                                                                   const juce::AudioBuffer<float>& input) {
    VXSubtractAudioProcessor subtract;
    subtract.prepareToPlay(sr, blockSize);
    if (!primeSubtractLearn(subtract, sr))
        return {};
    setParamNormalized(subtract, "subtract", 0.78f);
    setParamNormalized(subtract, "protect", 0.48f);
    auto afterSubtract = render(subtract, input, blockSize);

    VXCleanupAudioProcessor cleanup;
    cleanup.prepareToPlay(sr, blockSize);
    setParamNormalized(cleanup, "cleanup", 0.52f);
    setParamNormalized(cleanup, "body", 0.44f);
    setParamNormalized(cleanup, "focus", 0.55f);
    auto afterCleanup = render(cleanup, afterSubtract, blockSize);

    VXProximityAudioProcessor proximity;
    proximity.prepareToPlay(sr, blockSize);
    setParamNormalized(proximity, "closer", 0.24f);
    setParamNormalized(proximity, "air", 0.18f);
    auto afterProximity = render(proximity, afterCleanup, blockSize);

    VXFinishAudioProcessor finish;
    finish.prepareToPlay(sr, blockSize);
    setParamNormalized(finish, "finish", 0.42f);
    setParamNormalized(finish, "body", 0.32f);
    setParamNormalized(finish, "gain", 0.28f);
    return render(finish, afterProximity, blockSize);
}

juce::AudioBuffer<float> makeMonoBuffer(const juce::AudioBuffer<float>& stereo) {
    juce::AudioBuffer<float> mono(1, stereo.getNumSamples());
    for (int i = 0; i < stereo.getNumSamples(); ++i) {
        const float sample = 0.5f * (stereo.getSample(0, i) + stereo.getSample(std::min(1, stereo.getNumChannels() - 1), i));
        mono.setSample(0, i, sample);
    }
    return mono;
}

bool testCleanupZeroIsIdentity() {
    constexpr double sr = 48000.0;
    auto dry = makeSpeechLike(sr, 1.0f);

    VXCleanupAudioProcessor processor;
    processor.prepareToPlay(sr, 256);
    setParamNormalized(processor, "cleanup", 0.0f);
    setParamNormalized(processor, "body", 0.5f);
    setParamNormalized(processor, "focus", 0.5f);

    const auto out = render(processor, dry);
    const float diff = maxAbsDiff(dry, out);
    if (diff > 1.0e-6f) {
        std::cerr << "[VXSuitePluginRegression] Cleanup zero should be identity: diff=" << diff << "\n";
        return false;
    }
    return true;
}

bool primeSubtractLearn(VXSubtractAudioProcessor& processor, const double sr) {
    juce::AudioBuffer<float> warmup(2, 256);
    warmup.clear();
    setParamNormalized(processor, "learn", 0.0f);
    processSingleBlock(processor, warmup);

    auto noise = makeNoise(sr, 0.8f, 0.10f);
    setParamNormalized(processor, "learn", 1.0f);

    float lastProgress = 0.0f;
    float lastObserved = 0.0f;
    juce::MidiBuffer midi;
    constexpr int blockSize = 256;
    for (int start = 0; start < noise.getNumSamples(); start += blockSize) {
        const int num = std::min(blockSize, noise.getNumSamples() - start);
        juce::AudioBuffer<float> block(2, num);
        for (int ch = 0; ch < 2; ++ch)
            block.copyFrom(ch, 0, noise, ch, start, num);
        processor.processBlock(block, midi);
        const float progress = processor.getLearnProgress();
        const float observed = processor.getLearnObservedSeconds();
        if (progress + 1.0e-5f < lastProgress) {
            std::cerr << "[VXSuitePluginRegression] Subtract learn progress regressed\n";
            return false;
        }
        if (observed + 1.0e-5f < lastObserved) {
            std::cerr << "[VXSuitePluginRegression] Subtract observed learn seconds regressed\n";
            return false;
        }
        lastProgress = progress;
        lastObserved = observed;
    }

    if (!(processor.isLearnActive() && processor.getLearnProgress() > 0.20f && processor.getLearnObservedSeconds() > 0.10f)) {
        std::cerr << "[VXSuitePluginRegression] Subtract learn never accumulated sensible progress\n";
        return false;
    }

    setParamNormalized(processor, "learn", 0.0f);
    juce::AudioBuffer<float> stopBlock(2, 256);
    stopBlock.clear();
    processSingleBlock(processor, stopBlock);

    if (processor.isLearnActive() || !processor.isLearnReady()) {
        std::cerr << "[VXSuitePluginRegression] Subtract learn did not finalize into a ready profile\n";
        return false;
    }
    if (processor.getLearnConfidence() < 0.0f || processor.getLearnConfidence() > 1.0f) {
        std::cerr << "[VXSuitePluginRegression] Subtract learn confidence out of range\n";
        return false;
    }
    return true;
}

bool testSubtractLearnLifecycleMakesSense() {
    constexpr double sr = 48000.0;
    VXSubtractAudioProcessor processor;
    processor.prepareToPlay(sr, 256);
    return primeSubtractLearn(processor, sr);
}

bool testSubtractLearnStartsOnFirstPress() {
    constexpr double sr = 48000.0;
    VXSubtractAudioProcessor processor;
    processor.prepareToPlay(sr, 256);

    auto noise = makeNoise(sr, 0.2f, 0.08f);
    setParamNormalized(processor, "learn", 1.0f);
    juce::AudioBuffer<float> firstBlock(2, 256);
    for (int ch = 0; ch < 2; ++ch)
        firstBlock.copyFrom(ch, 0, noise, ch, 0, 256);
    processSingleBlock(processor, firstBlock);

    if (!processor.isLearnActive()) {
        std::cerr << "[VXSuitePluginRegression] Subtract learn did not start on the first press after prepare/reset\n";
        return false;
    }
    return true;
}

bool testSubtractListenOutputsMeaningfulRemovedDelta() {
    constexpr double sr = 48000.0;
    VXSubtractAudioProcessor processor;
    processor.prepareToPlay(sr, 256);
    if (!primeSubtractLearn(processor, sr))
        return false;

    auto speech = makeSpeechLike(sr, 1.0f);
    auto noise = makeNoise(sr, 1.0f, 0.08f);
    auto noisy = addBuffers(speech, noise);

    setParamNormalized(processor, "subtract", 0.82f);
    setParamNormalized(processor, "protect", 0.45f);
    setParamNormalized(processor, "listen", 0.0f);
    auto wet = render(processor, noisy);

    processor.reset();
    setParamNormalized(processor, "subtract", 0.82f);
    setParamNormalized(processor, "protect", 0.45f);
    setParamNormalized(processor, "listen", 1.0f);
    auto listen = render(processor, noisy);

    const float listenRms = rms(listen);
    const auto recombined = addBuffers(wet, listen);
    const float recombineDiff = maxAbsDiffSkip(noisy, recombined, 4096);
    if (!(listenRms > 1.0e-4f)) {
        std::cerr << "[VXSuitePluginRegression] Subtract listen output was unexpectedly empty\n";
        return false;
    }
    if (recombineDiff > 5.0e-2f) {
        std::cerr << "[VXSuitePluginRegression] Subtract wet/listen steady-state no longer recombines close to dry input: diff="
                  << recombineDiff << "\n";
        return false;
    }
    return true;
}

bool testCleanupFinishSubtractChainStaysStable() {
    constexpr double sr = 48000.0;
    auto speech = makeSpeechLike(sr, 1.2f);
    auto noise = makeNoise(sr, 1.2f, 0.07f);
    auto noisy = addBuffers(speech, noise);
    auto finalOut = renderSubtractCleanupProximityFinishChain(sr, 256, noisy);

    if (!allFinite(finalOut)) {
        std::cerr << "[VXSuitePluginRegression] Combined chain produced non-finite samples\n";
        return false;
    }
    if (peakAbs(finalOut) > 1.05f) {
        std::cerr << "[VXSuitePluginRegression] Combined chain peak too high: peak=" << peakAbs(finalOut) << "\n";
        return false;
    }
    const float corr = std::abs(speechBandCorrelation(speech, finalOut, sr));
    if (corr < 0.45f) {
        std::cerr << "[VXSuitePluginRegression] Combined chain damaged speech coherence too much: |corr|=" << corr << "\n";
        return false;
    }
    return true;
}

bool testDeverbExtremeBlendStaysStable() {
    constexpr double sr = 48000.0;
    constexpr int tailSamples = static_cast<int>(sr * 0.35);

    auto speech = makeSpeechLike(sr, 0.35f);
    juce::AudioBuffer<float> input(2, speech.getNumSamples() + tailSamples);
    input.clear();
    for (int ch = 0; ch < std::min(2, speech.getNumChannels()); ++ch)
        input.copyFrom(ch, 0, speech, ch, 0, speech.getNumSamples());

    VXDeverbAudioProcessor deverb;
    deverb.prepareToPlay(sr, 256);
    setParamNormalized(deverb, "reduce", 1.0f);
    setParamNormalized(deverb, "body", 1.0f);
    auto out = render(deverb, input, 256);

    const float outPeak = peakAbs(out);
    if (!allFinite(out) || outPeak > 1.05f) {
        std::cerr << "[VXSuitePluginRegression] Deverb extreme settings produced unstable output: finite="
                  << allFinite(out) << " peak=" << outPeak << "\n";
        return false;
    }

    juce::AudioBuffer<float> tail(2, tailSamples);
    for (int ch = 0; ch < 2; ++ch)
        tail.copyFrom(ch, 0, out, ch, out.getNumSamples() - tailSamples, tailSamples);
    if (rms(tail) > 0.02f) {
        std::cerr << "[VXSuitePluginRegression] Deverb extreme Blend left too much sustained tail / buzzing: rms="
                  << rms(tail) << "\n";
        return false;
    }
    return true;
}

bool testCleanupBlockSizeInvariance() {
    constexpr double sr = 48000.0;
    auto input = makeSpeechLike(sr, 1.1f);

    VXCleanupAudioProcessor cleanup64;
    cleanup64.prepareToPlay(sr, 64);
    setParamNormalized(cleanup64, "cleanup", 0.58f);
    setParamNormalized(cleanup64, "body", 0.40f);
    setParamNormalized(cleanup64, "focus", 0.62f);
    const auto out64 = render(cleanup64, input, 64);

    VXCleanupAudioProcessor cleanup512;
    cleanup512.prepareToPlay(sr, 512);
    setParamNormalized(cleanup512, "cleanup", 0.58f);
    setParamNormalized(cleanup512, "body", 0.40f);
    setParamNormalized(cleanup512, "focus", 0.62f);
    const auto out512 = render(cleanup512, input, 512);

    const float corr = bufferCorrelationSkip(out64, out512, 1024);
    if (corr < 0.985f) {
        std::cerr << "[VXSuitePluginRegression] Cleanup changed too much across host block sizes: corr=" << corr << "\n";
        return false;
    }
    return true;
}

bool testFullChainBlockSizeInvariance() {
    constexpr double sr = 48000.0;
    auto speech = makeSpeechLike(sr, 1.3f);
    auto noise = makeNoise(sr, 1.3f, 0.07f);
    auto noisy = addBuffers(speech, noise);

    const auto out64 = renderSubtractCleanupProximityFinishChain(sr, 64, noisy);
    if (out64.getNumSamples() <= 0)
        return false;
    const auto out512 = renderSubtractCleanupProximityFinishChain(sr, 512, noisy);
    if (out512.getNumSamples() <= 0)
        return false;

    const float corr = bufferCorrelationSkip(out64, out512, 4096);
    if (corr < 0.95f) {
        std::cerr << "[VXSuitePluginRegression] Full chain changed too much across host block sizes: corr=" << corr << "\n";
        return false;
    }
    return true;
}

bool testCombinedChainKeepsSilenceSilent() {
    constexpr double sr = 48000.0;
    juce::AudioBuffer<float> silence(2, static_cast<int>(sr * 0.75f));
    silence.clear();

    VXSubtractAudioProcessor subtract;
    subtract.prepareToPlay(sr, 256);
    setParamNormalized(subtract, "subtract", 0.80f);
    setParamNormalized(subtract, "protect", 0.40f);
    auto afterSubtract = render(subtract, silence);

    VXCleanupAudioProcessor cleanup;
    cleanup.prepareToPlay(sr, 256);
    setParamNormalized(cleanup, "cleanup", 0.60f);
    setParamNormalized(cleanup, "body", 0.40f);
    setParamNormalized(cleanup, "focus", 0.60f);
    auto afterCleanup = render(cleanup, afterSubtract);

    VXFinishAudioProcessor finish;
    finish.prepareToPlay(sr, 256);
    setParamNormalized(finish, "finish", 0.45f);
    setParamNormalized(finish, "body", 0.25f);
    setParamNormalized(finish, "gain", 0.20f);
    auto finalOut = render(finish, afterCleanup);

    if (rms(finalOut) > 1.0e-5f) {
        std::cerr << "[VXSuitePluginRegression] Combined chain raised silence too far above zero: rms=" << rms(finalOut) << "\n";
        return false;
    }
    return true;
}

bool testCleanupStrongSettingIsAudibleButBounded() {
    constexpr double sr = 48000.0;
    auto noisy = addBuffers(makeSpeechLike(sr, 1.0f), makeNoise(sr, 1.0f, 0.05f));

    VXCleanupAudioProcessor cleanup;
    cleanup.prepareToPlay(sr, 256);
    setParamNormalized(cleanup, "cleanup", 0.85f);
    setParamNormalized(cleanup, "body", 0.35f);
    setParamNormalized(cleanup, "focus", 0.72f);
    auto out = render(cleanup, noisy, 256);

    const float diff = maxAbsDiffSkip(noisy, out, 1024);
    if (diff < 0.01f) {
        std::cerr << "[VXSuitePluginRegression] Cleanup strong setting is still too subtle\n";
        return false;
    }
    if (!allFinite(out) || peakAbs(out) > 1.02f) {
        std::cerr << "[VXSuitePluginRegression] Cleanup strong setting clipped or became unstable\n";
        return false;
    }
    return true;
}

bool testFinishStrongSettingsAreAudibleButBounded() {
    constexpr double sr = 48000.0;
    auto input = makeSpeechLike(sr, 1.0f);

    VXFinishAudioProcessor finishLow;
    finishLow.prepareToPlay(sr, 256);
    setParamNormalized(finishLow, "finish", 0.15f);
    setParamNormalized(finishLow, "body", 0.15f);
    setParamNormalized(finishLow, "gain", 0.15f);
    const auto lowOut = render(finishLow, input, 256);

    VXFinishAudioProcessor finishHigh;
    finishHigh.prepareToPlay(sr, 256);
    setParamNormalized(finishHigh, "finish", 0.80f);
    setParamNormalized(finishHigh, "body", 0.80f);
    setParamNormalized(finishHigh, "gain", 0.70f);
    const auto highOut = render(finishHigh, input, 256);

    const float diff = maxAbsDiffSkip(lowOut, highOut, 512);
    if (diff < 0.01f) {
        std::cerr << "[VXSuitePluginRegression] Finish controls still do not move enough between low and high settings\n";
        return false;
    }
    if (!allFinite(highOut) || peakAbs(highOut) > 1.02f) {
        std::cerr << "[VXSuitePluginRegression] Finish strong settings clipped or became unstable\n";
        return false;
    }
    return true;
}

bool testFinishGainIsBipolarAroundCenter() {
    constexpr double sr = 48000.0;
    auto input = makeSpeechLike(sr, 1.0f);

    VXFinishAudioProcessor finishCut;
    finishCut.prepareToPlay(sr, 256);
    setParamNormalized(finishCut, "finish", 0.40f);
    setParamNormalized(finishCut, "body", 0.35f);
    setParamNormalized(finishCut, "gain", 0.20f);
    const auto cutOut = render(finishCut, input, 256);

    VXFinishAudioProcessor finishMid;
    finishMid.prepareToPlay(sr, 256);
    setParamNormalized(finishMid, "finish", 0.40f);
    setParamNormalized(finishMid, "body", 0.35f);
    setParamNormalized(finishMid, "gain", 0.50f);
    const auto midOut = render(finishMid, input, 256);

    VXFinishAudioProcessor finishBoost;
    finishBoost.prepareToPlay(sr, 256);
    setParamNormalized(finishBoost, "finish", 0.40f);
    setParamNormalized(finishBoost, "body", 0.35f);
    setParamNormalized(finishBoost, "gain", 0.80f);
    const auto boostOut = render(finishBoost, input, 256);

    const float cutRms = rms(cutOut);
    const float midRms = rms(midOut);
    const float boostRms = rms(boostOut);
    if (!(cutRms + 1.0e-4f < midRms && midRms + 1.0e-4f < boostRms)) {
        std::cerr << "[VXSuitePluginRegression] Finish gain is not behaving like a centered bipolar control\n";
        return false;
    }
    if (!allFinite(boostOut) || peakAbs(boostOut) > 1.02f) {
        std::cerr << "[VXSuitePluginRegression] Finish gain boost clipped or became unstable\n";
        return false;
    }
    return true;
}

bool testFinishResetIsDeterministic() {
    constexpr double sr = 48000.0;
    auto input = makeSpeechLike(sr, 1.0f);

    VXFinishAudioProcessor finish;
    finish.prepareToPlay(sr, 256);
    setParamNormalized(finish, "finish", 0.55f);
    setParamNormalized(finish, "body", 0.72f);
    setParamNormalized(finish, "gain", 0.5f);
    const auto first = render(finish, input, 256);

    finish.reset();
    setParamNormalized(finish, "finish", 0.55f);
    setParamNormalized(finish, "body", 0.72f);
    setParamNormalized(finish, "gain", 0.5f);
    const auto second = render(finish, input, 256);

    if (maxAbsDiffSkip(first, second, 512) > 1.0e-4f) {
        std::cerr << "[VXSuitePluginRegression] Finish reset no longer restores deterministic compressor state\n";
        return false;
    }
    if (!allFinite(second) || peakAbs(second) > 1.02f) {
        std::cerr << "[VXSuitePluginRegression] Finish reset path became unstable\n";
        return false;
    }
    return true;
}

bool testToneCenterIsIdentityAndExtremesStayBounded() {
    constexpr double sr = 48000.0;
    auto input = makeSpeechLike(sr, 1.0f);

    VXToneAudioProcessor toneFlat;
    toneFlat.prepareToPlay(sr, 256);
    setParamNormalized(toneFlat, "bass", 0.5f);
    setParamNormalized(toneFlat, "treble", 0.5f);
    const auto flatOut = render(toneFlat, input, 256);
    if (maxAbsDiff(input, flatOut) > 1.0e-6f) {
        std::cerr << "[VXSuitePluginRegression] Tone center should be identity\n";
        return false;
    }

    VXToneAudioProcessor toneExtreme;
    toneExtreme.prepareToPlay(sr, 256);
    setParamNormalized(toneExtreme, "bass", 1.0f);
    setParamNormalized(toneExtreme, "treble", 1.0f);
    const auto boostedOut = render(toneExtreme, input, 256);
    if (!allFinite(boostedOut) || peakAbs(boostedOut) > 1.02f) {
        std::cerr << "[VXSuitePluginRegression] Tone extreme boost clipped or became unstable\n";
        return false;
    }
    if (maxAbsDiffSkip(input, boostedOut, 128) < 0.01f) {
        std::cerr << "[VXSuitePluginRegression] Tone extreme boost was too subtle\n";
        return false;
    }
    return true;
}

bool testProximityExtremeIsBoundedAndAdditive() {
    constexpr double sr = 48000.0;
    auto input = makeSpeechLike(sr, 1.0f);

    VXProximityAudioProcessor proximity;
    proximity.prepareToPlay(sr, 256);
    setParamNormalized(proximity, "closer", 1.0f);
    setParamNormalized(proximity, "air", 1.0f);
    const auto out = render(proximity, input, 256);

    if (!allFinite(out) || peakAbs(out) > 1.02f) {
        std::cerr << "[VXSuitePluginRegression] Proximity extreme setting clipped or became unstable\n";
        return false;
    }
    if (maxAbsDiffSkip(input, out, 128) < 0.01f) {
        std::cerr << "[VXSuitePluginRegression] Proximity extreme setting was too subtle\n";
        return false;
    }
    return true;
}

bool testDenoiserStrongSettingStaysCoherentAndBounded() {
    constexpr double sr = 48000.0;
    auto speech = makeSpeechLike(sr, 1.0f);
    auto noisy = addBuffers(speech, makeNoise(sr, 1.0f, 0.07f));

    VXDenoiserAudioProcessor denoiser;
    denoiser.prepareToPlay(sr, 256);
    setParamNormalized(denoiser, "clean", 0.90f);
    setParamNormalized(denoiser, "guard", 0.65f);
    const auto out = render(denoiser, noisy, 256);

    if (!allFinite(out) || peakAbs(out) > 1.05f) {
        std::cerr << "[VXSuitePluginRegression] Denoiser strong setting clipped or became unstable\n";
        return false;
    }
    const float corr = std::abs(speechBandCorrelation(speech, out, sr));
    if (corr < 0.40f) {
        std::cerr << "[VXSuitePluginRegression] Denoiser strong setting damaged speech coherence too much: |corr|="
                  << corr << "\n";
        return false;
    }
    return true;
}

bool testLifecycleAndStateRestore() {
    constexpr double srA = 48000.0;
    constexpr double srB = 44100.0;

    VXSubtractAudioProcessor subtract;
    subtract.prepareToPlay(srA, 256);
    if (!primeSubtractLearn(subtract, srA))
        return false;

    juce::MemoryBlock state;
    subtract.getStateInformation(state);
    const float savedConfidence = subtract.getLearnConfidence();

    VXSubtractAudioProcessor restored;
    restored.prepareToPlay(srA, 256);
    restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    if (!restored.isLearnReady()) {
        std::cerr << "[VXSuitePluginRegression] Restored subtract state lost learned profile readiness\n";
        return false;
    }
    if (std::abs(restored.getLearnConfidence() - savedConfidence) > 0.08f) {
        std::cerr << "[VXSuitePluginRegression] Restored subtract confidence drifted too far\n";
        return false;
    }

    auto input = addBuffers(makeSpeechLike(srA, 0.7f), makeNoise(srA, 0.7f, 0.05f));
    auto first = render(restored, input, 128);
    restored.reset();
    auto second = render(restored, input, 128);
    if (!allFinite(first) || !allFinite(second)) {
        std::cerr << "[VXSuitePluginRegression] Reset lifecycle produced non-finite output\n";
        return false;
    }

    restored.prepareToPlay(srB, 512);
    auto third = render(restored, addBuffers(makeSpeechLike(srB, 0.7f), makeNoise(srB, 0.7f, 0.05f)), 512);
    if (!allFinite(third)) {
        std::cerr << "[VXSuitePluginRegression] Sample-rate reprepare produced non-finite output\n";
        return false;
    }
    return true;
}

bool testListenSemanticsAcrossPlugins() {
    constexpr double sr = 48000.0;
    auto speech = makeSpeechLike(sr, 0.9f);
    auto noise = makeNoise(sr, 0.9f, 0.06f);
    auto noisy = addBuffers(speech, noise);

    VXCleanupAudioProcessor cleanup;
    cleanup.prepareToPlay(sr, 256);
    setParamNormalized(cleanup, "cleanup", 0.60f);
    setParamNormalized(cleanup, "body", 0.42f);
    setParamNormalized(cleanup, "focus", 0.55f);
    setParamNormalized(cleanup, "listen", 0.0f);
    const auto cleanupWet = render(cleanup, noisy);
    cleanup.reset();
    setParamNormalized(cleanup, "cleanup", 0.60f);
    setParamNormalized(cleanup, "body", 0.42f);
    setParamNormalized(cleanup, "focus", 0.55f);
    setParamNormalized(cleanup, "listen", 1.0f);
    const auto cleanupListen = render(cleanup, noisy);
    if (maxAbsDiffSkip(noisy, addBuffers(cleanupWet, cleanupListen), 2048) > 0.08f) {
        std::cerr << "[VXSuitePluginRegression] Cleanup listen no longer behaves like removed-content audition\n";
        return false;
    }

    VXProximityAudioProcessor proximity;
    proximity.prepareToPlay(sr, 256);
    setParamNormalized(proximity, "closer", 0.30f);
    setParamNormalized(proximity, "air", 0.22f);
    setParamNormalized(proximity, "listen", 0.0f);
    const auto proxWet = render(proximity, speech);
    proximity.reset();
    setParamNormalized(proximity, "closer", 0.30f);
    setParamNormalized(proximity, "air", 0.22f);
    setParamNormalized(proximity, "listen", 1.0f);
    const auto proxListen = render(proximity, speech);
    if (maxAbsDiffSkip(proxWet, addBuffers(speech, proxListen), 128) > 1.0e-4f) {
        std::cerr << "[VXSuitePluginRegression] Proximity listen no longer behaves like additive delta audition\n";
        return false;
    }

    VXFinishAudioProcessor finish;
    finish.prepareToPlay(sr, 256);
    setParamNormalized(finish, "finish", 0.45f);
    setParamNormalized(finish, "body", 0.28f);
    setParamNormalized(finish, "gain", 0.35f);
    setParamNormalized(finish, "listen", 0.0f);
    const auto finishWet = render(finish, speech);
    finish.reset();
    setParamNormalized(finish, "finish", 0.45f);
    setParamNormalized(finish, "body", 0.28f);
    setParamNormalized(finish, "gain", 0.35f);
    setParamNormalized(finish, "listen", 1.0f);
    const auto finishListen = render(finish, speech);
    if (maxAbsDiffSkip(finishWet, addBuffers(speech, finishListen), 128) > 1.0e-3f) {
        std::cerr << "[VXSuitePluginRegression] Finish listen no longer behaves like finish-delta audition\n";
        return false;
    }
    return true;
}

bool testMultiRateAndBufferCoverage() {
    constexpr std::array<double, 3> sampleRates { 44100.0, 48000.0, 96000.0 };
    constexpr std::array<int, 4> blockSizes { 64, 128, 256, 512 };

    for (const double sr : sampleRates) {
        auto speech = makeSpeechLike(sr, 0.9f);
        auto noise = makeNoise(sr, 0.9f, 0.06f);
        auto noisy = addBuffers(speech, noise);
        for (const int blockSize : blockSizes) {
            auto out = renderSubtractCleanupProximityFinishChain(sr, blockSize, noisy);
            if (out.getNumSamples() <= 0 || !allFinite(out) || peakAbs(out) > 1.10f) {
                std::cerr << "[VXSuitePluginRegression] Chain failed sample-rate/buffer coverage at sr="
                          << sr << " block=" << blockSize << "\n";
                return false;
            }
        }
    }
    return true;
}

bool testMonoStereoConsistency() {
    constexpr double sr = 48000.0;
    auto stereoInput = makeSpeechLike(sr, 1.0f);
    auto monoInput = makeMonoBuffer(stereoInput);

    VXCleanupAudioProcessor cleanupStereo;
    cleanupStereo.prepareToPlay(sr, 256);
    setParamNormalized(cleanupStereo, "cleanup", 0.56f);
    setParamNormalized(cleanupStereo, "body", 0.46f);
    setParamNormalized(cleanupStereo, "focus", 0.58f);
    const auto stereoOut = render(cleanupStereo, stereoInput, 256);

    VXCleanupAudioProcessor cleanupMono;
    cleanupMono.prepareToPlay(sr, 256);
    setParamNormalized(cleanupMono, "cleanup", 0.56f);
    setParamNormalized(cleanupMono, "body", 0.46f);
    setParamNormalized(cleanupMono, "focus", 0.58f);
    const auto monoOut = render(cleanupMono, monoInput, 256);

    juce::AudioBuffer<float> stereoMid(1, stereoOut.getNumSamples());
    for (int i = 0; i < stereoOut.getNumSamples(); ++i)
        stereoMid.setSample(0, i, 0.5f * (stereoOut.getSample(0, i) + stereoOut.getSample(1, i)));

    const float corr = bufferCorrelationSkip(monoOut, stereoMid, 1024);
    if (corr < 0.98f) {
        std::cerr << "[VXSuitePluginRegression] Mono/stereo cleanup paths diverged too far: corr=" << corr << "\n";
        return false;
    }
    return true;
}

bool testNoSteadyStateAllocationsOnAudioThread() {
    constexpr double sr = 48000.0;
    auto noisy = addBuffers(makeSpeechLike(sr, 0.4f), makeNoise(sr, 0.4f, 0.05f));

    VXCleanupAudioProcessor cleanup;
    cleanup.prepareToPlay(sr, 256);
    setParamNormalized(cleanup, "cleanup", 0.55f);
    setParamNormalized(cleanup, "body", 0.45f);
    setParamNormalized(cleanup, "focus", 0.55f);

    {
        auto warmup = noisy;
        juce::MidiBuffer midi;
        cleanup.processBlock(warmup, midi);
    }

    auto testBlock = noisy;
    juce::MidiBuffer midi;
    AllocationScope allocationScope;
    cleanup.processBlock(testBlock, midi);
    if (allocationScope.allocations() != 0) {
        std::cerr << "[VXSuitePluginRegression] Audio-thread allocation detected during steady-state cleanup processing: count="
                  << allocationScope.allocations() << "\n";
        return false;
    }
    return true;
}

} // namespace

void* operator new(std::size_t size) {
    if (gAllocationTrackingEnabled.load(std::memory_order_relaxed))
        gTrackedAllocations.fetch_add(1, std::memory_order_relaxed);
    if (void* ptr = std::malloc(size))
        return ptr;
    throw std::bad_alloc();
}

void operator delete(void* ptr) noexcept {
    std::free(ptr);
}

void operator delete(void* ptr, std::size_t) noexcept {
    std::free(ptr);
}

void* operator new[](std::size_t size) {
    if (gAllocationTrackingEnabled.load(std::memory_order_relaxed))
        gTrackedAllocations.fetch_add(1, std::memory_order_relaxed);
    if (void* ptr = std::malloc(size))
        return ptr;
    throw std::bad_alloc();
}

void operator delete[](void* ptr) noexcept {
    std::free(ptr);
}

void operator delete[](void* ptr, std::size_t) noexcept {
    std::free(ptr);
}

int main() {
    bool ok = true;
    ok &= testCleanupZeroIsIdentity();
    ok &= testSubtractLearnStartsOnFirstPress();
    ok &= testSubtractLearnLifecycleMakesSense();
    ok &= testSubtractListenOutputsMeaningfulRemovedDelta();
    ok &= testDeverbExtremeBlendStaysStable();
    ok &= testCleanupFinishSubtractChainStaysStable();
    ok &= testCleanupStrongSettingIsAudibleButBounded();
    ok &= testFinishStrongSettingsAreAudibleButBounded();
    ok &= testFinishGainIsBipolarAroundCenter();
    ok &= testFinishResetIsDeterministic();
    ok &= testToneCenterIsIdentityAndExtremesStayBounded();
    ok &= testProximityExtremeIsBoundedAndAdditive();
    ok &= testDenoiserStrongSettingStaysCoherentAndBounded();
    ok &= testCleanupBlockSizeInvariance();
    ok &= testFullChainBlockSizeInvariance();
    ok &= testLifecycleAndStateRestore();
    ok &= testListenSemanticsAcrossPlugins();
    ok &= testMultiRateAndBufferCoverage();
    ok &= testMonoStereoConsistency();
    ok &= testNoSteadyStateAllocationsOnAudioThread();
    ok &= testCombinedChainKeepsSilenceSilent();
    return ok ? 0 : 1;
}
