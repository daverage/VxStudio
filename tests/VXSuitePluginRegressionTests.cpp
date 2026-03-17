#include "../Source/vxsuite/products/cleanup/VxCleanupProcessor.h"
#include "../Source/vxsuite/products/finish/VxFinishProcessor.h"
#include "../Source/vxsuite/products/proximity/VxProximityProcessor.h"
#include "../Source/vxsuite/products/subtract/VxSubtractProcessor.h"
#include "VxSuiteProcessorTestUtils.h"

#include <iostream>

namespace {

using namespace vxsuite::test;

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

} // namespace

int main() {
    bool ok = true;
    ok &= testCleanupZeroIsIdentity();
    ok &= testSubtractLearnLifecycleMakesSense();
    ok &= testSubtractListenOutputsMeaningfulRemovedDelta();
    ok &= testCleanupFinishSubtractChainStaysStable();
    ok &= testCleanupBlockSizeInvariance();
    ok &= testFullChainBlockSizeInvariance();
    ok &= testCombinedChainKeepsSilenceSilent();
    return ok ? 0 : 1;
}
