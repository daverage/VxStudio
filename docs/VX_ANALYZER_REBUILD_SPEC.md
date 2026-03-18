# VX Studio Analyser — Implementation Specification (Agent Build Version)

## Objective

Build `VX Studio Analyser`, a final-in-chain analysis plugin for VX Suite.

The system must:

* consume **standardized per-stage telemetry**
* reconstruct the **live VX processing chain within a scoped domain**
* allow inspection of:

  * a single stage
  * the full VX chain
* present **Before / After / Delta** views across:

  * Tone (frequency domain)
  * Dynamics (time-domain summary)

The system must be:

* realtime-safe in DSP plugins
* deterministic in data flow
* explicit about uncertainty where inference is used
* lightweight in always-on mode

---

## System Architecture

### Roles

#### 1. DSP Plugins (Publishers)

Each VX plugin:

* publishes **Tier 1 telemetry only by default**
* must remain lightweight and realtime-safe
* must not contain analyzer UI or heavy analysis logic

#### 2. VX Studio Analyser (Consumer)

The analyser:

* discovers publishers within its domain
* reconstructs the VX chain
* computes derived analysis (delta, classification)
* renders UI
* optionally requests Tier 2 detail

#### 3. Framework Telemetry Layer

The framework provides:

* shared-memory transport
* fixed-size schema
* lifecycle tracking
* request channel
* domain scoping

---

## Chain Truth Model (MANDATORY)

The system does NOT assume perfect host order visibility.

Define:

* Chain order is **domain-scoped and confidence-rated**
* Order is derived using:

  1. domain grouping (required)
  2. localOrderId
  3. chainOrderHint
  4. recency
  5. optional signal continuity scoring

Rules:

* signal matching must NEVER override domain or local ordering constraints
* order must be tagged with a **confidence level**
* UI must reflect low-confidence ordering

Confidence levels:

* `Exact` — strong structural agreement
* `High` — consistent hints + recency
* `Low` — partial inference
* `Unknown` — cannot determine

---

## Telemetry Domain Model (MANDATORY)

Each publisher belongs to a **domain**.

Define domain keys:

* `hostSessionId`
* `graphInstanceId`
* `analysisDomainId`

Rules:

* analyser only processes publishers within its domain
* cross-domain publishers must be ignored
* domain mismatch must never pollute chain reconstruction

Fallback:

* if domain is missing → treat as separate unknown domain
* never merge unknown domains automatically

---

## Domain Authority Model (REQUIRED)

Domain assignment is owned by the **VX Studio Analyser instance**, not by DSP plugins.

### Mechanism

The analyser generates a unique:

* `analysisDomainId`

And publishes it into shared memory as:

```cpp
struct DomainRegistry {
    uint64_t analysisDomainId;
    uint64_t hostProcessId;
    uint64_t creationTimeMs;
};
```

### Publisher Behaviour

Each DSP plugin:

1. scans shared memory for active analyser domains
2. if exactly one analyser domain exists in the current host process, attaches to it
3. if multiple analyser domains exist in the current host process, attaches to the most recent analyser
4. if no analyser domain exists, publishes into a null domain

Important rules:

* DSP plugins MUST NOT generate domain IDs
* the analyser is the only authority for `analysisDomainId`
* domain is soft-bound and framework-defined, not host-guaranteed
* publishers MUST re-check domain binding at a bounded low rate so late analyser insertion can rebind them

Fallback behaviour:

* no analyser present -> publishers remain in null domain
* analyser appears later -> publishers rebind on the next domain refresh
* analyser domains from other host processes MUST be ignored

### Domain Ordering Interaction

* `localOrderId` is only valid within a domain
* rebinding to a different domain MUST assign a new `localOrderId`
* changing domain resets ordering context

---

## Data Model

### Stage Identity (fixed)

```cpp
struct StageIdentity {
    char stageId[32];
    uint64_t instanceId;
    uint64_t localOrderId;
    char stageName[64];
    StageType stageType;
    uint32_t chainOrderHint;
    char pluginFamily[16]; // must equal "VXSuite"
    uint32_t semanticFlags;
    uint32_t telemetryFlags;
};
```

### Runtime State

```cpp
struct StageState {
    uint64_t timestampMs;
    bool isLive;
    bool isBypassed;
    bool isSilent;
    DetailLevel detailLevel;
    float sampleRate;
    uint8_t numChannels;
};
```

### Tier 1 Summary (STRICT SIZE)

Hard constraint:

* MUST be fixed-size
* MUST NOT exceed defined memory budget
* MUST be allocation-free

```cpp
struct AnalysisSummary {
    float spectrum[32];          // log-frequency bins
    float envelope[96];          // decimated amplitude envelope
    float rms;
    float peak;
    float crestFactor;
    float transientScore;
    float stereoWidth;
    float correlation;
};
```

IMPORTANT:

* Tier 1 MUST NOT include waveform buffers
* waveform data is Tier 2 only

### Stage Telemetry Record

```cpp
struct StageTelemetry {
    StageIdentity identity;
    StageState state;
    AnalysisSummary inputSummary;
    AnalysisSummary outputSummary;
};
```

Rules:

* `inputSummary` = signal before DSP
* `outputSummary` = signal after DSP
* both summaries MUST describe the same publish window
* input and output summaries MUST remain temporally aligned
* `instanceId` MUST be globally unique per runtime
* slot reuse MUST NOT preserve prior identity without assigning a new `instanceId`

---

## Telemetry Budget (MANDATORY)

* Tier 1 record size must be compile-time fixed
* publish cadence: max 20 Hz
* no heap allocation in publish path
* publish step must be bounded-copy only
* bounded publisher count per domain

---

## Incremental Summary Requirement

Tier 1 summaries MUST be maintained incrementally, not computed from scratch at publish time.

