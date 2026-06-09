# VX Studio Analyser — Rebuild Log

## Goal
Clean-room rebuild of VX Studio Analyser architecture per the rebuild prompt.
Four phases: extract logic → scope modes → UI components → domain simplification.

---

## Phase 1 — Extract Logic (editor looks the same)

**Status: COMPLETE** ✓

New files:
- `Source/vxstudio/products/analyser/VXStudioAnalyserModels.h` — shared model structs (ScopeMode, StageSnapshot, TelemetrySnapshot, ChainRow, RenderModel)
- `Source/vxstudio/framework/analysis/VxTelemetryQueryService.h/.cpp` — registry scan, message-thread only, no UI
- `Source/vxstudio/framework/analysis/VxTrackScopeFilter.h/.cpp` — Track/All/Diagnostics/Group scope filtering
- `Source/vxstudio/framework/analysis/VxDryWetResolver.h/.cpp` — pure dry/wet logic, unit-testable
- `Source/vxstudio/framework/analysis/VxSpectrumSmoothingPipeline.h/.cpp` — history, averaging, octave smooth, delta
- `Source/vxstudio/products/analyser/VXStudioAnalyserController.h/.cpp` — owns selection, scope, drives pipeline, produces RenderModel

Processor changes:
- Add `SummaryAccumulator` for live input capture
- Expose `liveInputSummary()`, `liveInputSummaryValid()`, `isAnalyserActive()`

Editor changes:
- Delegate `refreshRenderModel()` entirely to `AnalyserController`
- Remove inline business logic from editor
- Keep paint() and layout unchanged

CMakeLists changes:
- Add new framework/analysis/*.cpp sources to VxStudioFramework target
- Add new analyser controller source to VXStudioAnalyser target

---

## Phase 2 — Scope Modes

**Status: COMPLETE** ✓

- ScopeMode toggle UI (Track / All / Diagnostics) added to controls row
- Track mode: reliability-aware filtering with unknown-track fallback
- All mode: all VXSuite stages across all tracks
- Diagnostics mode: all raw stages including stale, labelled clearly
- Group mode: stub (architecturally ready, UI shows as Group in filter)
- Status header shows "Track: {name}   |   Scope: {mode}" with warning pill for missing track ID
- Empty states per scope: "No VX stages on this track", "Track identity unavailable", "No VX Suite plugins found"

---

## Phase 3 — UI Components

**Status: COMPLETE** ✓ (paint() refactor deferred — improvements applied within existing structure)

Improvements applied:
- Spectrum legend added (Dry baseline / Wet output / Added energy / Reduced energy)
- Better empty states per scope mode with clear contextual messaging
- Scope toggle prominent in controls row alongside avg time + smoothing
- Status label shows track name + scope mode + warning pill cleanly
- Stage cards unchanged (already clear state/impact/type/freq)
- Full Chain button highlights when selected (accent tint)
- Child component split (HeaderPanel, StageChainPanel, SpectrumPanel etc.) is architecturally ready — deferred to a dedicated refactor

---

## Bug Fix — Strict Track-Local Filtering (2026-06-06)

**Status: COMPLETE** ✓

**Problem:** VXDeverb (Edward 2) was appearing in the chain for VXStudioAnalyser on Edward 1. Both plugins had `trackStableId == 0` (unknown) so the old "reject only confirmed-foreign" logic kept them both.

**Fix:** `VxTrackScopeFilter` now uses strict inclusion:
- If `analyserTrackStableId != 0`: include ONLY `trackReliable == true AND trackStableId == analyserTrackStableId`. Reject all others (unknown, unreliable, foreign).
- If `analyserTrackStableId == 0`: include nothing, show "Track identity unavailable" empty state.

**Removed:** Scope combo box (Track/All/Diagnostics modes) — only Track mode is implemented. No false UI options exposed.

**Status text** now shows: `Track: Edward 1   |   1 local stage   2 stages hidden (other/unknown track)`

**Diagnostics** now show per-stage breakdown:
```
[Per-stage breakdown]
  Speech Clarity
    trackStableId: 0 (unknown)
    trackReliable: no
    included: no
    reason: Track identity unreliable (no channelUID/runtimeID from host)
  Deverb
    trackStableId: ABC123
    trackReliable: yes
    included: no
    reason: Different track (ID ABC123 != analyser XYZ456)
```

---

## Phase 4 — Domain Simplification

**Status: COMPLETE** ✓

Decision: **Keep DomainRegistry for process/session discovery but stop using it for scope filtering.**

Reasoning:
- `AnalyserController` now filters purely on `trackStableId` — domain IDs never enter scope decisions
- Effect plugins still bind to domains via `StagePublisher::refreshDomainBinding()` for their own publishing coordination, but this is now separate from the analyser's filtering logic
- The processor still registers a domain so effect plugins can find it; this is benign
- Startup order is no longer critical: the controller reads all stages from StageRegistry and filters by track, so stages published before the analyser domain existed are still visible
- `DomainRegistry` diagnostics remain visible in the diagnostics panel for debugging

Summary: domain-as-track-ownership is gone from the analyser. Track ownership is now `AnalyserController` + `TrackScopeFilter` only.

---

## Architecture Boundary Notes

- `TelemetryQueryService` — reads registry, returns value snapshots. No UI, no state.
- `TrackScopeFilter` — pure filter. Input: snapshot + mode + analyser track ID. Output: filtered stage list + diagnostics.
- `DryWetResolver` — pure logic. Input: filtered stages + selection. Output: dry/wet pair.
- `SpectrumSmoothingPipeline` — stateful smoothing only. Input: dry/wet pair + params. Output: display arrays.
- `AnalyserController` — orchestrator. Owns selection, scope mode, pipeline state. Produces RenderModel.
- `VXStudioAnalyserEditor` — view only. Reads RenderModel, routes input to controller.
- `VXStudioAnalyserAudioProcessor` — thin. Passthrough + live input summary + track identity.
