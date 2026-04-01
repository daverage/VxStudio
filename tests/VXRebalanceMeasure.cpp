#include "../Source/vxstudio/products/rebalance/VxRebalanceProcessor.h"
#include "VxStudioProcessorTestUtils.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr std::array<const char*, 6> kStemExtensions { ".wav", ".mp3", ".m4a", ".flac", ".aif", ".aiff" };

struct StemSet {
    juce::AudioBuffer<float> original;
    juce::AudioBuffer<float> vocals;
    juce::AudioBuffer<float> drums;
    juce::AudioBuffer<float> bass;
    juce::AudioBuffer<float> guitar;
    juce::AudioBuffer<float> other;
};

juce::AudioBuffer<float> readAudioFile(const juce::File& file, double& sampleRate) {
    juce::AudioFormatManager manager;
    manager.registerBasicFormats();
    auto reader = std::unique_ptr<juce::AudioFormatReader>(manager.createReaderFor(file));
    if (reader == nullptr)
        throw std::runtime_error("Could not read audio file: " + file.getFullPathName().toStdString());

    sampleRate = reader->sampleRate;
    juce::AudioBuffer<float> buffer(static_cast<int>(reader->numChannels),
                                    static_cast<int>(reader->lengthInSamples));
    reader->read(&buffer, 0, buffer.getNumSamples(), 0, true, true);
    return buffer;
}

juce::File findStemFile(const juce::File& stemDir, const juce::String& suffix) {
    for (const auto* ext : kStemExtensions) {
        const auto exact = stemDir.getChildFile(suffix + ext);
        if (exact.existsAsFile())
            return exact;
    }

    juce::Array<juce::File> matches;
    stemDir.findChildFiles(matches, juce::File::findFiles, false, "*" + suffix + ".*");
    if (!matches.isEmpty())
        return matches.getReference(0);

    return {};
}

juce::AudioBuffer<float> matchToReference(const juce::AudioBuffer<float>& input,
                                          const juce::AudioBuffer<float>& reference) {
    const int channels = std::min(input.getNumChannels(), reference.getNumChannels());
    const int samples = std::min(input.getNumSamples(), reference.getNumSamples());
    juce::AudioBuffer<float> aligned(channels, samples);
    aligned.clear();
    for (int ch = 0; ch < channels; ++ch)
        aligned.copyFrom(ch, 0, input, ch, 0, samples);
    return aligned;
}

juce::AudioBuffer<float> add(const juce::AudioBuffer<float>& a,
                             const juce::AudioBuffer<float>& b) {
    const int channels = std::min(a.getNumChannels(), b.getNumChannels());
    const int samples = std::min(a.getNumSamples(), b.getNumSamples());
    juce::AudioBuffer<float> mixed(channels, samples);
    mixed.clear();
    for (int ch = 0; ch < channels; ++ch) {
        mixed.copyFrom(ch, 0, a, ch, 0, samples);
        mixed.addFrom(ch, 0, b, ch, 0, samples);
    }
    return mixed;
}

float dotProduct(const juce::AudioBuffer<float>& a,
                 const juce::AudioBuffer<float>& b) {
    const int channels = std::min(a.getNumChannels(), b.getNumChannels());
    const int samples = std::min(a.getNumSamples(), b.getNumSamples());
    double dot = 0.0;
    for (int ch = 0; ch < channels; ++ch) {
        const auto* da = a.getReadPointer(ch);
        const auto* db = b.getReadPointer(ch);
        for (int i = 0; i < samples; ++i)
            dot += static_cast<double>(da[i]) * static_cast<double>(db[i]);
    }
    return static_cast<float>(dot);
}

float energy(const juce::AudioBuffer<float>& buffer) {
    return dotProduct(buffer, buffer);
}

float projectionGain(const juce::AudioBuffer<float>& signal,
                     const juce::AudioBuffer<float>& stem) {
    return dotProduct(signal, stem) / std::max(1.0e-9f, energy(stem));
}

float correlation(const juce::AudioBuffer<float>& a,
                  const juce::AudioBuffer<float>& b) {
    const float denom = std::sqrt(std::max(1.0e-9f, energy(a) * energy(b)));
    return dotProduct(a, b) / denom;
}

juce::AudioBuffer<float> subtract(const juce::AudioBuffer<float>& a,
                                  const juce::AudioBuffer<float>& b) {
    const int channels = std::min(a.getNumChannels(), b.getNumChannels());
    const int samples = std::min(a.getNumSamples(), b.getNumSamples());
    juce::AudioBuffer<float> delta(channels, samples);
    delta.clear();
    for (int ch = 0; ch < channels; ++ch) {
        delta.copyFrom(ch, 0, a, ch, 0, samples);
        delta.addFrom(ch, 0, b, ch, 0, samples, -1.0f);
    }
    return delta;
}

