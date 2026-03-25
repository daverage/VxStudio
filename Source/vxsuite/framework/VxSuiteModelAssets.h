#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <memory>
#include <unordered_map>
#include <vector>

namespace vxsuite {

struct ModelAssetFile {
    juce::String fileName;
    juce::String url;
};

struct ModelPackage {
    juce::String id;
    juce::String displayName;
    juce::String reason;
    std::vector<ModelAssetFile> files;
};

class ModelAssetService {
public:
    enum class State {
        missing = 0,
        downloading,
        ready,
        failed
    };

    static ModelAssetService& instance();

    struct PackageState {
        std::atomic<int> state { static_cast<int>(State::missing) };
        std::atomic<float> progress { 0.0f };
        juce::String lastError;
    };

    [[nodiscard]] juce::File cacheRoot() const;
    [[nodiscard]] juce::File packageDirectory(const ModelPackage& pkg) const;
    [[nodiscard]] juce::File packageFile(const ModelPackage& pkg, const juce::String& fileName) const;
    [[nodiscard]] bool isReady(const ModelPackage& pkg) const;
    [[nodiscard]] bool isDownloading(const ModelPackage& pkg) const;
    [[nodiscard]] bool shouldPrompt(const ModelPackage& pkg) const;
    [[nodiscard]] juce::String lastError(const ModelPackage& pkg) const;
    [[nodiscard]] float progress(const ModelPackage& pkg) const;

    void declinePrompt(const ModelPackage& pkg);
    void clearPromptDecline(const ModelPackage& pkg);
    bool requestDownload(const ModelPackage& pkg);

private:
    ModelAssetService();
    ~ModelAssetService();

    PackageState& stateFor(const juce::String& packageId);
    const PackageState* findState(const juce::String& packageId) const;
    juce::File settingsFile() const;
    void loadPromptState();
    void savePromptState();
    static juce::String promptKeyFor(const juce::String& packageId);

    mutable juce::CriticalSection stateLock;
    std::unique_ptr<juce::ThreadPool> downloadPool;
    std::unordered_map<std::string, std::unique_ptr<PackageState>> packageStates;
    juce::StringPairArray promptFlags;
};

} // namespace vxsuite
