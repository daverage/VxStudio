#include "VxSubtractProcessor.h"
#include "../../framework/VxStudioHelpContent.h"
#include "VxStudioVersions.h"

#include <cmath>
#include <cstring>

namespace {

constexpr std::string_view kProductName = "VX Studio Subtract";
constexpr std::string_view kShortTag = "SUB";
constexpr std::string_view kSubtractParam = "subtract";
constexpr std::string_view kProtectParam = "protect";
constexpr std::string_view kModeParam = "mode";
constexpr std::string_view kListenParam = "listen";
constexpr std::string_view kLearnParam = "learn";
constexpr int kProfileFftSize = 1 << 11;
constexpr int kProfileHopSize = kProfileFftSize / 4;
constexpr float kMinimumStereoProfileConfidence = 0.12f;

} // namespace

VXSubtractAudioProcessor::VXSubtractAudioProcessor()
    : ProcessorBase(makeIdentity()) {}

vxsuite::ProductIdentity VXSubtractAudioProcessor::makeIdentity() {
    vxsuite::ProductIdentity identity {};
    identity.productName = kProductName;
    identity.shortTag = kShortTag;
    identity.primaryParamId = kSubtractParam;
    identity.secondaryParamId = kProtectParam;
    identity.modeParamId = kModeParam;
    identity.listenParamId = kListenParam;
    identity.learnParamId = kLearnParam;
    identity.learnButtonLabel = "Learn";
    identity.defaultMode = vxsuite::Mode::vocal;
    identity.primaryLabel = "Subtract";
    identity.secondaryLabel = "Protect";
    identity.primaryHint = "Smarter spectral subtraction than a raw profile notch, with adaptive tracking underneath.";
    identity.secondaryHint = "Keep consonants, harmonics, and detail when subtraction gets aggressive.";
    identity.dspVersion = vxsuite::versions::plugins::subtract;
    identity.helpTitle = vxsuite::help::subtract.title;
    identity.helpHtml = vxsuite::help::subtract.html;
    identity.readmeSection = vxsuite::help::subtract.readmeSection;
    identity.theme.accentRgb = { 0.90f, 0.28f, 0.18f };
    identity.theme.accent2Rgb = { 0.13f, 0.07f, 0.06f };
    identity.theme.backgroundRgb = { 0.06f, 0.04f, 0.04f };
    identity.theme.panelRgb = { 0.10f, 0.08f, 0.08f };
    identity.theme.textRgb = { 0.97f, 0.92f, 0.89f };
    return identity;
}

float VXSubtractAudioProcessor::getActivityLight(int) const noexcept {
    return subtractDisplayLevel.load(std::memory_order_relaxed);
}

juce::String VXSubtractAudioProcessor::getStatusText() const {
    if (isListenEnabled())
        return "Listen: hearing what was removed  -  lower Subtract or disable Listen to return to normal";

    if (isLearnActive()) {
        const int coveragePct   = juce::roundToInt(100.0f * getLearnProgress());
        const int qualityPct    = juce::roundToInt(100.0f * getLearnConfidence());
        const juce::String qual = qualityPct < 40 ? "low quality  -  play noise with no signal"
                                : qualityPct < 70 ? "moderate quality"
                                                  : "good quality";
        return "Capturing: play noise only, no signal. Coverage "
             + juce::String(coveragePct) + "% | Quality " + juce::String(qualityPct) + "% ("
             + qual + ")  -  press Learn again to lock";
    }

    if (isLearnReady()) {
        const int confidencePct = juce::roundToInt(100.0f * getLearnConfidence());
        const juce::String conf = confidencePct < 40 ? "low  -  recapture for better results"
                                : confidencePct < 70 ? "usable"
                                                     : "strong";
        const int64_t learnMs = learnCompletedTimeMs.load(std::memory_order_relaxed);
        const float profileAgeSeconds = learnMs > 0
            ? static_cast<float>((juce::Time::currentTimeMillis() - learnMs) / 1000LL)
            : 0.0f;
        if (profileAgeSeconds > 1200.0f)
            return "Profile ready  -  note: noise floor may have changed, consider re-learning";
        return "Profile ready (" + juce::String(confidencePct) + "% confidence, " + conf
             + ")  -  raise Subtract to remove the captured noise";
    }

    const bool isVoice = vxsuite::readMode(parameters, productIdentity) == vxsuite::Mode::vocal;
    return isVoice
        ? "Vocal: press Learn and play only the room noise, then press Learn again to subtract it"
        : "General: press Learn and play only the noise, then press Learn again to subtract it";
}

