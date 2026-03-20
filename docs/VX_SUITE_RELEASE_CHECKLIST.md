# VX Suite Release Checklist

## Framework
- Verify every latency-bearing processor reports latency through the shared framework path.
- Verify Listen mode semantics are correct:
  - Cleanup / Denoiser / Subtract / Deverb / DeepFilterNet: removed-content audition
  - Proximity / Finish: additive delta audition
- Verify shared smoothing and parameter semantics remain consistent across products.

## Host compatibility
- Test in a DAW at `44.1`, `48`, and `96 kHz`.
- Test at `64`, `128`, `256`, and `512` sample buffers.
- Test mono and stereo tracks.
- Test transport stop/start and loop playback.
- Test reset behavior after playback stop.
- Test plugin state save/restore inside a project.
- Test reopen-session state restore.
- Test sample-rate changes between sessions.
- Test offline bounce versus realtime playback.

## Packaging and release
- Run `tools/release/release_preflight.sh` before release signing; it checks `pluginval`, signing identities, notary profile access, and DeepFilterNet bundle resources.
- If the machine does not already have a notary keychain profile, create one with `tools/release/store_notary_profile.sh <profile-name>` and export `APPLE_NOTARY_PROFILE=<profile-name>`.
- Run `tools/release/sign_and_notarize_vst3.sh` with `APPLE_DEVELOPER_IDENTITY` set for macOS release candidates.
- If notarizing, also set `APPLE_NOTARY_PROFILE` so the script submits, staples, and validates each staged `.vst3` bundle.
- Verify the staged DeepFilterNet bundle includes its model archives in `Contents/Resources/`.

## Chain behavior
- Validate recommended chains:
  - `DeepFilterNet -> Deverb -> Cleanup -> Finish`
  - `Subtract -> Cleanup -> Proximity -> Finish`
- Confirm later plugins do not reintroduce problems earlier plugins removed.
- Confirm chain output remains finite, bounded, and tonally coherent.
- Confirm Listen mode remains meaningful at each stage.

## CPU and realtime safety
- Run `VXSuiteProfile` and capture timings for `44.1`, `48`, `96 kHz` and `64/128/256/512` buffers.
- Confirm no allocations occur in steady-state audio-thread processing.
- Audit hot paths for hidden container growth, locks, or blocking calls.
- Confirm stacked use remains comfortably realtime on target hardware.

## Product-specific checks

### Cleanup
- Verify breath / sibilance / plosive / harshness classification remains stable across host buffers.
- Verify high-pass and high-shelf toggles work and the status text remains readable.

### Finish
- Verify Finish acts as density / recovery / loudness only.
- Verify Listen mode outputs finish delta, not removed-content inversion.
- Verify compressor and limiter each engage sensibly and independently.

### Subtract
- Verify Learn progress is monotonic and confidence is sensible.
- Verify learned profile survives save/restore and playback stops.

## UI
- Verify all labels and hints are fully visible at default size.
- Verify minimum readable text stays around `10–12 pt` equivalent.
- Verify window sizes accommodate the longest status and hint text.
- Verify controls remain usable at the minimum and default window sizes.
