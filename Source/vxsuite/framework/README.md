# VX Suite Framework

An open-source C++ / JUCE framework for building professional VST3 plugins. It turns an "audio DSP class" into a shippable VST3 with almost no boilerplate — so you can focus on the sound, not the plumbing.

The framework is used internally to build the VX Suite of audio plugins and is released openly so that anyone can use it as a foundation for their own JUCE-based effects. Whether you are writing a simple EQ, a dynamics processor, or an ML-backed noise reducer, the same base classes handle parameter registration, responsive UI layout, editor creation, listen routing, telemetry publication, and output safety for you.

---

## Why the framework exists

Writing a JUCE plugin from scratch means subclassing `AudioProcessor`, writing parameter layouts, wiring up `AudioProcessorValueTreeState`, creating an editor, overriding `getName`, `hasEditor`, `createEditor`, `prepareToPlay`, `processBlock`, … most of which is identical across every product.

The framework collapses that to **three pure-virtual methods plus one identity descriptor**. Everything else — parameter registration, editor creation, output safety, plugin name, listen/bypass, telemetry publishing, and per-block smoothing — is provided by the base classes.

---

## What the framework gives you for free

| Feature | Where it lives |
|---|---|
| JUCE APVTS parameter layout | `createSimpleParameterLayout(identity)` |
| Plugin name (`"VX Tone"` etc.) | `ProcessorBase::getName()` |
| Default editor (knobs, help button, status bar, responsive text/layout) | `ProcessorBase::createEditor()` → `EditorBase` |
| Per-block exponential parameter smoothing | `VxSuiteBlockSmoothing.h` |
| Shared telemetry publishers for analysers | `VxSuiteSpectrumTelemetry.h` |
| Shared signal-quality analysis (`mono`, `compression`, `tilt`, confidence) | `VxSuiteSignalQuality.h` |
| Suite-wide final output safety trimmer | `ProcessorBase` + `VxSuiteOutputTrimmer.h` |
| Latency-aware listen routing | `ProcessorBase` internals |
| Latency-aligned listen/diff buffer | `ProcessorBase` internals |
| Reusable HTML help popup | `VxSuiteHelpView.h/.cpp` + `ProductIdentity::helpHtml` |
| CMake plugin registration | `vxsuite_add_framework` / `juce_add_plugin` helpers in `CMakeLists.txt` |

---

## Core types

### `ProductIdentity`  (`VxSuiteProduct.h`)

Plain struct that describes your plugin. Fill it in once; the framework reads it everywhere.

```cpp
vxsuite::ProductIdentity id;
id.productName        = "Tone";        // used in plugin name and UI header
id.shortTag           = "TNE";         // 3-char debug tag
id.dspVersion         = "1.2.0";       // DSP semver; independent from framework releases
id.primaryParamId     = "bass";        // main knob
id.secondaryParamId   = "treble";      // secondary knob
id.modeParamId        = "mode";        // vocal / general toggle
id.listenParamId      = "listen";      // diff-listen toggle
id.primaryLabel       = "Bass";
id.secondaryLabel     = "Treble";
id.primaryHint        = "Low-shelf EQ";
id.secondaryHint      = "High-shelf EQ";
id.helpTitle          = "VXTone Help";
id.helpHtml           = R"(<h1>VXTone</h1><p>...</p>)";
id.readmeSection      = "VXTone";
id.primaryDefaultValue   = 0.5f;       // 0-1, 0.5 = neutral
id.secondaryDefaultValue = 0.5f;
id.theme.accentRgb    = { 0.55f, 0.35f, 0.90f };   // purple
```

Most fields have sensible defaults. The only ones you must set are `productName`, `shortTag`, and the param IDs.

Documentation contract:

