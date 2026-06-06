The robust answer is: **do not key eviction or ownership by track metadata**.

In VST3, especially in REAPER, track info is useful as a **label**, not as identity. `kChannelUIDKey` is optional host context info, and JUCE forum experience is clear that `updateTrackProperties()` is host-dependent and not reliable enough for core behaviour. Steinberg defines `kChannelUIDKey` as an optional channel ID, not a guaranteed early lifecycle identifier. ([steinbergmedia.github.io][1]) JUCE users also report that track properties may be missing, delayed, or inconsistent across hosts. ([JUCE][2])

## Correct architecture

Use **three separate identities**:

```text
instanceId       = unique plugin instance lifetime identity
streamId         = stable telemetry stream identity
trackContext     = late, mutable display metadata
```

Your current bug is caused by conflating:

```text
stageId + analysisDomain
```

with ownership. That is not unique. Multiple child tracks can legitimately have the same stage and domain.

## Registry key should be

```text
pluginType + instanceId + analysisDomain
```

Not:

```text
stageId + analysisDomain
```

`stageId` should describe processing role, not slot ownership.

Example:

```cpp
struct SlotKey
{
    PluginType pluginType;      // VXClarity
    UUID instanceId;            // generated once per plugin instance
    AnalysisDomain domain;      // vocal, mix, noise, dynamics, etc.
};
```

Then store mutable metadata beside it:

```cpp
struct SlotMetadata
{
    String stageId;             // clarity, denoise, compressor, etc.
    String trackName;           // from updateTrackProperties if/when available
    String channelUid;          // optional
    uint64 processId;
    uint64 heartbeatCounter;
    double lastSeenTime;
};
```

## Eviction should be heartbeat-based, not same-stage cleanup

Do not evict because another instance appears with the same stage/domain.

Evict only when:

```text
same instanceId stops heartbeating
same processId is gone
slot generation is stale
explicit shutdown marker received
```

Use:

```text
slot.claimed == false
or now - lastHeartbeat > timeout
or generation mismatch after restart
```

This allows 20 VXClarity instances on 20 tracks to coexist.

## Track grouping should be best-effort

The master analyser should group by:

1. `channelUid` if present and stable
2. track name if available
3. fallback display name: `VXClarity #7`
4. optional user alias stored in plugin state

But grouping must not decide lifecycle.

In REAPER, assume track metadata can arrive after `prepareToPlay`, after the first `processBlock`, or not at all. REAPER’s own VST notes warn that audio processing can run concurrently with almost everything else, so anything shared with processing must be thread-safe. ([Reaper][3])

## Real-time telemetry path

Use this pattern:

```text
Child plugin audio thread
  writes telemetry to lock-free local buffer
  copies latest snapshot into shared-memory slot
  updates atomic heartbeat/sequence number

Master analyser UI/timer thread
  scans registry
  reads snapshots using sequence guard
  groups by metadata
  renders display
```

Avoid the master doing heavy reads on its audio thread. It is an analyser UI. Let the UI/message/timer thread aggregate.

## Shared memory slot design

Each slot should have:

```cpp
struct TelemetrySlot
{
    std::atomic<uint64_t> sequenceBegin;
    std::atomic<uint64_t> heartbeat;
    std::atomic<uint32_t> state;

    UUID instanceId;
    char pluginType[32];
    char analysisDomain[32];

    TelemetryPayload payload;

    std::atomic<uint64_t> sequenceEnd;
};
```

Writer:

```cpp
sequenceBegin++;
write payload;
heartbeat++;
sequenceEnd = sequenceBegin;
```

Reader:

```cpp
a = sequenceBegin;
copy payload;
b = sequenceEnd;

if (a == b && a is even/stable)
    accept snapshot;
else
    skip this frame;
```

## Instance ID rules

Generate `instanceId` in the constructor or first state load.

Persist it in plugin state **only if you want project reload continuity**.

But be careful with duplicated tracks. If a user duplicates a track, the copied plugin may inherit the same saved ID. So on load you need collision handling:

```text
If restored instanceId already active in registry:
    generate new runtimeInstanceId
    keep restoredId as projectLineageId if useful
```

So the true runtime key should be:

```text
runtimeInstanceId
```

not blindly the saved ID.

## Recommended final model

```text
Shared Registry
 ├── VXClarity / runtimeInstanceId A / spectrum
 ├── VXClarity / runtimeInstanceId B / spectrum
 ├── VXClarity / runtimeInstanceId C / dynamics
 └── VXDeNoise  / runtimeInstanceId D / noiseProfile
```

Each slot advertises:

```text
stageId: clarity
trackName: Lead Vox
channelUid: optional
parentHint: optional
lastSeen: current
```

The master analyser displays:

```text
Lead Vox
  VXClarity: spectrum, dynamics

Backing Vox
  VXClarity: spectrum

Unknown Track
  VXClarity #3
```

## Bottom line

Your eviction model is wrong, not just the REAPER callback timing.

Use **instance-owned slots**, heartbeat expiry, and late-bound track metadata. Treat `kChannelUIDKey` and `updateTrackProperties()` as display hints only. In REAPER, that is the difference between a stable inter-plugin telemetry system and one that randomly deletes valid instances.

[1]: https://steinbergmedia.github.io/vst3_doc/vstinterfaces/namespaceSteinberg_1_1Vst_1_1ChannelContext.html?utm_source=chatgpt.com "Steinberg::Vst::ChannelContext Namespace Reference"
[2]: https://forum.juce.com/t/how-to-call-the-updatetrackproperties-function-to-display-the-name-on-my-plugins-gui/58185?utm_source=chatgpt.com "How to call the updateTrackProperties function to display ..."
[3]: https://www.reaper.fm/sdk/vst/vst_ext.php "REAPER | Extensions to VST SDK"
