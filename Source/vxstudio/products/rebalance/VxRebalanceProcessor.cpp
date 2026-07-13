#include "VxRebalanceProcessor.h"
#include "VxRebalanceEditor.h"

#include "../../framework/VxStudioHelpContent.h"
#include "../../framework/VxStudioBlockSmoothing.h"
#include "../../framework/VxStudioParameters.h"
#include "VxStudioVersions.h"

#include <cmath>

namespace {

#if VXSTUDIO_REBALANCE_AI_VARIANT
constexpr std::string_view kProductName = "VX Studio Rebalance AI";
constexpr std::string_view kShortTag = "RAI";
constexpr std::string_view kStageId = "vx.rebalance.ai";
constexpr std::string_view kHelpTitle = "VxRebalanceAI Help";
constexpr std::string_view kReadmeSection = "VxRebalanceAI";
#else
constexpr std::string_view kProductName = "VX Studio Rebalance";
constexpr std::string_view kShortTag = "RBL";
constexpr std::string_view kStageId = "vx.rebalance";
constexpr std::string_view kHelpTitle = vxsuite::help::rebalance.title;
constexpr std::string_view kReadmeSection = vxsuite::help::rebalance.readmeSection;
#endif
constexpr std::string_view kVocalsParam = "vocals";
constexpr std::string_view kDrumsParam = "drums";
constexpr std::string_view kBassParam = "bass";
constexpr std::string_view kGuitarParam = "guitar";
constexpr std::string_view kOtherParam = "other";
constexpr std::string_view kStrengthParam = "strength";
constexpr std::string_view kRecordingTypeParam = "recordingType";
#if VXSTUDIO_REBALANCE_AI_VARIANT
constexpr std::string_view kAiModeParam = "aiMode";
#endif

constexpr std::array<std::string_view, vxsuite::ProductIdentity::maxControlBankControls> kBankParamIds {
    kVocalsParam, kDrumsParam, kBassParam, kGuitarParam, kOtherParam, kStrengthParam
};

constexpr std::array<std::string_view, vxsuite::ProductIdentity::maxControlBankControls> kBankLabels {
    "Vocals", "Drums", "Bass", "Guitar", "Other", "Strength"
};

constexpr std::array<std::string_view, vxsuite::ProductIdentity::maxControlBankControls> kBankHints {
    "Vocal Presence. 0% = unchanged, -100% = reduced, +100% = enhanced.",
    "Drum Presence. 0% = unchanged, -100% = reduced, +100% = enhanced.",
    "Bass Presence. 0% = unchanged, -100% = reduced, +100% = enhanced.",
    "Guitar Presence. 0% = unchanged, -100% = reduced, +100% = enhanced.",
    "Other Presence. 0% = unchanged, -100% = reduced, +100% = enhanced.",
    "Scale all five source moves together."
};

constexpr std::array<float, vxsuite::ProductIdentity::maxControlBankControls> kBankDefaults {
    0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 1.0f
};

bool isEffectivelyDualMono(const juce::AudioBuffer<float>& buffer, const int numSamples) noexcept {
    if (buffer.getNumChannels() < 2 || numSamples <= 0)
        return false;

    const auto* left = buffer.getReadPointer(0);
    const auto* right = buffer.getReadPointer(1);
    float peak = 0.0f;
    float maxDiff = 0.0f;
    for (int i = 0; i < numSamples; ++i) {
        peak = std::max(peak, std::max(std::abs(left[i]), std::abs(right[i])));
        maxDiff = std::max(maxDiff, std::abs(left[i] - right[i]));
    }
    return maxDiff <= (1.0e-6f + peak * 1.0e-4f);
}

} // namespace

VXRebalanceAudioProcessor::VXRebalanceAudioProcessor()
    : ProcessorBase(makeIdentity(), makeParameterLayout()) {
}

VXRebalanceAudioProcessor::~VXRebalanceAudioProcessor() = default;

juce::AudioProcessorEditor* VXRebalanceAudioProcessor::createEditor() {
    return new VXRebalanceEditor(*this);
}

vxsuite::rebalance::DebugSnapshot VXRebalanceAudioProcessor::getDebugSnapshot() const noexcept {
#if VXSTUDIO_REBALANCE_AI_VARIANT
    return aiDsp.getDebugSnapshot();
#else
    return dsp.getDebugSnapshot();
#endif
}