- Every DSP should provide `dspVersion`, `helpTitle`, `helpHtml`, and `readmeSection`.
- Keep the in-plugin Help popup and the public `README.md` aligned whenever behaviour, UI, or recommended usage changes.
- DSP semantic versions are independent from the framework version. Bump the DSP when its behaviour, UI, or mirrored docs change; bump the framework when shared framework behaviour changes.

---

### `ProcessorBase`  (`VxSuiteProcessorBase.h`)

Inherit from this. Override three methods:

```cpp
class MyProcessor final : public vxsuite::ProcessorBase {
public:
    MyProcessor() : ProcessorBase(makeIdentity()) {}       // single-arg ctor

protected:
    void prepareSuite(double sampleRate, int blockSize) override; // called on prepareToPlay
    void resetSuite() override;                                   // called on reset
    void processProduct(juce::AudioBuffer<float>&,
                        juce::MidiBuffer&) override;              // called on processBlock
private:
    static vxsuite::ProductIdentity makeIdentity();
};
```

The framework handles the rest.

If your DSP wants shared signal-quality evidence, do not build a separate detector inside the product. Read the snapshot the framework already maintains:

```cpp
void VXMyPluginAudioProcessor::processProduct(juce::AudioBuffer<float>& buffer,
                                              juce::MidiBuffer&) {
    const auto signalQuality = getSignalQualitySnapshot();
    const float trust = signalQuality.separationConfidence;
    juce::ignoreUnused(buffer, trust);
}
```

---

### `EditorBase`  (`VxSuiteEditorBase.h`)

You usually don't touch this. `ProcessorBase::createEditor()` returns `new EditorBase(*this)` automatically. If you need a custom layout, override `createEditor()` and return your own subclass.

The shared editor is also where the current baseline resize/readability behavior lives:

- fitted header, status, label, and hint text
- roomier vertical spacing for labels
- control/header wrapping when width gets tight
- more forgiving framework-level minimum sizes
- optional top-right Help button that opens HTML help content from `ProductIdentity`

---

### `OutputTrimmer`  (`VxSuiteOutputTrimmer.h`)

Instantaneous peak-limiting safety net: if any sample exceeds the ceiling the gain snaps down, then releases slowly back to unity.

Current framework behavior:

- `ProcessorBase` now applies a final emergency output trimmer automatically for every VX processor.
- Product-local trimmers are still useful when a specific DSP needs tighter local control before the shared final safety stage.

```cpp
vxsuite::OutputTrimmer localTrimmer;
// in prepareSuite: localTrimmer.setReleaseSeconds(0.18f);  // optional
// in resetSuite:   localTrimmer.reset();
// at end of processProduct:
localTrimmer.process(buffer, currentSampleRateHz);
```

---

### Shared telemetry  (`VxSuiteSpectrumTelemetry.h`)

The framework can publish lightweight dry/wet spectrum and stage telemetry for analyser-style tools.

This is the path used by `VXStudioAnalyser`:

- each VX processor publishes dry-vs-wet spectrum snapshots after processing
- processors can also publish ordered stage telemetry
- the analyser reads the live chain and lets the user inspect either the full chain or one stage at a time

The telemetry path is designed to stay realtime-safe and cheap enough for normal plugin use. It is meant for chain-aware inspection, not for embedding a full lab analyser inside every product.

---

### Shared signal quality  (`VxSuiteSignalQuality.h`)

The framework also provides a lightweight shared recording-condition detector alongside `VoiceAnalysis` and `VoiceContext`.

It computes four reusable fields:

- `monoScore`: how close the signal is to near-mono capture
- `compressionScore`: how crushed / AGC-like the dynamics appear
- `tiltScore`: how strongly the spectrum leans toward low-frequency-heavy / lo-fi character
- `separationConfidence`: derived trust score for source- or structure-sensitive DSP

`ProcessorBase` updates this automatically once per block and exposes it with:

```cpp
const auto signalQuality = getSignalQualitySnapshot();
```

Framework rule:

- the framework owns the evidence
- each product owns the response

