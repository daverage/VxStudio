# REAPER Preset Pack

This folder contains the REAPER-facing VX Suite preset pack:

- [`RPL Files/`](./RPL%20Files) has one `.RPL` library per effect.
- [`FX Chains/`](./FX%20Chains) has full `.RfxChain` starting chains for the shared scenarios.

The scenario names are identical across all effect libraries:

- `Camera Review - Far Phone`
- `Live Music - Front Of Room`
- `Podcast Finishing - Clean Voice`
- `Mixed Audio - Voice + Guitar`

That lets a REAPER user choose the same scenario title in each VX effect when building a chain by hand, or start from the matching `.RfxChain` file and refine from there.

Scenario intent:

- `Camera Review - Far Phone`: full voice-repair chain for slightly noisy phone capture recorded a few meters away.
- `Live Music - Front Of Room`: gentle tone and dynamics shaping for whole-mix captures where voice-only denoise is the wrong tool.
- `Podcast Finishing - Clean Voice`: close-mic spoken-word polish and level control.
- `Mixed Audio - Voice + Guitar`: conservative cleanup for one-track voice-plus-instrument material.

Generation:

- [`tools/reaper/generate_vx_reaper_presets.lua`](../../tools/reaper/generate_vx_reaper_presets.lua) regenerates both the `.RPL` libraries and the `.RfxChain` files from the current staged VX Suite VST3s inside REAPER.
