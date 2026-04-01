Good. Naming it **VxMixStudio** forces a higher bar. It implies a system, not a tool. So the spec needs to be precise enough that an agent cannot “interpret” behaviour loosely or drift across modules.

What follows is a **single, cohesive technical specification**. It defines:

* DSP behaviour (not just intent)
* data flow and structures
* thresholds and formulas
* deterministic outputs for conflict + suggestion

No ambiguity, no “AI magic”.

---

# VxMixStudio — Full Technical Specification

## 1. System Overview

**Type:** Multi-instance analysis + decision engine (VST3/AU)
**Components:**

* Track Node (per track)
* Hub (aggregator + UI)
* Optional Resolve processor (later phase)

**Core Loop:**

1. Each Track Node produces `SpectrumFrame`
2. Hub aggregates frames into `OwnershipMap`
3. Conflict engine produces `ConflictRecords`
4. Suggestion engine produces ranked `Suggestions`
5. UI renders spectrum + ownership + conflicts + actions

---

# 2. Audio & DSP Foundations

## 2.1 Signal Handling Constraints

* Internal format: `float32`
* Channels: preserve input (mono/stereo)
* No resampling between nodes
* Analysis must not modify audio path

---

## 2.2 Multi-Resolution FFT

You must run **three parallel FFTs**:

| Window | Size  | Purpose            |
| ------ | ----- | ------------------ |
| Short  | 1024  | transient clarity  |
| Medium | 4096  | general balance    |
| Long   | 16384 | low-end resolution |

### Window Function

Use Hann window.

### Overlap

* Short: 75%
* Medium: 75%
* Long: 50%

---

## 2.3 Frequency Bin Mapping

Convert FFT bins → perceptual bins using **log grouping**.

### Perceptual Bands

| Band     | Range      |
| -------- | ---------- |
| Sub      | 20–60 Hz   |
| Low      | 60–120 Hz  |
| Low-mid  | 120–400 Hz |
| Mid      | 400–2k Hz  |
| Presence | 2–5k Hz    |
| Air      | 5–12k Hz   |
| Ultra    | 12–20k Hz  |

Also retain fine bins (~256–512 bins total after grouping).

---

## 2.4 Energy Calculation

For each bin:

```id="i29kag"
energy = sqrt(real^2 + imag^2)
```

Convert to dB:

```id="o5r8w7"
dB = 20 * log10(energy + 1e-9)
```

---

## 2.5 Smoothing

### Time smoothing (per bin):

```id="sjkbgv"
smoothed = alpha * current + (1 - alpha) * previous
```

Where:

* alpha_short = 0.6
* alpha_medium = 0.3
* alpha_long = 0.1

---

## 2.6 Transient Score

Compute:

```id="42cvl2"
transient = max(0, peak - rms)
```

Normalize:

```id="nf61l7"
transientScore = clamp(transient / (rms + 1e-6), 0, 1)
```

---

## 2.7 Stereo Width (per band)

```id="a9n1ph"
width = (L - R) / (|L| + |R| + 1e-6)
```

Absolute value averaged per band.

---

## 2.8 Low-End Phase Correlation

Below 120 Hz:

```id="s51x42"
corr = sum(L * R) / sqrt(sum(L^2) * sum(R^2) + 1e-9)
```

Range: [-1, 1]

---

# 3. Data Transport

## 3.1 Frame Rate

* Analysis frame rate: 50 Hz (every 20 ms)
* UI update: 30–60 FPS

---

## 3.2 SpectrumFrame Structure

```id="b2f8mn"
struct SpectrumFrame {
  uint64 timestamp;
  float bins[512];
  float smoothedBins[512];
  float bandEnergy[7];
  float transientScore;
  float stereoWidth[7];
  float phaseLow;
};
```

---

## 3.3 Transport Mechanism

* Lock-free ring buffer per instance
* Shared registry keyed by plugin class
* No blocking on audio thread
* Max buffer depth: 128 frames

---

# 4. Ownership Model

## 4.1 Per-Bin Ownership

For bin `i`:

```id="k7q6ty"
totalEnergy = sum(trackEnergy[i])
ownership(track) = trackEnergy[i] / (totalEnergy + 1e-9)
```

---

## 4.2 Stability Filter

Apply temporal smoothing:

```id="r3xkl9"
ownershipSmoothed = 0.7 * current + 0.3 * previous
```

---

## 4.3 Dominant Track

```id="1rx7ju"
dominant = argmax(ownershipSmoothed)
```

Confidence:

```id="m6ewh2"
confidence = maxOwnership - secondMaxOwnership
```

---

# 5. Conflict Detection

## 5.1 Precondition

Only evaluate bins where:

