# VX Suite Pre-Ship Work Plan

Generated: 2026-06-03  
Scope: All plugins in `Source/vxstudio/products/`  
Priority order: 🔴 Blockers → 🟠 High → 🟡 Correctness → 🔵 Competitive

Each section specifies **exactly which files to touch, which lines to change, and what the replacement code must be.**
After completing each item, rebuild and smoke-test the affected VST.

---

## 🔴 BLOCKER 1 — VXClarity: audio processing completely missing

**Plugin:** VXClarity (= `products/speech_clarity/`)  
**Symptom:** Knobs move, LEDs respond, audio passes through unmodified. All three DSP classes exist and work (VxRepair uses them), but VxSpeechClarityProcessor never calls them.

### Files to change

#### `products/speech_clarity/VxSpeechClarityProcessor.h`

Add three DSP includes after the existing `#include` block:
```cpp
#include "dsp/VxDeEsserDsp.h"
#include "dsp/VxDePolosiveDsp.h"
#include "dsp/VxDeBreathDsp.h"
```

Add three private DSP member fields inside the `private:` section (after the detection filter states):
```cpp
vxsuite::speech_clarity::DeEsserDsp   deEsserDsp;
vxsuite::speech_clarity::DePolosiveDsp dePlosiveDsp;
vxsuite::speech_clarity::DeBreathDsp  deBreathDsp;
```

#### `products/speech_clarity/VxSpeechClarityProcessor.cpp`

**In `prepareSuite`**, after the `onsetDetector.setSampleRate(...)` call, add:
```cpp
const int numChannels = getTotalNumOutputChannels();
deEsserDsp.prepare(currentSampleRateHz, samplesPerBlock, numChannels);
dePlosiveDsp.prepare(currentSampleRateHz, samplesPerBlock, numChannels);
deBreathDsp.prepare(currentSampleRateHz, samplesPerBlock, numChannels);
```

**In `resetSuite`**, after the existing `reset()` calls, add:
```cpp
deEsserDsp.reset();
dePlosiveDsp.reset();
deBreathDsp.reset();
```

**In `processProduct`**, replace the entire `// 4. APPLY PROCESSING` block (lines 247–251, the three TODO comments) with:
```cpp
// 4. APPLY PROCESSING
if (sibilanceStrength > 0.001f)
    deEsserDsp.process(buffer, { sibilanceStrength, detectionState.sibilanceIntensity });
if (plosiveStrength > 0.001f)
    dePlosiveDsp.process(buffer, { plosiveStrength, detectionState.plosiveIntensity });
if (breathStrength > 0.001f)
    deBreathDsp.process(buffer, { breathStrength, detectionState.breathIntensity });
```

### Verify
- Open VXClarity with a vocal recording containing clear sibilance (/s/ sounds).
- Push Sibilance knob to 0.8 — sibilance should be audibly reduced.
- Push Plosive knob with a plosive-heavy recording (/p/ /b/ sounds) — plosive thump should be gated.
- Breath knob with breathy material — level of breaths should drop.
- LEDs should still respond independently of the knob positions (detection runs first).
- Listen mode: should emit the delta (removed content only).
- Also rebuild and smoke-test VXRepair — its use of the same DSP files must be unaffected.

---

## 🔴 BLOCKER 2 — VXDeverb: `createPluginFilter` missing preprocessor guard

**Plugin:** VXDeverb  
**File:** `products/deverb/VxDeverbProcessor.cpp`, line 349

Every other plugin wraps `createPluginFilter` in:
```cpp
#if !defined(VXSUITE_DISABLE_PLUGIN_ENTRYPOINT) && !defined(VXSTUDIO_DISABLE_PLUGIN_ENTRYPOINT)
...
#endif
```
VxDeverb is the sole exception, causing potential duplicate-symbol linker errors in any multi-plugin or test build.

### Change

Replace the current bare function at the bottom of `VxDeverbProcessor.cpp`:
```cpp
// BEFORE (line 349):
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new VXDeverbAudioProcessor();
}
```
With:
```cpp
// AFTER:
#if !defined(VXSUITE_DISABLE_PLUGIN_ENTRYPOINT) && !defined(VXSTUDIO_DISABLE_PLUGIN_ENTRYPOINT)
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new VXDeverbAudioProcessor();
}
#endif
```

