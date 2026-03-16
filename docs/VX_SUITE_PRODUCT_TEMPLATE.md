# VX Suite Product Template

Use this for every new plugin.

## Identity

- Product name:
- Short tag:
- Problem solved in one sentence:
- Primary outcome:
- Secondary outcome:
- Default mode:

## UX

- Knob 1:
- Knob 2:
- Knob 3 (only if truly justified):
- Shared `Listen` toggle needed?:
- Hidden advanced controls to avoid in v1:
- Mode status text:

## DSP Contract

- Inputs:
- Outputs:
- Latency:
- Mono/stereo policy:
- Proven lab/reference contract:
- Streaming reintegration rule:
- Failure-safe behavior:
- Analysis-only helpers allowed:
- Exposed audible helpers allowed:
- Hidden audible helpers forbidden:
- State that must never reset during playback:
- State that must reset on transport/silence:

## Core Effect Rules

- What is the proven core DSP path before wrappers?
- How is wet authority mapped from the hero control?
- What is the exact stereo re-entry contract if the core DSP is mono?
- What dry reference or delayed reference must stay latency-aligned?
- Which helpers are allowed to shape the result after the core DSP is already known-good?
- Which helpers are forbidden from reducing effect audibility?

## Verification

- bypass null / transparency
- core DSP only path changes audio audibly and measurably
- outcome-specific metric moves in the right direction
- mode-policy correctness (`Voice` vs `General`) if applicable
- `Listen` delta correctness if applicable
- latency-aligned dry/wet or safety comparison if applicable
- automation continuity
- prepare/reset stability
- sample-rate coverage
- large host block safety
- silence -> speech recovery

## Framework Hooks

- `ProductIdentity`
- `ProductIdentity.defaultMode`
- `ProductIdentity.listenParamId`
- parameter layout
- `vxsuite::ModePolicy`
- `ProcessorBase::currentModePolicy()`
- `ProcessorBase::renderListenOutput()`
- processor-local DSP core
- shared VX Suite editor shell