```id="z0x7eq"
totalEnergy > -60 dB
```

---

## 5.2 Spectral Overlap

For tracks A, B:

```id="g0gshc"
overlap = min(energyA, energyB) / max(energyA, energyB)
```

---

## 5.3 Time Coincidence

Binary:

```id="c2cm9e"
active = (energy > threshold)
```

Where threshold = -50 dB

Coincidence:

```id="51sc37"
timeCoincidence = activeA * activeB
```

---

## 5.4 Perceptual Weight

Apply weighting curve:

* 2–5 kHz = 1.0
* 400 Hz–2 kHz = 0.8
* 120–400 Hz = 0.7
* 60–120 Hz = 0.6
* <60 Hz = 0.5
* > 5 kHz = 0.6

---

## 5.5 Role Conflict Factor

```id="7sqy5n"
roleConflict = abs(priorityA - priorityB)
```

Invert:

```id="w2g4mq"
roleFactor = 1 - roleConflict
```

---

## 5.6 Dominance Uncertainty

```id="56m0q7"
uncertainty = 1 - confidence
```

---

## 5.7 Final Interference Score

```id="vaz9z9"
interference =
  overlap *
  timeCoincidence *
  perceptualWeight *
  roleFactor *
  uncertainty
```

Clamp 0–1.

---

## 5.8 Conflict Thresholds

| Score   | Meaning  |
| ------- | -------- |
| <0.2    | ignore   |
| 0.2–0.4 | mild     |
| 0.4–0.7 | moderate |
| >0.7    | severe   |

---

## 5.9 Conflict Types (Rules)

### Transient Masking

```id="y8yrv1"
if transientA > 0.6 AND transientB > 0.6 AND overlap > 0.5
```

### Sustained Masking

```id="il7eq5"
if transientA < 0.3 AND transientB < 0.3 AND overlap > 0.5
```

### Low-End Conflict

```id="sohujd"
if frequency < 120 Hz AND overlap > 0.4
```

### Stereo Masking

```id="f8a1qk"
if both width < 0.2
```

### Phase Risk

```id="odn4rj"
if phaseLow < 0.2 AND frequency < 120 Hz
```

---

# 6. Suggestion Engine

## 6.1 Candidate Generation

For each conflict:

* generate 1–3 candidate actions

---

## 6.2 Track Selection Rule

Track that moves:

```id="q3ixc6"
loser = lower priority track
```

Override:

* if transient vs sustained → sustained loses
* if center vs wide → wide loses first

---

## 6.3 Suggestion Types

### Static EQ

```id="cmt8su"
gain = -min(3 dB, interference * 4 dB)
Q = map bandwidth → 0.7–2.0
```

---

### Dynamic EQ

Triggered if:

```id="6j3l0v"
timeCoincidence < 1 OR transient conflict
```

Parameters:

```id="af3a2f"
gain = -interference * 5 dB
attack = 10–40 ms
release = 80–200 ms
```

---

### Spectral Ducking

For strong conflicts:

```id="11h1o5"
if interference > 0.7
```

---

### M/S Suggestion

```id="j95k2u"
if stereoMasking AND width < 0.2
```

→ widen losing track

---

### Frequency Shift

```id="n7c4z3"
if sustained conflict in mid bands
```

→ suggest ±200–500 Hz shift

---

## 6.4 Suggestion Ranking

```id="l9k3e4"
score =
  interference *
  expectedBenefit *
  confidence
```

Keep top 5.

---

## 6.5 Expected Benefit

Estimate:

```id="2g0wvv"
benefit = interference * (priorityDifference + 0.5)
```

---

## 6.6 Confidence

```id="l6k2n9"
confidence =
  ownershipConfidence *
  timeStability *
  consistencyOverFrames
```

---

# 7. UI SPECIFICATION

## 7.1 Spectrum View

* log frequency axis (20 Hz–20 kHz)
* dB range: -90 to 0 dB
* grid lines at standard bands

---

## 7.2 Ownership View

Render:

* stacked translucent fields
* dominant track = brightest
* others fade proportionally

---

## 7.3 Conflict Heatmap

Overlay:

* red = masking
* magenta = priority conflict
* blue = phase risk
* amber = mild overlap

---

## 7.4 Suggestion Panel

Each item:

* Title: conflict summary
* Range: Hz
* Tracks involved
* Reason
* Fix
* Alternative
* Risk
* Confidence %

---

## 7.5 Interaction

* hover = preview curve
* click = isolate conflict
* apply = send to Resolve plugin (future)

---

# 8. PERFORMANCE LIMITS

* CPU per Track Node: <2%
* Hub: <5%
* Memory per instance: <10 MB
* No blocking calls in audio thread

---

