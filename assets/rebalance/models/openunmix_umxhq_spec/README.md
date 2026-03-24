# Open-Unmix HQ Spec Baseline

This folder contains the official `umxhq_spec` 4-stem Open-Unmix checkpoint bundle used as the VX Rebalance v2.0 baseline.

- Source: `sigsep/open-unmix-pytorch`
- Variant: `umxhq_spec`
- Targets: `vocals`, `drums`, `bass`, `other`
- STFT config:
  - sample rate `44100`
  - `nfft = 4096`
  - `nhop = 1024`
  - `nb_channels = 2`
  - hidden size `512`
  - bandwidth `16000`

Files:

- `vocals.pth`
- `drums.pth`
- `bass.pth`
- `other.pth`
- per-target `*.json` metadata
- `separator.json`

This is the **v2.0 model bundle**, not yet the final plugin runtime format. The current VX Rebalance code can detect that the bundle exists, but it still needs the dedicated inference/conversion path before masks are produced inside the plugin.
