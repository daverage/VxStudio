# Lessons

- When the user asks for extraction instead of copying, prefer factoring shared logic into active VX Suite code and reusing it from the old path so legacy files visibly shrink in responsibility.
- Never leave a new VX Suite product linked to sibling-repo legacy code; import the needed implementation into this repo and improve it locally before wiring the product.
- For profile-based cleanup tools, match the product mental model literally: `Learn` should be an explicit non-destructive capture/commit flow, not a hidden auto-stop heuristic layered on top of a profile-removal UI.
- When a protection control causes audible combing or robotic tone, do not solve it with heavier dry reblending by default; prefer backing off the processor itself, especially on older ML models like `DFN2`.
- When splitting a broad voice tool into separate products, draw the boundary by job type: corrective removal in one plugin, recovery/dynamics/level in the other, and make every gain-adding path explicitly noise-aware.
- De-breath is not a generic HF smoother; gate it away from strong sibilant/transient moments so it follows real breathy exhales rather than bright consonants.
- When multiple active products own the same low-level primitive like `juce::dsp::FFT`, centralize that ownership in `Source/vxsuite/framework/` before adding more product-local analysis code on top.
