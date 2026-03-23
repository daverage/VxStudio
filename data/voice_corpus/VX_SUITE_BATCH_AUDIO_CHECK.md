# VX Suite Batch Audio Check

This summary consolidates the first real-file `Voice`-mode batch checks from `VXSuiteBatchAudioCheck`.

The harness lives at [tests/VXSuiteBatchAudioCheck.cpp](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/tests/VXSuiteBatchAudioCheck.cpp) and can process corpus WAVs through multiple VX products, compute shared metrics, and optionally write rendered outputs.

## How to run

Build:

```bash
cmake -S . -B build
cmake --build build --target VXSuiteBatchAudioCheck -j4
```

Example:

```bash
./build/VXSuiteBatchAudioCheck \
  /Users/andrzejmarczewski/Documents/GitHub/VxStudio/data/voice_corpus/wav \
  /Users/andrzejmarczewski/Documents/GitHub/VxStudio/data/voice_corpus/VX_SUITE_BATCH_AUDIO_CHECK.md \
  --products=leveler,cleanup
```

Optional render output:

```bash
./build/VXSuiteBatchAudioCheck \
  /Users/andrzejmarczewski/Documents/GitHub/VxStudio/data/voice_corpus/wav \
  /Users/andrzejmarczewski/Documents/GitHub/VxStudio/data/voice_corpus/VX_SUITE_BATCH_AUDIO_CHECK.md \
  --products=leveler,cleanup \
  --renders=/Users/andrzejmarczewski/Documents/GitHub/VxStudio/data/voice_corpus/renders
```

## Shared metrics

- `Spread in/out`: 100 ms windowed loudness-spread estimate. Lower out than in means the track became more even.
- `Improvement`: `spread_in - spread_out`. Positive is better for levelling-style products.
- `Corr`: output/input correlation. Lower values usually mean a stronger tonal or subtractive change.
- `Delta RMS`: RMS of the difference signal in dB. Less negative means a larger audible change.
- `Peak out`: output peak level in dBFS.

## Current measured baseline

### Full speech corpus (`wav/`)

Source reports:

- [VX_SUITE_BATCH_AUDIO_CHECK_PART1.md](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/data/voice_corpus/VX_SUITE_BATCH_AUDIO_CHECK_PART1.md)
- [VX_SUITE_BATCH_AUDIO_CHECK_CLEANUP_METRICS.md](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/data/voice_corpus/VX_SUITE_BATCH_AUDIO_CHECK_CLEANUP_METRICS.md)

Summary:

| Product | Corpus | Avg spread in | Avg spread out | Avg improvement | Avg corr |
|---|---|---:|---:|---:|---:|
| `Leveler` | full corpus | `16.459 dB` | `14.077 dB` | `+2.382 dB` | `0.932` |
| `Cleanup` | full corpus | `16.459 dB` | `16.838 dB` | `-0.379 dB` | `0.996` |

`Cleanup` is the important exception to the simple spread story: on the full speech corpus its loudness spread gets slightly worse, but the new speech-preservation metrics show it is still behaving sensibly as a corrective processor rather than a leveller:

- avg speech-band correlation: `0.996`
- avg gain-matched residual ratio: `0.091`

So for `Cleanup`, spread should be treated as a guardrail, not the primary success metric.

### Short speech corpus (`wav_short/`)

Source report:

- [VX_SUITE_BATCH_AUDIO_CHECK_LIGHT_SHORT.md](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/data/voice_corpus/VX_SUITE_BATCH_AUDIO_CHECK_LIGHT_SHORT.md)

Summary:

| Product | Corpus | Avg spread in | Avg spread out | Avg improvement | Avg corr |
|---|---|---:|---:|---:|---:|
| `Finish` | 2-file short corpus | `11.557 dB` | `8.822 dB` | `+2.734 dB` | `0.978` |
| `OptoComp` | 2-file short corpus | `11.557 dB` | `8.885 dB` | `+2.672 dB` | `0.979` |
| `Tone` | 2-file short corpus | `11.557 dB` | `11.378 dB` | `+0.179 dB` | `0.999` |
| `Proximity` | 2-file short corpus | `11.557 dB` | `11.190 dB` | `+0.367 dB` | `0.993` |

### Single 5s vocal clip (`wav_clip/`)

Source reports:

- [VX_SUITE_BATCH_AUDIO_CHECK_DEVERB_CLIP.md](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/data/voice_corpus/VX_SUITE_BATCH_AUDIO_CHECK_DEVERB_CLIP.md)
- [VX_SUITE_BATCH_AUDIO_CHECK_DENOISER_CLIP.md](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/data/voice_corpus/VX_SUITE_BATCH_AUDIO_CHECK_DENOISER_CLIP.md)
- [VX_SUITE_BATCH_AUDIO_CHECK_SUBTRACT_CLIP.md](/Users/andrzejmarczewski/Documents/GitHub/VxStudio/data/voice_corpus/VX_SUITE_BATCH_AUDIO_CHECK_SUBTRACT_CLIP.md)

Summary:

| Product | Corpus | Spread in | Spread out | Improvement | Corr |
|---|---|---:|---:|---:|---:|
| `Deverb` | 5s clip | `9.153 dB` | `9.602 dB` | `-0.450 dB` | `0.987` |
| `Denoiser` | 5s clip | `9.153 dB` | `9.601 dB` | `-0.449 dB` | `0.999` |
| `Subtract` | 5s clip | `9.153 dB` | `10.061 dB` | `-0.908 dB` | `0.102` |

## Practical note

The harness is working as intended, but the heavier products do not all fit comfortably into one long full-corpus run inside the current desktop execution window. For day-to-day tuning:

- use the full corpus for `Leveler` and `Cleanup`
- use the short corpus for `Finish`, `OptoComp`, `Tone`, and `Proximity`
- use short clips for `Deverb`, `Denoiser`, and `Subtract`

That still gives a real, repeatable audio-check loop without having to rely only on synthetic regressions.
