#pragma once

#include "../dsp/VxRebalanceDsp.h"

#include <atomic>
#include <functional>
#include <string>

namespace vxsuite::rebalance::ml {

class OnnxUmx4Model {
public:
    static constexpr int kModelBins = 4096 / 2 + 1;
    // Matches the exported Open-Unmix v2.0 baseline contract.
    static constexpr int kModelFrames = 64;
    static constexpr int kHeadCount = 4;

    OnnxUmx4Model();
    ~OnnxUmx4Model();

    bool prepare(const std::string& onnxPath, std::string& errorOut);
    void reset() noexcept;
    [[nodiscard]] bool isReady() const noexcept;

    // Async inference: dispatches to ORT's global thread pool and returns
    // immediately. onComplete is called on an ORT thread when done.
    // Returns false (and never calls onComplete) if dispatch fails.
    bool runAsync(const float* inputMagnitudes,
                  int frames,
                  std::function<void(const float*, int)> onComplete,
                  std::string& errorOut);

    // Blocks until all dispatched runAsync callbacks have fired.
    // Must be called before destroying any state the callback references.
    void waitForPendingAsync() noexcept;

private:
    struct RuntimeHandles;
    RuntimeHandles* handles = nullptr;
    std::string inputName;
    std::string outputName;
};

} // namespace vxsuite::rebalance::ml