That means `SignalQuality` should stay generic and reusable, while products decide how to use it for threshold easing, confidence blending, protection, or UI hints.

Typical uses:

- back off aggressive correction on mono, crushed, or low-tilted material
- reduce confidence in source-ownership or structure-sensitive DSP when conditions are poor
- surface recording-condition hints in analyser-style tools

Adoption guidance:

- use it as an input-trust layer, not as a hidden product-mode switch
- prefer threshold shaping, confidence blending, and protection over hard branching
- keep product responses local: `Cleanup` can ease corrective targeting, `Leveler` can soften ride/restore confidence, `Rebalance` can blend source moves toward unity
- if a product is not based on `ProcessorBase`, bridge the snapshot explicitly instead of duplicating the detector

Avoid putting product-specific weighting laws into the framework. Shared detection belongs here; behaviour remains product-local.

---

### Block smoothing  (`VxSuiteBlockSmoothing.h`)

Smooth a parameter value block-by-block without per-sample branching:

```cpp
float smoothed = 0.5f;
// each block:
smoothed = vxsuite::smoothBlockValue(smoothed, target, sampleRate, numSamples, 0.06f /*time constant s*/);
```

Prime on first block to avoid a ramp from zero:

```cpp
if (!primed) { smoothed = target; primed = true; }
else         { smoothed = vxsuite::smoothBlockValue(...); }
```

---

## Step-by-step: adding a new plugin

**1. Create the processor header**

```
Source/vxsuite/products/myplugin/VxMyPlugin.h
```

```cpp
#pragma once
#include "../../framework/VxSuiteProcessorBase.h"

class VXMyPluginAudioProcessor final : public vxsuite::ProcessorBase {
public:
    VXMyPluginAudioProcessor();
    juce::String getStatusText() const override;
protected:
    void prepareSuite(double sampleRate, int samplesPerBlock) override;
    void resetSuite() override;
    void processProduct(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
private:
    static vxsuite::ProductIdentity makeIdentity();
    // your DSP state here
};
```

**2. Create the processor source**

```
Source/vxsuite/products/myplugin/VxMyPlugin.cpp
```

```cpp
#include "VxMyPlugin.h"
#include "vxsuite/framework/VxSuiteBlockSmoothing.h"
#include "vxsuite/framework/VxSuiteParameters.h"

namespace {
    constexpr std::string_view kProductName = "MyPlugin";
    constexpr std::string_view kShortTag    = "MYP";
    constexpr std::string_view kDryParam    = "dry";
    constexpr std::string_view kWetParam    = "wet";
    constexpr std::string_view kModeParam   = "mode";
    constexpr std::string_view kListenParam = "listen";
}

VXMyPluginAudioProcessor::VXMyPluginAudioProcessor()
    : ProcessorBase(makeIdentity()) {}

vxsuite::ProductIdentity VXMyPluginAudioProcessor::makeIdentity() {
    vxsuite::ProductIdentity id{};
    id.productName       = kProductName;
    id.shortTag          = kShortTag;
    id.primaryParamId    = kDryParam;
    id.secondaryParamId  = kWetParam;
    id.modeParamId       = kModeParam;
    id.listenParamId     = kListenParam;
    id.primaryLabel      = "Dry";
    id.secondaryLabel    = "Wet";
    id.theme.accentRgb   = { 0.3f, 0.7f, 0.5f };
    return id;
}

juce::String VXMyPluginAudioProcessor::getStatusText() const { return ""; }
void VXMyPluginAudioProcessor::prepareSuite(double, int) {}
void VXMyPluginAudioProcessor::resetSuite() {}
void VXMyPluginAudioProcessor::processProduct(juce::AudioBuffer<float>&, juce::MidiBuffer&) {}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new VXMyPluginAudioProcessor();
}
```

**3. Add the CMake target**

In `CMakeLists.txt`, add after the existing product blocks:

```cmake
juce_add_plugin(VXMyPlugin
    PLUGIN_MANUFACTURER_CODE "Vxst"
    PLUGIN_CODE              "VXMP"
    FORMATS                  VST3
    VST3_CATEGORIES          "Fx"
    PRODUCT_NAME             "VXMyPlugin"
    BUNDLE_IDENTIFIER        "com.vxsuite.vxmyplugin"
    IS_SYNTH                 FALSE
    NEEDS_MIDI_INPUT         FALSE
    NEEDS_MIDI_OUTPUT        FALSE)

target_sources(VXMyPlugin PRIVATE
    Source/vxsuite/products/myplugin/VxMyPlugin.cpp)

target_link_libraries(VXMyPlugin PRIVATE
    VxSuiteFramework
    juce::juce_dsp)

vxsuite_stage_vst3_bundle(VXMyPlugin_VST3 "VXMyPlugin")
```

**4. Build**

```bash
cmake --build build -j$(nproc)
```

The VST3 is staged into `Source/vxsuite/vst/` automatically.

---

## Worked example: VXTone

VXTone is a bass/treble shelf EQ plugin included in the suite. It demonstrates the full pattern end-to-end.

**Files:**
- `Source/vxsuite/products/tone/VxToneProcessor.h`
- `Source/vxsuite/products/tone/VxToneProcessor.cpp`

### What it does

Two independent biquad IIR shelf filters — one low shelf (bass) and one high shelf (treble) — applied in series. The cutoff frequencies and gain range differ by mode:

| Mode | Bass shelf | Treble shelf | Gain range |
|---|---|---|---|
| **Vocal** | 200 Hz | 6 000 Hz | ±5 dB |
| **General** | 120 Hz | 8 000 Hz | ±6 dB |

Vocal mode positions the shelves so the 200 Hz–6 kHz speech band is left completely untouched — you can boost the warmth below it or add air above it without affecting consonants or fundamentals.

General mode widens both shelves and raises the headroom for non-vocal material.

### Control mapping

Both controls are normalised `[0, 1]` with `0.5` = neutral (0 dB). The mapping is:

```
gainDb = (normalised - 0.5) * 2 * maxGainDb
```

So `0.0` → full cut, `0.5` → flat, `1.0` → full boost.

### Smoothing

Both parameters are block-smoothed at a 60 ms time constant to prevent clicks when the user moves the knobs. The smoothed value is used to compute filter coefficients once per block.

### DSP: Audio EQ Cookbook shelf filters

The biquad coefficients follow the Audio EQ Cookbook (Robert Bristow-Johnson) shelf formulae:

```
A        = 10^(gainDb / 40)
w0       = 2π × freqHz / sampleRate
alpha    = sin(w0) / √2          (S = 1 shelf slope)
twoSqrtA = 2 × √A

Low shelf:
  b0 =   A × [(A+1) − (A−1)×cos(w0) + 2√A × alpha]
  b1 = 2A × [(A−1) − (A+1)×cos(w0)               ]
  b2 =   A × [(A+1) − (A−1)×cos(w0) − 2√A × alpha]
  a0 =       [(A+1) + (A−1)×cos(w0) + 2√A × alpha]
  a1 =  −2 × [(A−1) + (A+1)×cos(w0)               ]
  a2 =       [(A+1) + (A−1)×cos(w0) − 2√A × alpha]

High shelf: signs on (A−1)×cos and a1 flip, sign convention for b1/a1 also flips.
```

When `|gainDb| < 0.01` the coefficients default to passthrough (`b0=1`, all others 0) to avoid computing near-unity filters needlessly.

### Reading the processor source

