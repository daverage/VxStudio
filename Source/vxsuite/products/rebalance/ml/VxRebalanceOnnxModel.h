#pragma once

#include "../dsp/VxRebalanceDsp.h"

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

    bool run(const float* inputMagnitudes,
             int frames,
             float* outputMagnitudes,
             std::string& errorOut);

private:
    struct RuntimeHandles;
    RuntimeHandles* handles = nullptr;
    std::string inputName;
    std::string outputName;
};

} // namespace vxsuite::rebalance::ml