# 9. FAILURE MODES (MUST HANDLE)

* Missing tracks → degrade gracefully
* Sparse data → suppress suggestions
* Noise-only input → ignore
* Silent tracks → excluded
* High CPU → reduce FFT resolution dynamically

---

# 10. DETERMINISM RULES

To prevent drift:

* Same input → same suggestions
* No randomization
* All thresholds fixed
* No ML in v1

---

# 11. EXTENSIBILITY

Future additions must not break:

* ownership model
* interference formula
* role system

ML (later) can only adjust:

* weights
* thresholds
* ranking

Never core structure.

---

# 12. PRODUCT IDENTITY

VxMixStudio is:

* not an EQ
* not an AI auto-mixer
* not a spectrum analyzer

It is:

**a deterministic system that models frequency ownership, detects meaningful interference, and proposes the least destructive fix based on role and context**

---

# Final note

If you build this exactly as specified, you will already surpass most tools in this space.

Where it will succeed or fail is not DSP accuracy.
That part is straightforward.

It will succeed or fail on one thing:

**whether the suggestions feel consistently “right” to an experienced engineer within 2–3 uses.**

If they do, this becomes something people keep open on every mix.
If they don’t, it becomes another analyzer.

---

If you want the next step, we should define:

* exact JUCE class structure
* threading model
* and how Resolve integrates without creating routing chaos

That’s the part that usually breaks otherwise solid DSP designs.
Good. The framework is a strong fit for the **processor shell**, parameter plumbing, default editor baseline, block smoothing, and output safety. It is not enough by itself for inter-instance coordination, rich analyzer rendering, or a conflict engine, so VxMixStudio should sit on top of it rather than trying to force everything through the existing two-knob product pattern. The key reusable pieces are `ProductIdentity`, `ProcessorBase`, `EditorBase`, block smoothing, and `OutputTrimmer`. The framework is explicitly designed so the product-specific DSP lives inside `prepareSuite`, `resetSuite`, and `processProduct`, with the rest handled by the base classes.  

# VxMixStudio

## Single Detailed Technical Specification

### JUCE + VX Suite Framework Integration Plan

## 1. Product definition

**VxMixStudio** is a multi-instance analyzer and mix-conflict decision system for VST3/AU.

It does four things:

1. Captures per-track spectral, temporal, stereo, and role metadata
2. Aggregates active tracks into a shared mix model
3. Detects meaningful interference, not just overlap
4. Produces ranked, explainable suggestions and optional corrective actions

This is a **studio coordination plugin**, not just an EQ or a graph.

---

## 2. Product family structure

Build it as **three related plugin targets** under the VX Suite structure.

### A. VxMixStudioTrack

Inserted on tracks you want included in the shared mix model.

Purpose:

* analyze local audio
* register track identity
* publish analysis frames
* optionally receive corrective instructions later

### B. VxMixStudioHub

Inserted on a bus, mix bus, or any visible location the user prefers.

Purpose:

* collect all Track nodes
* render the full UI
* calculate ownership and conflict
* generate ranked suggestions
* manage snapshots, filters, and priorities

### C. VxMixStudioResolve

Optional later processor.

Purpose:

* apply accepted actions
* dynamic EQ
* static EQ
* sidechain keyed shaping
* M/S relocation

This split is important. It stops the main product becoming a routing mess and makes failure modes easier to handle.

---

## 3. VX Suite framework fit

Use the framework for what it already does well:

* `ProcessorBase` as the plugin shell
* `ProductIdentity` for consistent product naming, labels, and base parameter registration
* `EditorBase` only as a temporary fallback or mini editor basis
* `VxSuiteBlockSmoothing.h` for non-audio-thread-hostile parameter smoothing
* `VxSuiteOutputTrimmer.h` for optional output safety on Resolve, not on pure analyzers
* standard CMake plugin registration and staging   

Where the framework is **not** enough:

* multi-pane analyzer UI
* inter-instance registry and ring-buffer transport
* ownership/conflict engines
* large custom editor rendering
* suggestion persistence and application model

So the right approach is:

**Framework for shell and base ergonomics. Custom subsystem for analysis, transport, UI, and decision logic.**

---

## 4. Repository layout

Recommended layout inside the VX Suite repo:

