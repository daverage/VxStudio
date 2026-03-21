#include "../Source/vxsuite/products/leveler/VxLevelerProcessor.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>
#include <iostream>
#include <memory>
#include <vector>

namespace {

float clamp01(const float x) {
    return juce::jlimit(0.0f, 1.0f, x);
}

void setParamNormalized(juce::AudioProcessorValueTreeState& state, const char* paramId, const float value) {
    if (auto* p = state.getParameter(paramId))
        p->setValueNotifyingHost(clamp01(value));
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
        const float rms = count > 0 ? static_cast<float>(std::sqrt(energy / static_cast<double>(count))) : 0.0f;
        levels.push_back(juce::Decibels::gainToDecibels(std::max(rms, 1.0e-5f), -100.0f));
    }
    if (levels.size() < 2)
        return 0.0f;

    double mean = 0.0;
    for (const float level : levels)
        mean += level;
    mean /= static_cast<double>(levels.size());

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
        throw std::runtime_error("Could not read input file");

    sampleRate = reader->sampleRate;
    juce::AudioBuffer<float> buffer(static_cast<int>(reader->numChannels),
                                    static_cast<int>(reader->lengthInSamples));
    reader->read(&buffer, 0, buffer.getNumSamples(), 0, true, true);
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

juce::AudioBuffer<float> render(VXLevelerAudioProcessor& processor,
                                const juce::AudioBuffer<float>& input,
                                const int blockSize = 256) {
    const int latency = std::max(0, processor.getLatencySamples());
    juce::AudioBuffer<float> staged(input.getNumChannels(), input.getNumSamples() + latency);
    staged.clear();
    for (int ch = 0; ch < input.getNumChannels(); ++ch)
        staged.copyFrom(ch, 0, input, ch, 0, input.getNumSamples());

    juce::AudioBuffer<float> rendered(input.getNumChannels(), staged.getNumSamples());
    juce::MidiBuffer midi;
    for (int start = 0; start < staged.getNumSamples(); start += blockSize) {
        const int num = std::min(blockSize, staged.getNumSamples() - start);
        juce::AudioBuffer<float> block(staged.getNumChannels(), num);
        for (int ch = 0; ch < staged.getNumChannels(); ++ch)
            block.copyFrom(ch, 0, staged, ch, start, num);
        processor.processBlock(block, midi);
        for (int ch = 0; ch < rendered.getNumChannels(); ++ch)
            rendered.copyFrom(ch, start, block, ch, 0, num);
    }

    juce::AudioBuffer<float> output(input.getNumChannels(), input.getNumSamples());
    for (int ch = 0; ch < output.getNumChannels(); ++ch)
        output.copyFrom(ch, 0, rendered, ch, latency, input.getNumSamples());
    return output;
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: VXLevelerMeasure <input.wav> <output.wav> [voice|general] [level] [control]\n";
        return 1;
    }

    const juce::ScopedJuceInitialiser_GUI juceInit;
    const juce::File inputFile(argv[1]);
    const juce::File outputFile(argv[2]);
    const juce::String mode = argc > 3 ? juce::String(argv[3]).toLowerCase() : "voice";
    const float level = argc > 4 ? std::stof(argv[4]) : 1.0f;
    const float control = argc > 5 ? std::stof(argv[5]) : 1.0f;

    try {
        double sampleRate = 48000.0;
        auto input = readWaveFile(inputFile, sampleRate);

        VXLevelerAudioProcessor leveler;
        leveler.prepareToPlay(sampleRate, 256);
        setParamNormalized(leveler.getValueTreeState(), "mode", mode == "general" ? 1.0f : 0.0f);
        setParamNormalized(leveler.getValueTreeState(), "level", level);
        setParamNormalized(leveler.getValueTreeState(), "control", control);

        const auto output = render(leveler, input, 256);

        if (!writeWaveFile(outputFile, output, sampleRate)) {
            std::cerr << "Could not write output file\n";
            return 1;
        }

        std::cout << "Mode: " << mode << "\n";
        std::cout << "Input spread dB: " << windowedLevelSpreadDb(input, sampleRate) << "\n";
        std::cout << "Output spread dB: " << windowedLevelSpreadDb(output, sampleRate) << "\n";
        std::cout << "Wrote: " << outputFile.getFullPathName() << "\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }

    return 0;
}
