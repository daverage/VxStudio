# Stem Profile Report: The_Killers_-_Mr_Brightside_Official_Music_Video_stems

## Recording condition
- Mono score: `0.000`
- Compression score: `0.477`
- Tilt score: `1.000`
- Separation confidence: `0.633`

## Plots
- [Stem spectral profiles](stem_profiles.png)
- [Band dominance vs conflict](stem_conflict.png)

## Suggested safe regions
- `vocals`: 1.4 kHz - 2.0 kHz, 2.0 kHz - 2.9 kHz
- `drums`: 20 Hz - 29 Hz, 29 Hz - 43 Hz, 6.3 kHz - 9.3 kHz, 9.3 kHz - 13.6 kHz, 13.6 kHz - 20.0 kHz
- `bass`: 136 Hz - 200 Hz
- `guitar`: No strongly safe regions found
- `piano`: No strongly safe regions found
- `other`: No strongly safe regions found

## High-conflict regions
- `vocals`: 431 Hz - 632 Hz, 632 Hz - 928 Hz, 2.9 kHz - 4.3 kHz
- `drums`: 43 Hz - 63 Hz
- `bass`: 93 Hz - 136 Hz
- `guitar`: 294 Hz - 431 Hz
- `piano`: No dominant high-conflict regions flagged
- `other`: No dominant high-conflict regions flagged

## Dominant band summary
- `20 Hz - 29 Hz`: dominant `drums` share `0.93`, conflict `0.16`; top `drums:0.93, bass:0.06, guitar:0.01`
- `29 Hz - 43 Hz`: dominant `drums` share `0.79`, conflict `0.29`; top `drums:0.79, bass:0.21, guitar:0.00`
- `43 Hz - 63 Hz`: dominant `drums` share `0.62`, conflict `0.38`; top `drums:0.62, bass:0.38, guitar:0.00`
- `63 Hz - 93 Hz`: dominant `drums` share `0.71`, conflict `0.35`; top `drums:0.71, bass:0.29, guitar:0.00`
- `93 Hz - 136 Hz`: dominant `bass` share `0.61`, conflict `0.41`; top `bass:0.61, drums:0.37, guitar:0.01`
- `136 Hz - 200 Hz`: dominant `bass` share `0.81`, conflict `0.34`; top `bass:0.81, drums:0.14, guitar:0.05`
- `200 Hz - 294 Hz`: dominant `bass` share `0.40`, conflict `0.74`; top `bass:0.40, guitar:0.28, vocals:0.16`
- `294 Hz - 431 Hz`: dominant `guitar` share `0.44`, conflict `0.78`; top `guitar:0.44, bass:0.21, vocals:0.16`
- `431 Hz - 632 Hz`: dominant `vocals` share `0.43`, conflict `0.67`; top `vocals:0.43, guitar:0.39, drums:0.10`
- `632 Hz - 928 Hz`: dominant `vocals` share `0.60`, conflict `0.52`; top `vocals:0.60, guitar:0.34, bass:0.03`
- `928 Hz - 1.4 kHz`: dominant `vocals` share `0.65`, conflict `0.48`; top `vocals:0.65, guitar:0.30, bass:0.03`
- `1.4 kHz - 2.0 kHz`: dominant `vocals` share `0.82`, conflict `0.32`; top `vocals:0.82, guitar:0.16, drums:0.01`
- `2.0 kHz - 2.9 kHz`: dominant `vocals` share `0.71`, conflict `0.45`; top `vocals:0.71, guitar:0.24, drums:0.03`
- `2.9 kHz - 4.3 kHz`: dominant `vocals` share `0.47`, conflict `0.56`; top `vocals:0.47, drums:0.43, guitar:0.09`

## Rebalance notes
- Use the safe regions as heuristic candidates, not as hard ownership bands.
- Treat high-conflict bands as low-confidence areas where sliders should move less aggressively.
- Compare multiple recording conditions before turning any region into a product-level prior.