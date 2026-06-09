#include "VxTelemetryQueryService.h"

#include "../VxStudioSpectrumTelemetry.h"

namespace vxanalyser {

namespace {
constexpr std::uint64_t kStaleThresholdMs = 8000;

juce::String fromChars(const auto& chars) {
    return juce::String(chars.data());
}
} // namespace

TelemetrySnapshot TelemetryQueryService::query(const std::uint64_t nowMs) const noexcept {
    TelemetrySnapshot snap;
    auto& registry = vxsuite::analysis::StageRegistry::instance();
    snap.stages.reserve(static_cast<std::size_t>(registry.maxSlots()));

    for (int slot = 0; slot < registry.maxSlots(); ++slot) {
        vxsuite::analysis::StageView view;
        if (!registry.readStage(slot, view))
            continue;

        ++snap.totalSlots;
        if (!view.active)
            continue;
        ++snap.activeSlots;

        const juce::String family = fromChars(view.telemetry.identity.pluginFamily);
        if (family != "VXSuite")
            continue;
        ++snap.vxSlots;

        const bool stale = (nowMs - view.telemetry.state.timestampMs) > kStaleThresholdMs;
        if (!stale)
            ++snap.nonStaleSlots;

        StageSnapshot s;
        s.instanceId       = view.telemetry.identity.instanceId;
        s.analysisDomainId = view.analysisDomainId;
        s.localOrderId     = view.telemetry.identity.localOrderId;
        s.stageId          = fromChars(view.telemetry.identity.stageId);
        s.stageName        = fromChars(view.telemetry.identity.stageName);
        s.pluginFamily     = family;
        s.stale            = stale;
        s.active           = !stale && view.active;
        s.bypassed         = view.telemetry.state.isBypassed;
        s.live             = view.telemetry.state.isLive;
        s.silent           = view.telemetry.state.isSilent;
        s.input            = view.telemetry.inputSummary;
        s.output           = view.telemetry.outputSummary;

        const auto info      = registry.getTrackInfo(s.instanceId);
        s.trackStableId      = info.stableId;
        s.trackReliable      = info.isReliable;
        s.trackName          = info.name;
        s.trackColour        = info.colourARGB != 0 ? juce::Colour(info.colourARGB) : juce::Colour(0);

        snap.stages.push_back(std::move(s));
    }

    return snap;
}

} // namespace vxanalyser
