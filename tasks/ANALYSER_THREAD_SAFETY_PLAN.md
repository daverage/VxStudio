# VXStudioAnalyser Thread Safety — Implementation Plan

**Status**: CRITICAL (data race protection needed before production)  
**Severity**: 🔴 Can cause segmentation faults, silent data corruption  
**Effort**: 8-12 hours (requires careful refactoring)

---

## The Problem

**Current architecture** uses two registries for analyser telemetry:
- `DomainRegistry` — tracks Analyser instances per DAW project
- `StageRegistry` — tracks all stage telemetry slots

**Audio thread** writes to these registries via `StagePublisher::publish()`  
**UI thread** reads from these registries via `VXStudioAnalyserEditor` (timer callbacks at 24-96 Hz)

**No mutex protection between threads** → undefined behavior:
- UI reads while audio writes → partial struct copies → garbage data
- Spectrum data is a 256-float array → high likelihood of mid-copy reads
- Crashes possible on aggressive add/remove plugin chain operations

---

## Current Synchronization Approach (Insufficient)

### DomainRegistry::latestDomainForProcess() — READS
```cpp
bool latestDomainForProcess(const std::uint64_t hostProcessId, DomainView& out) const noexcept {
    auto* state = domainState();  // Load shared memory pointer
    // NO LOCKING HERE ❌
    for (int slotIndex = 0; slotIndex < state->slots.size(); ++slotIndex) {
        auto& slot = state->slots[slotIndex];  // Raw read while audio thread may write
        if (slot.creationTimeMs > out.creationTimeMs) {
            out.creationTimeMs = slot.creationTimeMs;  // Atomic for this field?
            // But other fields not atomic!
        }
    }
}
```

**Problem**: Fields are read individually without guaranteeing consistency.

### StageRegistry::readStage() — READS
```cpp
bool readStage(const int slotIndex, StageView& out) const noexcept {
    auto& localState = localStageRegistryState();
    {
        const juce::ScopedLock localScoped(localState.lock);  // ✅ Local registry locked
        // ... read from local state OK
    }
    
    // Shared state read — uses version number polling ⚠️
    for (int attempt = 0; attempt < 4; ++attempt) {
        const auto versionStart = slot->version.load(std::memory_order_acquire);
        if ((versionStart & 1u) != 0u)  // Write in progress? Retry
            continue;
        
        out.telemetry = slot->telemetry;  // 😱 Copy while audio thread updates
        
        const auto versionEnd = slot->version.load(std::memory_order_acquire);
        if (versionStart == versionEnd)
            return true;  // Assume read was atomic
    }
}
```

**Problem**: Version number approach is fragile:
- If version changes between checks, retry (good)
- But if version is even, assumes read is safe without synchronization
- `telemetry` struct may be mid-update
- Only retries 4 times — can fail and return partial data

### StageRegistry::publish() — WRITES
```cpp
bool publish(const int slotIndex, ..., const StageTelemetry& telemetry) noexcept {
    // Local state write (has lock)
    {
        const juce::ScopedLock localScoped(localState.lock);
        slot.telemetry = telemetry;  // ✅ Protected
    }
    
    // Shared state write (InterProcessLock only — not intra-process)
    const auto version = slot->version.load(std::memory_order_acquire);
    slot->version.store(version + 1u, std::memory_order_release);  // Odd = write in progress
    slot->telemetry = telemetry;  // Audio thread, writing...
    slot->version.store(version + 2u, std::memory_order_release);  // Even = done
}
```

**Problem**: InterProcessLock is for cross-process sync, not thread-safe within a process.

---

## Solution: Add Mutex Protection

### Phase 1: Add Intra-Process Mutex (CRITICAL)

Add `mutable juce::CriticalSection` to both registries:

```cpp
class DomainRegistry {
private:
    mutable juce::CriticalSection registryMutex;
    
public:
    bool latestDomainForProcess(std::uint64_t hostProcessId, DomainView& out) const noexcept {
        const juce::ScopedLock scoped(registryMutex);  // ✅ Lock reads
        auto* state = domainState();
        if (state == nullptr) return false;
        // ... read safely
    }
    
    std::uint64_t registerAnalyserDomain(std::string_view ownerStageId) noexcept {
        const juce::ScopedLock scoped(registryMutex);  // ✅ Lock writes
        // ... already has InterProcessLock, now also has thread-local lock
    }
};
```