### Verify
- VXDeverb builds and loads normally.
- VXRepair still builds (it compiles `deverb/dsp/` not `deverb/VxDeverbProcessor.cpp`, so this is expected to be a no-op for Repair).
- Run any multi-plugin test build with `VXSUITE_DISABLE_PLUGIN_ENTRYPOINT` defined — should link without duplicate symbol error.

---

## 🟠 HIGH 1 — VXDeepFilterNet: Guard knob does nothing

**Plugin:** VXDeepFilterNet  
**File:** `products/deepfilternet/VxDeepFilterNetProcessor.cpp`

`smoothedGuard` is maintained but never used in `processProduct`. `blendProcessedWithDry()` exists but is never called. The Guard knob currently has zero effect on audio output.

### Design
Guard (0→1) controls wet/dry blend: 0 = full dry (maximum protection), 1 = full DeepFilter output. Artifact risk detection (`estimateArtifactRisk`) should adaptively pull Guard back when the model suppresses too aggressively.

### Changes

**In `processProduct`** (lines 272–291), replace the current body with:

```cpp
void VXDeepFilterNetAudioProcessor::processProduct(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    if (numSamples <= 0 || numChannels <= 0)
        return;

    const float cleanTarget = vxsuite::readNormalized(parameters, productIdentity.primaryParamId, 0.5f);
    const float guardTarget = vxsuite::readNormalized(parameters, productIdentity.secondaryParamId, 0.5f);

    if (!controlsPrimed) {
        smoothedClean = cleanTarget;
        smoothedGuard = guardTarget;
        controlsPrimed = true;
    } else {
        smoothedClean = vxsuite::smoothBlockValue(smoothedClean, cleanTarget, currentSampleRateHz, numSamples, 0.050f);
        smoothedGuard = vxsuite::smoothBlockValue(smoothedGuard, guardTarget, currentSampleRateHz, numSamples, 0.080f);
    }

    const float effectiveClean = vxsuite::clamp01(smoothedClean);

    // Capture dry for wet/dry blend and artifact detection
    ensureLatencyAlignedListenDry(numSamples);
    ensureAnalysisScratch(numChannels, numSamples);
    for (int ch = 0; ch < numChannels; ++ch)
        analysisScratch.copyFrom(ch, 0, buffer, ch, 0, numSamples);

    engine.processRealtime(buffer, currentSampleRateHz, effectiveClean, 0);

    // Artifact risk: scale Guard back when model over-suppresses
    lastArtifactRisk = 0.85f * lastArtifactRisk
                     + 0.15f * estimateArtifactRisk(analysisScratch, buffer, numChannels, numSamples);
    const float artifactPenalty = vxsuite::clamp01(lastArtifactRisk * 0.50f);
    const float effectiveGuard  = vxsuite::clamp01(smoothedGuard * (1.0f - artifactPenalty));

    // Startup ramp: fade in over ~200 ms to prevent click on first process
    const float rampStep = static_cast<float>(numSamples) / (0.200f * static_cast<float>(currentSampleRateHz));
    startupWetRamp = std::min(1.0f, startupWetRamp + rampStep);
    const float wetMix = effectiveGuard * startupWetRamp;

    blendProcessedWithDry(buffer, wetMix);
}
```

### Verify
- Guard at 0.0: output should be identical (or nearly identical) to dry input.
- Guard at 1.0: full DeepFilter output (same as current behaviour).
- Guard at 0.5 (default): audible blend — natural-sounding denoise without full suppression.
- Listening through noisy speech with a deliberate high Clean value and Guard at 0 should sound dry.
- Check that `startupWetRamp` correctly prevents click on plugin load (first block fades from 0→1).

---

## 🟠 HIGH 2 — VXCleanup: archived plugin still shipping

**Situation:** `products/cleanup/` and `VXCleanup.vst3` are in the build. Memory note says VxCleanup is archived, replaced by VXClarity and VXRefine.

