#include "VxRebalanceProcessor.h"

#include "../../framework/VxSuiteBlockSmoothing.h"
#include "../../framework/VxSuiteHelpContent.h"
#include "../../framework/VxSuiteParameters.h"
#include "VxSuiteVersions.h"

#include <cmath>

namespace {

constexpr std::string_view kProductName = "Rebalance";
constexpr std::string_view kShortTag = "RBL";
constexpr std::string_view kVocalsParam = "vocals";
constexpr std::string_view kDrumsParam = "drums";
constexpr std::string_view kBassParam = "bass";
constexpr std::string_view kGuitarParam = "guitar";
constexpr std::string_view kOtherParam = "other";
constexpr std::string_view kStrengthParam = "strength";
constexpr std::string_view kModeParam = "mode";
constexpr std::string_view kRecordingTypeParam = "recordingType";

constexpr std::array<std::string_view, vxsuite::ProductIdentity::maxControlBankControls> kBankParamIds {
    kVocalsParam, kDrumsParam, kBassParam, kGuitarParam, kOtherParam, kStrengthParam
};

constexpr std::array<std::string_view, vxsuite::ProductIdentity::maxControlBankControls> kBankLabels {
    "Vocals", "Drums", "Bass", "Guitar", "Other", "Strength"
};

constexpr std::array<std::string_view, vxsuite::ProductIdentity::maxControlBankControls> kBankHints {
    "Lead-vocal stem level. 0% = unchanged, -100% = removed, +100% = boosted.",
    "Drums stem level. 0% = unchanged, -100% = removed, +100% = boosted.",
    "Bass stem level. 0% = unchanged, -100% = removed, +100% = boosted.",
    "Guitar stem level. 0% = unchanged, -100% = removed, +100% = boosted.",
    "Other stem level. 0% = unchanged, -100% = removed, +100% = boosted.",
    "Scale all five stem moves together."
};

constexpr std::array<float, vxsuite::ProductIdentity::maxControlBankControls> kBankDefaults {
    0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 1.0f
};

} // namespace

VXRebalanceAudioProcessor::VXRebalanceAudioProcessor()
    : ProcessorBase(makeIdentity(), makeParameterLayout()) {
}

VXRebalanceAudioProcessor::~VXRebalanceAudioProcessor() = default;

vxsuite::ProductIdentity VXRebalanceAudioProcessor::makeIdentity() {
    vxsuite::ProductIdentity id {};
    id.productName = kProductName;
    id.shortTag = kShortTag;
    id.modeParamId = kModeParam;
    id.selectorLabel = "Engine";
    id.selectorChoiceLabels = { "DSP" };
    id.defaultMode = vxsuite::Mode::vocal;
    id.auxSelectorParamId = kRecordingTypeParam;
    id.auxSelectorLabel = "Recording Type";
    id.auxSelectorChoiceLabels = { "Studio", "Live", "Phone / Rough" };
    id.auxSelectorDefaultIndex = 0;
    id.auxSelectorFollowsGeneralMode = false;
    id.controlBankCount = vxsuite::rebalance::Dsp::kControlCount;
    id.controlBankVertical = true;
    id.compactControlBankLayout = true;
    id.controlBankParamIds = kBankParamIds;
    id.controlBankLabels = kBankLabels;
    id.controlBankHints = kBankHints;
    id.controlBankDefaultValues = kBankDefaults;
    id.dspVersion = vxsuite::versions::plugins::rebalance;
    id.helpTitle = vxsuite::help::rebalance.title;
    id.helpHtml = vxsuite::help::rebalance.html;
    id.readmeSection = vxsuite::help::rebalance.readmeSection;
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
        juce::ParameterID { kModeParam.data(), 1 },
        "Engine",
        juce::StringArray { "DSP" },
        0,
        vxsuite::makeChoiceAttributes("Engine")));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { kRecordingTypeParam.data(), 1 },
        "Recording Type",
        juce::StringArray { "Studio", "Live", "Phone / Rough" },
        0,
        vxsuite::makeChoiceAttributes("Recording Type")));

    for (int i = 0; i < vxsuite::rebalance::Dsp::kSourceCount; ++i) {
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
    return "Linked-stereo source rebalance  -  DSP heuristic engine  -  " + modeLabel + "  -  latency "
        + juce::String(dsp.latencySamples()) + " samples";
}

