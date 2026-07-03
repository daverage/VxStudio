# VxRebalanceAI RT3S backend investigation

Date: 2026-07-02

## Scope

This note records the first pass over GPU Audio RT3S as a possible realtime GPU stem-separation backend for VxRebalanceAI. The current product implementation remains ONNX/StemgenRT-based, and the new code boundary keeps that as the fallback backend.

Decision update: GPU Audio RT3S is deferred for VxRebalanceAI. The product should remain CPU/ONNX-based for now because we do not want proprietary GPU Audio binaries, external platform installs, or redistribution/licensing risk in this plugin.

## Repositories inspected

- `https://github.com/gpuaudio/gpuaudio-sdk`
- `https://github.com/gpuaudio/rt3s_processor`

Local inspection copies were cloned under `/tmp/gpuaudio-sdk` and `/tmp/rt3s_processor`.

## Public SDK build/deployment notes

From `/tmp/gpuaudio-sdk/installation/main.md`:

- macOS supported versions are listed as 13, 14, 15, and 26.
- macOS prerequisites are Xcode, CMake 3.26.3+, and GPU Audio Platform.
- macOS configure command is `cmake -S . -B @BUILD -G Xcode`.
- Tests require adding `path_to_gpuaudio_sdk/@BUILD/bin/RelWithDebInfo` to `GPUAUDIO_PROCESSOR_PATH`.
- DAW use also requires `GPUAUDIO_PROCESSOR_PATH` to contain the built processor `.so` files, commonly via `launchctl setenv GPUAUDIO_PROCESSOR_PATH path_to_sdk/@BUILD/bin/RelWithDebInfo`, then restarting the DAW.
- Plugin deployment can be enabled with `-DVST3_DEPLOY=1` and/or `-DAUv2_DEPLOY=1`.

From `/tmp/gpuaudio-sdk/.gitmodules`:

- `SoundSourceSeparation/rt3s_processor` maps to `https://github.com/gpuaudio/rt3s_processor`.
- `SoundSourceSeparation/rt3s_plugin` maps to `https://github.com/gpuaudio/rt3s_plugin`.
- `SoundSourceSeparation/RT3SLib` maps to `https://github.com/gpuaudio/RT3SLib`.

The SDK top-level CMake includes `SoundSourceSeparation/rt3s_processor` in `PROCESSOR_LIST` and adds `SoundSourceSeparation`.

## RT3S processor facts

From `/tmp/rt3s_processor/README.md`:

- RT3S is a GPU Audio port of an HS-TasNet-style realtime source-separation model.
- Input is a stereo mixture.
- Output is four stereo streams: vocals, drums, bass, and other.
- The streaming design takes 512 new samples and combines them with the previous 512 samples to form a 1024-sample frame.
- Overlap handling emits the current 512 samples and keeps the remaining 512 for the next frame.

From `/tmp/rt3s_processor/rt3s_processor/src/Rt3sProcessor.cpp`:

- `c_signal_length = 1024`
- `c_hop_size = 512`
- `c_nchannels = 2`
- `c_buffer_capacity = 512`
- `c_nsources = 4`
- Output port `channel_count = c_nsources * c_nchannels`, so 8 channels.
- `Rt3sProcessor::SetUpCommon()` allocates persistent GPU memory, initialises weights, creates GPU task descriptors, and configures the output port.

From `/tmp/rt3s_processor/rt3s_processor/src/Rt3sInputPort.cpp`:

- Input port requires `PortDataType::eSample32`.
- Input port requires `channel_count == 2`.
- Output port is configured to 8 channels.
- Output `transfer_to_cpu` is set to `false`, so raw CPU stem buffers are not exposed by this processor path as-is.

From `/tmp/rt3s_processor/rt3s_processor/src/device/Rt3sProcessor.cuh`:

- Device task comments describe input as planar channel blocks: all samples of one channel, then all samples of the next.
- The enqueue task stores stereo input into a two-channel ring buffer.

## RT3SLib public API

After cloning the full SDK with submodules to `/tmp/gpuaudio-sdk-full`, `RT3SLib` was available.

From `/tmp/gpuaudio-sdk-full/SoundSourceSeparation/RT3SLib/include/SoundSourceSepInterface.h`:

- `createGpuProcessor(char const* params_path, bool async = true)` returns `std::unique_ptr<SoundSourceSepInterface>`.
- `SoundSourceSepInterface::arm()` prepares the processor.
- `SoundSourceSepInterface::disarm()` tears the processor down.
- `SoundSourceSepInterface::process(float const* const* in_buffer, float* const* out_buffer, int nsamples)` accepts two input channel pointers and writes eight output channel pointers.

