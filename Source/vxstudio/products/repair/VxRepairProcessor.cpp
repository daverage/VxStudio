#include "VxRepairProcessor.h"
#include "VxRepairEditor.h"
#include "../../framework/VxStudioParameters.h"
#include "../../framework/VxStudioHelpContent.h"

#include "VxStudioVersions.h"

namespace {

constexpr std::string_view kProductName  = "VX Repair";
constexpr std::string_view kShortTag     = "RPR";
constexpr std::string_view kStageId      = "repair";

constexpr std::string_view kClarityStrength  = "clarity_strength";
constexpr std::string_view kNoiseStrength    = "noise_strength";
constexpr std::string_view kReverbStrength   = "reverb_strength";
constexpr std::string_view kClarityOn        = "clarity_on";
constexpr std::string_view kNoiseOn          = "noise_on";
constexpr std::string_view kReverbOn         = "reverb_on";
constexpr std::string_view kClarityListen    = "clarity_listen";
constexpr std::string_view kNoiseListen      = "noise_listen";
constexpr std::string_view kReverbListen     = "reverb_listen";
constexpr std::string_view kMakeupGain       = "makeup_gain";
constexpr std::string_view kNoiseDeepFilter  = "noise_deepfilter";
constexpr std::string_view kClickStrength    = "click_strength";
constexpr std::string_view kClickOn          = "click_on";
constexpr std::string_view kClickListen      = "click_listen";

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
    id.helpTitle = vxsuite::help::repair.title;
    id.helpHtml  = vxsuite::help::repair.html;
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
    addStrength(kClarityStrength, "Speech Clarity", 0.0f);
    addStrength(kClickStrength,   "Clicks",         0.5f);
    addBool    (kNoiseOn,         "Noise On",       false);
    addBool    (kReverbOn,        "Reverb On",      false);
    addBool    (kClarityOn,       "Clarity On",     false);
    addBool    (kClickOn,         "Click On",       false);
    addBool    (kNoiseListen,     "Noise Listen",   false);
    addBool    (kReverbListen,    "Reverb Listen",  false);
    addBool    (kClarityListen,   "Clarity Listen", false);
    addBool    (kClickListen,     "Click Listen",   false);
    addBool    (kNoiseDeepFilter, "Noise DeepFilter", false);

    // Makeup gain: 0 = −12 dB, 0.5 = 0 dB (unity), 1.0 = +12 dB
    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID { kMakeupGain.data(), 1 }, "Makeup Gain",
        NormalisableRange<float> { -12.0f, 12.0f, 0.1f }, 0.0f,
        juce::AudioParameterFloatAttributes{}.withLabel("dB")));

    return layout;
}

juce::String VXRepairAudioProcessor::getStatusText() const {
    const auto phase = getAnalysisPhase();
    if (phase == AnalysisPhase::Phase1)
        return "Phase 1  -  play a representative section";
    if (phase == AnalysisPhase::Phase2)
        return "Phase 2  -  detecting reverb & clarity";
    if (phase == AnalysisPhase::Done) {
        const auto a = analyser.getAssessment();
        const int n = (a.cleanupActive ? 1 : 0) + (a.noiseActive ? 1 : 0)
                    + (a.reverbActive ? 1 : 0) + (a.clickActive ? 1 : 0);
        return n == 0 ? "No significant issues detected"
                      : juce::String(n) + " issue" + (n > 1 ? "s" : "") + " detected";
    }
    return "Ready  -  click Analyse to scan your audio";
}

juce::AudioProcessorEditor* VXRepairAudioProcessor::createEditor() {
    return new VXRepairEditor(*this);
}

