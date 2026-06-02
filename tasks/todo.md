# VxRepair + VxClarity Fixes — DONE

- [x] 1. VxRepair: Fix clarity analysis (sibilance-band FFT score, not hum/mud)
- [x] 2. VxRepair: Adapt denoiser ProcessOptions to strength
- [x] 3. VxRepair: Adapt deverb ProcessOptions to strength
- [x] 4. VxRepair: Remove dead deverb opts (voiceProtect + labRawMode)
- [x] 5. VxRepair: Raise scoreToStrength cap 0.85 → 0.95
- [x] 6. VxRepair: DeepFilter toggle moved lower (60→76px, 20px gap from confidence label)
- [x] 7. VxRepair + VxClarity: Fix sibilance detection threshold 0.25 → 0.08
- [x] 8. VxClarity: Remove broken performPreAnalysis; replace breathThreshold with running smoothedPeak
- [x] 9. VxClarity: Wire HPF (80 Hz, 2nd-order Butterworth) and hi-shelf (-2.5 dB @ 7 kHz) to hpf_on / hishelf_on params