**Option A (preferred): Remove from build**
- Find the CMakeLists.txt entry for VXCleanup and comment it out / delete it.
- Do NOT delete the source — keep `products/cleanup/` as archival reference.
- Remove `Source/vxstudio/vst/VXCleanup.vst3/` from git tracking (`git rm -r --cached`).

**Option B (fallback): Tombstone the plugin**
If removing from CMakeLists breaks other things (e.g., shared DSP references), instead make VXCleanup open with a status message telling users to switch, and bypass all audio processing. This is the nuclear fallback.

### Verify (Option A)
- CMake configure runs without VXCleanup target.
- `VXCleanup.vst3` does not appear in the build output directory.
- All other plugins still build and load normally.

---

## 🟠 HIGH 3 — VXSpeechClarity: remove `_CLEAN` development files

**Files to delete:**
- `products/speech_clarity/VxSpeechClarityProcessor_CLEAN.cpp`
- `products/speech_clarity/VxSpeechClarityProcessor_CLEAN.h`

**Steps:**
1. Check CMakeLists.txt for the `speech_clarity` target — confirm neither file is listed as a source. If they are, remove them from the source list first.
2. Delete both files from disk.
3. Confirm the build still compiles without them.

```bash
grep -r "_CLEAN" /path/to/CMakeLists.txt   # should find nothing
```

---

## 🟠 HIGH 4 — VXRebalance: hard `dsp.reset()` causes audible glitch on fader move

**File:** `products/rebalance/VxRebalanceProcessor.cpp`, lines 213–216

When any source fader moves out of center after being at center, `dsp.reset()` cold-starts the source-separation DSP, producing an audible discontinuity on every parameter change.

### Change

Replace the `wasNeutral` transition block:
```cpp
// BEFORE:
if (wasNeutral) {
    dsp.reset();
    wasNeutral = false;
}
```
With a smooth ramp-in instead of a hard reset. Add a member `float rebalanceRampGain = 0.0f;` to `VxRebalanceProcessor.h`.

Then in processProduct, after the neutral check:
```cpp
// AFTER:
if (wasNeutral) {
    // Do NOT reset the DSP — instead ramp in the output over ~80 ms
    // to mask any stale-state artifact without a hard discontinuity.
    rebalanceRampGain = 0.0f;
    wasNeutral = false;
}

dsp.setControlTargets(targets);
dsp.process(buffer);

// Ramp gain: 0 → 1 over ~80 ms (ramp step per block)
const float rampStep = static_cast<float>(numSamples)
                       / (0.080f * static_cast<float>(currentSampleRateHz));
rebalanceRampGain = std::min(1.0f, rebalanceRampGain + rampStep);
if (rebalanceRampGain < 1.0f) {
    // crossfade: ramp DSP output in against latency-delayed dry
    const int numChannels = std::min(buffer.getNumChannels(),
                                     static_cast<int>(dryDelayLines.size()));
    // Re-advance dry delay by numSamples to produce aligned dry output
    // (dryDelayLines is already current since processNeutralWithLatency wrote into it)
    // Simple approach: blend buffer (DSP wet) against delayed dry
    for (int ch = 0; ch < numChannels; ++ch) {
        auto* wet = buffer.getWritePointer(ch);
        auto& delay = dryDelayLines[static_cast<size_t>(ch)];
        const int delaySize = static_cast<int>(delay.size());
        int readPos = (dryDelayWritePos - numSamples + delaySize) % delaySize;
        for (int i = 0; i < numSamples; ++i) {
            const float dryOut = delay[static_cast<size_t>(readPos)];
            wet[i] = dryOut + (wet[i] - dryOut) * rebalanceRampGain;
            readPos = (readPos + 1) % delaySize;
        }
    }
}
```

Also add `rebalanceRampGain = 1.0f;` to `resetSuite()` so normal operation is unaffected.

### Verify
- Move a Vocals fader from center to +50%, back to center, out again — no click should be audible.
- Automation of the fader at medium tempo should produce smooth transitions without glitch.
- Full active processing should sound unchanged when `rebalanceRampGain == 1.0f`.

