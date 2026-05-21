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

---

## Algorithm Quality Decisions (Phase 1 Review)

### Denoiser: OM-LSA vs MMSE-LSA

**Choice:** OM-LSA (Optimal Modified Log-Spectral Amplitude) suppression

**Why:** OM-LSA is more conservative than MMSE-LSA, preserving consonant articulation better at the cost of slightly higher residual noise. For voice-focused denoising, this tradeoff favors speech clarity.

**Evidence:**
- OM-LSA gain curve is derived from log-spectral distance minimization, which aligns with human perceptual weighting
- MMSE-LSA minimizes mean-square error in linear amplitude, which tends over-suppress in noisy regions
- Phase 1 review confirmed implementation uses correct OM-LSA estimator

**Quality assurance:** Phase 2 (Algorithm Audits) should A/B listen across SNR levels (-5dB to +15dB) to validate that OM-LSA preserves intelligibility better than the alternative.

### Deverb: WPE Algorithm Selection

**Choice:** Weighted Prediction Error (WPE) for dereverberation, with cepstral RT60 estimation

**Why:** WPE is state-of-the-art for single-channel dereverberation of speech, with lower latency than frequency-domain methods and good quality on close-mic sources.

**Tradeoff:** WPE works best on close-mic/speech sources. Performance degrades on far-field or music sources with extreme reverberation (RT60 > 2.0s).

**Implementation detail:** RT60 is estimated from cepstral coefficients and used to adapt prediction filter length dynamically.

**Quality assurance:** Phase 2 (Algorithm Audits) should test WPE on varied reverberant sources, validate RT60 estimates, and confirm that the adaptive filter length prevents over-subtraction.

### DeepFilterNet: Neural Network Dereverberation/Denoising

**Choice:** DeepFilterNet 2/3 ONNX-based inference

**Why:** DFN achieves top-tier DNS Challenge benchmark scores for combined noise suppression + dereverberation, especially on reverberant-noisy mixes.

**Safety concern addressed in Phase 1:** DeepFilterNet had zero framework analysis integration, meaning it could suppress speech aggressively in low-SNR conditions. Phase 1 added `speechSafetyFactor` gating to reduce model strength when speech presence is low or intelligibility risk is high.

**Quality assurance:** Phase 2 should validate that the added safety gating does not degrade quality on high-SNR sources, and that DFN3 (if available) performs better than DFN2.

### Leveler: Adaptive Gain Riding with Offline Analysis

**Choice:** Onset-based voice detection + adaptive gain riding + loudness tracking

**Why:** Onset-based detection is more stable than energy-only envelope following, and offline loudness analysis provides a reference plan before online processing.

**Bug fixed in Phase 1:** Offline analysis was not invalidated on sample-rate changes, causing block-based indexing misalignment. Now cleared automatically on SR change.

**Quality assurance:** Phase 2 should validate that offline analysis produces accurate target levels and that online gain riding matches the offline plan within ±2 LUFS.

### Subtract: Spectral Profiling with Adaptive Confidence

**Choice:** FFT-based spectral profiling + Wiener-like gain application with learn mode

**Why:** Simple, transparent, and gives users explicit control via learn button. Adaptive confidence threshold (based on monoScore) improves robustness on both mono and stereo recordings.

**Limitation (documented in Phase 1):** Learned profile is static. Long sessions with changing noise floors require manual re-learn. This is acceptable; continuous online learning is future work.

**Quality assurance:** Phase 2 should test on varied recording types (mono voice, stereo music, phone, etc.) and validate that adaptive threshold behaves correctly.

### Cleanup/Tone/Proximity: Proven Designs, No Algorithm Changes

**Cleanup:** Corrective EQ + de-mud + de-ess with articulation protection. Working as designed; Phase 1 added sidechain boost from Denoiser.

**Tone:** Simple dual-shelf EQ with mode-dependent frequencies. Well-tuned curve exponent (0.72f); no changes needed. No dynamic range awareness (intentional: would add complexity).

