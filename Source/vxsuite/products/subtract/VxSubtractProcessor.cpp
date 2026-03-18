#include "VxSubtractProcessor.h"

#include <cmath>
#include <cstring>

namespace {

constexpr std::string_view kProductName = "Subtract";
constexpr std::string_view kShortTag = "SUB";
constexpr std::string_view kSubtractParam = "subtract";
constexpr std::string_view kProtectParam = "protect";
constexpr std::string_view kModeParam = "mode";
constexpr std::string_view kListenParam = "listen";
constexpr std::string_view kLearnParam = "learn";
constexpr int kProfileFftSize = 1 << 11;
constexpr int kProfileHopSize = kProfileFftSize / 4;

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
    identity.theme.accentRgb = { 0.90f, 0.28f, 0.18f };
    identity.theme.accent2Rgb = { 0.13f, 0.07f, 0.06f };
    identity.theme.backgroundRgb = { 0.06f, 0.04f, 0.04f };
    identity.theme.panelRgb = { 0.10f, 0.08f, 0.08f };
    identity.theme.textRgb = { 0.97f, 0.92f, 0.89f };
    return identity;
}

juce::String VXSubtractAudioProcessor::getStatusText() const {
    if (isListenEnabled())
        return "Listen - removed profile only";

    const auto confidenceSummary = [&](const int confidencePct) -> juce::String {
        if (confidencePct < 40)
            return juce::String(confidencePct) + "% profile confidence - low, try a cleaner noise-only capture";
        if (confidencePct < 75)
            return juce::String(confidencePct) + "% profile confidence - usable capture quality";
        return juce::String(confidencePct) + "% profile confidence - strong clean capture";
    };

    if (isLearnActive()) {
        const int progressPct = juce::roundToInt(100.0f * getLearnProgress());
        const int confidencePct = juce::roundToInt(100.0f * getLearnConfidence());
        return "Learning noise only - press Learn again to stop and lock this profile ("
             + juce::String(progressPct) + "%, "
             + confidenceSummary(confidencePct) + ")";
    }

    if (isLearnReady()) {
        const int confidencePct = juce::roundToInt(100.0f * getLearnConfidence());
        return "Profile learned - turn Subtract to remove it ("
             + confidenceSummary(confidencePct) + ")";
    }

    const bool isVoice = vxsuite::readMode(parameters, productIdentity) == vxsuite::Mode::vocal;
    return isVoice ? "Vocal - learn room noise, then remove it while protecting speech"
                   : "General - learn a profile, then remove it more aggressively";
}

void VXSubtractAudioProcessor::prepareSuite(const double sampleRate, const int samplesPerBlock) {
    currentSampleRateHz = sampleRate > 1000.0 ? sampleRate : 48000.0;
    // Persist any live learned profile before prepare() wipes it
    {
        std::vector<float> liveProfile;
        float liveConfidence = 0.0f;
        if (subtractDsp.getLearnedProfileData(liveProfile, liveConfidence)) {
            savedLearnProfile = std::move(liveProfile);
            savedLearnConfidence = liveConfidence;
        }
    }
    stageChain.prepare(currentSampleRateHz, samplesPerBlock);
    // prepare() clears the learned profile — restore it if we have one saved
    if (!savedLearnProfile.empty()
        && std::abs(savedLearnProfileSampleRate - currentSampleRateHz) <= 1.0
        && savedLearnProfileFftSize == kProfileFftSize
        && savedLearnProfileHopSize == kProfileHopSize) {
        subtractDsp.restoreLearnedProfile(savedLearnProfile, savedLearnConfidence);
        learnReady.store(true, std::memory_order_relaxed);
        learnConfidence.store(savedLearnConfidence, std::memory_order_relaxed);
    }
    setReportedLatencySamples(stageChain.totalLatencySamples());
    resetSuite();
}

void VXSubtractAudioProcessor::resetSuite() {
    subtractDsp.resetStreamingState();
    smoothedSubtract = 0.0f;
    smoothedProtect = 0.5f;
    controlsPrimed = false;
    learnToggleLatched = vxsuite::readBool(parameters, productIdentity.learnParamId, false);
    subtractDsp.setLearning(learnToggleLatched);
    learnActive.store(learnToggleLatched, std::memory_order_relaxed);
    // learnReady / learnProgress / learnConfidence / learnObservedSeconds are
    // intentionally preserved — the learned noise profile survives playback stops.
}

