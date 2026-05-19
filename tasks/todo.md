# Multi-product regression fix pass - 2026-05-13

## Goal
Fix the user-reported regressions: Subtract crackling during learn, Finish clipping,
Analyser flickering, Cleanup weakness, Rebalance needs more strength, Proximity low-end loss.

## Plan

- [ ] Subtract: remove `resetStreamingState()` from `finalizeLearnStopTransition()` — it clears the
      STFT pipeline after learn ends, creating a 32ms hole and reconstruction artifacts (crackling/stutter)
- [ ] Finish: add 0dBFS hard clip after recovery gain; the makeup recovery applies after the limiter
      so the signal can exceed 0dBFS on loud material
- [ ] Analyser: make domain binding sticky — currently rebinds every 4096 samples to the "latest"
      domain even when the current binding is still alive, causing effects to flicker off briefly
- [ ] Cleanup: lower `shelfProtect` floor (0.50→0.28) and `driftGuard` floor (0.42→0.22) in
      `VxStudioCorrectiveStage.cpp` — multiplicative guard stack makes vocal cleanup near-inaudible at 100%
- [ ] Rebalance: raise contribution ceiling from 2.0× to 3.0× and use steeper curve (0.78→0.62 exponent)
      so full-travel sliders produce a clearly decisive move
- [ ] Proximity: raise lowFc range (85-135Hz→120-200Hz voice; 95-180Hz→140-260Hz general) so the
      bass boost affects audible body/warmth, not just sub rumble; reduce mudCutDb depth ~25%
- [ ] Build and run regression suite; update this file with review notes

## Review

# Headroom review - 2026-05-15

## Goal
Review headroom handling across VxStudio VST effects, starting with `Proximity`, to find why stacked
effects can clip and identify the safest fix.

## Plan

- [x] Inspect prior effect-audit notes for `Proximity` and any existing max-effect/headroom reports
- [x] Trace `Proximity` processor/editor/framework signal path and locate gain/output limiting stages
- [x] Compare headroom policy across other VxStudio effects to find whether clipping is product-specific or suite-wide
- [x] Identify the root cause for stacked clipping and implement the minimal safe fix
- [x] Run targeted verification and record review notes here

## Review

- Root cause: stacked clipping was mainly a gain-staging problem in the additive shapers, not a missing hard safety stage. `ProcessorBase` already applies a final emergency `OutputTrimmer`, and `Proximity` / `Tone` also had local trimmers, but both products were still allowed to run too close to full scale under strong boost settings.
- `Proximity` already had a compensating `outputTrimDb` model, but it was capped too low for the current boost range. I increased that compensation and tightened the local trimmer setup so it stays an emergency guard rather than the normal operating path.
- `Tone` had no nominal output compensation at all, so strong bass/treble boosts were relying mostly on the local/final trimmers. I added boost-aware output trim smoothing plus a slightly stricter local trimmer setup.
- Verification: `cmake --build build --target VXStudioPluginRegressionTests -j4` passed, and `./build/VXStudioPluginRegressionTests` completed with exit code `0`, including a new stacked `Proximity -> Tone -> Finish` headroom regression.

# Suite-wide headroom audit - 2026-05-15

## Goal
Audit how each VxStudio VST manages level, gain recovery, peak output, and clipping safety, then rank
which products follow good headroom practice and which still need framework or DSP cleanup.

## Plan

- [x] Review shared framework output safety and define the audit criteria
- [x] Inspect each product's gain staging, makeup, limiter, and trim path
- [x] Classify products by headroom discipline and identify the main risks
- [x] Record audit conclusions and next recommended fixes

## Review

- Shared framework status: good emergency safety, not sufficient by itself as a best-practice gain policy. Every plugin gets a final sample-peak trimmer in `ProcessorBase`, but `OutputTrimmer` is intentionally simple and should stay a rare last resort rather than a normal shaping stage.
- Strongest gain-discipline products: `Finish`, `OptoComp`, `Leveler`, and `Cleanup`. They all compute recovery or makeup against measured loss and/or predicted peak ceilings before the final safety stage.
- Middle tier: `Denoiser`, `Deverb`, and `DeepFilterNet`. They have some bounded compensation or mostly subtractive behavior, but they do not all prove headroom as explicitly as the best tier.
- Weakest tier: additive shapers and unconstrained source rebalancers. `Tone` and `Proximity` were the clearest examples because they could generate large boosts and previously leaned too much on trimming; `Rebalance` still has high theoretical gain authority in the DSP and mostly relies on the framework safety stage.
- Main suite-wide gap: no shared telemetry or regression proving that the framework/product trimmers stay mostly idle under intended operating ranges. That makes it easy for a product to “work” while still violating good internal headroom practice.

