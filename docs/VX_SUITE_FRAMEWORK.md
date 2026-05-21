# VX Suite Framework

## Purpose

VX Suite should ship focused realtime plugins that are simple to learn, cheap to run, and consistent to maintain.

This framework exists so every new plugin starts from the same contract instead of growing ad hoc UI, parameter, and lifecycle patterns.

Research summary:

- premium plugin UIs privilege hierarchy, scaling, and workflow over raw parameter count
- best-practice JUCE/VST implementations keep parameter/state/editor concerns disciplined and boring

See also: [VX Suite Research](/Users/andrzejmarczewski/Documents/GitHub/VxCleaner/docs/VX_SUITE_RESEARCH.md)

## Product Rule Set

Each VX Suite product should:

- solve one main problem
- expose one or two headline knobs by default
- support `Vocal` and `General` modes only when the DSP genuinely needs them
- expose `Listen` only when removed-content audition is meaningful for that product
- keep all non-essential diagnostics and visualization out of the shipping UI
- be safe for live insert use in a DAW

## Realtime Rules

The framework assumes the following are non-negotiable:

- no heap allocation in `processBlock`
- no blocking I/O or locks on the audio thread
- stable parameter IDs and stable state schema
- deterministic latency reporting
- UI is optional and must never drive DSP correctness
- processor/editor ownership remains cleanly separated

These rules align with the direction of the JUCE plugin model and the VST3 processor/controller split:

