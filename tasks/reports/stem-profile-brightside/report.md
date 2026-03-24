# Stem Profile Report: brightside_stems

## Recording condition
- Mono score: `0.000`
- Compression score: `0.306`
- Tilt score: `1.000`
- Separation confidence: `0.693`

## Plots
- [Stem spectral profiles](stem_profiles.png)
- [Band dominance vs conflict](stem_conflict.png)

## Suggested safe regions
- `vocals`: 431 Hz - 632 Hz, 632 Hz - 928 Hz, 928 Hz - 1.4 kHz, 1.4 kHz - 2.0 kHz, 2.0 kHz - 2.9 kHz, 2.9 kHz - 4.3 kHz
- `drums`: 6.3 kHz - 9.3 kHz, 9.3 kHz - 13.6 kHz, 13.6 kHz - 20.0 kHz
- `bass`: 29 Hz - 43 Hz, 43 Hz - 63 Hz, 63 Hz - 93 Hz, 93 Hz - 136 Hz
- `guitar`: No strongly safe regions found
- `piano`: No strongly safe regions found
- `other`: No strongly safe regions found

## High-conflict regions
- `vocals`: No dominant high-conflict regions flagged
- `drums`: No dominant high-conflict regions flagged
- `bass`: 20 Hz - 29 Hz, 136 Hz - 200 Hz
- `guitar`: No dominant high-conflict regions flagged
- `piano`: No dominant high-conflict regions flagged
- `other`: No dominant high-conflict regions flagged

## Dominant band summary
- `20 Hz - 29 Hz`: dominant `bass` share `0.65`, conflict `0.42`; top `bass:0.65, drums:0.33, guitar:0.02`
- `29 Hz - 43 Hz`: dominant `bass` share `0.94`, conflict `0.13`; top `bass:0.94, drums:0.06, guitar:0.00`
- `43 Hz - 63 Hz`: dominant `bass` share `0.97`, conflict `0.08`; top `bass:0.97, drums:0.03, guitar:0.00`
- `63 Hz - 93 Hz`: dominant `bass` share `0.90`, conflict `0.18`; top `bass:0.90, drums:0.10, guitar:0.00`
- `93 Hz - 136 Hz`: dominant `bass` share `0.91`, conflict `0.19`; top `bass:0.91, guitar:0.06, drums:0.02`
- `136 Hz - 200 Hz`: dominant `bass` share `0.65`, conflict `0.42`; top `bass:0.65, guitar:0.32, drums:0.03`
- `200 Hz - 294 Hz`: dominant `vocals` share `0.45`, conflict `0.63`; top `vocals:0.45, bass:0.30, guitar:0.22`
- `294 Hz - 431 Hz`: dominant `guitar` share `0.55`, conflict `0.56`; top `guitar:0.55, vocals:0.29, bass:0.15`
- `431 Hz - 632 Hz`: dominant `vocals` share `0.86`, conflict `0.25`; top `vocals:0.86, guitar:0.13, drums:0.01`
- `632 Hz - 928 Hz`: dominant `vocals` share `0.95`, conflict `0.12`; top `vocals:0.95, guitar:0.05, other:0.00`
- `928 Hz - 1.4 kHz`: dominant `vocals` share `0.91`, conflict `0.18`; top `vocals:0.91, guitar:0.09, other:0.00`
- `1.4 kHz - 2.0 kHz`: dominant `vocals` share `0.92`, conflict `0.16`; top `vocals:0.92, guitar:0.07, drums:0.00`
- `2.0 kHz - 2.9 kHz`: dominant `vocals` share `0.81`, conflict `0.29`; top `vocals:0.81, guitar:0.19, drums:0.00`
- `2.9 kHz - 4.3 kHz`: dominant `vocals` share `0.98`, conflict `0.07`; top `vocals:0.98, guitar:0.02, drums:0.00`

## Rebalance notes
- Use the safe regions as heuristic candidates, not as hard ownership bands.
- Treat high-conflict bands as low-confidence areas where sliders should move less aggressively.
- Phone-style captures typically need stronger confidence gating and less low-mid semantic certainty.