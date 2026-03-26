#include "VxSuiteModelAssets.h"

#include <juce_core/juce_core.h>

namespace vxsuite {

namespace {

constexpr auto kPromptDeclinedValue = "declined";

class ModelDownloadJob final : public juce::ThreadPoolJob {
public:
    ModelDownloadJob(ModelAssetService& serviceIn,
                     const ModelPackage packageIn,
                     ModelAssetService::PackageState& stateIn)
        : juce::ThreadPoolJob("vx_model_download"),
          service(serviceIn),
          package(std::move(packageIn)),
          state(stateIn) {}

    JobStatus runJob() override {
        state.state.store(static_cast<int>(ModelAssetService::State::downloading), std::memory_order_relaxed);
        state.progress.store(0.0f, std::memory_order_relaxed);
        state.lastError.clear();

        const auto targetDir = service.packageDirectory(package);
        if (!targetDir.exists() && !targetDir.createDirectory()) {
            state.lastError = "could not create model cache directory";
            state.state.store(static_cast<int>(ModelAssetService::State::failed), std::memory_order_relaxed);
            return jobHasFinished;
        }

        const int totalFiles = std::max(1, static_cast<int>(package.files.size()));
        int64_t totalWeight = 0;
        for (const auto& file : package.files)
            totalWeight += std::max<int64_t>(1, file.expectedBytes);
        totalWeight = std::max<int64_t>(1, totalWeight);
        int64_t completedWeight = 0;

        for (int index = 0; index < totalFiles; ++index) {
            if (shouldExit())
                return jobHasFinished;

            const auto& file = package.files[static_cast<size_t>(index)];
            const int64_t fileWeight = std::max<int64_t>(1, file.expectedBytes);
            if (file.url.isEmpty() || file.fileName.isEmpty()) {
                state.lastError = "package manifest is incomplete";
                state.state.store(static_cast<int>(ModelAssetService::State::failed), std::memory_order_relaxed);
                return jobHasFinished;
            }

            const auto finalFile = service.packageFile(package, file.fileName);
            const auto tempFile = finalFile.withFileExtension(finalFile.getFileExtension() + ".download");
            tempFile.deleteFile();

            int statusCode = 0;
            auto stream = juce::URL(file.url)
                .createInputStream(juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                                       .withConnectionTimeoutMs(15000)
                                       .withNumRedirectsToFollow(4)
                                       .withStatusCode(&statusCode));
            if (stream == nullptr || statusCode < 200 || statusCode >= 300) {
                state.lastError = "download failed";
                state.state.store(static_cast<int>(ModelAssetService::State::failed), std::memory_order_relaxed);
                tempFile.deleteFile();
                return jobHasFinished;
            }

            auto out = tempFile.createOutputStream();
            if (out == nullptr) {
                state.lastError = "could not write model file";
                state.state.store(static_cast<int>(ModelAssetService::State::failed), std::memory_order_relaxed);
                tempFile.deleteFile();
                return jobHasFinished;
            }

            constexpr int kChunkSize = 1 << 16;
            juce::HeapBlock<char> buffer(kChunkSize);
            int64_t bytesWritten = 0;
            for (;;) {
                if (shouldExit()) {
                    tempFile.deleteFile();
                    return jobHasFinished;
                }

                const auto bytesRead = stream->read(buffer.getData(), kChunkSize);
                if (bytesRead <= 0)
                    break;
                out->write(buffer.getData(), static_cast<size_t>(bytesRead));
                bytesWritten += static_cast<int64_t>(bytesRead);
                const auto weightedBytes = std::min<int64_t>(fileWeight, bytesWritten);
                const auto progress = static_cast<float>(completedWeight + weightedBytes) / static_cast<float>(totalWeight);
                state.progress.store(juce::jlimit(0.0f, 0.999f, progress), std::memory_order_relaxed);
            }

            out->flush();
            finalFile.deleteFile();
            if (!tempFile.moveFileTo(finalFile)) {
                state.lastError = "could not finalize model file";
                state.state.store(static_cast<int>(ModelAssetService::State::failed), std::memory_order_relaxed);
                tempFile.deleteFile();
                return jobHasFinished;
            }

            completedWeight += fileWeight;
            state.progress.store(static_cast<float>(completedWeight) / static_cast<float>(totalWeight), std::memory_order_relaxed);
        }

        state.lastError.clear();
        state.state.store(static_cast<int>(ModelAssetService::State::ready), std::memory_order_relaxed);
        state.progress.store(1.0f, std::memory_order_relaxed);
        service.clearPromptDecline(package);
        return jobHasFinished;
    }

private:
    ModelAssetService& service;
    ModelPackage package;
    ModelAssetService::PackageState& state;
};

} // namespace

ModelAssetService& ModelAssetService::instance() {
    static ModelAssetService service;
    return service;
}

ModelAssetService::ModelAssetService()
    : downloadPool(std::make_unique<juce::ThreadPool>(juce::ThreadPoolOptions{}.withThreadName("VXModelDownload")
                                                                              .withNumberOfThreads(1))) {
    loadPromptState();
}

ModelAssetService::~ModelAssetService() {
    if (downloadPool != nullptr)
        downloadPool->removeAllJobs(true, 4000);
}

juce::File ModelAssetService::cacheRoot() const {
    const auto root = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("VX Suite")
        .getChildFile("Models");
    if (!root.exists())
        root.createDirectory();
    return root;
}

juce::File ModelAssetService::packageDirectory(const ModelPackage& pkg) const {
    auto dir = cacheRoot().getChildFile(pkg.id);
    if (!dir.exists())
        dir.createDirectory();
    return dir;
}

juce::File ModelAssetService::packageFile(const ModelPackage& pkg, const juce::String& fileName) const {
    return packageDirectory(pkg).getChildFile(fileName);
}

bool ModelAssetService::isReady(const ModelPackage& pkg) const {
    for (const auto& file : pkg.files) {
        if (!packageFile(pkg, file.fileName).existsAsFile())
            return false;
    }
    return !pkg.files.empty();
}

bool ModelAssetService::isDownloading(const ModelPackage& pkg) const {
    if (const auto* current = findState(pkg.id))
        return current->state.load(std::memory_order_relaxed) == static_cast<int>(State::downloading);
    return false;
}

bool ModelAssetService::shouldPrompt(const ModelPackage& pkg) const {
    const juce::ScopedLock lock(stateLock);
    bool ready = !pkg.files.empty();
    for (const auto& file : pkg.files) {
        if (!packageFile(pkg, file.fileName).existsAsFile()) {
            ready = false;
            break;
        }
    }

    const auto found = packageStates.find(pkg.id.toStdString());
    const auto* current = found != packageStates.end() ? found->second.get() : nullptr;
    const int currentState = current != nullptr
        ? current->state.load(std::memory_order_relaxed)
        : static_cast<int>(State::missing);
    const bool inFlight = currentState == static_cast<int>(State::downloading);
    const bool hasFailed = currentState == static_cast<int>(State::failed);

    return !ready
        && !inFlight
        && !hasFailed
        && promptFlags[promptKeyFor(pkg.id)] != kPromptDeclinedValue;
}

juce::String ModelAssetService::lastError(const ModelPackage& pkg) const {
    if (const auto* current = findState(pkg.id))
        return current->lastError;
    return {};
}

float ModelAssetService::progress(const ModelPackage& pkg) const {
    if (const auto* current = findState(pkg.id))
        return current->progress.load(std::memory_order_relaxed);
    return 0.0f;
}

void ModelAssetService::declinePrompt(const ModelPackage& pkg) {
    const juce::ScopedLock lock(stateLock);
    promptFlags.set(promptKeyFor(pkg.id), kPromptDeclinedValue);
    savePromptState();
}

void ModelAssetService::clearPromptDecline(const ModelPackage& pkg) {
    const juce::ScopedLock lock(stateLock);
    const int index = promptFlags.getAllKeys().indexOf(promptKeyFor(pkg.id));
    if (index >= 0)
        promptFlags.remove(index);
    savePromptState();
}

bool ModelAssetService::requestDownload(const ModelPackage& pkg) {
    if (pkg.id.isEmpty() || pkg.files.empty())
        return false;
    if (isReady(pkg) || isDownloading(pkg))
        return true;

    auto& current = stateFor(pkg.id);
    current.lastError.clear();
    current.state.store(static_cast<int>(State::downloading), std::memory_order_relaxed);
    current.progress.store(0.0f, std::memory_order_relaxed);
    clearPromptDecline(pkg);
    if (downloadPool == nullptr)
        return false;

    downloadPool->addJob(new ModelDownloadJob(*this, pkg, current), true);
    return true;
}

ModelAssetService::PackageState& ModelAssetService::stateFor(const juce::String& packageId) {
    const juce::ScopedLock lock(stateLock);
    const auto key = packageId.toStdString();
    auto found = packageStates.find(key);
    if (found != packageStates.end() && found->second != nullptr)
        return *found->second;

    auto state = std::make_unique<PackageState>();
    auto* raw = state.get();
    packageStates[key] = std::move(state);
    return *raw;
}

const ModelAssetService::PackageState* ModelAssetService::findState(const juce::String& packageId) const {
    const juce::ScopedLock lock(stateLock);
    const auto found = packageStates.find(packageId.toStdString());
    return found != packageStates.end() ? found->second.get() : nullptr;
}

juce::File ModelAssetService::settingsFile() const {
    const auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("VX Suite");
    if (!dir.exists())
        dir.createDirectory();
    return dir.getChildFile("model-assets.settings");
}

void ModelAssetService::loadPromptState() {
    const auto file = settingsFile();
    if (!file.existsAsFile())
        return;

    if (auto xml = juce::parseXML(file)) {
        forEachXmlChildElement(*xml, child) {
            if (child->hasTagName("FLAG"))
                promptFlags.set(child->getStringAttribute("key"), child->getStringAttribute("value"));
        }
    }
}

void ModelAssetService::savePromptState() {
    juce::XmlElement xml("MODEL_ASSETS");
    for (int i = 0; i < promptFlags.size(); ++i) {
        auto* child = xml.createNewChildElement("FLAG");
        child->setAttribute("key", promptFlags.getAllKeys()[i]);
        child->setAttribute("value", promptFlags.getAllValues()[i]);
    }
    if (auto out = settingsFile().createOutputStream())
        xml.writeTo(*out);
}

juce::String ModelAssetService::promptKeyFor(const juce::String& packageId) {
    return "prompt." + packageId;
}

} // namespace vxsuite
