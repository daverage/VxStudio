// VX Tune Phase 2 renderer acceptance tests.
// These are backend-neutral contracts for the production pitch renderer:
// stable audio, explicit latency, dynamic pitch envelopes, and no obvious
// discontinuities. They intentionally do not exercise the live detector or
// correction engine.

#include "../Source/vxstudio/products/tune/dsp/VxTunePitchDetector.h"
#include "../Source/vxstudio/products/tune/dsp/VxTunePitchRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <numeric>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(const bool condition, const std::string& label) {
    std::printf("  %s  %s\n", condition ? "PASS" : "FAIL", label.c_str());
    if (!condition)
        ++failures;
}

constexpr double kSampleRate = 48000.0;
constexpr int kBlock = 256;
constexpr float kTwoPi = 6.28318530717958647692f;

float hzToCents(const float hz) {
    return 1200.0f * std::log2(hz / 440.0f);
}

std::vector<float> renderTone(const float seconds, const float hz,
                              const float amplitude = 0.2f) {
    const int samples = static_cast<int>(seconds * kSampleRate);
    std::vector<float> audio(static_cast<size_t>(samples), 0.0f);
    double phase = 0.0;
    for (int i = 0; i < samples; ++i) {
        phase += kTwoPi * hz / static_cast<float>(kSampleRate);
        float s = 0.0f;
        for (int h = 1; h <= 4; ++h)
            s += std::sin(static_cast<float>(phase) * h) / static_cast<float>(h);
        audio[static_cast<size_t>(i)] = amplitude * s;
    }
    return audio;
}

struct RenderResult {
    std::vector<float> output;
    int latency = 0;
    vxsuite::tune::VxTunePitchRenderer::DebugSnapshot debug;
};

RenderResult renderWithEnvelope(const std::vector<float>& input,
                                const float startCents,
                                const float endCents,
                                const bool resetHalfway = false,
                                const vxsuite::tune::VxTunePitchRenderer::QualityProfile profile =
                                    vxsuite::tune::VxTunePitchRenderer::QualityProfile::Studio) {
    auto renderer = vxsuite::tune::createPitchRenderer(
        vxsuite::tune::VxTunePitchRenderer::Backend::Signalsmith);
    vxsuite::tune::VxTunePitchRenderer::PrepareSpec spec;
    spec.sampleRate = kSampleRate;
    spec.maximumBlockSize = kBlock;
    spec.numChannels = 1;
    spec.qualityProfile = profile;
    renderer->prepare(spec);

    RenderResult result;
    result.latency = renderer->latencySamples();
    result.output.assign(input.size(), 0.0f);

    for (size_t offset = 0; offset < input.size(); offset += kBlock) {
        const int n = static_cast<int>(std::min<size_t>(kBlock, input.size() - offset));
        const float a = input.size() > 1
            ? static_cast<float>(offset) / static_cast<float>(input.size() - 1)
            : 1.0f;
        const float b = input.size() > 1
            ? static_cast<float>(offset + static_cast<size_t>(n - 1))
                / static_cast<float>(input.size() - 1)
            : 1.0f;
        const float blockStart = startCents + a * (endCents - startCents);
        const float blockEnd = startCents + b * (endCents - startCents);

        if (resetHalfway && offset >= input.size() / 2 && offset < input.size() / 2 + kBlock)
            renderer->reset();

        vxsuite::tune::VxTunePitchRenderer::FrameInfo info;
        info.correctionCents = blockEnd;
        info.detectedF0Hz = 220.0f;
        info.pitchConfidence = 1.0f;
        info.voiced = true;
        renderer->setFrameInfo(info);

        const float* inChans[1] = { input.data() + offset };
        float* outChans[1] = { result.output.data() + offset };
        renderer->process(inChans, outChans, 1, n, blockStart, blockEnd);
    }
    result.debug = renderer->debugSnapshot();
    return result;
}