---

## 🟠 HIGH 5 — VXProximityClassic: shows VXProximity help content

**File:** `products/proximityClassic/VxProximityClassicProcessor.cpp`, lines 37–39

ProximityClassic has a different parameter set (no Mud dial, 2 controls instead of 3) and distinct identity, but exposes the VXProximity help content.

### Change

If a `help::proximityClassic` entry does not yet exist in `VxStudioHelpContent.h/cpp`, create a minimal one. Then replace:
```cpp
// BEFORE:
identity.helpTitle      = vxsuite::help::proximity.title;
identity.helpHtml       = vxsuite::help::proximity.html;
identity.readmeSection  = vxsuite::help::proximity.readmeSection;
```

**Option A:** Add a ProximityClassic-specific help entry to `VxStudioHelpContent.h`:
```cpp
inline constexpr HelpEntry proximityClassic {
    "VX Proximity Classic",
    "<p>A simplified two-control proximity simulator. <b>Closer</b> adds low-end body by "
    "emulating the proximity effect of a directional mic. <b>Air</b> adds upper-presence "
    "shimmer. For the three-control version with Mud compensation, use VX Proximity.</p>",
    "proximity-classic"
};
```

Then update the processor:
```cpp
identity.helpTitle      = vxsuite::help::proximityClassic.title;
identity.helpHtml       = vxsuite::help::proximityClassic.html;
identity.readmeSection  = vxsuite::help::proximityClassic.readmeSection;
```

**Option B (minimal):** If `HelpEntry` cannot be added quickly, at minimum update `productName` in status text and add a note in the existing help section indicating ProximityClassic is the 2-dial variant. Lower priority than Option A.

### Verify
- Open VXProximityClassic in a DAW, open the help panel — the text should describe the 2-dial version, not the 3-dial Mud version.

---

## 🟠 HIGH 6 — VXDeepFilterNet: model hosted on personal GitHub repo

**File:** `products/deepfilternet/VxDeepFilterNetProcessor.cpp`, lines 22–35

The model download URLs point to `https://github.com/daverage/VxStudio/releases/...`. This is a personal account. If the repo is renamed, privatised, or deleted, all user model downloads fail with no fallback.

### Change

Before shipping, move model archives to a company-controlled S3 bucket or CDN. Replace the URL strings:
```cpp
// BEFORE:
"https://github.com/daverage/VxStudio/releases/download/models-v1/DeepFilterNet2_onnx_ll.tar.gz"
"https://github.com/daverage/VxStudio/releases/download/models-v1/DeepFilterNet3_onnx.tar.gz"
```
```cpp
// AFTER (example CDN structure — replace domain with actual):
"https://models.vxstudio.io/v1/DeepFilterNet2_onnx_ll.tar.gz"
"https://models.vxstudio.io/v1/DeepFilterNet3_onnx.tar.gz"
```

Also verify the `fileSize` values (8628785 and 7983136 bytes) still match the new hosted files after upload. Mismatched sizes will cause the download verify check to fail.

### Verify
- Fresh install (no cached model): trigger download from plugin UI, download completes and model loads successfully.
- Model hash/size verification passes.

---

## 🟡 CORRECTNESS 1 — VXTone: dead variable in `peakingEqCoeffs`

**File:** `products/tone/VxToneProcessor.cpp`, line 325

`twoSqrtA` is computed inside `peakingEqCoeffs` but the peaking EQ formula does not use it (unlike the shelf functions which correctly use it). This is a dead computation that will generate a compiler warning.

### Change

In `peakingEqCoeffs` only (NOT in `lowShelfCoeffs` or `highShelfCoeffs`), remove line 325:
```cpp
// DELETE this line (inside peakingEqCoeffs only):
const float twoSqrtA = 2.f * std::sqrt(A);
```

The peaking EQ formula that follows (`b0 = 1.f + alpha * A` etc.) is correct and does not need `twoSqrtA`. The shelf functions use it correctly and must be left unchanged.

