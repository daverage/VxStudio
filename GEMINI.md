# Workflow Orchestration

## 1. Plan Node Default

- Enter plan mode for any non-trivial task (3+ steps or architectural decisions).
- If something goes sideways, stop and re-plan immediately. Do not keep pushing.
- Use plan mode for verification steps, not just building.
- Write detailed specs upfront to reduce ambiguity.

---

## 2. Subagent Strategy

- Use subagents liberally to keep the main context window clean.
- Offload research, exploration, and parallel analysis to subagents.
- For complex problems, increase compute via subagents.
- One task per subagent for focused execution.

---

## 3. Self-Improvement Loop

- After any correction from the user, update `tasks/lessons.md` with the pattern.
- Write rules that prevent the same mistake recurring.
- Ruthlessly iterate on lessons until mistake rate drops.
- Review lessons at session start for the relevant project.

---

## 4. Verification Before Done

- Never mark a task complete without proving it works.
- Diff behaviour between main and your changes when relevant.
- Ask yourself: Would a staff engineer approve this?
- Run tests, check logs, demonstrate correctness.

---

## 5. Demand Elegance (Balanced)

- For non-trivial changes, pause and ask: Is there a more elegant way?
- If a fix feels hacky: Knowing everything I know now, implement the elegant solution.
- Skip this for simple, obvious fixes. Do not over-engineer.
- Challenge your own work before presenting it.

---

## 6. Autonomous Bug Fixing

- When given a bug report, fix it. Do not ask for hand-holding.
- Point at logs, errors, failing tests, then resolve them.
- Require zero context switching from the user.
- Fix failing CI tests without being told how.

---

# Task Management

1. **Plan First**  
   Write a plan to `tasks/todo.md` with checkable items.

2. **Verify Plan**  
   Check in before starting implementation.

3. **Track Progress**  
   Mark items complete as you go.

4. **Explain Changes**  
   Provide a high-level summary at each step.

5. **Document Results**  
   Add a review section to `tasks/todo.md`.

6. **Capture Lessons**  
   Update `tasks/lessons.md` after corrections.

---

# Core Principles

- **Simplicity First**  
  Make every change as simple as possible. Impact minimal code.

- **No Laziness**  
  Find root causes. No temporary fixes. Senior developer standards.

- **Minimal Impact**  
  Changes should only touch what is necessary. Avoid introducing bugs.

---

# VX Suite Standards

- All new code for this project must follow the shared VX Suite framework and documentation.
- Treat [docs/VX_SUITE_FRAMEWORK.md](/Users/andrzejmarczewski/Documents/GitHub/VxCleaner/docs/VX_SUITE_FRAMEWORK.md), [docs/VX_SUITE_RESEARCH.md](/Users/andrzejmarczewski/Documents/GitHub/VxCleaner/docs/VX_SUITE_RESEARCH.md), and [docs/VX_SUITE_PRODUCT_TEMPLATE.md](/Users/andrzejmarczewski/Documents/GitHub/VxCleaner/docs/VX_SUITE_PRODUCT_TEMPLATE.md) as the source of truth for plugin architecture, UI rules, parameter discipline, and realtime constraints.
- Prefer adding or extending reusable code in `Source/vxsuite/framework/` instead of inventing one-off processor/editor patterns per plugin.
- New VX Suite products should default to a simple one- or two-knob UI, outcome-led wording, stable parameter IDs, resize/scaling support, and strict realtime-safe behavior.
- Do not add sprawling control surfaces, heavyweight visualisation, or product-specific framework forks unless the user explicitly approves a deviation.