#if VXSTUDIO_REBALANCE_AI_VARIANT
vxsuite::rebalance::ai::RealtimeStemSplitter::DebugSnapshot
VXRebalanceAudioProcessor::getAiDebugSnapshot() const noexcept {
    return realtimeSplitter.getDebugSnapshot();
}

juce::String VXRebalanceAudioProcessor::getAiStatusText() const {
    return realtimeSplitter.statusText();
}
#endif

vxsuite::ProductIdentity VXRebalanceAudioProcessor::makeIdentity() {
    vxsuite::ProductIdentity id {};
    id.productName = kProductName;
    id.shortTag = kShortTag;
    id.auxSelectorParamId = kRecordingTypeParam;
    id.auxSelectorLabel = "Source";
    id.auxSelectorChoiceLabels = { "Studio", "Live", "Phone / Rough" };
    id.auxSelectorDefaultIndex = 0;
    id.auxSelectorFollowsGeneralMode = false;
    id.controlBankCount = vxsuite::rebalance::kControlCount;
    id.controlBankVertical = true;
    id.compactControlBankLayout = true;
    id.controlBankParamIds = kBankParamIds;
    id.controlBankLabels = kBankLabels;
    id.controlBankHints = kBankHints;
    id.controlBankDefaultValues = kBankDefaults;
    id.stageId   = kStageId;
    id.stageType = vxsuite::StageType::mixed;
    id.dspVersion = vxsuite::versions::plugins::rebalance;
    id.helpTitle = kHelpTitle;
    id.helpHtml = vxsuite::help::rebalance.html;
    id.readmeSection = kReadmeSection;
    id.theme.accentRgb = { 0.92f, 0.52f, 0.18f };
    id.theme.accent2Rgb = { 0.14f, 0.10f, 0.08f };
    id.theme.backgroundRgb = { 0.08f, 0.06f, 0.05f };
    id.theme.panelRgb = { 0.13f, 0.10f, 0.08f };
    id.theme.textRgb = { 0.97f, 0.93f, 0.88f };
    return id;
}

juce::AudioProcessorValueTreeState::ParameterLayout VXRebalanceAudioProcessor::makeParameterLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { kRecordingTypeParam.data(), 1 },
        "Recording Type",
        juce::StringArray { "Studio", "Live", "Phone / Rough" },
        0,
        vxsuite::makeChoiceAttributes("Recording Type")));

#if VXSTUDIO_REBALANCE_AI_VARIANT
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { kAiModeParam.data(), 1 },
        "AI Mode",
        juce::StringArray { "AI Assist", "AI Strong" },
        0,
        vxsuite::makeChoiceAttributes("AI Mode")));
#endif

    for (int i = 0; i < vxsuite::rebalance::kSourceCount; ++i) {
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { kBankParamIds[static_cast<size_t>(i)].data(), 1 },
            vxsuite::toJuceString(kBankLabels[static_cast<size_t>(i)]),
            juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f },
            0.5f,
            vxsuite::makeCenteredPercentFloatAttributes()));
    }

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { kStrengthParam.data(), 1 },
        "Strength",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f },
        1.0f,
        vxsuite::makePercentFloatAttributes()));
    return layout;
}

juce::String VXRebalanceAudioProcessor::getStatusText() const {
    const auto recordingType = vxsuite::readChoiceIndex(parameters, kRecordingTypeParam, 0);
    const juce::String modeLabel = recordingType == 1 ? "Live" : (recordingType == 2 ? "Phone / Rough" : "Studio");
    juce::String status = "Linked-stereo source rebalance  -  " + modeLabel + "  -  latency "
        + juce::String(getLatencySamples()) + " samples";
#if VXSTUDIO_REBALANCE_AI_VARIANT
    const int aiMode = vxsuite::readChoiceIndex(parameters, kAiModeParam, 0);
    const juce::String aiModeLabel = aiMode >= 1 ? "AI Strong" : "AI Assist";
    status += "  -  " + aiModeLabel + "  -  " + realtimeSplitter.statusText();
#endif
    return status;
}

