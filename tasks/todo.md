# VxRepair Build Plan

## Goal
New VST3 plugin — single intelligent repair assistant. Analyses ~5s of audio, detects noise/reverb/hum-mud, suggests per-tool strength. Three tool rows (Cleanup, Denoiser, Deverb), each with one knob + listen + bypass.

## Files
- [x] `products/repair/VxRepairAnalysis.h/.cpp` — audio analysis layer
- [x] `products/repair/VxRepairProcessor.h/.cpp` — processor
- [x] `products/repair/VxRepairEditor.h/.cpp` — custom 3-state UI
- [x] `CMakeLists.txt` — VXRepair target
- [x] `cmake/VxStudioVersions.h.in` — version entry
- [x] Builds and stages to Source/vxstudio/vst/VXRepair.vst3

## States
1. **Idle** — prompt + Analyse button
2. **Collecting** — 5s progress arc + level display
3. **Repair** — 3 tool rows (Cleanup / Noise / Reverb) + DeepFilterNet note + Reset

## Tools
| Row | DSP | Score drives |
|-----|-----|-------------|
| Hum & Mud | `cleanup::Dsp` | `humMudScore` |
| Noise | `denoiser::DenoiserDsp` | `noiseScore` |
| Reverb | `deverb::SpectralProcessor` | `reverbScore` |

DeepFilterNet: not embedded — show tip in footer if standard noise detected.

## Accent
Orange `{1.0f, 0.52f, 0.08f}` — distinctive from suite cyan.
