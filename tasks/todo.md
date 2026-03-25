# VX shared model download progress + DeepFilter bundle size — 2026-03-25

## Problem
The new shared model-download flow is too opaque once the user accepts the prompt: the popup closes immediately and the only remaining feedback is a subtle disabled button/state text. At the same time, the staged `VXDeepFilterNet.vst3` bundle is still over 100 MB locally, so we need to verify whether model payloads are still being embedded in the plugin binary or otherwise retained despite the new external-download path.

## Plan
- [x] Inspect the current shared editor/model-download seams and the `VXDeepFilterNet` build inputs to identify the smallest clean fix.
- [x] Add persistent shared UI feedback for model downloads after the popup closes, using the existing framework progress state rather than a product-specific hack.
- [x] Remove or disable any remaining embedded-model build inputs that still inflate `VXDeepFilterNet` now that it uses external model downloads.
- [x] Rebuild the affected targets, inspect the staged bundle contents/size, and record the kept result here.

## Review
- Added shared persistent model-download feedback in [VxSuiteProcessorBase.h](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/framework/VxSuiteProcessorBase.h), [VxSuiteEditorBase.h](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/framework/VxSuiteEditorBase.h), and [VxSuiteEditorBase.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/framework/VxSuiteEditorBase.cpp). The popup remains consent-only; once a download starts, the shared editor now shows a persistent `Model download X%` row with a progress bar until the asset is ready.
- Wired the shared progress hook through both current model-download products in [VxDeepFilterNetProcessor.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/deepfilternet/VxDeepFilterNetProcessor.cpp), [VxDeepFilterNetProcessor.h](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/deepfilternet/VxDeepFilterNetProcessor.h), [VxRebalanceProcessor.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/rebalance/VxRebalanceProcessor.cpp), and [VxRebalanceProcessor.h](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/rebalance/VxRebalanceProcessor.h), reusing `ModelAssetService::progress(...)` instead of adding product-local download state.
- Verified that the staged `VXDeepFilterNet` bundle was not still carrying the old model archives in `Contents/Resources`; the remaining size was dominated by the plugin executable itself. Added a targeted macOS post-build strip step for the staged `VXDeepFilterNet` binary in [CMakeLists.txt](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/CMakeLists.txt), followed by re-signing.
- Verification:
  - `cmake --build build --target VXDeepFilterNetPlugin VXRebalancePlugin -j4`
  - `codesign --verify --deep --strict Source/vxsuite/vst/VXDeepFilterNet.vst3`
  - `codesign --verify --deep --strict Source/vxsuite/vst/VXRebalance.vst3`
- Measured kept outcome after rebuild:
  - `Source/vxsuite/vst/VXDeepFilterNet.vst3` dropped from `105M` to `80M`
  - `Source/vxsuite/vst/VXDeepFilterNet.vst3/Contents/MacOS/VXDeepFilterNet` dropped from `73M` to `47M`
  - staged `VXDeepFilterNet` resources now contain only `moduleinfo.json`
- Kept conclusion: the earlier “full model still embedded” issue is no longer in the bundle resources. The remaining DeepFilterNet size comes mostly from shipping both native DeepFilter runtimes plus plugin code; stripping removed a meaningful chunk of symbol weight, but a further size reduction would require changing which runtime(s) DeepFilterNet ships.

# VX Rebalance background inference thread — 2026-03-25

## Problem
`VX Rebalance` with the ONNX Open-Unmix model cannot run in realtime on a modest machine because `onnxModel.run()` is called directly on the audio thread inside `analyseBlock()`. Open-Unmix is an offline BiLSTM model: every hop it processes 64 frames × 2049 bins × 2 channels (~1M floats through a recurrent network), which takes hundreds of milliseconds and causes dropouts. DeepFilterNet works because its purpose-built streaming C library processes tiny 480-sample frames designed for realtime use — a fundamentally different architecture.

## Plan
- [ ] Add a background `juce::Thread` + `juce::WaitableEvent` to `ModelRunner`. The thread owns the FFT accumulation and ONNX inference pipeline.
- [ ] Replace `SampleFifo` with a `juce::AbstractFifo`-based lock-free queue for the audio-thread-to-inference-thread audio handoff.
- [ ] `analyseBlock()` becomes: resample + push to lock-free queue + signal thread. No FFT or inference on the audio thread.
- [ ] Protect `latestSnapshot` with a `juce::SpinLock` for safe cross-thread reads.
- [ ] Build, run regression tests, and verify no audio-thread blocking by checking that dropouts are gone in host.

## Review

# VX instrument-balancer concept evaluation — 2026-03-24

# VX Suite shared help + text-fit + semantic versioning pass — 2026-03-24

## Problem
The shared plugin UI still has readability issues on first load, especially where labels rely on aggressive horizontal scaling instead of robust fit rules. The framework also lacks a reusable Help surface, so product guidance is split between the repo README and user intuition rather than living inside the plugins. On top of that, versioning is still effectively global and informal, while the user now wants proper independent semantic versions for the framework and each DSP.

## Plan
- [ ] Audit the shared editor/layout system, current public plugin documentation, and existing version metadata to identify the cleanest framework-level extension points.
- [ ] Add a framework-level fit-text pass so titles, status lines, knob labels, hints, and related shared UI text do not appear squashed on initial load.
- [ ] Add a reusable Help button in the shared editor and a modal/popup capable of rendering HTML help content.
- [ ] Extend shared product metadata so each plugin can provide HTML help content plus its own independent semantic version, while the framework also tracks its own version independently.
- [ ] Add per-plugin help content providers and keep the in-plugin content aligned with the public README documentation for every shipped plugin.
- [ ] Add a framework-level documentation-maintenance rule reminding contributors to keep both the in-plugin help popup and the README in sync.
- [ ] Build and verify the UI/layout/help/version changes, then record the review outcome here.

## Problem
We need to evaluate a new VX Suite DSP idea that lets the user raise or lower broad instrument groups from one place: `Bass`, `Drums`, `Vocals`, `Guitar`, and `Other`. The recommendation needs to stay aligned with the VX Suite framework, remain credible for realtime use, prefer elegant/simple architecture, and reuse external open-source code where that genuinely helps. We should compare non-ML and very-light-ML paths rather than assume full stem-separation ML is appropriate.

## Plan
- [x] Review the VX Suite framework, current product history, and the existing light-ML path for constraints that affect a multi-slider product.
- [x] Compare the main architecture options: no-ML intelligent control, lightweight realtime ML, and heavier source-separation-style designs.
- [x] Evaluate likely open-source building blocks and note where they fit or do not fit VX Suite’s realtime/product rules.
- [x] Write the recommended product direction, phased implementation plan, and verification approach here.

## Review
- Best fit for VX Suite: ship this as a new product with a shared framework slider layout and a deliberately simple contract such as `Bass`, `Drums`, `Vocals`, `Guitar`, `Other`, plus a lightweight `Focus` or `Precision` control only if testing proves it is needed. This is one of the rare cases where more than two main controls is justified because the whole product promise is direct group balancing.
- Recommended v1 architecture: do not begin with full ML stem separation. Start with an intelligent hybrid controller built from fast analysis and guided spectral rebalancing: low-band ownership for `Bass`, transient/onset density for `Drums`, shared vocal-context plus speech/lead cues for `Vocals`, mid/high harmonic + pitch-stability cues for `Guitar`, and residual energy for `Other`. That keeps latency, CPU, and maintainability compatible with the suite while still feeling meaningfully “smart”.
- Best open-source building blocks for that v1 path are analysis libraries, not separator models. `aubio` is useful for onset/pitch helpers, but its GPL license makes direct embedding awkward for a commercial-safe core. `Essentia` has rich MIR features but is AGPL, so it is better as a research/prototyping reference than as an embedded dependency. Practical conclusion: reuse their ideas and offline prototypes, but prefer implementing the shippable realtime primitives inside `Source/vxsuite/framework/` and the product DSP locally.
- Recommended v2 path, if v1 proves musically useful but not selective enough: add a very small optional ML assist that estimates control masks or ownership priors rather than generating full stems. This is the closest analogue to the current `DeepFilterNet` philosophy: use ML to guide a lightweight corrective path, not to run a heavyweight full demixer every block.
- Not recommended for v1 realtime shipping: direct reuse of `Demucs`, `Open-Unmix`, or `Spleeter` as the live core. They are valuable references and good offline benchmarks, but they are still fundamentally separation-first systems with materially higher context, CPU, latency, and integration burden than the suite currently tolerates. `Demucs` is also archived upstream, which lowers confidence for a new product dependency.
- If true realtime multi-stem ML becomes a hard requirement later, the most credible research direction is a causal/low-latency demixer family rather than offline-quality stem models adapted after the fact. That should be treated as a separate R&D phase with explicit latency and artifact gates.
- Framework implication: because the shared editor currently tops out at four rotary controls, implement a generic multi-slider control surface in the framework instead of a one-off product editor. This product is the right place to add a reusable “group mixer” layout to VX Suite.
- Verification should be staged:
  - Stage 1: offline labelled-stem evaluation on public multitrack material to score control selectivity (`raise vocals` should mostly raise vocal stem energy, etc.).
  - Stage 2: realtime plugin tests for CPU, latency, automation continuity, silence/reset safety, and stereo-image stability.
  - Stage 3: musical listening tests on dense mixes to judge whether the plugin behaves like an intelligent rebalance tool rather than a phasey stem separator.

# VX Rebalance framework-native spec rebuild — 2026-03-24

## Problem
The user provided a useful first-pass source-rebalance plugin spec, but it assumes a standalone JUCE plugin shape rather than the current VX Suite framework. We need to rebuild it into a VX-native spec that preserves the DSP idea while making the framework implications explicit: shared processor/editor base usage, parameter/layout contracts, slider-bank UI support, realtime rules, and verification expectations.

## Plan
- [x] Re-read the current VX Suite framework seams that constrain parameters and editor layout.
- [x] Rebuild the standalone source-rebalance brief into a VX Suite product spec for `VX Rebalance`.
- [x] Record the completed spec and note the required framework extension clearly.

## Review
- Rebuilt the standalone JUCE-first brief into a VX Suite-native product spec at [VX_REBALANCE_SPEC.md](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/docs/VX_REBALANCE_SPEC.md).
- Kept the user’s core V1 DSP direction intact: heuristic STFT-domain ownership masks, no ML inference, bounded source-family rebalance, low-end/transient guardrails, and a global `Strength` control.
- Adjusted the product contract to match the current suite architecture: `VX Rebalance` is specified as a `ProcessorBase` product with product-local DSP modules and explicit shared-framework work for a reusable multi-slider control bank.
- Made the main framework gap explicit: the current `ProductIdentity` / `createSimpleParameterLayout(...)` / `EditorBase` path tops out at four rotary controls, so this product should drive a clean framework extension for slider-bank layouts and centered rebalance-style parameter formatting rather than a one-off editor fork.

# VX Rebalance implementation — 2026-03-24

## Problem
The user wants `VX Rebalance` built now from the new spec. That means shipping both halves together: a reusable framework extension for a five-slider control bank and the first working `VX Rebalance` product using heuristic STFT-domain source-family masks.

## Plan
- [x] Extend the shared framework so a product can declare a multi-control slider bank without breaking existing 2–4 knob products.
- [x] Add centered rebalance-style parameter formatting/layout support for `VX Rebalance`.
- [x] Implement the `VX Rebalance` product skeleton, DSP engine, and product wiring under `Source/vxsuite/products/rebalance/`.
- [x] Add the new plugin target to the build and stage it like the other VX products.
- [x] Build the new target, fix compile/runtime issues, and run focused verification.
- [x] Record the kept outcome and any risks here.

## Review
- Added a shared framework control-bank path so products can declare up to six banked controls in [VxSuiteProduct.h](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/framework/VxSuiteProduct.h), with matching editor support in [VxSuiteEditorBase.h](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/framework/VxSuiteEditorBase.h) and [VxSuiteEditorBase.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/framework/VxSuiteEditorBase.cpp). Existing 2–4 knob products still stay on the old path.
- Added centered rebalance-style parameter display support in [VxSuiteParameters.h](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/framework/VxSuiteParameters.h) so source-family controls can present `-100% ... +100%` style values while staying normalized internally.
- Implemented the new product in [VxRebalanceProcessor.h](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/rebalance/VxRebalanceProcessor.h), [VxRebalanceProcessor.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/rebalance/VxRebalanceProcessor.cpp), [VxRebalanceDsp.h](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/rebalance/dsp/VxRebalanceDsp.h), and [VxRebalanceDsp.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/rebalance/dsp/VxRebalanceDsp.cpp). V1 is a linked-stereo STFT overlap-add rebalance engine with heuristic masks for `Vocals`, `Drums`, `Bass`, `Guitar`, and `Other`, plus a sixth `Strength` slider and low-end/transient guardrails.
- Added the new plugin target and staging path in [CMakeLists.txt](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/CMakeLists.txt). A staged bundle now exists at [VXRebalance.vst3](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/vst/VXRebalance.vst3).
- Focused verification:
  - `cmake -S . -B build`
  - `cmake --build build --target VXRebalancePlugin -j4`
- One unrelated framework compile issue surfaced during the first build: [VxSuiteHelpView.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/framework/VxSuiteHelpView.cpp) was already missing `toJuceString(...)` visibility, so I added the needed include while getting the new target through.
- Known v1 risks:
  - the heuristic mask engine is intentionally simple and not yet tuned against labelled stem datasets, so selectivity is plausible but not yet proven
  - the new control-bank UI currently renders all six controls as a vertical slider bank, which is the right shared-framework move for now but may still need visual polish
  - no product-specific measurement harness exists yet for selectivity, low-end stability, or transient retention, so this build is “compiles and stages” verified rather than fully outcome-verified

## Follow-up correction
- The first `VX Rebalance` pass had the wrong control contract and the wrong ownership priority. The source sliders were presented too much like broad percentage gain moves, and the initial heuristic masks let `Bass` and especially `Guitar` claim too much of the spectrum, which made several sliders feel like they were all moving the same material.
- I corrected the user-facing control range by switching the five source-family sliders to centered `dB` display in [VxSuiteParameters.h](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/framework/VxSuiteParameters.h) and [VxRebalanceProcessor.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/rebalance/VxRebalanceProcessor.cpp). They now read as bounded rebalance moves around `0 dB`, which is a much more honest fit for a heuristic source balancer.
- I corrected the deeper DSP bug in [VxRebalanceDsp.h](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/rebalance/dsp/VxRebalanceDsp.h) and [VxRebalanceDsp.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/rebalance/dsp/VxRebalanceDsp.cpp):
  - `Vocals` now get a stronger voice-biased claim in the speech/formant region
  - `Drums` now get stronger transient-led claims across kick/snare/cymbal regions
  - `Bass` is narrower and backs off more during kick/transient-heavy frames
  - `Guitar` is demoted from a broad default owner to a narrower residual midrange lane
  - `Other` now behaves more like a true residual catch-all
- I also fed shared framework voice-context evidence into the rebalance DSP so vocal ownership is no longer purely static-frequency-based.
- Verification after the correction:
  - `cmake --build build --target VXRebalancePlugin -j4`
- Remaining gap: this is a stronger heuristic allocation than the first pass, but I still have not run a labelled-stem selectivity harness or in-host listening pass on the corrected build, so the retune is compile-verified, not yet fully musically verified.

