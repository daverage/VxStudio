#include "../Source/vxsuite/products/cleanup/VxCleanupProcessor.h"
#include "../Source/vxsuite/products/deverb/VxDeverbProcessor.h"
#include "../Source/vxsuite/products/denoiser/VxDenoiserProcessor.h"
#include "../Source/vxsuite/products/finish/VxFinishProcessor.h"
#include "../Source/vxsuite/products/leveler/VxLevelerProcessor.h"
#include "../Source/vxsuite/products/OptoComp/VxOptoCompProcessor.h"
#include "../Source/vxsuite/products/proximity/VxProximityProcessor.h"
#include "../Source/vxsuite/products/subtract/VxSubtractProcessor.h"
#include "../Source/vxsuite/products/tone/VxToneProcessor.h"
#include "VxSuiteProcessorTestUtils.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numeric>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

using vxsuite::test::render;
using vxsuite::test::setParamNormalized;

namespace fs = std::filesystem;

struct AudioFileData {
    juce::File file;
    juce::AudioBuffer<float> buffer;
    double sampleRate = 48000.0;
};

struct Metrics {
    float inputSpreadDb = 0.0f;
    float outputSpreadDb = 0.0f;
    float spreadImprovementDb = 0.0f;
    float inputRmsDb = -100.0f;
    float outputRmsDb = -100.0f;
    float deltaRmsDb = -100.0f;
    float correlation = 0.0f;
    float speechBandCorrelation = 0.0f;
    float residualRatio = 0.0f;
    float targetCorrelation = 0.0f;
    float targetSpeechBandCorrelation = 0.0f;
    float targetResidualRatio = 0.0f;
    float outputPeakDbfs = -100.0f;
};

struct ProductSpec {
    std::string name;
    std::string slug;
    std::function<juce::AudioBuffer<float>(const juce::AudioBuffer<float>&, double)> prepareInput;
    std::function<juce::AudioBuffer<float>(const juce::AudioBuffer<float>&, const juce::AudioBuffer<float>&, double)> scoreReference;
    std::function<juce::AudioBuffer<float>(const juce::AudioBuffer<float>&, double)> process;
};

juce::AudioBuffer<float> addRoomTail(const juce::AudioBuffer<float>& dry, const double sampleRate) {
    juce::AudioBuffer<float> wet(dry);
    const std::array<int, 5> delays {
        static_cast<int>(0.018 * sampleRate),
        static_cast<int>(0.033 * sampleRate),
        static_cast<int>(0.055 * sampleRate),
        static_cast<int>(0.082 * sampleRate),
        static_cast<int>(0.117 * sampleRate)
    };
    const std::array<float, 5> gains { 0.44f, 0.31f, 0.24f, 0.18f, 0.12f };
    for (int ch = 0; ch < wet.getNumChannels(); ++ch) {
        auto* dst = wet.getWritePointer(ch);
        for (int i = 0; i < wet.getNumSamples(); ++i) {
            float acc = dst[i];
            for (size_t tap = 0; tap < delays.size(); ++tap) {
                const int idx = i - delays[tap];
                if (idx >= 0)
                    acc += dry.getSample(ch, idx) * gains[tap];
            }
            dst[i] = acc;
        }
    }
    return wet;
}

bool primeSubtractLearn(VXSubtractAudioProcessor& processor, const double sr) {
    auto noise = vxsuite::test::makeNoise(sr, 0.8f, 0.10f);
    setParamNormalized(processor, "learn", 1.0f);
    juce::MidiBuffer midi;
    constexpr int blockSize = 256;
    for (int start = 0; start < noise.getNumSamples(); start += blockSize) {
        const int num = std::min(blockSize, noise.getNumSamples() - start);
        juce::AudioBuffer<float> block(noise.getNumChannels(), num);
        for (int ch = 0; ch < noise.getNumChannels(); ++ch)
            block.copyFrom(ch, 0, noise, ch, start, num);
        processor.processBlock(block, midi);
    }
    setParamNormalized(processor, "learn", 0.0f);
    juce::AudioBuffer<float> silence(noise.getNumChannels(), blockSize);
    silence.clear();
    processor.processBlock(silence, midi);
    return processor.isLearnReady();
}

float gainToDb(const float gain, const float floorDb = -100.0f) {
    return juce::Decibels::gainToDecibels(std::max(gain, 1.0e-6f), floorDb);
}

