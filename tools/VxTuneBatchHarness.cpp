#include "../Source/vxstudio/products/tune/VxTuneProcessor.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <cmath>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct Case {
    std::string id;
    juce::File audioFile;
    std::string score;
    std::string group;
};

struct Preset {
    const char* name = "";
    float amount = 1.0f;
    float natural = 0.5f;
    float speed = 0.5f;
    float focus = 0.5f;
};

struct DebugFrame {
    double seconds = 0.0;
    float confidence = 0.0f;
    float centreCents = 0.0f;
    float residualCents = 0.0f;
    float correctionCents = 0.0f;
    float correctedCents = 0.0f;
    float musicalAuthority = 0.0f;
    float authorityStableBoost = 0.0f;
    float authorityNearCorrect = 1.0f;
    float targetMarginLog = 0.0f;
    float targetErrorCents = 0.0f;
    float rendererAppliedCents = 0.0f;
    int reason = 0;
};

struct RenderMetrics {
    int debugFrames = 0;
    int voicedFrames = 0;
    int stableFrames = 0;
    double voicedCoverage = 0.0;
    double meanConfidence = 0.0;
    double medianAbsInputErrorCents = 0.0;
    double medianAbsOutputErrorCents = 0.0;
    double medianAbsImprovementCents = 0.0;
    double meanAbsCorrectionCents = 0.0;
    double maxAbsCorrectionCents = 0.0;
    double meanMusicalAuthority = 0.0;
    double meanStableBoostAuthority = 0.0;
    double meanNearCorrectAuthority = 1.0;
    double meanLowConfidenceAbsCorrectionCents = 0.0;
    double vibratoExtentInputCents = 0.0;
    double vibratoExtentOutputCents = 0.0;
    double vibratoExtentRatio = 0.0;
};

struct RenderArtifacts {
    bool ok = false;
    juce::File wetPath;
    juce::File debugPath;
    int latency = 0;
    int inputChannels = 0;
    int inputSamples = 0;
    double sampleRate = 0.0;
};

std::string csvEscape(const std::string& value) {
    if (value.find_first_of(",\"\n\r") == std::string::npos)
        return value;

    std::string out = "\"";
    for (const char c : value) {
        if (c == '"')
            out += "\"\"";
        else
            out += c;
    }
    out += '"';
    return out;
}

std::vector<std::string> splitCsvLine(const std::string& line) {
    std::vector<std::string> out;
    std::string current;
    bool quoted = false;
    const auto pushCell = [&]() {
        if (!current.empty() && current.back() == '\r')
            current.pop_back();
        out.push_back(current);
        current.clear();
    };

    for (size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (quoted) {
            if (c == '"' && i + 1 < line.size() && line[i + 1] == '"') {
                current += '"';
                ++i;
            } else if (c == '"') {
                quoted = false;
            } else {
                current += c;
            }
        } else if (c == ',') {
            pushCell();
        } else if (c == '"') {
            quoted = true;
        } else {
            current += c;
        }
    }
    pushCell();
    return out;
}

std::string stemForFile(const juce::File& file) {
    return file.getFileNameWithoutExtension().toStdString();
}

double nearestChromaticErrorCents(const double cents) {
    const double target = std::round(cents / 100.0) * 100.0;
    return cents - target;
}