float VXSubtractAudioProcessor::getProfileTrust() const noexcept {
    // Use conservative static threshold (original 0.12f) for profile trust reporting
    // Since this is a const method, we can't access current signal quality snapshot
    const float staticThreshold = kMinimumStereoProfileConfidence;
    const bool leftReady = subtractDspLeft.hasLearnedProfile()
        && subtractDspLeft.getLearnConfidence() >= staticThreshold;
    const bool rightReady = subtractDspRight.hasLearnedProfile()
        && subtractDspRight.getLearnConfidence() >= staticThreshold;
    if (leftReady || rightReady) {
        const float leftTrust = leftReady ? subtractDspLeft.getLearnedProfileTrust() : 0.0f;
        const float rightTrust = rightReady ? subtractDspRight.getLearnedProfileTrust() : 0.0f;
        const float count = static_cast<float>((leftReady ? 1 : 0) + (rightReady ? 1 : 0));
        return count > 0.0f ? (leftTrust + rightTrust) / count : 1.0f;
    }
    return subtractDspMono.hasLearnedProfile() ? subtractDspMono.getLearnedProfileTrust() : 1.0f;
}

void VXSubtractAudioProcessor::prepareSuite(const double sampleRate, const int samplesPerBlock) {
    currentSampleRateHz = sampleRate > 1000.0 ? sampleRate : 48000.0;
    {
        std::vector<float> liveProfile;
        float liveConfidence = 0.0f;
        if (subtractDspMono.getLearnedProfileData(liveProfile, liveConfidence)) {
            savedLearnProfile = std::move(liveProfile);
            savedLearnConfidence = liveConfidence;
        }
        for (int ch = 0; ch < 2; ++ch) {
            auto& dsp = (ch == 0) ? subtractDspLeft : subtractDspRight;
            if (dsp.getLearnedProfileData(savedStereoLearnProfiles[static_cast<size_t>(ch)],
                                          savedStereoLearnConfidence[static_cast<size_t>(ch)])) {
                continue;
            }
            savedStereoLearnProfiles[static_cast<size_t>(ch)].clear();
            savedStereoLearnConfidence[static_cast<size_t>(ch)] = 0.0f;
        }
    }

    subtractDspMono.prepare(currentSampleRateHz, samplesPerBlock);
    subtractDspLeft.prepare(currentSampleRateHz, samplesPerBlock);
    subtractDspRight.prepare(currentSampleRateHz, samplesPerBlock);
    const int maxBlockSize = std::max(samplesPerBlock, 4096);
    leftScratch.setSize(1, maxBlockSize, false, false, true);
    rightScratch.setSize(1, maxBlockSize, false, false, true);
    applySavedProfiles();
    setReportedLatencySamples(subtractDspMono.getLatencySamples());
    resetSuite();
}

void VXSubtractAudioProcessor::resetSuite() {
    subtractDspMono.resetStreamingState();
    subtractDspLeft.resetStreamingState();
    subtractDspRight.resetStreamingState();
    controls.reset(vxsuite::readNormalized(parameters, productIdentity.primaryParamId, productIdentity.primaryDefaultValue),
                   vxsuite::readNormalized(parameters, productIdentity.secondaryParamId, productIdentity.secondaryDefaultValue));
    activeTailSamplesRemaining = 0;
    subtractDisplayLevel.store(0.0f, std::memory_order_relaxed);
    learnToggleLatched = vxsuite::readBool(parameters, productIdentity.learnParamId, false);
    controlsNeedRelatchAfterLearn = false;
    subtractDspMono.setLearning(learnToggleLatched);
    subtractDspLeft.setLearning(learnToggleLatched);
    subtractDspRight.setLearning(learnToggleLatched);
    learnActive.store(learnToggleLatched, std::memory_order_relaxed);
    // learnReady / learnProgress / learnConfidence / learnObservedSeconds are
    // intentionally preserved  -  the learned noise profile survives playback stops.
    // Note: learned profile is static; in long sessions with changing noise floors (e.g., HVAC cycling),
    // users should re-learn to adapt. This is a known limitation; continuous re-learning is future work.
}

