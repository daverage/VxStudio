# VxStudio DSP & Framework Review Plan

## Goal
Review all products and framework for best practices: coding standards, audio processing efficiency, memory safety, CPU efficiency, and DRY compliance.

## Standards Reference
- VX_SUITE_FRAMEWORK.md: product rules, realtime rules, template shape, UI rules, validation rules
- VxSuiteProcessorBase: realtime safety contract (no heap alloc in processBlock, no I/O/locks)
- Framework boundary: shared code in `framework/`, product-specific in `products/<product>/`

## Review Scope

### Framework (Source/vxsuite/framework/)
- [ ] Check for code duplication across helpers
- [ ] Verify realtime-safe patterns in all shared code
- [ ] Review memory allocations (all in prepare/reset, never in process)
- [ ] Check parameter smoothing implementation
- [ ] Review output trimmer logic
- [ ] Verify telemetry path doesn't block audio thread

### Products (Source/vxsuite/products/)
For each product (tone, subtract, deepfilternet, leveler, OptoComp, deverb, cleanup, finish, proximity, analyser, polish, denoiser, VxMixStudio):
- [ ] Verify ProcessorBase/EditorBase inheritance
- [ ] Check ProductIdentity descriptor completeness
- [ ] Realtime safety: no heap alloc in processBlock
- [ ] Mode policy compliance (use framework ModePolicy, not ad-hoc)
- [ ] Listen implementation (if used, verify latency-aware alignment)
- [ ] DSP state allocation strategy (per-channel arrays, not dynamic)
- [ ] Code duplication vs framework sharing
- [ ] CPU/memory efficiency

## Key Findings Tracked
- Critical realtime violations
- Code duplication opportunities
- Missing framework adoption
- Inefficient allocations or per-block operations
- Parameter handling issues
- Listen/latency misalignment

## Execution Order
1. Quick scan of all product headers/cpp for structure
2. Deep review of framework code (realtime safety, efficiency)
3. Review each product for framework compliance
4. Identify duplication patterns and consolidation opportunities
5. Summary report with prioritized fixes
