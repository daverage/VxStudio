#include "VxDryWetResolver.h"

#include <cmath>
#include <numeric>

namespace vxanalyser {

vxsuite::analysis::AnalysisSummary DryWetResolver::aggregate(
    const std::vector<vxsuite::analysis::AnalysisSummary>& list) noexcept {
    if (list.empty())
        return {};
    if (list.size() == 1)
        return list[0];

    vxsuite::analysis::AnalysisSummary out {};
    double rmsSquaredSum = 0.0;
    const float n = static_cast<float>(list.size());

    for (const auto& s : list) {
        for (std::size_t i = 0; i < vxsuite::analysis::kSummarySpectrumBins; ++i)
            out.spectrum[i] += s.spectrum[i];
        rmsSquaredSum    += static_cast<double>(s.rms) * s.rms;
        out.peak          = std::max(out.peak, s.peak);
        out.stereoWidth  += s.stereoWidth;
        out.correlation  += s.correlation;
    }

    for (auto& v : out.spectrum) v /= n;
    out.rms         = static_cast<float>(std::sqrt(rmsSquaredSum / list.size()));
    out.stereoWidth /= n;
    out.correlation /= n;
    return out;
}

DryWetPair DryWetResolver::resolveChain(const std::vector<StageSnapshot>& stages,
                                         const StageSnapshot* analyserStage,
                                         const juce::String& scopeKey,
                                         const juce::String& title) const noexcept {
    // Collect earliest input and latest output per track.
    std::vector<vxsuite::analysis::AnalysisSummary> dryList, wetList;
    std::uint64_t prevTrack = ~std::uint64_t(0);

    for (const auto& s : stages) {
        if (s.stale || s.bypassed) continue;
        if (s.trackStableId != prevTrack) {
            prevTrack = s.trackStableId;
            dryList.push_back(s.input);
            wetList.push_back(s.output);
        } else {
            wetList.back() = s.output;
        }
    }

    if (dryList.empty())
        return {};

    DryWetPair p;
    p.valid    = true;
    p.dry      = aggregate(dryList);
    // Prefer analyser's live input (real mixed wet signal) over per-track last output.
    p.wet      = (analyserStage != nullptr) ? analyserStage->input : aggregate(wetList);
    p.title    = title;
    p.scopeKey = scopeKey;
    return p;
}

DryWetPair DryWetResolver::resolveSingle(const StageSnapshot& stage) const noexcept {
    DryWetPair p;
    p.valid    = true;
    p.dry      = stage.input;
    p.wet      = stage.output;
    p.title    = stage.stageName;
    p.scopeKey = "stage:" + juce::String(static_cast<juce::int64>(stage.instanceId));
    return p;
}

DryWetPair DryWetResolver::resolveMulti(const std::vector<const StageSnapshot*>& selected,
                                         const juce::String& scopeKey) const noexcept {
    if (selected.empty())
        return {};

    DryWetPair p;
    p.valid    = true;
    p.dry      = selected.front()->input;
    p.wet      = selected.back()->output;
    p.title    = juce::String(static_cast<int>(selected.size())) + " Stages";
    p.scopeKey = scopeKey;
    return p;
}

DryWetPair DryWetResolver::resolveAnalyserOnly(const StageSnapshot& analyserStage) const noexcept {
    DryWetPair p;
    p.valid        = true;
    p.analyserOnly = true;
    p.dry          = analyserStage.input;
    p.wet          = analyserStage.input;
    p.title        = "Live Input";
    p.scopeKey     = "dry-only";
    return p;
}

DryWetPair DryWetResolver::resolveNoStages() const noexcept {
    return {};
}

} // namespace vxanalyser
