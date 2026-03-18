# VXSubtract Audit

## A. Executive summary
- Overall verdict: Fail
- Top risks by severity:
- High: reported latency stays active even when the DSP returns immediate dry at `Subtract=0`.
- High: learned profiles are restored without sample-rate or FFT metadata.
- Medium: learn mode can become visually “on” while capture is not actually re-armed after reset/state transitions.
- Low: dead multichannel delay plumbing remains despite mono/stereo-only buses.

## B. Findings table
| Severity | Category | File + function | Exact problem | Why it matters | Concrete fix |
| --- | --- | --- | --- | --- | --- |
| High | host correctness | `Source/vxsuite/products/subtract/VxSubtractProcessor.cpp::prepareSuite`, `Source/vxsuite/products/subtract/dsp/VxSubtractDsp.cpp::processInPlace` | Reported STFT latency remains fixed even when the stage early-outs to undelayed dry. | `Subtract=0` is not timing-transparent and automation through zero can shift the track against the session. | Preserve delayed-dry output when inactive, or make latency reporting dynamic and safe. |
| High | sample rate / host correctness | `Source/vxsuite/products/subtract/VxSubtractProcessor.cpp::getStateInformation`, `setStateInformation`, `Source/vxsuite/products/subtract/dsp/VxSubtractDsp.cpp::restoreLearnedProfile` | Learned profiles are serialized as raw per-bin powers only and restored if the bin count matches. | A profile captured at one sample rate can be wrongly reused at another with different bin-to-frequency meaning. | Serialize sample rate, FFT size, and hop; reject or remap mismatches. |
| Medium | host correctness | `Source/vxsuite/products/subtract/VxSubtractProcessor.cpp::resetSuite`, `processProduct` | Reset clears active learning but latches the current `Learn` parameter, so a stuck-on/restored-on button may never retrigger capture. | Transport resets and state restore can leave the UI implying learning while DSP capture is actually idle. | Force `Learn` false on reset or re-arm learning when the parameter is already true. |
| Low | framework cleanup | `Source/vxsuite/products/subtract/dsp/VxSubtractDsp.cpp` | `extraChannelDelays` plumbing survives even though the framework only allows mono/stereo. | Dead multichannel logic adds noise to the processing contract. | Remove the dead path or widen the bus policy intentionally. |

## C. Framework-specific cleanup list
- Add a shared helper for “APVTS state + product blob” persistence so profile-bearing products do not hand-roll state logic.
- Make the framework’s latency contract explicit for stage-off paths.
- Remove dead multichannel delay code if the suite stays mono/stereo only.

## D. Test matrix
- 44.1 / 48 / 96 kHz, including learned-profile restore across sample rates.
- Small and large block sizes.
- Mono and stereo, including side-only noise.
- Silence, speech, music, impulse.
- Extreme automation through `Subtract=0`.
- Learn toggling, reset, transport stop/start, and state restore with `Learn` already on.
- Bypass/listen toggling.
- Sample-rate change during session.

## E. Patch recommendations
- Quick wins:
- Preserve latency on the stage-off path.
- Store format metadata alongside learned profiles.
- Add reset/state tests for learn behavior.
- Cleanup:
- Remove dead multichannel delay plumbing.
