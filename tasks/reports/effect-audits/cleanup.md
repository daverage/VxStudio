# VXCleanup Audit

## A. Executive summary
- Overall verdict: Pass with issues
- Top risks by severity:
- Low: classifier behavior drifts across sample rates because the product-local spectral window is fixed at 1024 samples.
- Low: output protection is still product-local trimming after the corrective path, which can hide gain-staging mistakes instead of proving they are absent.
- Low: the product-local DSP wrapper is mostly plumbing around `polish::CorrectiveStage`, which is framework drift rather than a real product boundary.
- Low: no focused coverage for 44.1/96 kHz classifier behavior, impulse/DC input, or extreme automation.

## B. Findings table
| Severity | Category | File + function | Exact problem | Why it matters | Concrete fix |
| --- | --- | --- | --- | --- | --- |
| Low | sample rate | `Source/vxsuite/products/cleanup/VxCleanupProcessor.cpp::prepareSuite` | `chooseSpectralOrder()` always returns order 10, so the classifier time window changes with sample rate. | Breath/plosive/sibilance heuristics will not mean the same thing at 44.1, 48, and 96 kHz. | Pick FFT size from a target window in ms, or retune thresholds per sample rate. |
| Low | framework misuse | `Source/vxsuite/products/cleanup/dsp/VxCleanupDsp.cpp::processCorrective` | `vxsuite::cleanup::Dsp` is a thin pass-through wrapper over `polish::CorrectiveStage`. | The product boundary is artificial, which makes future bug fixes and audits harder. | Collapse the wrapper or move truly product-specific DSP into it. |
| Low | framework misuse / audio safety | `Source/vxsuite/products/cleanup/VxCleanupProcessor.cpp::processProduct` | Cleanup still relies on a product-local `OutputTrimmer` after its corrective chain. | Safety trimming can mask upstream gain mistakes instead of proving the stage itself is well behaved. | Keep the trimmer as an emergency guard only and add tests that normal operating ranges do not materially trigger it. |

## C. Framework-specific cleanup list
- Remove or consolidate the thin `cleanup::Dsp` wrapper.
- Decide whether output safety belongs in a shared corrective-stage utility instead of product-local code.
- Keep classifier/analysis ownership honest: product heuristics can stay local, but shared safety helpers should live in `Source/vxsuite/framework/`.

## D. Test matrix
- 44.1 / 48 / 96 kHz: verify identical cleanup decisions on breathy speech, plosives, and harsh sibilants.
- Small and large block sizes: 32, 64, 256, 1024, and a larger-than-prepared offline block.
- Mono and stereo: confirm no stale-channel data or unintended collapse.
- Silence, speech, music, impulse, DC: confirm no false classifier spikes or non-finite output.
- Extreme automation: rapid `Cleanup`, `Body`, and `Focus` sweeps.
- Bypass/listen toggling: confirm removed-content audition still recombines sensibly.
- Sample-rate change during session.
- Reopen / state restore.

## E. Patch recommendations
- Quick wins:
- Make the spectral analysis window sample-rate aware.
- Add regression coverage for 44.1/96 kHz, impulse, DC, and aggressive automation.
- Cleanup:
- Remove the wrapper layer if it continues to add no product-local behavior.
- Longer-term redesign:
- If corrective products keep sharing the same output-protection pattern, move that safety contract into shared framework utilities instead of repeating it per product.
