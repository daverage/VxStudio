# VXProximity Audit

## A. Executive summary
- Overall verdict: Pass with issues
- Top risks by severity:
- Medium: combined shelves can add significant gain with no safety or explicit headroom policy.
- Low: coefficient changes are block-stepped under automation.
- Low: the product bypasses the shared mode-policy helper and reads mode directly.

## B. Findings table
| Severity | Category | File + function | Exact problem | Why it matters | Concrete fix |
| --- | --- | --- | --- | --- | --- |
| Medium | audio safety | `Source/vxsuite/products/proximity/dsp/VxProximityDsp.cpp::processInPlace` | Proximity can stack roughly +8.5 dB low shelf with +4 dB high shelf and has no explicit output safety stage or documented headroom budget. | Real material can clip even if simple regression fixtures do not. | Reduce the gain law first; if needed, add an emergency guard after proving intentional headroom. |
| Low | realtime / automation stability | `Source/vxsuite/products/proximity/dsp/VxProximityDsp.cpp::processInPlace` | Parameter targets are smoothed per block, but the actual biquad coefficients still jump once per block. | Fast automation can still click or warp transients. | Interpolate coefficients or crossfade between old/new filters for large moves. |
| Low | framework misuse | `Source/vxsuite/products/proximity/VxProximityProcessor.cpp::processProduct` | The product reads raw mode instead of consistently using `currentModePolicy()`. | Shared mode semantics become easier to drift across products. | Route product behavior through the shared mode-policy helpers consistently. |

## C. Framework-specific cleanup list
- Use shared mode-policy access consistently.
- Decide whether additive EQ-style products need a shared headroom/output-protection policy.

## D. Test matrix
- 44.1 / 48 / 96 kHz.
- Small and large block sizes.
- Mono and stereo.
- Silence, speech, music, impulse.
- Extreme automation on `Closer` and `Air`.
- Bypass/listen toggling.
- Sample-rate change during session.
- Reopen / state restore.

## E. Patch recommendations
- Quick wins:
- Reduce the shelf gain law or explicitly add emergency headroom protection.
- Add an automation stress test.
- Cleanup:
- Stop bypassing `currentModePolicy()`.
