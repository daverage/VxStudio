# VXFinish Audit

## A. Executive summary
- Overall verdict: Pass with issues
- Top risks by severity:
- Medium: wrapper logic is largely duplicated with `VXOptoComp`, so correctness fixes will drift unless shared.
- Medium: output trimming sits after a dedicated limiter and can hide gain-staging mistakes.
- Low: local `blockBlendAlpha` duplicates framework smoothing logic.
- Low: no dedicated test proving the trimmer stays mostly idle under intended operating ranges.

## B. Findings table
| Severity | Category | File + function | Exact problem | Why it matters | Concrete fix |
| --- | --- | --- | --- | --- | --- |
| Medium | framework misuse | `Source/vxsuite/products/finish/VxFinishProcessor.cpp::processProduct`, `renderListenOutput` | Parameter smoothing, gain mapping, additive listen, and wrapper logic are substantially duplicated in `VXOptoComp`. | Every future correctness fix has to be done twice. | Extract a shared additive-finish wrapper/helper. |
| Medium | audio safety / framework misuse | `Source/vxsuite/products/finish/VxFinishProcessor.cpp::processProduct` | `OutputTrimmer` is always run after the internal limiter. | The second safety stage can conceal whether the actual product DSP is correctly gain staged. | Keep it as a rare emergency guard and add tests/telemetry that it does not engage materially in normal ranges. |
| Low | framework cleanup | `Source/vxsuite/products/finish/dsp/VxFinishDsp.cpp` | The DSP redefines `blockBlendAlpha` locally instead of using the framework helper. | Duplicated smoothing code invites drift. | Use `vxsuite::blockBlendAlpha` directly. |

## C. Framework-specific cleanup list
- Share the additive finish/opto wrapper behavior with `VXOptoComp`.
- Consolidate common smoothing and bipolar gain mapping into shared utilities.
- Treat output trimming as a framework-level emergency guard, not a hidden product compensator.

## D. Test matrix
- 44.1 / 48 / 96 kHz.
- Small and large block sizes.
- Mono and stereo.
- Silence, speech, music, impulse.
- Extreme automation on `Finish`, `Body`, and `Gain`.
- Bypass/listen toggling.
- Sample-rate change during session.
- Reopen / state restore.
- Limiter/trimmer telemetry under strong settings.

## E. Patch recommendations
- Quick wins:
- Replace the local smoothing helper with the framework function.
- Add a regression that fails if the trimmer is carrying normal operating headroom.
- Cleanup:
- Merge duplicated wrapper behavior with `VXOptoComp`.
