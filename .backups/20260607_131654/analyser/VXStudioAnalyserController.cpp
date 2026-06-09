#include "VXStudioAnalyserController.h"

#include <algorithm>
#include <cmath>

namespace vxanalyser {

// --- static helpers ---

float AnalyserController::toDb(const float linear, const float floorDb) noexcept {
    return juce::Decibels::gainToDecibels(std::max(1.0e-6f, linear), floorDb);
}

float AnalyserController::bandCenterHz(const int band) noexcept {
    constexpr float kMin = 20.0f;
    constexpr float kMax = 20000.0f;
    const float norm = (static_cast<float>(band) + 0.5f)
                     / static_cast<float>(vxsuite::analysis::kSummarySpectrumBins);
    return kMin * std::pow(kMax / kMin, norm);
}

juce::String AnalyserController::formatFrequency(const float hz) noexcept {
    if (hz >= 1000.0f)
        return juce::String(hz / 1000.0f, hz >= 10000.0f ? 1 : 2) + " kHz";
    return juce::String(juce::roundToInt(hz)) + " Hz";
}

juce::String AnalyserController::signedDb(const float value, const int decimals) noexcept {
    return juce::String(value >= 0.0f ? "+" : "") + juce::String(value, decimals) + " dB";
}

bool AnalyserController::hasMeaningfulBandEnergy(const float bLin, const float aLin) noexcept {
    constexpr float kFloor = -72.0f;
    return std::max(toDb(bLin, -120.0f), toDb(aLin, -120.0f)) > kFloor;
}

juce::String AnalyserController::displayStageName(const juce::String& raw) noexcept {
    auto name = raw.trim();
    if (name.startsWithIgnoreCase("VX Studio "))
        name = name.fromFirstOccurrenceOf("VX Studio ", false, false);
    else if (name.startsWithIgnoreCase("VX"))
        name = name.substring(2);
    name = name.trimStart();
    return name.isEmpty() ? raw : name;
}

juce::String AnalyserController::classLabel(const float spectral, const float dynamic,
                                              const float stereo) noexcept {
    const float maxVal = std::max({ spectral, dynamic, stereo });
    int near = (spectral >= maxVal * 0.8f ? 1 : 0)
             + (dynamic  >= maxVal * 0.8f ? 1 : 0)
             + (stereo   >= maxVal * 0.8f ? 1 : 0);
    if (near >= 2)   return "Mixed";
    if (maxVal == dynamic) return "Dynamic";
    if (maxVal == stereo)  return "Spatial";
    return "Tone";
}

juce::String AnalyserController::impactLabel(const float score) noexcept {
    if (score >= 2.0f)  return "Strong";
    if (score >= 0.75f) return "Moderate";
    return "Low";
}

juce::String AnalyserController::buildToneSummaryLine(
    const float dryRmsDb, const float wetRmsDb,
    const int largestBand, const float largestDeltaDb,
    const bool sparseTone,
    const std::vector<int>& sparseBands,
    const std::array<float, vxsuite::analysis::kSummarySpectrumBins>& deltaDb) noexcept {
    if (sparseTone) {
        juce::StringArray parts;
        const int count = std::min(3, static_cast<int>(sparseBands.size()));
        for (int i = 0; i < count; ++i) {
            const int b = sparseBands[static_cast<std::size_t>(i)];
            parts.add(formatFrequency(bandCenterHz(b)) + " " + signedDb(deltaDb[static_cast<std::size_t>(b)]));
        }
        return "Sparse: " + (parts.isEmpty() ? juce::String("narrowband") : parts.joinIntoString("  "));
    }

    std::array<int, 3> bands { 0, 0, 0 };
    std::array<float, 3> mags { 0.0f, 0.0f, 0.0f };
    for (int i = 0; i < vxsuite::analysis::kSummarySpectrumBins; ++i) {
        const float mag = std::abs(deltaDb[static_cast<std::size_t>(i)]);
        for (int slot = 0; slot < 3; ++slot) {
            if (mag > mags[static_cast<std::size_t>(slot)]) {
                for (int mv = 2; mv > slot; --mv) {
                    mags[static_cast<std::size_t>(mv)]  = mags[static_cast<std::size_t>(mv - 1)];
                    bands[static_cast<std::size_t>(mv)] = bands[static_cast<std::size_t>(mv - 1)];
                }
                mags[static_cast<std::size_t>(slot)]  = mag;
                bands[static_cast<std::size_t>(slot)] = i;
                break;
            }
        }
    }
    juce::StringArray dominant;
    for (int i = 0; i < 3; ++i) {
        if (mags[static_cast<std::size_t>(i)] < 0.75f) continue;
        dominant.add(formatFrequency(bandCenterHz(bands[static_cast<std::size_t>(i)]))
                     + " " + signedDb(deltaDb[static_cast<std::size_t>(bands[static_cast<std::size_t>(i)])]));
    }
    return dominant.isEmpty() ? "No strong band deltas" : dominant.joinIntoString("   ");
}

// --- chain rows ---

void AnalyserController::buildChainRows(const std::vector<StageSnapshot>& inScope,
                                         RenderModel& model) const noexcept {
    model.chainRows.reserve(inScope.size() + 8);

    std::uint64_t currentGroup = ~std::uint64_t(0);
    for (const auto& stage : inScope) {
        // Track header.
        if (stage.trackStableId != currentGroup) {
            currentGroup = stage.trackStableId;
            juce::String headerName = stage.trackName;
            if (headerName.isEmpty())
                headerName = (stage.trackStableId != 0)
                    ? "Track " + juce::String::toHexString(static_cast<juce::int64>(stage.trackStableId)).substring(0, 6).toUpperCase()
                    : "Unknown Track";
            ChainRow hdr;
            hdr.stageName    = headerName;
            hdr.trackColour  = stage.trackColour;
            hdr.trackStableId = stage.trackStableId;
            hdr.isTrackHeader = true;
            model.chainRows.push_back(std::move(hdr));
        }

        const bool isInactive = stage.stale || stage.bypassed || !stage.live;
        const bool isSelected = !isInactive
            && selectedInstanceIds.count(stage.instanceId) > 0;

        // Compute per-stage spectral metrics.
        float spectralSum = 0.0f, dynamicChange = 0.0f, stereoChange = 0.0f;
        float largestDelta = 0.0f;
        int   largestBand  = 0;
        juce::String stageTypeLabel = "Waiting";
        juce::String impactText;
        juce::String freqHint;

        if (!isInactive) {
            std::array<float, vxsuite::analysis::kSummarySpectrumBins> stageDelta {};
            for (int i = 0; i < vxsuite::analysis::kSummarySpectrumBins; ++i) {
                const auto si = static_cast<std::size_t>(i);
                const float d = toDb(stage.output.spectrum[si]) - toDb(stage.input.spectrum[si]);
                stageDelta[si] = d;
                spectralSum += std::abs(d);
                if (std::abs(d) > std::abs(largestDelta)) { largestDelta = d; largestBand = i; }
            }
            spectralSum /= static_cast<float>(vxsuite::analysis::kSummarySpectrumBins);
            dynamicChange = std::abs(toDb(stage.output.rms, -120.0f) - toDb(stage.input.rms, -120.0f));
            stereoChange  = std::abs(stage.output.stereoWidth - stage.input.stereoWidth)
                          + std::abs(stage.output.correlation  - stage.input.correlation);

            const float impactScore = 0.45f * spectralSum + 0.45f * dynamicChange + 0.10f * stereoChange;
            impactText = signedDb(juce::jlimit(-24.0f, 24.0f, largestDelta));
            stageTypeLabel = classLabel(spectralSum, dynamicChange, stereoChange);
            freqHint = "@" + formatFrequency(bandCenterHz(largestBand));
            juce::ignoreUnused(impactScore);
        }

        ChainRow row;
        row.stageName    = displayStageName(stage.stageName);
        row.stateText    = stage.stale    ? "Inactive"
                         : stage.bypassed ? "Bypassed"
                         : !stage.live    ? "Inactive"
                         : stage.silent   ? "Silent"
                                          : "Active";
        row.impactText   = impactText;
        row.typeLabel    = stageTypeLabel;
        row.freqHint     = freqHint;
        row.trackName    = stage.trackName;
        row.trackColour  = stage.trackColour;
        row.trackStableId = stage.trackStableId;
        row.instanceId   = stage.instanceId;
        row.inactive     = isInactive;
        row.selected     = isSelected;
        model.chainRows.push_back(std::move(row));
    }
}

// --- selection resolver ---

DryWetPair AnalyserController::resolveSelection(const std::vector<StageSnapshot>& inScope,
                                                  const StageSnapshot* analyserStage) noexcept {
    // Collect active (non-stale, non-bypassed) stages.
    std::vector<int> activeIndices;
    activeIndices.reserve(inScope.size());
    for (int i = 0; i < static_cast<int>(inScope.size()); ++i) {
        const auto& s = inScope[static_cast<std::size_t>(i)];
        if (!s.stale && !s.bypassed)
            activeIndices.push_back(i);
    }

    // Validate selectedInstanceIds.
    {
        std::unordered_set<std::uint64_t> live;
        for (const auto& s : inScope) live.insert(s.instanceId);
        for (auto it = selectedInstanceIds.begin(); it != selectedInstanceIds.end(); )
            it = live.count(*it) ? std::next(it) : selectedInstanceIds.erase(it);
        if (selectedInstanceIds.empty() && !fullChainSelected)
            fullChainSelected = true;
    }

    if (activeIndices.empty()) {
        if (analyserStage && !analyserStage->stale)
            return dryWetResolver.resolveAnalyserOnly(*analyserStage);
        return dryWetResolver.resolveNoStages();
    }

    if (fullChainSelected) {
        return dryWetResolver.resolveChain(inScope, analyserStage, "full", "Full Chain");
    }

    if (!selectedInstanceIds.empty()) {
        std::vector<const StageSnapshot*> picked;
        for (int i : activeIndices) {
            const auto& s = inScope[static_cast<std::size_t>(i)];
            if (selectedInstanceIds.count(s.instanceId) > 0)
                picked.push_back(&s);
        }
        if (picked.size() == 1)
            return dryWetResolver.resolveSingle(*picked.front());
        if (!picked.empty()) {
            juce::String key = "multi";
            for (const auto* p : picked)
                key += ":" + juce::String(static_cast<juce::int64>(p->instanceId));
            return dryWetResolver.resolveMulti(picked, key);
        }
        // All selected became inactive — fall back to full chain.
        fullChainSelected = true;
    }

    return dryWetResolver.resolveChain(inScope, analyserStage, "full", "Full Chain");
}

// --- diagnostics ---

void AnalyserController::buildDiagnostics(const TelemetrySnapshot& snap,
                                           const ScopeFilterResult& filterResult,
                                           const AnalyserContext& ctx,
                                           const DryWetPair& sel,
                                           const float averageSeconds,
                                           const int smoothingRadius,
                                           RenderModel& model) const noexcept {
    const juce::String selMode = sel.analyserOnly ? "Analyser-only (no local VX stages)"
        : fullChainSelected ? "Full chain"
        : selectedInstanceIds.size() == 1 ? "Single stage"
        : "Multi-select (" + juce::String(static_cast<int>(selectedInstanceIds.size())) + ")";

    juce::String text =
        "[Analyser]\n"
        "  Track ID: " + (filterResult.trackAvailable
                          ? juce::String::toHexString(static_cast<juce::int64>(ctx.trackStableId)).toUpperCase()
                          : "awaiting")
        + "\n  Track name: " + (ctx.trackDisplayName.isEmpty() ? "(none)" : ctx.trackDisplayName)
        + "\n  Domain ID: "  + juce::String::toHexString(static_cast<juce::int64>(ctx.analysisDomainId)).toUpperCase()
        + "\n  Active: "     + juce::String(ctx.isActive ? "yes" : "bypassed")
        + "\n\n[Stage Filter]\n"
        "  Track ID available: "        + juce::String(filterResult.trackAvailable ? "yes" : "no")
        + "\n  VX stages discovered: "  + juce::String(snap.vxSlots)
        + "\n  Included (confirmed): "  + juce::String(filterResult.includedConfirmedCount)
        + "\n  Included (domain fb): "  + juce::String(filterResult.includedDomainCount)
        + "\n  Rejected (foreign): "    + juce::String(filterResult.rejectedForeignCount)
        + "\n  Rejected (unknown): "    + juce::String(filterResult.rejectedUnknownCount)
        + "\n\n[Per-stage breakdown]\n";

    for (const auto& d : filterResult.allStageDiagnostics) {
        text += "  " + d.stageName
            + "\n    trackStableId: "
            + (d.trackStableId != 0
               ? juce::String::toHexString(static_cast<juce::int64>(d.trackStableId)).toUpperCase()
               : "0 (unknown)")
            + "\n    trackReliable: "   + juce::String(d.trackReliable ? "yes" : "no")
            + "\n    analysisDomainId: " + juce::String::toHexString(static_cast<juce::int64>(d.analysisDomainId)).toUpperCase()
            + "\n    included: "        + juce::String(d.included ? "yes" : "no");
        if (d.included && !d.inclusionReason.isEmpty())
            text += "\n    via: " + d.inclusionReason;
        if (!d.included && !d.rejectionReason.isEmpty())
            text += "\n    reason: " + d.rejectionReason;
        text += "\n";
    }

    text += "\n[Registry]\n"
        "  Total slots: "  + juce::String(snap.totalSlots)
        + "\n  Active: "   + juce::String(snap.activeSlots)
        + "\n  VXSuite: "  + juce::String(snap.vxSlots)
        + "\n  Non-stale: " + juce::String(snap.nonStaleSlots)
        + "\n\n[Selection]\n"
        "  " + selMode
        + "\n  Scope key: "   + sel.scopeKey
        + "\n  Avg time: "    + juce::String(averageSeconds, 2) + " s"
        + "\n  Smoothing r: " + juce::String(smoothingRadius);

    model.diagnosticsText = text;
}

// --- main refresh ---

void AnalyserController::reset() noexcept {
    selectedInstanceIds.clear();
    fullChainSelected = true;
    smoothingPipeline.reset();
    currentModel = {};
}

void AnalyserController::refresh(const AnalyserContext& ctx,
                                  const float averageSeconds,
                                  const int smoothingRadius) noexcept {
    // 1. Query.
    const auto snap = queryService.query(ctx.nowMs);

    // 2. Find the analyser's own entry (for the live-input wet fallback).
    const StageSnapshot* analyserStage = nullptr;
    // The analyser registers via DomainRegistry, not StagePublisher, so it typically
    // won't appear in the stage list. The live input comes from the processor's
    // SummaryAccumulator (ctx.liveInput). We synthesise a temporary snapshot for it.
    StageSnapshot liveSnapshot;
    if (ctx.liveInputValid) {
        liveSnapshot.instanceId = 0;   // sentinel — not a real stage
        liveSnapshot.input      = ctx.liveInput;
        liveSnapshot.output     = ctx.liveInput;
        liveSnapshot.stale      = false;
        analyserStage           = &liveSnapshot;
    }

    // 3. Filter — track-based + domain fallback.
    auto filterResult = scopeFilter.filter(snap, ctx.trackStableId,
                                           ctx.analysisDomainId, "vx.studio.analyser");

    // Patch stages whose track identity is unknown (domain-matched only): attribute them
    // to the analyser's track so the chain header shows the real name rather than "Unknown Track".
    if (!ctx.trackDisplayName.isEmpty()) {
        for (auto& s : filterResult.inScope) {
            if (s.trackName.isEmpty())
                s.trackName = ctx.trackDisplayName;
            if (s.trackStableId == 0)
                s.trackStableId = ctx.trackStableId;
        }
    }

    // 4. Bypassed state — stop analysis.
    if (!ctx.isActive) {
        RenderModel model;
        model.bypassed       = true;
        model.selectionTitle = "Analyser Disabled";
        model.statusText     = "Analyser is bypassed";
        model.summaryLines   = { "Enable the plugin in the host to resume analysis.", "", "" };
        currentModel = std::move(model);
        return;
    }

    // 5. Resolve dry/wet.
    DryWetPair sel = resolveSelection(filterResult.inScope, analyserStage);

    // 6. Build chain rows.
    RenderModel model;
    buildChainRows(filterResult.inScope, model);

    // 7. Status text — track name + hidden-stage count.
    const juce::String trackLabel = !filterResult.trackAvailable
        ? "Track: awaiting ID"
        : ctx.trackDisplayName.isNotEmpty()
            ? "Track: " + ctx.trackDisplayName
            : "Track: " + juce::String::toHexString(static_cast<juce::int64>(ctx.trackStableId)).substring(0, 6).toUpperCase();

    const int activeCount = static_cast<int>(std::count_if(filterResult.inScope.begin(),
        filterResult.inScope.end(), [](const StageSnapshot& s){ return !s.stale && !s.bypassed; }));

    const int hiddenTotal = filterResult.rejectedUnknownCount + filterResult.rejectedForeignCount;
    const int shownTotal  = filterResult.includedConfirmedCount + filterResult.includedDomainCount;

    juce::String hiddenNote;
    if (hiddenTotal > 0) {
        hiddenNote = "   " + juce::String(hiddenTotal)
            + " stage" + (hiddenTotal == 1 ? "" : "s")
            + " hidden (other/unknown track)";
    }

    model.trackLabel = trackLabel;
    model.statusText = trackLabel + "   |   "
                     + juce::String(shownTotal) + " local stage"
                     + (shownTotal == 1 ? "" : "s")
                     + (sel.analyserOnly ? " (no VX stages)" : "")
                     + hiddenNote;

    model.valid        = sel.valid;
    model.analyserOnly = sel.analyserOnly;
    model.selectionTitle = sel.valid
        ? ("Spectrum  |  " + (sel.title.isEmpty() ? "Full Chain" : sel.title))
        : !filterResult.trackAvailable ? "" : "";

    // 8. Spectrum smoothing.
    if (model.valid) {
        SpectrumSmoothingPipeline::Input pipeInput;
        pipeInput.dry           = &sel.dry;
        pipeInput.wet           = &sel.wet;
        pipeInput.selectionKey  = sel.scopeKey;
        pipeInput.averageSeconds = averageSeconds;
        pipeInput.smoothingRadius = smoothingRadius;
        pipeInput.nowMs         = ctx.nowMs;

        const auto pipeOut = smoothingPipeline.process(pipeInput);

        model.beforeToneDb  = pipeOut.beforeToneDb;
        model.afterToneDb   = pipeOut.afterToneDb;
        model.deltaToneDb   = pipeOut.deltaToneDb;
        model.largestToneBand = pipeOut.largestToneBand;
        model.sparseTone    = pipeOut.sparseTone;
        model.sparseToneBands = pipeOut.sparseToneBands;

        // Summary lines.
        const float dryRmsDb = toDb(sel.dry.rms, -120.0f);
        const float wetRmsDb = toDb(sel.wet.rms, -120.0f);
        model.summaryLines[0] = "Dry RMS " + juce::String(dryRmsDb, 1)
                              + " dB   Wet RMS " + juce::String(wetRmsDb, 1) + " dB";
        model.summaryLines[1] = "Largest: "
            + signedDb(model.deltaToneDb[static_cast<std::size_t>(model.largestToneBand)])
            + " @ " + formatFrequency(bandCenterHz(model.largestToneBand));
        model.summaryLines[2] = buildToneSummaryLine(
            dryRmsDb, wetRmsDb,
            model.largestToneBand,
            model.deltaToneDb[static_cast<std::size_t>(model.largestToneBand)],
            model.sparseTone, model.sparseToneBands, model.deltaToneDb);

    }

    // Diagnostics always populated (not just when valid) so the panel is informative
    // even during empty-state conditions.
    buildDiagnostics(snap, filterResult, ctx, sel, averageSeconds, smoothingRadius, model);

    currentModel = std::move(model);
}

// --- selection commands ---

void AnalyserController::selectStage(const std::uint64_t instanceId, const bool additive) noexcept {
    if (instanceId == 0) return;
    fullChainSelected = false;
    if (additive) {
        if (selectedInstanceIds.count(instanceId) > 0)
            selectedInstanceIds.erase(instanceId);
        else
            selectedInstanceIds.insert(instanceId);
        if (selectedInstanceIds.empty())
            fullChainSelected = true;
    } else {
        selectedInstanceIds = { instanceId };
    }
}

void AnalyserController::selectTrack(const std::uint64_t trackStableId) noexcept {
    fullChainSelected = false;
    selectedInstanceIds.clear();
    // Select all active stages on that track from the last known chain rows.
    for (const auto& row : currentModel.chainRows) {
        if (!row.isTrackHeader && row.trackStableId == trackStableId
            && !row.inactive && row.instanceId != 0)
            selectedInstanceIds.insert(row.instanceId);
    }
    if (selectedInstanceIds.empty())
        fullChainSelected = true;
}

void AnalyserController::selectFullChain() noexcept {
    selectedInstanceIds.clear();
    fullChainSelected = true;
}

} // namespace vxanalyser
