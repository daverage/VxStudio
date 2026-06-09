# Code Review Action Plan — Analyser Rewrite + Repair Always-Run STFT
_Generated 2026-06-07 from /code-review high_

---

## P0 — Correctness / real-time safety (fix before shipping)

### 1. Replace CriticalSection with lock-free publish in AnalyserProcessor
**File:** `VXStudioAnalyserProcessor.cpp` line 154  
**Problem:** `publishLiveInputSummary()` acquires a `juce::CriticalSection` on the audio thread every `processBlock`. If the message thread holds the same lock in `liveInputSummary()`, the audio thread blocks → dropout.  
**Fix:** Mirror the existing signalQuality pattern — four atomics. `AnalysisSummary` is too large for a single atomic, so use a seqlock or versioned copy:
- Add `std::atomic<uint32_t> liveInputVersion { 0 }` (odd = writing, even = done)
- Writer: `version.fetch_add(1, release)` → copy snapshot → `version.fetch_add(1, release)`
- Reader: spin until version is even, read snapshot, recheck version hasn't changed
- Remove `liveInputLock` (CriticalSection) entirely.

---

### 2. Fix STFT pass-through artefacts when denoiser/deverb are off
**File:** `VxRepairProcessor.cpp` lines 499–535  
**Problem:** `denoiserDsp.processInPlace(buffer, 0.0f, ...)` and `deverbDsp.processInPlace(buffer, 0.0f, ...)` run full FFT→IIR→IFFT→OLA pipelines on the main audio buffer at strength 0. The IIR gain smoother converges asymptotically (not immediately 1.0), so output ≠ input — audible colouration when both stages are "off".  
**Fix:** Two independent solutions, pick one:
- **(A) Preferred:** Add a `processStateOnly(AudioBuffer&, opts)` method to both DSP classes that advances internal FIFO/STFT state but writes back the *original input* rather than the processed output (true pass-through with constant latency).
- **(B) Acceptable short term:** Snapshot the buffer before each 0-strength call and copy it back after: `tempBuf = buffer; dsp.processInPlace(buffer, 0, opts); buffer = tempBuf;` — guaranteed transparency at the cost of one copy.

---

### 3. Fix clarityListen scratch aliasing
**File:** `VxRepairProcessor.cpp` lines 469–472  
**Problem:** `scratchView` and `scratch2View` both wrap `stftStateScratch.getArrayOfWritePointers()`. After `denoiserDsp` mutates the scratch, `deverbDsp` receives denoiser-processed audio — STFT state diverges from main chain, causing transients when exiting listen mode.  
**Fix:** Remove `scratch2View` entirely; reuse `scratchView`:
```cpp
juce::AudioBuffer<float> scratchView(stftStateScratch.getArrayOfWritePointers(), numCh, numSamples);
denoiserDsp.processInPlace(scratchView, 0.0f, noiseOpts);
// Need a fresh copy for deverb — re-fill scratch from the original buffer copy
// (scratch was already filled above in the listen-mode copy block, but denoiser clobbered it)
// Solution: use two separate scratch buffers, or copy before each call.
```
Concrete fix: allocate `stftStateScratch2` in `prepareSuite` (same size), fill both from `buffer` in the listen-mode copy block, then use separate scratch for each DSP.

---

### 4. Guard makeupGain when all stages off
**File:** `VxRepairProcessor.cpp` line ~542  
**Problem:** Removing the `if (!anyActive && !anyListen) return` early-out means `makeupDb` is applied to the buffer even when noise, reverb, and clarity are all disabled. Breaks level-transparent bypass comparisons.  
**Fix:** Wrap the makeup gain block:
```cpp
const bool anyActive = noiseOn || reverbOn || clarityOn;
if ((anyActive || anyListen) && std::abs(makeupLinear - 1.0f) > 0.001f)
    buffer.applyGain(makeupLinear);
```
Or restore the activity-decay-only early return for the no-active/no-listen case (before the STFT runs), and route only state-advance through the STFT — consistent with fix #2 above.

