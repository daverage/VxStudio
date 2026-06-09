# VXRepair: Add Click DSP

## Scope
Add `DeClickDsp` as a 4th tool row in VXRepair (after Speech Clarity, before Reverb).
Click DSP already compiled into VXRepair via CMake. Analysis, parameters, processing,
latency accounting, and editor all need updating.

## Files

- [x] VxRepairAnalysis.h/cpp — add clickScore to RepairAssessment + crest-factor detection
- [x] VxRepairProcessor.h — add deClickDsp, clickIntensity, clickDryDelay, update ToolActivity
- [x] VxRepairProcessor.cpp — params, prepare, reset, process, applyAssessment, getToolActivity
- [x] VxRepairEditor.h — 4 rows, clickActivityDisplay
- [x] VxRepairEditor.cpp — Click row, taller editor, paintRepair, timerCallback

## Key decisions
- Click DSP always runs (0 strength when off) → reported latency = noiseLat + clickLat + reverbLat
- click_listen uses a clickDryDelay buffer (same pattern as noise dry delay)
- Click order in chain: click → plosive → esser → breath (matches VXSpeechClarity)
- Editor height: 580 → 660 to fit 4th row