Each DSP publisher must maintain bounded rolling accumulators for:

* spectral bins
* envelope summary
* RMS / peak
* crest factor inputs
* transient estimate
* stereo width / correlation where applicable

Publish step rules:

* publish MUST copy precomputed summary values only
* publish MAY reset or rotate accumulator state after the copy
* publish MUST NOT run a full FFT over historical buffers
* publish MUST NOT rescan historical windows to rebuild summaries

Complexity rules:

* per-block summary updates must be bounded and fixed-cost
* publish step must remain memcpy-style work only

---

## Temporal Alignment Rule

Tier 1 summaries must represent a rolling window with a fixed configured duration.

Recommended v1 window:

* `50-100 ms`

Rules:

* `inputSummary` and `outputSummary` MUST represent the same rolling window
* analyzer comparisons MUST tolerate small publish skew between stages
* Tier 2 deep inspection may require tighter alignment than Tier 1

Without temporal alignment, `Before / After / Delta` is not valid.

---

## Capability Model

Split into two categories:

### Semantic Flags (what the stage does)

* affectsTone
* affectsDynamics
* affectsStereo
* nonlinear
* adaptive

### Telemetry Flags (what it can expose)

* canProvideTargetEqCurve
* canProvideGainReductionTrace
* canProvideDeepWaveform
* canProvideStereoDiagnostics
* supportsDeepInspection

---

## Tier Model

### Tier 1 (always-on)

Provides:

* identity
* state
* summary

Used for:

* chain list
* basic classification
* default inspector

### Tier 2 (on-demand)

Triggered when:

* analyser is open AND
* stage is selected

Provides:

* waveform windows
* higher-resolution spectrum
* EQ curves
* gain reduction traces

Rules:

* must timeout automatically
* must only apply to selected stage(s)
* must not affect other publishers

---

## Analyzer Responsibilities

### 1. Domain Filtering

* select only publishers with matching domain
* discard all others

### 2. Live Stage Filtering

A stage is eligible if:

* recent timestamp within threshold (<= 300 ms)
* not stale
* belongs to VXSuite

Bypassed stages:

* MUST remain visible
* MUST be marked as bypassed

---

### 3. Chain Reconstruction

Build ordered list using:

* domain
* localOrderId
* chainOrderHint
* recency

Apply signal continuity only to adjust confidence, not override order.

Output:

```cpp
struct ChainStage {
    StageIdentity id;
    StageState state;
    float orderConfidence;
};
```

---

### 4. Selected Scope

Supported scopes:

* Single stage
* Full chain

(V1: DO NOT implement multi-range selection)

Definitions:

* before = input summary of first stage
* after = output summary of last stage
* delta = after - before

---

### 5. Delta Computation

#### Spectral

```id="spec_delta"
Delta(f) = After(f) - Before(f)
```

#### Dynamics

Compute:

* RMS change
* peak change
* crest factor delta
* envelope difference

---

### 6. Classification

For each stage:

* spectralChangeMagnitude
* dynamicChangeMagnitude
* stereoChangeMagnitude

Assign:

* impact: low / moderate / strong
* class: spectral / dynamic / spatial / mixed

---

### 7. Confidence Handling

All outputs must carry confidence:

* chain order
* stage inclusion
* delta validity

UI must degrade when confidence is low:

* disable grouped claims
* label inferred results

---

## UI Specification

### Left Panel (Chain List)

Each row:

* stage name
* state (active / bypassed / silent)
* impact label
* class label

Rules:

* only show domain-matched live stages
* no duplicates
* no stale entries

---

### Main Panel

User selects:

* one stage OR
* full chain

Display:

* Before
* After
* Delta

---

### Tabs

#### Tone

* before spectrum
* after spectrum
* delta spectrum
* optional EQ curve (if available)

#### Dynamics

* envelope before/after
* RMS / peak / crest changes

#### Diagnostics

* domain info
* confidence
* telemetry freshness
* capability flags

---

## Derived Processing State

For UI clarity:

* Active
* Bypassed
* Silent
* Stale

Derived from StageState + timestamp

---

## Failure / Degradation Rules

If:

* no valid chain → show inventory only
* low confidence → label as inferred
* missing capabilities → fallback to Tier 1
* stale publishers → exclude from chain

Never:

* show stale stages as active
* assume order without confidence
* merge unrelated domains

---

## Framework Integration

### ProcessorBase MUST:

* register stage identity
* publish Tier 1 after processing
* publish explicit state
* remain allocation-free

### MUST NOT:

* render UI
* compute analyzer visuals
* manage deep inspection logic

---

## Migration Rules

REMOVE:

* waveform-correlation-based ordering as primary logic
* spectrum-only data model
* silence-as-state logic

REPLACE WITH:

* stage schema
* explicit state flags
* domain-scoped chain reconstruction

---

## Verification Requirements

The system is complete only if:

* Tier 1 publish path is allocation-free
* chain contains no stale entries
* bypassed stages are visible but marked correctly
* domain isolation works
* single-stage analysis is correct
* full-chain analysis is correct
* delta is stable across silence and transport changes
* confidence is correctly applied
* Tier 2 requests timeout cleanly

---

## V1 Scope (STRICT)

Include:

* Tier 1 telemetry
* domain filtering
* chain reconstruction
* single-stage view
* full-chain view
* Tone tab
* Dynamics tab

Exclude:

* multi-stage selection
* waveform zoom
* stereo tab
* advanced diagnostics
* deep Tier 2 features beyond basic support

---

## Final Definition

VX Studio Analyser is:

A domain-scoped, confidence-aware chain inspector that reconstructs the live VX processing path and presents stage-level transformation using Before / After / Delta across tone and dynamics, powered by lightweight standardized telemetry.
