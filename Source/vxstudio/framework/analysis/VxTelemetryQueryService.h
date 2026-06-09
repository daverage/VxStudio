#pragma once

// Reads the StageRegistry and returns a flat, immutable snapshot.
// Message-thread only. No UI code. No smoothing. No painting.

#include "../../products/analyser/VXStudioAnalyserModels.h"

#include <cstdint>

namespace vxanalyser {

class TelemetryQueryService {
public:
    // Scans StageRegistry and returns a fresh snapshot. Call on message thread only.
    [[nodiscard]] TelemetrySnapshot query(std::uint64_t nowMs) const noexcept;
};

} // namespace vxanalyser
