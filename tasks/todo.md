# Inference thread refactor - DeepFilterNet

## Goal
Move ONNX/RNNoise inference off the audio thread into a dedicated per-channel
inference thread. Audio thread only does FIFO push/pop and an atomic notify.

## Changes

### VxDeepFilterNetService.h
- [x] Remove `SampleFifo` struct
- [x] Add `ThreadSafeSampleFifo` (backed by `juce::AbstractFifo` for SPSC lock-free)
- [x] Update `ChannelState`: replace SampleFifo fields, add `inferenceThread`,
      `workSignal` (atomic<int>), `stopInference` (atomic<bool>),
      `requestedAttenDb` (atomic<float>), `requestedStrength` (atomic<float>)
- [x] Declare `runInferenceLoop(ChannelState&, RuntimeBundle&)`

### VxDeepFilterNetService.cpp
- [x] Implement `ThreadSafeSampleFifo::reset/clear/push/pop`
- [x] `releaseBundle`: stop + join inference threads BEFORE destroying runtimes
- [x] `prepareChannel`: start inference thread after channel setup
- [x] `processRealtime` callback: push input, atomic notify, pop output - no inference
- [x] Store `requestedAttenDb` / `requestedStrength` per channel before callback
- [x] Remove `setRuntimeAttenuation` call from audio thread
- [x] Implement `runInferenceLoop`: wait on workSignal, drain inputFifo, infer, push output

## Status: COMPLETE - builds clean
