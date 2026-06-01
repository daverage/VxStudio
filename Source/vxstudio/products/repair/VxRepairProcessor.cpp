#include "VxRepairProcessor.h"
#include "VxRepairEditor.h"
#include "../../framework/VxStudioParameters.h"

#include "VxStudioVersions.h"

namespace {

constexpr std::string_view kProductName  = "VX Repair";
constexpr std::string_view kShortTag     = "RPR";
constexpr std::string_view kStageId      = "repair";

constexpr std::string_view kClarityStrength = "clarity_strength";
constexpr std::string_view kNoiseStrength   = "noise_strength";
constexpr std::string_view kReverbStrength  = "reverb_strength";
constexpr std::string_view kClarityOn       = "clarity_on";
constexpr std::string_view kNoiseOn         = "noise_on";
constexpr std::string_view kReverbOn        = "reverb_on";
constexpr std::string_view kClarityListen   = "clarity_listen";
constexpr std::string_view kNoiseListen     = "noise_listen";
constexpr std::string_view kReverbListen    = "reverb_listen";
constexpr std::string_view kMakeupGain      = "makeup_gain";

} // namespace

VXRepairAudioProcessor::VXRepairAudioProcessor()
    : ProcessorBase(makeIdentity(), makeParams()) {}

vxsuite::ProductIdentity VXRepairAudioProcessor::makeIdentity() {
    vxsuite::ProductIdentity id {};
    id.productName      = kProductName;
    id.shortTag         = kShortTag;
    id.stageId          = kStageId;
    id.dspVersion       = vxsuite::versions::plugins::repair;
    id.primaryParamId   = kNoiseStrength;
    id.secondaryParamId = kReverbStrength;
    id.tertiaryParamId  = kClarityStrength;
    id.primaryLabel     = "Noise";
    id.secondaryLabel   = "Reverb";
    id.tertiaryLabel    = "Speech";
    id.stageType              = vxsuite::StageType::spectral;
    id.requiresVoiceAnalysis  = true;
    id.requiresSignalQuality  = false;
    id.theme.accentRgb     = { 1.0f, 0.52f, 0.08f };
    id.theme.accent2Rgb    = { 0.15f, 0.09f, 0.03f };
    id.theme.backgroundRgb = { 0.07f, 0.05f, 0.03f };
    id.theme.panelRgb      = { 0.11f, 0.08f, 0.05f };
    id.theme.textRgb       = { 0.97f, 0.93f, 0.87f };
    return id;
}

juce::AudioProcessorValueTreeState::ParameterLayout VXRepairAudioProcessor::makeParams() {
    using juce::AudioParameterFloat;
    using juce::AudioParameterBool;
    using juce::ParameterID;
    using juce::NormalisableRange;

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    auto pct = vxsuite::makePercentFloatAttributes();
    auto addStrength = [&](std::string_view id, std::string_view label, float def) {
        layout.add(std::make_unique<AudioParameterFloat>(
            ParameterID { id.data(), 1 }, label.data(),
            NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, def, pct));
    };
    auto addBool = [&](std::string_view id, std::string_view label, bool def) {
        layout.add(std::make_unique<AudioParameterBool>(
            ParameterID { id.data(), 1 }, label.data(), def));
    };

    addStrength(kNoiseStrength,   "Noise",          0.5f);
    addStrength(kReverbStrength,  "Reverb",         0.5f);
    addStrength(kClarityStrength, "Speech Clarity", 0.5f);
    addBool    (kNoiseOn,         "Noise On",       false);
    addBool    (kReverbOn,        "Reverb On",      false);
    addBool    (kClarityOn,       "Clarity On",     false);
    addBool    (kNoiseListen,     "Noise Listen",   false);
    addBool    (kReverbListen,    "Reverb Listen",  false);
    addBool    (kClarityListen,   "Clarity Listen", false);

    // Makeup gain: 0 = −12 dB, 0.5 = 0 dB (unity), 1.0 = +12 dB
    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID { kMakeupGain.data(), 1 }, "Makeup Gain",
        NormalisableRange<float> { -12.0f, 12.0f, 0.1f }, 0.0f,
        juce::AudioParameterFloatAttributes{}.withLabel("dB")));

    return layout;
}

