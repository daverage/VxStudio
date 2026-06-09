#pragma once

// Pure scope filtering. No UI. No registry reads. No state.
//
// Inclusion rules (track-local, strict):
//   Priority 1 — Confirmed DIFFERENT track: always reject.
//     stage.trackReliable && stage.trackStableId != 0 && stage.trackStableId != analyserTrack
//
//   Priority 2 — Confirmed SAME track: always include.
//     stage.trackReliable && stage.trackStableId == analyserTrack
//
//   Priority 3 — Domain match (same domain as analyser): include as fallback.
//     stage.analysisDomainId == analyserDomainId
//     Handles hosts that provide runtimeID to the analyser but not to effect plugins.
//
//   Priority 4 — Everything else: reject.
//     (unknown track, no domain match, analyser track ID unavailable)

#include "../../products/analyser/VXStudioAnalyserModels.h"

#include <vector>
#include <cstdint>

namespace vxanalyser {

// Per-stage diagnostic entry — explains exactly why a stage is included or rejected.
struct DiagnosticStage {
    juce::String  stageName;
    std::uint64_t trackStableId  = 0;
    std::uint64_t analysisDomainId = 0;
    bool          trackReliable  = false;
    bool          included       = false;
    juce::String  inclusionReason;  // "confirmed track" | "domain match"
    juce::String  rejectionReason;  // non-empty when not included
};

struct ScopeFilterResult {
    // Stages that pass the filter, sorted by localOrderId.
    std::vector<StageSnapshot> inScope;

    // Diagnostic: every VX stage seen (excluding the analyser itself).
    std::vector<DiagnosticStage> allStageDiagnostics;

    int includedConfirmedCount = 0;  // included via confirmed trackStableId match
    int includedDomainCount    = 0;  // included via analysisDomainId fallback
    int rejectedForeignCount   = 0;  // confirmed different track
    int rejectedUnknownCount   = 0;  // unknown track, no domain match
    bool trackAvailable        = false;  // analyserTrackStableId != 0
    juce::String statusNote;
};

class TrackScopeFilter {
public:
    ScopeFilterResult filter(const TelemetrySnapshot& snap,
                              std::uint64_t analyserTrackStableId,
                              std::uint64_t analyserDomainId,
                              const juce::String& analyserStageId) const noexcept;
};

} // namespace vxanalyser