void VXSubtractAudioProcessor::processProduct(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    if (numSamples <= 0 || numChannels <= 0)
        return;

    const float subtractTarget = vxsuite::readNormalized(parameters, productIdentity.primaryParamId, 0.0f);
    const float protectTarget = vxsuite::readNormalized(parameters, productIdentity.secondaryParamId, 0.5f);
    if (controlsNeedRelatchAfterLearn) {
        controls.reset(subtractTarget, protectTarget);
        controlsNeedRelatchAfterLearn = false;
    }

    const auto [smoothedSubtract, smoothedProtect] = controls.process(
        subtractTarget, protectTarget,
        currentSampleRateHz, numSamples,
        0.045f, 0.080f);

    const bool isVoice = vxsuite::readMode(parameters, productIdentity) == vxsuite::Mode::vocal;
    const auto voiceContext = getVoiceContextSnapshot();
    const float vocalPriority = isVoice
        ? vxsuite::clamp01(0.40f * voiceContext.vocalDominance
                         + 0.25f * voiceContext.intelligibility
                         + 0.20f * voiceContext.phraseActivity
                         + 0.15f * voiceContext.speechPresence)
        : 0.0f;
    const bool learnRequested = vxsuite::readBool(parameters, productIdentity.learnParamId, false);
    const bool learnStartEdge = learnRequested && !learnToggleLatched;
    const bool learnStopEdge = !learnRequested && learnToggleLatched;
    if (learnStartEdge) {
        subtractDspMono.setLearning(true);
        subtractDspLeft.setLearning(true);
        subtractDspRight.setLearning(true);
    }
    if (learnStopEdge) {
        subtractDspMono.setLearning(false);
        subtractDspLeft.setLearning(false);
        subtractDspRight.setLearning(false);
    }

    const auto finalizeLearnStopTransition = [&] {
        if (!learnStopEdge)
            return;

        // Finalize explicitly in case processInPlace was bypassed this block
        // (e.g. subtract knob at 0.0 on the stop block), so learnedProfileReady is set.
        subtractDspMono.finalizeLearnedProfile();
        subtractDspLeft.finalizeLearnedProfile();
        subtractDspRight.finalizeLearnedProfile();

        // Reset streaming state while preserving noisePowFrozen (the learned profile).
        subtractDspMono.resetStreamingState();
        subtractDspLeft.resetStreamingState();
        subtractDspRight.resetStreamingState();

        resetProcessCoordinator();
        activeTailSamplesRemaining = 0;
        controls.reset(subtractTarget, protectTarget);
        controlsNeedRelatchAfterLearn = true;
        resetOutputSafetyTrimmer();
        this->voiceAnalysis.reset();
        this->voiceContext.reset();
        this->signalQuality.reset();
    };

    const auto publishLearnStateAndLatch = [&] {
        updateLearnTelemetry(numChannels);
        learnToggleLatched = learnRequested;
    };
    const auto publishMeasuredRemoval = [&](const juce::AudioBuffer<float>& dry) {
        const int channels = std::min(buffer.getNumChannels(), dry.getNumChannels());
        if (channels <= 0 || numSamples <= 0) {
            subtractDisplayLevel.store(0.0f, std::memory_order_relaxed);
            return;
        }

        double deltaEnergy = 0.0;
        double dryEnergy = 0.0;
        int count = 0;
        for (int ch = 0; ch < channels; ++ch) {
            const auto* out = buffer.getReadPointer(ch);
            const auto* in = dry.getReadPointer(ch);
            for (int i = 0; i < numSamples; ++i) {
                const double d = static_cast<double>(in[i]) - out[i];
                deltaEnergy += d * d;
                dryEnergy += static_cast<double>(in[i]) * in[i];
                ++count;
            }
        }

        const double deltaRms = count > 0 ? std::sqrt(deltaEnergy / static_cast<double>(count)) : 0.0;
        const double dryRms = count > 0 ? std::sqrt(dryEnergy / static_cast<double>(count)) : 0.0;
        const float relative = static_cast<float>(deltaRms / std::max(1.0e-5, dryRms));
        const float absolute = static_cast<float>(deltaRms * 18.0);
        const float displayed = vxsuite::clamp01(std::max(absolute, relative * 1.8f));
        subtractDisplayLevel.store(displayed < 0.01f ? 0.0f : displayed, std::memory_order_relaxed);
    };

    const bool stereo = numChannels >= 2;
    // Adaptive stereo confidence threshold based on mono/stereo content
    // Near-mono (monoScore > 0.80) → stricter threshold; stereo-imaged (monoScore < 0.40) → permissive
    const auto signalQuality = getSignalQualitySnapshot();
    const float adaptiveStereoThreshold = juce::jmap(signalQuality.monoScore,
                                                     0.40f, 0.80f,
                                                     0.08f, 0.25f);
    const bool leftReady = subtractDspLeft.hasLearnedProfile()
        && subtractDspLeft.getLearnConfidence() >= adaptiveStereoThreshold;
    const bool rightReady = subtractDspRight.hasLearnedProfile()
        && subtractDspRight.getLearnConfidence() >= adaptiveStereoThreshold;
    const bool monoReady = subtractDspMono.hasLearnedProfile();
    const bool learnedReady = stereo ? (leftReady || rightReady) : monoReady;
    const bool learningActiveNow = stereo ? subtractDspLeft.isLearning() : subtractDspMono.isLearning();
    const float subtractStrength = vxsuite::clamp01(smoothedSubtract);
    const float protectStrength = vxsuite::clamp01(smoothedProtect);

    vxsuite::ProcessOptions options {};
    options.isVoiceMode = isVoice;
    options.sourceProtect = isVoice ? vxsuite::clamp01(0.64f + 0.36f * protectStrength + 0.18f * vocalPriority)
                                    : vxsuite::clamp01(0.12f + 0.38f * protectStrength);
    options.guardStrictness = isVoice ? vxsuite::clamp01(0.82f * protectStrength + 0.16f * vocalPriority)
                                      : vxsuite::clamp01(0.72f * protectStrength);
    options.speechFocus = isVoice ? vxsuite::clamp01(0.78f + 0.22f * protectStrength + 0.12f * vocalPriority) : 0.12f;
    options.learningActive = learningActiveNow;
    options.subtract = isVoice ? (6.00f * subtractStrength * (1.0f - 0.06f * vocalPriority))
                               : (6.00f * subtractStrength);
    options.sensitivity = isVoice ? ((0.78f + 0.42f * (1.0f - protectStrength)) * (1.0f - 0.08f * vocalPriority))
                                  : (1.10f + 0.55f * (1.0f - protectStrength));
    options.labRawMode = false;

    // Learn is explicit now: while capturing a new profile do not apply any
    // blind denoise or profile subtraction, but keep the old profile in memory
    // until the new one is explicitly finalized.
    if (learningActiveNow)
        options.subtract = 0.0f;

    const float blindBase = isVoice
        ? juce::jmap(protectStrength, 0.34f, 0.04f)
        : juce::jmap(protectStrength, 0.56f, 0.24f);
    const float blindAmount = learningActiveNow ? 0.0f
                                                : (learnedReady ? 0.0f
                                                                : vxsuite::clamp01(blindBase * subtractStrength));
    subtractDisplayLevel.store(0.0f, std::memory_order_relaxed);
    if (learningActiveNow) {
        const auto channelHasLearnSignal = [&](const int channel) {
            if (channel < 0 || channel >= buffer.getNumChannels())
                return false;
            const auto* data = buffer.getReadPointer(channel);
            float peak = 0.0f;
            for (int i = 0; i < numSamples; ++i)
                peak = std::max(peak, std::abs(data[i]));
            return peak > 1.0e-5f;
        };

        if (stereo) {
            if (leftScratch.getNumSamples() != numSamples || leftScratch.getNumChannels() != 1)
                leftScratch.setSize(1, numSamples, false, false, true);
            if (rightScratch.getNumSamples() != numSamples || rightScratch.getNumChannels() != 1)
                rightScratch.setSize(1, numSamples, false, false, true);
            leftScratch.copyFrom(0, 0, buffer, 0, 0, numSamples);
            rightScratch.copyFrom(0, 0, buffer, 1, 0, numSamples);
            if (channelHasLearnSignal(0))
                subtractDspLeft.processInPlace(leftScratch, 0.0f, options);
            if (channelHasLearnSignal(1))
                subtractDspRight.processInPlace(rightScratch, 0.0f, options);
        } else {
            if (leftScratch.getNumSamples() != numSamples || leftScratch.getNumChannels() != 1)
                leftScratch.setSize(1, numSamples, false, false, true);
            leftScratch.copyFrom(0, 0, buffer, 0, 0, numSamples);
            if (channelHasLearnSignal(0))
                subtractDspMono.processInPlace(buffer, 0.0f, options);
            buffer.copyFrom(0, 0, leftScratch, 0, 0, numSamples);
        }
        publishLearnStateAndLatch();
        return;
    }

    if (!learningActiveNow && subtractStrength <= 1.0e-4f) {
        ensureLatencyAlignedListenDry(numSamples);
        const auto& alignedDry = getLatencyAlignedListenDryBuffer();
        // Keep STFT warm — cold OLA on first active frame creates a pop.
        if (stereo) {
            if (leftScratch.getNumSamples() < numSamples || leftScratch.getNumChannels() != 1)
                leftScratch.setSize(1, numSamples, false, false, true);
            if (rightScratch.getNumSamples() < numSamples || rightScratch.getNumChannels() != 1)
                rightScratch.setSize(1, numSamples, false, false, true);
            leftScratch.copyFrom(0, 0, buffer, 0, 0, numSamples);
            rightScratch.copyFrom(0, 0, buffer, 1, 0, numSamples);
            subtractDspLeft.processInPlace(leftScratch, 0.0f, options);
            subtractDspRight.processInPlace(rightScratch, 0.0f, options);
        } else {
            subtractDspMono.processInPlace(buffer, 0.0f, options);
        }
        const int channels = std::min(buffer.getNumChannels(), alignedDry.getNumChannels());
        for (int ch = 0; ch < channels; ++ch)
            buffer.copyFrom(ch, 0, alignedDry, ch, 0, numSamples);
        publishMeasuredRemoval(alignedDry);
        finalizeLearnStopTransition();
        publishLearnStateAndLatch();
        return;
    }

    ensureLatencyAlignedListenDry(numSamples);
    const auto& alignedDry = getLatencyAlignedListenDryBuffer();
    double liveInputRmsSq = 0.0;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
        const auto* data = buffer.getReadPointer(ch);
        for (int i = 0; i < numSamples; ++i)
            liveInputRmsSq += static_cast<double>(data[i]) * data[i];
    }
    const int liveInputCount = buffer.getNumChannels() * numSamples;
    const float liveInputRms = liveInputCount > 0
        ? static_cast<float>(std::sqrt(liveInputRmsSq / static_cast<double>(liveInputCount)))
        : 0.0f;
    if (liveInputRms > 1.0e-5f)
        activeTailSamplesRemaining = std::max(activeTailSamplesRemaining, getLatencySamples() + numSamples);
    else if (!learnStopEdge && activeTailSamplesRemaining <= 0) {
        // Keep STFT warm — same pattern as zero-subtract path.
        if (stereo) {
            if (leftScratch.getNumSamples() < numSamples || leftScratch.getNumChannels() != 1)
                leftScratch.setSize(1, numSamples, false, false, true);
            if (rightScratch.getNumSamples() < numSamples || rightScratch.getNumChannels() != 1)
                rightScratch.setSize(1, numSamples, false, false, true);
            leftScratch.copyFrom(0, 0, buffer, 0, 0, numSamples);
            rightScratch.copyFrom(0, 0, buffer, 1, 0, numSamples);
            subtractDspLeft.processInPlace(leftScratch, 0.0f, options);
            subtractDspRight.processInPlace(rightScratch, 0.0f, options);
        } else {
            subtractDspMono.processInPlace(buffer, 0.0f, options);
        }
        buffer.clear();
        publishMeasuredRemoval(alignedDry);
        finalizeLearnStopTransition();
        publishLearnStateAndLatch();
        return;
    }

    if (stereo) {
        if (leftScratch.getNumSamples() != numSamples || leftScratch.getNumChannels() != 1)
            leftScratch.setSize(1, numSamples, false, false, true);
        if (rightScratch.getNumSamples() != numSamples || rightScratch.getNumChannels() != 1)
            rightScratch.setSize(1, numSamples, false, false, true);
        leftScratch.copyFrom(0, 0, buffer, 0, 0, numSamples);
        rightScratch.copyFrom(0, 0, buffer, 1, 0, numSamples);
        auto leftOptions = options;
        auto rightOptions = options;
        const float leftProcessAmount = learningActiveNow ? 0.0f
            : (leftReady ? subtractStrength : ((!rightReady) ? blindAmount : 0.0f));
        const float rightProcessAmount = learningActiveNow ? 0.0f
            : (rightReady ? subtractStrength : ((!leftReady) ? blindAmount : 0.0f));
        if (!leftReady)
            leftOptions.subtract = 0.0f;
        if (!rightReady)
            rightOptions.subtract = 0.0f;
        if (learningActiveNow || leftReady || leftProcessAmount > 1.0e-5f) {
            subtractDspLeft.processInPlace(leftScratch, leftProcessAmount, leftOptions);
            buffer.copyFrom(0, 0, leftScratch, 0, 0, numSamples);
        } else {
            subtractDspLeft.processInPlace(leftScratch, 0.0f, leftOptions);
            buffer.copyFrom(0, 0, alignedDry, 0, 0, numSamples);
        }
        if (learningActiveNow || rightReady || rightProcessAmount > 1.0e-5f) {
            subtractDspRight.processInPlace(rightScratch, rightProcessAmount, rightOptions);
            buffer.copyFrom(1, 0, rightScratch, 0, 0, numSamples);
        } else {
            subtractDspRight.processInPlace(rightScratch, 0.0f, rightOptions);
            buffer.copyFrom(1, 0, alignedDry, 1, 0, numSamples);
        }
    } else {
        const float monoProcessAmount = learningActiveNow ? 0.0f
            : (learnedReady ? subtractStrength : blindAmount);
        subtractDspMono.processInPlace(buffer, monoProcessAmount, options);
    }
    if (!learnedReady) {
        const float blindWet = isVoice
            ? juce::jlimit(0.08f, 1.0f, 1.0f - 0.92f * protectStrength)
            : juce::jlimit(0.28f, 1.0f, 1.0f - 0.62f * protectStrength);
        const int channels = std::min(buffer.getNumChannels(), alignedDry.getNumChannels());
        for (int ch = 0; ch < channels; ++ch) {
            auto* out = buffer.getWritePointer(ch);
            const auto* dry = alignedDry.getReadPointer(ch);
            for (int i = 0; i < numSamples; ++i)
                out[i] = dry[i] + (out[i] - dry[i]) * blindWet;
        }
    }

    if (liveInputRms <= 1.0e-5f)
        activeTailSamplesRemaining = std::max(0, activeTailSamplesRemaining - numSamples);

    publishMeasuredRemoval(alignedDry);
    finalizeLearnStopTransition();
    publishLearnStateAndLatch();
}

