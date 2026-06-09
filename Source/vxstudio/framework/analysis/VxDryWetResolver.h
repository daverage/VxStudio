#pragma once

// Pure dry/wet resolution. No UI. No state. No registry reads.
// All methods are const and stateless — unit-testable.

#include "../../products/analyser/VXStudioAnalyserModels.h"

#include <vector>

namespace vxanalyser {

struct DryWetPair {
    bool valid        = false;
    bool analyserOnly = false;  // true: showing analyser's own input, no external stages
    vxsuite::analysis::AnalysisSummary dry;
    vxsuite::analysis::AnalysisSummary wet;
    juce::String title;
    juce::String scopeKey;  // change → reset smoothing history
};

class DryWetResolver {
public:
    // Full chain (stages sorted by track/localOrderId).
    // dry = earliest active per-track input; wet = analyser live input if valid, else latest output.
    [[nodiscard]] DryWetPair resolveChain(const std::vector<StageSnapshot>& stages,
                                          const StageSnapshot* analyserStage,
                                          const juce::String& scopeKey,
                                          const juce::String& title) const noexcept;

    // Single stage: dry = its input, wet = its output.
    [[nodiscard]] DryWetPair resolveSingle(const StageSnapshot& stage) const noexcept;

    // Multi-select (sorted by localOrderId): dry = earliest input, wet = latest output.
    [[nodiscard]] DryWetPair resolveMulti(const std::vector<const StageSnapshot*>& selected,
                                          const juce::String& scopeKey) const noexcept;

    // No active in-scope stages — show analyser's own live input (dry == wet).
    [[nodiscard]] DryWetPair resolveAnalyserOnly(const StageSnapshot& analyserStage) const noexcept;

    // No stages and no analyser — nothing to show.
    [[nodiscard]] DryWetPair resolveNoStages() const noexcept;

    // Aggregate multiple summaries in the linear domain (never average dB).
    [[nodiscard]] static vxsuite::analysis::AnalysisSummary aggregate(
        const std::vector<vxsuite::analysis::AnalysisSummary>& list) noexcept;
};

} // namespace vxanalyser