### Verify
- Build with `-Wunused-variable` — no warning for peakingEqCoeffs.
- Confirm VXTone low-shelf, high-shelf, and mid-peak still sound correct at various settings.
- A/B bypass test: tone adjustments should be identical before and after this change.

---

## 🟡 CORRECTNESS 2 — VXRepair: `clarity_on` defaults false while `clarity_strength` defaults 0.5

**File:** `products/repair/VxRepairProcessor.cpp`, lines 75–77

After analysis detects clarity issues and calls `applyAssessmentToParams()`, `kClarityOn` is set correctly. But in a fresh session (no analysis yet), strength is pre-loaded at 0.5 while `kClarityOn` defaults false, meaning a user manually enabling clarity gets 50% strength without understanding why.

### Change — adjust the clarity_strength default

Change the default for `kClarityStrength` from `0.5f` to `0.0f` so that a manually-enabled but un-analysed clarity section starts from zero:
```cpp
// BEFORE (line 77):
addStrength(kClarityStrength, "Speech Clarity", 0.5f);

// AFTER:
addStrength(kClarityStrength, "Speech Clarity", 0.0f);
```

This aligns with noise (0.5f) and reverb (0.5f) which are also pre-loaded but hidden behind their On toggle. Re-evaluate whether noise and reverb should similarly default to 0.0f for the same reason — consistency suggests they should, but this requires checking whether analysis-suggested strengths override the defaults on first enable.

### Verify
- Fresh open of VXRepair: all three strengths at 0, all On toggles off.
- After analysis completes and user clicks Apply: strengths populated from assessment, On toggles activated for detected issues.
- Manually enabling clarity without analysis: starts from 0% strength, not 50%.

---

## 🟡 CORRECTNESS 3 — VXDeverb: direct public member access bypasses accessor

**File:** `products/deverb/VxDeverbProcessor.cpp`, line 217

`processProduct` writes `deverbProcessor.voiceMode = voiceMode;` directly while the class also exposes `setVoiceMode()` / `isVoiceMode()` which wrap the same field.

### Change

Replace the direct assignment:
```cpp
// BEFORE (line 217):
deverbProcessor.voiceMode = voiceMode;

// AFTER:
deverbProcessor.setVoiceMode(voiceMode);
```

If `setVoiceMode` is not on `SpectralProcessor` (only on the outer `VXDeverbAudioProcessor`), then the internal member access is fine and this item can be skipped. Verify by checking `VxDeverbSpectralProcessor.h` for `setVoiceMode()`.

---

## 🔵 COMPETITIVE 1 — VXClarity + VXToneRefine: all controls default to zero

**Plugins:** VXClarity (`speech_clarity/VxSpeechClarityProcessor.cpp:32–36`), VXToneRefine (`tone_refine/VxToneRefineProcessor.cpp:31–33`)

All controls default 0 (fully bypassed). Users opening the plugin hear nothing happen, reducing first-impression impact.

### Recommended defaults for VXClarity
```cpp
identity.primaryDefaultValue   = 0.4f;   // Sibilance — light de-essing by default
identity.secondaryDefaultValue = 0.3f;   // Plosive — light correction
identity.tertiaryDefaultValue  = 0.2f;   // Breath — minimal, most sources don't need it
```

### Recommended defaults for VXToneRefine
```cpp
identity.primaryDefaultValue   = 0.25f;  // Mud — gentle low-mid cleanup
identity.secondaryDefaultValue = 0.20f;  // Harshness — light presence softening
identity.tertiaryDefaultValue  = 0.15f;  // Smooth — barely perceptible on clean sources
```

**Note:** These are starting suggestions. Before shipping, test these defaults on 5–10 reference recordings (podcasts, VO, music) and tune so the default makes the plugin feel useful on open without being heavy-handed.

### Verify
- Open both plugins fresh — audio should be subtly processed at default positions.
- Reset to default: same subtle processing.
- Pulling all knobs to 0 should be true bypass.

---

## 🔵 COMPETITIVE 2 — VXSubtract: static noise profile limitation

**File:** `products/subtract/VxSubtractProcessor.cpp:147`