float rms(const juce::AudioBuffer<float>& buffer) {
    return vxsuite::test::rms(buffer);
}

float peakAbs(const juce::AudioBuffer<float>& buffer) {
    return vxsuite::test::peakAbs(buffer);
}

float correlation(const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b) {
    const int channels = std::min(a.getNumChannels(), b.getNumChannels());
    const int samples = std::min(a.getNumSamples(), b.getNumSamples());
    double dot = 0.0;
    double aEnergy = 0.0;
    double bEnergy = 0.0;
    for (int ch = 0; ch < channels; ++ch) {
        const auto* aa = a.getReadPointer(ch);
        const auto* bb = b.getReadPointer(ch);
        for (int i = 0; i < samples; ++i) {
            dot += static_cast<double>(aa[i]) * bb[i];
            aEnergy += static_cast<double>(aa[i]) * aa[i];
            bEnergy += static_cast<double>(bb[i]) * bb[i];
        }
    }
    if (aEnergy <= 1.0e-12 || bEnergy <= 1.0e-12)
        return 0.0f;
    return static_cast<float>(dot / std::sqrt(aEnergy * bEnergy));
}

float diffRms(const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b) {
    const int channels = std::min(a.getNumChannels(), b.getNumChannels());
    const int samples = std::min(a.getNumSamples(), b.getNumSamples());
    double energy = 0.0;
    int count = 0;
    for (int ch = 0; ch < channels; ++ch) {
        const auto* aa = a.getReadPointer(ch);
        const auto* bb = b.getReadPointer(ch);
        for (int i = 0; i < samples; ++i) {
            const double d = static_cast<double>(aa[i]) - bb[i];
            energy += d * d;
            ++count;
        }
    }
    return count > 0 ? static_cast<float>(std::sqrt(energy / static_cast<double>(count))) : 0.0f;
}

float windowedLevelSpreadDb(const juce::AudioBuffer<float>& buffer,
                            const double sr,
                            const float windowSeconds = 0.10f) {
    const int channels = buffer.getNumChannels();
    const int windowSamples = std::max(1, static_cast<int>(std::round(sr * windowSeconds)));
    std::vector<float> levels;
    for (int start = 0; start < buffer.getNumSamples(); start += windowSamples) {
        const int end = std::min(buffer.getNumSamples(), start + windowSamples);
        double energy = 0.0;
        int count = 0;
        for (int ch = 0; ch < channels; ++ch) {
            const auto* data = buffer.getReadPointer(ch);
            for (int i = start; i < end; ++i) {
                energy += static_cast<double>(data[i]) * data[i];
                ++count;
            }
        }
        const float blockRms = count > 0 ? static_cast<float>(std::sqrt(energy / static_cast<double>(count))) : 0.0f;
        levels.push_back(gainToDb(blockRms));
    }
    if (levels.size() < 2)
        return 0.0f;

    const double mean = std::accumulate(levels.begin(), levels.end(), 0.0) / static_cast<double>(levels.size());
    double variance = 0.0;
    for (const float level : levels) {
        const double d = static_cast<double>(level) - mean;
        variance += d * d;
    }
    variance /= static_cast<double>(levels.size());
    return static_cast<float>(std::sqrt(variance));
}

juce::AudioBuffer<float> readWaveFile(const juce::File& file, double& sampleRate) {
    juce::AudioFormatManager manager;
    manager.registerBasicFormats();
    auto reader = std::unique_ptr<juce::AudioFormatReader>(manager.createReaderFor(file));
    if (reader == nullptr)
        throw std::runtime_error("Could not read input file: " + file.getFullPathName().toStdString());

    sampleRate = reader->sampleRate;
    juce::AudioBuffer<float> buffer(static_cast<int>(reader->numChannels),
                                    static_cast<int>(reader->lengthInSamples));
    reader->read(&buffer, 0, buffer.getNumSamples(), 0, true, true);

    if (buffer.getNumChannels() == 1) {
        juce::AudioBuffer<float> stereo(2, buffer.getNumSamples());
        stereo.copyFrom(0, 0, buffer, 0, 0, buffer.getNumSamples());
        stereo.copyFrom(1, 0, buffer, 0, 0, buffer.getNumSamples());
        return stereo;
    }

    return buffer;
}

