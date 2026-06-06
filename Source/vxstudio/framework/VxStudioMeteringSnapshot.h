#pragma once

#include <array>

namespace vxsuite {

/**
 * Standard metering contract exposed by every VX Suite processor.
 *
 * Processors override ProcessorBase::getMeteringSnapshot() to populate this
 * struct from their DSP objects. The UI layer queries it uniformly without
 * knowing which DSP is underneath.
 *
 * Convention:
 *   - gainReductionDb: always positive (e.g. 3.0 = 3 dB of reduction applied)
 *   - activity fields: 0–1 normalised (0 = idle, 1 = fully active)
 *   - LUFS: short-term (≈ 3 s) in dBFS; −100 = not measured / not ready
 */
struct MeteringSnapshot {
    // ── Gain-change meters ───────────────────────────────────────────────────
    float gainReductionDb  = 0.0f;   // e.g. denoiser GR, compressor GR
    float noiseFloorDb     = -80.0f; // estimated noise floor (spectral DSPs)
    float signalPresence   = 0.0f;   // 0–1 speech / signal activity

    // ── Dynamics meters ──────────────────────────────────────────────────────
    float compActivity     = 0.0f;   // 0–1 compressor / leveler ride
    float limiterActivity  = 0.0f;   // 0–1 limiter engagement

    // ── Loudness meters ──────────────────────────────────────────────────────
    float inputLufsShort   = -100.0f;  // pre-processing short-term LUFS
    float outputLufsShort  = -100.0f;  // post-processing short-term LUFS

    // ── Per-band activity (multiband products: SpeechClarity, ToneRefine, Clarity) ──
    static constexpr int kMaxBands = 6;
    std::array<float, kMaxBands> bandActivity {};  // 0–1 per band
    int activeBandCount = 0;
};

} // namespace vxsuite
