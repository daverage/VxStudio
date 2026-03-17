# Lessons

- When the user asks for extraction instead of copying, prefer factoring shared logic into active VX Suite code and reusing it from the old path so legacy files visibly shrink in responsibility.
- Never leave a new VX Suite product linked to sibling-repo legacy code; import the needed implementation into this repo and improve it locally before wiring the product.
- For profile-based cleanup tools, match the product mental model literally: `Learn` should be an explicit non-destructive capture/commit flow, not a hidden auto-stop heuristic layered on top of a profile-removal UI.