bool writeWaveFile(const juce::File& file,
                   const juce::AudioBuffer<float>& buffer,
                   const double sampleRate) {
    juce::WavAudioFormat format;
    auto stream = std::unique_ptr<juce::FileOutputStream>(file.createOutputStream());
    if (stream == nullptr)
        return false;

    auto writer = std::unique_ptr<juce::AudioFormatWriter>(format.createWriterFor(stream.get(),
                                                                                   sampleRate,
                                                                                   static_cast<unsigned int>(buffer.getNumChannels()),
                                                                                   24,
                                                                                   {},
                                                                                   0));
    if (writer == nullptr)
        return false;

    stream.release();
    return writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
}

Metrics computeMetrics(const juce::AudioBuffer<float>& input,
                       const juce::AudioBuffer<float>& targetReference,
                       const juce::AudioBuffer<float>& output,
                       const double sampleRate) {
    Metrics m;
    m.inputSpreadDb = windowedLevelSpreadDb(input, sampleRate);
    m.outputSpreadDb = windowedLevelSpreadDb(output, sampleRate);
    m.spreadImprovementDb = m.inputSpreadDb - m.outputSpreadDb;
    m.inputRmsDb = gainToDb(rms(input));
    m.outputRmsDb = gainToDb(rms(output));
    m.deltaRmsDb = gainToDb(diffRms(input, output));
    m.correlation = correlation(input, output);
    m.speechBandCorrelation = vxsuite::test::speechBandCorrelation(input, output, sampleRate);
    m.residualRatio = vxsuite::test::bestGainResidualRatioSkip(input, output, 2048);
    m.targetCorrelation = correlation(targetReference, output);
    m.targetSpeechBandCorrelation = vxsuite::test::speechBandCorrelation(targetReference, output, sampleRate);
    m.targetResidualRatio = vxsuite::test::bestGainResidualRatioSkip(targetReference, output, 2048);
    m.outputPeakDbfs = gainToDb(peakAbs(output), -120.0f);
    return m;
}

std::vector<AudioFileData> collectWaveFiles(const juce::File& directory) {
    std::vector<AudioFileData> files;
    if (!directory.isDirectory())
        throw std::runtime_error("Input directory is not a directory: " + directory.getFullPathName().toStdString());

    std::vector<juce::File> waveFiles;
    for (const auto& entry : fs::recursive_directory_iterator(directory.getFullPathName().toStdString())) {
        if (!entry.is_regular_file())
            continue;
        const auto path = entry.path().string();
        if (juce::String(path).endsWithIgnoreCase(".wav"))
            waveFiles.emplace_back(path);
    }

    std::sort(waveFiles.begin(), waveFiles.end(),
              [](const juce::File& a, const juce::File& b) {
                  return a.getFullPathName() < b.getFullPathName();
              });

    for (const auto& file : waveFiles) {
        AudioFileData item;
        item.file = file;
        item.buffer = readWaveFile(file, item.sampleRate);
        files.push_back(std::move(item));
    }
    return files;
}

std::string formatFloat(const float value, const int precision = 3) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(precision) << value;
    return oss.str();
}

std::string relativeTo(const juce::File& root, const juce::File& child) {
    const auto rel = child.getRelativePathFrom(root);
    return rel.isNotEmpty() ? rel.toStdString() : child.getFileName().toStdString();
}

