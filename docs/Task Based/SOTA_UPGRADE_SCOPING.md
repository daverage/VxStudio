# SOTA Upgrade Scoping — Rebalance Neural Masks & Deverb Learned Model

Source: tasks/Full Review.md §6 items 11–12 (2026-07-28). These are the two
strategic items from the audit. Both need product decisions before code lands;
this doc records what exists, what's missing, and the recommended path.

## 11. Neural mask guidance into shipping VXRebalance

**What already exists**
- `Dsp::setAiMaskFrame()` + `AiMaskFrame` (confidence-weighted per-source,
  per-bin masks) are fully wired inside `VxRebalanceDsp` — the ownership
  routine consumes them (`frame.aiGuided`, neural gate at
  VxRebalanceDsp.cpp `buildOwnershipFrameForBin`). Verified: **no shipping
  call site** feeds it; it is dormant.
- A complete ONNX stem-separation backend exists in
  `products/rebalance/ai/` (`OnnxStemgenBackend`, `VxRealtimeStemSplitter`,
  `VxAiStemRebalanceDsp`) behind `VXSTUDIO_ENABLE_REBALANCE_AI=OFF`
  (experimental tool, hard FATAL_ERROR if assets missing).

**What's missing to ship it**
1. A small mask model (Open-Unmix-class, ~10–30 MB per checkpoint) producing
   5-source per-bin masks at the plugin's FFT resolution — the current
   experimental backend targets full stem splitting, which is heavier than
   guidance masks need.
2. Inference must run off the audio thread (DenoiserDsp worker pattern —
   masks are guidance, one-frame staleness is fine).
3. Asset distribution: embed vs download-on-demand (DFN already has the
   `ModelAssetService` download path — reuse it).
4. Licensing check on chosen weights (Open-Unmix UMX weights are CC BY-NC-SA —
   **not usable commercially**; would need retraining on MUSDB-licensed-clear
   data or a permissive alternative).

**Decision needed from you:** model sourcing (train/license/buy) and asset
size budget. Code-side risk is low once a model exists.

## 12. Learned-dereverb model slot for VXDeverb

**What already exists**
- Classical stack (LRSV + RT60 + RLS-WPE) is research-grade per the audit.
- DFN service demonstrates the exact integration pattern needed: 48 kHz
  resampled frame loop, model file management, startup holdback, dual-bundle
  atomic engine swap.

**Recommended path**
- Define a `DeverbModelSlot` mirroring `DeepFilterService`'s shape (prepare /
  processRealtime / capability query), defaulting to "unavailable" so the
  classical stack remains the shipping path.
- Candidate models: DFN3's dereverb variant, or a compact band-split RNN
  trained on reverberant speech. Same licensing caveats as above.

**Decision needed from you:** whether to invest in model training/licensing
now or keep this as the next-gen slot. No shipping-code change is warranted
until a model is chosen.

## Status
Both items are **scoped, awaiting product decision** — all engineering
prerequisites from the audit (A1 lock-free DFN path, off-thread patterns,
profile/allocation coverage) landed 2026-07-28.