void VXRebalanceAudioProcessor::prepareSuite(const double sampleRate, const int samplesPerBlock) {
    currentSampleRateHz = sampleRate > 1000.0 ? sampleRate : 48000.0;
    currentBlockSize = std::max(1, samplesPerBlock);
#if VXSTUDIO_REBALANCE_AI_VARIANT
    aiDsp.prepare(currentSampleRateHz, samplesPerBlock, getTotalNumOutputChannels());
    realtimeSplitter.prepare(currentSampleRateHz, samplesPerBlock, getTotalNumOutputChannels());
#else
    dsp.prepare(currentSampleRateHz, samplesPerBlock, getTotalNumOutputChannels());
#endif
    outputTrimmer.setCeiling(0.90f);  // Lowered from 0.96f to catch overload earlier
    outputTrimmer.setReleaseSeconds(0.16f);
    const int processingLatencySamples =
#if VXSTUDIO_REBALANCE_AI_VARIANT
        aiDsp.latencySamples();
#else
        dsp.latencySamples();
#endif
    dryDelayLines.assign(static_cast<size_t>(std::max(1, getTotalNumOutputChannels())),
                         std::vector<float>(static_cast<size_t>(std::max(1, processingLatencySamples)), 0.0f));
    dryDelayWritePos = 0;
#if VXSTUDIO_REBALANCE_AI_VARIANT
    setReportedLatencySamples(std::max(processingLatencySamples, realtimeSplitter.latencySamples()));
#else
    setReportedLatencySamples(processingLatencySamples);
#endif
    silenceGuard.prepare();
    resetSuite();
}

void VXRebalanceAudioProcessor::resetSuite() {
#if VXSTUDIO_REBALANCE_AI_VARIANT
    aiDsp.reset();
    realtimeSplitter.reset();
#else
    dsp.reset();
#endif
    outputTrimmer.reset();
    silenceGuard.reset();
    smoothedOutputTrimDb = 0.0f;
    outputTrimPrimed = false;
    for (auto& channel : dryDelayLines)
        std::fill(channel.begin(), channel.end(), 0.0f);
    dryDelayWritePos = 0;
    wasNeutral = false;
}

void VXRebalanceAudioProcessor::processNeutralWithLatency(juce::AudioBuffer<float>& buffer) {
    if (dryDelayLines.empty() || dryDelayLines.front().empty())
        return;

    const int numChannels = std::min(buffer.getNumChannels(), static_cast<int>(dryDelayLines.size()));
    const int numSamples = buffer.getNumSamples();
    const int delaySize = static_cast<int>(dryDelayLines.front().size());

    for (int sample = 0; sample < numSamples; ++sample) {
        for (int ch = 0; ch < numChannels; ++ch) {
            auto& delay = dryDelayLines[static_cast<size_t>(ch)];
            const float in = buffer.getSample(ch, sample);
            const float out = delay[static_cast<size_t>(dryDelayWritePos)];
            delay[static_cast<size_t>(dryDelayWritePos)] = in;
            buffer.setSample(ch, sample, out);
        }
        dryDelayWritePos = (dryDelayWritePos + 1) % delaySize;
    }
}

void VXRebalanceAudioProcessor::processProduct(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0)
        return;

    if (silenceGuard.update(buffer)) return;

    std::array<float, vxsuite::rebalance::kControlCount> targets {
        vxsuite::readNormalized(parameters, kVocalsParam, 0.5f),
        vxsuite::readNormalized(parameters, kDrumsParam, 0.5f),
        vxsuite::readNormalized(parameters, kBassParam, 0.5f),
        vxsuite::readNormalized(parameters, kGuitarParam, 0.5f),
        vxsuite::readNormalized(parameters, kOtherParam, 0.5f),
        vxsuite::readNormalized(parameters, kStrengthParam, 1.0f)
    };
#if VXSTUDIO_REBALANCE_AI_VARIANT
    const int aiMode = vxsuite::readChoiceIndex(parameters, kAiModeParam, 0);
#endif
#if !VXSTUDIO_REBALANCE_AI_VARIANT
    if (
        isEffectivelyDualMono(buffer, numSamples)) {
        for (int i = 0; i < vxsuite::rebalance::kSourceCount; ++i) {
            auto& target = targets[static_cast<size_t>(i)];
            target = 0.5f + (target - 0.5f) * 0.65f;
        }
    }
#endif

    const int recordingType = vxsuite::readChoiceIndex(parameters, kRecordingTypeParam, 0);
#if VXSTUDIO_REBALANCE_AI_VARIANT
    const auto recordingTypeEnum = static_cast<vxsuite::rebalance::RecordingType>(juce::jlimit(0, 2, recordingType));
    aiDsp.setRecordingType(recordingTypeEnum);