juce::String VXRepairAudioProcessor::getStatusText() const {
    if (analyser.isCollecting())
        return "Analysing — play a representative section";
    if (analyser.isComplete()) {
        const auto a = analyser.getAssessment();
        const int n = (a.cleanupActive ? 1 : 0) + (a.noiseActive ? 1 : 0) + (a.reverbActive ? 1 : 0);
        return n == 0 ? "No significant issues detected"
                      : juce::String(n) + " issue" + (n > 1 ? "s" : "") + " detected";
    }
    return "Ready — click Analyse to scan your audio";
}

juce::AudioProcessorEditor* VXRepairAudioProcessor::createEditor() {
    return new VXRepairEditor(*this);
}

void VXRepairAudioProcessor::getStateInformation(juce::MemoryBlock& destData) {
    auto xml = parameters.copyState().createXml();
    if (!xml) return;

    if (analyser.isComplete()) {
        const auto a = analyser.getAssessment();
        xml->setAttribute("ra_noise",   a.noiseScore);
        xml->setAttribute("ra_reverb",  a.reverbScore);
        xml->setAttribute("ra_clarity", a.humMudScore);
        xml->setAttribute("ra_conf",    a.confidence);
        xml->setAttribute("ra_done",    1);
    }
    copyXmlToBinary(*xml, destData);
}

void VXRepairAudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (!xml || !xml->hasTagName(parameters.state.getType())) return;

    parameters.replaceState(juce::ValueTree::fromXml(*xml));

    if (xml->getIntAttribute("ra_done", 0) == 1) {
        vxsuite::repair::RepairAssessment a;
        a.noiseScore   = static_cast<float>(xml->getDoubleAttribute("ra_noise",   0.0));
        a.reverbScore  = static_cast<float>(xml->getDoubleAttribute("ra_reverb",  0.0));
        a.humMudScore  = static_cast<float>(xml->getDoubleAttribute("ra_clarity", 0.0));
        a.confidence   = static_cast<float>(xml->getDoubleAttribute("ra_conf",    1.0));
        a.noiseActive   = a.noiseScore   >= vxsuite::repair::RepairAnalyser::kActiveThreshold;
        a.reverbActive  = a.reverbScore  >= vxsuite::repair::RepairAnalyser::kActiveThreshold;
        a.cleanupActive = a.humMudScore  >= vxsuite::repair::RepairAnalyser::kActiveThreshold;
        a.suggestedNoiseStrength   = vxsuite::repair::RepairAnalyser::scoreToStrengthStatic(a.noiseScore);
        a.suggestedReverbStrength  = vxsuite::repair::RepairAnalyser::scoreToStrengthStatic(a.reverbScore);
        a.suggestedCleanupStrength = vxsuite::repair::RepairAnalyser::scoreToStrengthStatic(a.humMudScore);
        analyser.restoreAssessment(a);
    }
}

void VXRepairAudioProcessor::triggerAnalysis() {
    triggerPending.store(true, std::memory_order_release);
}

void VXRepairAudioProcessor::applyAssessmentToParams() {
    const auto a = analyser.getAssessment();

    auto setFloat = [&](std::string_view id, float val) {
        if (auto* p = parameters.getParameter(id.data()))
            p->setValueNotifyingHost(val);
    };
    auto setBool = [&](std::string_view id, bool val) {
        if (auto* p = parameters.getParameter(id.data()))
            p->setValueNotifyingHost(val ? 1.0f : 0.0f);
    };

    setFloat(kNoiseStrength,   a.suggestedNoiseStrength);
    setFloat(kReverbStrength,  a.suggestedReverbStrength);
    setFloat(kClarityStrength, a.suggestedCleanupStrength);
    setBool (kNoiseOn,         a.noiseActive);
    setBool (kReverbOn,        a.reverbActive);
    setBool (kClarityOn,       a.cleanupActive);
}