void VXSubtractAudioProcessor::getStateInformation(juce::MemoryBlock& destData) {
    auto xml = parameters.copyState().createXml();
    if (!xml)
        return;

    std::vector<float> profile;
    float confidence = 0.0f;
    if (subtractDspMono.getLearnedProfileData(profile, confidence)) {
        auto* el = xml->createNewChildElement("LearnedProfile");
        el->setAttribute("confidence", static_cast<double>(confidence));
        el->setAttribute("sampleRate", currentSampleRateHz);
        el->setAttribute("fftSize", kProfileFftSize);
        el->setAttribute("hopSize", kProfileHopSize);
        juce::MemoryBlock blob(profile.data(), profile.size() * sizeof(float));
        el->setAttribute("data", blob.toBase64Encoding());
    }

    for (int ch = 0; ch < 2; ++ch) {
        auto& dsp = (ch == 0) ? subtractDspLeft : subtractDspRight;
        if (!dsp.getLearnedProfileData(profile, confidence))
            continue;
        auto* el = xml->createNewChildElement(ch == 0 ? "LearnedProfileLeft" : "LearnedProfileRight");
        el->setAttribute("confidence", static_cast<double>(confidence));
        el->setAttribute("sampleRate", currentSampleRateHz);
        el->setAttribute("fftSize", kProfileFftSize);
        el->setAttribute("hopSize", kProfileHopSize);
        juce::MemoryBlock blob(profile.data(), profile.size() * sizeof(float));
        el->setAttribute("data", blob.toBase64Encoding());
    }

    copyXmlToBinary(*xml, destData);
}