- JUCE `AudioProcessor`: [JUCE docs](https://docs.juce.com/master/classjuce_1_1AudioProcessor.html)
- JUCE `AudioProcessorValueTreeState`: [JUCE docs](https://docs.juce.com/master/classjuce_1_1AudioProcessorValueTreeState.html)
- VST3 component/controller model: [Steinberg SDK docs](https://steinbergmedia.github.io/vst3_doc/vstsdk/)

## Template Shape

Every product should start from:

- `vxsuite::ProcessorBase`
- `vxsuite::EditorBase`
- `vxsuite::ProductIdentity`
- `vxsuite::ModePolicy`
- a product-local DSP core with explicit `prepare/reset/process` lifecycle

The framework now owns the shared `Voice` / `General` contract:

- mode labels come from `VxSuiteModePolicy.h`
- `ProductIdentity.defaultMode` declares the product default
- `vxsuite::readModePolicy(...)` and `ProcessorBase::currentModePolicy()` provide the active product-grade mode mapping
- products should consume that policy instead of hard-coding their own `Voice` / `General` tables unless a product has a documented reason to diverge

The framework may also own shared removed-content audition:

- `ProductIdentity.listenParamId` opts a product into a shared `Listen` toggle
- the default `ProcessorBase::renderListenOutput(...)` contract is delta audition: `input - output`
- latency-bearing products must override that helper and subtract from a latency-aligned dry reference instead of using raw input
- `Listen` is a framework capability, not a mandatory product control

The framework also owns the final emergency output-safety telemetry:

- `OutputTrimmer` should remain a last-resort guard, not the main gain-staging strategy
- shared tests may read `ProcessorBase::getOutputSafetyTrimReductionDb()` and `getOutputSafetyTrimMaxReductionDb()`
- if those values are materially active during nominal strong settings, fix the product gain staging before widening the trimmer

The framework should also own shared analysis and protection evidence when multiple products need it:

- shared voice analysis belongs in `Source/vxsuite/framework/`
- shared analysis may inform product DSP, but must not silently replace or weaken a proven core effect contract
- `Vocal` safety and `Blend` are wrappers around the core effect, not substitutes for it

## UI Rules

The visual template should stay recognizable across the suite:

- strong product title
- two-knob hero layout
- simple mode switch
- optional small `Listen` toggle when delta audition is part of the product contract
- one short sentence under each knob
- resizable editor with sensible limits
- host scale-factor support
- no scrolling
- no inspector panel
- no meters unless they directly affect user decisions

Framework readability/responsiveness expectations:

- titles, status lines, knob labels, and hint text must tolerate narrower host widths without clipping
- minimum editor sizes should be large enough that text still reads cleanly; do not rely on ultra-tight packing at the minimum size
- shared layouts should prefer wrapping or extra vertical room over illegible one-line compression
- products built on `EditorBase` should inherit text-fitting behavior from the framework rather than patching readability ad hoc per plugin

## Suggested First Products

- `VX Deverb`
- `VX Proximity`
- `VX Polish`

## Template Checklist

Before starting a new plugin:

1. Write a one-line job statement.
2. Prove the main value can be expressed with one or two knobs.
3. Define whether `Vocal` and `General` truly need different DSP maps.
4. Decide whether removed-content `Listen` is genuinely useful and trustworthy for this product.
5. If the product uses modes, map them through the shared `ModePolicy` helpers instead of ad hoc parameter reads.
6. If the product uses `Listen`, use the shared framework toggle and document the exact subtraction reference.
7. Keep all product-specific DSP in its own module; do not fork the framework.
8. Add focused verification for bypass transparency, parameter automation safety, sample-rate changes, and silence/reset stability.

## Proven DSP Contract Rules

If a DSP stage already works in a lab harness, preserve that contract before productizing it.

- keep the same effective wet authority unless measurement proves the remap is better
- keep the same latency semantics end-to-end
- keep the same mono/stereo reintegration contract end-to-end
- add helpers like `Vocal` protection, `Blend`, or body restore only after the core effect is measured working on its own
- if a wrapper changes the audible result, that wrapper must be documented and testable

For mono corrective DSP in stereo products:

- specify whether the mono result is used as full wet, mono delta, or mid-only correction
- document exactly how the result is realigned to the original stereo signal
- implement that reintegration in a streaming-safe way; whole-buffer offline logic cannot be copied directly into per-block processing without delay compensation

For latency-bearing products:

- report latency from the active DSP path only
- align dry references, safety comparators, and stereo re-entry to that same latency
- never compare wet and dry signals on different timelines and call the result “protection”

## Effect Validation Rules

Every corrective effect should prove two things separately:

- the plugin changes audio
- the change moves the signal in the intended direction

Minimum validation for a new product:

- a focused automated measurement target that reports input RMS, output RMS, delta RMS, and one outcome-specific metric
- a product-specific test that fails if the effect becomes effectively dry or if the outcome metric moves the wrong way
- an explicit “core DSP only” validation path before optional safety/protection wrappers are tuned
- if `Listen` exists, a check that it emits the intended removed delta rather than dry/wet misalignment

Example for `VX Deverb`:

- deverb should measure tail reduction, not just output difference
- `Vocal` protection should preserve direct speech without canceling the core dereverb
- `Blend` should restore wanted body after deverb, not hide weak deverb by reintroducing dry signal broadly

## Purity Rules

Focused suite products should keep their audible path honest.

- analysis-only filtering is allowed
- exposed correction stages are allowed
- hidden audible “helper” stages are not
- product code should use domain-correct names on its active path

Example:

- `VX Deverb` may use internal speech-band weighting for protection/guard logic
- `VX Deverb` may not hide tonal EQ ahead of dereverb
- `Body` is acceptable because it is exposed and documented

## Framework Boundary Rule

Once a product lives under `Source/vxsuite/`, its active implementation should stay architecturally honest.

- shared suite code belongs in `Source/vxsuite/framework/`
- product-specific DSP belongs in `Source/vxsuite/products/<product>/`
- product wrappers must not depend on hidden legacy paths outside `Source/vxsuite/` when the suite version is meant to be self-contained

---

## Phase 1 Established Patterns (Denoiser/DeepFilterNet/Deverb/Leveler/Subtract Review)

### 1. ReadabilityGuard Integration for Artifact Protection

**Pattern:** Aggressive corrective effects (spectral subtraction, neural network suppression, dereverberation) benefit from a post-processing clarity check.

**Implementation:**
- After the core DSP applies heavy correction, compute `ReadabilityGuard` metrics on the output
- `articulation_risk` and `body_loss_risk` measure whether transients and low-mids have been over-corrected
- Scale the wet/dry blend or gate the correction bins based on these metrics (up to 55% blend adjustment)
- No analysis-only; this is an audible protection stage but disclosed in the product contract

**Products:**
- Denoiser: ReadabilityGuard on phrase boundaries with resetFifoState() for STFT stability
- DeepFilterNet: Post-inference clarity check with speech-safety gating to reduce model aggressiveness in low-SNR scenarios

**When to use:**
- Any product that applies broad spectral suppression (denoising, dereverberation, source separation)
- Any product using neural network inference without perceptual bounds
- Avoid if the product's core design already includes conservative suppression thresholds

### 2. Framework Analysis Snapshot Integration

**Pattern:** Every product should read available analysis snapshots (voice context, signal quality, analysis evidence) to make adaptive DSP decisions.

**Key snapshots:**
- `VoiceAnalysisSnapshot`: speech presence, intelligibility risk, formant stability, articulation risk
- `VoiceContextSnapshot`: vocal dominance, phrase boundaries, phraseStart/phraseEnd sample indices
- `SignalQualitySnapshot`: monoScore (mono vs stereo), SNR estimate, separation confidence
- `AnalysisEvidence`: raw metrics for ReadabilityGuard computation

**Implementation pattern:**
```cpp
// In processProduct():
auto analysis = getAnalysisSnapshot();
auto voiceContext = getVoiceContextSnapshot();
auto signalQuality = getSignalQualitySnapshot();

// Use to gate, adapt, or protect the core effect
float speechSafetyFactor = voiceContext.speechPresence * (1.0f - analysis.intelligibilityRisk);
float adaptiveThreshold = juce::jmap(signalQuality.monoScore, 0.40f, 0.80f, 0.08f, 0.25f);
```

**Products:**
- DeepFilterNet: speechSafetyFactor gates model strength in risky conditions
- Subtract: monoScore scales stereo profile confidence threshold
- Denoiser: phrase boundaries trigger FIFO reset for phase continuity
- Leveler: sample-rate changes invalidate offline analysis

**When to use:**
- Any product with context-dependent DSP (voice vs. general, quiet vs. loud, solo vs. ensemble)
- Products that maintain state across blocks (offline analysis, learned profiles)
- Products that benefit from signal-aware gating or adaptation

### 3. M/S Consolidation for State Memory

**Pattern:** Products processing stereo via three parallel instances (mono/left/right) can reduce state memory significantly by consolidating to a single M/S-aware instance.

**Approach:**
- Instead of: `dsp_mono`, `dsp_left`, `dsp_right` processing individual channels
- Use: single DSP instance that handles M/S internally, receiving full stereo buffer
- M/S conversion happens inside DSP; results are decoded back to stereo

**Benefits:**
- ~66% state memory reduction (3 instances → 1 instance + M/S arithmetic)
- Cleaner processor code
- Shared mode-aware parameters apply consistently across channels

**Tradeoff:**
- M/S math adds negligible CPU cost
- Must verify DSP's M/S logic is channel-aware (not assuming mono throughout)

**Products:**
- Denoiser: consolidated from three instances to single M/S-aware instance

**When to use:**
- Products with stateful DSP (STFT, filtering, analysis buffers) processing all channels independently
- Avoid if the DSP is purely analysis-only or if channel coupling would break the algorithm

### 4. Phrase Boundary Detection & FIFO Reset

**Pattern:** STFT-based processors can maintain phase continuity and avoid clicks by detecting phrase boundaries and resetting internal state.

**Implementation:**
- Track `phraseStart` and `phraseEnd` sample indices from `VoiceContextSnapshot`
- When a new phrase begins, call `resetFifoState()` to clear STFT overlap buffers
- Prevents spectral artifacts (ringing, phase distortion) from carryover between speech segments

**Code pattern:**
```cpp
bool phraseActive = voiceContext.phraseActivity > 0.5f;
if (phraseActive && !prevPhraseActive) {
  // Phrase boundary detected; reset STFT state
  dsp.resetFifoState();
}
prevPhraseActive = phraseActive;
```

**Products:**
- Denoiser: resetFifoState() at phrase boundaries for stability

**When to use:**
- STFT-based processors (denoising, dereverberation, spectral subtraction)
- Products where carryover between speech segments causes audible artifacts
- Not needed for time-domain DSP (compression, EQ, filtering)

### 5. Adaptive Parameters Based on Signal Quality

**Pattern:** Single-value parameters can become adaptive by reading signal quality snapshots.

**Example:**
- Stereo profile confidence threshold scales with monoScore:
  - Near-mono recording (monoScore > 0.80f) → stricter threshold (0.25f)
  - Stereo-imaged recording (monoScore < 0.40f) → permissive threshold (0.08f)
- Speech safety factor scales with speech presence and intelligibility

**Benefits:**
- Single control from user perspective
- Backend robustness on edge-case recordings without exposing complexity
- No extra knob burden

**Tradeoff:**
- Adds complexity to documentation ("why does this behave differently on my session?")
- Requires A/B validation across varied source material

**Products:**
- Subtract: monoScore-adaptive stereo confidence threshold
- DeepFilterNet: speechPresence-adaptive safety gating

**When to use:**
- Parameters that have natural recording-type dependencies
- Safety parameters where conservative is better (thresholds, gates)
- Avoid on primary creative controls (avoid surprises to users)

### 6. Default Value Tuning for UX Impact

**Pattern:** Changing a knob default from 0.0f to a gentle active value improves perceived consistency.

**Findings:**
- Users expect a plugin to do "something" by default
- Defaults of 0.0f (fully off) force manual dial-in, adding friction
- Gentle defaults (0.15f–0.25f) provide immediate perceived benefit without aggressive over-processing

**Examples:**
- Leveler: default level 0.0f → 0.15f (gentle immediate leveling, consistent with UI promise)
- Deverb: default body 0.0f → 0.25f (+2.5dB bass restore, warmer out-of-box feel)
- Proximity: closer knob defaults 0.0f (considered but deferred; 0.25f suggested for voice content)

**Cost:**
- Trivial: one line per parameter

**Validation needed:**
- A/B listen to ensure default behavior matches user expectations
- Document what the default achieves

**When to use:**
- Secondary/refinement parameters (body, blend, air)
- Not on primary creative controls (primary amount knobs should default to 0.0f for familiarity)

### 7. Cross-Product Sidechain Integration

**Pattern:** One product can publish useful metrics that another product consumes.

**Example:**
- Denoiser publishes `noiseFloorDb` to framework
- Cleanup uses `compSidechainBoostDb` to boost compression during noisy sections
- Subtract could gate profile subtraction when noise floor is extremely high

**Implementation:**
- Denoiser writes `noiseFloorDb` to a framework-visible field
- Downstream products read it and adapt their DSP accordingly
- Pure opt-in; no breaking changes if consumer ignores the sidechain

**Tradeoff:**
- Adds coupling between products
- Must be carefully documented so maintainers understand the dependency
- Should be feature-gated (if upstream product is inactive, sidechain is 0.0f)

**Status:**
- Phase 1: `compSidechainBoostDb` wired from Denoiser → Cleanup
- Phase 2+ candidates: Subtract gate on noise floor, Rebalance coordination with Denoiser

**When to use:**
- Cross-product optimizations that are purely beneficial (no downside if sidechain is ignored)
- Late in product development, after each product's core is validated independently
- Document the coupling explicitly in help text and architecture docs

---

## Phase 1 Key Learnings

1. **M/S consolidation** provides significant state memory savings without algorithmic compromise when DSP includes M/S logic internally
2. **ReadabilityGuard integration** provides unified artifact protection across different DSP approaches (spectral, neural, time-domain)
3. **Framework analysis snapshots** are underused; every product benefits from at least speech presence and signal quality reads
4. **Default value tuning** has outsized UX impact; 0.0f defaults force user dial-in friction
5. **Phrase boundary detection** eliminates a class of STFT artifacts with minimal code
6. **Adaptive parameters** (thresholds, gates based on signal quality) improve robustness on edge-case recordings without exposing complexity
7. **Cross-product coordination** should come late, after each product's core is independently validated

See also: [Phase 1 Completion Summary](/Users/andrzejmarczewski/.claude/plans/phase-1-completion-summary.md)
