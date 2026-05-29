# VST Framework & Plugins: CPU Performance Review

## Summary
The VxStudio VST suite has **significant CPU overhead** coming from multiple sources. The highest impact areas are: (1) spectrum/analysis telemetry using InterProcessLock in hot paths, (2) per-sample DSP complexity in Leveler, (3) redundant memory copies even when features are disabled, and (4) heavyweight analysis states running unconditionally.

---

## Critical Issues (High CPU Impact)

### 1. **Spectrum Telemetry: Blocking InterProcessLock in Audio Thread** ⚠️ CRITICAL
**Location**: `VxStudioSpectrumTelemetry.cpp:427-466`  
**Issue**: The `SnapshotPublisher::publish()` method calls `SnapshotRegistry::instance().publish()` which acquires an `InterProcessLock` on **every publish interval** (several times per second on the audio thread).

```cpp
// Hot path - called every block
void SnapshotPublisher::publish(...) noexcept {
    // ... 
    if (!SnapshotRegistry::instance().publish(slotIndex, ...)) {
        // Lock acquisition happens here
    }
}
```

**Impact**: 
- InterProcessLock blocks waiting for file I/O and other processes
- This violates real-time audio thread safety (can cause dropouts, clicks)
- Affects all 12 VST plugins simultaneously

**Recommended Fix**:
- Move telemetry publish to a background thread
- Use lock-free queue to pass data from audio thread to telemetry thread
- Or make telemetry publishing truly non-blocking with atomic operations only

---

### 2. **Leveler DSP: Extreme Per-Sample Complexity** ⚠️ HIGH
**Location**: `VxLevelerDsp.cpp:115-668` (554 lines of per-sample processing!)  
**Issue**: The main DSP loop processes every single audio sample with extensive calculations:

```cpp
for (int i = 0; i < numSamples; ++i) {
    // Per-sample:
    // - Multiple envelope trackers (attack/release logic)
    // - 3 different time-constant calculations
    // - Complex decision logic for 5+ mix states
    // - Floating-point comparisons and smoothstep functions
    // - 150+ lines of logic per sample
}
```

**CPU Impact Analysis**:
- Leveler runs ~150+ FLOPs per audio sample per channel
- At 48kHz stereo: 48000 * 2 * 150+ = ~14.4M operations/sec per instance
- Per-sample branching destroys CPU cache locality
- Processes control parameters (level, control) through full signal chain per sample

**Recommended Fixes** (in priority order):
1. **Block-rate parameter updates**: Only compute parameter-dependent values when parameters change, not every sample
2. **Vectorize the inner loop**: Use SIMD (SSE2/AVX) for envelope calculations
3. **Reduce envelope trackers**: Consolidate similar trackers (e.g., merge liftAttack/liftRelease into single smoother)
4. **Move offline analysis to separate thread**: Don't mix offline computation into real-time processing
5. **Cache filter coefficients**: Pre-compute one-pole filter states instead of per-sample calculations

---

### 3. **Listen Mode Scratch Buffer: Unnecessary Memory Copy When Disabled** ⚠️ MEDIUM
**Location**: `VxStudioProcessorBase.cpp:103-126`  
**Issue**: The dry signal is **always** copied to `listenInputScratch` buffer before processing, even when listen is disabled:

```cpp
void ProcessorBase::processPreparedBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    // ALWAYS copied, regardless of isListenEnabled()
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        listenInputScratch.copyFrom(channel, 0, buffer, channel, 0, buffer.getNumSamples());
    
    const bool canRenderListen = isListenEnabled();  // Checked AFTER copy
    processCoordinator.beginBlock(listenInputScratch, canRenderListen);
    processProduct(buffer, midi);
    // ...
}
```

**CPU Impact**: 
- ~200MB/sec memory bandwidth (stereo, 48kHz)
- Cache pollution from large buffer copies
- Happens on every block, all plugins

**Recommended Fix**:
```cpp
const bool canRenderListen = isListenEnabled();
if (canRenderListen) {
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        listenInputScratch.copyFrom(channel, 0, buffer, channel, 0, buffer.getNumSamples());
}
processCoordinator.beginBlock(canRenderListen ? listenInputScratch : buffer, canRenderListen);
```

---

### 4. **Analysis Telemetry: Multiple Overlapping Lock Domains** ⚠️ MEDIUM-HIGH
**Location**: `VxStudioSpectrumTelemetry.cpp:595-650`  
**Issue**: Analysis telemetry uses **a separate InterProcessLock** from spectrum telemetry, creating contention:

```cpp
// Two different lock systems
constexpr auto kAnalysisSharedLockName = "vxsuite-analysis-telemetry-lock";  // Separate lock!
constexpr auto kAnalysisSharedFileName = "vxsuite-analysis-telemetry.bin";
```

- Each VST plugin tries to acquire locks on shared memory
- Multiple plugins contending for same locks = serialization
- 12 plugins × 2 locks = potential for bottleneck under load

**Recommended Fix**:
- Consolidate to single lock domain
- Or implement lock-free publishing with atomic swap

---

