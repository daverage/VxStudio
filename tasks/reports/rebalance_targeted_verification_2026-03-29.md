# ReBalance Targeted Verification - 2026-03-29

These checks were run after the latest 2026-03-29 render retunes that followed a weaker
intermediate full listening-protocol run. They reflect the current installed
`/Library/Audio/Plug-Ins/VST3/VXRebalance.vst3`.

## Commands

```bash
build/VXRebalanceMeasure tests/fixtures/rebalance/listening_cases/studio_vocal_guitar -24 studio dsp guitar
build/VXRebalanceMeasure tests/fixtures/rebalance/listening_cases/live_room_band -24 live dsp guitar
build/VXRebalanceMeasure tests/fixtures/rebalance/listening_cases/drum_heavy_loop -24 studio dsp drums
build/VXRebalanceMeasure tests/fixtures/rebalance/listening_cases/bass_heavy_mix -24 studio dsp bass
```

## Results

- Studio guitar `-24 dB`: `target_corr = -0.550117`, isolated guitar `-0.663807 dB`
- Live guitar `-24 dB`: `target_corr = -0.224782`, isolated guitar `-0.415958 dB`
- Phone guitar `-24 dB`: `target_corr = -0.470331`, isolated guitar `-2.25415 dB`
- Drums `-24 dB`: `target_corr = -0.285718`, isolated drums `-0.194953 dB`
- Bass `-24 dB`: `target_corr = -0.937841`, isolated bass `-11.2889 dB`

## Interpretation

- Smaller default shell and faster control response are in place.
- Bass cut remains strong.
- Studio guitar improved modestly again.
- Phone guitar improved modestly again after relaxing Phone conservatism.
- Live guitar is materially better than the regressed intermediate build and improved again in the final pass, but it still remains weaker than the intended product contract.
- Drums remain the weakest lane and the final pass did not materially solve them.
- Guitar overall is now more body-led than before, but still reads as moderate reduction rather than clean object removal.

## Structural cleanup kept in current build

- Removed the dead `applyClusterInfluence()` helper so the inline cluster-influence path is now the only source of truth
- Added `TransientEvent.linkedClusterTrackId` and kept transient-to-cluster linkage alive across the event lifetime
- Relaxed Phone mode slightly for guitar evidence:
  `confidenceFloor 0.32 -> 0.28`
  `harmonicTrust 0.62 -> 0.74`
  `stereoWidthTrust 0.25 -> 0.32`
