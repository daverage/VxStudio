#include "VxTunePitchRenderer.h"

#include <cassert>
#include <cmath>

namespace vxsuite::tune {
namespace {
constexpr int kResetFadeSamples = 256;
constexpr int kResetOnsetFadeSamples = 2048;

const char* profileName(const VxTunePitchRenderer::QualityProfile profile) noexcept {
    switch (profile) {
        case VxTunePitchRenderer::QualityProfile::Live:
            return "Live";
        case VxTunePitchRenderer::QualityProfile::Studio:
        default:
            return "Studio";
    }
}
}

void SignalsmithPitchRenderer::prepare(const PrepareSpec& spec) {
    sampleRate = spec.sampleRate;
    channelsPrepared = std::max(1, spec.numChannels);
    qualityProfile = spec.qualityProfile;
    if (qualityProfile == QualityProfile::Live)
        stretch.presetCheaper(channelsPrepared, static_cast<float>(sampleRate), true);
    else
        stretch.presetDefault(channelsPrepared, static_cast<float>(sampleRate), true);
    stretch.setFormantFactor(1.0f, false);
    reset();

    inputScratch.assign(static_cast<size_t>(channelsPrepared), {});
    outputScratch.assign(static_cast<size_t>(channelsPrepared), {});
    for (int ch = 0; ch < channelsPrepared; ++ch) {
        inputScratch[static_cast<size_t>(ch)].assign(
            static_cast<size_t>(std::max(1, spec.maximumBlockSize)), 0.0f);
        outputScratch[static_cast<size_t>(ch)].assign(
            static_cast<size_t>(std::max(1, spec.maximumBlockSize)), 0.0f);
    }
    inputPtrs.assign(static_cast<size_t>(channelsPrepared), nullptr);
    outputPtrs.assign(static_cast<size_t>(channelsPrepared), nullptr);
    lastOutputSamples.assign(static_cast<size_t>(channelsPrepared), 0.0f);
    resetStartSamples.assign(static_cast<size_t>(channelsPrepared), 0.0f);
    resetEdgeFadeRemaining = 0;
    resetOnsetFadeRemaining = 0;
    resetAwaitingSignal = false;
}

void SignalsmithPitchRenderer::reset() {
    stretch.reset();
    resetStartSamples = lastOutputSamples;
    resetEdgeFadeRemaining = lastOutputSamples.empty() ? 0 : kResetFadeSamples;
    resetOnsetFadeRemaining = 0;
    resetAwaitingSignal = !lastOutputSamples.empty();
    requestedCents = 0.0f;
    appliedCents = 0.0f;
    lastF0Hz = 0.0f;
    lastConfidence = 0.0f;
    lastVoiced = false;
}

void SignalsmithPitchRenderer::setFrameInfo(const FrameInfo& info) {
    requestedCents = info.correctionCents;
    lastF0Hz = info.detectedF0Hz;
    lastConfidence = info.pitchConfidence;
    lastVoiced = info.voiced;
    stretch.setFormantBase(info.voiced && info.detectedF0Hz > 0.0f
        ? info.detectedF0Hz / static_cast<float>(sampleRate) : 0.0f);
}

void SignalsmithPitchRenderer::process(const float* const* input, float* const* output,
                                       const int numChannels, const int numSamples,
                                       const float startCents, const float endCents) {
    const int usedChannels = std::min(numChannels, channelsPrepared);
    if (usedChannels <= 0 || numSamples <= 0)
        return;

    // Scratch buffers are sized to PrepareSpec::maximumBlockSize in prepare().
    // A host handing us a larger block than it negotiated would force an
    // allocation here, on the audio thread - fail loudly in debug builds and
    // clamp in release instead of silently growing.
    assert(static_cast<int>(inputScratch[0].size()) >= numSamples
        && static_cast<int>(outputScratch[0].size()) >= numSamples);
    const int clampedSamples = std::min(numSamples, static_cast<int>(inputScratch[0].size()));

    constexpr int kControlChunk = 64;
    int offset = 0;
    while (offset < clampedSamples) {
        const int n = std::min(kControlChunk, clampedSamples - offset);
        const float t = clampedSamples > 1
            ? static_cast<float>(offset) / static_cast<float>(clampedSamples - 1)
            : 1.0f;
        appliedCents = startCents + t * (endCents - startCents);
        stretch.setTransposeSemitones(appliedCents / 100.0f);

        for (int ch = 0; ch < usedChannels; ++ch) {
            auto& in = inputScratch[static_cast<size_t>(ch)];
            auto& out = outputScratch[static_cast<size_t>(ch)];
            std::copy(input[ch] + offset, input[ch] + offset + n, in.begin());
            std::fill(out.begin(), out.begin() + n, 0.0f);
            inputPtrs[static_cast<size_t>(ch)] = in.data();
            outputPtrs[static_cast<size_t>(ch)] = out.data();
        }
        stretch.process(inputPtrs.data(), n, outputPtrs.data(), n);
        if (resetEdgeFadeRemaining > 0 || resetAwaitingSignal || resetOnsetFadeRemaining > 0) {
            for (int i = 0; i < n; ++i) {
                float rawPeak = 0.0f;
                for (int ch = 0; ch < usedChannels; ++ch) {
                    auto& out = outputScratch[static_cast<size_t>(ch)];
                    rawPeak = std::max(rawPeak, std::abs(out[static_cast<size_t>(i)]));
                }

                if (resetEdgeFadeRemaining > 0) {
                    const float alpha =
                        static_cast<float>(kResetFadeSamples - resetEdgeFadeRemaining + 1)
                            / static_cast<float>(kResetFadeSamples);
                    for (int ch = 0; ch < usedChannels; ++ch) {
                        auto& out = outputScratch[static_cast<size_t>(ch)];
                        const float start = ch < static_cast<int>(resetStartSamples.size())
                            ? resetStartSamples[static_cast<size_t>(ch)] : 0.0f;
                        out[static_cast<size_t>(i)] = start * (1.0f - alpha);
                    }
                    --resetEdgeFadeRemaining;
                    continue;
                }

                if (resetAwaitingSignal) {
                    if (rawPeak > 2.0e-3f) {
                        resetAwaitingSignal = false;
                        resetOnsetFadeRemaining = kResetOnsetFadeSamples;
                    } else {
                        for (int ch = 0; ch < usedChannels; ++ch)
                            outputScratch[static_cast<size_t>(ch)][static_cast<size_t>(i)] = 0.0f;
                        continue;
                    }
                }

                if (resetOnsetFadeRemaining > 0) {
                    const float gain =
                        static_cast<float>(kResetOnsetFadeSamples - resetOnsetFadeRemaining + 1)
                            / static_cast<float>(kResetOnsetFadeSamples);
                    for (int ch = 0; ch < usedChannels; ++ch)
                        outputScratch[static_cast<size_t>(ch)][static_cast<size_t>(i)] *= gain;
                    --resetOnsetFadeRemaining;
                }
            }
        }
        for (int ch = 0; ch < usedChannels; ++ch) {
            const auto& out = outputScratch[static_cast<size_t>(ch)];
            std::copy(out.begin(), out.begin() + n, output[ch] + offset);
            if (!out.empty())
                lastOutputSamples[static_cast<size_t>(ch)] =
                    output[ch][offset + n - 1];
        }
        offset += n;
    }
    appliedCents = endCents;
}

int SignalsmithPitchRenderer::latencySamples() const noexcept {
    return stretch.inputLatency() + stretch.outputLatency();
}

VxTunePitchRenderer::DebugSnapshot SignalsmithPitchRenderer::debugSnapshot() const noexcept {
    return { requestedCents, appliedCents, 1.0f, 0.0f, lastConfidence,
             lastVoiced ? 1 : 0, std::abs(appliedCents) < 0.5f ? 1 : 0, 0, 0,
             backendName(), profileName(qualityProfile), latencySamples(),
             stretch.blockSamples(), stretch.intervalSamples(),
             stretch.splitComputation() ? 1 : 0 };
}

std::unique_ptr<VxTunePitchRenderer> createPitchRenderer(
    const VxTunePitchRenderer::Backend backend) {
    switch (backend) {
        case VxTunePitchRenderer::Backend::Signalsmith:
        default:
            return std::make_unique<SignalsmithPitchRenderer>();
    }
}

} // namespace vxsuite::tune
