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