**Proximity:** Close-mic simulation with vocal dominance gating. Hardcoded blending coefficients are empirically tuned for voice; tested on music reveals no degradation. Defaults changed from 0.0f to 0.25f (soft default) for better out-of-box UX.

### Finish/OptoComp: LA-2A Time Constants Validated

**Choice:** LA-2A-style opto compressor with dual-stage release

**Implementation verified:**
- Attack: ~10ms (program-dependent, optical cell)
- Release (50%): ~60ms (fast stage)
- Release (full): 0.5–5s (slow stage, optical memory)
- Ratio: ~3:1 (fixed)

**Validation:** Time constants match hardware spec. No code changes needed. Phase 2 can skip this audit (already confirmed).

---

## Quality Assurance Plan for Phase 2

### Listening Test Corpus

**Core:** Existing voice corpus (4 historical speeches, 48kHz, ~4 min total)

**Expansion (Phase 2):**
- WHAMR or synthetic reverb: 5–10 reverberant samples at RT60 0.3s, 0.6s, 1.0s (Deverb testing)
- DNS Challenge or synthetic noisy: 5–10 samples at SNR -5dB, 0dB, 5dB, 10dB, 15dB (Denoiser testing)
- LibriSpeech test-clean: 50 diverse utterances (DeepFilterNet validation)

### A/B Listening Methodology

For each product audit:
1. **Setup:** Load plugin in DAW, prepare A/B switch between dry and processed
2. **Listen:** 2–3 passes per sample, focusing on specific aspects (clarity, artifacts, artifact-free noise reduction)
3. **Document:** Note whether effect moves in intended direction, any subjective concerns
4. **Measurement (where applicable):** Check automated metrics (SNR improvement, level consistency, latency)

### Specific Audit Success Criteria

| Product | Metric | Target | Test Corpus |
|---------|--------|--------|-------------|
| Denoiser | SNR improvement | ≥6dB at all SNR levels | Synthetic noisy (5 SNR levels) |
| Denoiser | Articulation | Preserved (no lisping) | Noisy speech samples |
| Deverb | Reverb reduction | ≥50% perceptually | Synthetic reverb (3 RT60 values) |
| Deverb | RT60 estimate | Within ±0.2s | Synthetic reverb |
| DeepFilterNet | Clarity | No muffling/phasey artifacts | LibriSpeech subset (10 samples) |
| DeepFilterNet | Safety gating | Effective in low-SNR | Noisy speech at -5dB SNR |
| Leveler | Level consistency | Within ±2 LUFS | Existing corpus (dynamic range) |
| Leveler | Offline accuracy | ±2 LUFS | Leveler-specific test signal |
| OptoComp | Time constants | Attack ~10ms, Release two-stage | Spec validation only |

---

## Deferred Algorithm Improvements

These are documented for future consideration; Phase 1 review did not implement them:

1. **Denoiser HF preservation:** At high denoise amounts (>0.70), preserve air above 8kHz. ✓ Phase 1 implemented (up to 12% boost)
2. **DeepFilterNet perceptual gating:** Reduce model strength in low-SNR scenarios. ✓ Phase 1 implemented (speechSafetyFactor)
3. **Deverb WPE verification:** A/B listen against alternatives, validate RT60 accuracy. → Phase 2 (Algorithm Audits)
4. **Subtract ReadabilityGuard:** Post-pass clarity check to prevent over-subtraction. → Phase 2/3 (medium effort)
5. **Rebalance denoiser sidechain:** Gate stem reduction when noise floor is high. → Future (high effort)

---

## References

- [Phase 1 Completion Summary](/Users/andrzejmarczewski/.claude/plans/phase-1-completion-summary.md)
- [VX Suite Framework](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/docs/VX_SUITE_FRAMEWORK.md)
- [Phase 2 Audit Methodology](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/data/voice_corpus/PHASE2_AUDIT_METHODOLOGY.md)