```text
Source/vxsuite/
├── framework/
├── common/
│   └── mixstudio/
│       ├── core/
│       │   ├── VxMixStudioTypes.h
│       │   ├── VxMixStudioConstants.h
│       │   ├── VxMixStudioRoles.h
│       │   ├── VxMixStudioLabels.h
│       │   └── VxMixStudioMath.h
│       ├── dsp/
│       │   ├── VxMixSpectrumAnalyzer.h
│       │   ├── VxMixTransientAnalyzer.h
│       │   ├── VxMixStereoAnalyzer.h
│       │   ├── VxMixPhaseAnalyzer.h
│       │   ├── VxMixBandMapper.h
│       │   └── VxMixFeatureExtractor.h
│       ├── comms/
│       │   ├── VxMixInstanceRegistry.h
│       │   ├── VxMixSharedFrameBus.h
│       │   ├── VxMixRingBuffer.h
│       │   └── VxMixSessionModel.h
│       ├── engine/
│       │   ├── VxMixOwnershipEngine.h
│       │   ├── VxMixConflictEngine.h
│       │   ├── VxMixSuggestionEngine.h
│       │   ├── VxMixPriorityModel.h
│       │   └── VxMixActionModel.h
│       ├── ui/
│       │   ├── VxMixTheme.h
│       │   ├── VxMixSpectrumView.h
│       │   ├── VxMixOwnershipView.h
│       │   ├── VxMixConflictHeatmapView.h
│       │   ├── VxMixSuggestionPanel.h
│       │   ├── VxMixTrackListView.h
│       │   ├── VxMixBottomTabs.h
│       │   └── VxMixHubEditor.h
│       └── resolve/
│           ├── VxMixDynamicEqKernel.h
│           ├── VxMixStaticEqKernel.h
│           ├── VxMixMsRouter.h
│           └── VxMixResolvePlan.h
└── products/
    ├── mixstudio_track/
    │   ├── VxMixStudioTrackProcessor.h
    │   └── VxMixStudioTrackProcessor.cpp
    ├── mixstudio_hub/
    │   ├── VxMixStudioHubProcessor.h
    │   ├── VxMixStudioHubProcessor.cpp
    │   ├── VxMixStudioHubEditor.cpp
    │   └── VxMixStudioHubEditor.h
    └── mixstudio_resolve/
        ├── VxMixStudioResolveProcessor.h
        └── VxMixStudioResolveProcessor.cpp
```

---

## 5. Core runtime model

### 5.1 Track nodes are authoritative for local audio facts

A Track node computes analysis locally and never depends on the Hub to function.

### 5.2 The Hub is authoritative for mix reasoning

The Hub consumes published frames and derives:

* ownership
* conflict records
* suggestion ranking
* global labels
* session snapshots

### 5.3 Resolve is authoritative for accepted corrective processing

Resolve is the only place audio changes should happen automatically.

This separation keeps causality clear.

---

## 6. Plugin identities and parameter strategy

The current framework assumes a simpler product identity with primary and secondary knobs plus mode/listen conventions. That is fine for basic VX products, but VxMixStudio needs a broader parameter set and a custom editor. Still, keep `ProductIdentity` for naming, theme, and standard product shell setup. 

### 6.1 Track node parameters

Core:

* role
* subgroup
* importance
* visibility
* publish enable
* analysis speed
* freeze long-term profile
* label preset
* input trim meter only, not gain
* bypass publish

Advanced:

* transient sensitivity
* masking sensitivity
* stereo weighting
* low-end phase watch
* track alias name

### 6.2 Hub parameters

Core:

* analysis source mode
* show all / filtered
* conflict threshold
* suggestion aggressiveness
* ownership smoothing
* time window
* spectrum mode
* snapshot compare
* audition preview
* profile preset

### 6.3 Resolve parameters

Core:

* action slot enable 1..8
* action type
* target range
* gain amount
* Q
* attack
* release
* sidechain source ID
* M/S mode
* dry/wet

### 6.4 Framework adaptation

Do not try to force these into only `primaryParamId` and `secondaryParamId`. Instead:

* use `ProcessorBase` for lifecycle and common behavior
* override parameter layout creation for the VxMixStudio products if needed
* override `createEditor()` with custom editors for Track and Hub
* keep theme and base product identity consistent with VX Suite naming and appearance

The framework already expects custom processors to override the three lifecycle methods and allows overriding `createEditor()` for custom layouts. 

---

## 7. Musical labels and real-world labels

This matters because suggestions should talk like an engineer, not a spreadsheet.

## 7.1 Role labels

Each track gets one primary role.

Musical:

* Kick
* Bass
* Sub Bass
* Snare
* Clap
* Hi-Hat
* Cymbals
* Percussion
* Rhythm Guitar
* Lead Guitar
* Acoustic Guitar
* Piano
* Keys
* Organ
* Pads
* Strings
* Brass
* Lead Vocal
* Backing Vocal
* Hook
* Lead Synth
* Arp
* FX
* Atmosphere
* Noise Texture
* Drum Bus
* Music Bus
* Mix Bus

Voice and post:

