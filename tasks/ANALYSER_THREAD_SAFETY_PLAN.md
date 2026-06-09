# VXStudioAnalyser Thread Safety — Implementation Plan

**Overall Status**: Phase 2 Complete ✅  
**Remaining**: Phase 3-4 (optional hardening only)

### Phase Status
- **Phase 1** ✅ COMPLETE — Mutex protection added to registries
- **Phase 2** ✅ COMPLETE — Domain generation counter for immediate stage discovery
- **Phase 3** ⏭️ NOT STARTED — Full registry architecture unification (optional)
- **Phase 4** ⏳ NOT STARTED — Validated data access patterns (optional)

---

## Original Problem Statement

**Status**: CRITICAL (data race protection needed before production)  
**Severity**: 🔴 Can cause segmentation faults, silent data corruption

---

## The Problem

**Current architecture** uses two registries for analyser telemetry:
- `DomainRegistry` — tracks Analyser instances per DAW project
- `StageRegistry` — tracks all stage telemetry slots

**Audio thread** writes to these registries via `StagePublisher::publish()`  
**UI thread** reads from these registries via `VXStudioAnalyserEditor` (timer callbacks at 24 Hz)

**Current risk is no longer raw thread tearing**. The registries now use intra-process mutexes plus interprocess fencing:
- UI reads and audio writes are serialized within a process
- Shared-memory writes are still fenced with version fields / process locks
- Remaining risk is discovery / matching logic, not struct tearing

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

### Phase 2: Implement Domain Generation Counter ✅ COMPLETED

Instead of full registry unification, implemented a more targeted solution:

**Root cause of latency**: StagePublisher polls for domain changes on a 96Hz timer, causing ~50-100ms delay before stages appear in the UI.

**Solution**: Add atomic generation counter to DomainRegistry:
- Increment counter when domain is registered/unregistered
- StagePublisher checks generation and forces immediate rebind when it changes
- Minimal code changes, no architectural restructuring needed

```cpp
// In DomainRegistry
private:
    std::atomic<std::uint32_t> domainGeneration { 0 };

public:
    std::uint32_t getDomainGeneration() const noexcept {
        return domainGeneration.load(std::memory_order_acquire);
    }

// In registerAnalyserDomain() and unregisterAnalyserDomain()
domainGeneration.fetch_add(1, std::memory_order_release);

// In StagePublisher::refreshDomainBinding()
const auto currentGeneration = domainReg.getDomainGeneration();
if (currentGeneration != lastSeenDomainGeneration) {
    // Force immediate rebind on generation change
    lastSeenDomainGeneration = currentGeneration;
    // ... refresh domain binding logic
}
```

**Result**: Stages appear instantly when Analysers are opened/closed (no ~50-100ms delay)

**Bonus cleanup**: Removed startup aggressive refresh optimization (4x timer for 500ms) since generation counter provides immediate feedback.

---

### Phase 3: Simplify StageRegistry Reads (Optional)

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

### Phase 1: Mutex Protection (✅ COMPLETE)
- [x] Add `mutable juce::CriticalSection registryMutex` to `DomainRegistry`
- [x] Add `mutable juce::CriticalSection registryMutex` to `StageRegistry`
- [x] Wrap all `DomainRegistry::*()` read methods with `const juce::ScopedLock scoped(registryMutex);`
- [x] Wrap all `DomainRegistry::register*()` write methods with the mutex (in addition to InterProcessLock)
- [x] Replace version-number polling in `StageRegistry::readStage()` with direct `juce::ScopedLock`
- [x] Wrap `StageRegistry::publish()` write with mutex
- [x] Wrap `StageRegistry::findStageByDomainAndStageId()` read with mutex

### Phase 2: Domain Generation Counter (✅ COMPLETE)
- [x] Add atomic generation counter to `DomainRegistry`
- [x] Implement `getDomainGeneration()` getter
- [x] Increment counter in `registerAnalyserDomain()` and `unregisterAnalyserDomain()`
- [x] Add generation tracking to `StagePublisher`
- [x] Force domain rebind when generation changes (immediate discovery)
- [x] Remove startup aggressive refresh optimization (4x timer for 500ms)
- [x] Test: All 6 regression tests pass
- [x] Verify: Stages appear instantly when Analysers are opened/closed

### Phase 3: Registry Unification (⏳ NOT STARTED - Optional)
- [ ] Evaluate whether full registry restructuring is needed (currently solved by generation counter)
- [ ] If needed: Unify DomainRegistry and StageRegistry architecture
- [ ] If needed: Consolidate duplicate fields and patterns

### Phase 4: Data Access Validation (⏳ NOT STARTED - Optional)
- [ ] Add bounds checking to prevent out-of-bounds stage/domain access
- [ ] Validate data structure consistency
- [ ] Add ThreadSanitizer verification

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
