Here’s the next phase spec.

This phase is not about making the current mask engine more clever. That path will keep sounding like moving EQ. The next phase is about changing the product from:

* bin-wise masked tonal balance shifting

into:

* object-aware foreground extraction with stable source ownership

Your current DSP already has the right base for this:

* short-window STFT
* linked stereo analysis
* harmonic peak and cluster detection
* per-source masks
* mask smoothing
* source persistence
* composite gain reconstruction
* a simple UI with five source controls plus strength

That gets you a prototype. It does not yet get you a product.

The reason it still feels like ineffective EQ is simple:

> the engine still mostly decides at the bin level, then applies gain to bins

That creates:

* spectral movement without object separation
* partial source ducking rather than true foreground lift
* little sense of extracted source ownership
* weak attack handling
* weak continuity on held notes
* no clear distinction between source presence and source loudness

So the next phase should be built around one idea:

# Phase goal

Build a DSP rebalance engine that behaves like it is controlling **musical objects**, not just frequency regions.

That means the engine must track:

* tonal objects over time
* transient objects over time
* likely source ownership of those objects
* confidence of ownership
* a foreground versus background decomposition that survives frame changes

---

# Phase name

## Rebalance Phase 2: Object-Based DSP Separation

---

# Product target

The product should no longer aim to be described internally as:

* heuristic stem rebalance
* source-like EQ
* spectral reweighting

It should aim to be:

* object-based source emphasis and suppression
* realtime source focus DSP
* voice, guitar, bass and drum foreground control

That framing matters, because the engine should now optimize for:

* separation feel
* source stability
* mix intelligibility
* attack/body continuity
* believable suppression

Not for:

* mask purity
* academic source estimates
* isolated stems

---

# Core problem to solve

The current engine can identify where energy likely belongs, but it does not yet maintain enough continuity for a listener to perceive:

* that the vocal is a single stable thing
* that the guitar is a single stable thing
* that attacks and sustains belong together
* that source gain changes are source-level rather than spectral-level

That is why it still reads as EQ.

The next phase fixes that by adding three layers the current system does not truly have:

1. cluster identity over time
2. separate tonal and transient object paths
3. foreground/background source rendering logic

---

# Architecture for the next phase

## 1. Split the engine into two object paths

Do not keep treating everything inside one unified mask path.

Create two explicit paths:

### A. Tonal object path

Responsible for:

* vocals sustained vowels
* guitar sustain and harmonic body
* bass tone
* tonal bleed in other harmonic sources

This path should be built around:

* harmonic clusters
* f0 estimation
* cluster continuity
* envelope shape
* stereo placement continuity

### B. Transient object path

Responsible for:

* drum attacks
* guitar pick attacks
* consonant edges in vocals
* fast broadband events

This path should be built around:

* spectral flux
* onset likelihood
* short-lived event linking across nearby bins
* transient family grouping

Then fuse the outputs of both paths into the final source mask field.

Right now transients are just influencing the same core mask logic. That is too weak. They need their own object path.

---

# 2. Add persistent cluster tracking across frames

This is the biggest missing piece.

Your current harmonic clusters are rebuilt every frame. That means they help local coherence, but they do not create identity.

You need a tracker.

## Add a new structure

```cpp
struct TrackedCluster {
    bool active = false;
    int id = -1;

    float estimatedF0Hz = 0.0f;
    float meanHz = 0.0f;
    float meanMagnitude = 0.0f;

    float stereoCenter = 0.0f;
    float stereoWidth = 0.0f;

    float onsetStrength = 0.0f;
    float sustainStrength = 0.0f;

    int dominantSource = otherSource;
    std::array<float, kSourceCount> sourceProbabilities {};

    float confidence = 0.0f;
    float ageFrames = 0.0f;
    float inactiveFrames = 0.0f;

    std::array<int, kClusterHarmonics> lastMemberBins {};
    int lastMemberCount = 0;
};
```

## Add state

```cpp
static constexpr int kMaxTrackedClusters = 32;
std::array<TrackedCluster, kMaxTrackedClusters> trackedClusters {};
int nextTrackedClusterId = 1;
```

## Matching logic per frame

For each new harmonic cluster:

* estimate cluster center
* estimate f0
* estimate width/center/magnitude
* match to an existing tracked cluster using cost based on:

  * f0 distance
  * average bin distance
  * stereo similarity
  * magnitude ratio
  * recent activity