double median(std::vector<double> values) {
    if (values.empty())
        return 0.0;
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

double percentile(std::vector<double> values, const double p) {
    if (values.empty())
        return 0.0;
    std::sort(values.begin(), values.end());
    const auto index = static_cast<size_t>(
        std::clamp(p, 0.0, 1.0) * static_cast<double>(values.size() - 1));
    return values[index];
}

std::vector<Case> loadCasesFromManifest(const juce::File& manifest) {
    std::ifstream in(manifest.getFullPathName().toStdString());
    std::vector<Case> cases;
    if (!in)
        return cases;

    const juce::File baseDir = manifest.getParentDirectory();
    std::string line;
    bool first = true;
    while (std::getline(in, line)) {
        if (line.empty())
            continue;

        const auto cells = splitCsvLine(line);
        if (cells.empty())
            continue;

        if (first) {
            first = false;
            if (cells[0] == "id" || cells[0] == "audio_path")
                continue;
        }

        Case c;
        if (cells.size() >= 2) {
            c.id = cells[0].empty() ? stemForFile(juce::File(cells[1])) : cells[0];
            c.audioFile = juce::File(cells[1]);
            if (!juce::File::isAbsolutePath(cells[1]))
                c.audioFile = baseDir.getChildFile(cells[1]);
            if (cells.size() > 2)
                c.score = cells[2];
            if (cells.size() > 3)
                c.group = cells[3];
        } else {
            c.audioFile = juce::File(cells[0]);
            if (!juce::File::isAbsolutePath(cells[0]))
                c.audioFile = baseDir.getChildFile(cells[0]);
            c.id = stemForFile(c.audioFile);
        }

        if (c.audioFile.existsAsFile())
            cases.push_back(c);
    }
    return cases;
}

std::vector<Case> loadCasesFromDirectory(const juce::File& directory) {
    std::vector<Case> cases;
    juce::Array<juce::File> files;
    directory.findChildFiles(files, juce::File::findFiles, false,
                             "*.wav;*.aif;*.aiff;*.flac;*.m4a");
    for (const auto& file : files) {
        Case c;
        c.id = stemForFile(file);
        c.audioFile = file;
        cases.push_back(c);
    }
    std::sort(cases.begin(), cases.end(),
              [](const Case& a, const Case& b) { return a.id < b.id; });
    return cases;
}

bool readAudioFile(const juce::File& file, juce::AudioBuffer<float>& buffer,
                   double& sampleRate) {
    juce::AudioFormatManager manager;
    manager.registerBasicFormats();

    auto reader = std::unique_ptr<juce::AudioFormatReader>(
        manager.createReaderFor(file));
    if (!reader)
        return false;

    const auto samples = static_cast<int>(reader->lengthInSamples);
    const auto channels = static_cast<int>(std::min<juce::uint32>(2, reader->numChannels));
    sampleRate = reader->sampleRate;
    buffer.setSize(std::max(1, channels), samples);
    return reader->read(&buffer, 0, samples, 0, true, true);
}

bool writeWavFile(const juce::File& file, const juce::AudioBuffer<float>& buffer,
                  const double sampleRate) {
    file.getParentDirectory().createDirectory();
    file.deleteFile();

    juce::WavAudioFormat format;
    auto stream = std::unique_ptr<juce::FileOutputStream>(file.createOutputStream());
    if (!stream)
        return false;

    auto writer = std::unique_ptr<juce::AudioFormatWriter>(
        format.createWriterFor(stream.get(), sampleRate,
                               static_cast<unsigned int>(buffer.getNumChannels()),
                               24, {}, 0));
    if (!writer)
        return false;

    stream.release();
    return writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
}

void setParam(VXTuneAudioProcessor& processor, const char* id, const float value) {
    if (auto* param = processor.getValueTreeState().getParameter(id))
        param->setValueNotifyingHost(value);
}

std::vector<DebugFrame> readDebugFrames(const juce::File& debugPath) {
    std::ifstream in(debugPath.getFullPathName().toStdString());
    std::vector<DebugFrame> frames;
    if (!in)
        return frames;

    std::string line;
    if (!std::getline(in, line))
        return frames;

    const auto header = splitCsvLine(line);
    std::map<std::string, size_t> columns;
    for (size_t i = 0; i < header.size(); ++i)
        columns[header[i]] = i;

    const auto value = [&](const std::vector<std::string>& cells, const char* name) {
        const auto it = columns.find(name);
        return it != columns.end() && it->second < cells.size()
            ? cells[it->second] : std::string {};
    };
    const auto asDouble = [](const std::string& s) {
        return s.empty() ? 0.0 : std::strtod(s.c_str(), nullptr);
    };
    const auto asInt = [](const std::string& s) {
        return s.empty() ? 0 : std::atoi(s.c_str());
    };

    while (std::getline(in, line)) {
        if (line.empty())
            continue;
        const auto cells = splitCsvLine(line);
        DebugFrame f;
        f.seconds = asDouble(value(cells, "seconds"));
        f.confidence = static_cast<float>(asDouble(value(cells, "confidence")));
        f.reason = asInt(value(cells, "reason"));
        f.centreCents = static_cast<float>(asDouble(value(cells, "centre_cents")));
        f.residualCents = static_cast<float>(asDouble(value(cells, "residual_cents")));
        f.correctionCents = static_cast<float>(asDouble(value(cells, "correction_cents")));
        f.correctedCents = static_cast<float>(asDouble(value(cells, "corrected_cents")));
        f.musicalAuthority = static_cast<float>(asDouble(value(cells, "musical_authority")));
        f.authorityStableBoost =
            static_cast<float>(asDouble(value(cells, "authority_stable_boost")));
        f.authorityNearCorrect =
            static_cast<float>(asDouble(value(cells, "authority_near_correct")));
        f.targetMarginLog = static_cast<float>(asDouble(value(cells, "target_margin_log")));
        f.targetErrorCents = static_cast<float>(asDouble(value(cells, "target_error_cents")));
        f.rendererAppliedCents =
            static_cast<float>(asDouble(value(cells, "shifter_smoothed_cents")));
        frames.push_back(f);
    }

    return frames;
}

RenderMetrics computeMetrics(const juce::File& debugPath) {
    const auto frames = readDebugFrames(debugPath);
    RenderMetrics m;
    m.debugFrames = static_cast<int>(frames.size());
    if (frames.empty())
        return m;

    std::vector<double> inputErrors;
    std::vector<double> outputErrors;
    std::vector<double> corrections;
    std::vector<double> authorities;
    std::vector<double> stableBoostAuthorities;
    std::vector<double> nearCorrectAuthorities;
    std::vector<double> lowConfidenceCorrections;
    std::vector<double> inputResiduals;
    std::vector<double> outputResiduals;
    double confidenceSum = 0.0;

    for (const auto& f : frames) {
        const bool voiced = f.confidence > 0.0f;
        if (voiced)
            ++m.voicedFrames;
        confidenceSum += f.confidence;

        const double detected = static_cast<double>(f.centreCents + f.residualCents);
        const double corrected = static_cast<double>(f.correctedCents);
        const double absCorrection = std::abs(static_cast<double>(f.correctionCents));
        corrections.push_back(absCorrection);
        authorities.push_back(f.musicalAuthority);
        stableBoostAuthorities.push_back(f.authorityStableBoost);
        nearCorrectAuthorities.push_back(f.authorityNearCorrect);
        m.maxAbsCorrectionCents = std::max(m.maxAbsCorrectionCents, absCorrection);

        if (f.confidence < 0.55f)
            lowConfidenceCorrections.push_back(absCorrection);

        const bool stable = f.confidence >= 0.75f && std::abs(f.residualCents) < 35.0f;
        if (stable) {
            ++m.stableFrames;
            inputErrors.push_back(std::abs(nearestChromaticErrorCents(detected)));
            outputErrors.push_back(std::abs(nearestChromaticErrorCents(corrected)));
            inputResiduals.push_back(f.residualCents);
            outputResiduals.push_back(nearestChromaticErrorCents(corrected));
        }
    }

    m.voicedCoverage = static_cast<double>(m.voicedFrames) / frames.size();
    m.meanConfidence = confidenceSum / frames.size();
    m.meanAbsCorrectionCents =
        corrections.empty() ? 0.0
                            : std::accumulate(corrections.begin(), corrections.end(), 0.0)
                                  / corrections.size();
    m.meanMusicalAuthority =
        authorities.empty() ? 0.0
                            : std::accumulate(authorities.begin(), authorities.end(), 0.0)
                                  / authorities.size();
    m.meanStableBoostAuthority = stableBoostAuthorities.empty()
        ? 0.0
        : std::accumulate(stableBoostAuthorities.begin(), stableBoostAuthorities.end(), 0.0)
              / stableBoostAuthorities.size();
    m.meanNearCorrectAuthority = nearCorrectAuthorities.empty()
        ? 1.0
        : std::accumulate(nearCorrectAuthorities.begin(), nearCorrectAuthorities.end(), 0.0)
              / nearCorrectAuthorities.size();
    m.meanLowConfidenceAbsCorrectionCents = lowConfidenceCorrections.empty()
        ? 0.0
        : std::accumulate(lowConfidenceCorrections.begin(), lowConfidenceCorrections.end(), 0.0)
              / lowConfidenceCorrections.size();
    m.medianAbsInputErrorCents = median(inputErrors);
    m.medianAbsOutputErrorCents = median(outputErrors);
    m.medianAbsImprovementCents =
        m.medianAbsInputErrorCents - m.medianAbsOutputErrorCents;

    if (inputResiduals.size() >= 8 && outputResiduals.size() >= 8) {
        m.vibratoExtentInputCents =
            percentile(inputResiduals, 0.95) - percentile(inputResiduals, 0.05);
        m.vibratoExtentOutputCents =
            percentile(outputResiduals, 0.95) - percentile(outputResiduals, 0.05);
        m.vibratoExtentRatio = m.vibratoExtentInputCents > 1.0
            ? m.vibratoExtentOutputCents / m.vibratoExtentInputCents : 0.0;
    }

    return m;
}

RenderArtifacts renderCase(const Case& c, const Preset& preset, const juce::File& outputDir) {
    RenderArtifacts artifacts;
    juce::AudioBuffer<float> input;
    double sampleRate = 0.0;
    if (!readAudioFile(c.audioFile, input, sampleRate)) {
        std::cerr << "Could not read " << c.audioFile.getFullPathName() << "\n";
        return artifacts;
    }

    constexpr int blockSize = 512;
    const juce::File caseDir = outputDir.getChildFile(c.id);
    caseDir.createDirectory();
    artifacts.wetPath = caseDir.getChildFile(
        juce::String(c.id) + "_" + preset.name + ".wav");
    artifacts.debugPath = caseDir.getChildFile(
        juce::String(c.id) + "_" + preset.name + "_debug.csv");
    artifacts.debugPath.deleteFile();

    artifacts.sampleRate = sampleRate;
    artifacts.inputChannels = input.getNumChannels();
    artifacts.inputSamples = input.getNumSamples();

    setenv("VXTUNE_DEBUG_CSV", artifacts.debugPath.getFullPathName().toRawUTF8(), 1);

    VXTuneAudioProcessor processor;
    processor.prepareToPlay(sampleRate, blockSize);
    setParam(processor, "amount", preset.amount);
    setParam(processor, "natural", preset.natural);
    setParam(processor, "speed", preset.speed);
    setParam(processor, "focus", preset.focus);
    const int latency = processor.getLatencySamples();
    artifacts.latency = latency;

    juce::MidiBuffer midi;
    juce::AudioBuffer<float> block(2, blockSize);
    juce::AudioBuffer<float> rendered(2, input.getNumSamples() + latency + blockSize);
    rendered.clear();

    int renderedOffset = 0;
    for (int inputOffset = 0; inputOffset < input.getNumSamples() + latency;
         inputOffset += blockSize) {
        block.clear();
        const int remainingInput = std::max(0, input.getNumSamples() - inputOffset);
        const int n = std::min(blockSize, std::max(blockSize, remainingInput));
        (void) n;

        for (int i = 0; i < blockSize; ++i) {
            const int src = inputOffset + i;
            if (src >= input.getNumSamples())
                break;
            const float left = input.getSample(0, src);
            const float right = input.getNumChannels() > 1 ? input.getSample(1, src) : left;
            block.setSample(0, i, left);
            block.setSample(1, i, right);
        }

        processor.processBlock(block, midi);
        for (int ch = 0; ch < 2; ++ch)
            rendered.copyFrom(ch, renderedOffset, block, ch, 0, blockSize);
        renderedOffset += blockSize;
    }

    juce::AudioBuffer<float> compensated(std::max(1, input.getNumChannels()),
                                         input.getNumSamples());
    compensated.clear();
    for (int ch = 0; ch < compensated.getNumChannels(); ++ch) {
        const int srcCh = std::min(ch, rendered.getNumChannels() - 1);
        if (latency + input.getNumSamples() <= rendered.getNumSamples())
            compensated.copyFrom(ch, 0, rendered, srcCh, latency,
                                 input.getNumSamples());
    }

    const bool wrote = writeWavFile(artifacts.wetPath, compensated, sampleRate);
    processor.releaseResources();
    artifacts.ok = wrote;
    return artifacts;
}

void printUsage() {
    std::cerr
        << "Usage: VxTuneBatchHarness <manifest.csv|audio-directory> <output-directory>\n"
        << "\n"
        << "Manifest columns: id,audio_path,score,group. Only audio_path is required.\n"
        << "Directory mode scans wav/aif/aiff/flac/m4a files non-recursively.\n";
}

} // namespace

