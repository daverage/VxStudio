# VXDeverb Audit

## A. Executive summary
- Overall verdict: Fail
- Top risks by severity:
- Critical: larger-than-prepared host blocks can leave the aligned dry buffer invalid while `applyBodyRestore()` still reads it sample-by-sample.
- High: RT60 is estimated from channel 0 only, then applied to both channels.
- Medium: instability recovery resets the whole processor and silently rerenders at lower strength.
- Medium: dead stage/plumbing obscures the actual processing contract.

## B. Findings table
| Severity | Category | File + function | Exact problem | Why it matters | Concrete fix |
| --- | --- | --- | --- | --- | --- |
| Critical | framework misuse / host correctness | `Source/vxsuite/framework/VxSuiteProcessorBase.cpp::processBlock`, `Source/vxsuite/framework/VxSuiteProcessCoordinator.h::beginBlock`, `Source/vxsuite/products/deverb/VxDeverbProcessor.cpp::processProduct`, `Source/vxsuite/products/deverb/VxDeverbProcessor.cpp::applyBodyRestore` | If a host delivers a block larger than the original `samplesPerBlock`, dry capture can be skipped and `applyBodyRestore()` still reads `dry[i]` across the larger wet block. | This is an audio-thread out-of-bounds/stale-buffer risk under valid host behavior, especially offline render and dynamic block-size changes. | Fix the framework to grow aligned-dry storage on demand or chunk oversize blocks; also hard-guard body restore when aligned dry is invalid. |
| High | signal consistency | `Source/vxsuite/products/deverb/dsp/VxDeverbSpectralProcessor.cpp::processInPlace` | RT60 is tracked from channel 0 only and reused for every channel. | Asymmetric stereo reverberation can produce left/right mismatch and wrong suppression on the non-reference channel. | Track RT60 per channel or document and implement a shared mono control signal explicitly. |
| Medium | audio safety | `Source/vxsuite/products/deverb/VxDeverbProcessor.cpp::processProduct` | The “stability” fallback resets the processor and rerenders at reduced strength whenever output exceeds an arbitrary peak ceiling or becomes non-finite. | A single hot event can wipe history, create discontinuities, and silently change the requested setting mid-stream. | Sanitize unstable bins/stages internally and reserve full reset for explicit catastrophic fallback. |
| Medium | framework misuse | `Source/vxsuite/products/deverb/VxDeverbProcessor.h`, `Source/vxsuite/products/deverb/VxDeverbProcessor.cpp::processProduct` | `StageChain<1>` and most `ProcessOptions` plumbing are dead or ignored. | The real product contract is harder to reason about and drifts from the suite architecture. | Remove dead stage/plumbing or route the DSP through the actual stage abstraction. |

## C. Framework-specific cleanup list
- Centralize oversize-block handling in `ProcessorBase` / `ProcessCoordinator`.
- Remove unused stage-chain scaffolding from the product unless Deverb is actually moved into that model.
- Tighten the framework contract around aligned dry validity so latency-bearing products cannot assume it blindly.

## D. Test matrix
- 44.1 / 48 / 96 kHz.
- Small and large block sizes, including blocks larger than the original prepare size.
- Mono and stereo, including asymmetric left/right reverb tails.
- Silence, speech, music, impulse, DC.
- Extreme automation on `Reduce` and `Blend`.
- Bypass/listen toggling with active latency.
- Sample-rate change during session.
- Reopen / state restore.

## E. Patch recommendations
- Quick wins:
- Fix dynamic aligned-dry allocation or chunking in the framework.
- Add a hard validity check before `applyBodyRestore()`.
- Add a stereo-asymmetry regression test.
- Surgical DSP fix:
- Split RT60 estimation per channel or explicitly analyze a documented mono reference.
- Redesign:
- Replace the reset-and-rerender stability fallback with bounded internal sanitizing and a deterministic catastrophic bypass path.