If match is good enough:

* update tracked cluster
* preserve identity
* smooth source probabilities

If no match:

* create new tracked cluster

If tracked cluster not matched:

* decay it for a short lifetime instead of deleting immediately

## Why this matters

This is the step that makes the system feel like it is following a voice or a guitar note rather than repainting bins every frame.

Without this, the product will keep sounding like animated EQ.

---

# 3. Add actual f0 estimation for tonal objects

Right now you have peaks and harmonic grouping. That is not enough.

You need a lightweight pitch estimate per cluster.

## Goal

Not perfect pitch tracking.
Just stable enough f0 to:

* keep harmonic ownership coherent
* distinguish voice from guitar better
* link harmonics over time

## Approach

For each cluster:

* root candidate from lowest member
* evaluate harmonic spacing consistency
* compute weighted f0 estimate
* smooth f0 over time per tracked cluster

Add:

```cpp
float estimateClusterF0Hz(const HarmonicCluster& cluster) const noexcept;
```

## Use f0 for source separation

Voice and guitar overlap spectrally, but not always structurally.

Voice tends to have:

* more stable formant envelope around a moving f0
* more center bias
* different harmonic-to-envelope relationship

Guitar tends to have:

* stronger pick onset
* different decay behaviour
* less vocal-formant shaping
* often wider stereo behaviour or less center lock

Once you track f0 and decay over time, vocal/guitar arbitration stops being just midrange weighting.

---

# 4. Create a source probability model per tracked object

Right now source support is mostly per-bin and per-frame.

Move the core ownership decision to the object level.

For each tracked cluster, compute and smooth:

```cpp
sourceProbabilities[vocals]
sourceProbabilities[drums]
sourceProbabilities[bass]
sourceProbabilities[guitar]
sourceProbabilities[other]
```

These should be based on:

### vocals

* center bias
* formant support
* moderate sustain
* lower transient dominance
* speech presence context from host

### guitar

* harmonic body
* pick attack history
* moderate width or reduced center lock
* weaker vocal formant support
* sustained upper harmonic structure

### bass

* low f0
* strong low harmonic energy
* stable sustain
* low width

### drums

* strong onset path
* broadband attack pattern
* poor harmonic continuity
* repeated transient family behaviour

### other

* residual object when no source ownership is strong

Then smooth object source probabilities across frames. Per-object smoothing matters more than per-bin smoothing.

---

# 5. Build a transient object engine

This is the second big missing piece.

Right now transients are just a scalar prior. That is not enough to make drums or pick attacks feel separated.

## Add transient events

Create:

```cpp
struct TransientEvent {
    bool active = false;
    int id = -1;
    int birthFrame = 0;
    float peakHz = 0.0f;
    float bandwidthHz = 0.0f;
    float magnitude = 0.0f;
    float stereoWidth = 0.0f;
    float drumProbability = 0.0f;
    float guitarProbability = 0.0f;
    float vocalProbability = 0.0f;
};
```

## Detect transient objects using:

* positive spectral flux
* local broadband rise
* high-frequency burst pattern
* attack compactness over neighboring bins

## Track transient events for short lifetimes

About 20 to 80 ms worth of frames is enough.

## Use them to:

* reinforce drum ownership in attack bins
* reinforce guitar attack bins immediately before sustain takeover
* keep vocal consonant edges from being swallowed by drums

## Important rule

Transient objects should merge into tonal tracked clusters when appropriate.

For example:

* guitar pick transient should influence the following tonal cluster
* vocal consonant onset should influence the following vocal cluster
* drum hit should remain primarily transient-owned

This is one of the biggest perception upgrades in the entire phase.

---

# 6. Introduce foreground/background rendering

This is where it stops sounding like EQ.

The current product maps user controls into per-source spectral gain. That is necessary, but not sufficient.

You need an explicit foreground/background model.

## New internal concept

For each bin and frame:

* one or two dominant source owners become foreground candidates
* the rest become background candidates

Instead of only doing:

```cpp
sum(mask * sourceGain)
```

do:

```cpp
final = foregroundRender + backgroundRender + residualSafety
```

## Foreground render

For target-raised sources:

* preserve cluster coherence
* preserve body and attack linkage
* allow slightly stronger gain than raw weighted masking would

## Background render

For suppressed competing sources:

* attenuate more smoothly
* bias toward decorrelated or softened contribution, not just raw subtraction
* avoid hollowing artifacts

