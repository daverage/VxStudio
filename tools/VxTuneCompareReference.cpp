#include "../Source/vxstudio/products/tune/dsp/VxTuneDecomposition.h"
#include "../Source/vxstudio/products/tune/dsp/VxTunePitchDetector.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

namespace {

using vxsuite::tune::PerformanceDecomposition;
using vxsuite::tune::PitchDetector;
using vxsuite::tune::PitchFrame;
using vxsuite::tune::PitchObservation;

struct AudioFile {
    juce::AudioBuffer<float> buffer;
    double sampleRate = 0.0;
};

struct Track {
    std::string name;
    std::vector<PitchFrame> frames;
};

bool readAudio(const juce::File& file, AudioFile& out) {
    juce::AudioFormatManager manager;
    manager.registerBasicFormats();
    auto reader = std::unique_ptr<juce::AudioFormatReader>(manager.createReaderFor(file));
    if (!reader)
        return false;
    out.sampleRate = reader->sampleRate;
    out.buffer.setSize(static_cast<int>(std::min<juce::uint32>(reader->numChannels, 2)),
                       static_cast<int>(reader->lengthInSamples));
    return reader->read(&out.buffer, 0, out.buffer.getNumSamples(), 0, true, true);
}

std::vector<float> monoMix(const juce::AudioBuffer<float>& buffer) {
    std::vector<float> mono(static_cast<size_t>(buffer.getNumSamples()), 0.0f);
    const int channels = std::max(1, buffer.getNumChannels());
    for (int ch = 0; ch < channels; ++ch) {
        const float* p = buffer.getReadPointer(ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            mono[static_cast<size_t>(i)] += p[i] / static_cast<float>(channels);
    }
    return mono;
}

std::vector<PitchFrame> analyse(const juce::AudioBuffer<float>& buffer, const double sr) {
    PitchDetector detector;
    detector.prepare(sr, PitchDetector::Config {});
    PerformanceDecomposition decomp;
    decomp.prepare(sr / detector.hopSamples(), PerformanceDecomposition::Config {});
    const auto mono = monoMix(buffer);

    std::vector<PitchFrame> frames;
    PitchObservation obs[64];
    constexpr int block = 512;
    for (size_t offset = 0; offset < mono.size(); offset += block) {
        const int n = static_cast<int>(std::min<size_t>(block, mono.size() - offset));
        const int produced = detector.process(mono.data() + offset, n, obs, 64);
        for (int i = 0; i < produced; ++i)
            frames.push_back(decomp.process(obs[i]));
    }
    return frames;
}

double hzToCents(const double hz) {
    return 1200.0 * std::log2(hz / 440.0);
}

double nearestChromaticError(const double cents) {
    return cents - std::round(cents / 100.0) * 100.0;
}

double median(std::vector<double> v) {
    if (v.empty())
        return 0.0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

double meanAbsDiffAligned(const AudioFile& a, const AudioFile& b) {
    const int n = std::min(a.buffer.getNumSamples(), b.buffer.getNumSamples());
    const int chs = std::min(a.buffer.getNumChannels(), b.buffer.getNumChannels());
    if (n <= 0 || chs <= 0)
        return 0.0;
    double sum = 0.0;
    int count = 0;
    for (int ch = 0; ch < chs; ++ch) {
        const float* ap = a.buffer.getReadPointer(ch);
        const float* bp = b.buffer.getReadPointer(ch);
        for (int i = 0; i < n; ++i) {
            sum += std::abs(static_cast<double>(ap[i] - bp[i]));
            ++count;
        }
    }
    return count > 0 ? sum / static_cast<double>(count) : 0.0;
}

void writeTrackCsv(const juce::File& file, const std::vector<Track>& tracks) {
    std::ofstream out(file.getFullPathName().toStdString());
    out << "track,frame,seconds,f0_hz,confidence,centre_cents,residual_cents,detected_cents,nearest_error_cents\n";
    for (const auto& track : tracks) {
        for (size_t i = 0; i < track.frames.size(); ++i) {
            const auto& f = track.frames[i];
            const double detected = f.f0Hz.value > 0.0 ? hzToCents(f.f0Hz.value) : 0.0;
            out << track.name << ',' << i << ','
                << (track.frames[i].timeSamples / 48000.0) << ','
                << f.f0Hz.value << ','
                << f.f0Hz.confidence << ','
                << f.centreCents << ','
                << f.residualCents << ','
                << detected << ','
                << (f.f0Hz.value > 0.0 ? nearestChromaticError(detected) : 0.0)
                << '\n';
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 6) {
        std::cerr << "Usage: VxTuneCompareReference <dry.wav> <reatune.wav> <vx.wav> <out.csv> <summary.md>\n";
        return 2;
    }

    AudioFile dry, reference, vx;
    if (!readAudio(juce::File(argv[1]), dry)
        || !readAudio(juce::File(argv[2]), reference)
        || !readAudio(juce::File(argv[3]), vx)) {
        std::cerr << "Could not read one or more audio files\n";
        return 1;
    }

    std::vector<Track> tracks {
        { "dry", analyse(dry.buffer, dry.sampleRate) },
        { "reatune", analyse(reference.buffer, reference.sampleRate) },
        { "vxtune", analyse(vx.buffer, vx.sampleRate) },
    };
    writeTrackCsv(juce::File(argv[4]), tracks);

    const size_t n = std::min({ tracks[0].frames.size(), tracks[1].frames.size(),
                                tracks[2].frames.size() });
    std::vector<double> reatuneMove, vxMove, vxVsReatune, dryErr, refErr, vxErr;
    for (size_t i = 0; i < n; ++i) {
        const auto& d = tracks[0].frames[i];
        const auto& r = tracks[1].frames[i];
        const auto& v = tracks[2].frames[i];
        if (d.f0Hz.confidence < 0.65f || r.f0Hz.confidence < 0.65f || v.f0Hz.confidence < 0.65f)
            continue;
        const double dc = hzToCents(d.f0Hz.value);
        const double rc = hzToCents(r.f0Hz.value);
        const double vc = hzToCents(v.f0Hz.value);
        reatuneMove.push_back(rc - dc);
        vxMove.push_back(vc - dc);
        vxVsReatune.push_back(vc - rc);
        dryErr.push_back(std::abs(nearestChromaticError(dc)));
        refErr.push_back(std::abs(nearestChromaticError(rc)));
        vxErr.push_back(std::abs(nearestChromaticError(vc)));
    }

    std::ofstream summary(argv[5]);
    summary << "# VX Tune vs ReaTune Reference\n\n";
    summary << "- analysed voiced frames: " << vxVsReatune.size() << "\n";
    summary << "- dry median abs chromatic error: " << median(dryErr) << "c\n";
    summary << "- ReaTune median abs chromatic error: " << median(refErr) << "c\n";
    summary << "- VX Tune median abs chromatic error: " << median(vxErr) << "c\n";
    summary << "- ReaTune median movement vs dry: " << median(reatuneMove) << "c\n";
    summary << "- VX Tune median movement vs dry: " << median(vxMove) << "c\n";
    summary << "- VX Tune median offset vs ReaTune: " << median(vxVsReatune) << "c\n";
    summary << "- mean absolute audio delta dry->ReaTune: " << meanAbsDiffAligned(dry, reference) << "\n";
    summary << "- mean absolute audio delta dry->VX Tune: " << meanAbsDiffAligned(dry, vx) << "\n";
    summary << "- mean absolute audio delta VX Tune->ReaTune: " << meanAbsDiffAligned(vx, reference) << "\n";
    return 0;
}