void VXSubtractAudioProcessor::setStateInformation(const void* data, const int sizeInBytes) {
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (!xml)
        return;

    if (xml->hasTagName(parameters.state.getType()))
        parameters.replaceState(juce::ValueTree::fromXml(*xml));

    savedLearnProfile.clear();
    for (auto& profile : savedStereoLearnProfiles)
        profile.clear();
    savedLearnConfidence = 0.0f;
    savedStereoLearnConfidence = { 0.0f, 0.0f };
    savedLearnProfileSampleRate = 0.0;
    savedLearnProfileFftSize = 0;
    savedLearnProfileHopSize = 0;

    if (auto* el = xml->getChildByName("LearnedProfile")) {
        const float confidence = static_cast<float>(el->getDoubleAttribute("confidence", 0.0));
        savedLearnProfileSampleRate = el->getDoubleAttribute("sampleRate", 0.0);
        savedLearnProfileFftSize = el->getIntAttribute("fftSize", 0);
        savedLearnProfileHopSize = el->getIntAttribute("hopSize", 0);
        juce::MemoryBlock blob;
        if (blob.fromBase64Encoding(el->getStringAttribute("data"))) {
            const size_t count = blob.getSize() / sizeof(float);
            if (count > 0) {
                savedLearnProfile.resize(count);
                std::memcpy(savedLearnProfile.data(), blob.getData(), blob.getSize());
                savedLearnConfidence = confidence;
                applySavedProfiles();
            }
        }
    }

    auto restoreStereoProfile = [&](const juce::String& tag, const int channel) {
        if (auto* el = xml->getChildByName(tag)) {
            const float confidence = static_cast<float>(el->getDoubleAttribute("confidence", 0.0));
            if (savedLearnProfileSampleRate <= 0.0) {
                savedLearnProfileSampleRate = el->getDoubleAttribute("sampleRate", 0.0);
                savedLearnProfileFftSize = el->getIntAttribute("fftSize", 0);
                savedLearnProfileHopSize = el->getIntAttribute("hopSize", 0);
            }
            juce::MemoryBlock blob;
            if (blob.fromBase64Encoding(el->getStringAttribute("data"))) {
                const size_t count = blob.getSize() / sizeof(float);
                if (count > 0) {
                    auto& profile = savedStereoLearnProfiles[static_cast<size_t>(channel)];
                    profile.resize(count);
                    std::memcpy(profile.data(), blob.getData(), blob.getSize());
                    savedStereoLearnConfidence[static_cast<size_t>(channel)] = confidence;
                }
            }
        }
    };
    restoreStereoProfile("LearnedProfileLeft", 0);
    restoreStereoProfile("LearnedProfileRight", 1);

    if (savedLearnProfile.empty() && savedStereoLearnProfiles[0].empty() && savedStereoLearnProfiles[1].empty()) {
        subtractDspMono.clearLearnedProfile();
        subtractDspLeft.clearLearnedProfile();
        subtractDspRight.clearLearnedProfile();
        learnReady.store(false, std::memory_order_relaxed);
        learnConfidence.store(0.0f, std::memory_order_relaxed);
        learnProgress.store(0.0f, std::memory_order_relaxed);
        learnObservedSeconds.store(0.0f, std::memory_order_relaxed);
        return;
    }

    applySavedProfiles();
}