From `/tmp/gpuaudio-sdk-full/SoundSourceSeparation/RT3SLib/rt3slib/src/GPUSoundSourceSep.cpp`:

- `c_nchannels_in = 2`
- `c_nchannels_out = 8`
- `c_buffer_capacity = 512`
- The client can run sync or async.
- It uses `GpuAudioManager::GetGpuAudio()`, creates a launcher, creates a processing graph, and looks for a module with id `rt3s`.
- Runtime requires GPU Audio Platform, including `libgpu_audio.dylib` on macOS.

From `/tmp/gpuaudio-sdk-full/SoundSourceSeparation/rt3s_plugin/src/PluginProcessor.cpp`:

- The public plugin creates the processor in its constructor with `createGpuProcessor(params.bw, true)`.
- Its audio callback calls `_proc->process(inStereo.data(), outStereoX4.data(), numSamples)`.
- It maps output channels as four stereo output buses.

## Local build results

Standalone RT3SLib build:

```sh
cmake -S /tmp/gpuaudio-sdk-full/SoundSourceSeparation/RT3SLib -B /tmp/rt3slib_build -DRT3S_DISABLE_TESTS=ON
cmake --build /tmp/rt3slib_build -j 6
```

Result: `librt3slib.a` built successfully.

Historical opt-in RT3S build proof:

```sh
cmake -S . -B build-gpuaudio \
  -DVXSTUDIO_REBALANCE_AI_GPUAUDIO_SDK_DIR=/tmp/gpuaudio-sdk-full \
  -DVXSTUDIO_REBALANCE_AI_RT3S_PARAMS_PATH=/tmp/rt3s_model/params.bw
cmake --build build-gpuaudio --target VxRebalanceAI_VST3 -j 6
```

Result: VxRebalanceAI built and linked with `rt3slib`.

This opt-in path was removed after the decision to keep VxRebalanceAI CPU/self-contained.

Runtime test without GPU Audio Platform installed:

- Initial smoke test aborted because `libgpu_audio.dylib` was not found.
- The backend now preflights the expected macOS engine path and `GPUAUDIO_PATH`.
- With the guard in place, the smoke test falls back cleanly to ONNX and passes.

## Current blockers for direct integration

- The public `rt3s_processor` repo license is CC BY-NC-SA 4.0, so it is not suitable for commercial product use without separate commercial permission.
- Several RT3S source headers say proprietary/confidential. Treat the public source as reference only until GPU Audio/Soundcore licensing is clarified.
- Public SDK deployment uses external GPU Audio Platform pieces and `GPUAUDIO_PROCESSOR_PATH`.
- RT3SLib does provide a CPU-facing client API that returns eight output channel buffers.
- GPU Audio Platform is not installed in this local test environment. On macOS the runtime searched `/Library/Application Support/GPU Audio/LTS/v2/engine` and `GPUAUDIO_PATH`.
- A local configure attempt for `/tmp/rt3s_processor` with `cmake -S /tmp/rt3s_processor -B /tmp/rt3s_processor_build -G Xcode` failed because the local Xcode generator reported `Xcode 1.5 not supported`. That is an environment/toolchain issue, not proof that RT3S cannot build on macOS.

## Architecture decision from this pass

The plugin should keep the audio callback backend-agnostic. The audio callback should collect 512-sample chunks, push them to the worker queue, reuse the latest valid `AiMaskFrame`, and never call GPU Audio/ONNX/Core ML directly.

The worker thread owns the selected backend. It calls the backend, receives four stereo stems, builds the spectral mask frame, and publishes that latest frame to the DSP.

## Deferred RT3S checks

Only revisit RT3S if we decide to accept a proprietary runtime dependency or receive explicit redistribution terms from GPU Audio/Soundcore.

1. Confirm commercial licensing and redistribution rights for GPU Audio Platform, RT3S processor modules, and `params.bw`.
2. Install GPU Audio Platform on macOS from `https://gpu.audio/sdk-binaries` and confirm `libgpu_audio.dylib` is available under `/Library/Application Support/GPU Audio/LTS/v2/engine` or `GPUAUDIO_PATH`.
3. Set `GPUAUDIO_PROCESSOR_PATH` so the runtime can find the built `rt3s` processor module.
4. Verify RT3S source order by listening/analyzing the eight output channels.
5. Verify whether RT3S supports 44.1 kHz directly; the inspected processor constants define frame sizes but do not state sample-rate restrictions.
6. Verify whether 48 kHz is supported or whether the model/weights are tied to 44.1 kHz.
7. Verify multiple-instance behavior through GPU Audio scheduler/client tests.