std::vector<ProductSpec> makeProductSpecs() {
    std::vector<ProductSpec> specs;

    specs.push_back({"leveler", "leveler",
    [](const juce::AudioBuffer<float>& input, double) { return input; },
    [](const juce::AudioBuffer<float>& original, const juce::AudioBuffer<float>&, double) { return original; },
    [](const juce::AudioBuffer<float>& input, double sr) {
        VXLevelerAudioProcessor processor;
        processor.prepareToPlay(sr, 256);
        setParamNormalized(processor, "mode", 0.0f);
        setParamNormalized(processor, "level", 1.0f);
        setParamNormalized(processor, "control", 1.0f);
        return render(processor, input, 256);
    }});

    specs.push_back({"cleanup", "cleanup",
    [](const juce::AudioBuffer<float>& input, double) { return input; },
    [](const juce::AudioBuffer<float>& original, const juce::AudioBuffer<float>&, double) { return original; },
    [](const juce::AudioBuffer<float>& input, double sr) {
        VXCleanupAudioProcessor processor;
        processor.prepareToPlay(sr, 256);
        setParamNormalized(processor, "mode", 0.0f);
        setParamNormalized(processor, "cleanup", 1.0f);
        setParamNormalized(processor, "body", 0.55f);
        setParamNormalized(processor, "focus", 0.65f);
        return render(processor, input, 256);
    }});

    specs.push_back({"denoiser", "denoiser",
    [](const juce::AudioBuffer<float>& input, double sr) {
        return vxsuite::test::addBuffers(input,
            vxsuite::test::makeNoise(sr,
                                     static_cast<float>(input.getNumSamples()) / static_cast<float>(sr),
                                     0.04f));
    },
    [](const juce::AudioBuffer<float>& original, const juce::AudioBuffer<float>&, double) { return original; },
    [](const juce::AudioBuffer<float>& input, double sr) {
        VXDenoiserAudioProcessor processor;
        processor.prepareToPlay(sr, 256);
        setParamNormalized(processor, "mode", 0.0f);
        setParamNormalized(processor, "clean", 0.85f);
        setParamNormalized(processor, "guard", 0.90f);
        return render(processor, input, 256);
    }});

    specs.push_back({"deverb", "deverb",
    [](const juce::AudioBuffer<float>& input, double sr) {
        return addRoomTail(input, sr);
    },
    [](const juce::AudioBuffer<float>& original, const juce::AudioBuffer<float>&, double) { return original; },
    [](const juce::AudioBuffer<float>& input, double sr) {
        VXDeverbAudioProcessor processor;
        processor.prepareToPlay(sr, 256);
        setParamNormalized(processor, "mode", 0.0f);
        setParamNormalized(processor, "reduce", 0.75f);
        setParamNormalized(processor, "body", 0.35f);
        return render(processor, input, 256);
    }});

    specs.push_back({"finish", "finish",
    [](const juce::AudioBuffer<float>& input, double) { return input; },
    [](const juce::AudioBuffer<float>& original, const juce::AudioBuffer<float>&, double) { return original; },
    [](const juce::AudioBuffer<float>& input, double sr) {
        VXFinishAudioProcessor processor;
        processor.prepareToPlay(sr, 256);
        setParamNormalized(processor, "mode", 0.0f);
        setParamNormalized(processor, "finish", 0.72f);
        setParamNormalized(processor, "body", 0.52f);
        setParamNormalized(processor, "gain", 0.5f);
        return render(processor, input, 256);
    }});

    specs.push_back({"optocomp", "optocomp",
    [](const juce::AudioBuffer<float>& input, double) { return input; },
    [](const juce::AudioBuffer<float>& original, const juce::AudioBuffer<float>&, double) { return original; },
    [](const juce::AudioBuffer<float>& input, double sr) {
        VXOptoCompAudioProcessor processor;
        processor.prepareToPlay(sr, 256);
        setParamNormalized(processor, "mode", 0.0f);
        setParamNormalized(processor, "peak_reduction", 0.70f);
        setParamNormalized(processor, "body", 0.52f);
        setParamNormalized(processor, "gain", 0.5f);
        return render(processor, input, 256);
    }});

    specs.push_back({"tone", "tone",
    [](const juce::AudioBuffer<float>& input, double) { return input; },
    [](const juce::AudioBuffer<float>& original, const juce::AudioBuffer<float>&, double) { return original; },
    [](const juce::AudioBuffer<float>& input, double sr) {
        VXToneAudioProcessor processor;
        processor.prepareToPlay(sr, 256);
        setParamNormalized(processor, "mode", 0.0f);
        setParamNormalized(processor, "bass", 0.60f);
        setParamNormalized(processor, "treble", 0.58f);
        return render(processor, input, 256);
    }});

    specs.push_back({"proximity", "proximity",
    [](const juce::AudioBuffer<float>& input, double) { return input; },
    [](const juce::AudioBuffer<float>& original, const juce::AudioBuffer<float>&, double) { return original; },
    [](const juce::AudioBuffer<float>& input, double sr) {
        VXProximityAudioProcessor processor;
        processor.prepareToPlay(sr, 256);
        setParamNormalized(processor, "mode", 0.0f);
        setParamNormalized(processor, "closer", 0.72f);
        setParamNormalized(processor, "air", 0.40f);
        return render(processor, input, 256);
    }});

    specs.push_back({"subtract", "subtract",
    [](const juce::AudioBuffer<float>& input, double sr) {
        return vxsuite::test::addBuffers(input,
            vxsuite::test::makeNoise(sr, static_cast<float>(input.getNumSamples()) / static_cast<float>(sr), 0.05f));
    },
    [](const juce::AudioBuffer<float>& original, const juce::AudioBuffer<float>&, double) { return original; },
    [](const juce::AudioBuffer<float>& input, double sr) {
        VXSubtractAudioProcessor processor;
        processor.prepareToPlay(sr, 256);
        if (!primeSubtractLearn(processor, sr))
            return input;
        setParamNormalized(processor, "mode", 0.0f);
        setParamNormalized(processor, "subtract", 0.80f);
        setParamNormalized(processor, "protect", 0.92f);
        return render(processor, input, 256);
    }});

    return specs;
}

