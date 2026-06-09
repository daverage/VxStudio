#pragma once

#include "../../framework/VxStudioSpectrumTelemetry.h"

#include <array>
#include <vector>
#include <cstdint>

#include <juce_graphics/juce_graphics.h>
#include <juce_core/juce_core.h>

namespace vxanalyser {

// ScopeMode reserved for future use; only Track is active.
enum class ScopeMode : int { Track = 0 };

// Flat snapshot of one stage slot, enriched from TrackInfo. Value type — no pointers.
struct StageSnapshot {
    std::uint64_t instanceId      = 0;
    std::uint64_t analysisDomainId = 0;
    std::uint64_t trackStableId   = 0;
    std::uint64_t localOrderId    = 0;
    bool trackReliable  = false;
    bool active         = false;   // slot active and non-stale
    bool stale          = false;
    bool bypassed       = false;
    bool live           = false;
    bool silent         = false;
    juce::String trackName;
    juce::Colour trackColour    { 0 };
    juce::String pluginFamily;
    juce::String stageId;
    juce::String stageName;
    vxsuite::analysis::AnalysisSummary input;
    vxsuite::analysis::AnalysisSummary output;
};

// Full view of the registry at one instant.
struct TelemetrySnapshot {
    std::vector<StageSnapshot> stages;
    int totalSlots    = 0;
    int activeSlots   = 0;
    int vxSlots       = 0;
    int nonStaleSlots = 0;
};

// A single display row in the stage chain panel.
struct ChainRow {
    juce::String   stageName;
    juce::String   stateText;
    juce::String   impactText;
    juce::String   typeLabel;
    juce::String   freqHint;
    juce::String   trackName;
    juce::Colour   trackColour { 0 };
    std::uint64_t  trackStableId = 0;
    std::uint64_t  instanceId    = 0;   // 0 for track-header rows
    bool inactive      = false;
    bool selected      = false;
    bool isTrackHeader = false;
};

// Immutable snapshot produced by AnalyserController on every refresh.
// Editor reads this; all production logic lives upstream.
struct RenderModel {
    bool valid        = false;
    bool bypassed     = false;
    bool sparseTone   = false;
    bool analyserOnly = false;  // showing analyser's own input — no external stages
    juce::String statusText;
    juce::String selectionTitle;
    juce::String trackLabel;
    std::array<juce::String, 3> summaryLines {};
    juce::String diagnosticsText;
    std::vector<ChainRow> chainRows;
    std::vector<int>            sparseToneBands;
    std::array<float, vxsuite::analysis::kSummarySpectrumBins> beforeToneDb {};
    std::array<float, vxsuite::analysis::kSummarySpectrumBins> afterToneDb  {};
    std::array<float, vxsuite::analysis::kSummarySpectrumBins> deltaToneDb  {};
    int largestToneBand = 0;
};

// Context passed from the processor to the controller on each refresh.
struct AnalyserContext {
    std::uint64_t trackStableId     = 0;
    juce::String  trackDisplayName;
    std::uint64_t analysisDomainId  = 0;
    std::uint64_t nowMs             = 0;
    bool          isActive          = true;   // false when bypassed
    // Live input from the processor's own accumulator (may be invalid when bypassed).
    bool                                    liveInputValid = false;
    vxsuite::analysis::AnalysisSummary     liveInput {};
};

} // namespace vxanalyser