### Phase 2: Simplify StageRegistry Reads

Replace version-number polling with direct mutex:

```cpp
class StageRegistry {
private:
    mutable juce::CriticalSection registryMutex;
    
public:
    bool readStage(const int slotIndex, StageView& out) const noexcept {
        // Local state (already has lock)
        auto& localState = localStageRegistryState();
        {
            const juce::ScopedLock localScoped(localState.lock);
            if (slotIndex >= 0 && ...) {
                out.telemetry = slot.telemetry;  // ✅ Safe copy
                return true;
            }
        }
        
        // Shared state — mutex instead of version polling
        const juce::ScopedLock scoped(registryMutex);  // ✅ Lock read
        auto* slot = stageSlotAt(slotIndex);
        if (slot == nullptr) return false;
        out.telemetry = slot->telemetry;  // ✅ Atomic struct copy
        return true;
    }
};
```

### Phase 3: Unify Registry Locking Pattern

Both registries should use the same pattern:
- **Writes**: `juce::ScopedLock` + `InterProcessLock` (for shared state)
- **Reads**: `juce::ScopedLock` (consistent, simple)
- **No atomics needed** once mutex protects reads/writes

---

## Implementation Checklist

- [ ] Add `mutable juce::CriticalSection registryMutex` to `DomainRegistry`
- [ ] Add `mutable juce::CriticalSection registryMutex` to `StageRegistry`
- [ ] Wrap all `DomainRegistry::*()` read methods with `const juce::ScopedLock scoped(registryMutex);`
- [ ] Wrap all `DomainRegistry::register*()` write methods with the mutex (in addition to InterProcessLock)
- [ ] Replace version-number polling in `StageRegistry::readStage()` with direct `juce::ScopedLock`
- [ ] Wrap `StageRegistry::publish()` write with mutex
- [ ] Wrap `StageRegistry::findStageByDomainAndStageId()` read with mutex
- [ ] Test: Rapid plugin add/remove while Analyser is open (verify no crashes)
- [ ] Test: Analyser stage discovery with 5+ plugins in chain
- [ ] Regression: VXStudioPluginRegressionTests should still pass

---

## Estimated Timeline

**Implementation**: 4-6 hours
- File edits: Add mutex declarations + ScopedLock calls
- Minimal behavioral change (just adds synchronization)

**Testing**: 2-4 hours
- Unit tests for concurrent registry access
- Manual testing: plugin chain stress test
- Regression test suite

**Code review**: 1-2 hours

---

## Risk Assessment

**Low risk**:
- Changes are localized to registry classes
- `juce::CriticalSection` is RAII (exception-safe locking)
- Existing InterProcessLock calls remain unchanged

**Medium risk**:
- Removing version-number polling simplifies but changes synchronization strategy
- Need to verify no code relies on version numbers

**Mitigations**:
- No behavioral changes to external API
- Mutex scopes are identical to current critical sections (localState.lock)
- Full regression test coverage required

---

## Related Issues (Already Identified)

1. **Stage discovery latency** (P1) — Bandaid with 96 Hz timer
   - Caused by async registry updates
   - Would be faster with explicit discovery + mutex
   
2. **Spectrum history buffer** (P2) — Unclear bounds enforcement
   - Needs separate fix but less critical than thread safety

3. **RenderModel invalidation** (P2) — Complex race conditions
   - Separate issue, lower priority than registry safety

---

## Success Criteria

✅ All `readStage()` / `latestDomainForProcess()` calls protected by mutex  
✅ All `publish()` / `registerStage()` calls protected by mutex  
✅ Version-number polling removed from `readStage()`  
✅ Zero data races under ThreadSanitizer  
✅ Regression tests pass  
✅ Manual plugin chain stress test: no crashes after 100+ plugin adds/removes  

---

## Next Steps

1. **Assign to**: Someone comfortable with JUCE threading
2. **Branch**: Create feature branch `analyser-thread-safety`
3. **Start with**: Phase 1 (add mutex declarations)
4. **Validate with**: ThreadSanitizer before shipping