* Dialogue
* Voiceover
* Podcast Host
* Podcast Guest
* Ambience
* Foley
* SFX Impact
* Trailer Hit
* Background Music
* Announcer
* Brand Cue

## 7.2 Functional labels

A track can have multiple functional labels.

* Low-End Anchor
* Intelligibility Carrier
* Transient Driver
* Sustained Bed
* Center Focus
* Wide Filler
* Groove Driver
* Tonal Support
* Motion Texture
* Presence Owner
* Air Owner
* Mono Critical
* Background Only

## 7.3 Conflict labels

Generated by engine.

* Sub Crowding
* Kick Masking
* Bass Smear
* Mud Build-Up
* Boxiness Clash
* Nasal Congestion
* Presence Fight
* Intelligibility Loss
* Harshness Stack
* Sibilance Pile-Up
* Air Pile-Up
* Center Congestion
* Stereo Wash
* Transient Blunting
* Sustain Veil
* Mono Risk
* Phase Risk
* Register Clash
* Chorus Density Overload
* Bed Too Forward

## 7.4 Suggestion labels

These are the actions shown to the user.

* Protect Lead
* Yield to Lead
* Carve Narrowly
* Share Band
* Widen Support
* Recenter Focus
* Duck on Demand
* Soften Transient
* Shift Presence
* Lift Above
* Clear Low-Mids
* Tighten Sub
* Leave As Is

## 7.5 Use-case labels and presets

Factory profiles should change priority defaults and labeling emphasis.

Presets:

* Pop Mix
* Rock Mix
* EDM Mix
* Hip-Hop Mix
* Podcast
* Dialogue Over Score
* Trailer Mix
* Broadcast Voice
* Live Multitrack Prep
* Acoustic Ensemble

Example:
In Podcast mode, Dialogue or Host gets very high intelligibility priority and music bed gets auto-downgraded in presence bands.

---

## 8. Detailed class structure

## 8.1 Common type layer

```cpp
enum class VxMixRole : uint16_t;
enum class VxMixFunctionTag : uint16_t;
enum class VxMixConflictType : uint16_t;
enum class VxMixSuggestionType : uint16_t;
enum class VxMixSeverity : uint8_t;
enum class VxMixBand : uint8_t;
```

Core structs:

```cpp
struct VxMixTrackDescriptor;
struct VxMixSpectrumFrame;
struct VxMixTrackState;
struct VxMixOwnershipBin;
struct VxMixConflictRecord;
struct VxMixSuggestion;
struct VxMixResolveAction;
struct VxMixSessionSnapshot;
```

## 8.2 Track processor classes

```cpp
class VxMixStudioTrackProcessor final : public vxsuite::ProcessorBase;
class VxMixTrackAnalysisController;
class VxMixSpectrumAnalyzer;
class VxMixTransientAnalyzer;
class VxMixStereoAnalyzer;
class VxMixPhaseAnalyzer;
class VxMixFeatureExtractor;
class VxMixTrackPublisher;
```

Responsibilities:

* audio-thread-safe feature extraction
* publish compact frame payloads
* expose local mini status

## 8.3 Hub processor classes

```cpp
class VxMixStudioHubProcessor final : public vxsuite::ProcessorBase;
class VxMixSessionCoordinator;
class VxMixFrameCollector;
class VxMixOwnershipEngine;
class VxMixConflictEngine;
class VxMixPriorityModel;
class VxMixSuggestionEngine;
class VxMixSuggestionStore;
class VxMixSnapshotManager;
```

Responsibilities:

* consume frames
* maintain recent per-track history
* build derived models
* feed UI state

## 8.4 Hub UI classes

```cpp
class VxMixStudioHubEditor : public juce::AudioProcessorEditor;
class VxMixTrackListView;
class VxMixSpectrumView;
class VxMixOwnershipView;
class VxMixConflictHeatmapView;
class VxMixSuggestionPanel;
class VxMixBottomTabView;
class VxMixDetailsDrawer;
class VxMixFilterBar;
```

## 8.5 Resolve classes

```cpp
class VxMixStudioResolveProcessor final : public vxsuite::ProcessorBase;
class VxMixResolvePlanStore;
class VxMixDynamicEqKernel;
class VxMixStaticEqKernel;
class VxMixBandDucker;
class VxMixMsRouter;
class VxMixSidechainBinding;
```

---

## 9. Threading model

This is where most analyzer plugins become unstable. So keep it rigid.

## 9.1 Threads

### Audio thread

Must only do:

* parameter reads
* no-lock analysis accumulation
* ring buffer push of compact frames
* local state updates with fixed storage
* optional pass-through audio

Must never do:

* heap allocation
* file I/O
* sorting
* logging
* UI messaging
* cross-instance locking