void setRebalanceDefaults(VXRebalanceAudioProcessor& processor) {
    vxsuite::test::setParamNormalized(processor, "mode", 1.0f);
    vxsuite::test::setParamNormalized(processor, "recordingType", 0.0f);
    vxsuite::test::setParamNormalized(processor, "vocals", 0.5f);
    vxsuite::test::setParamNormalized(processor, "drums", 0.5f);
    vxsuite::test::setParamNormalized(processor, "bass", 0.5f);
    vxsuite::test::setParamNormalized(processor, "guitar", 0.5f);
    vxsuite::test::setParamNormalized(processor, "other", 0.5f);
    vxsuite::test::setParamNormalized(processor, "strength", 1.0f);
}

float normalizedFromDb(const float db, const float maxAbsDb = 24.0f) {
    return juce::jlimit(0.0f, 1.0f, db / (2.0f * maxAbsDb) + 0.5f);
}

float normalizedChoiceFromIndex(const int index, const int choiceCount) {
    if (choiceCount <= 1)
        return 0.0f;
    return juce::jlimit(0.0f, 1.0f, static_cast<float>(index) / static_cast<float>(choiceCount - 1));
}

int recordingTypeIndexFromText(const juce::String& text) {
    const auto normalized = text.trim().toLowerCase();
    if (normalized == "live")
        return 1;
    if (normalized == "phone" || normalized == "rough" || normalized == "phone/rough" || normalized == "phone_rough")
        return 2;
    return 0;
}

int engineIndexFromText(const juce::String& text) {
    const auto normalized = text.trim().toLowerCase();
    if (normalized == "dsp")
        return 0;
    if (normalized == "umx4")
        return 2;
    return 1; // spleeter
}

void printStemRow(const std::string& name,
                  const juce::AudioBuffer<float>& delta,
                  const juce::AudioBuffer<float>& stem) {
    const float proj = projectionGain(delta, stem);
    const float corr = correlation(delta, stem);
    std::cout << "  " << name
              << " proj=" << proj
              << " corr=" << corr << "\n";
}