void VXRepairAudioProcessor::getStateInformation(juce::MemoryBlock& destData) {
    auto xml = parameters.copyState().createXml();
    if (!xml) return;

    if (isAnalysisComplete()) {
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
        analysisPhase.store(static_cast<int>(AnalysisPhase::Done), std::memory_order_relaxed);
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
    setFloat(kClickStrength,   a.suggestedClickStrength);
    setBool (kNoiseOn,         a.noiseActive);
    setBool (kReverbOn,        a.reverbActive);
    setBool (kClarityOn,       a.cleanupActive);
    setBool (kClickOn,         a.clickActive);
}

float VXRepairAudioProcessor::getAnalysisProgress() const noexcept {
    const auto phase = getAnalysisPhase();
    switch (phase) {
        case AnalysisPhase::Phase1: return analyser.getProgress() * 0.5f;
        case AnalysisPhase::Phase2: return 0.5f + analyser.getProgress() * 0.5f;
        case AnalysisPhase::Done:   return 1.0f;
        default:                    return 0.0f;
    }
}

bool VXRepairAudioProcessor::isAnalysisRunning() const noexcept {
    const auto phase = getAnalysisPhase();
    return phase == AnalysisPhase::Phase1 || phase == AnalysisPhase::Phase2;
}

void VXRepairAudioProcessor::prepareSuite(double sampleRate, int samplesPerBlock) {
    const int channels = getTotalNumOutputChannels();
    denoiserDsp.prepare(sampleRate, samplesPerBlock);
    analyserDenoiserDsp.prepare(sampleRate, samplesPerBlock);
    deverbDsp.setChannelCount(channels);
    deverbDsp.prepare(sampleRate, samplesPerBlock);
    deClickDsp.prepare(sampleRate, samplesPerBlock, channels);
    deEsserDsp.prepare(sampleRate, samplesPerBlock, channels);
    dePlosiveDsp.prepare(sampleRate, samplesPerBlock, channels);
    deBreathDsp.prepare(sampleRate, samplesPerBlock, channels);
    dfService.prepareRealtime(sampleRate, samplesPerBlock);

    if (sampleRate != currentSampleRate || samplesPerBlock != currentBlockSize) {
        currentSampleRate = sampleRate;
        currentBlockSize  = samplesPerBlock;
        analyser.prepare(sampleRate, samplesPerBlock);
    }

    // Size dry-delay buffers to the larger of denoiser / deepfilter latency
    const int noiseLat  = std::max(denoiserDsp.getLatencySamples(), dfService.getLatencySamples());
    const int clickLat  = deClickDsp.getLatencySamples();
    const int reverbLat = deverbDsp.getLatencySamples();
    const int ch        = getTotalNumOutputChannels();

    noiseDryDelay.setSize(ch, std::max(1, noiseLat),  false, true, true);
    reverbDryDelay.setSize(ch, std::max(1, reverbLat), false, true, true);
    clickDryDelay.setSize(ch, std::max(1, clickLat),  false, true, true);
    noiseDryDelayPos  = 0;
    reverbDryDelayPos = 0;
    clickDryDelayPos  = 0;

    // Pre-allocate scratch buffers for advancing idle STFT state in listen mode.
    stftStateScratch.setSize (ch, std::max(1, samplesPerBlock), false, true, true);
    stftStateScratch2.setSize(ch, std::max(1, samplesPerBlock), false, true, true);

    // Pre-allocate pass-through delay lines — each ring is sized to the corresponding
    // stage latency so that when a stage is off the output is the latency-correct dry signal.
    noisePassthroughDelay.setSize (ch, std::max(1, noiseLat),  false, true, true);
    reverbPassthroughDelay.setSize(ch, std::max(1, reverbLat), false, true, true);
    noisePassthroughPos  = 0;
    reverbPassthroughPos = 0;

    // All STFT stages + click lookahead always run (at 0 strength when disabled) so
    // the total latency is constant.  Report it so the host compensates via PDC.
    setReportedLatencySamples(noiseLat + clickLat + reverbLat);
}

void VXRepairAudioProcessor::resetSuite() {
    stftStateScratch.clear();
    stftStateScratch2.clear();
    noisePassthroughDelay.clear();
    reverbPassthroughDelay.clear();
    noisePassthroughPos  = 0;
    reverbPassthroughPos = 0;
    clickDryDelay.clear();
    clickDryDelayPos = 0;
    denoiserDsp.reset();
    analyserDenoiserDsp.reset();
    deverbDsp.reset();
    deClickDsp.reset();
    deEsserDsp.reset();
    dePlosiveDsp.reset();
    deBreathDsp.reset();
    dfService.resetRealtime();
    // Analyser is NOT reset here  -  see triggerAnalysis() and resetAnalysis().
}

void VXRepairAudioProcessor::resetAnalysis() {
    analyser.reset();
    analysisPhase.store(static_cast<int>(AnalysisPhase::Idle), std::memory_order_relaxed);
    savedNoiseScore = 0.0f;
    sibilanceIntensity.store(0.0f);
    plosiveIntensity.store(0.0f);
    breathIntensity.store(0.0f);
    smoothedPeak = 0.01f;
}

void VXRepairAudioProcessor::detectClarityIntensities(const juce::AudioBuffer<float>& buf, int n) noexcept {
    if (n < 2) return;
    const int numCh = buf.getNumChannels();

    float hfEnergy = 0.0f, bbEnergy = 0.0f, peakSq = 0.0f;

    for (int ch = 0; ch < numCh; ++ch) {
        const float* d = buf.getReadPointer(ch);
        for (int i = 1; i < n; ++i) {
            const float s    = d[i];
            const float diff = s - d[i - 1];
            hfEnergy += diff * diff;
            bbEnergy += s * s;
            peakSq = std::max(peakSq, s * s);
        }
    }

    const float bbTotal = std::max(bbEnergy, 1.0e-9f);
    const float rms     = std::sqrt(bbEnergy / std::max(1, numCh * (n - 1)));
    const bool  hasSig  = rms > 0.002f;

    const float hfRatio = hfEnergy / bbTotal;
    // Threshold lowered from 0.25 to 0.08: voiced speech 6 dB above sibilance
    // drives the ratio down to ~0.12, which the old threshold missed entirely.
    const float sib = hasSig && hfRatio > 0.08f
        ? juce::jlimit(0.0f, 1.0f, (hfRatio - 0.08f) / 0.30f)
        : 0.0f;

    const float crest = std::sqrt(peakSq * static_cast<float>(numCh * (n - 1)) / bbTotal);
    const float plos = hasSig && crest > 4.0f
        ? juce::jlimit(0.0f, 1.0f, (crest - 4.0f) / 4.0f)
        : 0.0f;

    smoothedPeak = std::max(smoothedPeak * 0.9998f, rms);
    const float levelRel = smoothedPeak > 1.0e-6f ? rms / smoothedPeak : 0.0f;
    const float brea = hasSig && levelRel < 0.35f && hfRatio > 0.06f
        ? juce::jlimit(0.0f, 1.0f, (0.35f - levelRel) / 0.20f)
        : 0.0f;

    // Click: very high crest factor (linear peak/rms > 8) indicates impulse artifact.
    const float click = hasSig && crest > 8.0f
        ? juce::jlimit(0.0f, 1.0f, (crest - 8.0f) / 12.0f)
        : 0.0f;

    // Faster smoothing for transient artifacts: 0.40 per block ≈ 2-block rise time.
    // Sibilance/plosive benefit from fast response; breath needs fast onset too.
    sibilanceIntensity.store(0.40f * sibilanceIntensity.load(std::memory_order_relaxed) + 0.60f * sib,   std::memory_order_relaxed);
    plosiveIntensity.store  (0.40f * plosiveIntensity.load(std::memory_order_relaxed)   + 0.60f * plos,  std::memory_order_relaxed);
    breathIntensity.store   (0.50f * breathIntensity.load(std::memory_order_relaxed)    + 0.50f * brea,  std::memory_order_relaxed);
    clickIntensity.store    (0.30f * clickIntensity.load(std::memory_order_relaxed)     + 0.70f * click, std::memory_order_relaxed);
}

void VXRepairAudioProcessor::processProduct(juce::AudioBuffer<float>& buffer,
                                             juce::MidiBuffer& /*midi*/) {
    const int numSamples = buffer.getNumSamples();
    if (numSamples == 0) return;

    // Trigger Phase 1 on request
    if (triggerPending.exchange(false, std::memory_order_acq_rel)) {
        analyserDenoiserDsp.reset();
        analyser.startCollection();
        analysisPhase.store(static_cast<int>(AnalysisPhase::Phase1), std::memory_order_release);
    }

    // Phase transitions (audio thread  -  no allocation)
    const auto phase = getAnalysisPhase();

    if (phase == AnalysisPhase::Phase1 && analyser.isComplete()) {
        const auto a = analyser.getAssessment();
        if (a.noiseScore >= vxsuite::repair::RepairAnalyser::kActiveThreshold) {
            // Significant noise found  -  run Phase 2 with denoised audio
            savedNoiseScore = a.noiseScore;
            analyserDenoiserDsp.reset();
            analyser.startCollection();
            analysisPhase.store(static_cast<int>(AnalysisPhase::Phase2), std::memory_order_release);
        } else {
            // No significant noise  -  single-pass is sufficient
            analysisPhase.store(static_cast<int>(AnalysisPhase::Done), std::memory_order_release);
        }
    } else if (phase == AnalysisPhase::Phase2 && analyser.isComplete()) {
        // Merge Phase 2 reverb/clarity with Phase 1 noise score
        auto merged = analyser.getAssessment();
        merged.noiseScore             = savedNoiseScore;
        merged.noiseActive            = savedNoiseScore >= vxsuite::repair::RepairAnalyser::kActiveThreshold;
        merged.suggestedNoiseStrength = vxsuite::repair::RepairAnalyser::scoreToStrengthStatic(savedNoiseScore);
        analyser.restoreAssessment(merged);
        analysisPhase.store(static_cast<int>(AnalysisPhase::Done), std::memory_order_release);
    }

    // Feed analyser  -  Phase 2 uses a denoised copy so reverb/clarity are noise-corrected
    if (analyser.isCollecting()) {
        const auto currentPhase = getAnalysisPhase();
        if (currentPhase == AnalysisPhase::Phase2) {
            juce::AudioBuffer<float> analysisBuf;
            analysisBuf.makeCopyOf(buffer);
            vxsuite::ProcessOptions noiseOptsForAnalysis {};
            noiseOptsForAnalysis.isVoiceMode     = true;
            noiseOptsForAnalysis.voiceProtect    = 0.75f;
            noiseOptsForAnalysis.speechFocus     = 0.75f;
            // Denoise at a moderate fixed strength so the reverb/clarity scores
            // see a clean signal without over-suppressing residue.
            analyserDenoiserDsp.processInPlace(analysisBuf,
                                               juce::jlimit(0.35f, 0.65f, savedNoiseScore * 0.8f),
                                               noiseOptsForAnalysis);
            analyser.process(analysisBuf, numSamples);
        } else {
            analyser.process(buffer, numSamples);
        }
    }

    const float noiseStr   = vxsuite::readNormalized(parameters, kNoiseStrength,   0.5f);
    const float reverbStr  = vxsuite::readNormalized(parameters, kReverbStrength,  0.5f);
    const float clarityStr = vxsuite::readNormalized(parameters, kClarityStrength, 0.5f);
    const float clickStr   = vxsuite::readNormalized(parameters, kClickStrength,   0.5f);
    const bool  noiseOn    = vxsuite::readBool(parameters, kNoiseOn,    false);
    const bool  reverbOn   = vxsuite::readBool(parameters, kReverbOn,   false);
    const bool  clarityOn  = vxsuite::readBool(parameters, kClarityOn,  false);
    const bool  clickOn    = vxsuite::readBool(parameters, kClickOn,    false);
    const bool  noiseListen   = vxsuite::readBool(parameters, kNoiseListen,   false);
    const bool  reverbListen  = vxsuite::readBool(parameters, kReverbListen,  false);
    const bool  clarityListen = vxsuite::readBool(parameters, kClarityListen, false);
    const bool  clickListen   = vxsuite::readBool(parameters, kClickListen,   false);
    const bool  useDeepFilter = vxsuite::readBool(parameters, kNoiseDeepFilter, false)
                                && dfService.isRealtimeReady();
    const float makeupDb   = [&] {
        auto* p = parameters.getParameter(kMakeupGain.data());
        return p ? p->convertFrom0to1(p->getValue()) : 0.0f;
    }();

    const bool anyListen = noiseListen || reverbListen || clarityListen || clickListen;

    auto bufRms = [](const juce::AudioBuffer<float>& b) -> float {
        double sum = 0.0;
        int total = 0;
        for (int ch = 0; ch < b.getNumChannels(); ++ch) {
            const float* d = b.getReadPointer(ch);
            for (int i = 0; i < b.getNumSamples(); ++i) { sum += d[i] * d[i]; ++total; }
        }
        return total > 0 ? static_cast<float>(std::sqrt(sum / total)) : 0.0f;
    };

    // Denoiser opts  -  scale protection inversely with strength:
    //   low strength → high guard, low aggression (surgical)
    //   high strength → lower guard, higher aggression (heavy removal)
    vxsuite::ProcessOptions noiseOpts {};
    noiseOpts.isVoiceMode        = true;
    noiseOpts.sourceProtect      = 0.85f - 0.25f * noiseStr;   // 0.85 → 0.60
    noiseOpts.guardStrictness    = 0.85f - 0.30f * noiseStr;   // 0.85 → 0.55
    noiseOpts.lateTailAggression = 0.35f + 0.40f * noiseStr;   // 0.35 → 0.75
    noiseOpts.speechFocus        = 0.75f;

    // Deverb opts  -  same adaptive philosophy
    vxsuite::ProcessOptions reverbOpts {};
    reverbOpts.isVoiceMode        = true;
    reverbOpts.sourceProtect      = 0.82f - 0.22f * reverbStr;  // 0.82 → 0.60
    reverbOpts.guardStrictness    = 0.80f - 0.25f * reverbStr;  // 0.80 → 0.55
    reverbOpts.lateTailAggression = 0.35f + 0.35f * reverbStr;  // 0.35 → 0.70
    reverbOpts.speechFocus        = 0.82f;

    auto applyNoiseDsp = [&](juce::AudioBuffer<float>& buf, float str) {
        if (useDeepFilter)
            dfService.processRealtime(buf, currentSampleRate, str, 0);
        else
            denoiserDsp.processInPlace(buf, str, noiseOpts);
    };

    using vxsuite::speech_clarity::DeEsserDsp;
    using vxsuite::speech_clarity::DePolosiveDsp;
    using vxsuite::speech_clarity::DeBreathDsp;

    detectClarityIntensities(buffer, numSamples);
    const float sibI   = sibilanceIntensity.load(std::memory_order_relaxed);
    const float plosI  = plosiveIntensity.load(std::memory_order_relaxed);
    const float breaI  = breathIntensity.load(std::memory_order_relaxed);
    const float clickI = clickIntensity.load(std::memory_order_relaxed);

    // Advance noisePassthroughDelay with the raw input on every block.
    // Reads the noiseLat-old signal into stftStateScratch for use in the
    // !noiseOn non-listen path, giving latency-correct transparent pass-through.
    {
        const int nCh  = buffer.getNumChannels();
        const int dLen = noisePassthroughDelay.getNumSamples();
        if (noisePassthroughDelay.getNumChannels() >= nCh && dLen > 0
            && stftStateScratch.getNumChannels() >= nCh
            && stftStateScratch.getNumSamples() >= numSamples)
        {
            for (int ch = 0; ch < nCh; ++ch) {
                int p = noisePassthroughPos;
                const float* src  = buffer.getReadPointer(ch);
                float*       ring = noisePassthroughDelay.getWritePointer(ch);
                float*       out  = stftStateScratch.getWritePointer(ch);
                for (int i = 0; i < numSamples; ++i) {
                    out[i]  = ring[p];
                    ring[p] = src[i];
                    p = (p + 1 == dLen) ? 0 : p + 1;
                }
            }
            noisePassthroughPos = (noisePassthroughPos + numSamples) % dLen;
        }
    }

    if (anyListen) {
        const int numCh = buffer.getNumChannels();

        // Copy input into scratch so idle STFT stages can advance their state
        // without disturbing the main buffer.
        if (stftStateScratch.getNumChannels() >= numCh
            && stftStateScratch.getNumSamples() >= numSamples)
        {
            for (int ch = 0; ch < numCh; ++ch)
                stftStateScratch.copyFrom(ch, 0, buffer, ch, 0, numSamples);
        }

        auto pushDry = [&](juce::AudioBuffer<float>& delayBuf, int& pos) {
            const int delayLen  = delayBuf.getNumSamples();
            const int startPos  = pos;
            juce::AudioBuffer<float> delayedDry(numCh, numSamples);
            for (int ch = 0; ch < numCh; ++ch) {
                int p            = startPos;
                const float* src = buffer.getReadPointer(ch);
                float*       ring = delayBuf.getWritePointer(ch);
                float*       dst  = delayedDry.getWritePointer(ch);
                for (int i = 0; i < numSamples; ++i) {
                    dst[i]  = ring[p];
                    ring[p] = src[i];
                    p = (p + 1) % delayLen;
                }
            }
            pos = (startPos + numSamples) % delayLen;
            return delayedDry;
        };

        if (noiseListen) {
            auto delayed = pushDry(noiseDryDelay, noiseDryDelayPos);
            applyNoiseDsp(buffer, noiseStr);
            // Advance deverb state using scratch so it stays in sync.
            juce::AudioBuffer<float> scratchView(stftStateScratch.getArrayOfWritePointers(), numCh, numSamples);
            deverbDsp.processInPlace(scratchView, 0.0f, reverbOpts);
            for (int ch = 0; ch < numCh; ++ch) {
                auto* out = buffer.getWritePointer(ch);
                const float* dry = delayed.getReadPointer(ch);
                for (int i = 0; i < numSamples; ++i) out[i] = dry[i] - out[i];
            }
        } else if (reverbListen) {
            auto delayed = pushDry(reverbDryDelay, reverbDryDelayPos);
            // Advance denoiser state using scratch so it stays in sync.
            juce::AudioBuffer<float> scratchView(stftStateScratch.getArrayOfWritePointers(), numCh, numSamples);
            denoiserDsp.processInPlace(scratchView, 0.0f, noiseOpts);
            const float reduce = std::pow(reverbStr, 0.76f);
            deverbDsp.processInPlace(buffer, reduce, reverbOpts);
            for (int ch = 0; ch < numCh; ++ch) {
                auto* out = buffer.getWritePointer(ch);
                const float* dry = delayed.getReadPointer(ch);
                for (int i = 0; i < numSamples; ++i) out[i] = dry[i] - out[i];
            }
        } else if (clarityListen) {
            // Neither STFT stage is part of clarity listen — advance both for state.
            // Use two separate scratch buffers (both pre-filled from input above) so
            // denoiser output does not pollute the deverb state-advance pass.
            juce::AudioBuffer<float> scratchView (stftStateScratch.getArrayOfWritePointers(),  numCh, numSamples);
            juce::AudioBuffer<float> scratch2View(stftStateScratch2.getArrayOfWritePointers(), numCh, numSamples);
            for (int ch = 0; ch < numCh; ++ch)
                stftStateScratch2.copyFrom(ch, 0, buffer, ch, 0, numSamples);
            denoiserDsp.processInPlace(scratchView,  0.0f, noiseOpts);
            deverbDsp.processInPlace  (scratch2View, 0.0f, reverbOpts);
            // Advance click state at 0 strength so its ring buffer stays in sync.
            deClickDsp.process(buffer, { 0.0f, 0.0f });
            juce::AudioBuffer<float> dryCopy;
            dryCopy.makeCopyOf(buffer);
            dePlosiveDsp.process(buffer, { clarityStr, plosI });
            deEsserDsp.process  (buffer, { clarityStr, sibI  });
            deBreathDsp.process (buffer, { clarityStr, breaI });
            for (int ch = 0; ch < numCh; ++ch) {
                auto* out = buffer.getWritePointer(ch);
                const float* dry = dryCopy.getReadPointer(ch);
                for (int i = 0; i < numSamples; ++i) out[i] = dry[i] - out[i];
            }
        } else if (clickListen) {
            // Advance both STFT stages on scratch so they stay in sync.
            juce::AudioBuffer<float> scratchView (stftStateScratch.getArrayOfWritePointers(),  numCh, numSamples);
            juce::AudioBuffer<float> scratch2View(stftStateScratch2.getArrayOfWritePointers(), numCh, numSamples);
            for (int ch = 0; ch < numCh; ++ch) {
                stftStateScratch.copyFrom (ch, 0, buffer, ch, 0, numSamples);
                stftStateScratch2.copyFrom(ch, 0, buffer, ch, 0, numSamples);
            }
            denoiserDsp.processInPlace(scratchView,  0.0f, noiseOpts);
            deverbDsp.processInPlace  (scratch2View, 0.0f, reverbOpts);
            auto delayed = pushDry(clickDryDelay, clickDryDelayPos);
            deClickDsp.process(buffer, { clickStr, clickI });
            for (int ch = 0; ch < numCh; ++ch) {
                auto* out = buffer.getWritePointer(ch);
                const float* dry = delayed.getReadPointer(ch);
                for (int i = 0; i < numSamples; ++i) out[i] = dry[i] - out[i];
            }
        }
        return;
    }

    // ── Repair chain: Noise → Speech Clarity → Reverb ────────────────────────
    // Denoiser and deverb STFT stages always run (at 0 strength when disabled)
    // so the reported latency stays constant regardless of what is toggled on.

    if (useDeepFilter && noiseOn) {
        // DeepFilter replaces the denoiser STFT; its latency is handled separately.
        const float preRms = bufRms(buffer);
        dfService.processRealtime(buffer, currentSampleRate, noiseStr, 0);
        const float wetRms = bufRms(buffer);
        const float gr = preRms > 1.0e-6f ? juce::jlimit(0.0f, 1.0f, 1.0f - wetRms / preRms) : 0.0f;
        noiseActivity.store(0.88f * noiseActivity.load(std::memory_order_relaxed) + 0.12f * gr, std::memory_order_relaxed);
    } else {
        if (noiseOn) {
            const float preRms = bufRms(buffer);
            denoiserDsp.processInPlace(buffer, noiseStr, noiseOpts);
            const float wetRms = bufRms(buffer);
            const float gr = preRms > 1.0e-6f ? juce::jlimit(0.0f, 1.0f, 1.0f - wetRms / preRms) : 0.0f;
            noiseActivity.store(0.88f * noiseActivity.load(std::memory_order_relaxed) + 0.12f * gr, std::memory_order_relaxed);
        } else {
            // Advance STFT state; restore the correctly-delayed dry signal from
            // noisePassthroughDelay (written to stftStateScratch above).
            const int nCh = buffer.getNumChannels();
            denoiserDsp.processInPlace(buffer, 0.0f, noiseOpts);
            for (int ch = 0; ch < nCh; ++ch)
                buffer.copyFrom(ch, 0, stftStateScratch, ch, 0, numSamples);
            noiseActivity.store(noiseActivity.load(std::memory_order_relaxed) * 0.92f, std::memory_order_relaxed);
        }
    }

    // Click repair (always runs to maintain constant lookahead latency)
    if (clickOn) {
        deClickDsp.process(buffer, { clickStr, clickI });
        const float rep = juce::jlimit(0.0f, 1.0f,
            deClickDsp.getLastHardClickRepair() + deClickDsp.getLastMouthClickRepair());
        clickActivity.store(0.88f * clickActivity.load(std::memory_order_relaxed) + 0.12f * rep,
                            std::memory_order_relaxed);
    } else {
        deClickDsp.process(buffer, { 0.0f, 0.0f });
        clickActivity.store(clickActivity.load(std::memory_order_relaxed) * 0.92f,
                            std::memory_order_relaxed);
    }

    if (clarityOn) {
        dePlosiveDsp.process(buffer, { clarityStr, plosI });
        deEsserDsp.process  (buffer, { clarityStr, sibI  });
        deBreathDsp.process (buffer, { clarityStr, breaI });
        // Clarity tools act on narrow bands  -  broadband RMS barely moves.
        // Use detection intensity directly, same as the standalone VxClarity plugin.
        const float activity = clarityStr * std::max({ sibI, plosI, breaI });
        clarityActivity.store(0.88f * clarityActivity.load(std::memory_order_relaxed) + 0.12f * activity, std::memory_order_relaxed);
    } else {
        clarityActivity.store(clarityActivity.load(std::memory_order_relaxed) * 0.92f, std::memory_order_relaxed);
    }

    // Advance reverbPassthroughDelay with the post-denoiser/post-click/post-clarity buffer.
    // Reads the reverbLat-old signal into stftStateScratch for the !reverbOn path.
    {
        const int nCh  = buffer.getNumChannels();
        const int dLen = reverbPassthroughDelay.getNumSamples();
        if (reverbPassthroughDelay.getNumChannels() >= nCh && dLen > 0
            && stftStateScratch.getNumChannels() >= nCh
            && stftStateScratch.getNumSamples() >= numSamples)
        {
            for (int ch = 0; ch < nCh; ++ch) {
                int p = reverbPassthroughPos;
                const float* src  = buffer.getReadPointer(ch);
                float*       ring = reverbPassthroughDelay.getWritePointer(ch);
                float*       out  = stftStateScratch.getWritePointer(ch);
                for (int i = 0; i < numSamples; ++i) {
                    out[i]  = ring[p];
                    ring[p] = src[i];
                    p = (p + 1 == dLen) ? 0 : p + 1;
                }
            }
            reverbPassthroughPos = (reverbPassthroughPos + numSamples) % dLen;
        }
    }

    {
        if (reverbOn) {
            const float preRms = bufRms(buffer);
            const float reduce = std::pow(reverbStr, 0.76f);
            deverbDsp.processInPlace(buffer, reduce, reverbOpts);
            const float wetRms = bufRms(buffer);
            if (preRms > 1.0e-5f && wetRms > 1.0e-5f) {
                const float targetGain = juce::jlimit(1.0f, 1.6f, (preRms * 0.92f) / wetRms);
                smoothedReverbMakeup = 0.92f * smoothedReverbMakeup + 0.08f * targetGain;
                buffer.applyGain(smoothedReverbMakeup);
            }
            const float gr = preRms > 1.0e-6f ? juce::jlimit(0.0f, 1.0f, 1.0f - wetRms / preRms) : 0.0f;
            reverbActivity.store(0.88f * reverbActivity.load(std::memory_order_relaxed) + 0.12f * gr, std::memory_order_relaxed);
        } else {
            // Advance STFT state; restore the correctly-delayed post-denoiser signal
            // from reverbPassthroughDelay (written to stftStateScratch above).
            const int nCh = buffer.getNumChannels();
            deverbDsp.processInPlace(buffer, 0.0f, reverbOpts);
            for (int ch = 0; ch < nCh; ++ch)
                buffer.copyFrom(ch, 0, stftStateScratch, ch, 0, numSamples);
            smoothedReverbMakeup = 0.92f * smoothedReverbMakeup + 0.08f * 1.0f;
            reverbActivity.store(reverbActivity.load(std::memory_order_relaxed) * 0.92f, std::memory_order_relaxed);
        }
    }

    const bool anyActive = noiseOn || reverbOn || clarityOn || clickOn;
    const float makeupLinear = std::pow(10.0f, makeupDb / 20.0f);
    if (anyActive && std::abs(makeupLinear - 1.0f) > 0.001f)
        buffer.applyGain(makeupLinear);
}

#if !defined(VXSUITE_DISABLE_PLUGIN_ENTRYPOINT) && !defined(VXSTUDIO_DISABLE_PLUGIN_ENTRYPOINT)
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new VXRepairAudioProcessor();
}
#endif
