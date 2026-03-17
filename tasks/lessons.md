# Lessons

- When the user asks for extraction instead of copying, prefer factoring shared logic into active VX Suite code and reusing it from the old path so legacy files visibly shrink in responsibility.
- Never leave a new VX Suite product linked to sibling-repo legacy code; import the needed implementation into this repo and improve it locally before wiring the product.
