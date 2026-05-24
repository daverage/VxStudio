#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <iostream>

using namespace juce;

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <input.wav> <output.wav>\n";
        return 1;
    }
    
    File inputPath(argv[1]);
    File outputPath(argv[2]);
    
    if (!inputPath.existsAsFile()) {
        std::cerr << "Error: Input file not found\n";
        return 1;
    }
    
    // Read input file
    AudioFormatManager formatManager;
    formatManager.registerBasicFormats();
    
    std::unique_ptr<AudioFormatReader> reader(
        formatManager.createReaderFor(inputPath)
    );
    
    if (!reader) {
        std::cerr << "Error: Could not read audio\n";
        return 1;
    }
    
    int numChannels = reader->numChannels;
    int numSamples = static_cast<int>(reader->lengthInSamples);
    double sampleRate = reader->sampleRate;
    
    std::cout << "Input: " << inputPath.getFileName().toStdString() << "\n";
    std::cout << "  " << numChannels << " ch, " << sampleRate << " Hz, " 
              << numSamples << " samples\n\n";
    
    // Load audio
    AudioBuffer<float> audioBuffer(numChannels, numSamples);
    reader->read(&audioBuffer, 0, numSamples, 0, true, true);
    reader.reset();
    
    // Write output
    std::cout << "Writing output...\n";
    WavAudioFormat wavFormat;
    auto outStream = std::unique_ptr<FileOutputStream>(outputPath.createOutputStream());
    
    if (!outStream) {
        std::cerr << "Error: Could not create output file\n";
        return 1;
    }
    
    auto writer = std::unique_ptr<AudioFormatWriter>(
        wavFormat.createWriterFor(outStream.get(), sampleRate, 
                                 static_cast<unsigned int>(numChannels), 24, {}, 0)
    );
    
    if (writer) {
        outStream.release();
        writer->writeFromAudioSampleBuffer(audioBuffer, 0, audioBuffer.getNumSamples());
        std::cout << "Output: " << outputPath.getFileName().toStdString() << "\n";
        std::cout << "✓ Complete\n";
        return 0;
    } else {
        std::cerr << "Error: Could not create writer\n";
        return 1;
    }
}
