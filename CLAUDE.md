# Core Rules
- Simplicity first. Touch minimal code. Find root causes, not workarounds.
- Never mark done without proving it works (tests, logs, diff).
- For non-trivial changes: pause and ask if there's a more elegant solution.
- Bug reports: fix autonomously. No hand-holding needed.
- Try not to think for more than 8000 tokens.

# Context Management
- Use /clear between unrelated tasks. Do not carry stale context.
- Read only the specific files needed. Never explore broadly unless asked.

# Planning
- Non-trivial tasks (3+ steps): write a brief plan to `tasks/todo.md` first, then check in.
- If something goes sideways, stop and re-plan. Don't push through.
- Simple/obvious fixes: skip planning overhead entirely.

# Subagents
- Use only when genuine parallelism is needed, not by default.
- One focused task per subagent. Keep spawn prompts minimal.
- Clean up subagents when done — idle ones still consume tokens.

# Lessons
- After any user correction, append one concise rule to `tasks/lessons.md`.
- Keep lessons under 30 entries. Prune redundant ones.
- Load lessons only when starting work on a relevant project, not by default.

# VX Suite Standards
- All new code in this repo must follow the VX Suite framework guidance.
- Use [docs/VX_SUITE_FRAMEWORK.md](/Users/andrzejmarczewski/Documents/GitHub/VxCleaner/docs/VX_SUITE_FRAMEWORK.md), [docs/VX_SUITE_RESEARCH.md](/Users/andrzejmarczewski/Documents/GitHub/VxCleaner/docs/VX_SUITE_RESEARCH.md), and [docs/VX_SUITE_PRODUCT_TEMPLATE.md](/Users/andrzejmarczewski/Documents/GitHub/VxCleaner/docs/VX_SUITE_PRODUCT_TEMPLATE.md) as the standing reference for product shape, UI hierarchy, scaling/resizing, processor/editor separation, and realtime-safe implementation.
- Prefer shared code in `Source/vxsuite/framework/` over one-off product scaffolding.
- New suite plugins should aim for simple outcome-led UX, stable parameter/state contracts, and performance-first implementation by default.
