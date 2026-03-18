# VXOptoComp Audit

## A. Executive summary
- Overall verdict: Pass with issues
- Top risks by severity:
- Medium: wrapper behavior is duplicated with `VXFinish`.
- Medium: output trimming can hide limiter/compressor gain-staging mistakes.
- Low: dedicated product-level regression coverage is missing.

## B. Findings table
| Severity | Category | File + function | Exact problem | Why it matters | Concrete fix |
| --- | --- | --- | --- | --- | --- |
| Medium | framework misuse | `Source/vxsuite/products/OptoComp/VxOptoCompProcessor.cpp::processProduct`, `renderListenOutput` | The wrapper duplicates `VXFinish` smoothing, gain mapping, additive listen, and activity-light behavior. | Correctness and UX changes will drift between products that are supposed to share the same DSP contract. | Extract a shared wrapper/helper for finish-style additive products. |
| Medium | audio safety / framework misuse | `Source/vxsuite/products/OptoComp/VxOptoCompProcessor.cpp::processProduct` | The product always runs `OutputTrimmer` after its limiter stage. | The trimmer can mask whether the compressor/limiter path is actually safe on its own. | Add telemetry/tests to prove the trimmer is only an emergency guard. |
| Low | test coverage | `tests/` | There is no dedicated `VXOptoComp` regression target; it inherits coverage indirectly from `VXFinish`. | Wrapper-level bugs can ship even if shared DSP still works. | Add a small dedicated regression pass for identity, strong settings, listen semantics, and reset determinism. |

## C. Framework-specific cleanup list
- Share wrapper logic with `VXFinish`.
- Use common additive listen handling instead of product-local duplicated loops.
- Keep output trimming as a verifiable emergency guard only.

## D. Test matrix
- 44.1 / 48 / 96 kHz.
- Small and large block sizes.
- Mono and stereo.
- Silence, speech, music, impulse.
- Extreme automation.
- Bypass/listen toggling.
- Sample-rate change during session.
- Reopen / state restore.

## E. Patch recommendations
- Quick wins:
- Add dedicated `VXOptoComp` regression tests.
- Share wrapper behavior with `VXFinish`.
- Add a “trimmer should stay mostly idle” regression.
