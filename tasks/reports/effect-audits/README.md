# VX Suite Effect Audit Reports

Audit date: 2026-03-18

Audited effects:
- `VXCleanup`
- `VXDeverb`
- `VXDenoiser`
- `VXDeepFilterNet`
- `VXFinish`
- `VXOptoComp`
- `VXProximity`
- `VXSubtract`
- `VXTone`

Excluded from this sweep:
- `VXSpectrum`

Reason:
- `VXSpectrum` is an analysis/telemetry product, not an audio effect with the wet/dry, gain staging, latency, and host-behavior contract requested in the prompt. Its realtime telemetry path still surfaced framework risks during the shared-framework review, but it does not belong in the per-effect release-readiness set.

Reports:
- `cleanup.md`
- `deverb.md`
- `denoiser.md`
- `deepfilternet.md`
- `finish.md`
- `optocomp.md`
- `proximity.md`
- `subtract.md`
- `tone.md`