void VXRepairAudioProcessor::prepareSuite(double sampleRate, int samplesPerBlock) {
    const int channels = getTotalNumOutputChannels();
    denoiserDsp.prepare(sampleRate, samplesPerBlock);
    deverbDsp.setChannelCount(channels);
    deverbDsp.prepare(sampleRate, samplesPerBlock);
    deEsserDsp.prepare(sampleRate, samplesPerBlock, channels);
    dePlosiveDsp.prepare(sampleRate, samplesPerBlock, channels);
    deBreathDsp.prepare(sampleRate, samplesPerBlock, channels);

    if (sampleRate != currentSampleRate || samplesPerBlock != currentBlockSize) {
        currentSampleRate = sampleRate;
        currentBlockSize  = samplesPerBlock;
        analyser.prepare(sampleRate, samplesPerBlock);
    }

    // Size dry-delay buffers to each DSP's reported latency so listen can
    // output dry − wet with correct time alignment.
    const int noiseLat  = denoiserDsp.getLatencySamples();
    const int reverbLat = deverbDsp.getLatencySamples();
    const int ch        = getTotalNumOutputChannels();

    noiseDryDelay.setSize(ch, std::max(1, noiseLat),  false, true, true);
    reverbDryDelay.setSize(ch, std::max(1, reverbLat), false, true, true);
    noiseDryDelayPos  = 0;
    reverbDryDelayPos = 0;
}

void VXRepairAudioProcessor::resetSuite() {
    denoiserDsp.reset();
    deverbDsp.reset();
    deEsserDsp.reset();
    dePlosiveDsp.reset();
    deBreathDsp.reset();
    // Analyser is NOT reset here — the host calls reset() on every transport
    // start, which would wipe an in-progress collection. Analyser lifetime is
    // controlled explicitly via triggerAnalysis() and resetAnalysis().
}

void VXRepairAudioProcessor::resetAnalysis() {
    analyser.reset();
}