float medianDetectedCents(const std::vector<float>& audio, const float skipSeconds) {
    vxsuite::tune::PitchDetector detector;
    detector.prepare(kSampleRate, vxsuite::tune::PitchDetector::Config {});
    vxsuite::tune::PitchObservation observations[64];
    std::vector<float> cents;
    for (size_t offset = 0; offset < audio.size(); offset += kBlock) {
        const int n = static_cast<int>(std::min<size_t>(kBlock, audio.size() - offset));
        const int produced = detector.process(audio.data() + offset, n, observations, 64);
        for (int i = 0; i < produced; ++i) {
            const auto& o = observations[i];
            const float seconds = static_cast<float>(o.timeSamples / kSampleRate);
            if (seconds >= skipSeconds && o.f0Hz.value > 0.0f && o.f0Hz.confidence > 0.7f)
                cents.push_back(hzToCents(o.f0Hz.value));
        }
    }
    std::sort(cents.begin(), cents.end());
    return cents.empty() ? -10000.0f : cents[cents.size() / 2];
}

float maxDerivative(const std::vector<float>& audio, const float skipSeconds = 0.0f) {
    const size_t start = static_cast<size_t>(skipSeconds * kSampleRate);
    float m = 0.0f;
    for (size_t i = std::max<size_t>(1, start); i < audio.size(); ++i)
        m = std::max(m, std::abs(audio[i] - audio[i - 1]));
    return m;
}

float maxDerivativeRange(const std::vector<float>& audio, const float fromSeconds,
                         const float toSeconds) {
    const size_t start = static_cast<size_t>(fromSeconds * kSampleRate);
    const size_t end = std::min(audio.size(), static_cast<size_t>(toSeconds * kSampleRate));
    float m = 0.0f;
    for (size_t i = std::max<size_t>(1, start); i < end; ++i)
        m = std::max(m, std::abs(audio[i] - audio[i - 1]));
    return m;
}

std::pair<float, float> maxDerivativeRangeWithTime(const std::vector<float>& audio,
                                                   const float fromSeconds,
                                                   const float toSeconds) {
    const size_t start = static_cast<size_t>(fromSeconds * kSampleRate);
    const size_t end = std::min(audio.size(), static_cast<size_t>(toSeconds * kSampleRate));
    float m = 0.0f;
    float seconds = fromSeconds;
    for (size_t i = std::max<size_t>(1, start); i < end; ++i) {
        const float d = std::abs(audio[i] - audio[i - 1]);
        if (d > m) {
            m = d;
            seconds = static_cast<float>(i) / static_cast<float>(kSampleRate);
        }
    }
    return { m, seconds };
}

float peakAbs(const std::vector<float>& audio) {
    float peak = 0.0f;
    for (const float v : audio)
        peak = std::max(peak, std::abs(v));
    return peak;
}

bool allFinite(const std::vector<float>& audio) {
    for (const float v : audio)
        if (!std::isfinite(v))
            return false;
    return true;
}

void testLatencyAndStartup() {
    std::printf("Renderer latency/startup:\n");
    const auto input = renderTone(3.0f, 220.0f);
    const auto out = renderWithEnvelope(input, 0.0f, 0.0f);
    check(out.latency > 0 && out.latency < static_cast<int>(0.25 * kSampleRate),
          "reports sane latency (got " + std::to_string(out.latency) + ")");
    check(allFinite(out.output), "startup output is finite");
    check(peakAbs(out.output) < 1.2f * peakAbs(input),
          "startup has no level blowup");
    check(std::string(out.debug.backendName) == "Signalsmith",
          "debug reports backend name");
    check(std::string(out.debug.qualityProfileName) == "Studio",
          "debug reports Studio profile");
    check(out.debug.latencySamples == out.latency,
          "debug latency matches renderer latency");
    check(out.debug.blockSamples > 0 && out.debug.intervalSamples > 0,
          "debug reports renderer block and interval");
}

