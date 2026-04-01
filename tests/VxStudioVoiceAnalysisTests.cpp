#include "vxsuite/framework/VxStudioVoiceAnalysis.h"

#include <cmath>
#include <iostream>

namespace {

juce::AudioBuffer<float> makeSilence(const int channels, const int samples) {
    juce::AudioBuffer<float> buf(channels, samples);
    buf.clear();
    return buf;
}

juce::AudioBuffer<float> makeSpeechBurst(const double sampleRate, const float seconds) {
    const int samples = static_cast<int>(sampleRate * seconds);
    juce::AudioBuffer<float> buf(2, samples);
    for (int i = 0; i < samples; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(sampleRate);
        const float sig = 0.5f * std::sin(2.0f * juce::MathConstants<float>::pi * 180.0f * t)
                        + 0.2f * std::sin(2.0f * juce::MathConstants<float>::pi * 900.0f * t);
        buf.setSample(0, i, sig);
        buf.setSample(1, i, sig * 0.99f);
    }
    return buf;
}

// Feed a buffer in blocks of blockSize, return the last snapshot.
vxsuite::VoiceAnalysisSnapshot feed(vxsuite::VoiceAnalysisState& state,
                                    const juce::AudioBuffer<float>& input,
                                    const int blockSize = 256) {
    vxsuite::VoiceAnalysisSnapshot snap;
    for (int start = 0; start < input.getNumSamples(); start += blockSize) {
        const int n = std::min(blockSize, input.getNumSamples() - start);
        juce::AudioBuffer<float> block(input.getNumChannels(), n);
        for (int ch = 0; ch < input.getNumChannels(); ++ch)
            block.copyFrom(ch, 0, input, ch, start, n);
        state.update(block, n);
        snap = state.snapshot();
    }
    return snap;
}

// --- Tests ---

bool testSilenceStability() {
    vxsuite::VoiceAnalysisState state;
    state.prepare(48000.0, 512);

    auto silence = makeSilence(2, static_cast<int>(48000.0 * 2.0f)); // 2 seconds
    const auto snap = feed(state, silence);

    if (snap.speechPresence > 0.05f) {
        std::cerr << "[VoiceAnalysis] silence should give near-zero presence, got: "
                  << snap.speechPresence << "\n";
        return false;
    }
    if (snap.transientRisk > 0.05f) {
        std::cerr << "[VoiceAnalysis] silence should give near-zero transientRisk, got: "
                  << snap.transientRisk << "\n";
        return false;
    }
    if (snap.tailLikelihood > 0.05f) {
        std::cerr << "[VoiceAnalysis] silence should give near-zero tailLikelihood, got: "
                  << snap.tailLikelihood << "\n";
        return false;
    }
    return true;
}

bool testResetBehavior() {
    vxsuite::VoiceAnalysisState state;
    state.prepare(48000.0, 512);

    // Feed speech, then reset, then check silence gives blank slate
    auto speech = makeSpeechBurst(48000.0, 0.5f);
    feed(state, speech);

    state.reset();

    auto snap = state.snapshot();
    if (snap.speechPresence != 0.0f || snap.protectVoice != 0.0f ||
        snap.speechBandEnergy != 0.0f || snap.transientRisk != 0.0f) {
        std::cerr << "[VoiceAnalysis] snapshot should be zero after reset\n";
        return false;
    }

    // After reset, a few blocks of silence should yield near-zero presence
    auto silence = makeSilence(2, 512);
    state.update(silence, 512);
    snap = state.snapshot();
    if (snap.speechPresence > 0.01f) {
        std::cerr << "[VoiceAnalysis] presence non-zero immediately after reset+silence: "
                  << snap.speechPresence << "\n";
        return false;
    }
    return true;
}

bool testSpeechBurstRisesPresence() {
    vxsuite::VoiceAnalysisState state;
    state.prepare(48000.0, 512);

    // Establish a noise floor with silence first
    auto silence = makeSilence(2, static_cast<int>(48000.0 * 0.5f));
    feed(state, silence);

    // Feed a sustained speech burst
    auto speech = makeSpeechBurst(48000.0, 1.0f);
    const auto snap = feed(state, speech);

    if (snap.speechPresence < 0.1f) {
        std::cerr << "[VoiceAnalysis] speech burst should raise presence, got: "
                  << snap.speechPresence << "\n";
        return false;
    }
    if (snap.protectVoice < 0.05f) {
        std::cerr << "[VoiceAnalysis] protectVoice should be > 0 during speech, got: "
                  << snap.protectVoice << "\n";
        return false;
    }
    if (snap.centerConfidence < 0.5f) {
        std::cerr << "[VoiceAnalysis] nearly-mono speech should yield high centerConfidence, got: "
                  << snap.centerConfidence << "\n";
        return false;
    }
    return true;
}

bool testSpeechEnergyShrinks() {
    // After speech stops (silence), presence should decay significantly within 1s.
    vxsuite::VoiceAnalysisState state;
    state.prepare(48000.0, 512);

    auto speech = makeSpeechBurst(48000.0, 1.0f);
    const auto snapDuring = feed(state, speech);

    auto silence = makeSilence(2, static_cast<int>(48000.0 * 1.0f));
    const auto snapAfter = feed(state, silence);

    if (snapAfter.speechBandEnergy >= snapDuring.speechBandEnergy) {
        std::cerr << "[VoiceAnalysis] speechBandEnergy should decay after speech stops: during="
                  << snapDuring.speechBandEnergy << " after=" << snapAfter.speechBandEnergy << "\n";
        return false;
    }
    return true;
}

} // namespace

int main() {
    bool ok = true;
    ok &= testSilenceStability();
    ok &= testResetBehavior();
    ok &= testSpeechBurstRisesPresence();
    ok &= testSpeechEnergyShrinks();
    return ok ? 0 : 1;
}
