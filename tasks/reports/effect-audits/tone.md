# VXTone Audit

## A. Executive summary
- Overall verdict: Pass with issues
- Top risks by severity:
- Medium: combined shelves can exceed headroom with no safety policy.
- Medium: listen semantics are wrong for an additive EQ-style product.
- Low: coefficients jump once per block under automation.

## B. Findings table
| Severity | Category | File + function | Exact problem | Why it matters | Concrete fix |
| --- | --- | --- | --- | --- | --- |
| Medium | audio safety | `Source/vxsuite/products/tone/VxToneProcessor.cpp::processProduct` | Tone allows up to roughly +/-8 dB shelves per band with no explicit headroom or output-protection policy. | Combined bass/treble boosts can clip normal material. | Reduce the gain law first; if needed, add a guard stage after proving the intended headroom. |
| Medium | framework misuse / host correctness | `Source/vxsuite/products/tone/VxToneProcessor.cpp::makeIdentity`, `Source/vxsuite/framework/VxSuiteProcessorBase.cpp::renderListenOutput` | Tone opts into the shared listen toggle but does not override additive listen semantics. | Users hear polarity-inverted removed-delta behavior instead of the added EQ effect. | Remove `listenParamId` or implement additive listen explicitly. |
| Low | realtime / automation stability | `Source/vxsuite/products/tone/VxToneProcessor.cpp::processProduct` | Parameter targets are smoothed per block, but coefficients still hard-swap once per block. | Fast automation can still click or smear transients. | Interpolate coefficients or crossfade between old/new filter states for large changes. |

## C. Framework-specific cleanup list
- Revisit which products should expose the shared listen toggle at all.
- Define a shared policy for additive EQ-style products if listen remains supported.

## D. Test matrix
- 44.1 / 48 / 96 kHz.
- Small and large block sizes.
- Mono and stereo.
- Silence, speech, music, impulse.
- Extreme automation on `Bass` and `Treble`.
- Bypass/listen toggling.
- Sample-rate change during session.
- Reopen / state restore.

## E. Patch recommendations
- Quick wins:
- Remove or fix listen semantics.
- Reduce shelf gain or add a verified safety guard.
- Add an automation-click regression.