### Analysis worker thread

One worker per plugin instance type or one shared worker pool with strict ownership.

Track node worker:

* converts STFT accumulations into publishable frames
* rate limits frame output

Hub worker:

* collects latest frames
* rebuilds ownership/conflict/suggestions at fixed cadence, e.g. 20 Hz

### Message thread

UI only:

* consume immutable snapshots from Hub
* user interactions
* filtering and selection
* preview state only

## 9.2 Data cadence

Recommended:

* audio accumulation continuous
* Track publish rate: 20 to 50 Hz
* Hub reasoning pass: 10 to 20 Hz
* UI redraw: 30 to 60 FPS

Do not run conflict ranking at UI frame rate.

## 9.3 Memory model

Use:

* preallocated ring buffers
* fixed-capacity vectors or arrays for current session limits
* immutable snapshot copies from engine to UI
* generation counters for snapshot coherency

---

## 10. Inter-instance communication

The framework does not provide this, so build a dedicated subsystem.

## 10.1 Session model

Each active instance registers with a global process-local registry:

* instance UUID
* plugin kind: Track, Hub, Resolve
* host sample rate
* channel config
* friendly name
* role
* last heartbeat

## 10.2 Transport

Recommended approach:

* lock-free single-producer single-consumer ring buffer per Track instance
* Hub polls all active Track buffers
* transport only compact analysis data, not raw audio

### Publish payload

```cpp
struct VxMixSpectrumFrame {
    uint64_t timestampSamples;
    uint32_t trackIdHash;
    float bandEnergy[7];
    float perceptualBins[256];
    float smoothedBins[256];
    float transientScore;
    float stereoWidth[7];
    float phaseLow;
    float centroid;
    float flatness;
    uint32_t activityMask;
};
```

## 10.3 Time alignment

Use host sample position when available.
Fallback:

* monotonic sample counter per instance

Hub aligns frames into nearest analysis bucket by timestamp tolerance.

## 10.4 Failure handling

If a Track node disappears:

* mark stale after 500 ms
* fade out its data in UI over 1 s
* suppress suggestions involving it after stale timeout

---

## 11. DSP analysis implementation

## 11.1 STFT core

Per Track instance:

* 1024 short FFT, 75% overlap
* 4096 medium FFT, 75% overlap
* 16384 long FFT, 50% overlap

Hann windows throughout.

Blend logic:

* short dominates transient and high band features
* medium dominates general display
* long dominates sub and low ownership accuracy

## 11.2 Band model

Primary display and logic bands:

* Sub: 20 to 60 Hz
* Low: 60 to 120 Hz
* Low-Mid: 120 to 400 Hz
* Mid: 400 Hz to 2 kHz
* Presence: 2 to 5 kHz
* Air: 5 to 12 kHz
* Ultra: 12 to 20 kHz

Internal fine bins: 256 perceptual bins, log spaced.

## 11.3 Feature extraction

Per frame compute:

* linear bin energy
* dB bin energy
* smoothed bins
* band energy sums
* transient score
* spectral centroid
* spectral flatness
* band stereo width
* low-band phase correlation
* active band bitmask
* long-term moving average

## 11.4 Long-term profile

Keep:

* 10 s moving average
* 60 s moving average optional if CPU allows
* recent burst history for event-based conflict labeling

---

## 12. Ownership engine

This is the key differentiator.

For each perceptual bin:

* total energy = sum of active tracks
* ownership(track) = track energy / total energy
* smooth ownership over time
* compute dominant owner and ownership confidence

Derived labels:

* stable owner
* shared band
* unstable ownership
* contested band

### Ownership confidence

Use:

* difference between first and second owner
* stability across N recent frames
* track activity consistency

### Ownership summaries

Generate readable descriptions:

* Vocal owns 2.1 to 4.8 kHz in chorus
* Guitar and synth share 700 Hz to 1.3 kHz
* Kick ownership unstable at 55 to 80 Hz

---

## 13. Conflict engine

## 13.1 Pre-filtering

Only evaluate bins where total level exceeds a floor, e.g. -60 dBFS equivalent normalized analyzer energy.

Ignore:

* silent tracks
* bins below noise floor
* unstable one-off blips unless transient mode is enabled

## 13.2 Core interference logic

Pairwise track conflict score per bin is derived from:

* spectral overlap
* time coincidence
* perceptual weight
* priority relationship
* ownership uncertainty
* functional role mismatch
* stereo position conflict
* transient relevance

Then aggregate neighboring bins into meaningful ranges.

## 13.3 Conflict aggregation

Merge adjacent bins into a single conflict zone when:

* same track pair
* same conflict type
* gap smaller than max merge distance
* stable over minimum frame count