std::set<std::string> parseProductsCsv(const std::string& csv) {
    std::set<std::string> out;
    std::stringstream ss(csv);
    std::string item;
    while (std::getline(ss, item, ',')) {
        if (!item.empty())
            out.insert(juce::String(item).trim().toLowerCase().toStdString());
    }
    return out;
}

void writeReport(const juce::File& reportFile,
                 const juce::File& corpusRoot,
                 const std::vector<AudioFileData>& files,
                 const std::vector<ProductSpec>& specs,
                 const std::vector<std::vector<Metrics>>& allMetrics) {
    std::ostringstream md;
    md << "# VX Suite Batch Audio Check\n\n";
    md << "Corpus: `" << corpusRoot.getFullPathName() << "`\n\n";
    md << "Products checked in `Voice` mode with representative tuning presets.\n\n";

    md << "## Summary\n\n";
    md << "| Product | Avg spread in (dB) | Avg spread out (dB) | Avg spread improvement (dB) | Avg corr | Avg speech-band corr | Avg residual ratio | Avg target corr | Avg target speech corr | Avg target residual | Avg delta RMS (dB) | Avg peak out (dBFS) |\n";
    md << "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|\n";

    for (size_t p = 0; p < specs.size(); ++p) {
        Metrics avg;
        for (const auto& m : allMetrics[p]) {
            avg.inputSpreadDb += m.inputSpreadDb;
            avg.outputSpreadDb += m.outputSpreadDb;
            avg.spreadImprovementDb += m.spreadImprovementDb;
            avg.correlation += m.correlation;
            avg.speechBandCorrelation += m.speechBandCorrelation;
            avg.residualRatio += m.residualRatio;
            avg.targetCorrelation += m.targetCorrelation;
            avg.targetSpeechBandCorrelation += m.targetSpeechBandCorrelation;
            avg.targetResidualRatio += m.targetResidualRatio;
            avg.deltaRmsDb += m.deltaRmsDb;
            avg.outputPeakDbfs += m.outputPeakDbfs;
        }
        const float denom = static_cast<float>(std::max<size_t>(1, allMetrics[p].size()));
        md << "| " << specs[p].name
           << " | " << formatFloat(avg.inputSpreadDb / denom)
           << " | " << formatFloat(avg.outputSpreadDb / denom)
           << " | " << formatFloat(avg.spreadImprovementDb / denom)
           << " | " << formatFloat(avg.correlation / denom)
           << " | " << formatFloat(avg.speechBandCorrelation / denom)
           << " | " << formatFloat(avg.residualRatio / denom)
           << " | " << formatFloat(avg.targetCorrelation / denom)
           << " | " << formatFloat(avg.targetSpeechBandCorrelation / denom)
           << " | " << formatFloat(avg.targetResidualRatio / denom)
           << " | " << formatFloat(avg.deltaRmsDb / denom)
           << " | " << formatFloat(avg.outputPeakDbfs / denom)
           << " |\n";
    }

    for (size_t p = 0; p < specs.size(); ++p) {
        md << "\n## " << specs[p].name << "\n\n";
        md << "| File | Spread in (dB) | Spread out (dB) | Improvement (dB) | In RMS (dB) | Out RMS (dB) | Delta RMS (dB) | Corr | Speech-band corr | Residual ratio | Target corr | Target speech corr | Target residual | Peak out (dBFS) |\n";
        md << "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|\n";
        for (size_t i = 0; i < files.size(); ++i) {
            const auto& m = allMetrics[p][i];
            md << "| " << relativeTo(corpusRoot, files[i].file)
               << " | " << formatFloat(m.inputSpreadDb)
               << " | " << formatFloat(m.outputSpreadDb)
               << " | " << formatFloat(m.spreadImprovementDb)
               << " | " << formatFloat(m.inputRmsDb)
               << " | " << formatFloat(m.outputRmsDb)
               << " | " << formatFloat(m.deltaRmsDb)
               << " | " << formatFloat(m.correlation)
               << " | " << formatFloat(m.speechBandCorrelation)
               << " | " << formatFloat(m.residualRatio)
               << " | " << formatFloat(m.targetCorrelation)
               << " | " << formatFloat(m.targetSpeechBandCorrelation)
               << " | " << formatFloat(m.targetResidualRatio)
               << " | " << formatFloat(m.outputPeakDbfs)
               << " |\n";
        }
    }

    reportFile.deleteFile();
    if (auto stream = std::unique_ptr<juce::FileOutputStream>(reportFile.createOutputStream())) {
        stream->writeText(md.str(), false, false, "\n");
    } else {
        throw std::runtime_error("Could not write report file: " + reportFile.getFullPathName().toStdString());
    }
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: VXSuiteBatchAudioCheck <input_dir> <report.md> [--products=a,b,c] [--renders=/path]\n";
        return 1;
    }

    const juce::ScopedJuceInitialiser_GUI juceInit;
    const juce::File inputDir(argv[1]);
    const juce::File reportFile(argv[2]);
    juce::File rendersDir;
    bool writeRenders = false;
    std::set<std::string> requestedProducts;

    for (int i = 3; i < argc; ++i) {
        const juce::String arg(argv[i]);
        if (arg.startsWith("--products=")) {
            requestedProducts = parseProductsCsv(arg.fromFirstOccurrenceOf("=", false, false).toStdString());
        } else if (arg.startsWith("--renders=")) {
            rendersDir = juce::File(arg.fromFirstOccurrenceOf("=", false, false));
            writeRenders = true;
        } else if (!writeRenders) {
            rendersDir = juce::File(arg);
            writeRenders = true;
        }
    }

    try {
        const auto files = collectWaveFiles(inputDir);
        if (files.empty())
            throw std::runtime_error("No WAV files found under: " + inputDir.getFullPathName().toStdString());

        auto specs = makeProductSpecs();
        if (!requestedProducts.empty()) {
            specs.erase(std::remove_if(specs.begin(), specs.end(),
                                       [&](const ProductSpec& spec) {
                                           return requestedProducts.count(spec.slug) == 0
                                               && requestedProducts.count(spec.name) == 0;
                                       }),
                        specs.end());
        }
        if (specs.empty())
            throw std::runtime_error("No matching products selected for batch run");

        std::vector<std::vector<Metrics>> allMetrics(specs.size());
        for (auto& bucket : allMetrics)
            bucket.reserve(files.size());

        if (writeRenders)
            rendersDir.createDirectory();

        std::vector<ProductSpec> completedSpecs;
        std::vector<std::vector<Metrics>> completedMetrics;

        for (size_t p = 0; p < specs.size(); ++p) {
                std::cout << "Running " << specs[p].name << " on " << files.size() << " files" << std::endl;
            for (const auto& file : files) {
                const auto preparedInput = specs[p].prepareInput(file.buffer, file.sampleRate);
                const auto scoreReference = specs[p].scoreReference(file.buffer, preparedInput, file.sampleRate);
                const auto output = specs[p].process(preparedInput, file.sampleRate);
                allMetrics[p].push_back(computeMetrics(preparedInput, scoreReference, output, file.sampleRate));

                if (writeRenders) {
                    const auto productDir = rendersDir.getChildFile(specs[p].slug);
                    productDir.createDirectory();
                    const auto outFile = productDir.getChildFile(file.file.getFileNameWithoutExtension() + "_" + specs[p].slug + ".wav");
                    writeWaveFile(outFile, output, file.sampleRate);
                }
            }

            completedSpecs.push_back(specs[p]);
            completedMetrics.push_back(allMetrics[p]);
            writeReport(reportFile, inputDir, files, completedSpecs, completedMetrics);
        }

        std::cout << "Wrote report: " << reportFile.getFullPathName() << "\n";
        if (writeRenders)
            std::cout << "Wrote renders under: " << rendersDir.getFullPathName() << "\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }

    return 0;
}
