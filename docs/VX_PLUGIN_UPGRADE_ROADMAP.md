# VX Plugin Upgrade Roadmap

## Purpose

This document captures the **meaningful** upgrade path for each shipped VXStudio plugin.

It is intentionally not a backlog of small tweaks. The focus is:

- changes that would materially improve results
- changes that would materially improve control feel
- changes that would materially improve product clarity

The suite already has a strong shared framework and several solid DSP foundations. The biggest remaining opportunities are less about adding more knobs and more about making each product's internal model match its user promise.

---

## Suite-wide themes

Before going plugin by plugin, a few upgrade themes show up repeatedly across the suite.

### 1. Move more products from RMS thinking to perceptual loudness thinking

Several processors still restore or protect output mainly by RMS-style compensation. That is better than raw gain matching, but it is not yet best practice for products that promise polished, loudness-stable outcomes.

Meaningful upgrade direction:

- prefer K-weighted / LUFS-adjacent control targets where practical
- use perceptual retention targets instead of broad RMS giveback
- make `0 -> 100` feel consistent in perceived effect, not just internal gain change

### 2. Reduce heuristic sprawl in the most complex products

`Cleanup`, `Leveler`, and `Rebalance` are all capable, but they are also carrying a lot of layered heuristics. More tuning alone will eventually give diminishing returns.

Meaningful upgrade direction:

- simplify the internal objective
- collapse overlapping detectors and corrective branches
- split multi-job engines where the product contract has become muddy

### 3. Expose confidence and intent more clearly

A recurring state-of-the-art gap is not just algorithm quality but user trust. Modern smart tools increasingly tell the user when they are confident, when they are backing off, and what kind of problem they think they are solving.

Meaningful upgrade direction:

- add lightweight confidence-aware behaviour
- surface when a product is protecting, restoring, or backing off
- avoid hidden compensation paths that make strong settings feel polite or inconsistent

### 4. Prefer better core models over endless retuning

Some products are nearing the limit of what their current algorithm class can deliver. In those cases, the right upgrade is architectural, not another round of parameter reshaping.

The main examples are:

- `Rebalance`
- `Denoiser`
- `Deverb`
- `Subtract`

---

## By plugin

## Studio Analyser

### Current strength

The analyser is already well integrated into the VX Suite stage-publishing model.

### Meaningful upgrade

Turn it from an internal telemetry viewer into a more semantically calibrated analysis tool.

What that means:

- stabilize the display contract around familiar units and cadences
- make comparisons read like a trusted audio tool, not debug output
- add interpretation layers that explain what the metrics imply for the user

Why this matters:

The value ceiling here is not prettier rendering. It is making the analyser legible enough that it helps users decide what to do next.

Avoid:

- adding visual density without first locking the semantic contract
- showing more internal metrics if they do not improve user decisions

---

## Cleanup

### Current strength

`Cleanup` already has a rich ingredient pool and can solve multiple classes of trouble in one pass.

### Meaningful upgrade

Simplify the corrective engine so the plugin behaves more like one deliberate product and less like many overlapping mini-processors.

What that means:

- reduce detector overlap between mud, harshness, breath, sibilance, plosive, and density paths
- unify the corrective objective around a smaller number of perceptual targets
- make confidence gating stronger so uncertain cases back off earlier

Why this matters:

The current architecture is powerful, but it is also heavy with heuristics. That makes tuning harder, increases the chance of contradictory moves, and makes the product harder to evolve.

Best next-step direction:

- reshape `Cleanup` around a smaller latent model of “trouble” plus clearer body/focus recovery

Avoid:

- adding more specialist sub-detectors unless a whole existing class is removed
- expanding the UI into a channel strip

---

## DeepFilterNet

### Current strength

This is currently the most state-of-the-art product in the suite because it already uses a real modern denoise model.

### Meaningful upgrade

Improve orchestration around the model rather than replacing the denoise core.

What that means:

- auto-select or auto-prefer the best model/backend for the current context
- adapt behaviour to latency budget and host conditions
- surface artifact confidence and use it to steer `Guard`