#else
    const auto voiceContext = getVoiceContextSnapshot();
    const auto signalQuality = getSignalQualitySnapshot();
    dsp.setAnalysisContext({
        voiceContext.vocalDominance,
        voiceContext.intelligibility,
        voiceContext.speechPresence,
        voiceContext.transientRisk
    });
    dsp.setSignalQuality(signalQuality);
    const auto recordingTypeEnum = static_cast<vxsuite::rebalance::RecordingType>(juce::jlimit(0, 2, recordingType));
    dsp.setRecordingType(recordingTypeEnum);
#endif

#if VXSTUDIO_REBALANCE_AI_VARIANT
    vxsuite::rebalance::AiMaskFrame aiMaskFrame;
    vxsuite::rebalance::StemFrame stemFrame;
    if (realtimeSplitter.processBlock(buffer, aiMaskFrame, stemFrame)) {
        if (aiMode >= 1)
            stemFrame.confidence = 1.0f;
        else {
            stemFrame.confidence = juce::jlimit(0.0f, 0.72f, stemFrame.confidence);
            aiMaskFrame.confidence = juce::jlimit(0.0f, 0.72f, aiMaskFrame.confidence);
        }
        aiDsp.setStemFrame(stemFrame);
    }
#endif

    const float strength = targets[static_cast<size_t>(vxsuite::rebalance::kControlCount - 1)];
    bool effectivelyNeutral = strength <= 1.0e-4f;
    if (!effectivelyNeutral) {
        effectivelyNeutral = true;
        for (int i = 0; i < vxsuite::rebalance::kSourceCount; ++i) {
            if (std::abs(targets[static_cast<size_t>(i)] - 0.5f) > 1.0e-3f) {
                effectivelyNeutral = false;
                break;
            }
        }
    }

    if (effectivelyNeutral) {
        wasNeutral = true;
        outputTrimPrimed = false;
        processNeutralWithLatency(buffer);
        return;
    }

    if (wasNeutral) {
#if VXSTUDIO_REBALANCE_AI_VARIANT
        // Do not reset the AI stem queue here: the splitter may just have delivered
        // the first usable frame after a neutral/waiting pass.
#else
        dsp.reset();
#endif
        wasNeutral = false;
    }

#if VXSTUDIO_REBALANCE_AI_VARIANT
    if (!aiDsp.hasUsableFrame()) {
        wasNeutral = true;
        outputTrimPrimed = false;
        processNeutralWithLatency(buffer);
        return;
    }

    aiDsp.setControlTargets(targets);
    aiDsp.process(buffer);
#else
    dsp.setControlTargets(targets);
    dsp.process(buffer);
#endif

    const float strengthDrive = juce::jlimit(0.0f, 1.0f, strength);
    float positiveIntentSum = 0.0f;
    float maxPositiveIntent = 0.0f;
    for (int i = 0; i < vxsuite::rebalance::kSourceCount; ++i) {
        const float positiveIntent = juce::jlimit(0.0f, 1.0f,
            (targets[static_cast<size_t>(i)] - 0.5f) * 2.0f);
        positiveIntentSum += positiveIntent;
        maxPositiveIntent = std::max(maxPositiveIntent, positiveIntent);
    }
    const float stackedPositiveIntent = std::max(0.0f, positiveIntentSum - 1.0f);
    const float outputTrimTargetDb = -juce::jlimit(0.0f, 12.0f,
        strengthDrive * (7.0f * stackedPositiveIntent + 4.0f * maxPositiveIntent * stackedPositiveIntent));
    const float prevOutputTrimDb = smoothedOutputTrimDb;
    if (!outputTrimPrimed) {
        smoothedOutputTrimDb = outputTrimTargetDb;
        outputTrimPrimed = true;
    } else {
        smoothedOutputTrimDb = vxsuite::smoothBlockValue(smoothedOutputTrimDb,
                                                         outputTrimTargetDb,
                                                         currentSampleRateHz,
                                                         numSamples,
                                                         0.140f);
    }
    const float prevOutputTrim = juce::Decibels::decibelsToGain(prevOutputTrimDb);
    const float outputTrim = juce::Decibels::decibelsToGain(smoothedOutputTrimDb);
    if (std::abs(outputTrim - 1.0f) > 1.0e-4f || std::abs(prevOutputTrim - 1.0f) > 1.0e-4f)
        buffer.applyGainRamp(0, numSamples, prevOutputTrim, outputTrim);

    outputTrimmer.process(buffer, currentSampleRateHz);
}

#if !defined(VXSTUDIO_DISABLE_PLUGIN_ENTRYPOINT)
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new VXRebalanceAudioProcessor();
}
#endif
