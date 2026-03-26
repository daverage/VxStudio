# Rebalance: htdemucs_6s integration + DSP guitar improvements — 2026-03-26

## Goal
Replace UMX4/Spleeter with htdemucs_6s (explicit guitar stem) and improve DSP heuristic
guitar separation across all three recording-quality modes.

## Plan

- [x] Write export script `assets/rebalance/export_demucs6_onnx.py`
      Contract: input [1,2,88200] waveform @ 44100 Hz → output [1,6,2,88200] stems
      Stems: 0=drums 1=bass 2=other 3=vocals 4=guitar 5=piano
- [ ] Run script to download model and produce `vx_rebalance_demucs6.onnx` + JSON
      → user needs to run: `pip install demucs torch torchaudio onnx onnxruntime`
        then `cd assets/rebalance && python export_demucs6_onnx.py`
        then place outputs in `assets/rebalance/models/demucs6/`
- [x] Create `VxRebalanceDemucsModel.h/.cpp` (waveform-in/out ONNX runner, same C-API pattern)
- [x] Extend `ModelRunner`: add `demucs6` ActiveModel + chunk accumulation path
- [x] Update `VxRebalanceProcessor.cpp`: makeDemucsPackage() + pass demucs file to runner
- [x] DSP guitar profiles: reverted to proven baseline (presence-zone boost regressed guitar corr
      from 0.677 to 0.294 — guitar improvement comes from Demucs, not heuristic DSP)
- [x] Add new sources to CMakeLists.txt
- [x] Build verified: VXRebalanceMeasure builds clean

## Notes
- demucs stem order: drums(0) bass(1) other(2) vocals(3) guitar(4) piano(5)
- VX lane mapping: vocals←3, drums←0, bass←1, guitar←4, other←(2+5 merged)
- chunk size 88200 = 2s @ 44100 Hz (good balance quality/mask refresh rate)
- keep existing UMX4 as fallback when demucs model file absent
