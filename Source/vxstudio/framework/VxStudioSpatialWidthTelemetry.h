#pragma once

namespace vxsuite {

/**
 * Phase 2 UI contract for VX Width's spatial-width visualiser
 * (docs/Task Based/VX_WIDTH_ENGINE_UPGRADE.md §6-§9).
 *
 * Processors override ProcessorBase::getSpatialWidthTelemetry() to populate
 * this struct from the target-geometry engine introduced in Phase 1/1.1
 * (VxWidthInputAnalyser.h's estimatedWidth01, VxWidthProcessor.cpp's
 * requestedOutputWidth01/actualOutputWidth01). The editor queries it
 * uniformly and NEVER writes back to the processor from it - this is a
 * read-only reporting contract, not a control path (Phase 2 §1's "audio
 * classification must never rewrite the parameter" rule).
 *
 * All width fields are the SAME raw 0..1 estimator scale Phase 1 measured
 * (moderate stereo ~0.13, broad stereo ~0.40, mono full-width ~0.14-0.16 for
 * typical tonal material) - the UI layer applies its own perceptual display
 * mapping (VxStudioSpatialWidthView.h) rather than expecting these to reach
 * 1.0 for "maximum useful" material. See that file for the calibration.
 */
struct SpatialWidthTelemetry {
    // 0 = clearly stereo, 1 = effectively mono (InputAnalysisSnapshot::monoConfidence).
    // Drives the UI's mono/stereo presentation-mode hysteresis - never fed
    // back into DSP or parameter state from here.
    float monoConfidence01 = 0.0f;
    float estimatedInputWidth01 = 0.0f;
    float requestedOutputWidth01 = 0.0f;
    float actualOutputWidth01 = 0.0f;
    // 0..1, mirrors the Double/Tightness/Focus knob values (already
    // normalised 0..1 parameter reads) - secondary visual cues only
    // (§10-§12): must never move the width geometry itself.
    float doubleAmount01 = 0.0f;
    float tightness01 = 0.0f;
    float focus01 = 0.0f;
    // Whether the current block had enough signal energy to trust the
    // width measurements at all - lets the UI freeze/hide signal-derived
    // animation (e.g. Double's ghost dots) rather than visibly drifting on
    // pure noise floor or while the transport is stopped.
    bool hasSignal = false;
};

} // namespace vxsuite
