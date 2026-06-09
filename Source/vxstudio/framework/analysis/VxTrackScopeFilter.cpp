#include "VxTrackScopeFilter.h"

#include <algorithm>

namespace vxanalyser {

ScopeFilterResult TrackScopeFilter::filter(const TelemetrySnapshot& snap,
                                            const std::uint64_t analyserTrackStableId,
                                            const std::uint64_t analyserDomainId,
                                            const juce::String& analyserStageId) const noexcept {
    ScopeFilterResult result;
    result.trackAvailable = (analyserTrackStableId != 0);

    for (const auto& s : snap.stages) {
        if (s.stageId == analyserStageId)
            continue;
        if (s.stale)
            continue;

        DiagnosticStage d;
        d.stageName        = s.stageName;
        d.trackStableId    = s.trackStableId;
        d.analysisDomainId = s.analysisDomainId;
        d.trackReliable    = s.trackReliable;

        // Priority 1 — Different track: reject.
        // Uses trackStableId regardless of trackReliable — name-based IDs are sufficient
        // for filtering when both the analyser and the stage have a known non-zero ID.
        const bool differentTrack = result.trackAvailable
                                 && s.trackStableId != 0
                                 && s.trackStableId != analyserTrackStableId;
        if (differentTrack) {
            d.included        = false;
            d.rejectionReason = "Different track (ID "
                + juce::String::toHexString(static_cast<juce::int64>(s.trackStableId)).toUpperCase()
                + " != analyser "
                + juce::String::toHexString(static_cast<juce::int64>(analyserTrackStableId)).toUpperCase()
                + ")";
            ++result.rejectedForeignCount;
            result.allStageDiagnostics.push_back(std::move(d));
            continue;
        }

        // Priority 2 — Same track: include.
        const bool sameTrack = result.trackAvailable
                            && s.trackStableId != 0
                            && s.trackStableId == analyserTrackStableId;
        if (sameTrack) {
            d.included        = true;
            d.inclusionReason = s.trackReliable ? "Confirmed same track" : "Same track (name match)";
            ++result.includedConfirmedCount;
            result.inScope.push_back(s);
            result.allStageDiagnostics.push_back(std::move(d));
            continue;
        }

        // Priority 3 — Same analyser domain (fallback for hosts that don't provide
        // reliable track IDs to effect plugins but do bind domains correctly).
        const bool domainMatch = (analyserDomainId != 0)
                              && (s.analysisDomainId == analyserDomainId);
        if (domainMatch) {
            d.included       = true;
            d.inclusionReason = "Same analyser domain (track ID not confirmed)";
            ++result.includedDomainCount;
            result.inScope.push_back(s);
            result.allStageDiagnostics.push_back(std::move(d));
            continue;
        }

        // Priority 4 — Unknown or unreliable, no domain match: reject.
        d.included        = false;
        d.rejectionReason = s.analysisDomainId == 0
            ? "Unknown track and no domain registered"
            : "Unknown track, domain "
              + juce::String::toHexString(static_cast<juce::int64>(s.analysisDomainId)).toUpperCase()
              + " != analyser domain "
              + juce::String::toHexString(static_cast<juce::int64>(analyserDomainId)).toUpperCase();
        ++result.rejectedUnknownCount;
        result.allStageDiagnostics.push_back(std::move(d));
    }

    // Sort inScope by localOrderId for deterministic chain display.
    std::stable_sort(result.inScope.begin(), result.inScope.end(),
        [](const StageSnapshot& a, const StageSnapshot& b) {
            if (a.localOrderId != b.localOrderId)
                return a.localOrderId < b.localOrderId;
            return a.instanceId < b.instanceId;
        });

    if (!result.trackAvailable)
        result.statusNote = "Track identity unavailable";

    return result;
}

} // namespace vxanalyser
