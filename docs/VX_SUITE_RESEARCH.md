# VX Suite Research Notes

## Goal

Define the strongest practical look/feel and implementation direction for a lightweight VX Suite plugin line.

## What Best-In-Class Plugins Do Well

### 1. Strong visual hierarchy

The best plugin UIs make the primary action obvious immediately.

- FabFilter emphasizes a large interactive area, clear typography, and rapid workflow over crowded controls. Source: [FabFilter Pro-Q 4](https://www.fabfilter.com/products/pro-q-4-equalizer-plug-in)
- oeksound positions `soothe2` around a single outcome first, then reveals advanced controls only when needed. Source: [soothe2](https://oeksound.com/plugins/soothe2/)
- iZotope Velvet packages multiple vocal-finish tasks into one simple front-end rather than exposing every subsystem at once. Source: [Velvet](https://www.izotope.com/en/products/velvet.html)

Implication for VX Suite:

- default UI should show only the main job
- two-knob hero layouts are a feature, not a limitation
- advanced controls should be hidden or deferred until a product truly needs them

### 2. Scalable, host-friendly editors

The premium standard now includes resize/scaling support rather than a fixed pixel UI.

- JUCE exposes `AudioProcessorEditor::setResizable`, `setResizeLimits`, and `setScaleFactor` for this exact reason. Source: [JUCE AudioProcessorEditor](https://docs.juce.com/master/classjuce_1_1AudioProcessorEditor.html)
- FabFilter explicitly markets resizable interfaces, full-screen support, and customizable scaling. Source: [FabFilter Pro-Q 4](https://www.fabfilter.com/products/pro-q-4-equalizer-plug-in)

Implication for VX Suite:

- every editor should support host scaling
- layouts must gracefully stack/reflow when space changes
- no fixed-size design assumptions in custom drawing

### 3. Excellent parameter and state discipline

The strongest implementations are boring in the right ways.

- JUCE recommends constructing `AudioProcessorValueTreeState` with a full `ParameterLayout` up front, and using attachments for UI wiring. Source: [JUCE AudioProcessorValueTreeState](https://docs.juce.com/master/classjuce_1_1AudioProcessorValueTreeState.html)
- JUCE also warns that `copyState()` uses locks and is thread-safe but not realtime-safe, so state serialization belongs outside audio processing. Source: [JUCE AudioProcessorValueTreeState](https://docs.juce.com/master/classjuce_1_1AudioProcessorValueTreeState.html)
- The VST3 model keeps processor and editor/controller responsibilities separate. Source: [Steinberg VST3 SDK](https://steinbergmedia.github.io/vst3_doc/vstsdk/)

Implication for VX Suite:

- parameter IDs must be stable from day one
- editor classes should not own DSP behavior
- state copy/replace belongs in preset/load/save paths, never `processBlock`

### 4. Outcome-led product design

The market leaders describe results, not internal DSP graphs.

- `soothe harshness so your EQ doesn't have to` is outcome language. Source: [soothe2](https://oeksound.com/plugins/soothe2/)
- `Smooth vocals, the smart way` is outcome language. Source: [Velvet](https://www.izotope.com/en/products/velvet.html)
- sonible's `proximity:EQ+` sells repositioning and acoustic zoom, not filter topology. Source: [proximity:EQ+](https://www.sonible.com/proximityeq/)

Implication for VX Suite:

- `VX Deverb` should sell tail/body cleanup, not WPE
- `VX Proximity` should sell move closer/farther, not shelves and direct/reverb ratio math
- `VX Polish` should sell finish/sit/smooth, not de-mud + de-ess + compressor

## Recommended VX Suite Visual Direction

### Overall

- warm, premium, studio-hardware-inspired palette
- strong product title, subtle suite branding
- large controls with generous spacing
- one dark focal panel on a lighter outer field
- one accent color per product family

### Controls

- one or two large rotary controls
- one compact mode selector
- one-line hints beneath each main control
- no permanent meters unless they change decisions

### Motion and interaction

- smooth parameter interpolation in DSP
- subtle UI polish only: hover, highlight, focus, scaling
- no ornamental animation that steals attention from listening

## Recommended VX Suite Code Direction

### Base architecture

- one shared processor base
- one shared editor base
- one shared look-and-feel/token layer
- one shared mode-policy layer
- product-local DSP modules only

### Editor rules

- support resize limits by default
- respect host `setScaleFactor()`
- build responsive layouts that can stack vertically
- keep all geometry tokenized rather than hard-coded in many places

### State and parameter rules

- use `AudioProcessorValueTreeState` constructor with `ParameterLayout`
- read atomics in DSP using `getRawParameterValue`
- keep save/load in `getStateInformation` / `setStateInformation`
- never call `copyState()` on the audio thread
- centralize reusable `Voice` / `General` semantics in framework helpers instead of re-deriving them in each product

### Product purity rules

- corrective plugins should avoid hidden audible assist stages
- analysis-only helpers are acceptable when they do not change the audible path
- if an audible helper exists, it should be exposed and named in the product contract
- active code paths should use domain-correct names so the implementation matches the product users think they are buying

### Effect validation rules

- a product should not be considered “working” just because output differs from input
- the suite should keep small objective measurement tools that confirm the effect moves the intended metric in the right direction
- wrappers around proven DSP should be introduced one at a time and re-measured after each layer
- offline/reference harness behavior must be translated carefully into streaming plugin behavior when latency or stereo reintegration is involved

Implication for VX Suite:

- if a deverb works in a raw harness, preserve its wet mapping, latency alignment, and stereo re-entry contract first
- shared framework safety/protection features must not be allowed to make the effect effectively dry or directionally wrong

## Conclusion

The best direction for VX Suite is not to imitate giant “do everything” restoration UIs. It is to combine:

- FabFilter-grade hierarchy and polish
- sonible/oeksound-style outcome-led simplicity
- JUCE/VST3 best-practice separation of processor, state, and editor

That gives VX Suite the strongest chance of feeling premium, modern, and maintainable without turning into another sprawling utility plugin.