Why this matters:

The main remaining gap is not denoise quality in principle. It is making the ML engine feel more adaptive and more trustworthy in realtime use.

Best next-step direction:

- confidence-aware model/backoff logic
- smarter offline/realtime policy

Avoid:

- adding more exposed model complexity to the UI
- treating static model selection as the long-term UX

---

## Denoiser

### Current strength

The current denoiser is a strong classical spectral design with meaningful voice protection, transient handling, and smoothing.

### Meaningful upgrade

Add a hybrid confidence-aware residual stage rather than continuing to rely on classical suppression alone.

What that means:

- keep the current spectral engine as the main reduction stage
- add artifact-risk estimation or a learned residual cleanup stage
- improve stereo coherence with joint constraints instead of purely compensating afterward

Why this matters:

The current engine is close to the ceiling of what a classical OM-LSA-style design can do before artifact tradeoffs dominate.

Best next-step direction:

- hybrid DSP + learned residual suppression
- stronger artifact-confidence model

Avoid:

- endlessly increasing suppression complexity inside the same spectral framework
- solving audible artifacts only with louder speech preservation or more makeup

---

## Deverb

### Current strength

The current dereverb path is already stronger and more assertive than before, and it has sensible loudness and body recovery controls.

### Meaningful upgrade

Make the product more adaptive to actual room-tail evidence instead of mainly driving a static reduction law.

What that means:

- estimate tail confidence more explicitly
- separate early/direct content from late decay more clearly
- back off faster on material that is already dry or only ambiguously reverberant

Why this matters:

Modern dereverb is less about “subtract more” and more about “subtract only when the room evidence is real.”

Best next-step direction:

- adaptive tail-confidence control
- better early/late discrimination

Avoid:

- compensating uncertain dereverb only with louder body restore
- making the plugin more aggressive globally when the bigger need is selectivity

---

## Finish

### Current strength

`Finish` has a solid macro-compression concept and now feels much more audible than before.

### Meaningful upgrade

Upgrade the mastering/protection layer to modern loudness and peak-management standards.

What that means:

- replace the simple sample-peak limiter with a true-peak-aware short-lookahead limiter
- move recovery/makeup logic closer to perceptual loudness targets
- keep the single-knob macro feel while making level outcomes more trustworthy

Why this matters:

This is one of the clearest cases where the next product-class improvement is not more range but better output discipline.

Best next-step direction:

- true-peak aware finishing path
- perceptual loudness-aware makeup and recovery

Avoid:

- stacking more RMS-based auto recovery on top of the current limiter
- adding more user-facing mastering knobs

---

## Leveler

### Current strength

`Leveler` covers both voice-riding and mix-leveling scenarios and already has useful analysis-aware behaviour.

### Meaningful upgrade

Separate the product roles more cleanly.

What that means:

- split vocal riding logic from whole-mix leveling logic, either internally or as separate products
- move whole-mix control toward loudness-domain control rather than weighted instantaneous amplitude
- make offline and realtime behaviour share a cleaner common target model

Why this matters:

Right now the engine is trying to solve multiple different automation problems with one large heuristic controller. That makes tuning fragile and makes user expectation harder to satisfy.

Best next-step direction:

- clearer controller separation
- LUFS/K-weighted target design for mix leveling

Avoid:

- continuing to add case-specific guards inside a blended engine without addressing the core role overlap

---

## OptoComp

### Current strength

The shared opto compressor is now much stronger and more usable than before, especially in macro range.

### Meaningful upgrade

Modernize the finishing behaviour around it and make the tonal body model more perceptual.

What that means:

- inherit the same true-peak and loudness improvements recommended for `Finish`
- make the `Body` contour smarter than a simple low-shelf surrogate
- keep the friendly opto feel while improving consistency across different sources

Why this matters:

The compressor core is useful, but the product can still feel like a good internal building block rather than a fully finished character processor.

Best next-step direction:

- perceptual body/tone contour
- better peak/loudness finishing discipline

Avoid:

- turning `OptoComp` into a fully parameterized studio compressor
- adding complexity that undermines the simple macro contract

---

## Proximity

### Current strength

`Proximity` is no longer just a timid EQ trick. It now behaves more like a voiced close-mic contour.

### Meaningful upgrade

Make the distance illusion source-dependent rather than static.

What that means:

- react differently to already-boomy, already-bright, or already-sibilant material
- keep perceived loudness steadier as body/presence rise
- model more of the “distance impression” than just EQ balance

Why this matters:

Static curves can sound good, but state-of-the-art smart tone tools increasingly respond to program context rather than applying one fixed contour family to everything.

Best next-step direction:

- adaptive distance modeling
- loudness-invariant contour behaviour

Avoid:

- solving the next round of user feedback only by increasing bass or air ceilings further

---

## Rebalance

### Current strength

The current rebalance engine is sophisticated for a heuristic DSP design and now has much better slider feel than before.

### Meaningful upgrade

Pivot the source-inference layer toward learned or hybrid learned ownership.

What that means:

- use a lightweight learned model to estimate source ownership masks or priors
- keep the current DSP layer responsible for bounded remix behaviour, protection, and smoothing
- stop expecting heuristic ownership alone to deliver modern semantic separation expectations

Why this matters:

This is the plugin with the biggest gap between user expectation and current algorithm class. Users hear “rebalance instruments in a mix” and compare it mentally to modern source-aware tools.

Best next-step direction:

- lightweight ML-guided mask estimation
- DSP kept as the rendering and safety layer

Avoid:

- continuing to grow the heuristic ownership graph as the main long-term strategy
- presenting the product like a stem separator if it is still fundamentally a remix engine

See also:

- [VX_REBALANCE_V2_SPEC.md](./VX_REBALANCE_V2_SPEC.md)

---

## Subtract

### Current strength

`Subtract` is a solid profile-based subtraction tool with a clear learn/process model.

### Meaningful upgrade

Make profile handling more adaptive and resilient to mismatch.

What that means:

- support better profile aging, profile confidence, or multiple profile states
- detect mismatch more clearly and adjust subtraction behaviour accordingly
- reduce the dependence on one frozen learned profile for changing real-world noise

Why this matters:

Modern subtraction-style tools feel much smarter when they can recognize “this is no longer the same noise” instead of forcing the user back into a binary relearn cycle.

Best next-step direction:

- adaptive profile management
- stronger mismatch handling

Avoid:

- replacing the product identity with generic broadband denoising
- making the learn workflow more complicated without improving real mismatch behaviour

---

## Tone

### Current strength

`Tone` is structurally clean and already does its current job reliably.

### Meaningful upgrade

Move from static shelves toward perceptual tilt/body shaping.

What that means:

- make the control feel more loudness-invariant
- shape bass and treble in a more source-aware way
- preserve the simple two-control contract while making the result feel more “finished” than a plain shelf pair

Why this matters:

The current plugin is good for what it is, but it is closer to a clean utility than a standout modern smart-tone product.

Best next-step direction:

- perceptual tilt/body model
- smarter source-aware shelf behaviour

Avoid:

- adding more bands
- widening the dB range just to seem more powerful

---

## Suggested upgrade order

If the goal is product impact rather than equal attention, the best order is:

1. `Rebalance`
2. `Leveler`
3. `Finish` / `OptoComp`
4. `Cleanup`
5. `Denoiser`
6. `Deverb`
7. `Subtract`
8. `Proximity`
9. `Tone`
10. `DeepFilterNet`
11. `Studio Analyser`

This order reflects where the biggest product-class gains are likely to come from, not which plugins are currently “worst.”

---

## Final principle

The most important next move for VXStudio is to keep the suite outcome-led and simple on the surface while becoming more **intelligent**, more **perceptually consistent**, and more **honest about confidence** underneath.

That means:

- better core models where needed
- simpler internal objectives where heuristics have grown too dense
- stronger loudness and peak discipline
- fewer hidden compensation behaviours that make strong settings feel weaker than they should