---

## P1 — Regression / reliability (fix soon)

### 5. Restore transient-failure domain guard in StagePublisher
**File:** `VxStudioSpectrumTelemetry.cpp` line 1654  
**Problem:** If `allDomainsForProcess()` returns 0 transiently (lock timeout, stale mmap), the stage immediately rebinds to the fallback domain and drops from the analyser view. The old guard `if (!force && analysisDomainIdValue != 0 && analysisDomainIdValue != fallbackId) return` prevented this.  
**Fix:** Re-add that guard just before the fallback assignment:
```cpp
if (newDomainId == 0) {
    const auto fallbackId = domainReg.fallbackDomainIdForCurrentProcess();
    if (!force && analysisDomainIdValue != 0 && analysisDomainIdValue != fallbackId)
        return;  // preserve working real binding on transient failure
    newDomainId = fallbackId;
}
```

---

### 6. Fix liveInputSnapshotValid memory ordering
**File:** `VXStudioAnalyserProcessor.cpp` line 164  
**Problem:** `liveInputSummaryValid()` reads `liveInputSnapshotValid` with `memory_order_relaxed` and no lock — no happens-before with the lock-protected write. On ARM this could expose a stale flag.  
**Fix:** Change the load to `memory_order_acquire` and the store (already inside the lock) to `memory_order_release`:
```cpp
// writer (inside ScopedLock):
liveInputSnapshotValid.store(true, std::memory_order_release);

// reader:
return liveInputSnapshotValid.load(std::memory_order_acquire);
```
Note: this item becomes moot if fix #1 is implemented (lock + atomic replaced by seqlock).

---

## P2 — Cleanup (tidy up before review)

### 7. Remove dead ternary in selectionTitle
**File:** `VXStudioAnalyserController.cpp` line 392  
```cpp
// Before:
: !filterResult.trackAvailable ? "" : "";
// After:
: "";
```

### 8. Remove redundant scratch2View
**File:** `VxRepairProcessor.cpp` line 471  
After fix #3 (separate scratch buffers), `scratch2View` goes away naturally. If #3 isn't landed yet, at minimum rename `scratch2View` → `scratchView` and use the same object for the second call.

### 9. Deduplicate bandCenterHz / toDb / formatFrequency
**Files:** `VXStudioAnalyserController.cpp`, `VxSpectrumSmoothingPipeline.cpp`, `VXStudioAnalyserEditor.cpp` (anon namespace)  
All three have identical implementations. Extract to a single header, e.g. `VXStudioAnalyserHelpers.h`, and include it from all three. Keeps the -72 dB floor and 20–20k Hz mapping in one place.

### 10. Split resolveSelection into mutation + query
**File:** `VXStudioAnalyserController.cpp` line 197  
`resolveSelection()` silently prunes `selectedInstanceIds` and resets `fullChainSelected` as a side-effect inside a method named and called as a pure resolver. A stale stage for one timer tick kills the user's explicit selection.  
Fix: split into:
```cpp
void reconcileSelectionState(const std::vector<StageSnapshot>& inScope);  // mutates
DryWetPair resolveSelection(const std::vector<StageSnapshot>& inScope, ...) const;  // pure
```
Call `reconcileSelectionState` once at the top of `refresh()`, then pass the clean state into a pure `resolveSelection`.

---

## Order of attack

| # | Item | Est. effort |
|---|------|-------------|
| 2 | STFT pass-through artefacts (option B stopgap) | 30 min |
| 3 | clarityListen scratch aliasing | 30 min |
| 4 | makeupGain guard | 15 min |
| 5 | Domain transient-failure guard | 10 min |
| 1 | Seqlock for liveInputSummary | 1–2 h |
| 6 | Memory ordering fix (moot after #1) | 10 min |
| 7–10 | Cleanup | 1 h total |