## 13.4 Conflict types

* Spectral Masking
* Transient Masking
* Sustained Masking
* Low-End Collision
* Center Congestion
* Phase Risk
* Stereo Wash
* Register Clash
* Intelligibility Conflict
* Density Overload

## 13.5 Conflict severity

Map final score:

* 0.00 to 0.19 ignore
* 0.20 to 0.39 mild
* 0.40 to 0.69 moderate
* 0.70 to 1.00 severe

---

## 14. Priority and role logic

## 14.1 Default priorities

Examples:

* Lead Vocal: 1.00
* Dialogue: 1.00
* Podcast Host: 1.00
* Kick: 0.95
* Snare: 0.90
* Bass: 0.90
* Hook: 0.88
* Lead Guitar: 0.80
* Rhythm Guitar: 0.70
* Pads: 0.50
* Atmosphere: 0.35
* Background Music in dialogue profile: 0.30

## 14.2 Band-specific priority modifiers

Example:

* Kick gets higher priority in 40 to 90 Hz than in 2 to 4 kHz
* Vocal gets highest priority in 1.5 to 5 kHz
* Pads lose priority in center-focused presence range
* Dialogue gets hard protection in intelligibility bands

## 14.3 Functional overrides

* sustained bed loses to transient driver in the same band
* wide filler loses to center focus
* non-mono-critical source loses in sub if mono-critical source conflicts

---

## 15. Suggestion engine

The point is not to produce many suggestions. It is to produce very few that feel right.

## 15.1 Candidate actions

For each conflict, generate up to three actions:

* preferred
* alternate
* conservative

## 15.2 Action families

* Static bell cut
* Dynamic bell cut
* Dynamic shelf
* Narrow-band ducking
* M/S widening of support source
* Center protection
* Low-mid cleanup
* Sub tightening
* Presence relocation
* Arrangement warning only
* Leave unchanged

## 15.3 Winner/loser determination

Track that should move:

* lower priority loses
* sustained bed loses to transient source
* wide support loses to center focus
* background music loses to dialogue
* FX loses to hook unless user override says otherwise

## 15.4 Suggestion record

Each suggestion contains:

* ID
* involved tracks
* preferred action type
* frequency range
* exact parameter suggestion
* why
* confidence
* expected audible benefit
* risk note
* apply target: Track, Resolve, manual only

## 15.5 Ranking

Rank by:

* interference severity
* confidence
* expected benefit
* collateral damage estimate
* user profile relevance

Return top 5 only.

---

## 16. Resolve integration without routing chaos

This is where the design must stay realistic.

## 16.1 Resolve modes

### Manual mode

Hub suggests settings. User applies them manually in Resolve.

### Linked mode

Hub emits a `ResolvePlan` that a Resolve instance on a selected track can pull.

### Semi-auto mode

User presses Apply in Hub, selected Resolve instance accepts action slot.

Do not attempt fully automatic global retargeting in v1.

## 16.2 Resolve plan format

```cpp
struct VxMixResolveAction {
    uint32_t actionId;
    uint32_t targetTrackHash;
    VxMixSuggestionType type;
    float freqHz;
    float q;
    float gainDb;
    float attackMs;
    float releaseMs;
    bool useExternalKey;
    uint32_t externalKeyTrackHash;
    bool midOnly;
    bool sideOnly;
};
```

## 16.3 Sidechain policy

Where host supports sidechain:

* Resolve can accept sidechain from key track

Where host does not:

* suggestion remains manual
* UI marks sidechain action as unavailable

## 16.4 Resolve UI concept

Keep it practical:

* 8 action slots
* each slot shows source suggestion and current state
* bypass per slot
* intensity trim
* dry/wet
* output trimmer optional

This is where `OutputTrimmer` from the VX framework is useful because Resolve may create unexpected cumulative gain behavior. 

---

## 17. Hub UI specification

Because the framework’s default editor is a generic editor for simpler plugin layouts, VxMixStudioHub should use a custom editor from the start. The README explicitly supports overriding `createEditor()` when a custom layout is needed. 

## 17.1 Layout

Left:

* Track list
* role icons
* priority sliders
* conflict score
* visibility and filter toggles

Center:

* main graph region
* tabs: Spectrum, Ownership, Conflict, Stereo, Low-End, Time

Right:

* ranked suggestions
* details drawer
* preview controls
* apply / pin / ignore

Bottom:

* timeline conflict strip
* selected pair history
* snapshot compare

## 17.2 Signature view

Not just line overlays.

Main render modes:

* Spectrum Overlay
* Ownership Field
* Conflict Heatmap
* Pair Isolation

The Ownership Field should be your signature. It shows who owns the mix, not just who is loud.

