# VX Suite Control Semantics

Shared controls should mean the same kind of thing across the suite.

## `Body`
- Means preserve or restore useful weight.
- In corrective products, `Body` reduces over-thinning while cleanup rises.
- In finishing products, `Body` means tasteful recovery, not mud reintroduction.

## `Gain`
- Means output finish intent, not blind makeup.
- In `VXFinish`, `Gain` targets louder / more produced output while respecting clarity and limiter safety.
- Do not use `Gain` as a hidden denoise or corrective intensity control.

## `Guard`
- Means protection against artifacts or speech damage.
- In denoise / subtract products, higher `Guard` should preserve harmonics, consonants, and transients.
- `Guard` should not work by heavy dry reblending unless there is no safer option.

## `Listen`
- Corrective/removal products should audition what was removed.
- Additive/finishing products should audition what was added or changed.

## Review rule
- Before shipping a new product or changing a shared control mapping, check that the user-facing meaning still matches this document.