void testQualityProfiles() {
    std::printf("Renderer quality profiles:\n");
    const auto input = renderTone(3.0f, 220.0f);
    const auto studio = renderWithEnvelope(input, 25.0f, 25.0f, false,
        vxsuite::tune::VxTunePitchRenderer::QualityProfile::Studio);
    const auto live = renderWithEnvelope(input, 25.0f, 25.0f, false,
        vxsuite::tune::VxTunePitchRenderer::QualityProfile::Live);

    check(std::string(live.debug.qualityProfileName) == "Live",
          "debug reports Live profile");
    check(studio.debug.blockSamples != live.debug.blockSamples
              || studio.debug.intervalSamples != live.debug.intervalSamples
              || studio.latency != live.latency,
          "Live uses a distinct Signalsmith configuration");
    check(live.latency <= studio.latency,
          "Live latency is not higher than Studio (live " + std::to_string(live.latency)
              + ", studio " + std::to_string(studio.latency) + ")");

    const float liveMeasured = medianDetectedCents(live.output, 1.0f);
    const float expected = hzToCents(220.0f) + 25.0f;
    check(std::abs(liveMeasured - expected) < 10.0f,
          "Live profile tracks +25c shift (error "
              + std::to_string(liveMeasured - expected) + "c)");
}

void testFixedShiftTracking() {
    std::printf("Renderer fixed-shift tracking:\n");
    auto input = renderTone(4.0f, 220.0f);
    input.resize(input.size() + static_cast<size_t>(0.5 * kSampleRate), 0.0f);

    for (const float cents : { -25.0f, 0.0f, 25.0f }) {
        const auto out = renderWithEnvelope(input, cents, cents);
        const float measured = medianDetectedCents(out.output, 1.0f);
        const float expected = hzToCents(220.0f) + cents;
        check(std::abs(measured - expected) < 8.0f,
              "tracks " + std::to_string(cents) + "c shift (error "
                  + std::to_string(measured - expected) + "c)");
    }
}

void testRampContinuity() {
    std::printf("Renderer ramp continuity:\n");
    auto input = renderTone(4.0f, 220.0f);
    input.resize(input.size() + static_cast<size_t>(0.5 * kSampleRate), 0.0f);
    const auto rampUp = renderWithEnvelope(input, 0.0f, 50.0f);
    const auto rampAcross = renderWithEnvelope(input, 50.0f, -50.0f);
    const float dryDiff = maxDerivative(input, 0.5f);
    check(maxDerivative(rampUp.output, 0.5f) < 4.0f * dryDiff,
          "0c -> +50c ramp has no large discontinuity");
    check(maxDerivative(rampAcross.output, 0.5f) < 4.0f * dryDiff,
          "+50c -> -50c ramp has no large discontinuity");
}

void testResetMidNote() {
    std::printf("Renderer reset mid-note:\n");
    const auto input = renderTone(3.0f, 220.0f);
    const auto out = renderWithEnvelope(input, 25.0f, 25.0f, true);
    const float dryDiff = maxDerivative(input, 0.2f);
    const float resetAt = static_cast<float>(input.size() / 2) / static_cast<float>(kSampleRate);
    const auto wet = maxDerivativeRangeWithTime(out.output, resetAt - 0.02f, resetAt + 0.35f);
    const float wetDiff = wet.first;
    check(allFinite(out.output), "reset mid-note output is finite");
    check(wetDiff < 5.0f * dryDiff,
          "reset mid-note avoids a hard pop (max diff x"
              + std::to_string(wetDiff / std::max(dryDiff, 1.0e-9f))
              + " at " + std::to_string(wet.second - resetAt) + "s)");
}

} // namespace

int main() {
    std::printf("VX Tune renderer tests\n");
    testLatencyAndStartup();
    testQualityProfiles();
    testFixedShiftTracking();
    testRampContinuity();
    testResetMidNote();

    if (failures == 0) {
        std::printf("All VX Tune renderer tests passed\n");
        return 0;
    }
    std::printf("%d VX Tune renderer test(s) FAILED\n", failures);
    return 1;
}