```cpp
// 1. Identity descriptor — drives the UI and parameter layout
vxsuite::ProductIdentity VXToneAudioProcessor::makeIdentity() { … }

// 2. prepareSuite — allocate per-channel biquad state vectors
void VXToneAudioProcessor::prepareSuite(double sampleRate, int) {
    bassState.assign(numChannels, {});
    trebleState.assign(numChannels, {});
}

// 3. processProduct — smooth → map to dB → compute coeffs → apply per channel
void VXToneAudioProcessor::processProduct(juce::AudioBuffer<float>& buf, juce::MidiBuffer&) {
    // block-smooth both knobs
    // select freq/gain constants from mode
    // compute BiquadCoeffs once (not per sample)
    // apply bass biquad then treble biquad to each channel
}
```

The biquad filter state (`x1, x2, y1, y2`) is kept in `std::vector<BiquadState>` — one element per channel — so the plugin handles mono and stereo correctly without branching.

### Why this is a good DSP template

- **No JUCE DSP module dependency** for the filter — the biquad is three structs and two static functions, easy to unit-test in isolation.
- **Per-block coefficient compute** — not per sample, so it's cheap even at high sample rates.
- **Mode switch is instant but ramp-free** — the smoothed knob values bridge any discontinuity when the mode changes.
- If your DSP needs extra local peak discipline, add a product-local `OutputTrimmer` before returning from `processProduct`. The framework already provides a final emergency safety trimmer in `ProcessorBase`.

---

## Parameter conventions

All knob parameters are registered as `[0, 1]` floats. Your DSP maps `0.5` to neutral ("nothing happens") and the extremes to maximum effect. This gives the UI a consistent feel — the knob at 12 o'clock always means "off".

The shared UI now presents standard percentage-style controls as `0%` to `100%` instead of raw `0.0` to `1.0`. Controls with a neutral midpoint should keep that midpoint semantically clear in both mapping and text, for example `50% .. 150%` with `100%` at centre for unity gain.

Mode parameters are integers (`0`, `1`, …) registered as a choice list. Read them with `vxsuite::readMode(parameters, identity)`.

The listen parameter is a `[0, 1]` boolean toggle. By default the base class renders the removed delta. Additive products can override `renderListenOutput(...)` and call `renderAddedDeltaOutput(...)` so listen matches the product role.

---

## Theme colours

Set four RGB triples in `id.theme` to match your plugin's visual identity:

```cpp
id.theme.accentRgb      = { r, g, b };   // main accent, knob fill, glow
id.theme.accent2Rgb     = { r, g, b };   // secondary accent / dark complement
id.theme.backgroundRgb  = { r, g, b };   // outer background
id.theme.panelRgb       = { r, g, b };   // inner panel / control area
id.theme.textRgb        = { r, g, b };   // labels and status text
```

All values are `[0, 1]` linear floats. The editor picks these up automatically.

---

## File layout reference

```
Source/vxsuite/
├── framework/
│   ├── README.md                      ← you are here
│   ├── VxSuiteProduct.h               ← ProductIdentity struct
│   ├── VxSuiteProcessorBase.h/.cpp    ← ProcessorBase
│   ├── VxSuiteEditorBase.h/.cpp       ← EditorBase
│   ├── VxSuiteBlockSmoothing.h        ← smoothBlockValue / blockBlendAlpha
│   ├── VxSuiteOutputTrimmer.h         ← OutputTrimmer
│   ├── VxSuiteSpectrumTelemetry.h/.cpp← analyser telemetry + stage publishers
│   └── VxSuiteParameters.h            ← readNormalized / readMode helpers
└── products/
    ├── tone/                          ← VXTone example (bass/treble EQ)
    ├── cleanup/                       ← VXCleanup
    ├── deverb/                        ← VXDeverb
    ├── finish/                        ← VXFinish
    ├── subtract/                      ← VXSubtract
    ├── proximity/                     ← VXProximity
    ├── denoiser/                      ← VXDenoiser
    ├── deepfilternet/                 ← VXDeepFilterNet (ML)
    ├── OptoComp/                      ← VXOptoComp (opto compressor)
    └── analyser/                      ← VXStudioAnalyser (chain analyser)
```
