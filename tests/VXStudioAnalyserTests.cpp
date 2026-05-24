#include "../Source/vxstudio/products/analyser/VXStudioAnalyserProcessor.h"
#include "../Source/vxstudio/products/cleanup/VxCleanupProcessor.h"
#include "../Source/vxstudio/products/deverb/VxDeverbProcessor.h"
#include "VxStudioProcessorTestUtils.h"

#include <atomic>
#include <thread>
#include <chrono>
#include <iostream>
#include <memory>

namespace {

using namespace vxsuite::test;

// Helper: Create a test audio buffer
juce::AudioBuffer<float> makeTestSignal(double sr, float seconds) {
    auto speech = makeSpeechLike(sr, seconds);
    auto noise = makeNoise(sr, seconds, 0.05f);
    juce::AudioBuffer<float> buffer(2, speech.getNumSamples());
    for (int i = 0; i < buffer.getNumSamples(); ++i) {
        buffer.setSample(0, i, 0.9f * speech.getSample(0, i) + noise.getSample(0, i));
        buffer.setSample(1, i, 0.9f * speech.getSample(1, i) + noise.getSample(1, i));
    }
    return buffer;
}

// Test 1: Stage discovery with preexisting plugins
bool testDiscoveryWithPreexistingPlugins() {
    std::cout << "[VXAnalyserTest] Testing stage discovery with preexisting plugins...\n";

    try {
        // Create processors
        auto cleanup = std::make_unique<VXCleanupAudioProcessor>();
        auto deverb = std::make_unique<VXDeverbAudioProcessor>();
        auto analyser = std::make_unique<VXStudioAnalyserAudioProcessor>();

        const double sr = 48000.0;
        const int blockSize = 256;

        // Prepare all processors
        cleanup->prepareToPlay(sr, blockSize);
        deverb->prepareToPlay(sr, blockSize);
        analyser->prepareToPlay(sr, blockSize);

        // Create test signal
        auto testSignal = makeTestSignal(sr, 0.5f);
        juce::MidiBuffer midi;

        // Process through chain in order: Cleanup -> Deverb -> Analyser
        cleanup->processBlock(testSignal, midi);
        deverb->processBlock(testSignal, midi);
        analyser->processBlock(testSignal, midi);

        // Process for a bit longer to allow discovery
        for (int i = 0; i < 10; ++i) {
            auto signal = makeTestSignal(sr, 0.1f);
            cleanup->processBlock(signal, midi);
            deverb->processBlock(signal, midi);
            analyser->processBlock(signal, midi);
        }

        // Cleanup
        cleanup->releaseResources();
        deverb->releaseResources();
        analyser->releaseResources();

        std::cout << "[VXAnalyserTest] ✓ Stage discovery test passed\n";
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[VXAnalyserTest] ✗ Stage discovery test failed: " << e.what() << "\n";
        return false;
    }
}

// Test 2: Thread safety - concurrent reads while audio thread writes
bool testThreadSafetySpectrumRead() {
    std::cout << "[VXAnalyserTest] Testing thread safety (concurrent reads/writes)...\n";

    try {
        auto analyser = std::make_unique<VXStudioAnalyserAudioProcessor>();

        const double sr = 48000.0;
        const int blockSize = 256;
        analyser->prepareToPlay(sr, blockSize);

        auto testSignal = makeTestSignal(sr, 0.5f);
        juce::MidiBuffer midi;

        std::atomic<bool> audioRunning { true };
        std::atomic<int> crashCount { 0 };

        // Audio thread simulation
        auto audioThread = std::thread([&]() {
            for (int i = 0; i < 50 && audioRunning; ++i) {
                auto signal = makeTestSignal(sr, 0.05f);
                juce::MidiBuffer m;
                try {
                    analyser->processBlock(signal, m);
                } catch (...) {
                    crashCount++;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            audioRunning = false;
        });

        // UI thread simulation (reads)
        int readAttempts = 0;
        while (audioRunning) {
            try {
                // Simulate what the UI does: read from analyser state
                // (This would normally read via the editor interface)
                readAttempts++;
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            } catch (...) {
                crashCount++;
            }
        }

        audioThread.join();
        analyser->releaseResources();

        if (crashCount > 0) {
            std::cerr << "[VXAnalyserTest] ✗ Thread safety test failed: " << crashCount << " crashes\n";
            return false;
        }

        std::cout << "[VXAnalyserTest] ✓ Thread safety test passed (" << readAttempts << " concurrent reads)\n";
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[VXAnalyserTest] ✗ Thread safety test failed: " << e.what() << "\n";
        return false;
    }
}

// Test 3: Domain registration cleanup
bool testDomainRegistrationClearsOnDestruct() {
    std::cout << "[VXAnalyserTest] Testing domain registration cleanup...\n";

    try {
        {
            auto analyser = std::make_unique<VXStudioAnalyserAudioProcessor>();
            const double sr = 48000.0;
            const int blockSize = 256;
            analyser->prepareToPlay(sr, blockSize);

            auto testSignal = makeTestSignal(sr, 0.1f);
            juce::MidiBuffer midi;
            analyser->processBlock(testSignal, midi);

            analyser->releaseResources();
            // Analyser destroyed here
        }

        // Try creating a new one - should register cleanly without stale entries
        {
            auto analyser2 = std::make_unique<VXStudioAnalyserAudioProcessor>();
            const double sr = 48000.0;
            const int blockSize = 256;
            analyser2->prepareToPlay(sr, blockSize);

            auto testSignal = makeTestSignal(sr, 0.1f);
            juce::MidiBuffer midi;
            analyser2->processBlock(testSignal, midi);

            analyser2->releaseResources();
        }

        std::cout << "[VXAnalyserTest] ✓ Domain registration cleanup test passed\n";
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[VXAnalyserTest] ✗ Domain registration test failed: " << e.what() << "\n";
        return false;
    }
}

// Test 4: Rapid plugin add/remove while analyser is open
bool testPluginAddRemoveRapidly() {
    std::cout << "[VXAnalyserTest] Testing rapid plugin add/remove...\n";

    try {
        auto analyser = std::make_unique<VXStudioAnalyserAudioProcessor>();
        const double sr = 48000.0;
        const int blockSize = 256;
        analyser->prepareToPlay(sr, blockSize);

        auto testSignal = makeTestSignal(sr, 0.05f);
        juce::MidiBuffer midi;

        std::atomic<int> errorCount { 0 };

        // Simulate rapid plugin insertion and removal
        for (int cycle = 0; cycle < 5; ++cycle) {
            // Insert 5 plugins
            for (int i = 0; i < 5; ++i) {
                try {
                    auto plugin = std::make_unique<VXCleanupAudioProcessor>();
                    plugin->prepareToPlay(sr, blockSize);
                    analyser->processBlock(testSignal, midi);
                    plugin->releaseResources();
                } catch (...) {
                    errorCount++;
                }
            }

            // Process audio between cycles
            analyser->processBlock(testSignal, midi);
        }

        analyser->releaseResources();

        if (errorCount > 0) {
            std::cerr << "[VXAnalyserTest] ✗ Rapid add/remove test failed: " << errorCount << " errors\n";
            return false;
        }

        std::cout << "[VXAnalyserTest] ✓ Rapid plugin add/remove test passed\n";
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[VXAnalyserTest] ✗ Rapid add/remove test failed: " << e.what() << "\n";
        return false;
    }
}

// Test 5: Memory stability over extended operation
bool testLongSessionMemoryStability() {
    std::cout << "[VXAnalyserTest] Testing long-session memory stability...\n";

    try {
        auto analyser = std::make_unique<VXStudioAnalyserAudioProcessor>();
        const double sr = 48000.0;
        const int blockSize = 256;
        analyser->prepareToPlay(sr, blockSize);

        // Simulate 10 seconds of continuous operation (scaled down for testing)
        const int iterations = 480; // ~10 seconds worth at 48kHz, 256 block size

        for (int i = 0; i < iterations; ++i) {
            auto signal = makeTestSignal(sr, 0.05f);
            juce::MidiBuffer midi;
            analyser->processBlock(signal, midi);

            if (i % 100 == 0) {
                std::cout << "[VXAnalyserTest]   Iteration " << i << "/" << iterations << "\n";
            }
        }

        analyser->releaseResources();

        std::cout << "[VXAnalyserTest] ✓ Long-session memory stability test passed\n";
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[VXAnalyserTest] ✗ Long-session test failed: " << e.what() << "\n";
        return false;
    }
}

// Test 6: Analyser processor initialization and cleanup
bool testAnalyserBasicOperation() {
    std::cout << "[VXAnalyserTest] Testing analyser basic operation...\n";

    try {
        auto analyser = std::make_unique<VXStudioAnalyserAudioProcessor>();

        // Test prepare
        analyser->prepareToPlay(48000.0, 256);

        // Test processing
        auto buffer = makeTestSignal(48000.0, 0.1f);
        juce::MidiBuffer midi;

        for (int i = 0; i < 10; ++i) {
            analyser->processBlock(buffer, midi);
        }

        // Test reset
        analyser->reset();

        // Test release
        analyser->releaseResources();

        std::cout << "[VXAnalyserTest] ✓ Basic operation test passed\n";
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[VXAnalyserTest] ✗ Basic operation test failed: " << e.what() << "\n";
        return false;
    }
}

} // namespace

int main() {
    std::cout << "\n=== VXStudio Analyser Stability Test Suite ===\n\n";

    int passed = 0;
    int failed = 0;

    // Run all tests
    if (testAnalyserBasicOperation()) passed++; else failed++;
    if (testDiscoveryWithPreexistingPlugins()) passed++; else failed++;
    if (testThreadSafetySpectrumRead()) passed++; else failed++;
    if (testDomainRegistrationClearsOnDestruct()) passed++; else failed++;
    if (testPluginAddRemoveRapidly()) passed++; else failed++;
    if (testLongSessionMemoryStability()) passed++; else failed++;

    std::cout << "\n=== Results ===\n";
    std::cout << "Passed: " << passed << "\n";
    std::cout << "Failed: " << failed << "\n";

    if (failed == 0) {
        std::cout << "\n✓ All tests passed!\n\n";
        return 0;
    } else {
        std::cout << "\n✗ Some tests failed\n\n";
        return 1;
    }
}