The code itself notes: *"in long sessions with changing noise floors (e.g., HVAC cycling), users should re-learn to adapt. This is a known limitation; continuous re-learning is future work."*

### Pre-ship action (documentation, not code)
Add visible user guidance in the status text when the profile is older than a reasonable duration. Track when the profile was learned (a `juce::Time` stamp in the DSP state) and update `getStatusText()`:

```cpp
// If profile was learned > 20 minutes ago, warn:
if (isLearnReady() && profileAgeSeconds > 1200.0f)
    return "Profile ready — note: noise floor may have changed, consider re-learning";
```

This sets user expectation without requiring the full adaptive implementation.

---

## 🔵 COMPETITIVE 3 — All spectral processors: no GR metering display

**Plugins:** VXDenoiser, VXDeverb, VXSubtract

These plugins apply spectral gain reduction but offer no dB-referenced display. The activity LED approach used in OptoComp/Finish/Leveler is correct; the spectral tools need the same treatment.

**VXDenoiser** should expose GR via `getActivityLight`:
- Add `getActivityLightCount() = 1` override returning 1
- `getActivityLight(0)` = `denoiserDsp.getGainReductionDb() / 20.0f` (check if this method exists on `DenoiserDsp`, add it if not)
- `getActivityLightLabel(0)` = `"GR"`

**VXDeverb** similarly:
- `getActivityLightCount() = 1`
- `getActivityLight(0)` = deverb GR estimate (tail energy removed / input energy)
- `getActivityLightLabel(0)` = `"GR"`

**VXSubtract:**
- `getActivityLightCount() = 1`  
- `getActivityLight(0)` = subtraction depth (can be approximated from `smoothedSubtract * learnedReady ? 1.0f : blindAmount`)
- `getActivityLightLabel(0)` = `"Sub"`

This requires verifying what the DSP exposes and potentially adding a `getGainReductionDb()` method to each DSP class.

---

## Build order recommendation

Execute in this order to minimise risk of breaking shared DSP:

1. ✅ **Blocker 2** (VXDeverb guard) — DONE  
2. ✅ **Correctness 1** (VXTone dead variable) — DONE  
3. ✅ **High 5** (ProximityClassic help) — DONE  
4. ✅ **High 3** (delete _CLEAN files) — DONE  
5. ✅ **Blocker 1** (VXClarity DSP) — DONE (fixed type errors, wired DSP, added to CMake)  
6. ✅ **High 1** (DeepFilter Guard) — DONE  
7. ✅ **High 4** (VXRebalance ramp) — DONE  
8. ✅ **High 2** (VXCleanup removal) — DONE (commented out of CMake, un-tracked VST3)  
9. ✅ **Correctness 2** (Repair clarity default) — DONE  
10. ✅ **Correctness 3** (Deverb accessor) — DONE: added `setVoiceMode()` to SpectralProcessor  
11. ✅ **High 6** (DeepFilter model URL) — ACCEPTED: model is publicly downloadable from GitHub releases, no CDN needed  
12. ✅ **Competitive 1** (default knob values) — DONE  
13. ✅ **Competitive 2** (Subtract status text) — DONE  
14. ✅ **Competitive 3** (GR metering) — DONE: GR on Denoiser (mean gainSmooth→dB), Deverb (first-channel gainSmooth), Subtract (subtract strength display)

---

## Shared DSP re-test matrix

Any change to files under `dsp/` in the following products also affects VXRepair (which embeds those DSPs directly):

| DSP change in... | Must also re-test... |
|---|---|
| `denoiser/dsp/` | VXDenoiser + VXRepair |
| `deverb/dsp/` | VXDeverb + VXRepair |
| `speech_clarity/dsp/` | VXClarity + VXRepair |
| `deepfilternet/dsp/` | VXDeepFilterNet + VXRepair |

After completing Blocker 1 (VXClarity), load VXRepair and verify the clarity section still works correctly — specifically that the `dePlosiveDsp`, `deEsserDsp`, `deBreathDsp` calls in `VxRepairProcessor.cpp` produce the same output as before.