## 17.3 Preview behavior

On hover over suggestion:

* show ghost EQ curve
* highlight affected bins
* highlight winning and yielding tracks
* show expected result caption, not fake audio change

---

## 18. Track UI specification

Track node editor should stay compact.

Show:

* current role
* publish status
* mini spectrum
* conflict summary badge
* importance slider
* local labels
* route to Hub status

Do not try to replicate the full Hub there.

---

## 19. Custom editor strategy with VX framework

Recommended approach:

* `VxMixStudioTrackProcessor` derives from `ProcessorBase`
* override `createEditor()` to return `VxMixStudioTrackEditor`
* `VxMixStudioHubProcessor` derives from `ProcessorBase`
* override `createEditor()` to return `VxMixStudioHubEditor`
* `VxMixStudioResolveProcessor` derives from `ProcessorBase`
* either custom editor or extend `EditorBase` if compact enough

This matches the framework’s intended extension model and avoids fighting the built-in editor assumptions. 

---

## 20. Status text and diagnostics

Use `getStatusText()` heavily.

Examples:

* 6 tracks active
* no Hub detected
* stale session data
* low-end phase risk on 2 tracks
* sidechain unavailable in this host
* profile: Podcast
* CPU safe mode enabled

This fits the VX framework pattern shown in the README example processor shell.

---

## 21. CMake and target plan

Add three plugin targets using the existing VX Suite pattern.

* `VxMixStudioTrack`
* `VxMixStudioHub`
* `VxMixStudioResolve`

Each should:

* link `VxSuiteFramework`
* link `juce::juce_dsp`
* include shared common sources from `common/mixstudio`
* stage VST3 bundles using the same helper pattern shown in the framework README 

---

## 22. CPU and safety policy

### Track node

Target:

* <2% typical CPU per stereo track at 48 kHz
* degrade gracefully by lowering long FFT cadence in safe mode

### Hub

Target:

* <5% typical CPU on modern desktop
* reasoning pass capped at fixed cadence

### Resolve

Target depends on number of active actions.

* 1 to 4 actions: light
* 5 to 8 actions with dynamic sidechain: moderate

Safety:

* no allocations on audio thread
* no locks on audio thread
* output trimmer only where processing occurs
* analyzer products must remain sonically transparent

---

## 23. Snapshot and persistence model

Persist:

* user roles
* priorities
* filters
* ignored suggestions
* pinned suggestions
* selected profile
* snapshot comparisons

Do not persist:

* transient live frames
* stale session registry state
* host-specific ephemeral route assumptions

---

## 24. Determinism rules

This matters for trust.

Given the same audio and same settings:

* same conflicts
* same ownership ranges
* same ranked suggestions

No random weights.
No opaque ML in v1.
If ML is added later, it may adjust ranking weights but not replace the visible logic.

---

## 25. Suggested implementation phases

## Phase 1

* Track shell on VX framework
* Hub shell on VX framework
* instance registry
* frame transport
* per-track FFT and mini editor
* Hub spectrum overlay

## Phase 2

* ownership engine
* priority model
* conflict engine
* basic suggestion panel
* labels and presets

## Phase 3

* timeline conflict history
* stereo and low-end phase panels
* snapshot compare
* better pair isolation
* profile-specific priority maps

## Phase 4

* Resolve processor
* resolve plan transport
* sidechain-aware actions
* semi-auto apply

## Phase 5

* arrangement-level warnings
* section-aware labels like verse/chorus density if feasible
* learn-from-user ranking refinement, still bounded by deterministic core

---

## 26. Hard constraints and decisions

These should be treated as non-negotiable unless proven wrong by testing.

1. **No raw audio sharing between plugin instances in v1**
   Share analysis frames only.

2. **No auto-processing from Hub in v1**
   Suggestions first. Controlled apply later.

3. **Custom Hub editor from day one**
   The default framework editor is the wrong shape for this product. 

4. **Track node is analyzer-first, not processor-first**
   Keep it transparent.

5. **Resolve is separate**
   Do not jam correction into the Hub.

6. **Ownership view is the flagship feature**
   Not just another analyzer line graph.

---

## 27. Final architectural summary

Use the VX framework as the **foundation layer**, not the product architecture. It is good at turning a DSP class into a plugin with standard lifecycle, parameter layout support, editor scaffolding, smoothing, and output safety. That saves time and keeps VxMixStudio consistent with the rest of your suite. But VxMixStudio’s real value lives above that layer: custom inter-instance comms, ownership modeling, conflict reasoning, and a purpose-built editor.

The clean product stack is:

* **Track** for analysis and publishing
* **Hub** for reasoning and visualization
* **Resolve** for corrective action
