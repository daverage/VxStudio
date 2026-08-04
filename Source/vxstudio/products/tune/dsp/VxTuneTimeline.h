#pragma once

#include <cstdint>

// VX Tune shared data model — the stable contract between analysis and
// rendering stages (docs/Task Based/VXTUNE_VISION_ARCHITECTURE.md §7).
// Extend by adding fields; never repurpose existing ones.

namespace vxsuite::tune {

enum class EstimateReason : std::uint8_t {
    nominal = 0,
    periodicityCollapse,
    lowSnr,
    onsetTransient,
    octaveAmbiguity,
    unvoiced,
    sparseEvidence,
    conflictingEvidence
};

// Every module boundary speaks Estimate<T>: value, confidence, why the
// confidence is what it is, and how fast the estimate is moving.
template <typename T>
struct Estimate {
    T value {};
    float confidence = 0.0f;                      // 0..1
    EstimateReason reason = EstimateReason::unvoiced;
    float stability = 0.0f;                       // 0..1, 1 = settled
};

// One pitch observation per analysis hop, before decomposition.
struct PitchObservation {
    std::int64_t timeSamples = 0;
    Estimate<float> f0Hz {};                      // value 0 = unvoiced
    float voicedProb = 0.0f;
    float levelDb = -120.0f;
};

// Hop-rate frame after performance decomposition.
// Invariant: for voiced frames, centreCents + residualCents == raw pitch in
// cents (reconstruction identity — the expression layer is preserved verbatim).
struct PitchFrame {
    std::int64_t timeSamples = 0;
    Estimate<float> f0Hz {};
    float voicedProb = 0.0f;
    float centreHz = 0.0f;                        // decomposed intent layer
    float centreCents = 0.0f;                     // centre vs A440
    float residualCents = 0.0f;                   // expression layer
    float decompConfidence = 0.0f;                // how cleanly the split held
    float levelDb = -120.0f;
};

enum class Behaviour : std::uint8_t {
    unvoiced = 0, onset, sustain, vibrato, bend, slide, scoop, fall,
    passingNote,
    count
};

struct NoteSegment {
    std::int64_t startSample = 0;
    std::int64_t endSample = 0;                   // provisional while open
    bool open = true;

    // analysis
    float medianCentreCents = 0.0f;               // vs A440 reference
    float segmentConfidence = 0.0f;
    float behaviourProb[static_cast<int>(Behaviour::count)] {};
    float behaviourEntropy = 0.0f;
    float vibratoRateHz = 0.0f;
    float vibratoExtentCents = 0.0f;
    float driftCentsPerSec = 0.0f;

    // intent
    Estimate<int> targetMidiNote { -1, 0.0f, EstimateReason::sparseEvidence, 0.0f };
    int runnerUpMidiNote = -1;
    float targetMarginLog = 0.0f;

    // decision
    float correctionCents = 0.0f;                 // centre-line offset at full Amount
    float convergenceRate = 0.0f;                 // permitted approach speed
    float budgetSpent = 0.0f;                     // perceptual-cost units
};

struct PhraseSegment {
    std::int64_t startSample = 0;
    std::int64_t endSample = 0;
    bool open = true;
    float breathConfidence = 0.0f;
    int climaxNoteIndex = -1;
    int resolutionNoteIndex = -1;
    float budgetRemaining = 0.0f;
};

// Slowly-learned singer profile, published as Estimate<SingerProfile>.
struct SingerProfile {
    float intonationOffsetCents = 0.0f;
    float vibratoRateHz = 0.0f;
    float vibratoDepthCents = 0.0f;
    float driftCentsPerSec = 0.0f;
    float scoopDepthCents = 0.0f;
    float scoopLengthMs = 0.0f;
    float settleTimeMs = 0.0f;
    float rangeLowMidi = 0.0f;
    float rangeHighMidi = 0.0f;
    float observationSeconds = 0.0f;
};

} // namespace vxsuite::tune