## Why this matters

Listeners do not hear “bin gain changed”.
They hear:

* the vocal came forward
* the guitar moved back
* the drums detached from the midrange

Foreground/background rendering is what creates that perception.

---

# 7. Add object-level rendering rules

This is where the product feel comes from.

For each tracked object, define rendering traits.

## Vocal object

When boosted:

* preserve formant core
* preserve intelligibility band
* preserve consonant front edge
* do not over-boost breath/noise tail

When cut:

* reduce formant core first
* leave some room tone and consonants
* avoid robotic holes

## Guitar object

When boosted:

* preserve pick-to-body continuity
* avoid over-brightening only the upper mids
* follow harmonic body, not just 1–3 kHz

When cut:

* reduce body and sustain, not only pick edge
* leave some residual attack to avoid unnatural hollowness

## Drum object

When boosted:

* keep attack compact
* preserve kick low-end and cymbal clarity separately
* avoid turning all broadband bursts into “drums”

When cut:

* suppress attack families
* leave some ambience

## Bass object

When boosted:

* favor continuity and low harmonic coherence
* avoid exciting unrelated low mid mud

When cut:

* preserve kick separation
* avoid low-end pumping

This rendering layer is where the current engine is weakest. Without it, the controls mostly behave like source-flavored EQ.

---

# 8. Add confidence-aware aggression

The system already computes separation confidence. Start using it as a top-level policy control.

## When confidence is high

Allow:

* stronger foreground lift
* stronger background suppression
* tighter source ownership
* reduced other bleed

## When confidence is low

Reduce:

* source-specific gain depth
* suppression sharpness
* object isolation strength

Increase:

* residual preservation
* background blend
* ambiguity tolerance

This prevents:

* overconfident wrong separation
* phasey artifacts
* weird holes in dense mixes

The product should feel reliable, not bold-but-wrong.

---

# 9. Rework the user control mapping

The current control model is technically fine, but the perceptual mapping is not strong enough for product feel.

Users want to hear:

* “bring vocal forward”
* “push guitar back”
* “pull drums out”
* “clear bass”
  not:
* “apply 6 dB equivalent weighted mask lift”

## Keep the current controls

* Vocals
* Drums
* Bass
* Guitar
* Other
* Strength

## But change what they do internally

Each source control should drive three things:

1. object gain target
2. object foreground priority
3. competitor suppression bias

Example:

### vocal up

* increase vocal object render gain
* increase vocal foreground priority
* mildly suppress guitar/other competition in shared regions

### guitar down

* reduce guitar object body and sustain
* suppress guitar attack-to-sustain linkage slightly
* do not over-cut raw upper mids globally

That makes the same simple UI behave like a source controller, not EQ.

---

# 10. Add explicit anti-EQ safeguards

Because that is exactly what the product currently feels like.

## Safeguard A: minimum object coherence

Do not apply a strong gain move to a source unless:

* object confidence exceeds threshold
* or object has enough age
* or enough harmonic or transient coherence exists

## Safeguard B: avoid isolated band moves

If only a narrow frequency slice is source-owned but the object is weak, cap gain move.

## Safeguard C: preserve attack/body coupling

If a guitar or vocal object is boosted or cut, keep the coupled bins moving together within a limit.

## Safeguard D: preserve ambience floor

Do not zero out all low-confidence residual. Maintain a small ambience floor to avoid holes.

These four rules alone will make the result feel far less like animated EQ.

---

# 11. Processing pipeline for Phase 2

Use this order.

## Per frame

1. STFT analysis
2. stereo feature extraction
3. spectral envelope build
4. onset / flux analysis
5. peak detection
6. harmonic cluster build
7. cluster f0 estimation
8. tracked cluster matching and update
9. transient event detection and update
10. object source probability update
11. object-level mask proposal
12. bin-level mask refinement using object ownership
13. confidence-aware mask moderation
14. source persistence
15. foreground/background render weighting
16. composite gain build
17. inverse STFT and OLA

That is the correct product architecture.

---

# 12. Data structures to add

## In `VxRebalanceDsp.h`

Add:

```cpp
static constexpr int kMaxTrackedClusters = 32;
static constexpr int kMaxTransientEvents = 32;
```

### tracked clusters

```cpp
std::array<TrackedCluster, kMaxTrackedClusters> trackedClusters {};
int nextTrackedClusterId = 1;
```

### transient events

