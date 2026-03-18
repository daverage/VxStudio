# VXDeepFilterNet Audit

## A. Executive summary
- Overall verdict: Fail
- Top 10 risks by severity:
- Critical: timer-thread runtime rebuild races the audio thread and can free active ML runtime state during processing.
- High: `resetSuite()` destroys and recreates runtimes/resamplers instead of doing a pure state reset.
- High: low `Clean` values act like wet-signal attenuation rather than a light denoise because dry is never properly recombined.
- High: model changes are serviced on a 4 Hz timer, causing transient dry fallback and stale latency/reporting.
- Medium: stereo channels run as independent mono ML pipelines with no linked mask policy.
- Low: attenuation limits are pushed to the runtime every block even when unchanged.
- Low: dead mono-to-stereo copy code remains in the realtime path.

## B. Findings table
| Severity | Category | File + function | Exact problem | Why it matters | Concrete fix |
| --- | --- | --- | --- | --- | --- |
| Critical | realtime / host correctness | `Source/vxsuite/products/deepfilternet/VxDeepFilterNetProcessor.cpp::timerCallback`, `Source/vxsuite/products/deepfilternet/dsp/VxDeepFilterNetService.cpp::prepareRealtime`, `releaseRuntime`, `processRealtime` | The timer thread tears down and rebuilds runtime state while the audio thread can still read the same pointers and buffers. | This is a release-blocking race and potential use-after-free during playback, automation, or state restore. | Use an immutable prepared runtime bundle and atomically swap it at a safe block boundary; never free active runtime memory from another thread. |
| High | realtime | `Source/vxsuite/products/deepfilternet/VxDeepFilterNetProcessor.cpp::resetSuite`, `Source/vxsuite/products/deepfilternet/dsp/VxDeepFilterNetService.cpp::resetRealtime` | Reset destroys and recreates runtimes/resamplers instead of only clearing streaming state. | Host resets and transport changes can trigger heavyweight allocation/init near realtime. | Split “clear state” from “recreate backend”; keep reset realtime-safe. |
| High | signal consistency / host correctness | `Source/vxsuite/products/deepfilternet/dsp/VxDeepFilterNetService.cpp::processRealtime`, `Source/vxsuite/products/deepfilternet/VxDeepFilterNetProcessor.cpp::processProduct` | Processed output is multiplied by `wet` without a proper dry recombination path; `effectiveClean` also bottoms above zero. | Low `Clean` values attenuate the whole signal instead of behaving like mild denoise, and `Clean=0` is not a real near-bypass. | Separate model aggressiveness from wet mix and explicitly blend processed output with aligned dry. |
| High | host correctness / automation | `Source/vxsuite/products/deepfilternet/VxDeepFilterNetProcessor.cpp::prepareEngineIfNeeded`, `timerCallback`, `processProduct` | Model switching is deferred to a 4 Hz timer, so the product can pass dry with stale latency for up to 250 ms. | Automated or restored model changes can glitch behavior and PDC. | Make model changes explicit non-realtime reconfiguration events with immediate safe handoff. |
| Medium | signal consistency | `Source/vxsuite/products/deepfilternet/dsp/VxDeepFilterNetService.cpp::processRealtime` | Stereo channels are processed by independent mono runtimes with no shared mask/link policy. | Left/right denoise decisions can diverge and destabilize stereo image. | Add a linked stereo policy or document an intentional mono/voice-only contract. |
| Low | CPU | `Source/vxsuite/products/deepfilternet/dsp/VxDeepFilterNetService.cpp::processRealtime` | Runtime attenuation is set every block even when unchanged. | Avoidable hot-path overhead into third-party runtime code. | Cache and update only when the attenuation limit changes. |
| Low | cleanup | `Source/vxsuite/products/deepfilternet/dsp/VxDeepFilterNetService.cpp::processRealtime` | The `numChannels == 1` stereo copy path is effectively dead in the current bus model. | Stale code obscures the channel-handling contract. | Remove it or replace it with an explicit documented fallback. |

## C. Framework-specific cleanup list
- DeepFilterNet needs a framework-sanctioned runtime swap / reconfiguration primitive instead of a product-local timer hack.
- The framework should offer a safer pattern for latency-bearing ML products that need aligned dry and asynchronous preparation.
- Additive or partial-wet ML products need an explicit framework helper for aligned dry blending and listen semantics.

## D. Test matrix
- 44.1 / 48 / 96 kHz, including resampled engine behavior.
- Small and large block sizes.
- Mono and stereo, including decorrelated stereo ambience.
- Silence, speech, music, impulse.
- Extreme automation on `Clean`, `Guard`, and model selection.
- Bypass/listen toggling.
- Sample-rate change during session.
- Reopen / state restore.
- Offline render.

## E. Patch recommendations
- Minimal fixes first:
- Replace timer-thread mutation with atomic runtime handoff.
- Make `resetRealtime()` a pure state reset.
- Separate model strength from wet mix and restore a true low-strength bypass endpoint.
- Add dedicated regression coverage for model switching, latency, listen, and state restore.
- Redesign:
- If ML runtimes remain in VX Suite, move backend lifetime/orchestration into shared framework infrastructure instead of product-local code.
