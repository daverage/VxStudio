#include "../Source/vxstudio/products/repair/VxRepairProcessor.h"
#include "VxStudioProcessorTestUtils.h"
#include <iostream>
#include <cmath>

using namespace vxsuite::test;

namespace {

juce::AudioBuffer<float> makeNoisySpeech(double sr, float seconds) {
    const int n = static_cast<int>(sr * seconds);
    juce::AudioBuffer<float> buf(2, n);
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> noise(-1.0f, 1.0f);
    for (int i = 0; i < n; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(sr);
        const float v = 0.40f * std::sin(2.0f * juce::MathConstants<float>::pi * 200.0f * t)
                      + 0.15f * std::sin(2.0f * juce::MathConstants<float>::pi * 600.0f * t);
        const float ns = 0.08f * noise(rng);
        buf.setSample(0, i, v + ns);
        buf.setSample(1, i, v * 0.98f + ns * 0.96f);
    }
    return buf;
}

juce::AudioBuffer<float> makeReverberant(double sr, float seconds) {
    const int n = static_cast<int>(sr * seconds);
    juce::AudioBuffer<float> buf(2, n);
    std::mt19937 rng(99);
    std::uniform_real_distribution<float> nd(-1.0f, 1.0f);
    for (int i = 0; i < n; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(sr);
        const float v = 0.35f * std::sin(2.0f * juce::MathConstants<float>::pi * 180.0f * t);
        const float tail = 0.30f * nd(rng) * std::exp(-6.9f * t / 1.0f);
        buf.setSample(0, i, v + tail);
        buf.setSample(1, i, v * 0.98f + tail * 0.95f);
    }
    return buf;
}

void setParam(VXRepairAudioProcessor& p, const char* id, float v) {
    if (auto* param = p.getValueTreeState().getParameter(id))
        param->setValueNotifyingHost(v);
}

} // namespace

int main() {
    constexpr double sr = 48000.0;
    std::cout << "=== VXRepair Passthrough Latency Check ===\n";
    bool allPass = true;

    // --- Test 1: all off, sample-accurate pass-through ---
    {
        VXRepairAudioProcessor proc;
        proc.prepareToPlay(sr, 256);
        std::cout << "  Reported latency: " << proc.getLatencySamples()
                  << " samples (" << proc.getLatencySamples() / 48.0f << " ms)\n";

        auto input = makeSpeechLike(sr, 3.0f);
        render(proc, input, 256);           // warm up ring buffers
        auto out = render(proc, input, 256);

        float diff = maxAbsDiff(input, out);
        bool ok = diff < 1e-4f;
        allPass &= ok;
        std::cout << "  [all off — sample-accurate] maxAbsDiff=" << diff
                  << (ok ? "  PASS" : "  FAIL (latency still mismatched)") << "\n";
    }

    // --- Test 2: stages-on still processes (not accidentally identity) ---
    {
        VXRepairAudioProcessor proc;
        proc.prepareToPlay(sr, 256);
        setParam(proc, "noise_on",  1.0f);
        setParam(proc, "reverb_on", 1.0f);

        auto input = makeSpeechLike(sr, 3.0f);
        render(proc, input, 256);
        auto out = render(proc, input, 256);

        float diff = maxAbsDiff(input, out);
        bool ok = diff > 1e-3f;
        allPass &= ok;
        std::cout << "  [both on — processing active] maxAbsDiff=" << diff
                  << (ok ? "  PASS" : "  FAIL") << "\n";
    }

    // --- Test 3: noiseOn=false, reverbOn=true — mixed path ---
    {
        VXRepairAudioProcessor proc;
        proc.prepareToPlay(sr, 256);
        setParam(proc, "reverb_on",      1.0f);
        setParam(proc, "reverb_strength",1.0f);

        auto input = makeReverberant(sr, 3.0f);
        render(proc, input, 256);
        auto out = render(proc, input, 256);

        float rmsIn  = rms(input);
        float rmsOut = rms(out);
        float ratio  = rmsIn > 1e-6f ? rmsOut / rmsIn : 1.0f;
        bool ok = ratio < 0.90f;
        allPass &= ok;
        std::cout << "  [noiseOff+reverbOn] rmsRatio=" << ratio
                  << (ok ? "  PASS (deverb active)" : "  FAIL") << "\n";
    }

    // --- Test 4: noiseOn=true, reverbOn=false — mixed path ---
    {
        VXRepairAudioProcessor proc;
        proc.prepareToPlay(sr, 256);
        setParam(proc, "noise_on",      1.0f);
        setParam(proc, "noise_strength",1.0f);

        auto input = makeNoisySpeech(sr, 3.0f);
        render(proc, input, 256);
        auto out = render(proc, input, 256);

        float rmsIn  = rms(input);
        float rmsOut = rms(out);
        float ratio  = rmsIn > 1e-6f ? rmsOut / rmsIn : 1.0f;
        bool ok = ratio < 0.90f;
        allPass &= ok;
        std::cout << "  [noiseOn+reverbOff] rmsRatio=" << ratio
                  << (ok ? "  PASS (denoiser active)" : "  FAIL") << "\n";
    }

    // --- Test 5: reverbListen outputs non-trivial delta at strength=1 ---
    // The listen path computes dry[t-reverbLat] - deverb(dry[t-reverbLat]).
    // On reverberant audio, the removed reverb tail should have measurable energy.
    {
        VXRepairAudioProcessor proc;
        proc.prepareToPlay(sr, 256);
        setParam(proc, "reverb_listen",  1.0f);
        setParam(proc, "reverb_strength",1.0f);

        auto input = makeReverberant(sr, 3.0f);
        render(proc, input, 256);    // warm up STFT
        auto out = render(proc, input, 256);

        const float rmsIn  = rms(input);
        const float rmsOut = rms(out);
        // Delta should be non-trivial (> 2% of input) and not full signal (< 95%)
        bool ok = rmsOut > 0.02f * rmsIn && rmsOut < 0.95f * rmsIn;
        allPass &= ok;
        std::cout << "  [reverbListen str=1 delta] rmsRatio=" << (rmsOut / rmsIn)
                  << (ok ? "  PASS (non-trivial delta)" : "  FAIL") << "\n";
    }

    // --- Test 6: reverbListen delta ≈ 0 at strength=0 (nothing removed) ---
    {
        VXRepairAudioProcessor proc;
        proc.prepareToPlay(sr, 256);
        setParam(proc, "reverb_listen",  1.0f);
        setParam(proc, "reverb_strength",0.0f);

        auto input = makeReverberant(sr, 3.0f);
        render(proc, input, 256);
        auto out = render(proc, input, 256);

        const float rmsIn  = rms(input);
        const float rmsOut = rms(out);
        bool ok = rmsOut < 0.05f * rmsIn;
        allPass &= ok;
        std::cout << "  [reverbListen str=0 delta] rmsRatio=" << (rmsOut / rmsIn)
                  << (ok ? "  PASS (silent when nothing removed)" : "  FAIL") << "\n";
    }

    std::cout << "\n=== " << (allPass ? "ALL PASS" : "SOME TESTS FAILED") << " ===\n";
    return allPass ? 0 : 1;
}