float rmsGainDb(const juce::AudioBuffer<float>& input,
                const juce::AudioBuffer<float>& output) {
    const float inRms = std::max(1.0e-9f, vxsuite::test::rms(input));
    const float outRms = std::max(1.0e-9f, vxsuite::test::rms(output));
    return juce::Decibels::gainToDecibels(outRms / inRms);
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: VXRebalanceMeasure <stem_dir> [boost_db] [recording_type] [dsp|spleeter|umx4] [lane_id] [--skip-isolated]\n";
        return 1;
    }

    const juce::ScopedJuceInitialiser_GUI juceInit;
    const juce::File stemDir(argv[1]);
    const float boostDb = argc > 2 ? std::stof(argv[2]) : 6.0f;
    const int recordingTypeIndex = argc > 3 ? recordingTypeIndexFromText(argv[3]) : 0;
    const int engineIndex = argc > 4 ? engineIndexFromText(argv[4]) : 1;
    const juce::String requestedLane = argc > 5 ? juce::String(argv[5]).trim().toLowerCase() : juce::String();
    const bool skipIsolated = argc > 6 && juce::String(argv[6]).trim().equalsIgnoreCase("--skip-isolated");

    try {
        double sampleRate = 48000.0;
        const auto originalFile = findStemFile(stemDir, "_original");
        const auto vocalsFile = findStemFile(stemDir, "_vocals");
        const auto drumsFile = findStemFile(stemDir, "_drums");
        const auto bassFile = findStemFile(stemDir, "_bass");
        const auto guitarFile = findStemFile(stemDir, "_guitar");
        const auto pianoFile = findStemFile(stemDir, "_piano");
        const auto otherFile = findStemFile(stemDir, "_other");

        if (!originalFile.existsAsFile() || !vocalsFile.existsAsFile() || !drumsFile.existsAsFile()
            || !bassFile.existsAsFile() || !guitarFile.existsAsFile() || !pianoFile.existsAsFile()
            || !otherFile.existsAsFile()) {
            throw std::runtime_error("Could not find one or more required stem files in: " + stemDir.getFullPathName().toStdString());
        }

        auto original = readAudioFile(originalFile, sampleRate);
        const auto vocalsRaw = readAudioFile(vocalsFile, sampleRate);
        const auto drumsRaw = readAudioFile(drumsFile, sampleRate);
        const auto bassRaw = readAudioFile(bassFile, sampleRate);
        const auto guitarRaw = readAudioFile(guitarFile, sampleRate);
        const auto pianoRaw = readAudioFile(pianoFile, sampleRate);
        const auto otherRaw = readAudioFile(otherFile, sampleRate);

        StemSet stems {
            original,
            matchToReference(vocalsRaw, original),
            matchToReference(drumsRaw, original),
            matchToReference(bassRaw, original),
            matchToReference(guitarRaw, original),
            add(matchToReference(pianoRaw, original), matchToReference(otherRaw, original))
        };

        VXRebalanceAudioProcessor processor;
        setRebalanceDefaults(processor);
        vxsuite::test::setParamNormalized(processor, "mode", normalizedChoiceFromIndex(engineIndex, 3));
        vxsuite::test::setParamNormalized(processor, "recordingType", normalizedChoiceFromIndex(recordingTypeIndex, 3));
        processor.prepareToPlay(sampleRate, 256);
        std::cout << "Status: " << processor.getStatusText() << "\n";
        const auto neutral = vxsuite::test::render(processor, stems.original, 256);
        const auto neutralDelta = subtract(neutral, stems.original);
        std::cout << "Status after neutral render: " << processor.getStatusText() << "\n";
        std::cout << "Neutral diff rms=" << vxsuite::test::rms(neutralDelta)
                  << " peak=" << vxsuite::test::peakAbs(neutralDelta) << "\n";

        struct Lane {
            const char* id;
            const char* label;
            const juce::AudioBuffer<float>* targetStem;
        };

        const std::array<Lane, 5> lanes {{
            { "vocals", "Vocals", &stems.vocals },
            { "drums", "Drums", &stems.drums },
            { "bass", "Bass", &stems.bass },
            { "guitar", "Guitar", &stems.guitar },
            { "other", "Other", &stems.other }
        }};

        for (const auto& lane : lanes) {
            if (requestedLane.isNotEmpty() && !requestedLane.equalsIgnoreCase(lane.id))
                continue;

            processor.releaseResources();
            setRebalanceDefaults(processor);
            vxsuite::test::setParamNormalized(processor, "mode", normalizedChoiceFromIndex(engineIndex, 3));
            vxsuite::test::setParamNormalized(processor, "recordingType", normalizedChoiceFromIndex(recordingTypeIndex, 3));
            processor.prepareToPlay(sampleRate, 256);
            vxsuite::test::setParamNormalized(processor, lane.id, normalizedFromDb(boostDb));

            const auto rendered = vxsuite::test::render(processor, stems.original, 256);
            const auto delta = subtract(rendered, stems.original);
            std::cout << "  status_after_render=" << processor.getStatusText() << "\n";

            std::cout << "\nLane " << lane.label << " @" << boostDb << " dB"
                      << " delta_rms=" << vxsuite::test::rms(delta)
                      << " delta_peak=" << vxsuite::test::peakAbs(delta) << "\n";
            printStemRow("vocals", delta, stems.vocals);
            printStemRow("drums ", delta, stems.drums);
            printStemRow("bass  ", delta, stems.bass);
            printStemRow("guitar", delta, stems.guitar);
            printStemRow("other ", delta, stems.other);

            std::cout << "  target_corr=" << correlation(delta, *lane.targetStem) << "\n";

            const std::array<std::pair<const char*, const juce::AudioBuffer<float>*>, 5> isolated {{
                { "vocals", &stems.vocals },
                { "drums ", &stems.drums },
                { "bass  ", &stems.bass },
                { "guitar", &stems.guitar },
                { "other ", &stems.other }
            }};

            if (!skipIsolated) {
                std::cout << "  isolated_gain_db\n";
                for (const auto& stem : isolated) {
                    VXRebalanceAudioProcessor isolatedProcessor;
                    setRebalanceDefaults(isolatedProcessor);
                    vxsuite::test::setParamNormalized(isolatedProcessor, "mode", normalizedChoiceFromIndex(engineIndex, 3));
                    vxsuite::test::setParamNormalized(isolatedProcessor, "recordingType", normalizedChoiceFromIndex(recordingTypeIndex, 3));
                    isolatedProcessor.prepareToPlay(sampleRate, 256);
                    vxsuite::test::setParamNormalized(isolatedProcessor, lane.id, normalizedFromDb(boostDb));
                    const auto renderedStem = vxsuite::test::render(isolatedProcessor, *stem.second, 256);
                    std::cout << "    " << stem.first << " " << rmsGainDb(*stem.second, renderedStem) << " dB\n";
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }

    return 0;
}
