#pragma once

// AnalyserController: orchestrates the analysis pipeline and produces RenderModel.
// Track-local only — no scope mode switching exposed to the UI.
// Message thread only.

#include "VXStudioAnalyserModels.h"
#include "../../framework/analysis/VxTelemetryQueryService.h"
#include "../../framework/analysis/VxTrackScopeFilter.h"
#include "../../framework/analysis/VxDryWetResolver.h"
#include "../../framework/analysis/VxSpectrumSmoothingPipeline.h"

#include <unordered_set>
#include <cstdint>

namespace vxanalyser {

class AnalyserController {
public:
    // Called from editor timer (24 Hz). Runs the full pipeline, updates model().
    void refresh(const AnalyserContext& context, float averageSeconds, int smoothingRadius) noexcept;

    // Clears selection and spectrum history — use from the Refresh button.
    void reset() noexcept;

    // Selection.
    void selectStage(std::uint64_t instanceId, bool additive) noexcept;
    void selectTrack(std::uint64_t trackStableId) noexcept;
    void selectFullChain() noexcept;
    [[nodiscard]] bool isFullChainSelected() const noexcept { return fullChainSelected; }

    // Read-only access to the last produced model.
    [[nodiscard]] const RenderModel& model() const noexcept { return currentModel; }

    // Shared formatting helpers (used by both controller and editor).
    static float        bandCenterHz(int band) noexcept;
    static juce::String formatFrequency(float hz) noexcept;
    static float        toDb(float linear, float floorDb = -100.0f) noexcept;

private:
    TelemetryQueryService     queryService;
    TrackScopeFilter          scopeFilter;
    DryWetResolver            dryWetResolver;
    SpectrumSmoothingPipeline smoothingPipeline;

    std::unordered_set<std::uint64_t> selectedInstanceIds;
    bool fullChainSelected = true;

    RenderModel currentModel;

    void reconcileSelectionState(const std::vector<StageSnapshot>& inScope) noexcept;
    DryWetPair resolveSelection(const std::vector<StageSnapshot>& inScope,
                                 const StageSnapshot* analyserStage) const noexcept;

    void buildChainRows(const std::vector<StageSnapshot>& inScope,
                        RenderModel& model) const noexcept;

    void buildDiagnostics(const TelemetrySnapshot& snap,
                           const ScopeFilterResult& filterResult,
                           const AnalyserContext& ctx,
                           const DryWetPair& sel,
                           float averageSeconds,
                           int smoothingRadius,
                           RenderModel& model) const noexcept;

    static juce::String buildToneSummaryLine(float dryRmsDb, float wetRmsDb,
                                              int largestBand, float largestDeltaDb,
                                              bool sparseTone,
                                              const std::vector<int>& sparseBands,
                                              const std::array<float, vxsuite::analysis::kSummarySpectrumBins>& deltaDb) noexcept;
    static juce::String displayStageName(const juce::String& raw) noexcept;
    static juce::String signedDb(float value, int decimals = 1) noexcept;
    static juce::String classLabel(float spectral, float dynamic, float stereo) noexcept;
    static juce::String impactLabel(float score) noexcept;
    static bool         hasMeaningfulBandEnergy(float bLin, float aLin) noexcept;
};

} // namespace vxanalyser
