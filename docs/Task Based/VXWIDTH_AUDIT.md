I reviewed the current processor and the supporting DSP files. The codebase is substantially cleaner than it was, and the control-ownership work is heading in the right direction. However, I found **two architectural issues I would fix before calling the DSP finished**, plus several smaller clean-ups.

## 1. High priority: the new ADT “equal-power spatial placement” maths is not actually panning A and B apart

This is the biggest finding.

The new code defines a separation value and then crossfades between the generated Mid and Side components:

```cpp
adtMidWeight  = cos(separation * pi/2);
adtSideWeight = sin(separation * pi/2);

doubleMid  = adtMidWeight  * 0.5 * (A + B);
doubleSide = adtSideWeight * 0.5 * (A - B);
```

 

The intention is good, but mathematically this does something different from moving A and B apart.

At `separation = 0`:

```text
M = (A+B)/2
S = 0
```

That correctly centres the pair.

But at `separation = 1`:

```text
M = 0
S = (A-B)/2
```

which reconstructs approximately:

```text
L = +(A-B)
R = -(A-B)
```

That is a **pure Side / anti-phase difference signal**.

It is *not*:

```text
L = A
R = B
```

which is what “A and B hard-separated left/right” should mean.

The comment saying separation 1 reproduces the previous `0.5/0.5` behaviour is therefore incorrect.  The previous system had **both**:

```text
M = 0.5(A+B)
S = 0.5(A-B)
```

at the same time.

That is exactly what you need to reconstruct separate A/B channels.

### What it should do

I would implement the spatial placement as **actual equal-power panning of the two performances**.

Conceptually, for separation `s`:

```text
A moves Centre -> Left
B moves Centre -> Right
```

with equal-power pan gains.

Something roughly like:

```cpp
theta = separation * pi/4;

nearGain = cos(theta);
farGain  = sin(theta);

doubleL = A * nearGain + B * farGain;
doubleR = A * farGain  + B * nearGain;
```

with the gains/normalisation adjusted for your desired energy convention.

Then:

```text
s = 0
A and B both centre

s = 1
A hard left
B hard right
```

If you want to stay in M/S internally, derive the equivalent M/S coefficients from those pan gains. The important point is:

> **As separation increases, Side should grow relative to Mid. Mid should not fall all the way to zero.**

Hard-separated A/B requires both Mid and Side.

This may sound good currently because an anti-phase-ish generated layer creates an enormous perception of width, but it is not the spatial model we designed and will be much less mono-friendly than proper A/B placement.

I would fix this first.

---

## 2. High priority: the new 18 dB Width authority is not actually being checked by the guardrail you think is checking it

The justification for increasing the direct Side ceiling to 18 dB says the PhaseRiskGuardrail reported identical safety readings at 9/12/15/18 dB. 

Having now reviewed the guardrail closely, **that result is expected even if the additional Side expansion creates new correlation problems**.

The “per-band coherence” calculation only analyses the **input L/R**:

```cpp
bl = splitterL.process(inL);
br = splitterR.process(inR);

corr = correlation(bl, br);
```



And the source confirms explicitly:

> `"coherence by perceptual band", on the INPUT`



The sustained negative-correlation tracker also receives the original input broadband correlation, not processed-output correlation. 

The two output-aware checks are:

* mono downmix spectral change;
* L/R energy centroid displacement.

But direct symmetric Side expansion can leave **both of those essentially unchanged** while driving the processed L/R relationship toward negative correlation.

Consider:

```text
L = M + kS
R = M - kS
```

As `k` gets very large:

* L/R correlation can move strongly negative;
* `L+R` remains `2M`;
* L/R energy can remain perfectly balanced.

So the guardrail may say “nothing has become more dangerous” precisely because **none of its output-aware measurements is measuring the danger created by large direct Side gain**.

### I would add

For each guardrail path, calculate processed-output correlation/coherence by the same three bands.

Then consider:

```text
input coherence
versus
hypothetical processed-output coherence
```

The safety question should be:

> Did our processing introduce materially worse phase/coherence behaviour?

That would make the 18 dB experiment meaningful.

I would **not automatically put the ceiling back to 12 dB**. The philosophy of giving the Width knob authority is right. But I would fix this blind spot and then rerun 12/15/18 dB.

If 18 dB still passes, I'm much more comfortable with it.

---

## 3. Medium-high: the loudness compensator appears to be solving the wrong feedback equation

`compGain` is already applied when the output RMS is accumulated:

```cpp
outL = (...) * compGain;
outR = (...) * compGain;

outEnergySum += outL² + outR²;
```



That compensated output RMS is then passed into:

```cpp
loudnessCompensator.updateForNextBlock(inRms, outRms, ...);
```



But `LoudnessCompensator` says it expects **pre-compensation output RMS** and calculates:

```cpp
targetGain = inputRms / outputRms;
```



Those are inconsistent.

If the raw effect increases amplitude by factor `E`, and current compensation is `C`, you're measuring:

```text
output = E × C
```

then asking for:

```text
target C = 1 / (E × C)
```

At equilibrium:

```text
C = 1 / (E × C)

C² = 1/E

C = 1/sqrt(E)
```

rather than the intended:

```text
C = 1/E
```

So in steady state it only compensates approximately **half the level difference in dB**.

Two straightforward solutions:

```text
A. Measure output energy before compGain
```

or, using the currently measured output:

```text
desiredCompGain =
    currentCompGain * inputRms / measuredOutputRms
```