void VXRepairAudioProcessor::processProduct(juce::AudioBuffer<float>& buffer,
                                             juce::MidiBuffer& /*midi*/) {
    const int numSamples = buffer.getNumSamples();
    if (numSamples == 0) return;

    if (triggerPending.exchange(false, std::memory_order_acq_rel))
        analyser.startCollection();

    if (analyser.isCollecting())
        analyser.process(buffer, numSamples);

    const float noiseStr   = vxsuite::readNormalized(parameters, kNoiseStrength,   0.5f);
    const float reverbStr  = vxsuite::readNormalized(parameters, kReverbStrength,  0.5f);
    const float clarityStr = vxsuite::readNormalized(parameters, kClarityStrength, 0.5f);
    const bool  noiseOn    = vxsuite::readBool(parameters, kNoiseOn,    false);
    const bool  reverbOn   = vxsuite::readBool(parameters, kReverbOn,   false);
    const bool  clarityOn  = vxsuite::readBool(parameters, kClarityOn,  false);
    const bool  noiseListen   = vxsuite::readBool(parameters, kNoiseListen,   false);
    const bool  reverbListen  = vxsuite::readBool(parameters, kReverbListen,  false);
    const bool  clarityListen = vxsuite::readBool(parameters, kClarityListen, false);
    const float makeupDb   = [&] {
        auto* p = parameters.getParameter(kMakeupGain.data());
        return p ? p->convertFrom0to1(p->getValue()) : 0.0f;
    }();

    const bool anyListen = noiseListen || reverbListen || clarityListen;
    const bool anyActive = noiseOn    || reverbOn    || clarityOn;

    if (!anyActive && !anyListen)
        return;

    vxsuite::ProcessOptions opts {};
    opts.isVoiceMode        = true;
    opts.voiceProtect       = 0.75f;
    opts.sourceProtect      = 0.75f;
    opts.guardStrictness    = 0.65f;
    opts.lateTailAggression = 0.55f;

    using vxsuite::speech_clarity::DeEsserDsp;
    using vxsuite::speech_clarity::DePolosiveDsp;
    using vxsuite::speech_clarity::DeBreathDsp;

    if (anyListen) {
        // delta listen: output dry − wet, time-aligned via per-DSP delay buffer.
        // Denoiser and deverb each have ~16 ms algorithmic latency; without
        // compensation dry − wet sounds like a comb-filter echo.
        const int numCh = buffer.getNumChannels();

        auto pushDry = [&](juce::AudioBuffer<float>& delayBuf, int& pos) {
            const int delayLen  = delayBuf.getNumSamples();
            const int startPos  = pos;  // all channels must start from the same position
            juce::AudioBuffer<float> delayedDry(numCh, numSamples);
            for (int ch = 0; ch < numCh; ++ch) {
                int p             = startPos;
                const float* src  = buffer.getReadPointer(ch);
                float*       ring = delayBuf.getWritePointer(ch);
                float*       dst  = delayedDry.getWritePointer(ch);
                for (int i = 0; i < numSamples; ++i) {
                    dst[i]  = ring[p];   // oldest sample = latency-delayed dry
                    ring[p] = src[i];    // overwrite with current dry
                    p = (p + 1) % delayLen;
                }
            }
            pos = (startPos + numSamples) % delayLen;  // advance shared pos once
            return delayedDry;
        };

        if (noiseListen) {
            auto delayed = pushDry(noiseDryDelay, noiseDryDelayPos);
            denoiserDsp.processInPlace(buffer, noiseStr, opts);
            for (int ch = 0; ch < numCh; ++ch) {
                auto* out = buffer.getWritePointer(ch);
                const float* dry = delayed.getReadPointer(ch);
                for (int i = 0; i < numSamples; ++i)
                    out[i] = dry[i] - out[i];
            }
        } else if (reverbListen) {
            auto delayed = pushDry(reverbDryDelay, reverbDryDelayPos);
            deverbDsp.processInPlace(buffer, reverbStr, opts);
            for (int ch = 0; ch < numCh; ++ch) {
                auto* out = buffer.getWritePointer(ch);
                const float* dry = delayed.getReadPointer(ch);
                for (int i = 0; i < numSamples; ++i)
                    out[i] = dry[i] - out[i];
            }
        } else if (clarityListen) {
            // Speech clarity DSPs are IIR — negligible latency, direct subtract is clean.
            juce::AudioBuffer<float> dryCopy;
            dryCopy.makeCopyOf(buffer);
            dePlosiveDsp.process(buffer, { clarityStr, 1.0f });
            deEsserDsp.process  (buffer, { clarityStr, 1.0f });
            deBreathDsp.process (buffer, { clarityStr, 1.0f });
            for (int ch = 0; ch < numCh; ++ch) {
                auto* out = buffer.getWritePointer(ch);
                const float* dry = dryCopy.getReadPointer(ch);
                for (int i = 0; i < numSamples; ++i)
                    out[i] = dry[i] - out[i];
            }
        }
        return;
    }

    // Repair chain: Noise → Speech Clarity → Reverb
    if (noiseOn)
        denoiserDsp.processInPlace(buffer, noiseStr, opts);

    if (clarityOn) {
        dePlosiveDsp.process(buffer, { clarityStr, 1.0f });
        deEsserDsp.process  (buffer, { clarityStr, 1.0f });
        deBreathDsp.process (buffer, { clarityStr, 1.0f });
    }

    if (reverbOn)
        deverbDsp.processInPlace(buffer, reverbStr, opts);

    // Makeup gain (applied after all processing)
    const float makeupLinear = std::pow(10.0f, makeupDb / 20.0f);
    if (std::abs(makeupLinear - 1.0f) > 0.001f)
        buffer.applyGain(makeupLinear);
}

#if !defined(VXSUITE_DISABLE_PLUGIN_ENTRYPOINT) && !defined(VXSTUDIO_DISABLE_PLUGIN_ENTRYPOINT)
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new VXRepairAudioProcessor();
}
#endif
