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

