#pragma once

#include "VxRebalanceFeatureBuffer.h"

#include "../../../framework/VxSuiteSignalQuality.h"
#include "../dsp/VxRebalanceDsp.h"

namespace vxsuite::rebalance::ml {

class ConfidenceTracker {
public:
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    float update(const FeatureSnapshot& features,
                 Dsp::RecordingType recordingType,
                 const vxsuite::SignalQualitySnapshot& signalQuality) noexcept;

private:
    double sampleRateHz = 48000.0;
    float smoothedConfidence = 0.0f;
};

} // namespace vxsuite::rebalance::ml
