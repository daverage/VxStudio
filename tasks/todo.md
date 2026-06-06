# Analyser Architecture Rework

## Goal
Fix inter-plugin telemetry so multiple REAPER track instances never evict/merge,
and fix dry/wet display for all selection modes.

## Files
- `VxStudioSpectrumTelemetry.h/.cpp` — TrackInfo.stableId + helper
- `VXStudioAnalyserEditor.h` — new selection model, SelectionSummary, StageEntry.trackStableId
- `VXStudioAnalyserEditor.cpp` — dry/wet helpers, grouping, filter by trackStableId

## Steps

### 1. Framework: TrackInfo.stableId [ ]
Add to `StageRegistry::TrackInfo`:
- `uint64_t stableId` — channelUID hash > runtimeID > ctorInstanceId fallback
- `bool isReliable` — true if channelUID or runtimeID was provided

Update `flushTrackInfoToRegistry()` to compute and store stableId.

### 2. Editor header: new types [ ]
- `StageEntry`: add `trackStableId`, `trackIsReliable`
- Add `SelectionSummary` struct (valid, dry, wet, title, status, contributingIds)
- Replace `selectedStageIndex` + `selectedStageInstanceId` atomics with
  `std::unordered_set<uint64_t> selectedInstanceIds` + `bool fullChainSelectedValue`
- `activeTrackFilter` → `uint64_t` (0=all, else trackStableId)
- Add `trackComboItems: std::vector<uint64_t>` (maps ComboBox ID-1 → trackStableId)

### 3. Editor: refreshRenderModel [ ]
- Populate `entry.trackStableId` from TrackInfo.stableId
- Sort by (trackStableId, localOrderId, instanceId)
- Track filter: compare by trackStableId, not string
- ComboBox: rebuild by trackStableId keys
- Selection validation: use selectedInstanceIds set
- Analyser-only path: valid=true, dry=wet=analyser.inputSummary, status="Waiting for VX stages"

### 4. SelectionSummary helpers [ ]
Static helpers in Editor.cpp:
- `buildIndividualSummary(stage)` — dry=input, wet=output
- `buildMultiSummary(stages)` — dry=earliest input, wet=latest output (linear aggregation)
- `buildTrackSummary(trackId, stages)` — same as multi on one track
- `buildAllTracksSummary(stages)` — per-track earliest/latest, then average in linear domain
- `buildAnalyserFallbackSummary(stage)` — dry=wet=analyser input

All aggregation in linear amplitude, not dB.

### 5. Multi-select UI [ ]
- `selectStage(instanceId, modifierHeld)`: toggle if modifier, else exclusive
- Click on track header: select all stages on that trackStableId
- Stage row: selected if instanceId in selectedInstanceIds

### 6. Debug hooks [ ]
Add coexistence + dry-source checks to diagnostics text (shown when diagnosticsExpanded).

## Key rules
- trackStableId=0 → "unknown"; all unidentified stages group together
- ComboBox item 1 = All Tracks; 2..N → trackComboItems[i-2]
- paint() unchanged — only data feeding it changes
- No host folder-child discovery
