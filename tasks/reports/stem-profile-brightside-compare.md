# Stem Profile Comparison: Phone vs Pro Mr Brightside

## Inputs
- Phone-style capture split: [stem-profile-brightside/report.md](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/tasks/reports/stem-profile-brightside/report.md)
- Pro release split: [stem-profile-brightside-pro/report.md](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/tasks/reports/stem-profile-brightside-pro/report.md)

## Recording condition summary
- Phone split:
  - mono `0.000`
  - compression `0.306`
  - tilt `1.000`
  - confidence `0.693`
- Pro split:
  - mono `0.000`
  - compression `0.477`
  - tilt `1.000`
  - confidence `0.633`

## Stable safe regions across both datasets
- `Vocals`: `1.4 kHz - 2.0 kHz`, `2.0 kHz - 2.9 kHz`
- `Drums`: `6.3 kHz - 9.3 kHz`, `9.3 kHz - 13.6 kHz`, `13.6 kHz - 20.0 kHz`

## Dataset-specific regions
- Phone-only:
  - `Bass`: `29 Hz - 43 Hz`, `43 Hz - 63 Hz`, `63 Hz - 93 Hz`, `93 Hz - 136 Hz`
  - `Vocals`: `431 Hz - 632 Hz`, `632 Hz - 928 Hz`, `928 Hz - 1.4 kHz`, `2.9 kHz - 4.3 kHz`
- Pro-only:
  - `Drums`: `20 Hz - 29 Hz`, `29 Hz - 43 Hz`
  - `Bass`: `136 Hz - 200 Hz`

## Product implications
- The only robust static `Vocals` prior worth trusting across both conditions is the presence band around `1.4-2.9 kHz`.
- The only robust static `Drums` prior worth trusting across both conditions is the high cymbal/hat region above `6.3 kHz`.
- Low-end ownership changes materially by recording condition, so `Bass` and kick-related `Drums` should stay confidence-gated and transient-aware rather than hard-banded.
- `Guitar`, `Piano`, and `Other` still do not produce trustworthy static safe regions across the two datasets; they should stay residual or require stronger evidence than simple frequency occupancy.
- `200 Hz - 430 Hz` remains the most dangerous area for heuristic semantic control and should move conservatively.
