# VXDenoiser Audit

## A. Executive summary
- Overall verdict: Fail
- Top risks by severity:
- High: the plugin reports STFT latency even when the DSP returns immediate dry at `Clean=0`.
- Medium: only the mono mid is denoised while side content is passed through unchanged.
- Medium: no dedicated PDC-through-zero or side-noise regression coverage.

## B. Findings table
| Severity | Category | File + function | Exact problem | Why it matters | Concrete fix |
| --- | --- | --- | --- | --- | --- |
| High | host correctness | `Source/vxsuite/products/denoiser/VxDenoiserProcessor.cpp::prepareSuite`, `Source/vxsuite/products/denoiser/dsp/VxDenoiserDsp.cpp::processInPlace` | Reported latency stays fixed at 768 samples, but the DSP hard-bypasses to undelayed dry when `wet <= 0`. | The host still applies PDC, so `Clean=0` is not timing-transparent and automation through zero can jump timing. | Preserve the latency path and emit delayed dry when “off”, or make latency truly dynamic and transition it safely. |
| Medium | signal consistency | `Source/vxsuite/products/denoiser/dsp/VxDenoiserDsp.cpp::processInPlace` | The STFT runs on mono mid only and reconstructs stereo with untouched side. | Decorrelated side noise and off-center noise remain largely untreated, which does not match a general broadband denoiser claim. | Process channels independently in `General` mode, or narrow the documented contract to centered-voice cleanup only. |

## C. Framework-specific cleanup list
- None mandatory beyond the shared aligned-dry / latency contract, but the product should stop depending on “stage inactive means dry and zero latency” while still reporting fixed latency.

## D. Test matrix
- 44.1 / 48 / 96 kHz.
- Small and large block sizes.
- Mono and stereo, including side-only noise.
- Silence, speech, music, impulse, DC.
- Extreme automation through `Clean=0`.
- Bypass/listen toggling with latency active.
- Sample-rate change during session.
- Reopen / state restore.

## E. Patch recommendations
- Quick wins:
- Add a delayed-dry latency-preserving path when the denoiser is effectively off.
- Add a regression test for PDC correctness while automating `Clean` through zero.
- Medium change:
- Revisit stereo policy in `General` mode so side-channel noise is not silently excluded.