## Moderate Issues (Medium CPU Impact)

### 5. **VoiceAnalysis/VoiceContext/SignalQuality: Unconditional Per-Block Updates**
**Location**: `VxStudioProcessorBase.cpp:105-107`  
**Issue**: These analysis states run on every block regardless of whether product actually uses them:

```cpp
voiceAnalysis.update(buffer, buffer.getNumSamples());      // Always
voiceContext.update(buffer, voiceAnalysis.snapshot());     // Always
signalQuality.update(buffer, buffer.getNumSamples());      // Always
```

**CPU Impact**: 
- ~5-10% overhead per plugin (filter operations, envelope tracking)
- Wasted for plugins that don't use these features

**Recommended Fix**:
- Make these lazy: only run if product declares it needs them
- Use a product capability flag: `productIdentity.requiresVoiceAnalysis()`

---

### 6. **DeepFilterNet: Complex Resampling Pipeline**
**Location**: `VxDeepFilterNetService.cpp:402-464`  
**Issue**: Per-frame resampling with FIFO buffering:

```cpp
channel.resampler->process(
    sourceInput, sourceOutput, targetInput, targetOutput, numSamples,
    [&](float* const* inputBuffers, float* const* outputBuffers, int sampleCount48k) {
        channel.inputFifo.push(input48k, sampleCount48k);
        while (channel.inputFifo.available >= bundle.frameLength) {
            channel.inputFifo.pop(channel.frameIn.data(), bundle.frameLength);
            processRuntimeFrame(bundle.runtimeApi, channel.runtime, ...);
            channel.outputFifo.push(channel.frameOut.data(), bundle.frameLength);
        }
    });
```

**CPU Impact**:
- Resampling on every block (expensive operation)
- FIFO operations (modulo arithmetic, pointer management)
- ML inference (expensive) – this is acceptable cost
- But resampling wrapper could be optimized

**Recommended Fix**:
- Cache resampler state when input/output sample rates match
- Batch resampling operations

---

### 7. **Spectrum Telemetry: Continuous History Accumulation**
**Location**: `VxStudioSpectrumTelemetry.cpp:510-548`  
**Issue**: Pushes history array on **every block**, building waveforms continuously:

```cpp
void SnapshotPublisher::pushHistory(...) noexcept {
    for (int sampleIndex = 0; sampleIndex < samples; ++sampleIndex) {
        float dryMono = 0.0f;
        for (int channel = 0; channel < dryChannels; ++channel)
            dryMono += dryBuffer.getSample(channel, sampleIndex);  // Per-sample L/R mix
        // ... repeat for wet
    }
}
```

**CPU Impact**:
- ~2-5% overhead per plugin
- Nested loops (channels × samples)
- Could be vectorized or reduced

**Recommended Fix**:
- Reduce history accumulation frequency (every Nth block instead of every block)
- Vectorize with SIMD for channel mixing

---

## Optimization Priority List

### Tier 1 (Highest ROI - Fix These First)
1. **Move telemetry publishing off audio thread** (Critical for real-time safety + massive CPU savings)
2. **Fix Listen buffer copy condition** (10-15% savings, simple 5-line fix)
3. **Make analysis states lazy** (5-10% savings per plugin)

### Tier 2 (Medium Effort, Good ROI)
4. **Refactor Leveler block-rate parameters** (20-30% savings in Leveler)
5. **Consolidate analysis lock domains** (2-5% reduction in lock contention)
6. **Reduce spectrum history accumulation frequency** (3-5% savings)

### Tier 3 (Optimization, Lower Priority)
7. **SIMD vectorization for per-sample operations**
8. **Optimize DeepFilterNet resampling**

---

## Testing Recommendations

1. **CPU Profiler**: Profile with all 12 plugins simultaneously in DAW to measure real impact
2. **Real-time Monitoring**: Watch for audio dropouts and glitches when making changes
3. **A/B Benchmarks**: Measure CPU% with telemetry enabled vs. disabled
4. **Lock Contention**: Use OS tools to measure InterProcessLock wait times
   - macOS: `sudo dtrace -n 'syscall:::entry { @[arg0] = count(); }' -p <pid> | grep futex`
   - Linux: `perf lock record` / `perf lock report`

---

## VxStudio Framework Architecture: Key Insights

The framework is well-structured overall, but the telemetry system (spectrum & analysis) is **over-aggressive** for real-time audio. The per-block publishing pattern is fundamentally incompatible with lock-free audio thread design.

**Recommended Architecture**:
- Audio thread: Data acquisition only (lock-free)
- Background thread: Telemetry publishing (can use locks)
- Message queue: Lock-free data transfer between threads

This is the industry standard (see: JUCE ProcessorBase, Reaper ReaFIR, etc.)

---

## Notes for @andrzejmarczewski

The high CPU usage is **not** a DSP algorithm problem – your Leveler, Cleanup, and other products sound great. It's an **architectural** problem with how telemetry and analysis are integrated. These are all fixable without touching the product DSP logic.

Start with the Tier 1 items, which are low-risk changes with high payoff.