void VXSubtractAudioProcessor::updateLearnTelemetry(const int numChannels) {
    if (numChannels >= 2) {
        const bool leftReady = subtractDspLeft.hasLearnedProfile()
            && subtractDspLeft.getLearnConfidence() >= kMinimumStereoProfileConfidence;
        const bool rightReady = subtractDspRight.hasLearnedProfile()
            && subtractDspRight.getLearnConfidence() >= kMinimumStereoProfileConfidence;
        learnProgress.store(0.5f * (subtractDspLeft.getLearnProgress() + subtractDspRight.getLearnProgress()),
                            std::memory_order_relaxed);
        learnConfidence.store(0.5f * (subtractDspLeft.getLearnConfidence() + subtractDspRight.getLearnConfidence()),
                              std::memory_order_relaxed);
        learnObservedSeconds.store(0.5f * (subtractDspLeft.getLearnObservedSeconds() + subtractDspRight.getLearnObservedSeconds()),
                                   std::memory_order_relaxed);
        learnActive.store(subtractDspLeft.isLearning() || subtractDspRight.isLearning(), std::memory_order_relaxed);
        const bool nowReady = leftReady || rightReady;
        if (nowReady && !learnReady.load(std::memory_order_relaxed))
            learnCompletedTimeMs.store(juce::Time::currentTimeMillis(), std::memory_order_relaxed);
        learnReady.store(nowReady, std::memory_order_relaxed);
        return;
    }

    learnProgress.store(subtractDspMono.getLearnProgress(), std::memory_order_relaxed);
    learnConfidence.store(subtractDspMono.getLearnConfidence(), std::memory_order_relaxed);
    learnObservedSeconds.store(subtractDspMono.getLearnObservedSeconds(), std::memory_order_relaxed);
    learnActive.store(subtractDspMono.isLearning(), std::memory_order_relaxed);
    const bool nowReadyMono = subtractDspMono.hasLearnedProfile();
    if (nowReadyMono && !learnReady.load(std::memory_order_relaxed))
        learnCompletedTimeMs.store(juce::Time::currentTimeMillis(), std::memory_order_relaxed);
    learnReady.store(nowReadyMono, std::memory_order_relaxed);
}

