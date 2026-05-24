# VxStudio CPU/Memory Profiling Guide

This guide explains how to profile VxStudio plugins for CPU usage, memory leaks, and performance regressions using the VxStudioTestHost.

## Quick Start

### Build
```bash
cmake --build build -j8
ls -l build/bin/VxStudioTestHost
```

### Test
```bash
# Create test audio
ffmpeg -f lavfi -i sine=f=440:d=5 -ar 48000 test_audio/tone_5s.wav

# Run test host
./build/bin/VxStudioTestHost test_audio/tone_5s.wav /tmp/out.wav
```

## CPU Profiling

### Method 1: `perf` (Linux)
```bash
# Record CPU profile
perf record -F 99 -g ./build/bin/VxStudioTestHost test_audio/tone_30s.wav /tmp/out.wav

# View results
perf report

# Summary statistics
perf stat ./build/bin/VxStudioTestHost test_audio/tone_30s.wav /tmp/out.wav
```

### Method 2: Instruments (macOS)
```bash
xcrun xctrace record --template "System Trace" \
  ./build/bin/VxStudioTestHost test_audio/tone_30s.wav /tmp/out.wav
open *.trace
```

### Method 3: Carla + VulcanAudio (Visual)
```bash
# (See user's recommendation above)
# Use Carla to load plugins, profile with external tools
```

## Memory Profiling

### Valgrind - Memory Leaks
```bash
valgrind --leak-check=full \
         --show-leak-kinds=all \
         ./build/bin/VxStudioTestHost test_audio/tone_30s.wav /tmp/out.wav 2>&1 | tee leak_report.txt
```

### Valgrind - Memory Usage
```bash
valgrind --tool=massif \
         --massif-out-file=massif.out \
         ./build/bin/VxStudioTestHost test_audio/tone_30s.wav /tmp/out.wav

ms_print massif.out > memory_profile.txt
```

### Heaptrack (More detailed)
```bash
heaptrack ./build/bin/VxStudioTestHost test_audio/tone_30s.wav /tmp/out.wav
heaptrack_gui heaptrack.*.gz
```

## Performance Regression Testing

### Baseline Measurement
```bash
# Establish current performance
perf stat -r 5 ./build/bin/VxStudioTestHost test_audio/tone_30s.wav /tmp/out.wav > baseline.txt
cat baseline.txt
```

### After Code Changes
```bash
# Measure again
perf stat -r 5 ./build/bin/VxStudioTestHost test_audio/tone_30s.wav /tmp/out.wav > current.txt

# Compare
# Should see same or lower cycle count
# Alert if cycles increase >5%
```

## Integration with CI

Add to CI pipeline:
```bash
# Performance regression check
echo "Running CPU profile..."
perf stat -e cycles,instructions,cache-references,cache-misses \
  ./build/bin/VxStudioTestHost test_audio/tone_30s.wav /tmp/out.wav

# Memory leak check
echo "Running leak detection..."
valgrind --leak-check=full --error-exitcode=1 \
  ./build/bin/VxStudioTestHost test_audio/tone_30s.wav /tmp/out.wav

# Fail if leaks detected
if [ $? -ne 0 ]; then
  echo "Memory leaks detected!"
  exit 1
fi
```

## Future Enhancements

1. **Plugin Loading** - Load VST3 plugins and profile chains
2. **JACK Support** - Real-time audio chain profiling
3. **Parameter Sweeping** - Test plugins at different parameter values
4. **Output Verification** - Automated audio quality checks
5. **Report Generation** - JSON output for comparison tools

## Tools Comparison

| Tool | Use Case | Platform | Learning Curve |
|------|----------|----------|-----------------|
| `perf` | CPU hotspot identification | Linux | Medium |
| `Instruments` | Comprehensive profiling | macOS | Low |
| `Valgrind` | Memory leaks, detailed analysis | All | Medium |
| `Heaptrack` | Detailed memory allocation | Linux | Medium |
| Carla + VulcanAudio | Plugin chains, visual | All | High |

## Next Steps

1. ✅ Build VxStudioTestHost (done)
2. ⏳ Extend to load and chain VST3 plugins
3. ⏳ Add JACK audio support
4. ⏳ Automate CI performance regression tests
5. ⏳ Generate comparison reports

See PROFILING_ROADMAP.md for detailed implementation plan.