Then apply your existing 0.70–1.20 bounds and smoothing.

This is worth fixing because your recent philosophy explicitly separates **effect strength** from **loudness management**. A correct compensator makes listening comparisons much more trustworthy.

---

## 4. Related: `compGain` creates a hidden cross-coupling in the safety measurements

This is subtle but relevant to the control-ownership work.

The comments correctly note that Side cancels from:

```text
L + R
```

and therefore Width Side processing cannot directly affect the mono-downmix guardrail.

However the actual output is:

```cpp
outL = (...) * compGain;
outR = (...) * compGain;
```



And `compGain` is derived from the **combined Width + Double output energy**.

Therefore:

```text
more Width Side
   ↓
higher overall RMS
   ↓
loudness compensator lowers compGain
   ↓
L+R falls
   ↓
MonoDownmixGuardrail sees a reduction
   ↓
DoubleMid gets restrained
```

So Width can still indirectly alter Double through the shared loudness compensator.

Similarly, the separate PhaseRiskGuardrail Width/Double instances are fed hypothetical outputs which already contain the same global `compGain`.

I would make the **safety analysis pre-loudness-compensation**.

Think of the order as:

```text
DSP geometry
    ↓
evaluate whether DSP geometry is safe
    ↓
final loudness compensation
    ↓
output trim
```

Loudness correction shouldn't masquerade as phase/mono damage.

That would make the Width/Double separation even cleaner.

---

## 5. Medium: Tightness doesn't currently appear to control the ADT spectral variation as documented

The ADT implementation correctly makes delay range, Doppler budget and gain variation dependent on Tightness. 

But the spectral tilt comment says:

> `+/-0.2..1.0dB`

whereas the actual calculation is simply:

```cpp
tiltDb = tiltWalker.process(); // -1..+1
```

with no Tightness scaling. 

So Tightness currently controls:

* delay range ✓
* delay velocity/Doppler ✓
* gain variation ✓
* spectral independence **apparently no**
* dedicated micro-pitch independence **apparently no**

That isn't necessarily audibly wrong, but it doesn't fully meet the conceptual model:

> Tightness = how independently the other performance behaves.

I'd consider:

```cpp
tiltRangeDb = map(tightnessInternal, 0.2f, 1.0f);
tiltDb = tiltWalker.process() * tiltRangeDb;
```

Micro-pitch could similarly have a restrained Tight→Loose scaling if listening supports it.

This is secondary; don't change it just for conceptual purity if current Tightness already sounds excellent.

---

## 6. Low: the micro-pitch crossfade comment is mathematically wrong

The micro-pitch implementation says two triangular windows whose gains add to one are:

> “constant-power by construction”



They're **constant-sum / constant-amplitude for identical coherent signals**, not constant-power.

For example:

```text
0.5² + 0.5² = 0.5
```

not 1.

I don't necessarily recommend changing the algorithm. For crossfading two related taps, a constant-sum window can actually be desirable.

Just fix the terminology so somebody doesn't “correct” it later based on a false premise.

---

## 7. Documentation is now lagging the actual architecture

There are several obvious stale comments.

The processor header still says the H/R/T decomposition and full phase guardrail are “later refinements, not built here”, despite both now being implemented. 

The public Focus hint still says:

> “Where in the spectrum the **width and doubling effect** concentrates”



and a processor comment still says Focus shapes the “wet paths” including decorrelation, despite the actual architecture correctly applying it only to the ADT input. 

That one matters because the code has now deliberately established:

```text
Focus = Double only
```

The ADT header also still calls micro-pitch experimental/off-by-default while the processor explicitly re-enables it on every reset as a permanent always-on feature.  

I'd do a documentation/comment sweep after the DSP corrections.

---

# What I think is already strong

There is a lot here I would **not** disturb.

The separation of Width and Double adaptive/safety paths is good architecture. You've avoided the earlier shared-state problem rather than hiding it behind thresholds.

Removing the centre-confidence Double gate was also reasonable. Double amount is now directly owned by the Double control, with genuine safety systems acting independently rather than an arbitrary source-suitability multiplier. 

The target-seeking Width architecture is also still the right model. The move to 18 dB isn't itself what worries me; the issue is that the safety validation used to justify it currently has a blind spot.

Likewise the harmonic/residual analyser remains appropriately lightweight rather than turning the plugin into an FFT architecture unnecessarily. Its autocorrelation detector is deliberately limited to roughly 70–600 Hz and should continue to be viewed as a programme-aware tonality estimator rather than perfect decomposition. 

And structurally, the code is surprisingly readable considering how much DSP behaviour has accumulated. Most of the complexity has at least been separated into purpose-specific classes rather than one giant pile of processor conditionals.

## My priority order

I would **not add any more features now**. I would do four corrective jobs:

1. **Replace the ADT Mid↔Side crossfade with genuine A/B spatial panning.**
2. **Make PhaseRiskGuardrail measure processed-output per-band correlation/coherence, then revalidate 18 dB.**
3. **Fix loudness compensation so it measures pre-comp output or accounts for current `compGain`, and keep safety measurements pre-compensation.**
4. **Clean stale comments/help text.**

Then listen again.

The first item is the only one where I think the current implementation is fundamentally doing something different from the intended design. The second and third are more dangerous because the plugin can sound excellent while the safety/measurement logic is telling you something slightly different from what it is actually measuring.

After those are corrected and the real listening still says it sounds good, I think you are getting very close to a point where **further DSP work is more likely to make VX Width worse rather than better**.