## Research notes
- Reviewed current mixing/source-separation references before further tuning. The key practical takeaway is that real live/mastered music does not divide cleanly by one static frequency map; instead, each source tends to have a few strong ownership regions plus a lot of harmonic/transient overlap.
- Stable source-prior anchors from the references:
  - `Bass`: strongest fundamentals/sub energy in roughly `20–160 Hz`, with mud/conflict often around `200–400 Hz` and attack/presence up into about `700 Hz–2 kHz`. See [iZotope bass guide](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/tasks/todo.md) research summary and external source [How to EQ bass to sit well in the mix](https://www.izotope.com/en/learn/how-to-eq-bass).
  - `Kick / drums`: kick punch commonly around `60–100 Hz`, body around `100–250 Hz`, click/attack around `1–5 kHz`; snare body around `150–250 Hz`, attack around `2–5 kHz`, cymbal/hat energy higher still. See [Yamaha EQ guide](https://hub.yamaha.com/proaudio/livesound/eq/) and [iZotope EQ cheat sheet](https://www.izotope.com/en/learn/eq-cheat-sheet).
  - `Vocals`: low rumble generally below `80–100 Hz`, body/warmth around `100–400 Hz`, box/nasal zones around `400 Hz–1.5 kHz`, intelligibility around `1.5–5 kHz`, sibilance `5–8 kHz`, air above that. See [How to EQ vocals](https://www.izotope.com/en/learn/how-to-eq-vocals.html/1000).
  - `Guitar`: lives mostly as midrange and upper-mid harmonic content; electric guitar harshness often `1–2 kHz`, body/punch can reach into a few hundred Hz, and pick/pluck lives in upper mids. It should not own the whole `250 Hz–5 kHz` band by default. See [Yamaha EQ guide](https://hub.yamaha.com/proaudio/livesound/eq/), [Sweetwater guitar EQ notes](https://www.sweetwater.com/insync/quickie-guide-mixing-part-14/), and [iZotope mixing guide PDF](https://downloads.izotope.com/guides/iZotope-Mixing-Guide-Principles-Tips-Techniques.pdf).
- Important mix-structure findings:
  - The broad low end is usually a negotiated space between kick and bass, not “bass owns lows” or “drums own lows” globally. The exact split often depends on which instrument carries the main fundamental in that song.
  - The midrange (`500 Hz–2 kHz`) is shared by almost everything in pop/rock mixes, including vocals, snare, guitars, keys, and horns. So a static “midrange = guitar” heuristic is especially wrong on mastered mixes.
  - The `2–8 kHz` region is not “vocals only” or “drums only”; it contains vocal intelligibility, kick/snare attack, acoustic pick, cymbals, breaths, and plenty of guitar detail. Ownership there needs transient/steadiness/voice evidence, not only frequency.
  - Center information matters: bass, kick, snare, vocals, and other low-tone anchors are commonly mixed near the center, while higher-frequency material often spreads wider. See [Sweetwater stereo-field note](https://www.sweetwater.com/insync/get-creative-with-the-stereo-field/).
  - Even reference source-separation datasets are imperfect: MUSDB18 documents bleed and stem-label ambiguity such as “other mixed into drums” and “bleeding of other instruments into vocals,” which is a good reminder that our heuristic lanes must be soft, not absolute. See [MUSDB18](https://sigsep.github.io/datasets/musdb.html).
- Research-backed tuning direction for the next VX Rebalance pass:
  - Treat `Vocals` as a center-weighted, voice-evidence-weighted lane with strongest ownership around `150 Hz–5 kHz`, but only when the spectrum is sufficiently steady and speech-like.
  - Split `Drums` into sub-lanes conceptually: kick (`45–110 Hz` transient-led), snare/body (`150 Hz–300 Hz` + `2–5 kHz`), cymbal/hat (`5 kHz+` transient-led).
  - Treat `Bass` as fundamentals plus first harmonics with continuity and center weighting, but explicitly suppress its claim during strong kick-like transient frames.
  - Treat `Guitar` as a residual harmonic lane shaped by steadiness, midrange harmonic density, and reduced center priority, not as a default owner of everything non-vocal in the mids.
  - Use `Other` as the true residual bucket for keys, ambience, pads, brass, backing clutter, and uncategorized overlap.

## Research-driven retune
- Reworked the `VX Rebalance` ownership logic in [VxRebalanceDsp.h](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/rebalance/dsp/VxRebalanceDsp.h) and [VxRebalanceDsp.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/rebalance/dsp/VxRebalanceDsp.cpp) around the research findings instead of continuing to tweak the earlier static-band version.
- The retuned heuristics now:
  - derive per-bin `center` vs `side` weighting from stereo mid/side magnitude during each STFT frame
  - bias `Vocals` toward center-heavy, speech-evidence-weighted, steadier bins in roughly the vocal body/presence region
  - split `Drums` into low kick support, low-mid snare/body support, upper-mid attack support, and high-band cymbal/hat support, all transient-led
  - make `Bass` more center/continuity-driven and explicitly reduce its claim during kick-like transient frames
  - make `Guitar` a narrower harmonic residual lane, with less authority when the bin is center-heavy and voice-like
  - keep `Other` as the true catch-all residual bucket rather than an afterthought
- I also kept the earlier `dB around center` source-slider contract, so the controls now behave more like believable rebalance moves instead of pseudo-stem volume sliders.
- Verification after the research-driven retune:
  - `cmake --build build --target VXRebalancePlugin -j4`
- Remaining limitation: this is still a heuristic rebalance engine on mastered stereo audio, so the build is stronger conceptually and compile-verified, but it still needs host listening and ideally a labelled-stem selectivity harness to prove whether the new ownership rules are actually better in practice.

## User correction
- After the research-driven retune, the user reported that `Vocals` still control too much of the spectrum. That confirms the broader conclusion from the tuning loop: on mastered stereo material, the overlap between vocals, guitars, snare attack, keys, and other center-heavy content is too strong for this heuristic-only v1 approach to be trustworthy.
- Kept conclusion: `VX Rebalance` likely needs an ML-guided ownership stage for this job if it is meant to feel convincingly source-specific on mixed/mastered recordings.

## Engineering follow-up
- The user then reviewed the implementation and identified several real engineering issues in the current `VX Rebalance` DSP path.
- Fixed in [VxRebalanceDsp.h](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/rebalance/dsp/VxRebalanceDsp.h) and [VxRebalanceDsp.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/rebalance/dsp/VxRebalanceDsp.cpp):
  - replaced the per-sample O(n) output FIFO left-shift with a circular-buffer read/write scheme
  - changed control-target and analysis-context handoff to atomically published values
  - added DSP-local `juce::SmoothedValue` control smoothing so source gains move continuously rather than stepping at block boundaries
  - moved `prevAnalysisMag` state update out of the composite-gain loop for clearer maintenance
  - documented the current `kMinCut = 0.5f` floor in code as an intentional v1 guardrail rather than an accidental hidden limit
- Verification after the engineering fixes:
  - `cmake --build build --target VXRebalancePlugin -j4`
- Important context: these fixes improve CPU correctness and realtime behavior, but they do not change the broader product conclusion above that source-specific mastered-mix rebalance still likely needs ML guidance to be truly convincing.

## Signal-quality detector
- Added an internal `SignalQuality` path to [VxRebalanceDsp.h](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/rebalance/dsp/VxRebalanceDsp.h) and [VxRebalanceDsp.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/rebalance/dsp/VxRebalanceDsp.cpp).
- The detector now estimates, per frame with smoothing:
  - `monoScore`
  - `compressionScore`
  - `tiltScore`
  - derived `separationConfidence`
- The current retune uses those signals conservatively:
  - reduce vocal center bias when the recording is effectively mono
  - reduce “steady = vocal” bias when dynamics are crushed
  - lower the transient threshold under compression so drums still register
  - weaken bass continuity bonus under strong low-frequency tilt
  - blend all source gains back toward unity when separation confidence is low
- Verification:
  - `cmake --build build --target VXRebalancePlugin -j4`
- This is the right pre-ML step because it makes the heuristic path more self-aware and gives us a confidence signal we can later reuse if the product pivots to ML-guided ownership.

## Framework signal-quality layer — 2026-03-24

## Problem
The current signal-quality detector now exists inside `VX Rebalance`, but the idea is broader than that one product. We need to recreate it as a proper shared VX Suite framework analysis layer so all products can read the same recording-quality evidence instead of each one inventing its own ad hoc detector.

## Plan
- [x] Add a shared framework `SignalQuality` snapshot/state with prepare/reset/update lifecycle matching the existing analysis layers.
- [x] Wire `ProcessorBase` to maintain and expose the shared signal-quality snapshot for all products.
- [x] Refactor `VX Rebalance` to consume the shared framework signal-quality detector instead of its local copy.
- [x] Rebuild `VXRebalancePlugin` and verify the framework refactor compiles/stages cleanly.
- [x] Record the kept framework outcome here.

## Review
- Added a shared framework signal-quality analysis layer in [VxSuiteSignalQuality.h](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/framework/VxSuiteSignalQuality.h) and [VxSuiteSignalQuality.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/framework/VxSuiteSignalQuality.cpp). It computes reusable `monoScore`, `compressionScore`, `tiltScore`, and `separationConfidence` snapshots with the same prepare/reset/update lifecycle pattern as the existing voice-analysis layers.
- Wired `ProcessorBase` to own and update that state in [VxSuiteProcessorBase.h](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/framework/VxSuiteProcessorBase.h) and [VxSuiteProcessorBase.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/framework/VxSuiteProcessorBase.cpp), so every product can now read `getSignalQualitySnapshot()` from the shared framework path.
- Updated the framework build in [VxSuitePlugin.cmake](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/cmake/VxSuitePlugin.cmake) to compile the new shared analysis layer.
- Refactored `VX Rebalance` to consume the shared framework detector instead of maintaining a duplicate local one in [VxRebalanceProcessor.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/rebalance/VxRebalanceProcessor.cpp), [VxRebalanceDsp.h](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/rebalance/dsp/VxRebalanceDsp.h), and [VxRebalanceDsp.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/rebalance/dsp/VxRebalanceDsp.cpp).
- Added the first broader suite consumer in [VxCleanupProcessor.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/cleanup/VxCleanupProcessor.cpp). `Cleanup` now uses `SignalQuality` to back off its more confident corrective decisions on mono, crushed, or low-tilted material rather than over-reading bad input.
- Documented the shared `SignalQuality` contract and usage pattern in the framework docs at [README.md](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/framework/README.md).
- Verification:
  - `cmake --build build --target VXCleanupPlugin VXRebalancePlugin -j4`
- Kept architectural conclusion: shared signal-quality analysis belongs in the framework, but product-specific responses to that analysis still belong in the individual products.

# Shared SignalQuality rollout continuation — 2026-03-24

## Problem
The framework now owns `SignalQuality`, and `Cleanup` plus `Rebalance` consume it, but the rollout is still narrow. We should extend it into the next most natural `ProcessorBase` product so the shared detector starts paying off as a suite-level input-trust layer rather than staying a one-feature experiment. `Leveler` is the best next fit because it already makes confidence-sensitive decisions about how firmly to ride programme material.

## Plan
- [x] Review `VX Leveler`'s current processor/DSP seam and identify the smallest clean place to pass `SignalQuality` through.
- [x] Use `SignalQuality` in `Mix Leveler` to ease aggressive ride, spike, and restore decisions on mono, crushed, or low-confidence material without changing the vocal engine contract.
- [x] Expand the framework `README.md` with suite-wide guidance on where `SignalQuality` belongs and how products should consume it.
- [x] Build the affected target(s) and record the kept outcome plus any deferred rollout notes for `Analyser`.

## Review
- Extended [VxLevelerDsp.h](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/leveler/dsp/VxLevelerDsp.h) and [VxLevelerProcessor.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/leveler/VxLevelerProcessor.cpp) so `VX Leveler` now consumes the shared framework `SignalQuality` snapshot directly through its DSP params instead of inventing another local detector.
- Kept the rollout conservative in [VxLevelerDsp.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/leveler/dsp/VxLevelerDsp.cpp): the shared snapshot only modulates `Mix Leveler` trust-sensitive decisions. Lower confidence now widens the ride deadband, softens ride depth, scales normalize/restore confidence, slightly lowers compressed-material spike thresholds, and eases brightness taming on low-tilt material. `Vocal Rider` behaviour was left unchanged on purpose.
- Expanded the shared framework guidance in [README.md](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/framework/README.md) so future products treat `SignalQuality` as an input-trust layer and keep product-specific response logic local.
- Deferred direct `Analyser` adoption for now. It is not a `ProcessorBase` product, so the right next step there is an explicit bridge or UI-only exposure, not a second copy of the detector.
- Verification:
  - `cmake --build build --target VXLevelerPlugin -j4`
  - `cmake --build build --target VXLevelerMeasure -j4`
  - `./build/VXLevelerMeasure /Users/andrzejmarczewski/Downloads/loud_quiet.wav /Users/andrzejmarczewski/Downloads/loud_quiet_signal_quality_leveler.wav general 1.0 1.0 smart`
- Measured sanity check on `/Users/andrzejmarczewski/Downloads/loud_quiet.wav` after the rollout:
  - spread `12.9225 -> 12.3683 dB`
  - RMS `-23.2013 -> -23.7865 dBFS`
  - peak `-4.5736 -> -3.5545 dBFS`
- Kept conclusion: `Leveler` is now the most comfortable second framework consumer after `Cleanup`, and `SignalQuality` is starting to act like a real suite-wide trust layer rather than a `Rebalance`-only experiment.

# Studio Analyser SignalQuality exposure — 2026-03-24

## Problem
`SignalQuality` is now useful across the suite, but the one product that should make it visible to users still does not surface it. `Studio Analyser` is not a `ProcessorBase` product, so the clean implementation is to bridge the shared framework detector into the analyser processor and expose compact recording-condition hints in the UI without cloning detector logic.

## Plan
- [x] Add shared `SignalQualityState` ownership plus an atomic snapshot bridge to `Studio Analyser`'s processor.
- [x] Surface concise recording-condition hints and confidence in the analyser header without adding a heavy new panel.
- [x] Build `VXStudioAnalyserPlugin` and record the kept outcome.

## Review
- Bridged the shared framework detector into [VXStudioAnalyserProcessor.h](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/analyser/VXStudioAnalyserProcessor.h) and [VXStudioAnalyserProcessor.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/analyser/VXStudioAnalyserProcessor.cpp) using `SignalQualityState` plus an atomic snapshot handoff. That keeps the analyser on the same suite-wide detector without requiring `ProcessorBase` or duplicating DSP logic.
- Exposed the result in [VXStudioAnalyserEditor.h](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/analyser/VXStudioAnalyserEditor.h) and [VXStudioAnalyserEditor.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/analyser/VXStudioAnalyserEditor.cpp) as a compact header line:
  - stereo condition (`Near mono` / `Stereo-limited` / `Stereo-open`)
  - dynamics condition (`AGC / crushed` to `Natural dynamics`)
  - tonal condition (`Low-heavy / lo-fi` to `Balanced spectrum`)
  - overall DSP trust percentage from `separationConfidence`
- Kept the UI efficient on purpose: no new diagnostics panel, no duplicated detector readout, just one readable recording-condition sentence that updates with the existing analyser timer.
- Verification:
  - `cmake --build build --target VXStudioAnalyserPlugin -j4`
- Kept conclusion: `Studio Analyser` now acts as the suite’s visible recording-condition surface, while the shared framework still owns the actual `SignalQuality` detection.

# VX Rebalance neutral attenuation + weak range fix — 2026-03-24

## Problem
`VX Rebalance` is still misbehaving in two fundamental ways: with `Strength` at `100%` and all source sliders at `0 dB`, it audibly attenuates the signal instead of behaving like neutral bypass, and the current boost/bury law is too weak to make the product feel meaningful even before the remaining source-ownership issues. We need to fix the neutral contract first, then strengthen the source gain law so the sliders produce visible and audible movement.

## Plan
- [x] Inspect the neutral signal path and current rebalance gain law to identify why unity settings still attenuate.

# Stem profiler phone-vs-pro comparison — 2026-03-24

## Problem
We now have two split-stem datasets for the same song family: a mobile-phone capture plus stem split, and a cleaner released-track stem split. We need to compare them with the same offline profiler so `VX Rebalance` tuning is guided by what survives across both recording conditions rather than overfitting to the phone example.

## Plan
- [x] Inspect the provided pro stem folder and confirm it matches the profiler's expected stem layout.
- [x] Run the stem profiler on the pro dataset and collect report/plot outputs.
- [x] Compare phone vs pro findings, then write the stable source-region guidance and tuning implications here.

## Review
- Extended [stem_profile.py](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/tools/stem_profile.py) so it no longer hard-codes the phone dataset filenames. It now finds `*_original`, `*_vocals`, `*_drums`, `*_bass`, `*_guitar`, `*_piano`, and `*_other` with common audio extensions, which makes it reusable for future R&D packs without renaming assets.
- Ran the same profiler on the pro release split at [stem-profile-brightside-pro/report.md](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/tasks/reports/stem-profile-brightside-pro/report.md) and generated comparison notes at [stem-profile-brightside-compare.md](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/tasks/reports/stem-profile-brightside-compare.md).
- Stable regions across both the phone capture split and the pro release split are much narrower than the current heuristic `Rebalance` model assumed:
  - `Vocals`: only `1.4 kHz - 2.9 kHz` survives cleanly across both sets as a strong static prior.
  - `Drums`: only the high cymbal/hat bands above `6.3 kHz` survive cleanly across both sets as a strong static prior.
- The low end is condition-dependent:
  - the phone split makes `Bass` look strong below `136 Hz`,
  - the pro split pushes more low ownership into kick-led `Drums` below about `93 Hz` and leaves `Bass` strongest at `136 Hz - 200 Hz`.
- `Guitar`, `Piano`, and `Other` still do not produce robust static safe regions across both datasets, which supports keeping those lanes residual/confidence-gated rather than frequency-led in the current heuristic product.
- Practical tuning implication for `VX Rebalance`: treat roughly `200 Hz - 430 Hz` as a low-confidence semantic band, trust `Vocals` mainly in the `1.4-2.9 kHz` presence zone, trust `Drums` most strongly in the upper transient/cymbal zone, and keep low-end ownership adaptive rather than hard-banded.

# VX Rebalance recording-type mode profiles — 2026-03-24

## Problem
`VX Rebalance` still relies on one inline heuristic profile even though the user has now defined a clearer V1 contract: `Studio`, `Live`, and `Phone / Rough` should be explicit user-facing recording types with data-driven source band profiles, confidence behaviour, and safety limits. We need to implement that cleanly through the VX framework instead of burying another pile of mode-specific constants inside `computeMasks()`.

## Plan
- [x] Review the current `VX Rebalance` processor/editor seam and pick the cleanest framework-native place for a `Recording Type` selector.
- [x] Add data-driven recording-type profile structs plus mode selection plumbing in the processor and DSP.
- [x] Refit mask smoothing, confidence behaviour, and source gain limits to read from those profiles instead of hard-coded single-mode constants.
- [x] Build the affected targets and verify the new mode path compiles and behaves sanely.

## Review
- Reworked [VxRebalanceDsp.h](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/rebalance/dsp/VxRebalanceDsp.h) and [VxRebalanceDsp.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/rebalance/dsp/VxRebalanceDsp.cpp) so recording-type profiles can now express multiple weighted frequency regions per source instead of only one contiguous `strong / medium / light` band. That fixes the earlier structural mismatch where `Drums`, `Vocals`, and `Guitars` had to be flattened into overly broad fake bands.
- Kept the user-requested `±24 dB` slider range in [VxRebalanceProcessor.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/rebalance/VxRebalanceProcessor.cpp), but restored mode-specific safety in the DSP:
  - `Studio` now applies bounded composite moves around `+6 / -9 dB`
  - `Live` around `+4.5 / -7 dB`
  - `Phone / Rough` around `+3 / -5 dB`
  - while the wide slider throw still acts as the user request into the engine.
- Fixed the confidence-law bug in [VxRebalanceDsp.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/rebalance/dsp/VxRebalanceDsp.cpp): `confidenceFloor` now behaves as a threshold below which confident moves collapse more quickly, instead of acting like a permanent ceiling that weakened every user move even at high confidence.
- Removed the remaining hop-time FIFO/OLA shifts from the realtime DSP path in [VxRebalanceDsp.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/rebalance/dsp/VxRebalanceDsp.cpp). Input history is now read from a circular buffer, and overlap-add accumulation advances through a ring instead of `std::move`-shifting whole buffers every hop.
- Repaired [VXRebalanceMeasure.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/tests/VXRebalanceMeasure.cpp) so it matches the actual product contract again:
  - generic stem-file discovery for phone and pro naming styles
  - `±24 dB` normalization instead of stale `±12 dB`
  - optional `recording_type` argument so the harness can exercise `Studio`, `Live`, or `Phone / Rough`
- Verification:
  - `cmake --build build --target VXRebalancePlugin VXRebalanceMeasure -j4`
  - `./build/VXRebalanceMeasure /Users/andrzejmarczewski/Downloads/brightside_stems 6.0 studio`
- Current measured state after the structural fixes:
  - neutral remains exact: `rms=0 peak=0`
  - the harness is trustworthy again
  - but source selectivity still needs musical retuning, especially `Bass`, `Guitar`, and `Other`, which remain too correlated with non-target material on the phone split

### Follow-up: slider authority restore
- After the structural fixes, the user correctly reported that the bands still felt weak and that the recording-type modes were not audibly distinct enough.
- Kept fix in [VxRebalanceDsp.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/rebalance/dsp/VxRebalanceDsp.cpp):
  - increased effective mask contribution,
  - strengthened per-source dB weighting,
  - relaxed the over-diluting gain focus law,
  - widened the practical difference between `Studio`, `Live`, and `Phone / Rough` safety ceilings.
- Verification:
  - `cmake --build build --target VXRebalancePlugin VXRebalanceMeasure -j4`
  - `./build/VXRebalanceMeasure /Users/andrzejmarczewski/Downloads/brightside_stems 24.0 studio`
- Measured result:
  - the plugin now has real authority again; for example `Vocals @ +24 dB` now gives about `+9.75 dB` on the isolated vocal stem instead of the earlier ~`+2 dB` range
  - `Drums @ +24 dB` now gives about `+8.78 dB` on the isolated drum stem
- Honest conclusion:
  - this pass fixed the “almost no effect” problem,
  - but it also makes the remaining selectivity leakage much more obvious, especially for `Bass`, `Guitar`, and `Other`
  - the current build is stronger and more honest, but still not semantically clean enough to call the heuristic path finished

---
# VX Rebalance v2 ML spec — 2026-03-24

## Problem
The heuristic `VX Rebalance` path has taught us something useful but clear: users want believable control of `Vocals`, `Drums`, `Bass`, and `Guitar`, and pure heuristic STFT ownership is not selective enough on real mastered music to meet that promise reliably. We need a proper v2 specification for an ML-guided rebalance engine that stays aligned with VX Suite’s realtime/product constraints instead of drifting toward a heavyweight offline stem-separation product.

## Plan
- [x] Decide the core v2 product direction: ML-guided source ownership masks, not full stem export as the primary contract.
- [x] Write a framework-native `VX Rebalance` v2 specification covering model role, DSP role, mode behaviour, latency/size constraints, and verification.
- [x] Record the completed spec here and point to the new doc file.

## Review
- Wrote the v2 ML-based spec at [VX_REBALANCE_V2_SPEC.md](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/docs/VX_REBALANCE_V2_SPEC.md).
- The kept direction is explicitly **ML-guided rebalance**, not “live Demucs in a plugin” and not “stem export”.
- The central design choice is a five-head soft-mask model for:
  - `Vocals`
  - `Drums`
  - `Bass`
  - `Guitar`
  - `Other`
- `Guitar` is specified as a **direct-plus-negative-space** lane:
  - the model predicts direct guitar evidence,
  - stronger lanes like `Vocals`, `Drums`, and `Bass` claim their confident bins first,
  - `Guitar` then combines its own direct evidence with the remaining residual opportunity,
  - `Other` absorbs the unresolved remainder.
- The spec keeps VX Suite’s product identity intact:
  - the plugin remains a rebalance processor,
  - ML only estimates ownership/masks,
  - product DSP still handles bounded gain, smoothing, low-end protection, transient protection, and recording-type behaviour.
- The spec also sets practical v2 constraints:
  - no multi-GB offline-quality separator as the first shipping target,
  - prefer a small/medium mask-prediction model,
  - keep `Studio / Live / Phone / Rough` as behavioural priors around the model, not as separate models unless validation proves that necessary.
- After review, tightened the spec around the real implementation risks:
  - added an explicit **Training Data** section and named the actual blocker: guitar-specific labelled multitrack data
  - committed the first cross-platform runtime direction to **ONNX Runtime**
  - changed the latency story from vague “maybe realtime / maybe high precision” language to an explicit **near-realtime / high-precision default** with lower-latency modes treated as secondary
  - added a hard shipping gate: if guitar-labelled data is not strong enough, the first ML shipping plan falls back to **4-head ML plus heuristic guitar shaping** instead of pretending a weak accompaniment model is a real guitar detector
- [x] Add an exact dry bypass path for effectively neutral rebalance settings so `0 dB` really means no change.
- [x] Strengthen the source gain law so masked boosts/cuts are materially stronger without breaking realtime safety.
- [x] Rebuild `VXRebalancePlugin`, run focused verification, and record the kept outcome.

## Review
- Added an explicit neutral bypass in [VxRebalanceProcessor.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/rebalance/VxRebalanceProcessor.cpp): if `Strength` is effectively zero or all five source sliders are effectively centred at `0 dB`, the processor now returns the dry signal instead of sending audio through the heuristic STFT path. That directly fixes the user-facing contract that neutral settings must not attenuate.
- Strengthened the source gain law in [VxRebalanceDsp.h](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/rebalance/dsp/VxRebalanceDsp.h) and [VxRebalanceDsp.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/rebalance/dsp/VxRebalanceDsp.cpp):
  - per-source range widened from `±6 dB` to `±12 dB`
  - per-bin rebalance now combines masked source moves in the `dB` domain instead of as a diluted linear-gain average
  - final composite gain remains bounded to a safe `±12 dB`
- Kept the change minimal and realtime-safe: no new allocations, no new framework fork, and no extra detector layer. This pass was about fixing the neutral/buried/boost-too-small contract first, not about claiming source ownership is solved.
- Verification:
  - `cmake --build build --target VXRebalancePlugin -j4`
- Important remaining gap:
  - there is still no dedicated `Rebalance` measurement harness in this repo, so I verified build/staging and fixed the neutral contract in code, but I have not yet run a product-specific selectivity or loudness-retention render test for this version.

# VX Rebalance source-mapping correction — 2026-03-24

## Problem
The latest `VX Rebalance` build still does not map its controls credibly. The analyser screenshots with each lane at `+6 dB` show several controls acting like broad overlapping tone shelves instead of distinct source-family moves, which matches the listening report. We need a focused ownership pass that makes the five lanes more orthogonal before we spend any more time on cosmetic gain-law tweaks.

## Plan
- [x] Inspect the current mask windows against the screenshot behaviour and the provided `brightside.wav` example.
- [x] Retune the ownership logic so `Bass`, `Drums`, `Vocals`, `Guitar`, and `Other` claim more distinct regions and stop behaving like near-duplicates.
- [x] Keep the new neutral/unity contract intact while strengthening source specificity.
- [x] Rebuild `VXRebalancePlugin` and record the kept outcome plus any remaining ML boundary.

## Review
- Added a dedicated stem-aware measurement harness in [VXRebalanceMeasure.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/tests/VXRebalanceMeasure.cpp) and wired it into [CMakeLists.txt](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/CMakeLists.txt). It can now render `VX Rebalance` against a provided split-stem set and report both mix-delta correlation and isolated-stem gain per lane.
- The first meaningful finding from the `brightside` stems was not just “bad heuristics” but a real wiring bug: the UI control order (`Vocals`, `Drums`, `Bass`, `Guitar`, `Other`) did not match the internal raw mask order. I fixed that in [VxRebalanceDsp.h](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/rebalance/dsp/VxRebalanceDsp.h) and [VxRebalanceDsp.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/rebalance/dsp/VxRebalanceDsp.cpp) by introducing explicit source indices.
- I also fixed the neutral path properly in [VxRebalanceProcessor.h](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/rebalance/VxRebalanceProcessor.h) and [VxRebalanceProcessor.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/rebalance/VxRebalanceProcessor.cpp): neutral now stays both dry and latency-aligned, so the measurement harness reports `Neutral diff rms=0 peak=0` instead of a false mismatch caused by bypass timing.
- Current stem-driven state on `/Users/andrzejmarczewski/Downloads/brightside_stems` at `Vocals +6 dB`:
  - mix-delta correlation is strongest with the vocal stem (`0.7798`) and near zero for bass/drums
  - isolated stem gains show `Vocals` now lifts the vocal stem by about `+1.81 dB`, bass by effectively `0 dB`, drums by effectively `0 dB`, guitar by about `+0.29 dB`, and `Other` by about `+1.28 dB`
- The same stem run also shows the remaining weak spots clearly:
  - `Drums` is still too weak
  - `Bass` still over-lifts drum/guitar stems more than desired
  - `Guitar` is still effectively non-functional
- Verification:
  - `cmake -S . -B build`
  - `cmake --build build --target VXRebalanceMeasure VXRebalancePlugin -j4`
  - `./build/VXRebalanceMeasure /Users/andrzejmarczewski/Downloads/brightside_stems 6.0`
- Honest conclusion:
  - `Vocals` is now materially closer to the intended family than it was when you first showed the screenshots.
  - `Bass`, `Drums`, and especially `Guitar` still need another measurement-driven retune pass.
  - The new harness means future retunes can now be scored directly against your split stems instead of by ear or analyser screenshots alone.

# Stem-profiler R&D tool — 2026-03-24

## Problem
We need an offline analysis tool that can profile split stems from phone footage and cleaner recordings so `VX Rebalance` tuning stops relying on ad hoc intuition. The tool should measure spectral occupancy, overlap/conflict regions, stereo tendencies, and coarse recording-condition cues, then produce reports that help us decide which bands are safe heuristically and which ones are too entangled to trust.

## Plan
- [x] Implement a Python stem-profiler tool under `tools/` that reads an original mix plus split stems and measures per-stem occupancy, overlap, and phone-relevant quality cues.
- [x] Have the tool emit both machine-friendly data and a readable Markdown report with suggested “safe” and “high-conflict” frequency regions.
- [x] Run it on the provided `brightside` stem set and save the first report/artifacts under `tasks/reports/`.
- [x] Summarize how the findings should change `VX Rebalance` heuristics and where they point toward ML.

## Review
- Added an offline Python profiler at [stem_profile.py](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/tools/stem_profile.py). It loads an original mix plus split stems, computes STFT-based mean spectral occupancy, band dominance, conflict/entropy, and framework-style recording-condition cues (`mono`, `compression`, `tilt`, `separation confidence`).
- The tool emits a readable Markdown report plus plots under [tasks/reports/stem-profile-brightside/report.md](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/tasks/reports/stem-profile-brightside/report.md), [tasks/reports/stem-profile-brightside/stem_profiles.png](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/tasks/reports/stem-profile-brightside/stem_profiles.png), and [tasks/reports/stem-profile-brightside/stem_conflict.png](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/tasks/reports/stem-profile-brightside/stem_conflict.png).
- First `brightside` findings that matter for `VX Rebalance`:
  - `Bass` has genuinely safe low regions around roughly `29–136 Hz`, with growing conflict by `136–200 Hz`
  - `Vocals` have the clearest safe heuristic zone here, roughly `431 Hz–4.3 kHz`
  - `Drums` are only strongly dominant in the upper cymbal/hat region (`6.3 kHz+`) on this split
  - `Guitar`, `Piano`, and `Other` have no strongly safe dominant regions on this phone-derived material, which is a strong signal that those lanes should either stay conservative or move toward ML-guided ownership
- Recording-condition summary for this split/original set:
  - `monoScore = 0.000`
  - `compressionScore = 0.306`
  - `tiltScore = 1.000`
  - `separationConfidence = 0.693`
- Practical heuristic implication:
  - the next `Rebalance` pass should trust `Bass` mainly below about `140 Hz`
  - trust `Vocals` mostly from about `430 Hz` upward, not in the broad low-mids
  - treat `200–430 Hz` as a high-conflict region
  - stop expecting `Guitar` to have a robust static heuristic lane on material like this
- Verification:
  - `python3 tools/stem_profile.py /Users/andrzejmarczewski/Downloads/brightside_stems --out-dir tasks/reports/stem-profile-brightside`

# VXLeveler in-host Offline analyse + programme restore — 2026-03-24

# VX Suite knob semantics + gain staging audit — 2026-03-24

## Problem
Several VX products feel quieter than they should in host use, especially the ones with explicit `Gain` knobs. The current knob presentation also exposes raw `0.0–1.0` values instead of consistent `0–100%` user-facing semantics. We need to audit the shared parameter path and the finishing-stage gain laws, fix the weak/attenuating behaviour without broad DSP churn, and verify the result with the existing batch-audio harness rather than by inspection alone.

## Plan
- [x] Audit the shared framework parameter formatting and the products with explicit `Gain` controls to identify the smallest safe fix.
- [x] Change user-facing float knob display/parse semantics to `0–100%` in the shared framework path while keeping stable normalized automation underneath.
- [x] Retune the finishing-product gain/output path so `Gain` has a more meaningful range and normal use does not read as unnecessary attenuation.
- [x] Build the affected targets and run focused batch/regression verification for loudness retention, safety, and parameter compatibility.
- [x] Record the review outcome here.

## Review
- Shared knob semantics now come from the framework in [VxSuiteParameters.h](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/framework/VxSuiteParameters.h): all standard float controls still store normalized `0..1` internally for stable automation/state, but the user-facing text now reads/parses as `0–100%` instead of raw decimals.
- `VXFinish` and `VXOptoComp` now map their `Gain` control across a wider `-12 dB .. +12 dB` range instead of the previous `±6 dB`, which makes the knob materially more useful when a user needs real recovery lift.
- I removed the redundant product-local `OutputTrimmer` from [VxFinishProcessor.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/finish/VxFinishProcessor.cpp) and [VxOptoCompProcessor.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/OptoComp/VxOptoCompProcessor.cpp), leaving the framework safety trimmer as the final emergency guard instead of silently double-trimming after the internal limiter.
- I also changed the shared finish/opto DSP in [VxFinishDsp.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/finish/dsp/VxFinishDsp.cpp) so auto makeup is no longer capped at a tiny fixed amount; it now follows both knob intent and recent measured gain reduction within a bounded range.
- Verification:
  - `cmake --build build --target VXFinishPlugin VXOptoCompPlugin VXSuiteBatchAudioCheck VXSuitePluginRegressionTests -j4`
  - `./build/VXSuiteBatchAudioCheck data/voice_corpus/wav tasks/reports/VX_SUITE_BATCH_AUDIO_CHECK_2026-03-24_GAIN_AUDIT.md --products=cleanup,denoiser,deverb,finish,leveler,optocomp,proximity,subtract,tone`
  - `./build/VXSuiteBatchAudioCheck data/voice_corpus/wav tasks/reports/VX_SUITE_BATCH_AUDIO_CHECK_2026-03-24_FINISH_GAIN_FOLLOWUP.md --products=finish,optocomp`
  - `./build/VXSuiteBatchAudioCheck data/voice_corpus/wav tasks/reports/VX_SUITE_BATCH_AUDIO_CHECK_2026-03-24_FINISH_GAIN_100_CENTER.md --products=finish,optocomp`
- Kept outcome:
  - The `0–100%` knob-display issue is fixed suite-wide through the shared framework path.
  - `Finish`/`OptoComp` gained a meaningfully stronger gain knob and less hidden trimming.
  - `Gain` for those products now follows unity-centered percent semantics: `50%` at the left edge, `100%` at center, `150%` at the right edge.
  - The focused follow-up batch report still shows those two products running quieter than ideal at neutral `Gain=50%` on the speech corpus, but markedly less attenuated than the first post-range-change pass. This means the “gain knob too weak” complaint is improved, but there is still a separate product-voicing decision left if we want neutral settings to preserve more absolute loudness by default.
  - The dedicated `100%`-center verification report stayed effectively unchanged versus the previous focused run, which is the right result: it confirms the neutral-gain point is now truly unity, and that the remaining quietness comes from the compressor/finish voicing rather than from a mis-scaled gain control.
  - `./build/VXSuitePluginRegressionTests` still fails on an existing subtract steady-state allocation check (`Audio-thread allocation detected during steady-state subtract processing: count=1125`), which does not point back to the files changed in this pass.

## Problem
`VXLeveler` now has a strong `Offline` analysis path in the harness, but the DAW plugin still cannot actually build that map in-host. `Mix Leveler` also still trends too quiet overall, which makes it feel more like controlled attenuation than a true intelligent rider. We need a real `Analyze` workflow inside the plugin and a bounded programme-level restore stage so the processed track does not end up quieter than the original unless headroom safety requires it.

## Plan
- [x] Reuse the shared framework action-button path to expose `Analyze` for `VXLeveler` only when `Mix Leveler` + `Offline` analysis are selected.
- [x] Add an in-host offline-analysis capture path that records fixed-size loudness blocks during playback and converts them into an offline target map without allocating in `process()`.
- [x] Add a programme-level restore stage to `VXLeveler` so `Mix` avoids ending up globally quieter than the source unless peak/headroom safety forces it.
- [x] Rebuild `VXLeveler`, rerun `VXLevelerMeasure` and `VXSuitePluginRegressionTests`, and keep only the verified build.
- [x] Record the review result here and add the new lesson if the user’s “too quiet overall” correction changes our standard.

## Review
- Reused the shared framework `learnButton` path as an in-host `Analyze` action for `VXLeveler` instead of creating a custom editor fork. The processor now only shows that UI when `Mix Leveler` + `Offline` are selected, and the shared editor presents analysis-specific copy rather than the old noise-learn text.
- Added a fixed-block offline capture path in [VxLevelerProcessor.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/leveler/VxLevelerProcessor.cpp). While `Analyze` is armed, the processor accumulates fixed-size dry-program loudness blocks with preallocated storage, then converts them into an offline target map through the new `OfflineAnalyzer::analyse(blockDb, ...)` overload in [VxLevelerOfflineAnalyzer.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/leveler/dsp/VxLevelerOfflineAnalyzer.cpp).
- Added a bounded programme-level restore stage in [VxLevelerDsp.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/leveler/dsp/VxLevelerDsp.cpp) for `Mix Leveler` only. It tracks long-horizon dry vs wet programme loudness and restores only enough gain to keep the processed result from ending up meaningfully quieter than the source, while still respecting spike activity and peak headroom.
- The first restore attempt also touched `Vocal Rider`, but that weakened the hot-mix regression by undoing too much of the actual level-improvement work, so I backed restore back out of `Vocal Rider` and kept it only on the `Mix` path.
- Follow-up UI cleanup: the analysis meter now explicitly reports `Coverage` while capture is running and reserves `Confidence` for the locked result after stop. Once capture has reached the nominal coverage target, the button now reads `Lock Analysis` instead of implying that analysis should already be finished.
- Follow-up confidence fix: locked offline confidence now starts from a practical “usable map” floor instead of reading as ~`50%` after a normal full-track capture. The score now weights coverage most heavily, adds a smaller duration/range contribution, and produces results that better match what the feature is actually telling the user.
- Follow-up attenuation check: the user-supplied DAW exports under `/Users/andrzejmarczewski/Downloads/noise test/.../mix test/` were confirmed to come from an older bad build. Their RMS values are roughly `12–15 dB` below the dry source (`offline_old -36.78 dBFS`, `realtime_old -35.49 dBFS`, `smart_old -36.45 dBFS`, `vocal_old -38.25 dBFS` versus dry `-23.20 dBFS`). Fresh renders from the current build are much closer to the source and no longer show that extreme attenuation:
  - `Realtime`: `-21.84 dBFS`
  - `Smart`: `-22.81 dBFS`
  - `Offline`: `-24.44 dBFS`
- Final verified result on `/Users/andrzejmarczewski/Downloads/loud_quiet.wav`:
  - `Mix Leveler` `Smart Realtime`: spread `12.9225 -> 12.7545 dB`, RMS `-23.2013 -> -22.8142 dBFS`, peak `-4.5736 -> -1.4367 dBFS`
  - `Vocal Rider`: spread `12.9225 -> 11.8820 dB`, RMS `-23.2013 -> -24.6122 dBFS`, peak `-4.5736 -> -5.6580 dBFS`
- Verification:
  - `cmake --build build --target VXLevelerPlugin VXLevelerMeasure VXSuitePluginRegressionTests -j4`
  - `./build/VXLevelerMeasure /Users/andrzejmarczewski/Downloads/loud_quiet.wav /Users/andrzejmarczewski/Downloads/loud_quiet_mix_restore.wav general 1.0 1.0 smart`
  - `./build/VXLevelerMeasure /Users/andrzejmarczewski/Downloads/loud_quiet.wav /Users/andrzejmarczewski/Downloads/loud_quiet_voice_restore.wav voice 1.0 1.0`
  - `./build/VXSuitePluginRegressionTests`

# VxLeveler state-machine refactor + mode split — 2026-03-20

## Problem
The current leveller behavior still reads mostly as broad attenuation instead of intentional adaptive levelling, and the user has now provided two missing layers: explicit runtime states for speech-aware handling and a proper top-level split between `Voice` and `General` modes. The product also needs to be renamed to `VxLeveler`, and the implementation must be verified against the reported pumping/non-adaptive behavior rather than assumed correct from earlier synthetic checks.

## Plan
- [x] Refactor the leveller DSP around explicit mix states and a separate decision block, keeping the existing band split and stereo-aware handling.
- [x] Split runtime behaviour into separate `Voice` and `General` decision engines over shared DSP primitives.
- [x] Make the `Level` control more assertive by driving relative levelling decisions instead of a mostly fixed target window.
- [x] Rename the product/build-facing identity from `VX Perform` to `VxLeveler` without disturbing unrelated VX Suite products.
- [x] Rebuild the renamed plugin and regression target, then run focused verification on the levelling regression.
- [x] Record the review result and the new lesson from this correction.

## Review
- `VxLeveler` now uses the shared framework mode selector with explicit `Voice` and `General` modes. `Voice` keeps the speech-aware state machine and stereo-aware side taming, while `General` uses a simpler anchor-based leveller plus non-semantic transient containment.
- The DSP now commits to explicit `MixState` transitions in voice mode and maps them through a dedicated decision layer before applying shared lookahead/band/tame primitives. The level ride is now relative to a moving anchor instead of a fixed target window.
- The product, plugin target, staged VST3 bundle, processor class, and regression coverage were renamed from `VXPerform` / `Perform` to `VXLeveler` / `Leveler`.
- Verification passed with `cmake -S . -B build`, `cmake --build build --target VXLevelerPlugin VXSuitePluginRegressionTests -j4`, and `./build/VXSuitePluginRegressionTests`.

# VxLeveler voice-mode file tuning — 2026-03-20

## Problem
`VxLeveler` now has separate `Voice` and `General` modes, but `Voice` mode still needs tuning against the user's actual `loud_quiet.wav` example rather than only the synthetic regression signal. We need a small file-based render/measure loop so the tuning can be based on the real capture and not inferred from screenshots alone.

## Plan
- [x] Add a tiny `VxLeveler` file render/measure harness that can read a WAV, process it in `Voice` or `General` mode, and print before/after level-spread metrics.
- [x] Run the harness on `/Users/andrzejmarczewski/Downloads/loud_quiet.wav` in `Voice` mode and inspect the current result.
- [x] Tune `Voice` mode against that real-file result, rebuild, and rerun the measurement.
- [x] Record the review result in this task log.

## Review
- Added `tests/VXLevelerMeasure.cpp` plus the `VXLevelerMeasure` CMake target so `VxLeveler` can be run directly on real WAV files with a printed before/after level-spread metric and a rendered output file.
- On `/Users/andrzejmarczewski/Downloads/loud_quiet.wav`, the initial full-strength `Voice` mode run measured `12.9225 dB` input spread and `12.1673 dB` output spread, which confirmed the mode was helping but not strongly enough.
- After tuning `Voice` mode to enter buried/guitar-dominant states earlier and intervene more aggressively, the same file now measures `11.8955 dB` output spread at full `Voice` settings, and the rendered output was written to `/Users/andrzejmarczewski/Downloads/loud_quiet_voice_test.wav`.
- A follow-up pass fixed the trace view so short live history no longer stretches across the entire zoom window and the trace no longer disappears just because the latest snapshot is marked silent; the view now anchors real history against the selected zoom window instead.
- I also tested a steeper top-end dial remap, but the first version made the real-file spread worse, so I backed that out and kept the stronger measured `Voice` behavior instead of leaving a “more active but less effective” mapping in place.
- Verified with `cmake --build build --target VXLevelerMeasure VXLevelerPlugin VXSuitePluginRegressionTests -j4`, `./build/VXLevelerMeasure /Users/andrzejmarczewski/Downloads/loud_quiet.wav /Users/andrzejmarczewski/Downloads/loud_quiet_voice_test.wav voice 1.0 1.0`, and `./build/VXSuitePluginRegressionTests`.

# VxLeveler intervention redesign — 2026-03-20

## Problem
`VxLeveler` still behaves too much like a smooth average-driven rider and not enough like an intelligent intervention tool. The user wants the high end of the controls to step in decisively, and the previous attempt to get that through simple top-end remapping made the real-file result worse. The fix needs to be structural: add a true intervention path rather than biasing the same smooth law harder.

## Plan
- [x] Add a dedicated intervention path in `VxLeveler` that can apply stronger downward control and band-targeted recovery at high settings.
- [x] Tune that path against `/Users/andrzejmarczewski/Downloads/loud_quiet.wav`, especially in `Voice` mode.
- [x] Rebuild, rerun the real-file measurement harness, and rerun the regression suite.
- [x] Record the outcome and lesson from the redesign pass.

## Review
- `VxLeveler` now has a separate intervention lane in [VxLevelerDsp.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/leveler/dsp/VxLevelerDsp.cpp) alongside the existing smooth rider. At high settings it can clamp loud “wrong winner” moments more decisively and add extra speech-band recovery only when the detector and state say it is warranted.
- The trace fix in [VxSuiteLevelTraceView.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/framework/VxSuiteLevelTraceView.cpp) remains in place, so short live history is anchored correctly in the selected zoom window and no longer presents as if it is erasing itself.
- On `/Users/andrzejmarczewski/Downloads/loud_quiet.wav`, the verified measurements are now `12.9225 dB` dry to `11.8811 dB` wet in `Voice` mode and `12.9225 dB` dry to `12.8836 dB` wet in `General` mode.
- Verified with `cmake --build build --target VXLevelerMeasure VXLevelerPlugin VXSuitePluginRegressionTests -j4`, `./build/VXLevelerMeasure /Users/andrzejmarczewski/Downloads/loud_quiet.wav /Users/andrzejmarczewski/Downloads/loud_quiet_voice_test.wav voice 1.0 1.0`, `./build/VXLevelerMeasure /Users/andrzejmarczewski/Downloads/loud_quiet.wav /Users/andrzejmarczewski/Downloads/loud_quiet_general_test.wav general 1.0 1.0`, and `./build/VXSuitePluginRegressionTests`.

# VxLeveler radical non-ML redesign research — 2026-03-20

## Problem
The current `VxLeveler` still is not achieving the product goal strongly enough. We need to step back, research the best non-ML path for “make the track feel more even while keeping it dynamic and uncompressed,” and decide whether `Voice` mode should remain purely detector-driven or whether a voice-extraction-assisted branch like DeepFilter is justified only for that mode.

## Plan
- [ ] Research primary references for loudness-based adaptive leveling and dynamic preservation, plus inspect the local DeepFilter path for possible reuse.
- [ ] Choose the strongest framework-compatible non-ML redesign, with a clear recommendation on whether DeepFilter-assisted voice isolation should be avoided or used only for `Voice` mode.
- [ ] Implement the selected redesign in `VxLeveler`.
- [ ] Verify against the real file and the regression suite, then document the outcome.

# VxLeveler harder non-ML voice intervention — 2026-03-20

## Problem
The user chose the non-ML path, and the current `Voice` mode is still too graceful: on the real `loud_quiet.wav` file it mostly rides the whole track rather than stepping in hard enough when the instrument wins. We need a more asymmetrical `Voice`-only intervention branch that clamps loud masking moments decisively at the top of the dials while keeping `General` mode restrained.

## Plan
- [x] Refactor `Voice` mode so high `Level` / `Control` settings can trigger a stronger override lane than the smooth rider alone.
- [x] Tune the new intervention path against `/Users/andrzejmarczewski/Downloads/loud_quiet.wav` and keep only measured improvements.
- [x] Rebuild `VXLeveler`, rerun `VXLevelerMeasure` for `Voice` and `General`, and rerun `VXSuitePluginRegressionTests`.
- [x] Document the outcome here and add a lesson if this pass fixes a repeated mistake.

## Review
- `VxLeveler` `Voice` mode now keeps the existing smooth rider but adds a separate fast override lane in `Source/vxsuite/products/leveler/dsp/VxLevelerDsp.cpp`. At high settings that lane can pull overall gain down faster, add extra speech-band lift, and deepen transient taming only when the detector reports real masking pressure in `guitarDominant` or `voiceBuried` states.
- I tested more radical variants of the override law, including a deeper dB-style clamp and a peak-surge biased trigger, but those made the real-file result worse. The kept version is the strongest branch that still measured better on the user file.
- Verified on `/Users/andrzejmarczewski/Downloads/loud_quiet.wav` with `./build/VXLevelerMeasure`:
  - `Voice`: `12.9225 dB` dry to `11.8952 dB` wet
  - `General`: `12.9225 dB` dry to `12.8924 dB` wet
- Rebuilt and restaged with `cmake --build build --target VXLevelerMeasure VXLevelerPlugin VXSuitePluginRegressionTests -j4`, and the regression suite passed with `./build/VXSuitePluginRegressionTests`.

# VxLeveler Voice Rider + Mix Leveller redesign — 2026-03-21

## Problem
`VXLeveler` now has the right product split conceptually, but the underlying DSP is still too close to one adaptive leveller with heuristics on top. We need to rebuild it so `Voice` behaves like an intelligent vocal rider and `General` behaves like a broader `Mix` leveller, both using a clearer multi-timescale analysis model and a strict separation between slow riding and fast peak protection.

## Plan
- [x] Refactor the leveler detector/DSP around shared micro, meso, and macro analysis with deterministic block-rate outputs.
- [x] Implement `Voice` as a vocal-rider target engine and `Mix` as a general levelling target engine over the same slow-rider / fast-protector backend.
- [x] Update `VXLeveler` labels and status text so the user-facing mode contract is `Voice` / `Mix`.
- [x] Rebuild, rerun the real-file measure harness and regression suite, then document the outcome and lesson.

## Review
- I built a first full `Voice Rider / Mix Leveller` DSP rewrite with multi-timescale analysis and a clearer slow-rider / fast-protector split, then tuned it against `/Users/andrzejmarczewski/Downloads/loud_quiet.wav`.
- That redesign was directionally right architecturally, but it regressed the real-file result badly compared with the existing tuned core. The strongest tuned rewrite variant only reached `12.6244 dB` wet spread in `Voice`, which is materially worse than the existing verified `11.8952 dB`.
- Because of that, I reverted the DSP back to the strongest verified leveller core instead of leaving the product on a weaker “new architecture” build.
- I kept the user-facing contract update in `Source/vxsuite/products/leveler/VxLevelerProcessor.cpp`, so the mode selector and status text now read as `Voice` and `Mix` even though the underlying DSP remains the previously verified core for now.
- Current kept verification on `/Users/andrzejmarczewski/Downloads/loud_quiet.wav`:
  - `Voice`: `12.9225 dB` dry to `11.8952 dB` wet
  - `Mix` (parameter value still `general` internally): `12.9225 dB` dry to `12.8924 dB` wet
- Verified with `cmake --build build --target VXLevelerMeasure VXLevelerPlugin VXSuitePluginRegressionTests -j4`, `./build/VXLevelerMeasure /Users/andrzejmarczewski/Downloads/loud_quiet.wav /Users/andrzejmarczewski/Downloads/loud_quiet_voice_test.wav voice 1.0 1.0`, `./build/VXLevelerMeasure /Users/andrzejmarczewski/Downloads/loud_quiet.wav /Users/andrzejmarczewski/Downloads/loud_quiet_mix_test.wav general 1.0 1.0`, and `./build/VXSuitePluginRegressionTests`.

# VxLeveler Voice speech-band anchor assist — 2026-03-21

## Problem
The cleaner `Voice` / `Mix` product wording is in place, but the kept DSP still leans on full-band level envelopes for most of its riding decisions. On the user's real file, `Voice` mode still needs a more direct way to recognise when the speech band is lagging behind its recent baseline and push it forward without destabilising `Mix` mode.

## Plan
- [x] Add a `Voice`-only speech-band envelope and anchor inside the current verified DSP core.
- [x] Use that speech-band anchor to bias lift and/or override behaviour only when speech material falls behind its recent baseline.
- [x] Rebuild, rerun `VXLevelerMeasure` on `/Users/andrzejmarczewski/Downloads/loud_quiet.wav`, and rerun `VXSuitePluginRegressionTests`.
- [x] Document the outcome here and add a lesson if the assist proves useful.

## Review
- I added a `Voice`-only speech-band anchor/lift assist inside the current DSP core, then measured it directly on `/Users/andrzejmarczewski/Downloads/loud_quiet.wav`.
- The assist made `Voice` mode worse on the real file, moving the wet spread from the verified baseline `11.8952 dB` to `11.9158 dB`, so I reverted it instead of leaving the product on a regression.
- The kept verified state remains:
  - `Voice`: `12.9225 dB` dry to `11.8952 dB` wet
  - `Mix`: `12.9225 dB` dry to `12.8924 dB` wet
- Re-verified with `cmake --build build --target VXLevelerMeasure VXLevelerPlugin VXSuitePluginRegressionTests -j4`, `./build/VXLevelerMeasure /Users/andrzejmarczewski/Downloads/loud_quiet.wav /Users/andrzejmarczewski/Downloads/loud_quiet_voice_test.wav voice 1.0 1.0`, `./build/VXLevelerMeasure /Users/andrzejmarczewski/Downloads/loud_quiet.wav /Users/andrzejmarczewski/Downloads/loud_quiet_mix_test.wav general 1.0 1.0`, and `./build/VXSuitePluginRegressionTests`.

# VxLeveler separate Vocal Rider path — 2026-03-21

## Problem
We agreed that `Voice` should not just be a tuned variant of the same leveller. It needs a separate `Vocal Rider` path, while `Mix Leveler` stays on the current proven mixed-track levelling code. The previous attempts kept mutating the shared core; this pass needs a true split inside the DSP.

## Plan
- [ ] Keep the current `Mix Leveler` path intact and verified.
- [ ] Rename the user-facing mode labels/status text to `Vocal Rider` and `Mix Leveler`.
- [ ] Implement a separate `Voice`-only rider/protector path inside `VxLevelerDsp` and compare it directly against the current verified baseline on `/Users/andrzejmarczewski/Downloads/loud_quiet.wav`.
- [ ] Rebuild, rerun the real-file measure harness and regression suite, then document the kept outcome and lesson.

# VxLeveler section-based Vocal Rider target engine — 2026-03-21

## Problem
The previous separate `Vocal Rider` attempts were still too sample-envelope-driven and regressed both the real file and the hot-mix regression. The next step needs to be a real block/section target engine: decide at a slower phrase-like timescale, then let a smoother ride toward that target while `Mix Leveler` remains on the proven path.

## Plan
- [ ] Keep `Mix Leveler` on the current verified path.
- [ ] Add a separate `Vocal Rider` block/section target generator that updates per block from the detector snapshot instead of per-sample envelopes.
- [ ] Rebuild, compare the new `Vocal Rider` against the current `11.8952 dB` voice baseline on `/Users/andrzejmarczewski/Downloads/loud_quiet.wav`, and rerun `VXSuitePluginRegressionTests`.
- [ ] Keep only the stronger verified version and document the outcome plus lesson.

# VX Suite shared vocal context layer — 2026-03-21

## Problem
`Voice` mode quality should not depend on one plugin inventing its own heuristics. We need a shared, lightweight vocal-understanding layer in the framework so every VX product can make better `Voice`-mode decisions from the same signal evidence.

## Plan
- [x] Add a framework-level vocal context snapshot built from the existing voice analysis plus lightweight structural cues.
- [x] Wire `ProcessorBase` to maintain and expose that shared context to products.
- [x] Use `VXLeveler` as the first consumer while preserving the current verified baseline unless the new path proves better.
- [x] Verify against the real speech corpus, `/Users/andrzejmarczewski/Downloads/loud_quiet.wav`, and `VXSuitePluginRegressionTests`, then document the kept outcome.

## Review
- Added a new shared framework layer in [VxSuiteVoiceContext.h](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/framework/VxSuiteVoiceContext.h) and [VxSuiteVoiceContext.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/framework/VxSuiteVoiceContext.cpp). It derives lightweight vocal-structure cues such as `vocalDominance`, `buriedSpeech`, `phraseActivity`, `phraseStart`, `phraseEnd`, and `intelligibility` from the existing block analysis plus a few extra band/phrase smoothers.
- Wired `ProcessorBase` to maintain that shared context and expose it to products via `getVoiceContextSnapshot()` in [VxSuiteProcessorBase.h](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/framework/VxSuiteProcessorBase.h) and [VxSuiteProcessorBase.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/framework/VxSuiteProcessorBase.cpp).
- Updated `VXLeveler` as the first consumer by feeding the shared context into [VxLevelerDetector.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/leveler/dsp/VxLevelerDetector.cpp) through [VxLevelerProcessor.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/leveler/VxLevelerProcessor.cpp). This kept the proven DSP core but improved the detector inputs with framework-level voice understanding.
- The change verified better on both the user file and the new real speech corpus:
  - `/Users/andrzejmarczewski/Downloads/loud_quiet.wav`: `12.9225 dB` -> `11.8946 dB` in `Vocal Rider`
  - `churchill_be_ye_men_of_valour`: `22.8432 dB` -> `20.1124 dB`
  - `edward_viii_abdication`: `10.1938 dB` -> `7.18846 dB`
  - `old_letters_librivox`: `19.8806 dB` -> `18.0528 dB`
  - `princess_elizabeth_21st_birthday`: `12.9195 dB` -> `10.9563 dB`
- Verified with `cmake --build build --target VXLevelerMeasure VXLevelerPlugin VXSuitePluginRegressionTests -j4`, the `VXLevelerMeasure` run on `/Users/andrzejmarczewski/Downloads/loud_quiet.wav`, the four corpus runs under `/Users/andrzejmarczewski/Documents/GitHub/VxStudio/data/voice_corpus/wav/`, and `./build/VXSuitePluginRegressionTests`.

# Voice corpus acquisition for shared vocal tuning — 2026-03-21

## Problem
We need more than one user file and synthetic regression material if we want `Voice` mode and future framework-level vocal understanding to improve reliably. The tuning loop needs a small repeatable real-world speech corpus inside the repo.

## Plan
- [x] Find permissive/public real-world spoken-word source files that can be downloaded directly.
- [x] Download them into a local corpus folder and convert them to consistent WAV files for tuning.
- [x] Run the current `Voice` mode across the corpus to establish a baseline.
- [x] Record the corpus inventory and baseline measurements in the repo.

## Review
- Added a small real-world speech corpus under `/Users/andrzejmarczewski/Documents/GitHub/VxStudio/data/voice_corpus/` with four source recordings from Wikimedia Commons and the Internet Archive / LibriVox.
- Converted all files to `48 kHz` mono WAV for repeatable tuning and evaluation.
- Added `/Users/andrzejmarczewski/Documents/GitHub/VxStudio/data/voice_corpus/README.md` with source links, local file paths, and baseline `Voice`-mode measurements.
- Current `Voice`-mode baseline across the corpus:
  - `churchill_be_ye_men_of_valour`: `22.8432 dB` -> `20.1216 dB`
  - `edward_viii_abdication`: `10.1938 dB` -> `7.21141 dB`
  - `old_letters_librivox`: `19.8806 dB` -> `18.121 dB`
  - `princess_elizabeth_21st_birthday`: `12.9195 dB` -> `10.99 dB`

# Shared vocal context rollout to voice-aware products — 2026-03-22

## Problem
The new shared framework vocal context already improved `VXLeveler`, but it is still only helping one product directly. To make `Voice` mode meaningfully better across the suite without destabilising proven DSP, we need a conservative framework-first rollout into products that already consume voice-aware evidence, starting with `Cleanup` and then a small guarded pass on `Denoiser`.

## Plan
- [x] Extend shared polish analysis evidence so it can blend the framework `VoiceContextSnapshot` into existing speech-confidence and safety cues.
- [x] Update `Cleanup` to pass the shared voice context through that evidence path and keep its DSP contract unchanged otherwise.
- [x] Add a conservative `Voice`-mode protection bias in `Denoiser` using the shared vocal context, without changing its core denoise topology.
- [x] Rebuild the affected targets, run the regression suite, and keep the changes only if they verify cleanly.
- [x] Record the kept outcome here and add a lesson if the rollout confirms the framework-first pattern again.

## Review
- Extended shared analysis evidence in [VxPolishAnalysisEvidence.h](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/polish/VxPolishAnalysisEvidence.h) so products can blend framework `VoiceContextSnapshot` cues like `vocalDominance`, `phraseActivity`, `intelligibility`, and `speechBandEnergy` into the existing speech-confidence, artifact-risk, proximity, and speech-loudness evidence instead of inventing another product-local detector.
- Updated [VxCleanupProcessor.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/cleanup/VxCleanupProcessor.cpp) to consume that richer shared evidence and give `Voice` mode a small extra preservation bias from the framework vocal context without changing the corrective topology or adding a separate DSP branch.
- Updated [VxDenoiserProcessor.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/denoiser/VxDenoiserProcessor.cpp) so `Voice` mode now uses the shared vocal context to modestly reduce over-cleaning pressure during high-priority vocal regions and to increase voice-retention bias during makeup recovery, while keeping the same denoiser backend and noise-only safeguards.
- Verification stayed clean: `cmake --build build --target VXCleanupPlugin VXDenoiserPlugin VXSuitePluginRegressionTests -j4` and `./build/VXSuitePluginRegressionTests` both passed, including the existing `Cleanup` voiced-material/plosive regressions and the `Denoiser` vocal/general level-retention and noise-only tests.
- Kept outcome: the shared vocal context is now helping `VXLeveler`, `Cleanup`, and `Denoiser`, which is a better suite-wide use of the framework layer than another isolated `VXLeveler` DSP rewrite.

# Shared vocal context rollout batch 2 — 2026-03-22

## Problem
The framework vocal context is still only reaching part of the suite. Several products already expose a meaningful `Voice` mode but still rely on fixed voice-mode mappings rather than the new shared vocal understanding. We need a second conservative rollout pass for the products that can benefit from context-aware `Voice` protection without changing their DSP topology.

## Plan
- [x] Audit the remaining voice-aware products and choose the batch where shared-context remapping is low-risk and likely beneficial.
- [x] Apply conservative `Voice`-mode remaps for `Finish`, `OptoComp`, `Tone`, `Proximity`, and `Subtract` using the shared vocal context.
- [x] Leave `Deverb` out of this batch unless a clearly safer shared-context seam emerges, because its dereverb/body-restore contract is more specialised.
- [x] Rebuild the affected targets and rerun `VXSuitePluginRegressionTests`.

# Subtract voice-mode corpus tuning — 2026-03-23

## Problem
The improved batch harness now scores `Subtract` against a realistic noisy-speech input and a clean-speech target reference. On that corrected measurement, `Subtract` is the clearest remaining weak spot in suite `Voice` mode: it is over-subtracting the spoken clip and drifting far from both the noisy input and the clean target. The fix needs to improve `Voice`-mode speech preservation and learned-profile behavior without breaking the existing subtract regressions or the learned/listen workflow.

## Plan
- [x] Review the current `Subtract` voice-mode DSP/control flow against the new target-reference batch metrics and identify the strongest low-risk tuning seam.
- [x] Implement a focused `Voice`-mode tuning pass for `Subtract`, keeping `General` behavior and learn/listen semantics intact.
- [x] Rebuild and rerun `VXSuitePluginRegressionTests` plus the focused `Subtract` batch report, then keep only the tuned version if it improves the correct metrics.
- [x] Record the verified outcome and any new lesson from the pass.

## Review
- Tuned `Subtract` in two places:
  - [VxSubtractDsp.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/subtract/dsp/VxSubtractDsp.cpp) now adds a narrower speech-preservation path inside the learned-profile subtraction stage. In `Voice` mode it only protects bins that look genuinely voiced and mid-band, instead of broadly lifting the learned subtract floor.
  - [VxSubtractProcessor.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/subtract/VxSubtractProcessor.cpp) now maps `Voice` mode toward slightly stronger protection weighting and a slightly softer internal subtract drive, while leaving `General` mode unchanged.
- Kept outcome on the corrected clean-target batch check in [VX_SUITE_BATCH_AUDIO_CHECK_SUBTRACT_REAL.md](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/data/voice_corpus/VX_SUITE_BATCH_AUDIO_CHECK_SUBTRACT_REAL.md):
  - baseline: target corr `0.105`, target speech corr `0.127`, target residual `0.992`
  - kept build: target corr `0.111`, target speech corr `0.129`, target residual `0.992`
- This is a modest improvement, not a breakthrough. The important part is that it improves the correct `Voice`-mode metric without regressing the subtract lifecycle/listen/stereo regressions.
- Verified with `./build/VXSuitePluginRegressionTests` and `./build/VXSuiteBatchAudioCheck /Users/andrzejmarczewski/Documents/GitHub/VxStudio/data/voice_corpus/wav_clip /Users/andrzejmarczewski/Documents/GitHub/VxStudio/data/voice_corpus/VX_SUITE_BATCH_AUDIO_CHECK_SUBTRACT_REAL.md --products=subtract`.

# Deverb voice-mode corpus tuning — 2026-03-23

## Problem
The corrected batch harness shows `Deverb` is much healthier than `Subtract`, but it is still one of the remaining voice-mode products worth refining against a clean dereverb target. The current clean-target report is already decent, so the only acceptable change here is a conservative voice-mode tuning pass that improves dereverb recovery without destabilising speech coherence, level retention, or existing deverb regressions.

## Plan
- [x] Review the current `Deverb` clean-target batch report, DSP control path, and regression seams to identify the safest voice-mode tuning seam.
- [x] Implement a conservative `Voice`-mode tuning pass for `Deverb`, keeping the core spectral processor topology intact.
- [x] Rebuild and rerun the deverb regressions plus the focused clean-target batch report, then keep only the version that improves the right metrics.
- [x] Record the verified outcome and any new lesson from the pass.

## Review
- Tried a conservative `Voice`-mode retune in [VxDeverbProcessor.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/deverb/VxDeverbProcessor.cpp) by slightly increasing effective dereverb depth and body recovery when shared vocal context was strong.
- The focused clean-target dereverb report got slightly worse, so I reverted the DSP change and kept the previous verified `Deverb` baseline. Candidate report outcome before revert: target corr stayed `0.854`, target speech corr stayed `0.866`, and target residual worsened from `0.520` to `0.521`.
- That confirmed `Deverb` is not a good blind-tweak target right now. The more useful follow-up was improving the harness itself so `Denoiser` is also measured against a noisy-input / clean-target pair.
- Updated [VXSuiteBatchAudioCheck.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/tests/VXSuiteBatchAudioCheck.cpp) so `Denoiser` now receives synthetic noisy speech as input and is scored against the original clean clip. The new focused report is [VX_SUITE_BATCH_AUDIO_CHECK_DENOISER_REAL.md](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/data/voice_corpus/VX_SUITE_BATCH_AUDIO_CHECK_DENOISER_REAL.md), which gives a more honest baseline:
  - target corr `0.749`
  - target speech corr `0.885`
  - target residual `0.660`
- Verified final kept state with `./build/VXSuitePluginRegressionTests` after the `Deverb` revert and harness update.

# Denoiser voice-mode corpus tuning — 2026-03-23

## Problem
`Denoiser` now has a corrected clean-target batch report instead of a misleading clean-vs-clean score. On that more honest noisy-input / clean-target check, `Voice` mode is decent but still has room to retain spoken detail better while keeping noise-only reduction and the existing denoiser regressions intact.

## Plan
- [x] Review the current `Denoiser` voice-mode gain law and makeup-retention policy against the new clean-target batch baseline.
- [x] Implement a focused `Voice`-mode tuning pass that improves clean-target speech recovery without collapsing noise-only performance.
- [x] Rebuild and rerun `VXSuitePluginRegressionTests` plus the focused `Denoiser` batch report, then keep only the version that improves the right metrics.
- [x] Record the verified outcome and any new lesson from the pass.

## Review
- Tuned [VxDenoiserProcessor.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/denoiser/VxDenoiserProcessor.cpp) so `Voice` mode is less timid:
  - increased `effectiveClean` in vocal mode,
  - eased `sourceProtect` slightly,
  - reduced the vocal makeup-retention target a bit so cleanup is not immediately undone.
- Kept outcome on the corrected clean-target report in [VX_SUITE_BATCH_AUDIO_CHECK_DENOISER_REAL.md](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/data/voice_corpus/VX_SUITE_BATCH_AUDIO_CHECK_DENOISER_REAL.md):
  - baseline: target corr `0.749`, target speech corr `0.885`, target residual `0.660`
  - kept build: target corr `0.765`, target speech corr `0.891`, target residual `0.642`
- This is a meaningful improvement because the output moves farther from the noisy input and closer to the clean target while remaining speech-coherent.
- Verified with `./build/VXSuitePluginRegressionTests` after the tuning pass, so the denoiser still passes the existing strong-setting, level-retention, noise-only, stereo-independence, identity, latency, tail-window, and no-allocation checks.

# OptoComp voice-mode short-corpus tuning — 2026-03-23

## Problem
On the short speech corpus, `OptoComp` is already decent but slightly behind `Finish` on evenness (`11.557 dB -> 8.885 dB` versus `8.822 dB`). Because both products share a similar levelling/compression role, `OptoComp` is a good next target for a conservative `Voice`-mode remap that might improve spoken-word levelling without risking a larger DSP rewrite.

## Plan
- [ ] Review the current `OptoComp` voice-mode mapping and short-corpus baseline to identify the smallest promising tuning seam.
- [ ] Implement a focused `Voice`-mode remap for `OptoComp` and rerun the short-corpus batch report before deciding whether to keep it.
- [ ] Run `VXSuitePluginRegressionTests` and document the kept outcome only if the short-corpus result improves without regressions.

## Review
- Tried a very small `Voice`-mode remap in `OptoComp` by slightly increasing vocal-mode peak-reduction engagement and trimming the body assist.
- The short-corpus result did not improve enough to justify keeping it:
  - baseline: `11.557 dB -> 8.885 dB`
  - candidate: `11.557 dB -> 8.885 dB` (slightly worse at full precision)
- Reverted the `OptoComp` tweak and kept the existing verified mapping.

# Finish voice-mode short-corpus tuning — 2026-03-23

## Problem
`Finish` is already the best-performing short-corpus speech leveller in this batch, but it is close enough to the current `OptoComp` result that a very small `Voice`-mode remap may still yield a measurable improvement. Because it is already strong, any kept change must be clearly better on the short corpus and regression-clean.

## Plan
- [x] Review the current `Finish` voice-mode mapping and short-corpus baseline to identify the narrowest promising tuning seam.
- [x] Implement one conservative `Voice`-mode remap for `Finish` and rerun the short-corpus report before deciding whether to keep it.
- [x] Run `VXSuitePluginRegressionTests` and document the outcome, reverting immediately if the corpus result does not improve.

## Review
- Tried one conservative `Voice`-mode remap in `Finish` by slightly increasing vocal-mode peak-reduction engagement and trimming the body assist a little.
- The short-corpus result was effectively flat-to-slightly-worse overall, so I reverted it:
  - baseline: `11.557 dB -> 8.822 dB`
  - candidate: `11.557 dB -> 8.823 dB`
- Verified final kept state with `./build/VXSuitePluginRegressionTests` after reverting the `Finish` candidate.
- Practical conclusion: under the current short-corpus harness, `Finish` and `OptoComp` both look close to their local optimum already. The more useful remaining work is in the corrective products and the harness/reporting layer, not more micro-remaps in this dynamics pair.
- [x] Record the kept outcome here and add a lesson if the batch confirms the same rollout pattern.

## Review
- Audited the remaining `Voice`-aware processors and applied only parameter/remap-level changes so each product keeps its proven DSP core while gaining framework-level vocal understanding.
- `Finish` and `OptoComp` now use `VoiceContextSnapshot` in `Voice` mode to ease peak reduction slightly during high-priority vocal regions and add a small body-preservation bias instead of always treating vocal mode as a fixed table. See [VxFinishProcessor.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/finish/VxFinishProcessor.cpp) and [VxOptoCompProcessor.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/OptoComp/VxOptoCompProcessor.cpp).
- `Tone` now uses shared vocal priority to narrow how much `Voice` mode can boost/cut and to push its shelves a bit farther from the speech-critical band when speech is clearly dominant. See [VxToneProcessor.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/tone/VxToneProcessor.cpp).
- `Proximity` now uses the shared vocal context to be a little more careful with `Air` in consonant-sensitive vocal passages while still allowing `Closer` to help when speech is buried. See [VxProximityProcessor.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/proximity/VxProximityProcessor.cpp).
- `Subtract` now uses shared vocal priority to raise protection and speech focus and to trim subtraction aggressiveness slightly in important vocal regions instead of relying only on the user `Protect` control. See [VxSubtractProcessor.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/subtract/VxSubtractProcessor.cpp).
- I also found a conservative enough seam for `Deverb`, so it joined this kept batch: `Voice` mode now slightly moderates dereverb depth and slightly reinforces body restore when the shared context says the vocal itself is the priority, without changing the underlying dereverb algorithm. See [VxDeverbProcessor.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/deverb/VxDeverbProcessor.cpp).
- Verification passed with `cmake --build build --target VXDeverbPlugin VXFinishPlugin VXOptoCompPlugin VXTonePlugin VXProximityPlugin VXSubtractPlugin VXSuitePluginRegressionTests -j4` and `./build/VXSuitePluginRegressionTests`.
- Kept outcome: the shared vocal context now meaningfully reaches `VXLeveler`, `Cleanup`, `Denoiser`, `Deverb`, `Finish`, `OptoComp`, `Tone`, `Proximity`, and `Subtract`, which is enough to treat the current framework-level `Voice` rollout as suite-wide for the non-ML products that can benefit from it.

# VX Suite batch audio checks harness — 2026-03-22

## Problem
The suite now has a shared vocal-context rollout, but we still verify most products through regressions and a couple of one-off measure tools. To refine `Voice` mode properly across the whole suite, we need a reusable offline batch harness that can run real corpus WAVs through multiple VX products, compute comparable metrics, optionally write renders, and emit a report that is easy to use during tuning.

## Plan
- [x] Inspect the existing `VXLevelerMeasure` and `VXDeverbMeasure` utilities and define a reusable multi-product batch harness shape.
- [x] Implement a `VXSuiteBatchAudioCheck` executable with real WAV input, per-product voice-mode presets, shared metrics, and markdown output.
- [x] Add the new target to CMake and document how to run it on the speech corpus.
- [x] Run the harness on the current real speech corpus for the upgraded voice-aware products and record the measured baseline.
- [x] Keep the harness and report only if they build and run cleanly.

## Review
- Added a reusable offline harness in [VXSuiteBatchAudioCheck.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/tests/VXSuiteBatchAudioCheck.cpp). It can batch-process corpus WAVs through multiple VX products in `Voice` mode, compute shared metrics (`spread`, `delta RMS`, `correlation`, `peak out`), optionally write renders, and emit markdown reports.
- Added the corresponding build target in [CMakeLists.txt](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/CMakeLists.txt) so the harness can be built and rerun locally like the other measure tools.
- Hardened the harness for real corpus work by upmixing mono files to dual mono on load and by writing the markdown report incrementally after each completed product, so long/heavy product runs still leave usable partial results.
- Recorded the first real suite baselines in [VX_SUITE_BATCH_AUDIO_CHECK.md](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/data/voice_corpus/VX_SUITE_BATCH_AUDIO_CHECK.md), with detailed per-batch reports under `/Users/andrzejmarczewski/Documents/GitHub/VxStudio/data/voice_corpus/`.
- Current measured highlights:
  - `Leveler` on the full 4-file corpus: `16.459 dB` spread in -> `14.077 dB` out
  - `Cleanup` on the full 4-file corpus: `16.459 dB` spread in -> `16.838 dB` out
  - `Finish` on the 2-file short corpus: `11.557 dB` spread in -> `8.822 dB` out
  - `OptoComp` on the 2-file short corpus: `11.557 dB` spread in -> `8.885 dB` out
  - `Tone` on the 2-file short corpus: `11.557 dB` spread in -> `11.378 dB` out
  - `Proximity` on the 2-file short corpus: `11.557 dB` spread in -> `11.190 dB` out
  - `Deverb`, `Denoiser`, and `Subtract` were measured on a 5-second speech clip to keep runtime practical; the summary is in the consolidated report.
- Verification passed for the harness itself with `cmake -S . -B build` (already configured), `cmake --build build --target VXSuiteBatchAudioCheck -j4`, and repeated successful harness runs on corpus subsets.

# Weak-product voice tuning with batch harness — 2026-03-22

## Problem
The new harness exposed the weakest real-speech results in `Cleanup`, `Deverb`, and `Subtract`. The goal of this pass was to use the harness as the gatekeeper and improve those products' `Voice` behavior on spoken-word material without destabilising the suite.

## Plan
- [x] Identify the smallest plausible voice-mode remaps for `Cleanup`, `Deverb`, and `Subtract`.
- [x] Implement the candidate tuning changes and rerun focused harness checks.
- [x] Keep only the measured improvements and revert anything that did not move the baseline meaningfully.
- [x] Rebuild and rerun the regression suite after the kept outcome.

## Review
- I tried a conservative tuning pass on `Cleanup`, `Deverb`, and `Subtract` using the new harness as the evaluation loop.
- The changes did not deliver meaningful real-file improvements:
  - `Cleanup` remained effectively unchanged on the full speech corpus.
  - `Deverb` did not improve on the speech clip check.
  - `Subtract` only moved by a rounding-level amount, not enough to justify a behavior change.
- Because of that, I reverted the tuning changes and kept the suite on the last verified DSP state. The real value from this pass is the harness/reporting loop itself, not the abandoned parameter tweaks.
- I then extended [VXSuiteBatchAudioCheck.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/tests/VXSuiteBatchAudioCheck.cpp) with `speech-band correlation` and `gain-matched residual ratio`, reran `Cleanup`, and confirmed that the product is actually preserving spoken-word material well (`avg speech-band corr 0.996`, `avg residual ratio 0.091`) even though its plain loudness spread is slightly worse. That makes `Cleanup` a measurement-interpretation issue, not a clear DSP failure.
- While rerunning verification, the regression suite caught a separate real issue in the earlier `Finish` / `OptoComp` voice-context rollout: the body assist had nudged zero-amount defaults away from full transparency. I fixed that by making the body bias proportional to actual compression amount rather than always active at neutral settings.
- Final verification passed with `cmake --build build --target VXSuitePluginRegressionTests -j4` and `./build/VXSuitePluginRegressionTests`.

# VX Perform v2 levelling + reusable trace view — 2026-03-20

## Problem
`VX Perform` needs to behave more like a true whole-track leveller, and the user wants visible proof that the track is being made more consistent over time while adjusting controls. The current visual telemetry is too short-lived for that job, so the framework needs a reusable dry/wet level-history display with zoom, and `VX Perform` should move straight to a short-lookahead v2 architecture.

## Plan
- [x] Inspect the shared editor and telemetry hooks to find a reusable path for a dry/wet level-history display.
- [x] Extend the shared framework telemetry to publish longer dry/wet level traces suitable for zoomable UI display.
- [x] Add a reusable framework `LevelTraceView` and integrate it into the shared editor layout.
- [x] Upgrade `VX Perform` to a short-lookahead leveller with stronger whole-track balancing and retained spike control.
- [x] Rebuild the affected targets and verify the new plugin still builds cleanly.

## Review
- The shared spectrum telemetry now carries a longer dry/wet level-history trace, and the framework exposes it through a reusable `LevelTraceView` with zoom options in the shared editor shell.
- `VX Perform` now uses a short lookahead (`~10 ms`) with fixed reported latency, stronger whole-track levelling behavior, and the earlier zero-setting transparency issue was corrected so latency-compensated neutral settings stay clean.
- Verified with `cmake -S . -B build`, `cmake --build build --target VXSuitePluginRegressionTests -j4`, `./build/VXSuitePluginRegressionTests`, and `cmake --build build --target VXPerformPlugin -j4`.

# VX Perform implementation — 2026-03-20

## Problem
Build the new VX Suite plugin for mixed instrument-to-camera recordings where the instrument is consistently hotter than the spoken voice. The processor should keep levels more consistent without source splitting, with a v1 contract focused on speech recovery plus bright/transient containment.

## Plan
- [x] Review the existing VX product/build/test patterns and confirm the framework fit for `VX Perform`.
- [x] Add the new product-local processor and DSP modules for `VX Perform`.
- [x] Register the new plugin in CMake and add it to the regression/build lanes.
- [x] Add focused regression coverage for zero-setting transparency and speech-recovery behaviour.
- [x] Build the plugin and run the affected regression target.

## Review
- Added `VX Perform` under `Source/vxsuite/products/perform/` with a two-knob processor (`Clarity`, `Control`), a product-local detector, and a zero-latency DSP core that combines slow levelling, speech-band lift, and high-band transient containment.
- Registered the plugin in `CMakeLists.txt`, added it to the staged VST3 aggregate target, and included it in pluginval plus the shared regression executable.
- Added regression coverage for zero-setting transparency and a synthetic hot instrument-over-speech recovery case. The new plugin built successfully with `cmake --build build --target VXPerformPlugin VXSuitePluginRegressionTests -j4`.
- Running `./build/VXSuitePluginRegressionTests` still fails, but the remaining failures are pre-existing unrelated regressions in `Subtract` state/learn behavior and `OptoComp` audio-thread allocation checks, not the newly added `VX Perform` checks.

# Mixed speech + guitar balancer concept review — 2026-03-20

## Problem
We want to know whether the VX Suite framework can support a plugin for a single mixed track that keeps speech intelligible while preserving the expressiveness of an accompanying guitar. The concept depends on behavior-aware dynamics rather than source separation, so the key question is whether we can express that honestly within the framework's simple UI and realtime-safe DSP rules.

## Plan
- [x] Review the VX Suite framework/docs plus relevant existing dynamics products for fit.
- [x] Translate the user concept into a framework-aligned product contract and decide whether it belongs as a VX Suite product.
- [x] Define the recommended v1 DSP architecture, controls, and verification plan.
- [x] Record the recommendation, risks, and next-step implementation shape.

## Review
- The concept fits VX Suite if we ship it as a narrow adaptive dynamics tool rather than a source-separation product. The recommended v1 is a zero-latency mixed-track balancer with two knobs (`Clarity`, `Control`), no mode switch, and no `Listen` toggle.
- The proposed DSP shape is: slow broadband leveller, speech-band upward lift, high-band transient containment, and a smoothed behaviour detector that biases those stages based on speech-like versus guitar-like dominance rather than hard switching states.
- The full framework-aligned product spec is documented in `docs/VX_LEVELER_SPEC.md`, including identity, UX contract, DSP rules, realtime constraints, and verification targets.

# DSP hot-path pass (phase 1) — 2026-03-19

## Problem
The latest DSP review identified several confirmed hot-path issues. The highest-value ones to start with were `CorrectiveStage` rebuilding six peaking EQs on every sample and `OptoCompressorLA2A` doing a slow-release `std::exp` per sample.

## Plan
- [x] Move Cleanup/Polish trouble-band EQ coefficient generation out of the per-sample path.
- [x] Reduce `OptoCompressorLA2A` per-sample transcendental work without changing the audible contract.
- [x] Rebuild affected plugins and run the regression suite.
- [ ] Tackle the bigger `SubtractDsp` memory-shift refactor in a dedicated follow-up pass.

## Review
- `Source/vxsuite/products/polish/dsp/VxPolishCorrectiveStage.cpp` now runs the trouble-band detection over the block, accumulates per-band cut depth, and builds the six peaking EQ coefficient sets once per block instead of once per sample. That removes the worst repeated trig/pow work from Cleanup's hottest path while preserving the same trouble-smoothing role.
- `Source/vxsuite/products/OptoComp/OptoCompressorLA2A.cpp` now computes the slow-release coefficient once per block from the current optical-memory / LF state estimate instead of calling `std::exp` for it on every sample.
- During verification, the earlier shared framework output trimmer change was corrected so the emergency trimmer protects normal processed output but does not touch listen-delta output. That keeps headroom safety while preserving subtract-style wet+listen recombination semantics.
- The larger `SubtractDsp` ring-buffer conversion was started, exposed a behavior regression, and was intentionally backed out for this pass. It should come back as a dedicated refactor with targeted verification rather than being forced through alongside the safe CPU wins.
- Verified with `cmake --build build --target VXCleanupPlugin VXOptoCompPlugin VXSuitePluginRegressionTests -j4` and `./build/VXSuitePluginRegressionTests`.

# README refresh for analyser + framework behavior — 2026-03-19

## Problem
The root README and framework README had drifted behind the current implementation. They still described the analyser as having no controls, described listen as always outputting removed material, and implied output safety was only an optional product-local concern instead of part of the shared framework behavior.

## Plan
- [x] Review the root project README and framework README for outdated analyser, listen, and framework-behavior descriptions.
- [x] Update both documents so they reflect the current analyser controls/telemetry model and the current framework output-safety/layout contract.
- [x] Review the doc diff for clarity and consistency.

## Review
- `README.md` now describes `VXStudioAnalyser` as a chain-aware spectrum analyser with `Avg Time`, `Smoothing`, and `Hide Chain`, and explains the current full-chain / per-stage dry-vs-wet workflow.
- The root README also now documents the current listen contract correctly: removal tools audition what was removed, while additive/finishing tools audition what they added.
- `Source/vxsuite/framework/README.md` now documents the responsive shared editor behavior, automatic final output safety in `ProcessorBase`, optional product-local trimmers for extra discipline, and the shared telemetry path used by `VXStudioAnalyser`.
- This was a documentation-only pass; no build was required.

# Cleanup / suite headroom safety pass — 2026-03-19

## Problem
`Cleanup` could clip when De-Ess or Plosive correction were driven hard, and not every VX Suite product had a final output safety stage. The user asked for good headroom across the suite so no processor clips just by being active.

## Plan
- [x] Inspect the Cleanup corrective path and confirm where De-Ess / Plosive overshoot can occur.
- [x] Add a shared suite-wide emergency output safety layer and a Cleanup-specific overshoot guard that preserves subtractive behavior.
- [x] Add regression coverage for Cleanup De-Ess/Plosive stress, rebuild affected targets, and verify the fix.

## Review
- `Source/vxsuite/framework/VxSuiteProcessorBase.*` now owns a shared emergency `OutputTrimmer` for every VX processor. It runs after normal product processing in non-listen mode and after listen rendering in listen mode, providing a final suite-wide headroom safeguard even for products that did not previously have local output trimming.
- `Source/vxsuite/products/cleanup/VxCleanupProcessor.cpp` now measures dry versus post-corrective peak and reins the corrective chain back to a tiny margin above the dry peak before its local trimmer runs. That keeps Cleanup aligned with its subtractive intent: De-Ess, Plosive, and related corrective moves should not create new peak overs just because of filter-split recombination.
- `tests/VXSuitePluginRegressionTests.cpp` now includes a dedicated Cleanup stress case with both low-frequency plosive bursts and aggressive high-frequency sibilance content at full settings. The regression asserts finite output, safe peak headroom, and no large peak inflation relative to the input.
- Verified with `cmake --build build --target VXCleanupPlugin VXStudioAnalyserPlugin VXSuitePluginRegressionTests -j4` and `./build/VXSuitePluginRegressionTests`.

# Analyzer low-band display regression — 2026-03-19

## Problem
The latest in-host analyser pass regressed at the bottom of the spectrum: low bins could dominate summary text with obviously wrong callouts like large `20 Hz` deltas, and the visible low-end contour diverged from the SPAN reference because the shared low-band reduction was still too spike-sensitive.

## Plan
- [x] Inspect the shared spectrum reduction and analyser summary-selection paths to find why very-low bands were over-winning both the plot and the readout.
- [x] Make low-band reduction less sensitive to single-bin spikes and stop near-floor bands from being selected as the largest/dominant change.
- [x] Rebuild `VXStudioAnalyserPlugin`, rerun regression coverage, and document the fix.

## Review
- `Source/vxsuite/framework/VxSuiteSpectrumTelemetry.cpp` now widens extremely small summary bands to a minimum three-bin neighborhood before reduction and uses a less peak-heavy blend for those tiny bands. That keeps one FFT bin at the edge of the log scale from distorting the whole low-end display.
- `Source/vxsuite/products/analyser/VXStudioAnalyserEditor.cpp` now ignores near-floor bands when choosing the largest/dominant spectral delta for the summary text. The graph marker and readout should no longer jump to fake `20 Hz` wins when both dry and wet are effectively at the noise floor there.
- Verified with `cmake --build build --target VXStudioAnalyserPlugin VXSuitePluginRegressionTests -j4` and `./build/VXSuitePluginRegressionTests`.

# Analyzer timing + spectrum fidelity pass — 2026-03-18

## Problem
The analyser is closer, but it still diverges from SPAN in two visible ways: `Avg Time` feels jerky because it currently gates visible refreshes instead of behaving like a smooth RT-average, and the spectrum shape still loses too much low/high detail because each log band is reduced with a broad RMS average.

## Plan
- [x] Rework `Avg Time` so the chart repaints steadily while averaging continuously over time, instead of stepping the visible snapshot cadence.
- [x] Improve the summary-spectrum reduction so the full `20 Hz` to `20 kHz` plot preserves prominent peaks and better matches the reference analyzer shape.
- [x] Build and run the analyser/deverb verification targets, then document the remaining gap versus SPAN.

## Review
- `Source/vxsuite/products/analyser/VXStudioAnalyserEditor.*` now treats `Avg Time` as a true time-domain average of incoming spectrum/envelope frames rather than a stepped snapshot throttle. The UI repaints steadily at `24 Hz`, and longer average times now simply lengthen the decay/settling of the displayed spectrum instead of making the chart visibly jerk between sparse refreshes.
- Expanded `Avg Time` options to `100 ms` through `10000 ms`, keeping the default at `1000 ms` so the analyzer can cover both transient debugging and long-horizon tonal-balance reading more like SPAN.
- `Source/vxsuite/framework/VxSuiteSpectrumTelemetry.h` now publishes a denser analyzer spectrum with `256` log-spaced display bands and an `8192`-sample FFT. `Source/vxsuite/framework/VxSuiteSpectrumTelemetry.cpp` now reduces each log band with a peak-preserving blend instead of a plain RMS average, which keeps narrow resonances and high-frequency structure from collapsing into a broad midrange hump.
- `Source/vxsuite/products/analyser/VXStudioAnalyserEditor.cpp` now applies a fixed `4.5 dB/oct` display slope, matching the supplied SPAN reference more closely, and labels the full visible frequency range from `20 Hz` to `20 kHz`.
- Verified with `cmake --build build --target VXStudioAnalyserPlugin VXDeverbTests -j4` and `./build/VXDeverbTests`.

# Analyzer stage visibility + smoothing calibration — 2026-03-18

## Problem
After the timing/fidelity pass, the analyser regressed in two practical ways: external stage rows could disappear so the UI looked like it only supported `Full Chain`, and the smoothing control became nearly inert because the old fixed-neighbor mapping was far too weak for the new `256`-band spectrum.

## Plan
- [x] Relax the stage freshness gate so valid VX Suite stages remain visible/selectable in the rail instead of dropping out to full-chain fallback.
- [x] Remap spectral smoothing by octave width so `1/12`, `1/6`, `1/3`, and `1 OCT` produce visibly different blur amounts on the denser spectrum.
- [x] Rebuild the analyser target and rerun the standing deverb regression check.

## Review
- `Source/vxsuite/products/analyser/VXStudioAnalyserEditor.cpp` now keeps stage rows alive for up to `1500 ms` of telemetry age, which is much more tolerant of host/UI jitter than the previous `300 ms` gate and should stop the analyser collapsing to `Full Chain` when the live stage list is still valid.
- The smoothing control now scales against actual bins-per-octave for the current analyzer spectrum density instead of a tiny fixed radius. On a `256`-band plot, `1/6 OCT`, `1/3 OCT`, and broader modes now smooth over meaningfully different frequency spans, so the control should finally behave like a real spectral smoothing selector.
- Verified with `cmake --build build --target VXStudioAnalyserPlugin -j4` and `./build/VXDeverbTests`.

# Analyzer stage fallback + Avg Time visibility pass — 2026-03-18

## Problem
The analyser still showed two regressions in-host: the left rail could still end up empty except for `Full Chain`, and `Avg Time` remained too subtle because the visible traces were no longer being averaged strongly enough for the user to perceive the longer time constants.

## Plan
- [x] Add a robust fallback stage scan so the analyser can still populate the rail from live VX Suite stages when the strict current-domain scan comes up empty.
- [x] Make `Avg Time` act on the visible dry/wet traces more strongly so long settings like `5000 ms` and `10000 ms` clearly slow and stabilize the chart.
- [x] Rebuild the analyser target and rerun the standing deverb regression check.

## Review
- `Source/vxsuite/products/analyser/VXStudioAnalyserEditor.h` / `Source/vxsuite/products/analyser/VXStudioAnalyserEditor.cpp` now carry a fallback-stage mode. If no valid stages are found in the analyser's exact current domain, the UI falls back to other recent live VX Suite stages from the shared registry instead of presenting an empty rail.
- The diagnostics text now reports whether the rail is coming from the current domain or the fallback registry scan, which should make any remaining domain-binding issue obvious instead of silent.
- `Avg Time` now drives a stronger second temporal averaging pass over the visible dry/wet traces themselves, not just the hidden linear intermediate state. That should make large values visibly stabilize and slow the plotted curves in a way the user can actually perceive.
- Verified with `cmake --build build --target VXStudioAnalyserPlugin -j4` and `./build/VXDeverbTests`.

# Analyzer cross-process domain binding fix — 2026-03-18

## Problem
The latest in-host screenshot showed `live stages: 0`, which meant the analyser was only seeing its own pass-through telemetry. That explains the empty left rail, the fake dry/wet behavior, and why stage selection could not work at all. The likely root cause is cross-process hosting: external VX stages were only trying to bind to the newest analyser domain in their own process, not the newest analyser domain globally.

## Plan
- [x] Add a global active-domain lookup alongside the existing per-process lookup in the shared analysis registry.
- [x] Make stage publishers fall back to that global analyser domain when no current-process analyser domain exists.
- [x] Rebuild the analyser target and rerun the standing deverb regression check.

## Review
- `Source/vxsuite/framework/VxSuiteSpectrumTelemetry.h` / `Source/vxsuite/framework/VxSuiteSpectrumTelemetry.cpp` now expose `DomainRegistry::latestActiveDomain(...)` in addition to `latestDomainForProcess(...)`.
- `StagePublisher::refreshDomainBinding(...)` now first tries to bind to the newest analyser domain in the current process, and if that fails, falls back to the newest active analyser domain globally. This should let VX stages still join the analyser chain even when the host bridges plugins into separate processes.
- Verified with `cmake --build build --target VXStudioAnalyserPlugin -j4` and `./build/VXDeverbTests`.

# Analyzer real Avg Time window — 2026-03-18

## Problem
Even after the earlier timing passes, `Avg Time` still behaved more like a subtle smoother than a real analyzer average. The user clarified the intended contract: higher values should visibly reduce fluctuation while the trace remains fluid, like SPAN's continuous RT averaging.

## Plan
- [x] Replace the editor-side exponential-only spectrum smoothing with a true rolling time window over recent spectrum frames.
- [x] Keep the UI repaint cadence smooth while using the selected `Avg Time` value to control how much recent spectrum history contributes to the visible trace.
- [x] Rebuild the analyser target and rerun the standing deverb regression check.

## Review
- `Source/vxsuite/products/analyser/VXStudioAnalyserEditor.h` / `Source/vxsuite/products/analyser/VXStudioAnalyserEditor.cpp` now maintain a rolling spectrum history per selection, along with running sums for dry and wet spectra.
- `Avg Time` now directly controls the size of that time window. Increasing the control causes the visible spectrum to average more recent frames together, which should reduce fluctuation without turning the chart into a stepped or frozen display.
- Verified with `cmake --build build --target VXStudioAnalyserPlugin -j4` and `./build/VXDeverbTests`.

# Restage VX chain after framework telemetry fixes — 2026-03-18

## Problem
The sidebar still did not repopulate in REAPER even after the framework-side domain fixes. The likely operational cause is that only `VXStudioAnalyser` had been rebuilt/restaged; the other VX Suite effects in the chain were still older binaries and therefore still publishing stage telemetry with the pre-fix shared framework behavior.

## Plan
- [x] Rebuild and restage the relevant VX Suite chain plugins so they all pick up the latest shared framework telemetry/domain changes.
- [x] Rerun the standing deverb regression check after the full plugin restage.

## Review
- Rebuilt/restaged `VXSubtract`, `VXCleanup`, `VXDeepFilterNet`, `VXDeverb`, `VXDenoiser`, `VXProximity`, `VXTone`, `VXOptoComp`, `VXFinish`, and `VXStudioAnalyser` with `cmake --build build --target VXSubtractPlugin VXCleanupPlugin VXDeepFilterNetPlugin VXDeverbPlugin VXDenoiserPlugin VXProximityPlugin VXTonePlugin VXOptoCompPlugin VXFinishPlugin VXStudioAnalyserPlugin -j4`.
- This ensures the whole REAPER chain now shares the same updated `VxSuiteFramework` telemetry/domain code rather than mixing a new analyser with old effect binaries.
- Re-ran `./build/VXDeverbTests` after the full restage.

# Analyzer FFT + range upgrade — 2026-03-18

## Problem
The analyser still feels fundamentally unlike SPAN because the underlying shared telemetry is too coarse. Even with better display logic, a `32`-band summary spectrum cannot present the full `20 Hz` to `20 kHz` picture with the width and separation the user expects from a real analyzer-style display.

## Plan
- [x] Increase the analyser telemetry FFT size and spectrum-band density so the dry/wet overlay can represent the audible range much more faithfully.
- [x] Tune the analyser plot labels and scaling so the denser spectrum reads clearly from `20 Hz` to `20 kHz`.
- [x] Build and run the affected targets/tests, then document the new analyzer contract and any remaining gaps versus SPAN.

## Review
- `Source/vxsuite/framework/VxSuiteSpectrumTelemetry.h` now publishes a denser analyser spectrum using a `4096`-sample FFT (`kSummarySpectrumFftOrder = 12`) and `128` display bands instead of the old `2048` / `32` summary path. That gives the dry/wet overlay much more room to represent the full `20 Hz` to `20 kHz` range rather than collapsing into a broad midrange sketch.
- `Source/vxsuite/products/analyser/VXStudioAnalyserEditor.cpp` continues to use the same stage-selection model on top of that denser telemetry, while the sparse-spectrum detection thresholds were widened so narrowband/test-tone cases still switch into the honest sparse display mode with the higher band count.
- The analyser still is not a clone of SPAN: it remains a VX Suite stage-aware analyzer rather than a full dedicated meter. But the backend is no longer bottlenecked by a 32-band summary spectrum, which was the main reason the view could not feel wide enough no matter how the UI was tuned.
- Verified with `cmake --build build --target VXStudioAnalyserPlugin VXDeverbTests -j4` and `./build/VXDeverbTests`.

# Deverb loudness + analyser spectrum redesign — 2026-03-18

## Problem
`VXDeverb` currently allows a substantial output level collapse because its wet path removes energy without any restrained direct-loudness compensation. Separately, `VX Studio Analyser` still uses a `Tone/Dynamics` presentation model when the desired product direction is closer to SPAN: dry and wet spectra overlaid in one place, while keeping stage-by-stage chain selection.

## Plan
- [x] Add a bounded loudness-preservation path to `VXDeverb` so dereverbing no longer causes a large level drop simply from inserting/using the plugin.
- [x] Add a regression test that catches unacceptable `VXDeverb` volume loss while preserving tail-reduction behavior.
- [x] Rework `VX Studio Analyser` into a single dry-vs-wet spectrum view with the current stage-selection model, replacing the current `Tone/Dynamics` tab split in practice.
- [x] Build and run the affected targets/tests, then document the new behavior contract and any remaining calibration work.

## Review
- `Source/vxsuite/products/deverb/VxDeverbProcessor.*` now applies a restrained post-deverb loudness compensation stage based on the dry-versus-wet RMS delta. The makeup is capped, smoothed over time, and peak-limited so `VXDeverb` no longer solves dereverbing by simply dumping overall level.
- Added a regression in `tests/VXDeverbTests.cpp` that fails if a representative dereverb render keeps too little of the input RMS, while the existing tail-reduction/coherence tests continue to guard the actual dereverb job.
- `Source/vxsuite/products/analyser/VXStudioAnalyserEditor.*` now behaves as a single dry/wet spectrum viewer instead of a `Tone/Dynamics` toggle. The main plot overlays dry and wet spectra in one place, keeps the stage-chain selection model, and preserves the sparse-spectrum honesty mode for narrowband/test-like signals.
- Added user-facing analyser controls for `Avg Time` and `Smoothing`, defaulting to `1000 ms` and `1/6 OCT` to match the supplied SPAN reference. These settings now drive the editor-side temporal averaging and frequency-domain smoothing so you can trade stability against detail.
- The current analyser still uses the existing backend telemetry overlap, which already starts from the same 75% overlap contract as the SPAN screenshot. The new controls are editor-side timing/smoothing controls rather than a full telemetry ABI change.
- Verified with `cmake --build build --target VXStudioAnalyserPlugin VXDeverbTests -j4` and `./build/VXDeverbTests`.

# Analyzer sparse-spectrum honesty pass — 2026-03-18

## Problem
The current `VX Studio Analyser` tone view still lies on sparse or test-like material. A sine/comb-like chain that reads as distinct peaks in SPAN is being blurred into a broad connected curve and summarized with language like `Low lift`, which makes the analyser look unstable and semantically wrong even when the underlying processors are not.

## Plan
- [x] Detect sparse / narrowband tone cases in the analyser render model instead of treating every spectrum like a broad voice-shaping curve.
- [x] Change the tone graph and summary language for sparse cases so the UI shows discrete affected bands and suppresses fake broad labels.
- [x] Build `VXStudioAnalyserPlugin`, review the new behavior contract, and document any remaining limitations.

## Review
- `Source/vxsuite/products/analyser/VXStudioAnalyserEditor.*` now classifies sparse / narrowband spectra from the actual band-energy distribution before choosing how to render the tone view. Signals dominated by a few peaks no longer get automatically passed through the broad-curve path.
- In sparse mode, the tone tab now stops neighbor-blending the bins into a fake shelf/tilt shape. It renders discrete per-band stems and markers instead, which is much closer to what tools like SPAN show for comb-like or test-tone material.
- The tone summary text now becomes honest for sparse cases: it reports the primary changed band plus a few significant band changes and explicitly suppresses broad labels like `Low lift` or `High cut`.
- The stage rail classifier now uses the same sparse detection, so rows that would previously chatter between `Tone`, `Dynamic`, and `Mixed` on narrowband material can identify as `Sparse` instead of pretending the change is a broad tonal shape.
- Verified with `cmake --build build --target VXStudioAnalyserPlugin -j4`. The existing ad-hoc code-signing replacement message appeared during VST3 staging, but the target completed successfully.
- Remaining limitation: this is still a 32-band explanation view, not a full-resolution analyzer like SPAN. The sparse mode makes it materially more honest, but it will still summarize rather than measure every narrow peak.

# Full VX Suite audit pass — 2026-03-20

## Problem
The repo now has a formal v2 audit baseline covering plugin validation, framework correctness, realtime safety, DSP correctness, REAPER host behavior, and release/build readiness. The suite needs that plan executed against the current codebase, with findings and coverage gaps documented from actual code and tooling results rather than assumptions.

## Plan
- [x] Capture the current environment, available validation tools, and shipping plugin inventory.
- [x] Run available automated verification lanes (`cmake` tests/profile/build checks, and `pluginval` if available) and record blockers for any missing external tooling.
- [x] Audit the shared framework against the v2 baseline: parameter/state contracts, denormal handling, latency/listen/tail behavior, oversized-block handling, and hot-path safety.
- [x] Audit products in risk order with emphasis on DeepFilterNet, Subtract, Deverb, and Denoiser, then cover the remaining processors and analyser.
- [x] Audit build/packaging metadata, bundle/resource layout, and platform/release readiness claims.
- [x] Produce a written audit report with severity-ranked findings, verification evidence, and explicit test/tooling gaps.

## Review
- Environment captured on macOS 15.7.4 / Apple Silicon with a clean worktree and all staged VST3 bundles present under `Source/vxsuite/vst/`. `pluginval` and `reaper` were not available on `PATH`, so those lanes were documented as blocked rather than guessed.
- Built and ran `VXSuitePluginRegressionTests`, `VXDeverbTests`, `VxSuiteVoiceAnalysisTests`, and `VXSuiteProfile`; all completed successfully. `VXSuiteProfile` showed the sampled Cleanup/Proximity/Finish/Subtract chain remaining comfortably below realtime cost on this machine, including at `96 kHz`.
- The audit report is in `tasks/reports/VX_SUITE_FULL_AUDIT_2026-03-20.md` and records the main confirmed findings: zero tail-length reporting across framework-based products, `VXDeepFilterNet` model/resource staging and lookup issues, missing pluginval/CTest integration, duplicate `VXDeverb` CMake source entries, ad-hoc/non-notarized macOS bundles, and arm64-only staged artifacts.

# Verification coverage expansion — 2026-03-20

## Problem
The audit follow-up still had several verification gaps: no-allocation checks only covered Cleanup, tail reporting was only asserted at the metadata level, and there were no objective frequency-shape regressions for the clearly spectral products.

## Plan
- [x] Add shared test utilities for post-input tail rendering and sine-based response measurements.
- [x] Expand steady-state no-allocation checks across the processors already covered by the regression target.
- [x] Add tail-window behavior tests for latency/tail-bearing processors in the regression target.
- [x] Add basic frequency-response regression checks for Tone, Proximity, and Finish body shaping.
- [x] Rebuild and rerun `VXSuitePluginRegressionTests`.

## Review
- `tests/VxSuiteProcessorTestUtils.h` now provides `makeSine(...)`, `renderWithTail(...)`, and `rmsSkip(...)` so regression tests can measure simple spectral and tail behavior without duplicating harness code.
- `tests/VXSuitePluginRegressionTests.cpp` now checks steady-state audio-thread allocations for Cleanup, Denoiser, Deverb, Subtract, Finish, OptoComp, Proximity, and Tone instead of only Cleanup.
- The same regression file now verifies that Deverb, Denoiser, and Subtract produce meaningful output within their reported tail window and decay after it, rather than only reporting a non-zero tail length.
- Added basic frequency-shape regression checks that confirm `VXTone` bass/treble controls, `VXProximity` closer/air controls, and `VXFinish` body shaping still bias the expected frequency regions.
- Verified with `cmake --build build --target VXSuitePluginRegressionTests -j4` and `./build/VXSuitePluginRegressionTests`.

# Cleanup voiced-distortion fix — 2026-03-20

## Problem
`VXCleanup` is still adding audible distortion on voiced material. The existing Cleanup stress regression covers peak/headroom safety, but it does not directly guard the more important failure mode here: corrective processing should not chew up a clean sustained tone or otherwise turn harmonic speech material crunchy.

## Plan
- [x] Add a focused Cleanup distortion regression that reproduces the voiced-material failure case and fails on excessive harmonic damage.
- [x] Inspect and fix the corrective DSP path so Cleanup preserves voiced material while keeping its subtractive cleanup role.
- [x] Rebuild and rerun the affected regression target, then document the result and capture the lesson.

## Review
- `Source/vxsuite/products/cleanup/VxCleanupProcessor.cpp` now applies an explicit voiced-material guard before handing `deEss`, `breath`, and `troubleSmooth` to the shared corrective stage. Cleanup still acts on bright/noisy content, but it no longer asks the downstream DSP to push high-band removal as hard when the analysis says the block is clearly harmonic speech.
- `tests/VXSuitePluginRegressionTests.cpp` now adds a second voiced edge-case regression with a sustained harmonic source plus strong upper-mid/top-end content. That complements the older voiced-material test and makes the distortion check much harder to accidentally satisfy with only peak/headroom safety.
- Verified with:
  - `cmake --build build --target VXSuitePluginRegressionTests -j4`
  - `/tmp/cleanup_verify` built from the current `VXSuitePluginRegressionTests` objects, which measured `voiced_tone corr=0.997552 residualRatio=0.0699273` and `voiced_edge corr=0.995491 residualRatio=0.0948516`, both finite and within the new thresholds
  - `./build/VXSuitePluginRegressionTests` still stops on the pre-existing unrelated Deverb tail regression (`Deverb tail window was unexpectedly empty`), so the full suite is not green yet even though the Cleanup-specific verification passed

# Analyzer readability fixes — 2026-03-18

## Problem
`VX Studio Analyser` is functionally live, but the current in-host UI still fails the readability bar from the latest screenshots. The left stage rail does not present plugin names clearly, its first row collides with the rail header, and the Tone/Dynamics plots move too quickly and abstractly to communicate meaningful change.

## Plan
- [x] Fix the analyser stage rail layout/rendering so the header, full-chain row, and per-stage rows do not overlap and each row shows the plugin name/state/impact clearly in-host.
- [x] Simplify and slow the tone/dynamics render model so plotted changes are easier to read, with stronger temporal smoothing/hold behaviour and less meaningless frame-to-frame motion.
- [x] Build the analyser target, review the resulting diff, and document what changed plus any remaining calibration follow-up.

## Spec Notes
- Prefer the smallest editor-side fix that restores readable stage names and reliable hit targets.
- Preserve the existing telemetry ABI unless the plot issue clearly requires a framework-level cadence/smoothing adjustment.
- Favor “stable explanation” over “maximum liveness”; the analyser should read like a metering tool, not raw telemetry.

## Review
- Removed the invisible per-row `TextButton` overlay from the stage rail interaction path. Stage rows are now painted directly and selected via `mouseUp`, which avoids child-component repainting over the custom row text.
- Added explicit spacing between the `STAGE CHAIN` header and the `Full Chain` row so the first control no longer collides with the section title.
- Kept row text readable with fitted stage-name/state/impact text instead of relying on the hidden buttons.
- Slowed the analyser backend refresh from 20 Hz to 10 Hz and increased tone/dynamics/summary smoothing constants so the view behaves more like a readable meter than raw telemetry.
- Smoothed neighbouring tone bins and replaced the Catmull-Rom tone path with a direct line path so the graph stops inventing spline wiggles between sparse summary bands.
- Compile-verified with `cmake --build build --target VXStudioAnalyserPlugin -j4`.
- Remaining follow-up: this is compile-verified and targeted at the exact screenshot issues, but it still needs an in-host visual check to confirm the new cadence feels right across real material.

# VX Studio Analyser UI correction pass — 2026-03-18

## Problem
The rebuilt analyser still misses the usability bar in-host: the left stage rail does not reliably show plugin names and the rail header overlaps the top control, while the tone/dynamics plots feel too twitchy and semantically weak to read as a practical explanation tool.

## Plan
- [x] Fix the stage rail layout/render path so the header has its own space and every live stage name is visibly rendered.
- [x] Simplify and stabilize the tone/dynamics display model so changes read as broad, interpretable processor behaviour instead of fast-moving telemetry.
- [x] Build `VXStudioAnalyserPlugin`, review the result, and document any remaining calibration gaps.

## Review
- `Source/vxsuite/products/analyser/VXStudioAnalyserEditor.*` no longer relies on invisible `TextButton` overlays for stage selection. The stage rail is now painted directly, stage clicks are resolved via row hit-testing, and the rail header has dedicated vertical space above the `Full Chain` control, so the plugin names/state text are no longer hidden under button painting or overlapping the header region.
- The analyser display cadence is calmer: backend refresh moved from 50 ms to 100 ms, tone/dynamics summary smoothing was lengthened, tiny tone deltas are suppressed, and neighboring bins are blended before rendering so the curves read as broad processor moves instead of frame-by-frame FFT chatter.
- The tone path now draws a direct band-connected line instead of the previous overshooting spline, which removes fake wiggles between analyzer bands. Dynamics curves also get an extra neighbor-smoothing pass before the path is built, so the history panel is easier to read.
- Cleaned up text presentation alongside the fix: the title/header stack now includes the `VX SUITE` label again, the summary card only shows the actionable lines, and diagnostics/dynamics text now uses plain ASCII state words instead of the broken glyphs shown in-host.
- Verified with `cmake --build build --target VXStudioAnalyserPlugin -j4`. The VST3 restage emitted the existing ad-hoc code-signing replacement message but completed successfully.
- Remaining caveat: this pass is compile-verified and targeted to the exact issues from the screenshots, but I have not done a fresh REAPER visual check from this environment. The next refinement, if needed, should be based on one more in-host screenshot so we can tune readability rather than guess at it.

# Analyzer smoothing + render contract — 2026-03-18

## Problem
`VX Studio Analyser` still had the right stage-aware structure but the underlying cadence, smoothing, and render model were too raw, so the plots read like unstable debug telemetry instead of an industry-standard explanation tool. The user provided exact smoothing constants, FFT settings, RMS/peak rules, and a definitive visualization contract that now needs to become the implementation target.

## Plan
- [x] Update framework telemetry accumulation to match the requested analyzer defaults: 15 Hz publish cadence, 2048 Hann tone analysis with 75% overlap semantics, rolling RMS/peak/transient calculations, and stable dry-baseline summaries.
- [x] Rebuild the analyzer backend/editor flow around a staged render model with 20 Hz backend updates, 30 Hz UI repaint, smoothed summary metrics, and the exact Tone/Dynamics presentation contract.
- [x] Verify the analyzer build and document the resulting behavior, limits, and any residual calibration work.

## Review
- `Source/vxsuite/framework/VxSuiteSpectrumTelemetry.*` now treats stage summaries as rolling state instead of one isolated publish frame. Tier 1 tone telemetry moved to 32 log-spaced bands driven by a 2048-sample Hann analysis window with quarter-hop updates, and the publish cadence is now 15 Hz.
- The framework dynamics summary is now more analyzer-like: rolling 100 ms RMS, 10 ms versus 100 ms transient contrast, decaying held peak, and a 250 ms RMS history feeding the dynamics graph. `StagePublisher` no longer resets accumulators after every publish, so snapshots stay temporally stable.
- `VXStudioAnalyserProcessor` now publishes its own pass-through stage telemetry as a dry baseline. The editor excludes that analyzer stage from the visible chain, but uses it when there are no active upstream VX processors so the UI shows a stable dry/zero-change state instead of looking empty.
- `VXStudioAnalyserEditor` was split into a backend/render pipeline: a `HighResolutionTimer` builds the render model at 20 Hz, a UI timer applies it at 30 Hz, and `paint()` now only draws the already-prepared model. Telemetry reads, scope resolution, smoothing, and summary generation are out of the paint path.
- Tone rendering now follows the requested contract more closely: log-frequency axis, centered dB range, delta-first fill, faint before/after context, largest-change marker, and summary-strip language derived from smoothed values. Dynamics now shows RMS-over-time in dBFS with proper meter-style axes instead of unlabeled abstract traces.
- The analyzer chrome now follows the same VX Suite shell more closely: quieter rounded panels, stronger typographic hierarchy, a calmer stage rail, a dedicated summary card, and smoothed spline-style curve drawing so the hero graph feels closer to an industry EQ/dynamics tool rather than a raw debug plot.
- Verified with `cmake --build build --target VXStudioAnalyserPlugin -j4` and `cmake --build build --target VXCleanupPlugin -j4`.
- Remaining caveat: this pass is compile-verified and architecture-aligned, but not yet visually calibrated in-host after the new smoothing constants landed. The next likely refinement, if needed, is tuning summary wording or graph normalization from real screenshots rather than changing the transport again.

# VX Analyzer rebuild spec - 2026-03-18

## Problem
Replace `VXSpectrum` with `VX Studio Analyser`, rebuilding the analyzer around a separate analysis layer instead of asking each DSP plugin to own rich inspection logic. The current `VXSpectrum` path is still a spectrum-first v1 that infers chain structure from waveform correlation and shared slot registration order, which is too fragile for reliable stage/group/full-chain inspection.

## Plan
- [x] Re-read the framework docs, research notes, and project lessons that constrain shared analysis and realtime telemetry work.
- [x] Inspect the current `VXSpectrum` and framework telemetry implementation to identify the exact seams a rebuild should replace.
- [x] Write a concrete technical spec for a two-tier telemetry model, analyzer responsibilities, transport semantics, and UI states.
- [x] Add a review section that calls out why the new design is more robust than the current inferred-chain model and what implementation phases should follow.
- [x] Tighten the spec with domain authority, explicit `StageTelemetry`, deterministic ordering, and incremental summary rules.
- [x] Start the framework implementation by introducing the new telemetry ABI and domain registry primitives.
- [x] Wire the new telemetry publisher through `ProcessorBase` without breaking current buildability.

# Analyzer live-chain sidebar contract fix — 2026-03-19

## Problem
The analyser sidebar regressed after the timing/smoothing work. Instead of representing the live VX Studio plugins active in the FX chain, it was effectively telemetry-driven: rows could disappear, partially appear, or map to the wrong stage whenever per-stage telemetry coverage lagged behind the chain snapshots. That also let incomplete telemetry create misleading full-chain graphs and absurd per-stage deltas.

## Plan
- [x] Re-center the sidebar on live VX chain snapshots so active plugins stay visible even when telemetry is partial.
- [x] Match stage telemetry onto snapshot rows with tolerant VX name normalization instead of brittle raw display-name equality.
- [x] Keep full-chain comparison in analyser passthrough mode whenever chain coverage is incomplete, while still allowing a selected stage row to show real telemetry if that stage has arrived.
- [x] Rebuild `VXStudioAnalyserPlugin` and rerun `VXDeverbTests`.

## Review
- `Source/vxsuite/products/analyser/VXStudioAnalyserEditor.cpp` now builds the sidebar from `SnapshotRegistry` first, using that as the source of truth for which VX plugins are currently live in the chain. Stage telemetry now enriches those rows instead of determining whether the row exists.
- Added VX-aware name normalization so snapshot product names like `VXSubtract` can reliably match telemetry stage names/IDs such as `Subtract` or similar framework identifiers.
- Sidebar selection is now row-based and stable. Clicking a visible chain row selects that row directly; the analyser then resolves any matched stage telemetry behind it. This removes the old index mismatch where a sidebar row could silently point at the wrong telemetry stage.
- Full-chain mode now only trusts the chain comparison when telemetry coverage is complete. If the live chain rows outnumber matched telemetry rows, the analyser falls back to its own passthrough view for the full-chain plot to avoid fake spikes. Individual rows with valid telemetry still show their own stage analysis while the rest of the chain catches up.
- Verified with `cmake --build build --target VXStudioAnalyserPlugin -j4` and `./build/VXDeverbTests`.

# Analyzer sidebar stability + spectrum crossover pass — 2026-03-19

## Problem
The analyser is very close now, but the live chain list still flickers and occasionally drops visible VX stages, and the spectrum still diverges from SPAN in two practical ways: the low end is visually underrepresented, and the wet/dry crossover is harder to read than it should be.

## Plan
- [x] Add a short-lived sidebar snapshot cache so the live VX chain remains stable through brief publish gaps instead of flickering like raw telemetry.
- [x] Correct the analyser display slope so low frequencies are not visually suppressed relative to the SPAN reference.
- [x] Improve the spectrum overlay with clearer wet fill and additive/subtractive crossover shading.
- [x] Rebuild `VXStudioAnalyserPlugin` and rerun `VXDeverbTests`.

## Review
- `Source/vxsuite/products/analyser/VXStudioAnalyserEditor.*` now keeps a short sidebar snapshot cache with a `3500 ms` hold, so brief snapshot dropouts no longer immediately remove a live VX stage row from the chain list.
- The analyser display slope was corrected to apply in the same direction as a conventional analyzer tilt. The previous sign error visually pushed lows down and highs up, which is why the low end looked weaker than SPAN even when the underlying signal was present.
- The spectrum paint path now adds a clearer wet fill plus differential crossover shading: additive regions are highlighted separately from subtractive regions, so it is much easier to see where wet rises above or falls below dry.
- Verified with `cmake --build build --target VXStudioAnalyserPlugin -j4` and `./build/VXDeverbTests`.

# Analyzer snapshot robustness + FFT contour correction — 2026-03-19

## Problem
The analyser was still missing active VX effects in the left panel, and the spectrum contour remained materially different from the SPAN reference. The evidence pointed to two backend issues: snapshot publishing was less robust than stage telemetry and could fail to rejoin after an initial registration miss, and the spectrum path was still using a packed real-FFT interpretation that could skew the displayed contour.

## Plan
- [x] Make the shared snapshot publisher retry slot registration during normal publish/silence updates instead of only at construction/prepare time.
- [x] Switch the analyser spectrum accumulation to JUCE's frequency-only FFT path so the display uses direct magnitude bins instead of hand-reading the packed real-transform buffer.
- [x] Keep the sidebar as a union of snapshot rows plus any unmatched live telemetry rows so active/silent VX plugins are less likely to disappear from the chain list.
- [x] Rebuild `VXStudioAnalyserPlugin` and rerun `VXDeverbTests`.

## Review
- `Source/vxsuite/framework/VxSuiteSpectrumTelemetry.cpp` now retries `SnapshotPublisher::ensureRegistered()` from both `publish()` and `publishSilence()` whenever the snapshot slot is unavailable, rather than silently staying missing for the rest of the session.
- `Source/vxsuite/framework/VxSuiteSpectrumTelemetry.cpp` now uses `performFrequencyOnlyForwardTransform()` for the analyzer spectrum path and reads direct magnitude bins from that result. This removes the previous manual interpretation of the packed real FFT buffer, which was a plausible source of the contour mismatch versus SPAN.
- `Source/vxsuite/products/analyser/VXStudioAnalyserEditor.cpp` now includes unmatched live telemetry stages in the sidebar even if there is no corresponding snapshot row, and silent stages are retained in the live stage pool instead of being dropped outright.
- Verified with `cmake --build build --target VXStudioAnalyserPlugin -j4` and `./build/VXDeverbTests`.

# Cleanup high-shelf control fix — 2026-03-19

## Problem
The user reported that the `Cleanup` high-shelf control did not seem to do much. Inspection showed that the UI toggle and parameter wiring were present, but the shared corrective DSP never actually used `hiShelfOn`, so the control was effectively a no-op.

## Plan
- [x] Trace the `Cleanup` high-shelf toggle from processor params into the shared corrective DSP.
- [x] Add a real high-shelf stage in the corrective DSP so enabling the control produces a meaningful top-end cleanup change.
- [x] Rebuild `VXCleanupPlugin` and rerun the standing regression check.

## Review
- `Source/vxsuite/products/polish/dsp/VxPolishDspCommon.h` now provides a reusable RBJ-style high-shelf biquad helper.
- `Source/vxsuite/products/polish/dsp/VxPolishCorrectiveStage.*` now owns a dedicated high-shelf filter/state and applies it when `params.hiShelfOn` is enabled. The shelf amount is derived from the current `deEss` and `troubleSmooth` drive, so the icon now has an audible cleanup effect instead of being dead wiring.
- Verified with `cmake --build build --target VXCleanupPlugin -j4` and `./build/VXDeverbTests`.

# Analyser CPU + memory efficiency pass — 2026-03-19

## Problem
The analyser is now functionally close, but the user explicitly asked to make it as CPU- and memory-efficient as possible. The hot paths still included avoidable work: copying the full FFT scratch buffer every analysis hop, allocating/sorting extra data in sparse-spectrum classification, repeated string normalization in sidebar matching, and redundant path/snapshot-string construction in the editor.

## Plan
- [x] Remove avoidable large-buffer copying from the shared spectrum telemetry FFT path.
- [x] Trim editor-side allocations and repeated string/path work in the analyser refresh/paint loop.
- [x] Rebuild `VXStudioAnalyserPlugin` and rerun the standing regression check.

## Review
- `Source/vxsuite/framework/VxSuiteSpectrumTelemetry.cpp` no longer copies the entire `fftData` buffer into a scratch array on every FFT hop. The analyser now fills and transforms the persistent buffer in place, which removes one full-size copy from the hottest telemetry path.
- `Source/vxsuite/products/analyser/VXStudioAnalyserEditor.cpp` now avoids the per-frame `std::vector` allocation/sort previously used to compute sparse-spectrum top energy. The classifier keeps the top four energies in a fixed array during a single pass instead.
- The editor refresh path now reserves stage/snapshot vectors up front, precomputes normalized external-stage match keys once per refresh, skips building the joined snapshot diagnostics list unless diagnostics are actually open, and reuses dry/wet spectrum paths within `paint()` instead of rebuilding them multiple times per frame.
- Verified with `cmake --build build --target VXStudioAnalyserPlugin -j4` and `./build/VXDeverbTests`.

# Analyser responsive text/layout pass — 2026-03-19

## Problem
The user asked to make sure the analyser text fits inside its boxes and that the UI responds cleanly at different sizes. The editor still had a few fixed-height/fixed-row assumptions that could make summary text and controls feel cramped, especially in narrower layouts.

## Plan
- [x] Tighten the analyser label scaling and stage-row text layout so long text fits more reliably.
- [x] Make the controls row wrap more gracefully in narrower content widths instead of crowding into one fixed strip.
- [x] Rebuild `VXStudioAnalyserPlugin` and rerun the standing regression check.

## Review
- `Source/vxsuite/products/analyser/VXStudioAnalyserEditor.cpp` now gives the subtitle, status, selection, and summary labels more tolerant horizontal scaling so long strings stay within their cards more reliably.
- Stage-row secondary text now uses a slightly smaller font and can wrap across two lines instead of truncating too aggressively in the rail.
- The top summary card gains a little extra height in narrower layouts, and the analyser control strip now splits into two rows when the content area is tight instead of forcing `Avg Time`, `Smoothing`, and `Hide Chain` into one crowded line.
- Verified with `cmake --build build --target VXStudioAnalyserPlugin -j4` and `./build/VXDeverbTests`.

# VXDenoiser vocal-mode amount fix — 2026-03-19

## Problem
The user reported that `VXDenoiser` in vocal mode appeared to do nothing. Inspection showed that the processor was scaling the vocal-mode clean amount down to only `8%` of the knob value before it ever reached the DSP, which effectively neutered the denoiser even at high settings.

## Plan
- [x] Inspect the vocal/general clean-amount mapping in `VXDenoiser`.
- [x] Correct the vocal-mode amount law while preserving the existing vocal protection/guard behavior.
- [x] Rebuild `VXDenoiserPlugin` and rerun the standing regression check.

## Review
- `Source/vxsuite/products/denoiser/VxDenoiserProcessor.cpp` now maps vocal-mode `Clean` to `0.55 * smoothedClean` instead of `0.08 * smoothedClean`. The DSP is now fed a meaningful amount in vocal mode while the existing `sourceProtect`, `guardStrictness`, and `speechFocus` safeguards remain in place.
- Verified with `cmake --build build --target VXDenoiserPlugin -j4` and `./build/VXDeverbTests`.
- [x] Build the affected targets and document the first implementation checkpoint.
- [x] Replace the old `VXSpectrum` product target/files with `VXStudioAnalyser` in the build and staged plugin output.
- [x] Replace the carried-over spectrum UI with a real stage-based `VXStudioAnalyser` shell built on the new analysis telemetry.

## Review
- Added `docs/VX_ANALYZER_REBUILD_SPEC.md` to define `VX Studio Analyser` as a separate analysis layer built on thin per-stage telemetry rather than product-local visualization logic.
- The spec replaces the current spectrum-centric `SnapshotView` model with a stage-oriented schema that separates identity, live state, bypass state, silence, lightweight summaries, capabilities, and deep-inspection requests.
- The design keeps `ProcessorBase` as the framework injection point, which means the rebuild stays aligned with VX Suite's shared-framework rule instead of creating new product-specific telemetry paths.
- The proposed two-tier model is safer than extending the current analyzer because it removes dependence on registration order and waveform-correlation heuristics as the primary chain model. Those can remain as fallback confidence tools rather than being treated as truth.
- The product direction is now explicit: this is a replacement of `VXSpectrum`, not a light refresh. The rewrite should land as `VX Studio Analyser` with a new product contract and UI model.
- The implementation sequence is intentionally phased: schema/transport first, framework publisher second, analyzer backend third, UI fourth, and deep inspection last. That should let us migrate without trying to replace the whole analyzer stack in one unsafe pass.
- First implementation checkpoint is in place: `ProductIdentity` now carries stage-analysis metadata, the framework has a new `vxsuite::analysis` telemetry ABI/domain registry/stage registry beside the legacy spectrum transport, and `ProcessorBase` now auto-publishes the new stage telemetry through `analysis::StagePublisher`.
- The new Tier 1 summary path is intentionally foundational rather than final-quality: it publishes aligned input/output summaries with incremental envelope/RMS/peak/transient/stereo metrics, while coarse spectrum bins are still placeholder-filled pending the dedicated band accumulator pass.
- Verified with `cmake --build build --target VXSpectrumPlugin VXCleanupPlugin -j4`. Both rebuilt successfully; the only output noise was the existing ad-hoc code-signing/staging warning during VST3 restaging.
- Replaced the old `Source/vxsuite/products/spectrum/` product with `Source/vxsuite/products/analyser/`, switched the CMake target/bundle from `VXSpectrum` to `VXStudioAnalyser`, registered analyzer-domain authority in the analyzer processor, and removed the staged `Source/vxsuite/vst/VXSpectrum.vst3` bundle.
- Verified the replacement with `cmake -S . -B build`, `cmake --build build --target VXStudioAnalyserPlugin -j4`, and `cmake --build build --target VXSuite_VST3 -j4`. The suite now stages `VXStudioAnalyser.vst3` instead of `VXSpectrum.vst3`.
- Rewrote `Source/vxsuite/products/analyser/VXStudioAnalyserProcessor.*` and `Source/vxsuite/products/analyser/VXStudioAnalyserEditor.*` so the product is no longer the old spectrum overlay view under a new name. The analyzer is now domain-scoped, stage-list driven, and shows `Before / After / Delta` scopes across `Tone`, `Dynamics`, and `Diagnostics`.
- The new analyzer shell reads `vxsuite::analysis::StageRegistry` instead of the legacy `spectrum::SnapshotRegistry`, orders stages by domain + `localOrderId`, supports single-stage or full-chain scope, and derives stage impact/class labels from the new Tier 1 summaries.
- Verified the rewritten analyzer with `cmake --build build --target VXStudioAnalyserPlugin -j4`. Current known limitation: the Tone view is structurally correct but still depends on placeholder Tier 1 spectrum bins from the first-pass accumulator, so the next cleanup should upgrade those bins from flat placeholders to proper coarse-band energy summaries.
- Replaced the placeholder spectrum fill in `Source/vxsuite/framework/VxSuiteSpectrumTelemetry.cpp` with a preallocated coarse FFT summary over a rolling mono window. Tier 1 spectrum bins are now derived from real band energy rather than `out.rms` replicated across every bin, so the analyzer traces can reflect actual tonal shape changes.
- Verified the spectrum summary upgrade with `cmake --build build --target VXStudioAnalyserPlugin VXCleanupPlugin -j4`.
- Rebuilt `VXStudioAnalyserEditor` around the definitive UI model: deterministic stage chain on the left, summary strip as the primary explanation surface, exactly one graph at a time, `Tone` and `Dynamics` tabs only, and diagnostics moved into a collapsible bottom panel.
- Tone now renders like a proper change-view: log-frequency x-axis, centered dB y-axis, delta-first filled presentation, faint before/after context, low/mid/high guides, and a largest-change marker. Dynamics now renders as before/after dBFS level history instead of an unlabeled abstract line plot.
- Verified the spec-driven UI rewrite with `cmake --build build --target VXStudioAnalyserPlugin -j4`.

# Finish/Opto idle telemetry fix — 2026-03-18

## Problem
`VXFinish` and `VXOptoComp` showed the `Opto` activity LED as active even in their nominal idle/open state. The shared compressor stage was still eligible to compute gain reduction at zero amount, and both products also inherited the framework's default `primaryDefaultValue = 0.5f`, so they opened half-engaged.

## Plan
- [x] Inspect the shared opto telemetry path and confirm whether the issue lived in DSP logic, product defaults, or both.
- [x] Patch the shared opto stage and product identities so zero amount is truly idle and telemetry reflects actual engaged compression.
- [x] Add or update regression coverage, build the affected targets, and document the result.

## Review
- Set `VXFinish` and `VXOptoComp` primary defaults to `0.0f` so both products now open with zero compression instead of inheriting the framework's generic midpoint default.
- Patched `Source/vxsuite/products/OptoComp/OptoCompressorLA2A.cpp` so `Peak Reduction = 0` forces zero target gain reduction and zero activity telemetry, while still allowing neutral gain/body handling to remain transparent.
- Fixed the centered `Gain` mapping in both processors so `0.5` is truly neutral; they were previously using the wrong `juce::jmap` overload and landing at `-6 dB` in the default state.
- Added focused regression coverage for zero-amount transparency and zero telemetry in both `Finish` and `OptoComp`, and updated the regression target to link the standalone `VXOptoComp` wrapper.
- Verified with `cmake --build build --target VXSuitePluginRegressionTests -j4 && ./build/VXSuitePluginRegressionTests` plus `cmake --build build --target VXFinishPlugin VXOptoCompPlugin -j4`. Both plugin targets rebuilt and restaged successfully, with only the existing ad-hoc code-signing warnings during VST3 staging.

# Cleanup slope toggle fix — 2026-03-18

## Problem
The `VXCleanup` slope icons were not reliably clickable because the shared editor rendered them in the label area, but those non-interactive labels and meter widgets could still intercept mouse events before the editor-level hit test ran.

## Plan
- [x] Inspect the shared editor hit-testing path and confirm whether child components were blocking the slope icon clicks.
- [x] Patch the shared framework editor so display-only widgets stay mouse-transparent and do not break shelf/slope toggles.
- [x] Build and verify the affected target or framework path, then document the result.

## Review
- Patched `Source/vxsuite/framework/VxSuiteEditorBase.cpp` so display-only labels and the learn meter explicitly call `setInterceptsMouseClicks(false, false)`, allowing editor-level shelf-icon hit testing to work as intended.
- The fix stays in the shared editor rather than adding a `VXCleanup`-specific workaround, so any current or future products using inline shelf icons inherit the corrected behavior.
- Verified with `cmake --build build --target VXCleanupPlugin -j4`; the plugin rebuilt and restaged successfully, with only the existing ad-hoc code-signing warning during VST3 staging.

# VX Spectrum implementation — 2026-03-17

# Per-effect audit fix pass — 2026-03-18

## Problem
Implement the audit fixes across each VX Suite effect individually, keeping changes consistent with the shared framework and with the expectation that the products work well together in a chain. Leave `VXDeepFilterNet` until last because its lifecycle/threading work is the riskiest.

## Plan
- [x] Update the task log and re-read the framework/lessons that constrain the fix pass.
- [x] Patch shared framework issues first: aligned-dry handling for oversized blocks, bypass-state behavior, and any helper cleanup needed by multiple products.
- [x] Fix `VXDeverb` against the updated framework and add focused regression coverage.
- [x] Fix `VXDenoiser` and `VXSubtract` latency/state correctness issues and add focused regression coverage.
- [x] Fix additive/listen/headroom consistency in `VXTone`, `VXProximity`, `VXFinish`, `VXOptoComp`, and any `VXCleanup` cleanup that should be aligned with framework usage.
- [x] Fix `VXDeepFilterNet` last: runtime lifecycle, thread safety, and wet/dry semantics.
- [x] Build and run the affected regression targets, then document the final results and residual risks.

## Spec Notes
- Framework changes should solve shared problems centrally rather than adding per-product defensive hacks where possible.
- Additive products should share consistent listen and headroom semantics.
- Output trimming remains an emergency guard, not the primary fix for gain-staging issues.
- DeepFilterNet work should preserve suite conventions where possible, but correctness and realtime safety take priority.

## Review
- Implemented the shared framework fix in `ProcessorBase` so oversized host blocks are chunked through the prepared scratch/block-size contract, bypass continues to advance latency-aligned dry state, and additive products can render consistent delta-audition output via a shared helper.
- Fixed `VXDeverb` by removing dead stage plumbing, moving RT60 tracking to per-channel estimators, and replacing the unstable-output fallback with a deterministic dry fallback for the offending block.
- Fixed `VXDenoiser` and `VXSubtract` so zero-strength paths explicitly return the framework-aligned dry signal, preserving reported latency/PDC behaviour instead of relying on latent DSP to reconstruct dry. `VXSubtract` state restore now rejects incompatible learned-profile formats and keeps Learn armed across reset.
- Aligned additive/listen semantics across `VXTone`, `VXProximity`, `VXFinish`, and `VXOptoComp` with the framework additive-delta helper, and reduced product-specific gain ranges where the audit showed output-trim-dependent headroom.
- Adjusted `VXCleanup` FFT-order selection to scale with sample rate instead of assuming a fixed 48 kHz analysis window.
- Fixed `VXDeepFilterNet` last by refactoring the service onto double-buffered runtime bundles, making model/runtime swaps API-consistent, avoiding audio-thread runtime teardown/reset, and restoring proper low-strength wet/dry behaviour with latency-aligned dry blending.
- Added regression coverage for zero-strength PDC alignment, mismatched Subtract profile restore, Learn-after-reset, Tone additive listen semantics, and oversized host-block processing through the shared framework path.
- Verification completed with `cmake --build build --target VXSuitePluginRegressionTests -j4`, `./build/VXSuitePluginRegressionTests`, and a full `cmake --build build -j4` project build. Residual note: the full build emits the existing macOS ad-hoc signing warnings while staging VST3 bundles, but the build completed successfully.

# REAPER preset pack — 2026-03-18

## Problem
Add a documented REAPER-facing preset pack for VX Suite: per-effect `.RPL` preset libraries with the same scenario names across products, plus matching `.RfxChain` scenario chains and README guidance for when to use each one.

## Plan
- [x] Add the task log entry and capture the REAPER API lesson from the failed probe.
- [x] Define four shared scenarios and per-effect starting values that stay consistent with the framework and with realistic chain usage.
- [x] Add a reproducible generator for REAPER `.RPL` libraries and `.RfxChain` files.
- [x] Generate and commit the preset pack outputs under a stable repo path.
- [x] Update `README.md` with scenario descriptions, recommended signal chains, and preset-pack usage notes.

## Review
- Added a repo-local REAPER preset generator at `tools/reaper/generate_vx_reaper_presets.lua` that uses the installed/staged VX Suite VST3s inside REAPER to emit per-effect `.RPL` libraries and scenario `.RfxChain` files.
- Generated the preset pack under `assets/reaper/`, with one `.RPL` file per effect in `assets/reaper/RPL Files/` and four scenario chains in `assets/reaper/FX Chains/`.
- Standardized four shared scenario names across all effect libraries: `Camera Review - Far Phone`, `Live Music - Front Of Room`, `Podcast Finishing - Clean Voice`, and `Mixed Audio - Voice + Guitar`.
- Updated `README.md` and added `assets/reaper/README.md` so the repo now documents what each scenario is for and which chain to start with.
- Captured the REAPER API lesson from the failed probe: REAPER Lua parameter/preset helpers do not always mirror the return conventions implied by generic docs or other language bindings, so probing return values directly is safer than assuming tuple layouts.

# Full per-effect audit sweep — 2026-03-18

## Problem
Review every VX Suite effect individually against the strict JUCE/VST3 + VX Suite audit prompt, then produce a separate report for each effect covering correctness, safety, efficiency, framework usage, host behavior, and validation gaps.

## Plan
- [x] Re-read the relevant project lessons and VX Suite framework rules, then inventory every effect target and its DSP/framework dependencies.
- [x] Audit the shared VX Suite framework paths that affect all effects: lifecycle, smoothing, listen, output trim, latency, buffer handling, and state/host semantics.
- [x] Perform an individual code audit for each effect product: `VXCleanup`, `VXDeverb`, `VXDenoiser`, `VXDeepFilterNet`, `VXFinish`, `VXOptoComp`, `VXProximity`, `VXSubtract`, and `VXTone`.
- [x] Decide whether `VXSpectrum` belongs in the effect audit set; if not, exclude it explicitly from the reports with a reason.
- [x] Write one report per audited effect in the requested format: executive summary, findings table, framework cleanup list, test matrix, and patch recommendations.
- [x] Review the reports for consistency, rank the highest-severity risks, and document any cross-product framework drift or repeated failure patterns.

## Spec Notes
- Release readiness is the bar; missing verification counts as risk, not a pass.
- Reports should stay effect-specific even when the root cause lives in shared framework code.
- Framework responsibilities must not be re-audited as if each product were raw JUCE unless the product is bypassing the framework.
- Minimal, surgical remediation suggestions come first; redesigns should be called out separately.

## Review
- Reports added under `tasks/reports/effect-audits/` for `Cleanup`, `Deverb`, `Denoiser`, `DeepFilterNet`, `Finish`, `OptoComp`, `Proximity`, `Subtract`, and `Tone`.
- `VXSpectrum` was explicitly excluded because it is an analysis product rather than an audio effect, though the shared framework review still flagged its telemetry path as a realtime risk.
- Highest-severity cross-product issues:
- The framework dry/aligned-listen path is not safe when hosts deliver larger-than-prepared blocks, which becomes a release-blocking bug in `VXDeverb` and a stale-state risk for other latency-bearing products.
- `VXDeepFilterNet` has release-blocking thread-safety and lifecycle issues around runtime preparation/reset.
- `VXDenoiser` and `VXSubtract` both have host-PDC mismatches when their effective wet amount reaches zero while latency remains reported.
- Repeated architecture drift:
- Output trimming is being used in multiple products as a secondary safety net without proof that the main DSP path is already gain-stable.
- Additive-product listen and wrapper logic are still duplicated instead of centralized.

# Cleanup + Finish opto integration review — 2026-03-18

## Problem
Review the new `optocomp` compressor integration for `VXFinish`, identify any correctness/build risks first, then apply the `clean and finish.md` product-split upgrades without breaking the new compressor path.

## Plan
- [x] Review the current `VXFinish` integration, the new `OptoComp` files, and the `clean and finish.md` intent document.
- [x] Capture the concrete integration issues and fix the build/runtime wiring so `VXFinish` really uses the new opto compressor.
- [x] Align `Cleanup` and `Finish` behavior with the requested split: Cleanup stays subtractive/protective, Finish stays additive/leveling.
- [x] Build and verify the affected targets, then document the final review and any residual risks.

## Spec Notes
- `Cleanup` must remain subtractive and protective: no compression, no loudness shaping, no guessing at what `Finish` should add back.
- `Finish` must stay focused on opto compression, light body shaping, and final level control.
- The new compressor should be preserved as the core `Finish` dynamics stage rather than replaced with the older shared polish chain.
- Any integration fix should be minimal and framework-friendly: no product-specific forks unless required for correctness.

## Review
- Review findings first:
- `VxFinishDsp.h` included `OptoCompressorLA2A.h` with no reachable path, so the new compressor could not compile at all.
- `VxFinishProcessor.cpp` switched over to a nonexistent `dsp` member while the class still owned `polishChain`, so the product would still fail after the include issue.
- `VxFinishDsp.cpp` was a stale pre-opto implementation that no longer matched the header, which meant the new compressor path was only half-integrated.
- `OptoCompressorLA2A.cpp` stored body-shelf state in `static thread_local` storage, so reset behavior was not instance-deterministic and could leak state across plugin instances on the same thread.
- Fixes landed:
- Rewired `VXFinish` so it now consistently drives the new `OptoComp` LA-2A-style compressor through `vxsuite::finish::Dsp`.
- Replaced the stale `VxFinishDsp.cpp` implementation with a focused Finish path: opto compression, light adaptive makeup, clean peak limiting, and final trimming.
- Kept the product split sharp: `Cleanup` remains the subtractive/protective stage, while `Finish` no longer depends on the old polish-analysis/recovery path.
- Made the compressor body shelf instance-owned and resettable instead of `thread_local`, then added a regression test to lock that behavior in.
- Verification:
- `cmake --build build --target VXSuitePluginRegressionTests -j4`
- `./build/VXSuitePluginRegressionTests`
- `cmake --build build --target VXFinishPlugin -j4`
- Residual risk:
- `Finish` now follows the requested architecture much more closely, but the adaptive makeup stage is intentionally simple and heuristic rather than analysis-driven. That keeps the new compressor path intact and predictable, but there is still room for future tuning by ear in-host.

# OptoComp standalone plugin — 2026-03-18

## Problem
Expose the new opto compressor as its own VX Suite plugin as well, without forking the compressor DSP away from the `Finish` path.

## Plan
- [x] Inspect the existing VX plugin shell pattern and choose the lightest reusable wrapper shape.
- [x] Add a dedicated `OptoComp` processor that reuses the shared finish/opto DSP path.
- [x] Register a new `VXOptoComp` VST3 target and include it in suite staging.
- [x] Build and stage the standalone plugin.

## Review
- Added a new thin product wrapper at `Source/vxsuite/products/OptoComp/VxOptoCompProcessor.{h,cpp}` instead of copying DSP code.
- `VXOptoComp` reuses `vxsuite::finish::Dsp`, so the compressor behavior stays shared between the standalone compressor and `Finish`.
- The standalone plugin exposes dedicated product identity and wording: `Peak Red.`, `Body`, and `Gain`, with the same vocal/general mode mapping and listen-delta semantics.
- Registered the new VST3 target in `CMakeLists.txt`, added it to the suite aggregate target, and staged it to `Source/vxsuite/vst/VXOptoComp.vst3`.
- Verified with `cmake -S . -B build && cmake --build build --target VXOptoCompPlugin -j4`.

## Problem
Build a dedicated VX analyzer plugin that can show dry/wet spectrum overlays plus one coloured trace per active VX-family plugin, while moving the shared snapshot engine into the framework so current and future plugins can publish telemetry without duplicating analysis code.

## Plan
- [x] Review VX Suite framework rules, research notes, and project lessons related to analysis, FFT ownership, and realtime/UI separation.
- [x] Inspect the current framework and processor architecture for shared FFT, stage-chain, and chain-visibility hooks.
- [x] Define a realistic v1 scope: standalone analyzer product, framework snapshot publisher, spectrum-first UI, waveform deferred or secondary.
- [x] Add a realtime-safe shared snapshot publisher to the VX Suite framework and auto-wire it into `ProcessorBase`.
- [x] Add a new analyzer product/plugin that renders dry, wet, and per-plugin spectrum traces with visibility toggles.
- [x] Build and verify the new plugin and the existing suite products that now publish telemetry.

## Review
- The idea is technically possible, but not as a passive VST that automatically inspects sibling plugins. The framework needs an explicit shared telemetry path.
- Added a framework-owned snapshot registry/publisher and wired it into `ProcessorBase`, so existing VX Suite products now publish compact dry/wet waveform telemetry automatically without product-local analyzer code.
- After the first build proved bundle-local only, replaced the registry backend with a shared-memory mapped transport so separate VX plugin bundles can see the same telemetry pool instead of each keeping a private static registry.
- Added a dedicated `VXSpectrum` plugin with a custom editor that renders the inferred chain dry bed, the analyzer input/final wet spectrum, and one coloured trace per active VX-family plugin with visibility toggles.
- The analyzer computes FFT visuals on the UI side from published waveform snapshots, keeping the heavier spectral rendering work out of the product audio path while the framework publishers stay preallocated, fixed-size, and lock-free during block publication.
- Verified with `cmake -S . -B build`, `cmake --build build --target VXSpectrum VXCleanup -j4`, `cmake --build build --target VXSpectrumPlugin VXDenoiser_VST3 -j4`, and `cmake --build build --target VXSuite_VST3 -j4` after the shared-memory backend landed.
- Remaining v1 limitation: chain ordering is still inferred from active VX publisher order rather than explicit host graph routing, so waveform view and more explicit chain/session routing are the next clean upgrades after we host-smoke-test the current analyzer.

# VX Spectrum pickup + comparison pass — 2026-03-17

## Problem
`VXSpectrum` still stops showing upstream VX plugins reliably after the pause/bypass work, and the live traces move too quickly to make useful visual comparisons in real time.

## Plan
- [x] Review the current telemetry publish/reset path and the chain matcher together to find why active upstream plugins are being dropped.
- [x] Make the analyzer resilient to pause/bypass-induced silence snapshots without showing stale garbage when a plugin is actually gone.
- [x] Improve the real-time comparison view so traces are visually stable enough to compare, while keeping the analyzer lightweight and off the audio path.
- [x] Build and run targeted verification for `VXSpectrum` and the shared telemetry framework.

## Review
- Root cause was split across the transport and UI: bypassed/paused plugins were still holding active telemetry slots with silent snapshots, and the analyzer was treating those zeroed frames as current truth when reconstructing the upstream chain.
- The shared telemetry slot now carries `silent` plus `lastPublishMs`, so paused publishers are still visible to the backend but can be reused safely when they stay silent. `SnapshotPublisher` also re-registers lazily if it loses a slot, which prevents `VXSpectrum` from getting stuck in a no-self-slot state after the pool fills.
- `VXSpectrum` now keeps a short UI-side hold on the last non-silent snapshot for each slot instead of immediately trusting a single silent frame, which makes chain pickup much less fragile around pause/bypass transitions.
- The display is now intentionally stabilised: spectrum lines use short attack/release smoothing so users can compare shapes in real time without chasing raw per-refresh jitter.
- A second matcher bug showed up in host testing: the analyzer was treating telemetry registration order like chain order. That is not a real DAW routing signal, so the matcher now uses signal-first correlation with order only as a weak hint instead of a hard gate.
- Cleaned up the no-match state so it renders as a small banner rather than overlapping the plot content.
- Verified with `cmake --build build --target VXSpectrumPlugin -j4` and `cmake --build build --target VXSuite_VST3 -j4` so the rebuilt staged bundles in `Source/vxsuite/vst/` all speak the same telemetry format.


# VX Cleanup architecture review — 2026-03-17

## Problem
Review the current `VXCleanup` processor against the supplied classifier-focused design document, identify where the implementation is already strong, where event decisions are still coupled, and call out any realtime/smoothing/state concerns before refactoring.

## Plan
- [x] Read the current `VXCleanup` processor implementation and shared VX Suite constraints.
- [x] Map the current analysis/data flow from input to `polishChain.processCorrective()`.
- [x] Identify where breath, sibilance, plosive, and tonal decisions are currently coupled.
- [x] Check realtime-safety, parameter smoothing, and persistent-state handling in the current implementation.
- [x] Write a review summary with file/line references and note the most suitable refactor direction.

## Review
- The processor already follows a clean analysis-to-mapping-to-DSP flow: control smoothing, voice snapshot capture, tonal analysis, derived ratios/risk metrics, and a final `polish::Dsp::Params` handoff in one place.
- The strongest existing design choices are block-rate smoothing for the three user controls, reuse of shared voice-analysis context, and preserving a simple VX Suite front-end while keeping corrective mapping internal.
- The main architectural gap is classifier coupling: `lowTrouble`, `highTrouble`, and `cleanupDrive` currently act as shared upstream drivers for multiple downstream actions, so de-breath, de-ess, plosive control, and broad HF smoothing are not independently classified.
- The most important concrete coupling today is that `params.deEss`, `params.breath`, and `params.troubleSmooth` all derive significant authority from `highTrouble`, while `params.plosive` depends on `cleanupDrive`, which itself rises with either low- or high-band trouble.
- Realtime behavior looks disciplined in this file: no heap work in `processProduct`, `ScopedNoDenormals` is present, smoothing is block-based, and long-lived state is limited to the expected smoothed controls plus tonal-analysis state.
- The most meaningful next refactor would be to keep the current top-level processor shape but split Stage 2 into explicit per-event classifier signals (`breathness`, `sibilance`, `plosiveRisk`, `tonalMud`) before mapping them independently into `polish::Dsp::Params`.

# Shelf toggles + listen audit — 2026-03-16

## Problem
1. Polish shelf icons (low/high) are purely decorative — no click, no DSP effect.
2. Verify listen toggle is visible for all 4 products (denoiser, deverb, polish, proximity).

## Listen audit result
All 4 products set `listenParamId = "listen"` → framework shows button. ✓

## Plan — shelf toggles

### 1. `ProductIdentity` — add two optional param IDs
- `lowShelfParamId`  (empty = not used)
- `highShelfParamId` (empty = not used)

### 2. `createSimpleParameterLayout` — add bool params with default=true when set

### 3. `VxPolishProcessor.cpp`
- Set `identity.lowShelfParamId = "demud_on"`, `highShelfParamId = "deess_on"`
- In `processProduct()`: read flags; if `demud_on` false → `params.deMud = 0`; if `deess_on` false → `params.deEss = 0` (and zero `params.troubleSmooth` for high-shelf)

### 4. `EditorBase`
- In `paint()`: dim shelf icon (alpha 0.25) when its param is false
- Override `mouseDown()`: if click hits `lowShelfIconBounds` or `highShelfIconBounds`, toggle the parameter

---

# Legacy denoise review — 2026-03-17

## Problem
Review legacy denoise-related code such as `HandmadePrimary` and `DenoiseEngine`, decide whether it should be folded into VX Suite DSP or removed, and identify worthwhile denoise options for the main denoiser.

## Plan
- [x] Trace which legacy DSP files are still compiled or referenced.
- [x] Compare `Source/dsp/HandmadePrimary.*` against `Source/vxsuite/products/denoiser/dsp/VxDenoiserDsp.*`.
- [x] Check whether the legacy feature set includes useful capabilities missing from `VXDenoiser`.
- [x] Write a keep / migrate / remove recommendation with concrete next steps.

## Review
- `Source/dsp/DenoiseEngine.h`, `Source/dsp/DenoiseOptions.h`, `Source/dsp/ProcessOptions.h`, and `Source/dsp/AudioProcessStage.h` are effectively duplicate compatibility wrappers. They are not directly built by CMake and only exist to support `HandmadePrimary`, so they should not become shared VX Suite framework surface area.
- `Source/dsp/HandmadePrimary.cpp` is still live because `VXSubtract` compiles it directly. Removing the legacy DSP folder today would break `VXSubtract`.
- `HandmadePrimary` and `VxDenoiserDsp` share the same core blind denoise architecture: OM-LSA gain estimation, Martin minimum statistics, Bark transient protection, Bark masking floor, harmonic comb protection, LF temporal stability, and phase-coherent resynthesis.
- The genuinely unique legacy capability is learned-profile subtraction for `VXSubtract`: learn state, frozen profile persistence, confidence scoring, and subtract-specific gain shaping.
- Recommendation: do not merge all of `HandmadePrimary` into the main denoiser. Instead, move `HandmadePrimary` into `Source/vxsuite/products/subtract/dsp/` (or split out only reusable learned-profile helpers if a second product needs them) and retire the duplicate `Source/dsp/*` wrapper layer afterward.
- Recommendation: keep the shipping `VXDenoiser` focused on broadband denoise. Do not add product-specific subtract controls like profile learn, `subtract`, `sensitivity`, or `labRawMode` unless the denoiser product goal changes.
- Potential denoiser additions worth prototyping: optional fixed hum suppression for 50/60 Hz harmonics ahead of the denoiser sidechain, and an optional narrowband tonal/noise detector for whistle/whine cleanup. These are meaningfully different from the current broadband denoise path.
- Not recommended as separate denoiser options right now: "rumble" as a denoise feature, because that is better handled as simple high-pass or proximity/cleanup filtering; and "surgical denoise" as a UI promise, because the current two-knob VX Suite template does not support exposing detailed band editing honestly.

---

# Denoiser hum + narrowband — 2026-03-17

## Problem
Add useful mains hum and narrowband tonal cleanup to `VXDenoiser` without growing dead legacy code. Extract reusable spectral helpers instead of copying more `HandmadePrimary` logic into the suite path.

## Plan
- [x] Add or update task documentation and capture the user correction in `tasks/lessons.md`.
- [x] Extract shared spectral helper code from the duplicated denoise implementations.
- [x] Add hum and narrowband suppression to `VxDenoiserDsp` while preserving the two-knob product shape.
- [x] Reuse the extracted helper from `HandmadePrimary` so the legacy path sheds duplicated logic.
- [x] Build a focused target to verify the denoiser still compiles cleanly.

## Review
- Added shared spectral math in `Source/vxsuite/framework/VxSuiteSpectralHelpers.h` and reused it from both `VxDenoiserDsp` and `HandmadePrimary`, so the new denoiser work did not introduce another copy of Bark conversion / tonalness / phase-wrap logic.
- Added two internal cleanup stages to `VxDenoiserDsp`: mains-hum suppression that auto-tracks the stronger 50 Hz or 60 Hz harmonic family, and narrowband tonal suppression for steady whistle/whine-style peaks.
- Kept the existing two-knob denoiser product surface intact. The new stages are internal and respect the existing `Clean`, `Guard`, and mode shaping rather than adding more front-panel controls.
- Verified by building `VXDenoiser` and `VXSubtract` successfully with `cmake --build build --target VXDenoiser VXSubtract -j4`.

---

# VxDeepFilterNet — 2026-03-17

## Problem
Create a new VX Suite plugin called `VxDeepFilterNet` that uses the existing realtime DeepFilterNet work as a voice-only denoiser, keeps the standard two-knob UX, replaces the Vocal/General dropdown with `DeepFilterNet 2` / `DeepFilterNet 3`, and stays as realtime-safe and efficient as possible.

## Plan
- [x] Inspect the reusable realtime DeepFilter code in `VxCleaner` and map the minimal subset needed in `VxStudio`.
- [x] Generalize the framework's existing two-choice selector so products can supply custom labels instead of only Vocal/General.
- [x] Add a new `VxDeepFilterNet` product with a product-local realtime DeepFilter engine wrapper, two main knobs, listen mode, and DFN2/DFN3 selection.
- [x] Add CMake and dependency wiring for ONNX Runtime in this repo, with a safe compiled fallback when ORT or model assets are unavailable.
- [x] Build the new target and fix integration issues until it compiles cleanly.

## Review
- Added a new VX Suite product at `Source/vxsuite/products/deepfilternet/` with the standard two-knob / listen UX, but with a custom `Model` selector that exposes `DeepFilterNet 3` and `DeepFilterNet 2` instead of Vocal/General.
- Generalized the shared framework selector surface so products can provide custom labels and choice text without forking the editor or parameter layout.
- Imported the realtime DeepFilter service into this repo as product-local code and renamed it into a VX Suite-owned namespace so the new product no longer links back to sibling-repo code.
- Added local DeepFilter model assets under `assets/deepfilternet/models/` and bundled them into the VST3 resources, so the plugin owns its runtime assets inside `VxStudio`.
- Added ONNX Runtime detection and conditional build wiring in `CMakeLists.txt`, with a compiled fallback that still builds the plugin when ORT is unavailable.
- Verified by running `cmake -S . -B build`, `cmake --build build --target VXDeepFilterNet -j4`, `cmake --build build --target VXDeepFilterNet_VST3 -j4`, and `cmake --build build --target VXDenoiser VXSubtract -j4`.
- Current limitation: the realtime engine can run the bundled monolithic ONNX path today, which makes `DeepFilterNet 3` the active realtime option on this machine. The bundled `dfn2/` and `dfn3/` official model directories are packaged locally, but a dedicated multi-graph realtime backend is still needed before the selector can drive those bundle variants directly.

## Follow-up Plan — realtime DFN2/DFN3 backend
- [ ] Inspect the bundled `dfn2/` and `dfn3/` graph I/O so the runtime can wire encoder and decoder sessions correctly.
- [ ] Add a shared prepared realtime bundle backend that keeps persistent ONNX sessions and fixed-shape state buffers for `enc.onnx`, `erb_dec.onnx`, and `df_dec.onnx`.
- [ ] Route the `Model` selector to true realtime `DFN2` and `DFN3` execution instead of the current bundle-only status fallback.
- [ ] Reduce hot-path overhead by moving tensor metadata discovery and allocations into `prepareRealtime()`.
- [ ] Rebuild `VXDeepFilterNet` and `VXDeepFilterNet_VST3`, then confirm the default repo build still succeeds.

---

# VxDeepFilterNet runtime cleanup — 2026-03-17

## Problem
Replace the experimental ONNX/bundle backend with a fully local, production-shaped DeepFilter runtime path inspired by Alt-Denoiser, while keeping the implementation owned inside `VxStudio`, realtime-safe, and free from dead legacy layers.

## Plan
- [x] Re-pin the vendored `libDF` crate to a known-good upstream dependency graph and remove unneeded default-model embedding.
- [x] Clean up the `VxDeepFilterNet` C++ bridge so it targets the local runtime only, with fixed 48 kHz processing and local model extraction.
- [x] Verify `VXDeepFilterNet` builds cleanly, then run the normal repo build to ensure the product is part of the global path.
- [x] Add review notes covering the final architecture, remaining runtime limits, and follow-up work.

## Review
- `VxDeepFilterNet` now uses a fully local vendored DeepFilter runtime path: the plugin shell stays in VX Suite, the realtime engine runs fixed-rate 48 kHz processing through vendored `libDF`, and model assets remain owned by `VxStudio` under `assets/deepfilternet/models/`.
- The vendored Rust crate is pinned to the compatible `tract 0.21.4` stack and built with `--locked --no-default-features`, which avoids the broken dependency drift and removes the upstream default-model embed that was conflicting with our local asset ownership.
- Cargo output for the vendored runtime now goes into the normal CMake build tree instead of `ThirdParty/deep_filter/libDF/target/`, so the imported source stays source-only and the repo no longer accumulates generated runtime artifacts.
- The C++ service no longer depends on the old custom ONNX bundle path. It now extracts the selected model tarball locally, instantiates per-channel `libDF` state, resamples to 48 kHz with the vendored `Resampler`, and returns to host rate in the standard VX Suite processor flow.
- Verified with `cargo build --release --locked --no-default-features --manifest-path ThirdParty/deep_filter/libDF/Cargo.toml --features capi`, `cmake --build build --target VXDeepFilterNet -j4`, `cmake --build build --target VXDeepFilterNet_VST3 -j4`, and `cmake --build build -j4`.
- Remaining product caveat: the current runtime bridge is optimized around the official monophonic C API path, so `VxDeepFilterNet` is now cleanly realtime and locally owned, but deeper DFN2/DFN3 model-specific optimization work is still future tuning rather than unfinished architecture.

---

# VxDeepFilterNet DFN2 compatibility — 2026-03-17

## Problem
The modern vendored `libDF` runtime runs `DFN3`, but `DFN2` still crashes during model initialization even with the official `v0.3.1` model tarball. The product needs a real `DFN2` mode instead of a broken selector entry.

## Plan
- [x] Reproduce the `DFN2` initialization failure directly outside the plugin shell.
- [x] Vendor a `DFN2`-compatible upstream runtime locally and expose a distinct C API so it can coexist with the modern `DFN3` runtime.
- [x] Route `VxDeepFilterNet` to the correct runtime per model variant and keep both runtime build outputs out of the source tree.
- [x] Verify direct `DFN2` initialization plus `VXDeepFilterNet` and full-repo builds.

## Review
- Confirmed that the current vendored runtime still panics on `DeepFilterNet2_onnx.tar.gz`, so this was a real runtime compatibility issue rather than a UI or model-packaging bug.
- Added a second local vendored runtime under `ThirdParty/deep_filter_dfn2/` based on upstream `v0.3.1`, with a small local C shim exposing `df2_*` symbols so it can link alongside the current `df_*` runtime cleanly.
- Updated `VxDeepFilterNetService` to choose the runtime by model variant: `DFN3` uses the modern vendored runtime, `DFN2` uses the compatibility runtime, and both continue to share the same VX Suite processor shell and local resampling path.
- Moved both Rust runtime builds into `build/cargo/` so neither vendored source tree accumulates generated `target/` artifacts.
- Verified direct `DFN2` initialization with a local probe against `build/cargo/libdf_dfn2/release/libdf.a`, which returned the expected `480` frame length for `assets/deepfilternet/models/DeepFilterNet2_onnx.tar.gz`.
- Verified with `cmake --build build --target VXDeepFilterNet -j4` and `cmake --build build -j4` after wiring both runtimes into the main build.

---

# VXPolish tuning + slope info — 2026-03-17

## Problem
Make `VXPolish` more audible in practice, especially the compressor and limiter, show a clear info-line message when the slope icons are clicked, add a vocal-mode breath remover, and keep plosive/de-ess/breath cleanup more restrained in general mode.

## Plan
- [x] Inspect `VXPolish` DSP and editor code to locate dynamic thresholds, mode shaping, and slope click handling.
- [x] Retune `VXPolish` compression and limiting so the dynamics stages engage more readily.
- [x] Add an internal breath cleanup stage that is strongest in vocal mode and reduced in general mode.
- [x] Surface slope click feedback in the editor info/status area and verify the repo still builds cleanly.

## Review
- Increased the effective compressor and limiter drive by raising the processor-side control mapping and tightening the DSP-side thresholds, ratio, attack/release, and limiter ceiling so `Comp` and `Limit` are noticeably easier to trigger.
- Added an internal breath cleanup stage in `VxPolishDsp` that targets low-level airy high-band events; it is intentionally stronger in vocal mode and much lighter in general mode.
- Rebalanced cleanup strength so vocal mode remains the more assertive speech-focused setting, while general mode now backs off plosive, de-ess, and breath reduction to preserve wider-band material more naturally.
- Updated the shared editor so clicking the low/high slope icons now writes a short explanatory message into the existing top-right info/status line instead of silently toggling.
- Verified with `cmake --build build -j4`, which rebuilt `VXPolish`, `VXPolish_VST3`, and the full suite successfully.

---

# VXPolish smart gain + DFN2 LL — 2026-03-17

## Problem
Add a fourth `VXPolish` dial for smarter mode-aware gain lifting, surface a de-breath activity light, make demud/smoothing more obvious, and reduce the `DFN2` echo issue without harming the `DFN3` path.

## Plan
- [x] Extend the shared framework so a product can expose an optional fourth dial cleanly.
- [x] Wire `VXPolish` to use that fourth dial as a smart gain control that pushes the selective auto-gain/recovery path rather than plain broadband gain.
- [x] Add the de-breath LED and strengthen audible demud/intelligent smoothing behavior.
- [x] Switch `DFN2` to the low-latency export and verify the suite still builds cleanly.

## Review
- Extended the shared VX Suite framework with an optional quaternary control, including parameter layout, attachments, and 4-knob editor layout support.
- `VXPolish` now exposes a fourth `Gain` dial that increases the selective mode-aware recovery/auto-gain path rather than simply boosting overall level; vocal mode leans more into presence/air lift, while general mode leans relatively more into body/presence recovery.
- Added a `De-breath` activity light to `VXPolish` and kept the existing `De-ess`, `Plosive`, `Comp`, and `Limit` indicators.
- Increased `deMud` and `troubleSmooth` drive in the processor mapping so mud cleanup and intelligent smoothing are more noticeable in practice.
- Switched `DFN2` model selection to prefer `DeepFilterNet2_onnx_ll.tar.gz`, which avoids the older lookahead-offset path that was producing the echo-like artifact; `DFN3` remains on the standard current runtime/model path.
- Verified with `cmake -S . -B build` and `cmake --build build -j4`.

---

# VXSubtract learn/remove overhaul — 2026-03-17

## Problem
`VXSubtract` does not behave like a clear Audacity-style "learn profile, then remove profile" tool. Learn is destructive and auto-stops on hidden silence detection, while the subtract path is conservative enough that users can complete a learn and still hear too little removal.

## Plan
- [x] Make Learn non-destructive and explicit: keep the previous learned profile active until a new learn is completed successfully, and remove the hidden silence auto-stop behavior.
- [x] Rebalance `Subtract` so the learned profile removal is more audible, with `General` mode removing more aggressively and `Vocal` mode preserving speech/harmonics more intentionally.
- [x] Update status text and related processor state so the learn/remove workflow is understandable from the UI.
- [x] Build `VXSubtract` and the full repo, then document the final behavior and verification result.

## Review
- `VXSubtract` now behaves like an explicit profile-capture tool: `Learn` starts capturing noise and only locks the replacement profile when the user turns `Learn` off. The old profile stays intact during capture instead of being destroyed at learn start.
- Removed the hidden silence-driven auto-stop behavior in the processor, so learn no longer ends unexpectedly on short gaps or room-tone dips.
- Reworded the product status text so the UI now explains the intended flow: capture noise, click again to lock, then use `Subtract` to remove the learned profile.
- Rebalanced the learned subtract path in `HandmadePrimary` so profile removal is audibly stronger overall, with lower subtract floors and higher subtraction authority than before.
- Split the mode behavior more intentionally: `Vocal` now keeps stronger protection and a slightly softer subtract curve, while `General` applies a more aggressive learned-profile removal with much lighter protection throttling.

---

# Learn / Deverb / Cleanup / Finish bug-fix pass — 2026-03-17

## Problem
- `VXSubtract` Learn button does not reliably start learning.
- `VXDeverb` can produce humming / buzzing at extreme `Blend`.
- `VXCleanup` can clip and its EQ-style cleanup moves feel too subtle.
- `VXFinish` can clip, and `Finish` / `Body` / `Gain` are too weak in practice.

## Plan
- [x] Fix the Learn toggle edge handling and add regression coverage.
- [x] Stabilize `VXDeverb` at extreme `Blend` / wet settings and add a focused regression test.
- [x] Make `VXCleanup` more audible while adding output headroom protection.
- [x] Make `VXFinish` more audible, separate loudness push from clipping, and strengthen limiting.
- [x] Build the affected targets, run the regression harness, and document the final result.

## Review
- Fixed the `VXSubtract` Learn bug in `Source/vxsuite/products/subtract/VxSubtractProcessor.cpp` by latching the current Learn parameter state on reset instead of hard-forcing the toggle state, so the first press after prepare/reset now starts capture correctly.
- Hardened `VXDeverb` in `Source/vxsuite/products/deverb/VxDeverbProcessor.cpp` and `Source/vxsuite/products/deverb/dsp/VxDeverbSpectralProcessor.cpp` by reducing extreme oversubtraction/WPE drive, making body restore more conservative at high reduce settings, clamping unstable spectral values, and sanitizing the final output path so maxed settings no longer explode into buzzing or giant peaks.
- Retuned `VXCleanup` in `Source/vxsuite/products/cleanup/VxCleanupProcessor.cpp` so `deMud`, `deEss`, `breath`, `plosive`, and HF smoothing all have more audible authority, while a fast output trim guard now prevents the cleanup stage from clipping the result.
- Retuned `VXFinish` in `Source/vxsuite/products/finish/VxFinishProcessor.cpp` and `Source/vxsuite/products/finish/dsp/VxFinishDsp.cpp` so `Finish`, `Body`, and `Gain` move more clearly, target makeup is less reckless, the limiter reacts faster with an instantaneous peak guard, and the old hard-clipping behavior is replaced with a softer final safety path.
- Expanded the regression harness in `tests/VXSuitePluginRegressionTests.cpp` and `CMakeLists.txt` to cover first-press Learn startup, Deverb at extreme Blend settings, stronger Cleanup audibility without clipping, and stronger Finish control separation without clipping.
- Verified with `cmake --build build --target VXSuitePluginRegressionTests -j4`, `./build/VXSuitePluginRegressionTests`, and `cmake --build build -j4`.

### Learn UX follow-up
- Updated the shared Learn UI in `Source/vxsuite/framework/VxSuiteEditorBase.cpp` and the Subtract status copy in `Source/vxsuite/products/subtract/VxSubtractProcessor.cpp` to follow common capture UX patterns: explicit start/stop wording, guidance to capture pure background noise for roughly 1 to 2 seconds, and confidence text framed as noise-print quality rather than abstract certainty.
- Verified with `cmake --build build --target VXSubtract VXSubtract_VST3 VXSuitePluginRegressionTests -j4`.
- Increased the learn target window and reduced the variance padding applied when freezing the learned profile, making the result closer to an Audacity-style learned noise estimate instead of an overly padded conservative profile.
- Verified with `cmake --build build --target VXSubtract -j4` and `cmake --build build -j4`.

---

# Denoiser latency + DFN2 guard pass — 2026-03-17

## Problem
The standard denoiser feels echoey on the wet path, and `VXDeepFilterNet` `DFN2` becomes robotic when `Guard` is raised. Both issues point to protection/latency behavior that sounds wrong in realtime use.

## Plan
- [x] Inspect the standard denoiser latency path and reduce the perceived delay without breaking the realtime-safe design.
- [x] Rework `DFN2` guard handling so protection no longer relies on a strong dry reblend that causes combing/robotic tone.
- [x] Build the affected targets and the full repo.
- [x] Document the final behavior and verification result.

## Review
- Reduced the standard denoiser STFT size from `2048/512` to `1024/256`, cutting the algorithmic latency from about `32 ms` to about `16 ms` while keeping the same overlap/WOLA structure and realtime-safe processing model.
- This does not make the spectral denoiser zero-latency, but it materially reduces the delayed-wet feel that could present as an echo on monitored material.
- Reworked `VXDeepFilterNet` guard behavior so `DFN2` no longer uses post-model dry/wet recovery, which was causing comb-filter/robotic artifacts when protection was raised.

---

# Legacy DSP removal + migration — 2026-03-17

## Problem
Remove the remaining legacy `Source/dsp/` code by moving the still-live subtract implementation into the VX Suite subtract product, replacing wrapper-only legacy process option/stage headers with the shared framework equivalents, and deleting the obsolete legacy files.

## Plan
- [x] Read project lessons and VX Suite framework guidance relevant to cleanup/corrective products.
- [x] Audit remaining legacy DSP files and identify which ones are still live.
- [x] Confirm the wrapper-only legacy headers duplicate framework-owned concepts.
- [x] Move the live subtract DSP into `Source/vxsuite/products/subtract/` and update product wiring.
- [x] Delete obsolete legacy wrapper headers/files and verify the repo still builds.

## Review
- Moved the only still-live legacy DSP implementation out of `Source/dsp/` and into the subtract product itself:
  - `Source/vxsuite/products/subtract/dsp/VxSubtractDsp.h`
  - `Source/vxsuite/products/subtract/dsp/VxSubtractDsp.cpp`
- Updated `VXSubtract` to own that DSP locally instead of reaching back into a legacy folder. The processor now includes `dsp/VxSubtractDsp.h`, uses `vxsuite::subtract::SubtractDsp`, and passes the shared `vxsuite::ProcessOptions` directly.
- Removed the obsolete compatibility wrapper headers entirely:
  - `Source/dsp/AudioProcessStage.h`
  - `Source/dsp/ProcessOptions.h`
  - `Source/dsp/DenoiseEngine.h`
  - `Source/dsp/DenoiseOptions.h`
- The migration also eliminates the old `vxcleaner::dsp` namespace from active code. The subtract implementation now lives under the suite-owned `vxsuite::subtract` namespace and uses framework primitives (`VxSuiteAudioProcessStage`, `VxSuiteProcessOptions`, `VxSuiteFft`, `VxSuiteSpectralHelpers`) directly.
- Updated the build graph so `VXSubtract` now compiles `Source/vxsuite/products/subtract/dsp/VxSubtractDsp.cpp` instead of `Source/dsp/HandmadePrimary.cpp`.
- Verified with:
  - `cmake --build build --target VXSubtract -j4`
  - `cmake --build build -j4`

---

# VX Suite FFT centralization — 2026-03-17

## Problem
Active VX Suite spectral products still own `juce::dsp::FFT` locally, which duplicates a core realtime primitive across the framework-facing codebase. Centralize FFT ownership in `Source/vxsuite/framework/` and migrate active users to the shared abstraction, while noting any still-live legacy path that should follow the same contract.

## Plan
- [x] Audit active and still-live FFT ownership across the repo.
- [x] Capture the correction in `tasks/lessons.md`.
- [x] Add a shared framework FFT wrapper that centralizes JUCE FFT ownership and transform calls.
- [x] Migrate active VX Suite FFT owners to the shared wrapper, plus the still-live legacy `HandmadePrimary` path if it fits cleanly.
- [x] Build the affected targets and document the result.

## Review
- Centralized FFT ownership in the framework wrapper at `Source/vxsuite/framework/VxSuiteFft.h`, keeping the abstraction intentionally small: `prepare(order)`, `isReady()`, `size()`, `bins()`, `performForward()`, and `performInverse()`.
- Confirmed the active VX Suite FFT owners are the denoiser and deverb spectral paths, and migrated both to the shared wrapper:
  - `Source/vxsuite/products/denoiser/dsp/VxDenoiserDsp.h`
  - `Source/vxsuite/products/denoiser/dsp/VxDenoiserDsp.cpp`
  - `Source/vxsuite/products/deverb/dsp/VxDeverbSpectralProcessor.h`
  - `Source/vxsuite/products/deverb/dsp/VxDeverbSpectralProcessor.cpp`
- Also aligned the still-live legacy FFT owner behind `VXSubtract`:
  - `Source/dsp/HandmadePrimary.h`
  - `Source/dsp/HandmadePrimary.cpp`
- Kept the change scoped to FFT ownership and transform dispatch only. Window generation, overlap-add logic, frame history, and per-bin DSP state remain product-local, so the spectral algorithms themselves were not rewritten.
- Verified with `cmake --build build --target VXDeverb VXDenoiser VXSubtract -j4` and `cmake --build build -j4`.

---

# VXPolish split into Cleanup + Finish — 2026-03-17

## Problem
`VXPolish` had grown into two different jobs: corrective cleanup and finish/loudness shaping. The product now needs to be split into two clear plugins, with every remaining stage doing meaningful work, preserving the existing vocal/general behavior, and keeping any gain/recovery path noise-aware.

## Plan
- [x] Define the split clearly: `Cleanup` owns corrective removal, `Finish` owns recovery/dynamics/final level.
- [x] Add shared tonal-analysis helpers plus new `VXCleanup` and `VXFinish` processors wired to the existing DSP stages.
- [x] Replace `VXPolish` in the build with the two new plugin targets and stage the new bundles.
- [x] Tighten the de-breath detector so it follows real breath-like events instead of generic bright content, then build and verify the repo.

## Review
- Added `VXCleanup`, a corrective-only plugin built around the existing polish DSP's subtractive stage. It owns `HPF`, `De-mud`, `Trouble smoothing`, `De-ess`, `De-breath`, and `Plosive` control.
- Added `VXFinish`, a finishing plugin built around the same DSP's dynamics/recovery stages. It owns smart recovery/body lift, compression, intelligent gain makeup, and limiting.
- Extracted shared tonal tracking into `Source/vxsuite/products/polish/VxPolishTonalAnalysis.h` so both new plugins follow the same vocal/general analysis instead of diverging.
- Kept vocal/general behavior aligned across both products: vocal remains more speech-protective, while general allows broader correction or finish reach.
- Kept all gain-adding behavior noise-aware in `VXFinish` by preserving the existing noise-floor gating and only applying smart makeup / recovery when the signal sits sufficiently above the estimated floor.
- Tightened the de-breath detector in `VxPolishDsp` so it is gated away from stronger transient and sibilant moments, making it follow breath-like airy events more specifically.
- Replaced `VXPolish` in the build graph with `VXCleanup` and `VXFinish`, and verified with `cmake -S . -B build`, `cmake --build build --target VXCleanup VXFinish -j4`, and `cmake --build build -j4`.

---

# VXCleanup classifier refactor — 2026-03-17

## Problem
`VXCleanup` still maps multiple corrective stages from shared `lowTrouble`, `highTrouble`, and `cleanupDrive` signals. This couples de-breath, de-ess, plosive, and tonal cleanup in ways that make the plugin feel less intelligent than its analysis-first architecture deserves.

## Plan
- [x] Re-read the current `VXCleanup` processor and confirm the active coupled mapping.
- [x] Add preallocated spectral feature extraction to `VXCleanup` using the shared framework FFT wrapper.
- [x] Compute explicit smoothed classifiers for breathness, sibilance, plosive risk, tonal mud, and harshness.
- [x] Remap `polish::Dsp::Params` from those classifier signals instead of shared trouble drives, with strict param clamps before `setParams()`.
- [x] Build `VXCleanup` and the full repo, then document the verification result.

## Review
- Added a preallocated spectral-analysis path directly in `VXCleanupAudioProcessor`, using the shared `VxSuiteFft` wrapper plus a ring buffer, Hann window, and FFT workspace allocated during `prepareSuite()`. No new heap work was added to `processProduct()`.
- `VXCleanup` now extracts explicit block features before mapping DSP params: spectral flatness, harmonicity (peak-dominance proxy), high-frequency ratio, high-band energy, and low-burst ratio. These are combined with the existing tonal ratios and voice-analysis snapshot instead of replacing them.
- Replaced the old coupled `lowTrouble` / `highTrouble` / `cleanupDrive` mapping with explicit classifier-style envelopes:
  - `breathEnv`
  - `sibilanceEnv`
  - `plosiveEnv`
  - `tonalMudEnv`
  - `harshnessEnv`
- Remapped the corrective stages from those classifiers rather than from a shared high-trouble signal:
  - `deMud` now follows `tonalMudWeight`
  - `deEss` now follows `sibilanceWeight`
  - `breath` now follows `breathWeight`
  - `plosive` now follows `plosiveWeight`
  - `troubleSmooth` now follows `harshWeight`
- The new breath/sibilance split is intentional:
  - breath classification favors noise-like, broadband, high-frequency content with low harmonicity and is gated by speech confidence/directness
  - sibilance classification favors bright, high-band, spectrally peaky content instead of sharing the same upstream drive as de-breath
- Added strict param clamps in the cleanup processor before `polishChain.setParams(params)` so the product enforces its own bounds even though the downstream DSP also guards inputs.
- Verified with:
  - `cmake --build build --target VXCleanup -j4`
  - `cmake --build build -j4`

---

# VX Suite shared-component review — 2026-03-17

## Problem
Review the VX Suite products for duplicate or near-duplicate processor/DSP/helper code that should be pulled into the framework or shared product components, and identify the strongest refactor opportunities without changing code yet.

## Plan
- [x] Inventory the current product modules and existing framework/shared helpers.
- [x] Compare processors, DSP shells, and helper patterns across products for duplication.
- [x] Identify the strongest 3-6 shared-component opportunities with file references and rationale.
- [x] Record the review result in this task log.

## Review
- [P1] Latency-aligned dry/listen scratch handling is duplicated across four products and should become a shared framework utility. `VXDenoiser`, `VXDeepFilterNet`, `VXDeverb`, and `VXSubtract` each own nearly the same `dryScratch` / `alignedDryScratch` / delay-line allocation, fill, and `renderListenOutput()` logic. See `Source/vxsuite/products/denoiser/VxDenoiserProcessor.cpp`, `Source/vxsuite/products/deepfilternet/VxDeepFilterNetProcessor.cpp`, `Source/vxsuite/products/deverb/VxDeverbProcessor.cpp`, and `Source/vxsuite/products/subtract/VxSubtractProcessor.cpp`. This is the strongest shared-component candidate because the repetition is structural, latency-bearing, and easy to drift out of sync.
- [P1] The spectral denoiser core is still split across two very similar product-local DSP implementations and should be factored into a shared spectral-cleanup core. `Source/vxsuite/products/denoiser/dsp/VxDenoiserDsp.*` and `Source/vxsuite/products/subtract/dsp/VxSubtractDsp.*` both carry overlapping STFT/min-stats/OM-LSA/bark-transient/harmonic-floor/phase-continuity machinery. `VXSubtract` adds learned-profile behavior on top, but the underlying blind denoise/scaffold is still clearly shared. This is a high-value extraction target, but it should be done carefully as a common spectral engine plus product-specific wrappers, not by forcing the products back together.
- [P2] Control smoothing helpers are duplicated in multiple processors and should be centralized in the framework. `blockBlendAlpha(...)` or equivalent inline exponential smoothing math appears in `VXCleanup`, `VXFinish`, `VXProximity`, `VXDenoiser`, `VXDeepFilterNet`, `VXSubtract`, and `VXDeverb`. See `Source/vxsuite/products/cleanup/VxCleanupProcessor.cpp`, `Source/vxsuite/products/finish/VxFinishProcessor.cpp`, `Source/vxsuite/products/proximity/VxProximityProcessor.cpp`, `Source/vxsuite/products/denoiser/VxDenoiserProcessor.cpp`, `Source/vxsuite/products/deepfilternet/VxDeepFilterNetProcessor.cpp`, `Source/vxsuite/products/subtract/VxSubtractProcessor.cpp`, and `Source/vxsuite/products/deverb/VxDeverbProcessor.cpp`. A small shared smoother/helper would remove repeated `exp()` math and keep time constants expressed consistently.
- [P2] The “Polish-derived analysis context” used by `VXCleanup` and `VXFinish` should probably be shared as a product-family helper rather than rebuilt ad hoc in each processor. Both compute tonal ratios from `VxPolishTonalAnalysis`, derive `speechConfidence` / `artifactRisk`, and then map those into `polish::Dsp::Params`. See `Source/vxsuite/products/cleanup/VxCleanupProcessor.cpp` and `Source/vxsuite/products/finish/VxFinishProcessor.cpp`. This should likely live under `Source/vxsuite/products/polish/` or a framework analysis helper used by the cleanup/finish family, not necessarily the global framework unless a third product needs the same contract.
- [P3] Simple parameter-layout construction is still reimplemented in a few processors even though the framework already has `createSimpleParameterLayout(...)`. `VXDenoiser` and `VXProximity` manually build what is effectively the standard two-float + mode + listen layout, and `VXDeverb` only diverges by control defaults/labels. See `Source/vxsuite/products/denoiser/VxDenoiserProcessor.cpp`, `Source/vxsuite/products/proximity/VxProximityProcessor.cpp`, `Source/vxsuite/products/deverb/VxDeverbProcessor.cpp`, and `Source/vxsuite/framework/VxSuiteParameters.h`. This is worth centralizing by making the framework helper support per-control defaults, but it is lower priority than the latency/listen and spectral-core duplication above.

---

# Cross-plugin shared-component review — 2026-03-17

## Problem
Review all current VX Suite plugins for duplicate or near-duplicate code and identify where multiple products should share a framework component instead of keeping separate local implementations.

## Plan
- [x] Inventory product modules and current framework/shared helpers.
- [x] Compare processors and DSP shells for repeated patterns that should become shared components.
- [x] Capture the strongest refactor opportunities with code references and risk notes.

## Review
- The strongest shared-component opportunity is latency-aligned listen support for latency-bearing products. `VXDenoiser`, `VXDeepFilterNet`, `VXSubtract`, and `VXDeverb` each keep their own dry scratch buffers, per-channel delay lines, aligned-dry builders, and custom `renderListenOutput()` paths even though they are all implementing the same core contract: aligned dry minus wet for removed-content audition. This should move into `ProcessorBase` as an optional latency-aligned listen helper rather than staying duplicated per product.
- Control smoothing is duplicated across most processors in slightly different local forms. `VxCleanupProcessor.cpp`, `VxFinishProcessor.cpp`, `VxDenoiserProcessor.cpp`, `VxDeepFilterNetProcessor.cpp`, `VxSubtractProcessor.cpp`, `VxProximityProcessor.cpp`, and `VxDeverbProcessor.cpp` all do some version of “prime on first block, then exponential block smoothing per control.” The exact time constants differ, but the state machine is mostly the same and should be a shared block-smoothing helper.
- `VxDenoiserDsp` and `VxSubtractDsp` still share a large spectral-core shape: min-stats noise tracking, OM-LSA presence probability, Bark transient hold, tonalness-driven protection, harmonic comb protection, frequency smoothing, and phase-continuity resynthesis. They should not be fully merged, but they are now close enough that a shared spectral engine/state/helper layer would reduce drift and make bug fixes land once.
- `VxCleanupProcessor.cpp` and `VxFinishProcessor.cpp` both rebuild the same high-level analysis fusion from `VxPolishTonalAnalysis` + `VoiceAnalysisSnapshot` before mapping into `polish::Dsp::Params`. This is product-specific enough that the final mapping should stay local, but the common “analysis evidence pack” wants a shared helper struct/function so the two products do not drift.
- Several products still duplicate custom parameter layouts that are very close to `createSimpleParameterLayout(...)`. `VxDenoiserProcessor.cpp`, `VxDeverbProcessor.cpp`, and `VxProximityProcessor.cpp` each hand-roll near-template layouts with the same mode/listen wiring and mostly standard float params. A more configurable shared layout builder would reduce repetition without forcing every product into the exact same defaults.

---

# Cross-plugin DRY refactor — 2026-03-17

## Problem
Reduce duplicated framework-shaped code that does not need to be product-local, especially where duplicated audio-path plumbing risks correctness drift across plugins.

## Plan
- [x] Add a shared framework helper for latency-aligned listen buffering/delta rendering.
- [x] Add a shared framework helper for block-rate smoothing/clamp math.
- [x] Migrate the duplicated latency-bearing processors to the shared helpers.
- [x] Migrate the simpler control-smoothing sites to the shared helper where it fits cleanly.
- [x] Build affected targets and the full repo, then document the result.
- [x] Extract the remaining shared Polish-family analysis evidence for Cleanup/Finish.
- [x] Expand the shared parameter-layout helper and migrate the near-template products.
- [x] Extract the denoiser/subtract spectral-core helpers that are still duplicated, then rebuild.

## Review
- Added `Source/vxsuite/framework/VxSuiteLatencyAlignedListen.h`, a shared helper that owns dry capture, aligned-dry buffering, per-channel delay lines, and removed-delta listen rendering for latency-bearing processors.
- Added `Source/vxsuite/framework/VxSuiteBlockSmoothing.h`, which centralizes `blockBlendAlpha`, block-rate one-time smoothing, attack/release smoothing, and shared `clamp01`.
- Extended `Source/vxsuite/framework/VxSuiteParameters.h` + `Source/vxsuite/framework/VxSuiteProduct.h` so products can declare per-control default values in the shared layout builder instead of hand-rolling near-template parameter layouts.
- Added `Source/vxsuite/products/polish/VxPolishAnalysisEvidence.h`, which centralizes the shared `TonalAnalysisState + VoiceAnalysisSnapshot` evidence pack used by both `VXCleanup` and `VXFinish`.
- Expanded `Source/vxsuite/framework/VxSuiteSpectralHelpers.h` with shared spectral setup helpers and a reusable Martin minimum-statistics state/update path used by both `VxDenoiserDsp` and `VxSubtractDsp`.
- Migrated the duplicated latency-aligned listen plumbing in:
  - `Source/vxsuite/products/denoiser/VxDenoiserProcessor.*`
  - `Source/vxsuite/products/deepfilternet/VxDeepFilterNetProcessor.*`
  - `Source/vxsuite/products/subtract/VxSubtractProcessor.*`
  - `Source/vxsuite/products/deverb/VxDeverbProcessor.*`
- Migrated the repeated local smoothing math in:
  - `Source/vxsuite/products/cleanup/VxCleanupProcessor.cpp`
  - `Source/vxsuite/products/finish/VxFinishProcessor.cpp`
  - `Source/vxsuite/products/proximity/VxProximityProcessor.cpp`
  - plus the latency-bearing processors above
- Removed the remaining near-template parameter-layout duplication from:
  - `Source/vxsuite/products/denoiser/VxDenoiserProcessor.*`
  - `Source/vxsuite/products/deverb/VxDeverbProcessor.*`
  - `Source/vxsuite/products/proximity/VxProximityProcessor.*`
- Migrated the shared Polish-family evidence derivation in:
  - `Source/vxsuite/products/cleanup/VxCleanupProcessor.*`
  - `Source/vxsuite/products/finish/VxFinishProcessor.*`
- Migrated shared spectral-core setup/min-stats helpers in:
  - `Source/vxsuite/products/denoiser/dsp/VxDenoiserDsp.*`
  - `Source/vxsuite/products/subtract/dsp/VxSubtractDsp.*`
- This removes the repeated per-product implementations of:
  - dry/aligned scratch allocation
  - per-channel latency delay lines
  - aligned dry reconstruction
  - removed-content listen delta rendering
  - duplicated block-rate smoothing formulas
- It also removes duplicated:
  - near-template mode/listen/float parameter layouts
  - Cleanup/Finish analysis-evidence formulas
  - denoiser/subtract sqrt-Hann prep, Bark-bin setup, and Martin minimum-statistics update/reset logic
- Verified with:
  - `cmake --build build --target VXDenoiser VXSubtract VXCleanup VXFinish VXDeverb VXProximity -j4`
  - `cmake --build build -j4`

---

# Legacy file removal pass — 2026-03-17

## Problem
Remove any remaining legacy or obsolete files left in the repo after the framework and DSP migrations, while keeping the active build tree intact.

## Plan
- [x] Audit the repo for remaining legacy/obsolete files and confirm whether they are referenced.
- [x] Delete unreferenced legacy files that are no longer part of the active suite.
- [x] Build the full repo and document the final state.

## Review
- Confirmed there are no remaining live `Source/dsp/`-style legacy code paths or backup/reject files in the repo.
- Confirmed the only remaining obsolete file under `Source/` was `Source/vxsuite/products/deverb/vx_deverb_phases_1_3_agent_guide.md`, an unreferenced agent-planning markdown artifact rather than active source.
- Deleted that stale deverb guide so `Source/` now contains active code/assets only.
- Verified with `cmake --build build -j4`.

---

# Polish DSP split — 2026-03-17

## Problem
`products/polish/dsp/VxPolishDsp.*` is still a monolithic leftover from when Polish behaved like a broader single product. Cleanup and Finish now own different jobs, so split the DSP by product responsibility and remove the old file.

## Plan
- [x] Identify the safe split boundary between shared corrective code and Finish-only recovery/limiter code.
- [x] Move Cleanup and Finish onto product-owned DSP entrypoints and delete the old `VxPolishDsp.*` file.
- [x] Build affected targets and the full repo, then document the result.

## Review
- Split the old monolithic `Source/vxsuite/products/polish/dsp/VxPolishDsp.*` into product-owned DSP entrypoints:
  - `Source/vxsuite/products/cleanup/dsp/VxCleanupDsp.*`
  - `Source/vxsuite/products/finish/dsp/VxFinishDsp.*`
- Kept only genuinely shared corrective-stage code in:
  - `Source/vxsuite/products/polish/dsp/VxPolishCorrectiveStage.*`
  - `Source/vxsuite/products/polish/dsp/VxPolishDspCommon.h`
  - `Source/vxsuite/products/polish/dsp/VxPolishSharedParams.h`
- Updated `VXCleanup` and `VXFinish` processors and `CMakeLists.txt` to compile against their own DSP files instead of the old shared monolith.
- Removed the obsolete `Source/vxsuite/products/polish/dsp/VxPolishDsp.h` and `Source/vxsuite/products/polish/dsp/VxPolishDsp.cpp` files entirely.
- This also eliminated the stale `sourcePreset` branch that was always pinned to `0` in both wrappers.
- Verified with:
  - `cmake --build build --target VXCleanup VXFinish -j4`
  - `cmake --build build -j4`

---

# Plugin regression harness — 2026-03-17

## Problem
Add a processor-level regression harness that checks the most failure-prone suite behaviors after the recent refactors: Subtract learn state/progress, listen-mode delta behavior, neutral cleanup behavior, and multi-plugin chain stability.

## Plan
- [x] Inspect the existing processor test pattern and choose the right harness structure.
- [x] Add shared processor test utilities for fixture generation, block rendering, and latency-aware trimming.
- [x] Implement an initial regression executable covering Cleanup identity, Subtract learn/listen behavior, and Subtract→Cleanup→Finish chain stability.
- [x] Configure/build the new test target, run it, and document the result.

## Review
- Added a new standalone regression executable at `tests/VXSuitePluginRegressionTests.cpp` plus shared helpers in `tests/VxSuiteProcessorTestUtils.h`, following the same "real processor instance" pattern as the existing suite test targets instead of introducing a separate unit-test framework.
- The first pass covers five high-value behaviors: `VXCleanup` identity at zero cleanup, `VXSubtract` learn lifecycle/progress monotonicity, `VXSubtract` listen-mode removed-content output, `VXSubtract -> VXCleanup -> VXFinish` chain stability, and silence preservation through the combined chain.
- The render helper is latency-aware, so processors are exercised with their reported plugin latency rather than with unrealistic zero-latency assumptions.
- The Subtract listen assertion was tuned to check steady-state recombination with a realistic tolerance after the initial alignment transient, which makes it useful as a regression guard without overfitting to startup buffer state.
- Verified with `cmake --build build --target VXSuitePluginRegressionTests -j4 && ./build/VXSuitePluginRegressionTests`, which completed successfully.

# Framework listen + Finish role pass — 2026-03-17

## Problem
Implement the next high-priority suite cleanup pass: centralize latency-aware listen and latency reporting in the shared framework, add a small coordination path for stage latency, reduce block-size dependence with explicit regression tests, and tighten `VXFinish` so it behaves like a finish stage rather than cleanup.

## Plan
- [x] Centralize latency-aware listen capture/alignment/rendering in the shared processor framework and remove duplicated product-local listen plumbing where it is no longer needed.
- [x] Add a small shared latency-coordination helper so processors can report summed stage latency consistently instead of open-coding `setLatencySamples(...)`.
- [x] Refactor `VXFinish` mapping and DSP flow so it no longer performs corrective cleanup work and its gain path is target-driven rather than RMS-reactive.
- [x] Extend the regression harness with host block-size invariance checks and an explicit full-chain test.
- [x] Build the affected targets, run the regression harness, and document the result.

## Review
- Added a shared coordinator at `Source/vxsuite/framework/VxSuiteProcessCoordinator.h` and wired it through `Source/vxsuite/framework/VxSuiteProcessorBase.h` / `Source/vxsuite/framework/VxSuiteProcessorBase.cpp`, so latency-aware removed-content listen now lives in the framework instead of four separate products carrying their own aligned-dry plumbing.
- `VXDenoiser`, `VXSubtract`, `VXDeverb`, and `VXDeepFilterNet` now report stage latency through the shared framework helpers and use the base listen path; `VXDeverb` still accesses aligned dry during processing for body restore, but it now does so through the base coordinator instead of a product-local listen buffer.
- `VXFinish` now behaves more like a finishing stage: its control mapping no longer rises from cleanup-style trouble metrics, recovery is driven by tonal deficit plus speech clarity, and makeup is target-driven from intended output loudness instead of being a direct reaction to pre/post RMS reduction inside the block.
- Expanded the regression harness to cover `Subtract -> Cleanup -> Proximity -> Finish` as an explicit suite chain and added block-size invariance checks so the current release bar now includes host-buffer consistency, not just single-block correctness.
- Verified with `cmake --build build --target VXSuitePluginRegressionTests -j4 && ./build/VXSuitePluginRegressionTests` and `cmake --build build -j4`.

# Suite master checklist — 2026-03-17

## Framework
- [x] Move latency-aligned Listen into `ProcessorBase` so every plugin uses the same removed-signal path.
- [x] Add a small stage-chain manager to own process order, total latency, and stage reset/prepare.
- [x] Report total latency to the host consistently from the framework.
- [x] Standardise on shared helpers like `VxSuiteBlockSmoothing.h` and remove duplicate local smoothing/clamp code.
- [x] Keep raw voice analysis separate from derived protection logic for easier tuning and debugging.

## Realtime / DAW safety
- [x] Test every plugin and the full chain at `64`, `128`, `256`, and `512` sample buffers.
- [x] Test at `44.1`, `48`, and `96` kHz.
- [~] Check transport stop/start, bypass, reset, state restore, and sample-rate changes in a DAW.
- [x] Verify no heap allocation, locks, or hidden vector growth happen on the audio thread.
- [x] Confirm behaviour stays consistent between mono and stereo paths.

## Plugin interaction
- [x] Tighten plugin role boundaries so each plugin does one job only.
- [x] Make sure later plugins never reintroduce problems earlier plugins removed.
- [x] Validate the full recommended chain, not just plugins in isolation.
- [x] Check that Listen mode behaves consistently across all plugins in the chain.

## VXCleanup
- [x] Keep pushing explicit event classification for breath, sibilance, plosive, and harshness.
- [x] Reduce dependence on current host block size for spectral/event detection.
- [x] Validate Cleanup across different DAW block sizes so detection feels stable.

## VXFinish
- [x] Remove or hard-cap any denoise/corrective behaviour.
- [x] Redefine `Gain` as a loudness target or finish bias, not reactive makeup.
- [x] Separate compressor role from limiter role more clearly.
- [x] Replace RMS-reactive makeup logic with a more stable target-driven approach.

## Consistency / product polish
- [x] Make shared controls like `Body`, `Gain`, and `Guard` feel consistent across plugins.
- [x] Keep UI and control behaviour coherent across the suite.
- [x] Add CPU profiling targets for single plugins and full-chain use.
- [x] Build a release checklist for host compatibility, latency, CPU, and chain behaviour.
- [x] Make the UI window larger than you think you need so all text is visible.
- [x] Ensure all text has enough space and is readable, with roughly `10–12 pt` minimum where possible.

## Notes
- `[x]` means completed in the current refactor pass.
- `[~]` means partially addressed in code or tests, but still needs deliberate follow-up or broader verification.
- The only remaining non-automated item is an actual interactive DAW host pass for transport/bypass/session-restore behaviour. The code now has automated coverage for lifecycle, sample-rate, block-size, mono/stereo, listen semantics, chain stability, and steady-state allocation safety, plus a profiling target and release checklist to drive the final manual host check.

## Final Review
- Added `Source/vxsuite/framework/VxSuiteStageChain.h` so stage latency / prepare / reset can be coordinated explicitly instead of by repeated convention in each processor shell.
- Expanded `tests/VXSuitePluginRegressionTests.cpp` into a broader lifecycle and safety harness: block-size invariance, multi-sample-rate coverage, mono/stereo consistency, Subtract state restore, listen-semantic checks, and a steady-state audio-thread allocation guard.
- Added `tests/VXSuiteProfile.cpp` as a profiling target for single-plugin and full-chain timing sweeps.
- Increased the shared editor’s default/minimum sizes and text/layout spacing in `Source/vxsuite/framework/VxSuiteEditorBase.cpp` and `Source/vxsuite/framework/VxSuiteLookAndFeel.cpp` so status text, hints, and control labels have more breathing room and stay readable.
- Added `docs/VX_SUITE_RELEASE_CHECKLIST.md` and `docs/VX_SUITE_CONTROL_SEMANTICS.md` so host validation and shared control meaning are now explicit project assets instead of only tribal knowledge.

---

# Deverb regression repair — 2026-03-17

## Problem
`VXDeverb` no longer behaves like the pre-refactor plugin. The dedicated tests show two hard regressions:
- `reduce=0` is not a true identity path
- speech correlation at meaningful `Reduce` settings collapses

The user explicitly wants to keep WPE, because the old plugin with WPE worked well.

## Plan
- [x] Compare the current Deverb wrapper and spectral path against the older working version to isolate the remaining regression.
- [x] Fix the underlying Deverb latency/processing mismatch without removing WPE or the good framework dry-capture fix.
- [x] Run `VXDeverbTests`, `VXDeverbMeasure`, and the broader regression harness.
- [x] Record the result and add a lesson if the root cause was introduced by the refactor.

## Review
- The biggest remaining regression was in the Deverb spectral stage timing: the STFT path was paying latency twice by both waiting for the causal analysis window and pre-advancing the OLA write cursor by the reported latency. Resetting `olaWritePos` to `0` in `Source/vxsuite/products/deverb/dsp/VxDeverbSpectralProcessor.cpp` restored true `reduce=0` identity and brought the processed output back into host-latency alignment.
- I kept the good framework change from `Source/vxsuite/framework/VxSuiteProcessorBase.cpp`: aligned dry is still captured every block so body-restore and listen paths can rely on it even when Listen mode is off.
- I compared the current Deverb files directly against commit `dc55d8c`, which confirmed that the wrapper was already close to the trusted behavior and that the real repair belonged in the spectral timing, not in removing WPE.
- `Source/vxsuite/products/deverb/VxDeverbProcessor.cpp` now gives `Body` a more meaningful low-band restoration path again by combining the original wet-to-dry low blend with a small bounded low-band support term.
- `tests/VXDeverbTests.cpp` was updated to match the corrected latency contract: full-wet Deverb is now expected to stay aligned after host latency compensation rather than line up better with an extra delayed dry signal.
- Verified with:
  - `cmake --build build --target VXDeverbTests VXSuitePluginRegressionTests VXDeverb -j4`
  - `./build/VXDeverbTests`
  - `./build/VXSuitePluginRegressionTests`
  - `./build/VXDeverbMeasure --reduce 0 --body 0 --render-seconds 0.9`
  - `cmake --build build -j4`

---

# Finish gain/compressor rebalance — 2026-03-17

## Problem
`VXFinish` still feels wrong in use:
- the compressor path can clip
- the `Gain` control is not meaningfully useful
- the middle of the `Gain` control should be neutral, with left reducing and right increasing

## Plan
- [x] Inspect the current Finish gain/compressor mapping and identify why the midpoint already adds level.
- [x] Rework the Finish gain law so `0.5` is neutral and the control behaves bipolarly around center.
- [x] Reduce compressor/makeup-driven clipping without making Finish feel lifeless.
- [x] Run focused Finish verification and the suite regression harness.
- [x] Record the result and any lesson from the fix.

## Review
- `Source/vxsuite/products/finish/VxFinishProcessor.cpp` no longer treats the `Gain` knob as a hidden driver for compression, recovery, limiter pressure, and target makeup. The midpoint is now truly neutral, with the knob interpreted as a bipolar final output trim around `0.5`.
- `Gain` now happens at the end of the Finish path as a smoothed final output gain stage after limiting, with the existing trim guard still catching overs so the plugin does not clip just because the user pushes the right side.
- Compression/makeup pressure was reduced at the processor mapping level so the compressor is less likely to overshoot into the limiter. `Finish` and `Body` still shape density/recovery, but `Gain` no longer quietly changes the compressor personality.
- Updated the `Gain` hint text to describe the intended behavior directly: middle neutral, left reduce, right increase.
- Added regression coverage in `tests/VXSuitePluginRegressionTests.cpp` so Finish now has an explicit test for bipolar `Gain` behavior around the centered midpoint.
- Verified with:
  - `cmake --build build --target VXSuitePluginRegressionTests VXFinish -j4`
  - `./build/VXSuitePluginRegressionTests`
  - `cmake --build build -j4`

---

# VXFinish clipping + slope hit-testing — 2026-03-17

## Problem
`VXFinish` still clips/distorts under compression, and the shared inline slope buttons no longer react after the icon layout change.

## Plan
- [x] Remove hidden compressor makeup from the shared corrective stage so `VXFinish` does not stack gain ahead of its limiter/output trim.
- [x] Fix shared slope icon hit-testing so inline icons toggle reliably even when the mouse event originates from a child component.
- [x] Rebuild the affected VST3 targets and record the verification result.

## Review
- Removed the shared corrective-stage compressor makeup path in `Source/vxsuite/products/polish/dsp/VxPolishCorrectiveStage.cpp`, so compression now only attenuates and can no longer quietly add level before `VXFinish` reaches its own limiter and output trim.
- Fixed the inline slope icon hit-testing in `Source/vxsuite/framework/VxSuiteEditorBase.cpp` by converting incoming mouse events into editor coordinates before checking the stored icon bounds. This restores the shared low/high slope toggles after the inline layout change.
- Verified with `cmake --build build --target VXFinish_VST3 VXCleanup_VST3 -j4` and then `cmake --build build --target VXSuite_VST3 -j4`, which rebuilt and restaged the full suite into `Source/vxsuite/vst/`.

---

# Full chain audit — 2026-03-17

## Problem
Review the full audio chain of each VX plugin and verify the suite stays clean: no clipping/distortion, no obvious phase/delay regressions, and sensible behavior when plugins are used together.

## Plan
- [x] Inspect the processor paths, latency handling, listen semantics, and output safety coverage across the current VX products.
- [x] Run the existing regression and deverb-focused tests, then expand coverage for under-tested products.
- [x] Apply focused fixes where the audit found concrete safety or routing issues.
- [x] Record the results, including any residual failures that still need deeper DSP work.

## Review
- Strengthened shared safety handling in `Source/vxsuite/framework/VxSuiteOutputTrimmer.h` so overload protection now applies immediate reduction on attack instead of ramping down too slowly through the same block.
- Added output-safety trimming to `Source/vxsuite/products/tone/VxToneProcessor.cpp`, `Source/vxsuite/products/proximity/VxProximityProcessor.cpp`, and `Source/vxsuite/products/deverb/VxDeverbProcessor.cpp`, which closes an output-headroom gap for additive/EQ-style products and extreme deverb body settings.
- Normalized plugin-entrypoint guarding in `Source/vxsuite/products/tone/VxToneProcessor.cpp` and `Source/vxsuite/products/denoiser/VxDenoiserProcessor.cpp` so these products can be linked into regression binaries without duplicate `createPluginFilter()` symbols.
- Expanded `tests/VXSuitePluginRegressionTests.cpp` to cover `Tone`, `Proximity`, and `Denoiser`, which were previously underrepresented compared with Cleanup / Finish / Deverb / Subtract.
- Reworked `Source/vxsuite/products/deverb/VxDeverbProcessor.cpp` body-restore logic so it no longer applies a negative low-band “restore” when wet low energy already exceeds dry, and so its restore path is more tightly gated and bounded.
- Rebalanced `Source/vxsuite/products/denoiser/VxDenoiserProcessor.cpp` to be more conservative at high settings and added a small latency-aligned dry recovery tied to `Guard`, reducing the risk of over-destructive speech damage.
- Verified successful rebuilds with:
  - `cmake --build build --target VXSuitePluginRegressionTests VXDeverbTests -j4`
  - `cmake --build build --target VXSuite_VST3 -j4`
- Current residual failures after the audit:
  - `./build/VXDeverbTests` still fails `Body` restoration magnitude (`off=0.774373 on=0.777374`), so Deverb `Blend` is now safer but still too weak to meet the intended restore contract.
  - `./build/VXSuitePluginRegressionTests` still fails Deverb extreme-tail suppression (`rms=0.186114` in the tail), which means Deverb `Blend` still reintroduces too much sustained low tail at the extreme.
  - `./build/VXSuitePluginRegressionTests` still fails the new denoiser coherence check (`|corr|=0.128598`), so the aggressive end of `VXDenoiser` still needs deeper DSP tuning beyond wrapper-level mapping changes.
- Conclusion from this audit: the suite is in a safer place than before, and several framework/product safety gaps were fixed, but I cannot honestly mark every plugin chain as fully clean yet because `VXDeverb` and `VXDenoiser` still have measurable behavior regressions under strong settings.
- Follow-up direction from the user: prefer upstream DSP/gain fixes over output trimming. I removed the new trim reliance from `VXTone`, `VXProximity`, and `VXDeverb`, reduced the shelf gain laws for Tone/Proximity, and switched the deverb audit pass toward speech-aware body gating plus an instability fallback rerender instead of simple output shaving.
- After that upstream-first pass, `Tone` and `Proximity` remain on the cleaner path, but `VXDeverb` still fails both body-restore and extreme-tail checks, and `VXDenoiser` still fails the aggressive-setting audit. Those two need deeper product-specific DSP work rather than more wrapper-level protection.

---

# Finish compressor/headroom review — 2026-03-17

## Problem
`VXFinish` still distorts when the compressor/finish path is pushed. Review the full in/out audio path and leave more real DSP headroom rather than relying on distortion-prone last-stage catching.

## Plan
- [x] Inspect the full Finish path: corrective stage, recovery lift, makeup, output gain, limiter, and final safety.
- [x] Remove distortion-prone limiter behavior and reduce gain stacking into the limiter.
- [x] Rebuild `VXFinish` and run focused regression/build verification.
- [x] Record the result and any remaining non-Finish regressions separately.

## Review
- `Source/vxsuite/products/finish/dsp/VxFinishDsp.cpp` no longer uses the old `softLimitSample(...)` stage in the limiter. The limiter is now a cleaner gain-only limiter with instant attack when reduction is needed, a slightly lower ceiling, and release smoothing only on recovery.
- `Source/vxsuite/products/finish/VxFinishProcessor.cpp` now leaves explicit internal headroom before the limiter by scanning the post-recovery/post-makeup/post-output-gain block and cleanly scaling it down to a `-6 dB` pre-limiter target when needed. This keeps the limiter from being driven into distortion as normal operating behavior.
- The Finish makeup target is also less aggressive: lower target loudness, smaller density lift, and a tighter maximum makeup range, so the product is less likely to stack recovery + makeup + gain into the limiter.
- Verified with:
  - `cmake --build build --target VXFinish_VST3 VXSuitePluginRegressionTests -j4`
  - `./build/VXSuitePluginRegressionTests`
- The regression executable still fails, but the remaining reported failures are currently `VXDeverb` and `VXDenoiser`, not `VXFinish`.

---

# Vocal mode consistency audit — 2026-03-19

## Problem
Check how the shared framework defines `Vocal` mode, how each VX product actually handles it, and whether the suite is consistent with the intended "scalpel, not chainsaw" vocal behavior.

## Plan
- [x] Inspect the shared framework mode contract and common process-option plumbing.
- [x] Review each major VX product's `Vocal` vs `General` behavior.
- [x] Record consistency findings and concrete gaps.

## Review
- Framework contract is consistent in principle: `Source/vxsuite/framework/VxSuiteModePolicy.h` defines `Vocal` as higher source protection, stronger speech focus, higher guard strictness, and lower late-tail aggression than `General`.
- `VXDenoiser`, `VXCleanup`, `VXSubtract`, `VXDeverb`, `VXTone`, `VXOptoComp`, and `VXFinish` all do change DSP behavior in response to `Vocal`, but they do so through different mechanisms and with uneven fidelity to the shared policy.
- Strongest alignment with the framework contract:
  - `VXDenoiser` maps the shared policy fields into `ProcessOptions` and its DSP uses those values for speech-band protection, transient guarding, and suppression depth.
  - `VXCleanup` uses mode both in the processor mappings and in the shared corrective DSP via `contentMode`, producing more speech-aware thresholds/cuts in `Vocal`.
  - `VXDeverb` uses `voiceMode` inside the spectral dereverb DSP to protect the speech band and enable speech-targeted WPE only in `Vocal`.
- Partially consistent / custom interpretations:
  - `VXSubtract` does become more protective and speech-focused in `Vocal`, but it hardcodes its own mapping instead of using `currentModePolicy()`, so it follows the suite intent without actually sharing the framework contract.
  - `VXTone` and `VXProximity` switch to narrower, more speech-aware shaping in `Vocal`, but they use simple product-local mode branches rather than the framework policy fields.
  - `VXOptoComp` and `VXFinish` switch between compressor-like `Vocal` behavior and limiter-like `General` behavior through `contentMode`, which is coherent, but much thinner than the framework's richer protection model.
- Clear consistency gap:
  - `VXDeepFilterNet` does not use the suite `Vocal/General` mode contract at all. Its `modeParamId` is repurposed as the model selector (`DeepFilterNet 3` vs `DeepFilterNet 2`), so there is no real `General` mode behavior to compare and no shared-policy mapping.
- Overall conclusion: the suite is directionally consistent that `Vocal` should be more surgical and speech-aware, but implementation consistency is mixed. Some products honor the shared framework policy directly, some implement their own local "voice mode" interpretation, and `VXDeepFilterNet` currently breaks the contract by replacing mode with model selection entirely.

---

# Denoiser / analyser regression pass — 2026-03-19

## Problem
`VXDenoiser` was reading like a broad full-range level drop in both modes, and the analyser was still occasionally flipping into the sparse stem/bar render with orange dots.

## Plan
- [x] Inspect the Denoiser amount/protection path and analyser sparse-render switching logic.
- [x] Implement targeted fixes for Denoiser specificity and analyser render stability.
- [x] Rebuild the affected plugins, run focused regression coverage, and record the result.

## Review
- `Source/vxsuite/products/denoiser/dsp/VxDenoiserDsp.cpp` now uses `speechFocus` inside the gain law, adding explicit speech-band protection so vocal cleanup stays more surgical instead of broadly pulling down the whole vocal range.
- The same DSP now slows upward motion in its per-bin noise estimate when speech presence is high, which reduces the tendency for the noise floor to chase live program material and turn the denoiser into broadband attenuation.
- `Source/vxsuite/products/denoiser/VxDenoiserProcessor.cpp` now applies bounded loudness-retention makeup after denoising, then runs the shared output trimmer. That keeps the result from feeling like simple volume loss without just blending the removed noise back in.
- `Source/vxsuite/products/analyser/VXStudioAnalyserEditor.cpp` no longer switches the spectrum plot into the sparse stem/orange-dot renderer; it stays in the normal overlay view even when the signal is narrowband, which removes the distracting bar-chart flash.
- Added a denoiser regression in `tests/VXSuitePluginRegressionTests.cpp` to ensure strong settings in both vocal and general modes retain useful output level instead of collapsing excessively.
- Verified with:
  - `cmake --build build --target VXDenoiserPlugin VXStudioAnalyserPlugin VXSuitePluginRegressionTests -j4`
  - `./build/VXSuitePluginRegressionTests`
- Follow-up pink-noise correction:
  - `Source/vxsuite/products/denoiser/VxDenoiserProcessor.cpp` now only applies loudness-retention makeup when the DSP actually sees speech-like presence, so noise-only material is no longer “protected” back toward its original broadband level.
  - Vocal and general clean-drive scaling were both increased so max settings can do real work on noise-only material instead of reading as near-bypass in vocal mode.
  - `Source/vxsuite/products/denoiser/dsp/VxDenoiserDsp.cpp` now gates speech-band protection by per-bin speech presence instead of always protecting the vocal band, which prevents pink noise from being treated like protected voice content.
  - Added a noise-only denoiser regression in `tests/VXSuitePluginRegressionTests.cpp` so both modes must measurably reduce noise-only input at full settings.
  - Re-verified with:
    - `cmake --build build --target VXDenoiserPlugin VXSuitePluginRegressionTests -j4`
    - `./build/VXSuitePluginRegressionTests`

---

# Framework UI responsiveness pass — 2026-03-19

## Problem
Ensure framework-based VX effects fit text cleanly, resize sensibly, and inherit readable layout behavior at the shared editor level instead of relying on one-off per-plugin fixes.

## Plan
- [x] Inspect the shared framework editor and framework UI rules.
- [x] Implement framework-level text-fitting and responsive layout improvements in the shared editor base.
- [x] Rebuild representative plugins and run regression verification.

## Review
- `Source/vxsuite/framework/VxSuiteEditorBase.cpp` now uses more forgiving shared minimum editor sizes, stronger minimum horizontal scaling on title/status/knob text, and more generous shared vertical space for knob labels and hint text.
- The shared header layout now tolerates narrower widths better by allowing the status line to fall to its own row when the control row gets tight, which prevents cramped header text across framework-based products.
- Shared mode/listen/learn controls now reserve slightly more width, and learn meter text is less likely to truncate aggressively.
- Updated `docs/VX_SUITE_FRAMEWORK.md` to make readable text and responsive shared layout part of the framework contract rather than an ad hoc cleanup item.
- Verified with:
  - `cmake --build build --target VXCleanupPlugin VXProximityPlugin VXFinishPlugin VXDenoiserPlugin -j4`
  - `./build/VXSuitePluginRegressionTests`

---

# Cleanup plosive false-trigger follow-up — 2026-03-20

## Problem
`VXCleanup` is still audibly distorting in a way that lines up with the plosive meter lighting. That suggests the remaining bug is not broad Cleanup harshness anymore, but false plosive detection on voiced material inside the Cleanup-only corrective path.

## Plan
- [x] Add a targeted regression that measures Cleanup plosive activity on real plosive material versus voiced edge-case material.
- [x] Tighten the Cleanup plosive detector/mapping so voiced harmonic content does not trigger plosive removal while real plosive bursts still do.
- [x] Rebuild, rerun the targeted verification, document the result, and commit the follow-up fix.

## Review
- `tests/VXSuitePluginRegressionTests.cpp` now includes `testCleanupPlosiveMeterTargetsBurstsNotVoicedMaterial()`, which measures the Cleanup plosive activity light directly instead of inferring false triggering only from output damage.
- `Source/vxsuite/products/polish/dsp/VxPolishCorrectiveStage.*` now uses a lower detector lowpass for the plosive follower and applies an explicit speech-aware plosive guard, so bright voiced material no longer drives the internal plosive envelope like a real low-frequency burst.
- `Source/vxsuite/products/cleanup/VxCleanupProcessor.cpp` now biases plosive correction much harder toward the low-focus side of Cleanup and further backs it off when the block looks strongly voiced/harmonic.
- Verified with:
  - `cmake --build build --target VXSuitePluginRegressionTests -j4`
  - relinked `/tmp/cleanup_plosive_probe` against current objects, which measured `burstActivity=0.838522` and `voicedActivity=0.0770964`
  - `./build/VXSuitePluginRegressionTests`
- The shared regression executable still exits on the pre-existing Deverb tail-window failure, but Cleanup no longer reports a plosive-related regression before that point.

---

# Deverb latency-vs-tail regression fix — 2026-03-20

## Problem
The shared regression suite was still failing on `Deverb tail window was unexpectedly empty`. The failure turned out not to be a Deverb DSP tail bug, but a framework/test contract bug: VX Suite was auto-reporting latency as if it were post-input tail audio.

## Plan
- [x] Reproduce the Deverb tail-window failure and inspect whether the processor really emits post-input tail audio or only host-reported latency.
- [x] Fix the framework/test contract so latency-bearing processors do not report fake tail length unless they truly emit carryover after input stops.
- [x] Rebuild and rerun the regression executable, then document the result and commit the fix.

## Review
- `Source/vxsuite/framework/VxSuiteProcessorBase.*` no longer inflates `tailLengthSeconds` from reported latency. Host PDC/latency reporting remains intact, but JUCE tail length now means actual post-input carryover only.
- `tests/VXSuitePluginRegressionTests.cpp` now checks that Deverb, Denoiser, and Subtract report latency without pretending that latency is tail, and the rendered-tail verification now only runs for processors that explicitly report non-zero tail length.
- Verified with `cmake --build build --target VXSuitePluginRegressionTests -j4` and `./build/VXSuitePluginRegressionTests`, which now passes end-to-end.

---

# Post-fix validation closure — 2026-03-20

## Problem
After the Cleanup distortion/plosive fixes and the latency-vs-tail reporting fix, the remaining task was to rerun the available local validation lanes and record the final repo-local status before closing this pass out.

## Plan
- [x] Re-run the key local regression/unit tests after the latest DSP/framework fixes.
- [x] Re-run the registered pluginval lane across all staged bundles.
- [x] Record the post-fix validation status and commit the closure note.

## Review
- Re-ran `ctest --output-on-failure -R '^(VXSuitePluginRegressionTests|VXDeverbTests|VxSuiteVoiceAnalysisTests)$'` from `build/`; all three tests passed.
- Re-ran `ctest --output-on-failure -R '^pluginval_'` from `build/`; all 10 pluginval checks passed: `VXCleanup`, `VXDeepFilterNet`, `VXDenoiser`, `VXDeverb`, `VXFinish`, `VXOptoComp`, `VXProximity`, `VXStudioAnalyser`, `VXSubtract`, and `VXTone`.
- Repo-local conclusion for this pass: source fixes are committed, the local regression suite is green, and the full available pluginval lane is green. Remaining follow-up work is external/in-host validation rather than another known code repair.

---

# Cleanup high-shelf distortion follow-up — 2026-03-20

## Problem
The remaining user report narrowed the residual Cleanup distortion down further: it only seemed to happen when the Cleanup amount was engaged and the high shelf was enabled. That pointed to the `hishelf_on` path rather than the broader corrective chain.

## Plan
- [x] Isolate the Cleanup high-shelf path on a voiced edge-case and confirm whether enabling the shelf adds disproportionate damage.
- [x] Make the high shelf gentler and more stable on voiced material, then add a regression that compares shelf-off versus shelf-on on the same signal.
- [x] Rebuild, rerun the full regression executable, document the result, and commit the fix.

## Review
- `Source/vxsuite/products/polish/dsp/VxPolishCorrectiveStage.cpp` now computes a gentler, higher-frequency high shelf for voiced Cleanup material, scales it back with `voicePreserve`/`speechPresence`, and stops clearing the shelf filter state on every coefficient retune.
- `tests/VXSuitePluginRegressionTests.cpp` now includes `testCleanupHighShelfDoesNotOverdamageVoicedEdgeCase()`, which compares the same voiced edge-case with `hishelf_on` off versus on and fails if the shelf path adds too much extra residual damage.
- Before the fix, the isolated shelf probe measured `off residual=0.0738695`, `on residual=0.0985489`, and `onOffResidual=0.0684699`. After the fix, that same probe measured `off residual=0.0738695`, `on residual=0.0748042`, and `onOffResidual=0.0144093`.
- Verified with `cmake --build build --target VXSuitePluginRegressionTests -j4` and `./build/VXSuitePluginRegressionTests`, which passed cleanly after the shelf-path change.

---

# Stereo-aware subtraction follow-up — 2026-03-20

## Problem
`VXSubtract` appears to be treating stereo input as one shared mono/mid denoise problem, then reconstructing stereo around that shared result. That is not acceptable when left and right channels have different noise content; each side needs its own noise estimation and subtraction path.

## Plan
- [x] Add a stereo-specific regression that fails if right-only learned noise removal bleeds incorrectly into the left channel or leaves asymmetric noise untreated.
- [x] Rework `VXSubtract` from shared mono/mid subtraction toward true stereo-aware subtraction for standard stereo files.
- [x] Re-run the relevant regression/pluginval lanes, document the result, and commit the fix.

## Review
- `Source/vxsuite/products/subtract/VxSubtractProcessor.*` now runs stereo `VXSubtract` through independent left/right DSP instances, restores stereo-only learned profiles correctly, and uses the framework latency-aligned dry path for channels that do not have a valid learned profile instead of forcing them through the subtract engine.
- `Source/vxsuite/products/subtract/dsp/VxSubtractDsp.*` now rejects learned profiles captured from effectively silent input, which prevents a right-only learn pass from accidentally generating a bogus high-confidence left-channel profile.
- `tests/VXSuitePluginRegressionTests.cpp` now includes a right-only stereo learn regression so asymmetric noise removal stays covered.
- Verification: `cmake --build build --target VXSuitePluginRegressionTests -j4` succeeds. `./build/VXSuitePluginRegressionTests` no longer reports the subtract stereo/state-restore failures; the current remaining failure is an unrelated pre-existing `OptoComp` steady-state allocation regression.
- Follow-up: `VXDenoiser` still processes stereo through a shared mono-mid noise path in `Source/vxsuite/products/denoiser/dsp/VxDenoiserDsp.cpp`, so it needs a matching stereo-aware audit/fix rather than being assumed correct.

---

# Stereo-aware denoiser follow-up — 2026-03-20

## Problem
`VXDenoiser` still denoises one shared mono/mid stream and reconstructs stereo around it. That is not acceptable for stereo files where left and right carry different noise beds or one side is noisier than the other.

## Plan
- [ ] Add a stereo-specific regression that fails if right-only added noise is not treated materially more strongly on the right channel than the left.
- [ ] Rework `VXDenoiser` from shared mono/mid denoising toward independent stereo denoising in the processor shell.
- [ ] Rebuild and rerun the relevant regression target, document the result, and commit the fix.

---

# Proximity voice-mode short-corpus tuning — 2026-03-23

## Problem
`Proximity` is subtle by design, but it still has some measurable short-corpus headroom compared with `Tone`. A small `Voice`-mode remap may improve spoken-word consistency by leaning slightly more into `Closer` when speech is buried and being a bit less conservative with `Air` when the vocal is clearly leading.

## Plan
- [x] Implement one conservative `Voice`-mode remap for `Proximity` and rerun the short-corpus report.
- [x] Keep it only if the short-corpus result improves and `VXSuitePluginRegressionTests` still pass.
- [x] Document the kept or reverted outcome before moving on.

## Review
- Tuned [VxProximityProcessor.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/proximity/VxProximityProcessor.cpp) so `Voice` mode leans slightly more into `Closer` when speech is buried/phrased and backs off less on `Air` when intelligibility is strong.
- Kept outcome on the short-corpus report in [VX_SUITE_BATCH_AUDIO_CHECK_PROXIMITY_REAL.md](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/data/voice_corpus/VX_SUITE_BATCH_AUDIO_CHECK_PROXIMITY_REAL.md):
  - baseline: `11.557 dB -> 11.190 dB`
  - kept build: `11.557 dB -> 11.154 dB`
- The move is small, but it is real and regression-clean:
  - `edward_viii_abdication.wav`: `9.852 dB -> 9.817 dB`
  - `princess_elizabeth_21st_birthday.wav`: `12.528 dB -> 12.490 dB`
- Verified with `./build/VXSuitePluginRegressionTests`.

---

# Level trace scope cleanup — 2026-03-23

## Problem
The shared level trace had become a suite-wide UI element even though it is only genuinely useful for `Leveler`. In other products it added noise, and because it renders immediately from the current processed block rather than host-latency-aligned audible output, it could also appear to run ahead of what the user was hearing.

## Plan
- [x] Make the shared level trace opt-in at the product identity/framework level instead of always-on.
- [x] Restrict the trace to `Leveler` for now and leave the rest of the suite on the cleaner default UI.
- [x] Rebuild representative plugins and rerun `VXSuitePluginRegressionTests`.

## Review
- Added `showLevelTrace` to [VxSuiteProduct.h](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/framework/VxSuiteProduct.h) and used it to gate trace telemetry/editor wiring in [VxSuiteProcessorBase.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/framework/VxSuiteProcessorBase.cpp) and [VxSuiteEditorBase.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/framework/VxSuiteEditorBase.cpp).
- Enabled the trace only for [VxLevelerProcessor.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/leveler/VxLevelerProcessor.cpp), so the rest of the suite now falls back to the simpler product UI.
- Verified with `cmake --build build --target VXLevelerPlugin VXFinishPlugin VXProximityPlugin VXDenoiserPlugin VXSuitePluginRegressionTests -j4` and `./build/VXSuitePluginRegressionTests`.

---

# Leveler rider redesign — 2026-03-23

## Problem
`Leveler` still reads more like a gentle adaptive dynamics stage than an intelligent volume rider. At high settings it should be actively reducing perceived and real level jumps while staying more natural than a compressor or limiter, but the current law mostly smooths around an anchor and feels too timid.

## Plan
- [x] Inspect the existing `Leveler` detector + DSP law and identify the parts that are still envelope-balancing rather than target-riding.
- [x] Prototype a stronger rider direction, reject the full rewrite when it performs worse, and keep the proven core as the base.
- [x] Make the top end of `Vocal Rider` materially more assertive while keeping `Mix Leveler` on the stable general path.
- [x] Verify with the real-file `VXLevelerMeasure` harness on `loud_quiet.wav`, rebuild the staged plugin, and rerun `VXSuitePluginRegressionTests`.

## Review
- Tried a full dB-target rider rewrite in [VxLevelerDsp.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/leveler/dsp/VxLevelerDsp.cpp), but it regressed both the real-file measure and the hot-mix regression, so it was intentionally discarded.
- Kept the stronger pass by returning to the proven core and adding a focused high-setting intervention zone only to `Vocal Rider`, with the extra gain control concentrated in the broadband rider/override path and the vocal assist lanes damped so they stop fighting the main level ride.
- Final verified measurements on [loud_quiet.wav](/Users/andrzejmarczewski/Downloads/loud_quiet.wav):
  - `Vocal Rider`: `12.9225 dB -> 12.1879 dB`
  - `Mix Leveler`: `12.9225 dB -> 12.8924 dB`
- Verified with `cmake --build build --target VXLevelerMeasure VXLevelerPlugin VXSuitePluginRegressionTests -j4`, `./build/VXLevelerMeasure /Users/andrzejmarczewski/Downloads/loud_quiet.wav /Users/andrzejmarczewski/Downloads/loud_quiet_voice_test.wav voice 1.0 1.0`, `./build/VXLevelerMeasure /Users/andrzejmarczewski/Downloads/loud_quiet.wav /Users/andrzejmarczewski/Downloads/loud_quiet_mix_test.wav general 1.0 1.0`, and `./build/VXSuitePluginRegressionTests`.

---

# Leveler architecture research pass — 2026-03-23

## Problem
`Leveler` is better than it was, but it still does not convincingly behave like an intelligent volume rider. Further implementation work should be grounded in established non-ML gain-riding and loudness-control practice instead of more blind tuning.

## Plan
- [x] Collect strong references on non-ML loudness levelling, automatic gain riding, and perceptual dynamics control.
- [x] Map those findings onto `Leveler`’s actual product goal: reduce perceived and real volume jumps without sounding compressed.
- [x] Write the recommended next architecture and tuning direction into project notes before another DSP rewrite.

## Next implementation step
- [x] Split `Leveler` internally into a preserved vocal engine and a separate general loudness engine so the two modes stop sharing one control law.
- [x] Keep the current verified vocal path as the baseline while introducing a neutral loudness-led `Mix Leveler` branch with separate level and spike gains.
- [x] Re-verify both modes on `loud_quiet.wav` and `VXSuitePluginRegressionTests` before attempting a deeper vocal-engine rewrite.

## Review
- `VxLevelerDsp.cpp` now uses two genuinely separate internal behaviors:
  - `Vocal Rider` stays on the previously verified vocal engine.
  - `Mix Leveler` now runs a separate loudness-led path with its own rolling baseline, deadbanded ride gain, and independent spike clamp.
- This keeps the known-good vocal result while moving `General` toward the architecture the research suggested, without forcing both modes through one experimental control law.
- Final verified measurements on [loud_quiet.wav](/Users/andrzejmarczewski/Downloads/loud_quiet.wav):
  - `Vocal Rider`: `12.9225 dB -> 11.8946 dB`
  - `Mix Leveler`: `12.9225 dB -> 12.6043 dB`
- Verified with:
  - `cmake --build build --target VXLevelerMeasure VXLevelerPlugin VXSuitePluginRegressionTests -j4`
  - `./build/VXLevelerMeasure /Users/andrzejmarczewski/Downloads/loud_quiet.wav /Users/andrzejmarczewski/Downloads/loud_quiet_voice_test.wav voice 1.0 1.0`
  - `./build/VXLevelerMeasure /Users/andrzejmarczewski/Downloads/loud_quiet.wav /Users/andrzejmarczewski/Downloads/loud_quiet_mix_test.wav general 1.0 1.0`
  - `./build/VXSuitePluginRegressionTests`

---

# Vocal Rider rework — 2026-03-23

## Problem
Now that `Mix Leveler` has its own branch, `Vocal Rider` is still the remaining mode that needs a more intentional architecture. It still leans on the older adaptive engine rather than a clearly committed rider contract.

## Plan
- [x] Inspect the current split `Leveler` DSP and identify where `Vocal Rider` still behaves like the older shared engine instead of a phrase-aware rider.
- [x] Implement a more intentional `Vocal Rider` path with stronger state commitment and lighter coupling between ride, lift, and tame.
- [x] Rebuild `Leveler`, measure both modes on `loud_quiet.wav`, and rerun `VXSuitePluginRegressionTests`.

## Review
- Research sources reviewed:
  - [ITU-R BS.1770-4](https://www.itu.int/dms_pubrec/itu-r/rec/bs/R-REC-BS.1770-4-201510-S%21%21PDF-E.pdf)
  - [EBU Tech 3341](https://tech.ebu.ch/publications/tech3341)
  - [EBU Loudness overview](https://tech.ebu.ch/loudness/)
  - [EBU Tech 3343 production guidelines](https://tech.ebu.ch/publications/tech3343)
  - [US20230162754A1 Automatic Leveling of Speech Content](https://patents.google.com/patent/US20230162754A1)
  - [Hathaway, “Automatic Audio Gain Controls” (AES historical paper)](https://www.aes.org/aeshc/pdf/how.the.aes.began/hathaway_automatic-audio-gain-controls.pdf)
- Main takeaways for `Leveler`:
  - A better rider should be based on perceptual loudness windows, not simple peak/envelope balancing.
  - The right control split is a slow rider for loudness consistency plus a separate fast peak protector; those two should not share the same control law.
  - Momentary and short-term loudness are the useful immediate riding views; integrated loudness is too slow for local riding.
  - The rider should mostly attenuate louder-than-local-target passages and recover upward more cautiously, which is closer to manual fader riding than to compressor makeup behavior.
  - `Vocal Rider` can add speech-aware weighting on top of that, but the core `Mix Leveler` path should stay non-semantic and loudness-led.
- A full loudness-led prototype was built in `VxLevelerDsp.cpp` and measured, but it was not kept because it still failed the hot-mix regression despite improving some real-file measurements. The verified build was restored afterward.
- Final kept verified state after reverting the failed prototype:
  - `Vocal Rider`: `12.9225 dB -> 11.8946 dB`
  - `Mix Leveler`: `12.9225 dB -> 12.8924 dB`
- Verification after restore:
  - `cmake --build build --target VXLevelerMeasure VXLevelerPlugin VXSuitePluginRegressionTests -j4`
  - `./build/VXLevelerMeasure /Users/andrzejmarczewski/Downloads/loud_quiet.wav /Users/andrzejmarczewski/Downloads/loud_quiet_voice_test.wav voice 1.0 1.0`
  - `./build/VXLevelerMeasure /Users/andrzejmarczewski/Downloads/loud_quiet.wav /Users/andrzejmarczewski/Downloads/loud_quiet_mix_test.wav general 1.0 1.0`
  - `./build/VXSuitePluginRegressionTests`
- Kept follow-up refinement on top of the split architecture:
  - threaded `phraseActivity`, `phraseStart`, `phraseEnd`, and `intelligibility` from the shared voice context into [VxLevelerDetector.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/leveler/dsp/VxLevelerDetector.cpp),
  - added a phrase-aware anchor in [VxLevelerDsp.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/leveler/dsp/VxLevelerDsp.cpp) so `Vocal Rider` can hold a more intentional local reference while speech is active,
  - kept that phrase awareness mostly in the lift/override lanes rather than directly biasing the main ride target after the first attempt proved slightly worse on the real file.
- Final kept verified state after this `Vocal Rider` rework:
  - `Vocal Rider`: `12.9225 dB -> 11.8820 dB`
  - `Mix Leveler`: `12.9225 dB -> 12.6043 dB`
- Verified with:
  - `cmake --build build --target VXLevelerMeasure VXLevelerPlugin VXSuitePluginRegressionTests -j4`
  - `./build/VXLevelerMeasure /Users/andrzejmarczewski/Downloads/loud_quiet.wav /Users/andrzejmarczewski/Downloads/loud_quiet_voice_test.wav voice 1.0 1.0`
  - `./build/VXLevelerMeasure /Users/andrzejmarczewski/Downloads/loud_quiet.wav /Users/andrzejmarczewski/Downloads/loud_quiet_mix_test.wav general 1.0 1.0`
  - `./build/VXSuitePluginRegressionTests`

---

# Mix Leveler loudness-window rework — 2026-03-23

## Problem
Even after the split architecture and the `Vocal Rider` phrase-aware pass, `Mix Leveler` still felt like a safe downward trim rather than an intelligent, neutral rider. The user screenshots showed the wet trace staying persistently low, which matched the underlying issue: the general branch still used an envelope-style baseline heuristic rather than a clearer perceptual loudness relationship.

## Plan
- [x] Replace the current `Mix Leveler` ride target with a loudness-window control law built around momentary, short-term, and baseline loudness.
- [x] Keep `Vocal Rider` unchanged while reworking only the `Mix` branch.
- [x] Rebuild, measure both modes on `loud_quiet.wav`, and rerun `VXSuitePluginRegressionTests`.
- [x] Keep the new `Mix` law only if it materially improves the real-file result without regressions.

## Review
- Reworked the `Mix Leveler` branch in [VxLevelerDsp.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/leveler/dsp/VxLevelerDsp.cpp) so it now behaves more like the intended perceptual rider:
  - `generalMomentary` now models the `L_m` view (`~400 ms`),
  - `generalShort` now models the `L_s` view (`~3 s`),
  - `generalBaseline` is a slower baseline follower,
  - the slow ride is driven by `L_s - L_b` with a deadband and shaped correction ramp,
  - the fast clamp is driven by `L_m - L_s` overshoot instead of mainly by sample-peak prediction.
- I also kept the previous `Vocal Rider` phrase-aware improvement intact, without changing that branch during this pass.
- Follow-up host debugging after the user screenshots showed the wet output still looking too low:
  - confirmed the trace is based on real dry/wet RMS history in [VxSuiteLevelTraceView.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/framework/VxSuiteLevelTraceView.cpp) and [VxSuiteSpectrumTelemetry.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/framework/VxSuiteSpectrumTelemetry.cpp), so the display is not just inventing a large offset,
  - removed the redundant product-local `OutputTrimmer` from [VxLevelerProcessor.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/leveler/VxLevelerProcessor.cpp) and [VxLevelerProcessor.h](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/leveler/VxLevelerProcessor.h), leaving only the shared framework safety trimmer,
  - expanded [VXLevelerMeasure.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/tests/VXLevelerMeasure.cpp) to report spread, RMS, and peak so future tuning can target “more even without ending up much quieter,”
  - tried more aggressive host-feel fixes for `Mix Leveler` including extra recentering and a new general-mode regression guard, but those candidates either failed the hot-mix regression or gave back too much of the actual leveling effect,
  - kept the best verified compromise from this round instead of the mathematically strongest spread result, because the user-facing problem is host feel, not spread alone.
- Final kept verified state on [loud_quiet.wav](/Users/andrzejmarczewski/Downloads/loud_quiet.wav):
  - `Vocal Rider`: `12.9225 dB -> 11.8820 dB`, RMS `-23.20 dBFS -> -24.61 dBFS`, peak `-4.57 dBFS -> -5.66 dBFS`
  - `Mix Leveler`: `12.9225 dB -> 12.7314 dB`, RMS `-23.20 dBFS -> -26.14 dBFS`, peak `-4.57 dBFS -> -6.18 dBFS`
- Kept follow-up improvement on top of that compromise:
  - added a separate `Mix`-only normalization stage in [VxLevelerDsp.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/leveler/dsp/VxLevelerDsp.cpp) with explicit dry-vs-wet baseline tracking,
  - bounded the normalization by headroom, capped it to a few dB, and backed it off when spike protection is active,
  - kept `Vocal Rider` unchanged during this pass.
- This is the first `Mix`-only output-centering pass that materially improves host-feel metrics without breaking the suite. A later refinement changed the normalizer to compare dry-vs-wet short-term loss, not just baseline loss, and then allowed a slightly stronger bounded recovery, which improved the kept `Mix` result again while leaving the regressions clean.
- Verified with:
  - `cmake --build build --target VXLevelerMeasure VXLevelerPlugin VXSuitePluginRegressionTests -j4`
  - `./build/VXLevelerMeasure /Users/andrzejmarczewski/Downloads/loud_quiet.wav /Users/andrzejmarczewski/Downloads/loud_quiet_voice_test.wav voice 1.0 1.0`
  - `./build/VXLevelerMeasure /Users/andrzejmarczewski/Downloads/loud_quiet.wav /Users/andrzejmarczewski/Downloads/loud_quiet_mix_test.wav general 1.0 1.0`
  - `./build/VXSuitePluginRegressionTests`

---
# Mix reference comparison — 2026-03-23

## Problem
We now have a second host-rendered Mix Leveler reference file and need to determine whether it is a better target than the first mix reference before doing more tuning.

## Plan
- [x] Measure and compare `loud_quiet_mix.wav` and `loud_quiet_mix_2.wav` against the dry source.
- [x] Summarize whether the second file is objectively closer to the dry target while still leveled.

## Review
- Compared [loud_quiet_mix.wav](/Users/andrzejmarczewski/Downloads/loud_quiet_mix.wav) and [loud_quiet_mix_2.wav](/Users/andrzejmarczewski/Downloads/loud_quiet_mix_2.wav) against the dry source [loud_quiet.wav](/Users/andrzejmarczewski/Downloads/loud_quiet.wav).
- Results:
  - dry: spread `13.056 dB`, RMS `-23.566 dBFS`, peak `-4.574 dBFS`
  - mix1: spread `12.780 dB`, RMS `-27.663 dBFS`, peak `-7.072 dBFS`, corr `0.85243`, RMSE `0.037848`
  - mix2: spread `12.842 dB`, RMS `-26.661 dBFS`, peak `-6.200 dBFS`, corr `0.85684`, RMSE `0.035742`
- Interpretation:
  - `mix1` levels slightly more aggressively than `mix2`,
  - but `mix2` retains more overall loudness, keeps peaks closer to the dry reference, and is slightly closer to the dry waveform overall,
  - which matches the user's listening note that the newer `Mix` result feels better balanced.
- Kept conclusion: [loud_quiet_mix_2.wav](/Users/andrzejmarczewski/Downloads/loud_quiet_mix_2.wav) is the better current `Mix Leveler` reference target.

---

# Automated Leveler tuning loop — 2026-03-23

## Problem
Manual `Leveler` tuning has reached the point where the tradeoff is hard to judge by spread numbers alone. The user asked for automated tuning passes until the product is in a stronger place, and we now have enough feedback to define a better score: preserve the smooth, non-pulsing feel, reduce spread, and keep `Mix Leveler` much closer to the dry track’s overall level.

## Plan
- [x] Expose a small internal `Mix Leveler` tuning surface so a search tool can try bounded candidate sets without rewriting source each time.
- [x] Add an automated tuning/search executable that scores candidates on dry-vs-wet correlation, spread improvement, RMS loss, and peak loss using [loud_quiet.wav](/Users/andrzejmarczewski/Downloads/loud_quiet.wav) and the better mix reference target [loud_quiet_mix_2.wav](/Users/andrzejmarczewski/Downloads/loud_quiet_mix_2.wav).
- [x] Run automated passes, inspect the best candidates, and keep only a candidate that also passes `VXSuitePluginRegressionTests`.
- [x] Document the kept tuning result and any new lesson.

## Review
- Added a small internal `Mix` tuning surface to [VxLevelerDsp.h](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/leveler/dsp/VxLevelerDsp.h) and a processor setter in [VxLevelerProcessor.h](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/leveler/VxLevelerProcessor.h) / [VxLevelerProcessor.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/leveler/VxLevelerProcessor.cpp) so search tools can evaluate bounded `Mix Leveler` candidates without source rewriting.
- Added [VXLevelerTuneSearch.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/tests/VXLevelerTuneSearch.cpp) and wired it into [CMakeLists.txt](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/CMakeLists.txt). The tool scores candidates on:
  - dry correlation,
  - correlation to [loud_quiet_mix_2.wav](/Users/andrzejmarczewski/Downloads/loud_quiet_mix_2.wav),
  - spread improvement,
  - RMS-loss penalty,
  - peak-loss penalty.
- Ran an automated 96-candidate search against [loud_quiet.wav](/Users/andrzejmarczewski/Downloads/loud_quiet.wav) and [loud_quiet_mix_2.wav](/Users/andrzejmarczewski/Downloads/loud_quiet_mix_2.wav). The best candidate materially outperformed the previous hand-tuned `Mix` default while keeping the same smooth behavior family.
- Promoted the best candidate into the default `Mix` tuning in [VxLevelerDsp.h](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/leveler/dsp/VxLevelerDsp.h).
- Final kept verified state on [loud_quiet.wav](/Users/andrzejmarczewski/Downloads/loud_quiet.wav):
  - `Vocal Rider`: spread `12.9225 dB -> 11.8820 dB`, RMS `-23.20 dBFS -> -24.61 dBFS`, peak `-4.57 dBFS -> -5.66 dBFS`
  - `Mix Leveler`: spread `12.9225 dB -> 12.7270 dB`, RMS `-23.20 dBFS -> -25.45 dBFS`, peak `-4.57 dBFS -> -6.19 dBFS`
- This is the strongest `Mix` result we have kept so far because it stays close to the better host reference while still preserving a modest levelling improvement and keeping the regression suite clean.
- Verified with:
  - `cmake -S . -B build`
  - `cmake --build build --target VXLevelerMeasure VXLevelerPlugin VXSuitePluginRegressionTests VXLevelerTuneSearch -j4`
  - `./build/VXLevelerTuneSearch /Users/andrzejmarczewski/Downloads/loud_quiet.wav /Users/andrzejmarczewski/Downloads/loud_quiet_mix_2.wav 96`
  - `./build/VXLevelerMeasure /Users/andrzejmarczewski/Downloads/loud_quiet.wav /Users/andrzejmarczewski/Downloads/loud_quiet_voice_test.wav voice 1.0 1.0`
  - `./build/VXLevelerMeasure /Users/andrzejmarczewski/Downloads/loud_quiet.wav /Users/andrzejmarczewski/Downloads/loud_quiet_mix_test.wav general 1.0 1.0`
  - `./build/VXSuitePluginRegressionTests`

---
# Mix reference comparison 2 — 2026-03-23

## Problem
A new Mix Leveler render (`loud_quiet_mix_4.wav`) may expose a different failure mode: the vocal feels too quiet, suggesting the mix mode may not be balancing all material equally. We need to compare it with the current better mix reference before using it as a tuning target.

## Plan
- [x] Measure and compare `loud_quiet_mix_2.wav` and `loud_quiet_mix_4.wav` against the dry source.
- [x] Summarize whether `mix_4` is a better target or evidence of a worse balance failure.

## Review
- Compared [loud_quiet_mix_2.wav](/Users/andrzejmarczewski/Downloads/loud_quiet_mix_2.wav) and [loud_quiet_mix_4.wav](/Users/andrzejmarczewski/Downloads/loud_quiet_mix_4.wav) against the dry source [loud_quiet.wav](/Users/andrzejmarczewski/Downloads/loud_quiet.wav).
- Results:
  - dry: spread `13.056 dB`, RMS `-23.566 dBFS`, peak `-4.574 dBFS`
  - mix2: spread `12.842 dB`, RMS `-26.661 dBFS`, peak `-6.200 dBFS`, corr `0.85684`, RMSE `0.035742`
  - mix4: spread `12.844 dB`, RMS `-25.954 dBFS`, peak `-6.187 dBFS`, corr `0.87185`, RMSE `0.033330`
- Interpretation:
  - `mix4` is essentially the same amount of levelling as `mix2`,
  - but it retains more overall level and is measurably closer to the dry track,
  - so it is the better current overall `Mix Leveler` reference.
- Important caveat from the user's listening note:
  - even though `mix4` is the better overall target, it may also expose a remaining content-balance issue, namely that `Mix Leveler` can still leave the vocal too quiet relative to the accompaniment.
- Kept conclusion: [loud_quiet_mix_4.wav](/Users/andrzejmarczewski/Downloads/loud_quiet_mix_4.wav) is the better current global `Mix Leveler` target, but it also highlights that `Mix` still needs better equality across content, not just better overall loudness retention.

---
# Leveler V3 implementation — 2026-03-23

## Problem
`VXLeveler` still behaves like a strong realtime rider rather than the fuller product defined in [levelerv3.md](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/levelerv3.md). The missing pieces are a true target-generation subsystem for `Mix`, Smart Realtime global loudness memory with confidence-based blending, and the first offline-analysis path so the plugin can stop treating long-form material as purely reactive.

## Plan
- [x] Refactor `Mix Leveler` so target generation is its own subsystem instead of being embedded inline with the gain math.
- [x] Add Smart Realtime global loudness tracking and confidence-based blending between local and global targets.
- [x] Keep `Vocal Rider` stable while landing the new `Mix` architecture.
- [x] Add offline-analysis scaffolding and target-provider plumbing so `Mix` can support both Smart Realtime and Offline without duplicating downstream gain code.
- [x] Add user-facing analysis-mode control and status messaging for `Mix`.
- [x] Rebuild, remeasure on `loud_quiet.wav`, compare against the newer host references, and rerun `VXSuitePluginRegressionTests` after each kept phase.

## Review
- Landed the V3 architectural split for `Mix Leveler`:
  - added a reusable auxiliary selector path to the shared framework so products can expose a second choice control without custom UI forks,
  - used that in [VxLevelerProcessor.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/leveler/VxLevelerProcessor.cpp) as `Analysis: Realtime / Smart Realtime / Offline`,
  - kept `Vocal Rider` on its proven branch and moved only `Mix` onto the new target-generation path.
- Added [VxLevelerGlobalLoudnessTracker.h](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/leveler/dsp/VxLevelerGlobalLoudnessTracker.h) and [VxLevelerGlobalLoudnessTracker.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/leveler/dsp/VxLevelerGlobalLoudnessTracker.cpp):
  - it consumes blockwise short-term loudness observations,
  - maintains long-memory baseline/upper estimates with a confidence score,
  - gives `Mix` the new Smart Realtime “learn the track over time” behavior from `levelerv3.md`.
- Added [VxLevelerOfflineAnalyzer.h](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/leveler/dsp/VxLevelerOfflineAnalyzer.h) and [VxLevelerOfflineAnalyzer.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/leveler/dsp/VxLevelerOfflineAnalyzer.cpp):
  - this is the first real offline-analysis path and target-curve generator,
  - the plugin falls back cleanly to Smart Realtime when no offline map is available in-host,
  - the measure harness can already exercise the offline path by pre-analyzing the full file.
- Refactored [VxLevelerDsp.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/leveler/dsp/VxLevelerDsp.cpp) so `Mix` target generation is now mode-specific:
  - `Realtime` uses the local target only,
  - `Smart Realtime` blends local and global targets with shaped confidence,
  - `Offline` consumes a precomputed target map when available.
- Verification:
  - `cmake --build build --target VXLevelerPlugin VXLevelerMeasure VXSuitePluginRegressionTests -j4`
  - `./build/VXLevelerMeasure /Users/andrzejmarczewski/Downloads/loud_quiet.wav /Users/andrzejmarczewski/Downloads/loud_quiet_voice_v3.wav voice 1.0 1.0 smart`
  - `./build/VXLevelerMeasure /Users/andrzejmarczewski/Downloads/loud_quiet.wav /Users/andrzejmarczewski/Downloads/loud_quiet_mix_v3_realtime.wav general 1.0 1.0 realtime`
  - `./build/VXLevelerMeasure /Users/andrzejmarczewski/Downloads/loud_quiet.wav /Users/andrzejmarczewski/Downloads/loud_quiet_mix_v3_smart.wav general 1.0 1.0 smart`
  - `./build/VXLevelerMeasure /Users/andrzejmarczewski/Downloads/loud_quiet.wav /Users/andrzejmarczewski/Downloads/loud_quiet_mix_v3_offline.wav general 1.0 1.0 offline`
  - `./build/VXSuitePluginRegressionTests`
- Kept measurements on [loud_quiet.wav](/Users/andrzejmarczewski/Downloads/loud_quiet.wav):
  - `Vocal Rider` (`Smart` path selected but ignored): spread `12.9225 -> 11.8820 dB`, RMS `-23.20 -> -24.61 dBFS`, peak `-4.57 -> -5.66 dBFS`
  - `Mix Leveler Realtime`: spread `12.9225 -> 12.7270 dB`, RMS `-23.20 -> -25.45 dBFS`, peak `-4.57 -> -6.19 dBFS`
  - `Mix Leveler Smart Realtime`: spread `12.9225 -> 12.5274 dB`, RMS `-23.20 -> -26.59 dBFS`, peak `-4.57 -> -6.08 dBFS`
  - `Mix Leveler Offline`: spread `12.9225 -> 11.5872 dB`, RMS `-23.20 -> -27.01 dBFS`, peak `-4.57 -> -6.57 dBFS`
- Additional comparison against the user's preferred [loud_quiet_mix_4.wav](/Users/andrzejmarczewski/Downloads/loud_quiet_mix_4.wav):
  - `Realtime` remains almost identical to that render,
  - `Smart Realtime` is measurably closer to the dry file than `mix_4`,
  - `Offline` is more assertive still, but also quieter.
- Honest conclusion:
  - the V3 architecture is now real and regression-safe,
  - but the next meaningful decision is sonic rather than structural: we need listening feedback on whether `Smart Realtime` actually feels better than the old `Realtime` path before deciding what should be the default `Mix` behavior.

---
# Leveler V3 Smart startup tuning — 2026-03-24

## Problem
`Mix Leveler` `Smart Realtime` was ending up with the right broad shape but still starting the file poorly: it learned too late, and the opening section stayed noticeably quieter than `Offline`. The risk was continuing to guess at startup heuristics without seeing what the Smart engine itself believed during those first seconds.

## Plan
- [x] Add lightweight debug tracing to the `VXLevelerMeasure` harness so Smart `Mix` state can be inspected over the opening seconds without changing the plugin UI/runtime path.
- [x] Use that trace to identify whether the early mismatch comes from confidence, baseline learning, ride gain, spike clamp, or normalization.
- [x] Implement the narrowest verified startup fix that improves the intro while keeping later sections stable and the regression suite green.
- [x] Record the kept result and the lesson from the pass.

## Review
- Extended [VXLevelerMeasure.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/tests/VXLevelerMeasure.cpp) with an optional trace pass that logs Smart `Mix` state every `0.5 s` for the first requested seconds. Added reusable debug snapshots through [VxLevelerDsp.h](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/leveler/dsp/VxLevelerDsp.h), [VxLevelerDsp.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/leveler/dsp/VxLevelerDsp.cpp), [VxLevelerProcessor.h](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/leveler/VxLevelerProcessor.h), and [VxLevelerProcessor.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/leveler/VxLevelerProcessor.cpp).
- The trace showed the real startup failure mode: Smart was letting near-silent opening material seed the `Mix` loudness engine, so by `0.5 s` its local baseline was still around `-80 dB`, which pushed the slow ride and spike clamp far too hard before the normalizer had enough time to recover.
- Kept fix in [VxLevelerDsp.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/leveler/dsp/VxLevelerDsp.cpp):
  - added a `generalPrimed` gate,
  - prevented `Mix` from priming its loudness engine from near-silent opening material,
  - and left the signal on the delayed dry path until real activity appeared.
- Kept measured result on `/Users/andrzejmarczewski/Downloads/loud_quiet.wav` with `Mix Leveler` `Smart Realtime`:
  - spread `12.9225 -> 12.5455 dB`
  - RMS `-23.2013 -> -26.5922 dBFS`
  - peak `-4.5736 -> -5.9820 dBFS`
- More importantly for host feel, the opening `0-5 s` section improved from `-37.9983 dB` to `-37.0481 dB`, and the opening slices improved materially:
  - `0-1 s`: `-46.50 -> -41.67 dB`
  - `1-2 s`: `-42.85 -> -40.90 dB`
  - `2-3 s`: `-39.81 -> -38.36 dB`
- Later sections stayed effectively stable or slightly improved:
  - `5-10 s`: `-27.85 -> -27.60 dB`
  - `10-15 s`: unchanged at `-23.35 dB`
  - `15-20 s`: effectively unchanged
  - `20-23 s`: effectively unchanged
- Verification:
  - `cmake --build build --target VXLevelerMeasure VXLevelerPlugin VXSuitePluginRegressionTests -j4`
  - `./build/VXLevelerMeasure /Users/andrzejmarczewski/Downloads/loud_quiet.wav /Users/andrzejmarczewski/Downloads/loud_quiet_mix_v3_smart_primed.wav general 1.0 1.0 smart 6.0`
  - `./build/VXSuitePluginRegressionTests`

---
# Leveler V3 final cleanup + harness sanity — 2026-03-24

## Problem
After the Smart startup fix, a later control-law tightening pass made `Smart Realtime` behave more like a broad level drop again, and the `VXLevelerMeasure` trace path also turned out to be capable of telling a different story from the plain render path because it traced and then reused the same processor instance. Before calling the current `Leveler` state clean, we needed to keep the real startup fix, reject the over-tight tuning, and make the measurement harness trustworthy.

## Plan
- [x] Revert the Smart/Offline control-law changes that worsened the real per-section dry deltas.
- [x] Keep the actual priming-based startup fix in `Mix Leveler` Smart Realtime.
- [x] Fix `VXLevelerMeasure` so trace runs and ordinary renders use separate processor instances.
- [x] Rebuild, rerun plain and traced Smart renders plus the regression suite, and record the final kept state.

## Review
- Rejected the “product-grade tightening” pass that added dynamic-range-aware deadband/limits and adaptive offline blend. On the real file it made `Smart Realtime` read more like a broad volume drop again and made `Offline` worse on spread, so those DSP changes were backed out.
- Kept the actual `Smart Realtime` startup improvement in [VxLevelerDsp.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/leveler/dsp/VxLevelerDsp.cpp):
  - `Mix` still uses the priming gate that avoids learning from near-silent intro material,
  - and `Offline` keeps the safer time-based target indexing rather than block-increment drift.
- Fixed the harness in [VXLevelerMeasure.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/tests/VXLevelerMeasure.cpp) so trace mode uses a dedicated processor instance and can no longer bias the subsequent render path.
- Final verified `Smart Realtime` on `/Users/andrzejmarczewski/Downloads/loud_quiet.wav`:
  - spread `12.9225 -> 12.5296 dB`
  - RMS `-23.2013 -> -26.5924 dBFS`
  - peak `-4.5736 -> -5.9828 dBFS`
- Final verified `Vocal Rider` remains:
  - spread `12.9225 -> 11.8820 dB`
  - RMS `-23.2013 -> -24.6122 dBFS`
  - peak `-4.5736 -> -5.6580 dBFS`
- The verified plain and traced Smart renders now match again, which means the current rendered references are trustworthy.
- Verification:
  - `cmake --build build --target VXLevelerMeasure VXLevelerPlugin VXSuitePluginRegressionTests -j4`
  - `./build/VXLevelerMeasure /Users/andrzejmarczewski/Downloads/loud_quiet.wav /Users/andrzejmarczewski/Downloads/loud_quiet_mix_v3_smart_plaincheck.wav general 1.0 1.0 smart`
  - `./build/VXLevelerMeasure /Users/andrzejmarczewski/Downloads/loud_quiet.wav /Users/andrzejmarczewski/Downloads/loud_quiet_mix_v3_smart_trace_sanity.wav general 1.0 1.0 smart 6.0`
  - `./build/VXSuitePluginRegressionTests`

---
# VX Rebalance v2 ML scaffolding build — 2026-03-24

## Problem
The v2 ML spec was written, but the product code still only had heuristic DSP. We needed a real compileable architecture seam for ML-guided masks without pretending trained weights or an ONNX runtime already existed in the repo.

## Plan
- [x] Add a rebalance-local ML scaffolding layer with feature analysis, confidence tracking, and model discovery/status.
- [x] Wire the processor to own that runner and hand optional ML masks into the DSP.
- [x] Keep the current heuristic engine as the truthful fallback path when no rebalance model/runtime is present.
- [x] Rebuild the plugin and measurement harness and verify that neutral/fallback behaviour still works.

## Review
- Added the first `VX Rebalance` v2 ML scaffolding under:
  - [VxRebalanceFeatureBuffer.h](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/rebalance/ml/VxRebalanceFeatureBuffer.h)
  - [VxRebalanceFeatureBuffer.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/rebalance/ml/VxRebalanceFeatureBuffer.cpp)
  - [VxRebalanceConfidence.h](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/rebalance/ml/VxRebalanceConfidence.h)
  - [VxRebalanceConfidence.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/rebalance/ml/VxRebalanceConfidence.cpp)
  - [VxRebalanceModelRunner.h](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/rebalance/ml/VxRebalanceModelRunner.h)
  - [VxRebalanceModelRunner.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/rebalance/ml/VxRebalanceModelRunner.cpp)
- Wired the processor and DSP seam in:
  - [VxRebalanceProcessor.h](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/rebalance/VxRebalanceProcessor.h)
  - [VxRebalanceProcessor.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/rebalance/VxRebalanceProcessor.cpp)
  - [VxRebalanceDsp.h](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/rebalance/dsp/VxRebalanceDsp.h)
  - [VxRebalanceDsp.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/rebalance/dsp/VxRebalanceDsp.cpp)
- The new runner currently:
  - analyses incoming blocks into lightweight features,
  - smooths a rebalance-specific confidence estimate,
  - discovers optional rebalance model assets,
  - reports truthful status text,
  - and publishes an optional ML mask snapshot to the DSP.
- The DSP now supports ML-guided mask blending when a future runner provides real masks, but today it remains on the heuristic path because there is no shipped rebalance model/runtime yet.
- Updated build wiring in [CMakeLists.txt](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/CMakeLists.txt) so both the plugin and [VXRebalanceMeasure.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/tests/VXRebalanceMeasure.cpp) include the new v2 scaffolding.
- Verification:
  - `cmake --build build --target VXRebalancePlugin VXRebalanceMeasure -j4`
  - `./build/VXRebalanceMeasure /Users/andrzejmarczewski/Downloads/brightside_stems 6.0 phone`
- Verified current fallback contract:
  - neutral remains exact: `Neutral diff rms=0 peak=0`
  - plugin/harness build cleanly with the new ML architecture seam in place
  - current behaviour is still heuristic because no rebalance ONNX model is present

### Follow-up: v2.0 realtime path
- Promoted the generic ML seam into the concrete **v2.0 shipping path**:
  - explicit **4-head** model contract (`Vocals / Drums / Bass / Other`)
  - explicit DSP-side `Guitar` derivation from `Other`
  - truthful product status text around the v2.0 path
- Updated:
  - [VxRebalanceModelRunner.h](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/rebalance/ml/VxRebalanceModelRunner.h)
  - [VxRebalanceModelRunner.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/rebalance/ml/VxRebalanceModelRunner.cpp)
  - [VxRebalanceDsp.h](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/rebalance/dsp/VxRebalanceDsp.h)
  - [VxRebalanceDsp.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/rebalance/dsp/VxRebalanceDsp.cpp)
  - [VX_REBALANCE_V2_SPEC.md](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/docs/VX_REBALANCE_V2_SPEC.md)
- The DSP-side derived guitar lane now uses:
  - model `Other`
  - harmonic/steady priors
  - penalties from vocal, drum/transient, and bass ownership
  - renormalization back into the five-lane competition model
- Verification:
  - `cmake --build build --target VXRebalancePlugin VXRebalanceMeasure -j4`
  - `./build/VXRebalanceMeasure /Users/andrzejmarczewski/Downloads/brightside_stems 6.0 phone`
- Verified result:
  - build clean
  - neutral still exact: `Neutral diff rms=0 peak=0`
  - because there is still no shipped rebalance ONNX model, current audible behaviour remains the heuristic fallback even though the v2.0 4-head + derived-guitar architecture is now the code path contract

### Follow-up: official v2.0 baseline model fetched
- Pulled the official Open-Unmix `umxhq_spec` 4-stem baseline into:
  - [assets/rebalance/models/openunmix_umxhq_spec/README.md](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/assets/rebalance/models/openunmix_umxhq_spec/README.md)
  - [assets/rebalance/models/openunmix_umxhq_spec/separator.json](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/assets/rebalance/models/openunmix_umxhq_spec/separator.json)
  - `vocals.pth`, `drums.pth`, `bass.pth`, `other.pth`
- Wrote local metadata files so the bundle is reproducible and discoverable as the v2.0 baseline.
- Updated [VxRebalanceModelRunner.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/rebalance/ml/VxRebalanceModelRunner.cpp) so the plugin can now detect the local Open-Unmix v2.0 bundle and report it as found.
- Verification:
  - `cmake --build build --target VXRebalancePlugin -j4`
- Current honest state:
  - official baseline weights are now in the repo
  - plugin can detect that the v2.0 bundle exists
  - actual in-plugin inference still needs the dedicated runtime/conversion step

### Follow-up: ONNX conversion + runnable inference bridge
- Added a practical v2.0 bridge tool at [rebalance_openunmix_v20.py](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/tools/rebalance_openunmix_v20.py).
- The tool now supports:
  - `export`: load the local official Open-Unmix `umxhq_spec` 4-head bundle and export a **combined** ONNX model
  - `infer`: run that ONNX model on audio, normalize 4-head masks, derive `Guitar` from `Other`, and write mask tensors/reports for tuning
- Kept the export aligned with the practical v2.0 architecture:
  - no full `Separator` export
  - no STFT/ISTFT inside ONNX
  - one combined spectral model with output shape `(batch, 4, 2, 2049, frames)`
- Produced the exported ONNX bundle:
  - [assets/rebalance/models/openunmix_umxhq_spec_onnx/vx_rebalance_umx4.onnx](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/assets/rebalance/models/openunmix_umxhq_spec_onnx/vx_rebalance_umx4.onnx)
  - [assets/rebalance/models/openunmix_umxhq_spec_onnx/rebalance_umx4.json](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/assets/rebalance/models/openunmix_umxhq_spec_onnx/rebalance_umx4.json)
- Validation:
  - `python3 tools/rebalance_openunmix_v20.py export --validate`
  - export max abs error: about `5.0e-06`
- Produced the first real inference output:
  - [tasks/reports/rebalance-openunmix-v20/brightside_masks.npz](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/tasks/reports/rebalance-openunmix-v20/brightside_masks.npz)
  - [tasks/reports/rebalance-openunmix-v20/brightside_masks.json](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/tasks/reports/rebalance-openunmix-v20/brightside_masks.json)
- First inference summary on `/Users/andrzejmarczewski/Downloads/brightside_stems/brightside_original.wav`:
  - 4-head mean share: `vocals 0.297`, `drums 0.545`, `bass 0.019`, `other 0.139`
  - 5-lane derived mean share: `vocals 0.297`, `drums 0.545`, `bass 0.019`, `guitar 0.012`, `other 0.126`
- Updated [VxRebalanceModelRunner.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/rebalance/ml/VxRebalanceModelRunner.cpp) so the plugin can detect the exported ONNX bundle as the v2.0 model asset.
- Verification:
  - `cmake --build build --target VXRebalancePlugin -j4`
- Current honest state:
  - ONNX export and offline inference are now real and verified
  - native in-plugin C++ ONNX inference still needs runtime header packaging/integration before the plugin can consume these masks directly

### Follow-up: native ONNX runtime integration made real
- Integrated a native in-plugin ONNX path for `VX Rebalance` using the exported Open-Unmix v2.0 baseline instead of the heuristic-only fallback architecture.
- Added a dedicated runtime wrapper at:
  - [VxRebalanceOnnxModel.h](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/rebalance/ml/VxRebalanceOnnxModel.h)
  - [VxRebalanceOnnxModel.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/rebalance/ml/VxRebalanceOnnxModel.cpp)
- Important implementation notes:
  - vendored the minimal ONNX Runtime headers into `ThirdParty/onnxruntime/include`
  - linked the local Python-installed `libonnxruntime` as an imported CMake runtime
  - switched the native wrapper from the ONNX C++ API to the lower-level C API after repeated teardown crashes from the header-only C++ wrapper path
  - fixed a real model contract bug: the exported Open-Unmix ONNX bundle uses `64` frames, not `8`
  - added ONNX tensor-shape validation at prepare time so future export/runtime mismatches fail cleanly instead of corrupting memory
- Updated:
  - [CMakeLists.txt](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/CMakeLists.txt)
  - [VxRebalanceModelRunner.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/rebalance/ml/VxRebalanceModelRunner.cpp)
  - [VxRebalanceModelRunner.h](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/rebalance/ml/VxRebalanceModelRunner.h)
  - [VxRebalanceOnnxModel.h](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/rebalance/ml/VxRebalanceOnnxModel.h)
  - [VxRebalanceOnnxModel.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/rebalance/ml/VxRebalanceOnnxModel.cpp)
  - [tests/VXRebalanceMeasure.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/tests/VXRebalanceMeasure.cpp)
- Verification:
  - `python3 tools/rebalance_openunmix_v20.py export --validate`
  - `cmake --build build --target VXRebalance VXRebalancePlugin VXRebalanceMeasure -j1`
  - `script -q /dev/null ./build/VXRebalanceMeasure /Users/andrzejmarczewski/Downloads/brightside_stems 6.0 studio`
- Verified result:
  - status now reports `V2.0 UMX4 masks active`
  - neutral remains exact: `Neutral diff rms=0 peak=0`
  - the native ML path is now active against the local exported Open-Unmix model instead of only detecting it
- Current limitation:
  - none for common host rates: the model path now uses an analysis-only resampling layer so the fixed `44.1 kHz` Open-Unmix model can stay active in `48 kHz` sessions too

### Follow-up: 48 kHz sample-rate support for the ML path
- Reused the same lightweight resampling approach already proven in the DeepFilterNet runtime path, but only on the analysis/model side so the audio path itself stays in the host sample rate.
- Updated [VxRebalanceModelRunner.h](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/rebalance/ml/VxRebalanceModelRunner.h) and [VxRebalanceModelRunner.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/products/rebalance/ml/VxRebalanceModelRunner.cpp):
  - added per-channel analysis resamplers targeting the fixed Open-Unmix model rate (`44.1 kHz`)
  - resampled input blocks into the ML FIFO before STFT/model inference
  - corrected model-bin mapping so DSP bins are projected into the model spectrum using the model sample rate instead of the host sample rate
  - removed the old hard `44.1 kHz` gate from model activation
- Verification:
  - `cmake --build build --target VXRebalance VXRebalancePlugin VXRebalanceMeasure -j1`
  - `script -q /dev/null ./build/VXRebalanceMeasure /Users/andrzejmarczewski/Downloads/brightside_stems 6.0 studio`
  - created a temporary `48 kHz` stem pack with `ffmpeg`
  - `script -q /dev/null ./build/VXRebalanceMeasure /tmp/vxrebalance48.nEOPER 6.0 studio`
- Verified result:
  - both `44.1 kHz` and `48 kHz` runs report `V2.0 UMX4 masks active`
  - neutral remains exact in both runs: `Neutral diff rms=0 peak=0`

### Follow-up: embed the Rebalance model into the VST bundle
- Updated [CMakeLists.txt](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/CMakeLists.txt) so `VXRebalance` now bundles its ONNX model assets into `Contents/Resources` during VST staging.
- Packaged assets:
  - [vx_rebalance_umx4.onnx](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/assets/rebalance/models/openunmix_umxhq_spec_onnx/vx_rebalance_umx4.onnx)
  - [rebalance_umx4.json](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/assets/rebalance/models/openunmix_umxhq_spec_onnx/rebalance_umx4.json)
- Verification:
  - `cmake --build build --target VXRebalancePlugin -j1`
  - confirmed staged bundle resources:
    - [Source/vxsuite/vst/VXRebalance.vst3/Contents/Resources/rebalance_umx4.json](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/vst/VXRebalance.vst3/Contents/Resources/rebalance_umx4.json)
    - [Source/vxsuite/vst/VXRebalance.vst3/Contents/Resources/vx_rebalance_umx4.onnx](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/Source/vxsuite/vst/VXRebalance.vst3/Contents/Resources/vx_rebalance_umx4.onnx)
- Result:
  - `VX Rebalance.vst3` is now self-contained and no longer depends on repo-relative asset lookup for the shipped model

### Review
- Root cause of the native-runtime crash was not “ONNX is unstable” but a contract mismatch between the exported model and the runner: the ONNX bundle had a fixed `64`-frame output while the runner allocated for `8`.
- The ONNX C++ wrapper also proved fragile in this local vendored-header + imported-dylib setup; the plain C API is the safer runtime boundary for this repo.
- `VX Rebalance` now has a real ML path, but it still needs one more pass for sample-rate flexibility and then a fresh tuning pass on the ML masks themselves.

### Follow-up: exact redesign trial
- I also trialed the proposed `Mix` redesign seams directly:
  - error blending instead of target blending,
  - zoned confidence shaping,
  - dynamic-range-aware ride/spike/upward limits,
  - adaptive offline local/global blend.
- On the real `loud_quiet.wav` file, the `Smart Realtime` versions with those changes consistently regressed toward “broad volume drop” behavior, especially in the intro, even when the architecture looked cleaner on paper.
- I restored the best verified `Smart` baseline after those trials. The current kept build is:
  - [loud_quiet_mix_v3_restored_best.wav](/Users/andrzejmarczewski/Downloads/loud_quiet_mix_v3_restored_best.wav)
  - spread `12.9225 -> 12.5376 dB`
  - RMS `-23.2013 -> -26.5655 dBFS`
  - peak `-4.5736 -> -6.1602 dBFS`
- The exact redesign review is still useful as a direction, but on this material the drop-in version did not beat the simpler primed Smart baseline.