void VXRebalanceAudioProcessor::prepareSuite(const double sampleRate, const int samplesPerBlock) {
    currentSampleRateHz = sampleRate > 1000.0 ? sampleRate : 48000.0;
    currentBlockSize = std::max(1, samplesPerBlock);
    dsp.prepare(currentSampleRateHz, samplesPerBlock, getTotalNumOutputChannels());
    dryDelayLines.assign(static_cast<size_t>(std::max(1, getTotalNumOutputChannels())),
                         std::vector<float>(static_cast<size_t>(std::max(1, dsp.latencySamples())), 0.0f));
    dryDelayWritePos = 0;
    setReportedLatencySamples(dsp.latencySamples());
    resetSuite();
}

void VXRebalanceAudioProcessor::resetSuite() {
    dsp.reset();
    smoothedControls = kBankDefaults;
    controlsPrimed = false;
    for (auto& channel : dryDelayLines)
        std::fill(channel.begin(), channel.end(), 0.0f);
    dryDelayWritePos = 0;
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

    std::array<float, vxsuite::rebalance::Dsp::kControlCount> targets {
        vxsuite::readNormalized(parameters, kVocalsParam, 0.5f),
        vxsuite::readNormalized(parameters, kDrumsParam, 0.5f),
        vxsuite::readNormalized(parameters, kBassParam, 0.5f),
        vxsuite::readNormalized(parameters, kGuitarParam, 0.5f),
        vxsuite::readNormalized(parameters, kOtherParam, 0.5f),
        vxsuite::readNormalized(parameters, kStrengthParam, 1.0f)
    };
    const auto voiceContext = getVoiceContextSnapshot();
    const auto signalQuality = getSignalQualitySnapshot();
    const int recordingType = vxsuite::readChoiceIndex(parameters, kRecordingTypeParam, 0);
    dsp.setAnalysisContext({
        voiceContext.vocalDominance,
        voiceContext.intelligibility,
        voiceContext.speechPresence,
        voiceContext.transientRisk
    });
    dsp.setSignalQuality(signalQuality);
    const auto recordingTypeEnum = static_cast<vxsuite::rebalance::Dsp::RecordingType>(juce::jlimit(0, 2, recordingType));
    dsp.setRecordingType(recordingTypeEnum);

    if (!controlsPrimed) {
        smoothedControls = targets;
        controlsPrimed = true;
    } else {
        for (int i = 0; i < vxsuite::rebalance::Dsp::kControlCount; ++i) {
            const float timeSeconds = i == vxsuite::rebalance::Dsp::kControlCount - 1 ? 0.070f : 0.050f;
            smoothedControls[static_cast<size_t>(i)] =
                vxsuite::smoothBlockValue(smoothedControls[static_cast<size_t>(i)],
                                          targets[static_cast<size_t>(i)],
                                          currentSampleRateHz,
                                          numSamples,
                                          timeSeconds);
        }
    }

    const float strength = smoothedControls[static_cast<size_t>(vxsuite::rebalance::Dsp::kControlCount - 1)];
    bool effectivelyNeutral = strength <= 1.0e-4f;
    if (!effectivelyNeutral) {
        effectivelyNeutral = true;
        for (int i = 0; i < vxsuite::rebalance::Dsp::kSourceCount; ++i) {
            if (std::abs(smoothedControls[static_cast<size_t>(i)] - 0.5f) > 1.0e-3f) {
                effectivelyNeutral = false;
                break;
            }
        }
    }

    if (effectivelyNeutral) {
        processNeutralWithLatency(buffer);
        return;
    }

    dsp.setControlTargets(smoothedControls);
    dsp.process(buffer);
}

#if !defined(VXSUITE_DISABLE_PLUGIN_ENTRYPOINT)
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new VXRebalanceAudioProcessor();
}
#endif
