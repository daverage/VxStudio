# VX Polish Product Brief

## Job

`VX Polish` is a focused voice-finishing plugin that removes common tonal trouble and helps cleaned speech sit naturally without exposing a full restoration channel strip.

## User Contract

Three controls:

- `Detrouble`
- `Body`
- `Focus`

Optional shared controls:

- `Vocal` / `General`
- `Listen`

## Control Meaning

### `Detrouble`

Main macro. Reduces the most common voice problems in an outcome-led way:

- boomy
- boxy
- muddy
- honky
- harsh
- sizzle

Internally this may drive multiple corrective stages together, but the user should experience it as one clear "make this less annoying" control.

### `Body`

Restores low and low-mid weight after corrective smoothing so the result does not become thin, papery, or weak.

This is an exposed helper, not a hidden compensation path.

### `Focus`

Steers where the correction leans:

- lower values bias toward low and low-mid cleanup
- higher values bias toward upper-mid and top-end smoothing

This should remain a broad steering control, not a multi-band editor.

## Shared Listen Mode

`Listen` should audition the removed delta:

- default contract: `input - output`
- latency-bearing products must use a latency-aligned reference before subtraction

For `VX Polish`, `Listen` should let the user hear what tonal trouble is being removed. This is useful because the product is intentionally broad and program-dependent.

## Mode Policy

`Vocal` and `General` should be shared framework behaviors, not product-specific UI inventions.

For `VX Polish`:

- `Vocal`: tighter speech-safe thresholds, gentler upper-mid action, conservative dynamics
- `General`: broader full-range cleanup and slightly less speech protection bias

## Internal DSP Direction

The current `PolishChain` is the right ingredient pool, but the standalone product should stay narrower than the full chain.

Likely v1 internals:

- trouble-band smoothing / de-ess style correction
- low-mid cleanup / anti-mud shaping
- light plosive softening only when clearly needed
- restrained dynamics control
- body restore
- output safety limiting

Avoid in v1:

- exposing separate `de-ess`, `de-mud`, `plosive`, `compress`, `limit` knobs
- turning the plugin into a generic vocal channel strip
- heavyweight visual analysis that distracts from the single-job workflow

## UX Direction

The front-end should feel more like a smart trouble-removal tool than an EQ:

- one hero knob for `Detrouble`
- two supporting controls for `Body` and `Focus`
- optional `Listen` toggle
- optional passive zone indicators if they directly help the user understand where correction is happening

## Verification

Minimum checks:

- `Detrouble=0` behaves transparently
- increasing `Detrouble` produces measurable corrective delta on troublesome material
- `Listen` reliably outputs removed content, not unrelated dry/wet timing error
- `Body` restores weight without broadly undoing correction
- `Vocal` remains more speech-safe than `General` if mode support is enabled