```cpp
std::array<TransientEvent, kMaxTransientEvents> transientEvents {};
int nextTransientEventId = 1;
```

### optional ownership maps

```cpp
std::array<int, kBins> binOwningCluster {};
std::array<float, kBins> binOwnershipConfidence {};
```

These will let you shift from per-bin heuristics to object-guided mask shaping.

---

# 13. New functions to add

## Tracking

```cpp
void updateTrackedClusters(const std::array<float, kBins>& analysisMag,
                           const std::array<float, kBins>& centerWeight,
                           const std::array<float, kBins>& sideWeight);

float estimateClusterF0Hz(const HarmonicCluster& cluster) const noexcept;
int findBestTrackedClusterMatch(const HarmonicCluster& cluster) const noexcept;
void ageTrackedClusters() noexcept;
```

## Transients

```cpp
void detectTransientEvents(const std::array<float, kBins>& analysisMag,
                           const std::array<float, kBins>& centerWeight,
                           const std::array<float, kBins>& sideWeight,
                           float transientPrior);

void updateTransientEvents() noexcept;
```

## Source/object probabilities

```cpp
void updateObjectSourceProbabilities(const std::array<float, kBins>& analysisMag,
                                     const std::array<float, kBins>& centerWeight,
                                     const std::array<float, kBins>& sideWeight,
                                     float transientPrior,
                                     float steadyPriorScale);

void writeObjectOwnershipToBins() noexcept;
```

## Rendering

```cpp
void buildForegroundBackgroundRender(const std::array<float, kBins>& analysisMag) noexcept;
float computeSourceForegroundPriority(int source, float userControl, float confidence) const noexcept;
float computeSourceSuppressionBias(int source, float userControl, float confidence) const noexcept;
```

---

# 14. Testing criteria for product readiness

Do not evaluate Phase 2 by looking at masks. Evaluate it by listening and by task success.

## Task 1: vocal lift

On:

* studio vocal over guitar
* phone speech over acoustic guitar
* live camera audio

Success means:

* vocal feels more forward
* not just brighter
* not just hollowing the guitar

## Task 2: guitar reduction

Success means:

* guitar body falls back
* voice remains intact
* upper mids do not simply collapse

## Task 3: drum reduction

Success means:

* drum attacks recede
* mix does not feel blanket-dulled
* bass does not disappear with kick

## Task 4: bass control

Success means:

* bass moves independently from kick more often than now
* low mids do not pump badly

## Task 5: neutral mode

Success means:

* absolutely no audible weirdness
* latency compensation path feels clean

---

# 15. Performance constraints

This must still be a realtime plugin.

So Phase 2 should obey:

* no large model inference
* no heavy iterative optimisation
* no offline-only analysis assumptions
* keep tracked object counts bounded
* keep matching O(N²) small with hard limits
* avoid per-bin expensive searches if ownership can be cached per object

Use:

* fixed-size arrays where practical
* bounded object counts
* simple matching costs
* simple f0 heuristics, not full pitch search

---

# 16. UI and product positioning changes

This matters more than it sounds.

If the engine becomes object-based, the product should stop hinting at raw stems.

Change internal and help language from:

* stem level
* source stem level

to:

* source focus
* source presence
* bring forward / push back

Because what you are building is:

* not isolated stem extraction
* but a source-focused rebalance engine

That is a stronger and more honest product story.

---

# 17. Implementation order

Do this in this order.

## Phase 2A

Tracked harmonic clusters

* add tracked cluster state
* matching
* f0 estimate
* smoothed source probabilities

## Phase 2B

Transient object engine

* detect
* short tracking
* drum/guitar/vocal onset handling

## Phase 2C

Object-to-bin ownership

* write tracked ownership into bin priors
* reduce direct raw per-bin guessing

## Phase 2D

Foreground/background renderer

* source-aware gain build
* competitor suppression logic
* ambience floor handling

## Phase 2E

Perceptual tuning

* object render traits
* confidence moderation
* user control remapping

That order gets you product movement fastest.

---

# 18. Hard truth

If you stop before tracked clusters and transient objects, it will keep sounding like EQ.

You can tune band windows, mask exponents and bias coefficients all day. It will not fix the core problem.

The product starts to feel real when:

* sustained things stay owned across time
* attacks stay linked to their source
* user moves affect objects, not regions

That is the line between prototype DSP and a real product.

If you want, I’ll turn this into a clean markdown implementation brief you can hand straight to your agent.