void VXSubtractAudioProcessor::applySavedProfiles() {
    const bool formatMatches = std::abs(savedLearnProfileSampleRate - currentSampleRateHz) <= 1.0
        && savedLearnProfileFftSize == kProfileFftSize
        && savedLearnProfileHopSize == kProfileHopSize;
    if (!formatMatches)
        return;

    if (!savedLearnProfile.empty()) {
        subtractDspMono.restoreLearnedProfile(savedLearnProfile, savedLearnConfidence);
        if (savedStereoLearnProfiles[0].empty())
            subtractDspLeft.restoreLearnedProfile(savedLearnProfile, savedLearnConfidence);
        if (savedStereoLearnProfiles[1].empty())
            subtractDspRight.restoreLearnedProfile(savedLearnProfile, savedLearnConfidence);
    }
    for (int ch = 0; ch < 2; ++ch) {
        const auto& profile = savedStereoLearnProfiles[static_cast<size_t>(ch)];
        if (profile.empty())
            continue;
        auto& dsp = (ch == 0) ? subtractDspLeft : subtractDspRight;
        dsp.restoreLearnedProfile(profile, savedStereoLearnConfidence[static_cast<size_t>(ch)]);
    }
    updateLearnTelemetry(getTotalNumOutputChannels());
}

void VXSubtractAudioProcessor::renderListenOutput(juce::AudioBuffer<float>& outputBuffer,
                                                   const juce::AudioBuffer<float>& inputBuffer) {
    juce::ignoreUnused(inputBuffer);
    if (subtractDisplayLevel.load(std::memory_order_relaxed) <= 0.0f) {
        outputBuffer.clear();
        return;
    }

    ensureLatencyAlignedListenDry(outputBuffer.getNumSamples());
    const auto& alignedDry = getLatencyAlignedListenDryBuffer();
    const int channels = std::min(outputBuffer.getNumChannels(), alignedDry.getNumChannels());
    const int samples = std::min(outputBuffer.getNumSamples(), alignedDry.getNumSamples());
    for (int ch = 0; ch < channels; ++ch) {
        auto* out = outputBuffer.getWritePointer(ch);
        const auto* dry = alignedDry.getReadPointer(ch);
        for (int i = 0; i < samples; ++i)
            out[i] = dry[i] - out[i];  // delta = alignedDry - wet (removed content)
    }
}

#if !defined(VXSUITE_DISABLE_PLUGIN_ENTRYPOINT) && !defined(VXSTUDIO_DISABLE_PLUGIN_ENTRYPOINT)
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new VXSubtractAudioProcessor();
}
#endif