int main(const int argc, char* argv[]) {
    if (argc < 3) {
        printUsage();
        return 2;
    }

    const juce::File input(argv[1]);
    const juce::File outputDir(argv[2]);
    outputDir.createDirectory();

    std::vector<Case> cases = input.isDirectory()
        ? loadCasesFromDirectory(input)
        : loadCasesFromManifest(input);

    if (cases.empty()) {
        std::cerr << "No readable audio cases found in " << input.getFullPathName() << "\n";
        return 1;
    }

    const Preset presets[] = {
        { "natural", 0.325f, 0.25f, 0.50f, 0.50f },
        { "balanced", 0.50f, 0.65f, 0.30f, 0.60f },
        { "tight", 0.50f, 0.85f, 0.50f, 0.50f },
        { "hard_tune", 1.0f, 1.0f, 1.00f, 1.00f },
    };

    const juce::File manifestPath = outputDir.getChildFile("vxtune_batch_manifest.csv");
    auto manifest = std::ofstream(manifestPath.getFullPathName().toStdString(),
                                  std::ios::out | std::ios::trunc);
    if (!manifest) {
        std::cerr << "Could not write manifest " << manifestPath.getFullPathName() << "\n";
        return 1;
    }

    manifest << "id,input_path,score,group,preset,amount,natural,speed,focus,sample_rate,"
                "input_channels,input_samples,latency_samples,wet_path,debug_csv,status\n";

    const juce::File metricsPath = outputDir.getChildFile("vxtune_batch_metrics.csv");
    auto metrics = std::ofstream(metricsPath.getFullPathName().toStdString(),
                                 std::ios::out | std::ios::trunc);
    if (!metrics) {
        std::cerr << "Could not write metrics " << metricsPath.getFullPathName() << "\n";
        return 1;
    }

    metrics << "id,preset,score,group,debug_frames,voiced_frames,stable_frames,"
               "voiced_coverage,mean_confidence,median_abs_input_error_cents,"
               "median_abs_output_error_cents,median_abs_improvement_cents,"
               "mean_abs_correction_cents,max_abs_correction_cents,"
               "mean_musical_authority,mean_stable_boost_authority,"
               "mean_near_correct_authority,"
               "mean_low_confidence_abs_correction_cents,"
               "vibrato_extent_input_cents,"
               "vibrato_extent_output_cents,vibrato_extent_ratio,wet_path,debug_csv,status\n";

    int ok = 0;
    int failed = 0;
    for (const auto& c : cases) {
        for (const auto& preset : presets) {
            const auto artifacts = renderCase(c, preset, outputDir);
            const auto renderMetrics = artifacts.ok
                ? computeMetrics(artifacts.debugPath) : RenderMetrics {};
            const char* status = artifacts.ok ? "ok" : "render_failed";

            manifest << csvEscape(c.id) << ','
                     << csvEscape(c.audioFile.getFullPathName().toStdString()) << ','
                     << csvEscape(c.score) << ','
                     << csvEscape(c.group) << ','
                     << preset.name << ','
                     << preset.amount << ','
                     << preset.natural << ','
                     << preset.speed << ','
                     << preset.focus << ','
                     << artifacts.sampleRate << ','
                     << artifacts.inputChannels << ','
                     << artifacts.inputSamples << ','
                     << artifacts.latency << ','
                     << csvEscape(artifacts.wetPath.getFullPathName().toStdString()) << ','
                     << csvEscape(artifacts.debugPath.getFullPathName().toStdString()) << ','
                     << status << '\n';

            metrics << csvEscape(c.id) << ','
                    << preset.name << ','
                    << csvEscape(c.score) << ','
                    << csvEscape(c.group) << ','
                    << renderMetrics.debugFrames << ','
                    << renderMetrics.voicedFrames << ','
                    << renderMetrics.stableFrames << ','
                    << renderMetrics.voicedCoverage << ','
                    << renderMetrics.meanConfidence << ','
                    << renderMetrics.medianAbsInputErrorCents << ','
                    << renderMetrics.medianAbsOutputErrorCents << ','
                    << renderMetrics.medianAbsImprovementCents << ','
                    << renderMetrics.meanAbsCorrectionCents << ','
                    << renderMetrics.maxAbsCorrectionCents << ','
                    << renderMetrics.meanMusicalAuthority << ','
                    << renderMetrics.meanStableBoostAuthority << ','
                    << renderMetrics.meanNearCorrectAuthority << ','
                    << renderMetrics.meanLowConfidenceAbsCorrectionCents << ','
                    << renderMetrics.vibratoExtentInputCents << ','
                    << renderMetrics.vibratoExtentOutputCents << ','
                    << renderMetrics.vibratoExtentRatio << ','
                    << csvEscape(artifacts.wetPath.getFullPathName().toStdString()) << ','
                    << csvEscape(artifacts.debugPath.getFullPathName().toStdString()) << ','
                    << status << '\n';

            if (artifacts.ok)
                ++ok;
            else
                ++failed;
        }
    }

    std::cout << "VX Tune batch harness wrote " << ok << " render(s) to "
              << outputDir.getFullPathName() << "\n";
    std::cout << "Metrics: " << metricsPath.getFullPathName() << "\n";
    if (failed > 0)
        std::cout << failed << " render(s) failed\n";
    return failed == 0 ? 0 : 1;
}