- Follow-up change: `Rebalance` now uses the shared framework `OutputTrimmer` as a product-local emergency guard rather than adding a bespoke limiter path. That keeps the fix inside the existing framework vocabulary and avoids forking a one-off dynamics stage into the product.
- Mode-policy decision: `Rebalance` still does not participate in shared `Voice/General` mode, and that is intentional. The framework rule is to use `Vocal`/`General` only when the DSP genuinely needs it; `Rebalance` is currently better expressed as `Studio / Live / Phone-Rough` recording types, so this pass did not force an artificial voice/general layer into it.
- Verification: `cmake --build build --target VXStudioPluginRegressionTests -j4` passed, and `./build/VXStudioPluginRegressionTests` completed with exit code `0` after adding a new strong-setting headroom regression for `Rebalance` across recording types.
- Framework follow-up: `OutputTrimmer` now reports current and max observed reduction/activity, and `ProcessorBase` exposes those values so suite tests can assert that the final emergency trimmer stays mostly idle under nominal strong settings.
- Regression follow-up: a new framework-level headroom test now checks `Tone`, `Proximity`, `Finish`, and `Rebalance` against max observed framework trim reduction, so future gain-staging regressions get caught before they turn into stacking/clipping reports.
- Product-local follow-up: products with local shared `OutputTrimmer` stages now expose max observed local trim reduction for tests. A new local-idle regression covers `Tone`, `Proximity`, `Finish`, `OptoComp`, `Cleanup`, and `Denoiser`.
- `Rebalance` remains a documented exception to the generic local-idle rule for now. It has a shared local trimmer and stronger nominal stacked-boost trim, but its current output law still makes the local guard part of real strong-setting behavior, so it stays covered by dedicated `Rebalance` headroom tests rather than the generic local-idle threshold.
- Rebalance structure follow-up: first refactor slice landed without behavior change. `computeMasks()` now delegates frame-context derivation and the object-analysis refresh pipeline to dedicated helpers, which reduces the amount of unrelated responsibility held in the top of the mask-construction path and gives the next cleanup pass cleaner seams.
- Rebalance structure follow-up: second refactor slice also landed without behavior change. The semantic-support analysis and conditioned-mask shaping inside `computeMasks()` now live behind dedicated helpers, so the main bin loop reads more clearly as raw-weight construction -> harmonic influence -> semantic conditioning -> smoothing, instead of carrying the entire heuristic stack inline.
- Verification: after the second slice, `cmake --build build --target VXStudioPluginRegressionTests -j4` passed and `./build/VXStudioPluginRegressionTests` completed with exit code `0`.
- Rebalance structure follow-up: third refactor slice landed without behavior change. Harmonic-cluster influence on per-bin raw weights now lives in its own helper, which leaves `computeMasks()` holding the sequence of decisions rather than the full cluster-weighting math inline.
- Verification: after the third slice, `cmake --build build --target VXStudioPluginRegressionTests -j4` passed and `./build/VXStudioPluginRegressionTests` completed with exit code `0`.
- Rebalance structure completion: the remaining midrange arbitration and per-bin smoothing/normalization work now also live behind focused helpers, and the temporary 2D raw-weight scratch buffer is gone. `computeMasks()` now reads as a straightforward pipeline: build frame context, refresh object analysis, derive per-bin raw weights, apply harmonic/midrange shaping, build semantic support, condition masks, smooth, then run the post-frame ownership/persistence passes.
- Finish state: this is a sensible stopping point for the monolith cleanup. The function is no longer carrying unrelated heuristics inline, behavior stayed unchanged, and the remaining helper bodies are cohesive enough that further splitting would likely be churn rather than a quality win.
- Verification: after the completion pass, `cmake --build build --target VXStudioPluginRegressionTests -j4` passed and `./build/VXStudioPluginRegressionTests` completed with exit code `0`.