void VXSubtractAudioProcessor::processProduct(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    if (numSamples <= 0 || numChannels <= 0)
        return;

    const float subtractTarget = vxsuite::readNormalized(parameters, productIdentity.primaryParamId, 0.0f);
    const float protectTarget = vxsuite::readNormalized(parameters, productIdentity.secondaryParamId, 0.5f);

    if (!controlsPrimed) {
        smoothedSubtract = subtractTarget;
        smoothedProtect = protectTarget;
        controlsPrimed = true;
    } else {
        smoothedSubtract = vxsuite::smoothBlockValue(smoothedSubtract, subtractTarget, currentSampleRateHz, numSamples, 0.045f);
        smoothedProtect = vxsuite::smoothBlockValue(smoothedProtect, protectTarget, currentSampleRateHz, numSamples, 0.080f);
    }

    const bool isVoice = vxsuite::readMode(parameters, productIdentity) == vxsuite::Mode::vocal;
    const bool learnRequested = vxsuite::readBool(parameters, productIdentity.learnParamId, false);
    const bool learnStartEdge = learnRequested && !learnToggleLatched;
    const bool learnStopEdge = !learnRequested && learnToggleLatched;
    if (learnStartEdge) {
        subtractDsp.setLearning(true);
    }
    if (learnStopEdge && subtractDsp.isLearning()) {
        subtractDsp.setLearning(false);
    }

    const bool learnedReady = subtractDsp.hasLearnedProfile();
    const bool learningActiveNow = subtractDsp.isLearning();
    const float subtractStrength = vxsuite::clamp01(smoothedSubtract);
    const float protectStrength = vxsuite::clamp01(smoothedProtect);

    vxsuite::ProcessOptions options {};
    options.isVoiceMode = isVoice;
    options.sourceProtect = isVoice ? vxsuite::clamp01(0.55f + 0.45f * protectStrength)
                                    : vxsuite::clamp01(0.12f + 0.38f * protectStrength);
    options.guardStrictness = isVoice ? vxsuite::clamp01(0.75f * protectStrength)
                                      : vxsuite::clamp01(0.30f * protectStrength);
    options.speechFocus = isVoice ? vxsuite::clamp01(0.72f + 0.28f * protectStrength) : 0.12f;
    options.learningActive = learningActiveNow;
    options.subtract = isVoice ? (4.15f * subtractStrength)
                               : (5.00f * subtractStrength);
    options.sensitivity = isVoice ? (0.85f + 0.50f * (1.0f - protectStrength))
                                  : (1.10f + 0.55f * (1.0f - protectStrength));
    options.labRawMode = false;

    // Learn is explicit now: while capturing a new profile do not apply any
    // blind denoise or profile subtraction, but keep the old profile in memory
    // until the new one is explicitly finalized.
    if (learningActiveNow)
        options.subtract = 0.0f;

    const float blindAmount = learningActiveNow ? 0.0f
                                                : (learnedReady ? 0.0f
                                                                : vxsuite::clamp01((isVoice ? 0.38f : 0.28f)
                                                                          * subtractStrength));

    if (!learningActiveNow && subtractStrength <= 1.0e-4f) {
        ensureLatencyAlignedListenDry(numSamples);
        const auto& alignedDry = getLatencyAlignedListenDryBuffer();
        const int channels = std::min(buffer.getNumChannels(), alignedDry.getNumChannels());
        for (int ch = 0; ch < channels; ++ch)
            buffer.copyFrom(ch, 0, alignedDry, ch, 0, numSamples);
        return;
    }

    stageChain.processInPlace(buffer, { blindAmount }, options);

    learnProgress.store(subtractDsp.getLearnProgress(), std::memory_order_relaxed);
    learnConfidence.store(subtractDsp.getLearnConfidence(), std::memory_order_relaxed);
    learnObservedSeconds.store(subtractDsp.getLearnObservedSeconds(), std::memory_order_relaxed);
    learnActive.store(subtractDsp.isLearning(), std::memory_order_relaxed);
    learnReady.store(subtractDsp.hasLearnedProfile(), std::memory_order_relaxed);
    learnToggleLatched = learnRequested;

    ensureLatencyAlignedListenDry(numSamples);
}

void VXSubtractAudioProcessor::getStateInformation(juce::MemoryBlock& destData) {
    auto xml = parameters.copyState().createXml();
    if (!xml)
        return;

    std::vector<float> profile;
    float confidence = 0.0f;
    if (subtractDsp.getLearnedProfileData(profile, confidence)) {
        auto* el = xml->createNewChildElement("LearnedProfile");
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
    savedLearnConfidence = 0.0f;
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
                // Apply immediately if already prepared
                if (std::abs(savedLearnProfileSampleRate - currentSampleRateHz) <= 1.0
                    && savedLearnProfileFftSize == kProfileFftSize
                    && savedLearnProfileHopSize == kProfileHopSize) {
                    subtractDsp.restoreLearnedProfile(savedLearnProfile, savedLearnConfidence);
                    learnReady.store(subtractDsp.hasLearnedProfile(), std::memory_order_relaxed);
                    learnConfidence.store(savedLearnConfidence, std::memory_order_relaxed);
                } else {
                    savedLearnProfile.clear();
                    savedLearnConfidence = 0.0f;
                    learnReady.store(false, std::memory_order_relaxed);
                    learnConfidence.store(0.0f, std::memory_order_relaxed);
                }
            }
        }
    } else {
        subtractDsp.clearLearnedProfile();
        learnReady.store(false, std::memory_order_relaxed);
        learnConfidence.store(0.0f, std::memory_order_relaxed);
        learnProgress.store(0.0f, std::memory_order_relaxed);
        learnObservedSeconds.store(0.0f, std::memory_order_relaxed);
    }
}

#if !defined(VXSUITE_DISABLE_PLUGIN_ENTRYPOINT)
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new VXSubtractAudioProcessor();
}
#endif
