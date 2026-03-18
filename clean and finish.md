What you need here is not just cleaner code, but a reassertion of intent across the two products. Right now Cleanup and Finish overlap because they both try to “fix” the signal. The separation should be sharper:

* **Cleanup** = subtractive, protective, stabilising
* **Finish** = additive control, leveling, polish

If you hold that line, everything else becomes simpler.

---

# 1. Product split (non-negotiable)

## Cleanup (Stage 1)

**Purpose**

* Remove problems
* Preserve important content
* Stabilise signal before anything musical happens

**Never does**

* Compression
* Loudness shaping
* Final tone enhancement

---

## Finish (Stage 2)

**Purpose**

* Control dynamics (opto compression)
* Add perceived density
* Set final level

**Never does**

* Noise removal
* Heavy corrective EQ
* Guess what cleanup removed

---

# 2. UI model (consistent across both)

Both plugins:

* Mode: `Vocal` / `General`
* Max 3 dials

### Cleanup dials

* Clean
* Preserve (or Body if you keep naming)
* (optional) Focus

### Finish dials

* Finish (Peak Reduction)
* Body (light enhancement only)
* Gain (optional)

---

# 3. CLEANUP — SPEC

## Concept

> “Remove problems while preserving intent”

It should behave like a smart subtractive processor.

---

## DSP stages

```text
Analysis → Problem Detection → Attenuation → Preserve Compensation
```

---

## Behaviour rules

### Vocal mode

* protect speech band (1–4 kHz)
* preserve consonants
* avoid over-cleaning breath/air

### General mode

* broader spectral cleanup
* less speech-specific protection

---

## Cleanup parameters

```cpp
struct CleanupParams
{
    float clean;        // 0..1
    float preserve;     // 0..1
    bool vocalMode;
};
```

---

## Cleanup DSP (simplified but correct)

### Header

```cpp
#pragma once
#include <JuceHeader.h>

class CleanupDSP
{
public:
    void prepare(double sampleRate, int channels);
    void reset();

    struct Params
    {
        float clean = 0.5f;
        float preserve = 0.5f;
        bool vocalMode = true;
    };

    void setParams(const Params& p);
    void process(juce::AudioBuffer<float>& buffer);

private:
    double sr = 48000.0;
    int ch = 2;

    float lowEnv = 0.0f;
    float highEnv = 0.0f;

    Params params;
};
```

---

### Implementation

```cpp
#include "CleanupDSP.h"

void CleanupDSP::prepare(double sampleRate, int channels)
{
    sr = sampleRate;
    ch = channels;
    reset();
}

void CleanupDSP::reset()
{
    lowEnv = 0.0f;
    highEnv = 0.0f;
}

void CleanupDSP::setParams(const Params& p)
{
    params = p;
}

void CleanupDSP::process(juce::AudioBuffer<float>& buffer)
{
    const int samples = buffer.getNumSamples();
    const int channels = buffer.getNumChannels();

    const float clean = juce::jlimit(0.0f, 1.0f, params.clean);
    const float preserve = juce::jlimit(0.0f, 1.0f, params.preserve);

    const float lowCoeff = std::exp(-1.0f / (0.02f * sr));
    const float highCoeff = std::exp(-1.0f / (0.005f * sr));

    for (int i = 0; i < samples; ++i)
    {
        float mono = 0.0f;
        for (int c = 0; c < channels; ++c)
            mono += buffer.getReadPointer(c)[i];
        mono /= (float)channels;

        float absMono = std::abs(mono);

        lowEnv = lowCoeff * lowEnv + (1.0f - lowCoeff) * absMono;
        highEnv = highCoeff * highEnv + (1.0f - highCoeff) * absMono;

        float noiseFloor = lowEnv * (params.vocalMode ? 0.6f : 0.8f);

        float gate = juce::jlimit(0.0f, 1.0f,
            (absMono - noiseFloor) / std::max(noiseFloor, 1e-6f));

        float attenuation = 1.0f - clean * (1.0f - gate);

        float preserveBoost = 1.0f + preserve * 0.3f * gate;

        float gain = attenuation * preserveBoost;

        for (int c = 0; c < channels; ++c)
            buffer.getWritePointer(c)[i] *= gain;
    }
}
```

---

# 4. FINISH — SPEC

## Concept

> “Level and polish what already exists”

---

## DSP stages

```text
Recovery-lite → Opto Compressor → Adaptive Makeup → Limiter → Trim
```

---

## Finish parameters

```cpp
struct FinishParams
{
    float finish;   // peak reduction
    float body;     // light enhancement
    float gain;     // output trim
    bool vocalMode;
};
```

---

# 5. Optical compressor (LA-2A style)

This is the core.

### Behaviour to emulate

* ~10 ms attack
* two-stage release
* program-dependent release
* soft knee
* peak reduction control

---

## Opto compressor code

### Header

```cpp
#pragma once
#include <JuceHeader.h>

class OptoCompressor
{
public:
    void prepare(double sampleRate, int channels);
    void reset();

    struct Params
    {
        float peakReduction = 0.5f;
        bool vocalMode = true;
    };

    void setParams(const Params& p);
    void process(juce::AudioBuffer<float>& buffer);

    float getGR() const { return gainReductionDb; }

private:
    double sr = 48000.0;
    int ch = 2;

    float env = 0.0f;
    float memory = 0.0f;
    float rms = 0.0f;

    float gainReductionDb = 0.0f;

    Params params;
};
```

---

### Implementation

```cpp
#include "OptoCompressor.h"

void OptoCompressor::prepare(double sampleRate, int channels)
{
    sr = sampleRate;
    ch = channels;
    reset();
}

void OptoCompressor::reset()
{
    env = 0.0f;
    memory = 0.0f;
    rms = 0.0f;
}

void OptoCompressor::setParams(const Params& p)
{
    params = p;
}

void OptoCompressor::process(juce::AudioBuffer<float>& buffer)
{
    const int samples = buffer.getNumSamples();
    const int channels = buffer.getNumChannels();

    float thresholdDb = juce::jmap(params.peakReduction, 0.0f, 1.0f, 6.0f, -36.0f);
    float ratio = params.vocalMode ? 3.5f : 3.0f;

    float attackA = std::exp(-1.0f / (0.01f * sr));
    float rmsA = std::exp(-1.0f / (0.03f * sr));

    float grSum = 0.0f;

    for (int i = 0; i < samples; ++i)
    {
        float mono = 0.0f;
        for (int c = 0; c < channels; ++c)
            mono += buffer.getReadPointer(c)[i];
        mono /= channels;

        rms = rmsA * rms + (1.0f - rmsA) * mono * mono;
        float level = std::sqrt(std::max(rms, 1e-12f));

        if (level > env)
        {
            env = attackA * env + (1.0f - attackA) * level;
            memory = std::min(1.0f, memory + 0.003f);
        }
        else
        {
            float slowMs = juce::jmap(memory, 0.0f, 1.0f, 0.5f, 3.0f);
            float relA = std::exp(-1.0f / (slowMs * sr));
            env = relA * env + (1.0f - relA) * level;
            memory *= 0.9995f;
        }

        float envDb = juce::Decibels::gainToDecibels(env, -120.0f);
        float over = envDb - thresholdDb;

        float grDb = 0.0f;
        if (over > 0.0f)
            grDb = over * (1.0f - 1.0f / ratio);

        float gain = juce::Decibels::decibelsToGain(-grDb);
        grSum += grDb;

        for (int c = 0; c < channels; ++c)
            buffer.getWritePointer(c)[i] *= gain;
    }

    gainReductionDb = grSum / samples;
}
```

---

# 6. Finish processor (simplified)

```cpp
void processFinish(juce::AudioBuffer<float>& buffer)
{
    // 1. light recovery (optional, subtle only)

    // 2. opto compression
    opto.setParams({ finish, vocalMode });
    opto.process(buffer);

    // 3. adaptive makeup (simple version)
    float makeupDb = opto.getGR() * 0.6f;
    buffer.applyGain(juce::Decibels::decibelsToGain(makeupDb));

    // 4. user gain
    float outDb = juce::jmap(gain, 0.0f, 1.0f, -6.0f, 6.0f);
    buffer.applyGain(juce::Decibels::decibelsToGain(outDb));

    // 5. limiter (keep simple)
    float ceiling = juce::Decibels::decibelsToGain(vocalMode ? -1.5f : -1.8f);
    float peak = buffer.getMagnitude(0, buffer.getNumSamples());

    if (peak > ceiling)
        buffer.applyGain(ceiling / peak);
}
```

---

# 7. Final architecture (clean and stable)

```text
[Cleanup]
  ↓
[Finish]
    - Opto Compressor (main)
    - Makeup
    - Gain
    - Limiter
```

No overlap. No confusion.

---

# 8. What this fixes

* Cleanup stops trying to “sound good”
* Finish stops trying to “fix problems”
* Compression becomes real, not implied
* Limiter stops doing the heavy lifting
* Behaviour becomes predictable and tunable

---

# 9. The key principle going forward

If you remember one thing:

> Cleanup decides what should exist
> Finish decides how it should feel

Right now your system mixes those. This spec separates them cleanly again.

---
Below is a **strict, implementation-ready specification** your agent can follow without interpretation.
It defines **architecture, rules, file layout, parameters, and exact DSP behaviour** for both `Cleanup` and `Finish`.

No design decisions are left open.

---

# 0. HARD RULES (must not be violated)

1. **Cleanup and Finish must not overlap responsibilities**
2. **Cleanup must not compress**
3. **Finish must not remove noise**
4. **Only Finish contains dynamics control**
5. **Max 3 user controls per plugin**
6. **All decision logic lives in Processor, not DSP**
7. **DSP modules must be stateless except for envelopes**
8. **No hidden gain stages outside defined chain**

---

# 1. FINAL CHAIN

```text
[Cleanup] → [Finish]
```

No cross-dependency. No feedback between plugins.

---

# 2. CLEANUP — FINAL SPEC

## 2.1 Purpose

* Remove noise and instability
* Preserve important signal
* Do NOT enhance, compress, or finalise sound

---

## 2.2 Controls

```cpp
clean   // 0..1 (amount of removal)
body    // 0..1 (preserve important signal)
mode    // vocal / general
```

---

## 2.3 Behaviour rules

### Vocal mode

* Protect midrange (speech)
* Avoid killing transients
* Preserve breath/air

### General mode

* More aggressive cleanup
* Less protection weighting

---

## 2.4 DSP chain

```text
Mono Analysis → Envelope → Noise Estimate → Attenuation → Preserve Boost
```

---

## 2.5 Required implementation

### File: `CleanupDSP.h`

```cpp
#pragma once
#include <JuceHeader.h>

class CleanupDSP
{
public:
    void prepare(double sampleRate, int channels);
    void reset();

    struct Params
    {
        float clean = 0.5f;
        float body = 0.5f;
        bool vocalMode = true;
    };

    void setParams(const Params& p);
    void process(juce::AudioBuffer<float>& buffer);

private:
    double sr = 48000.0;
    int ch = 2;

    float slowEnv = 0.0f;
    float fastEnv = 0.0f;

    Params params;
};
```

---

### File: `CleanupDSP.cpp`

```cpp
#include "CleanupDSP.h"

void CleanupDSP::prepare(double sampleRate, int channels)
{
    sr = sampleRate;
    ch = channels;
    reset();
}

void CleanupDSP::reset()
{
    slowEnv = 0.0f;
    fastEnv = 0.0f;
}

void CleanupDSP::setParams(const Params& p)
{
    params = p;
}

void CleanupDSP::process(juce::AudioBuffer<float>& buffer)
{
    const int samples = buffer.getNumSamples();
    const int channels = buffer.getNumChannels();

    const float clean = juce::jlimit(0.0f, 1.0f, params.clean);
    const float body  = juce::jlimit(0.0f, 1.0f, params.body);

    const float slowA = std::exp(-1.0f / (0.03f * sr));
    const float fastA = std::exp(-1.0f / (0.005f * sr));

    for (int i = 0; i < samples; ++i)
    {
        float mono = 0.0f;
        for (int c = 0; c < channels; ++c)
            mono += buffer.getReadPointer(c)[i];
        mono /= channels;

        float absMono = std::abs(mono);

        slowEnv = slowA * slowEnv + (1.0f - slowA) * absMono;
        fastEnv = fastA * fastEnv + (1.0f - fastA) * absMono;

        float noiseFloor = slowEnv * (params.vocalMode ? 0.6f : 0.8f);

        float gate = (absMono - noiseFloor) / (noiseFloor + 1e-6f);
        gate = juce::jlimit(0.0f, 1.0f, gate);

        float attenuation = 1.0f - clean * (1.0f - gate);

        float preserveBoost = 1.0f + body * 0.25f * gate;

        float gain = attenuation * preserveBoost;

        for (int c = 0; c < channels; ++c)
            buffer.getWritePointer(c)[i] *= gain;
    }
}
```

---

# 3. FINISH — FINAL SPEC

## 3.1 Purpose

* Control dynamics (primary)
* Add density
* Set final level

---

## 3.2 Controls

```cpp
finish  // 0..1 (peak reduction / compression amount)
body    // 0..1 (light enhancement)
gain    // 0..1 (output trim) OPTIONAL
mode    // vocal / general
```

---

## 3.3 DSP chain (MANDATORY)

```text
[Opto Compressor] → [Adaptive Makeup] → [User Gain] → [Limiter]
```

---

# 4. OPTO COMPRESSOR — FINAL SPEC

## 4.1 Required behaviour

* Attack ≈ 10 ms
* RMS-style detection
* Program-dependent release
* Two-stage release (fast + slow)
* Memory increases with compression depth
* Soft knee
* Stereo linked

---

## 4.2 File: `OptoCompressor.h`

```cpp
#pragma once
#include <JuceHeader.h>

class OptoCompressor
{
public:
    void prepare(double sampleRate, int channels);
    void reset();

    struct Params
    {
        float peakReduction = 0.5f;
        bool vocalMode = true;
    };

    void setParams(const Params& p);
    void process(juce::AudioBuffer<float>& buffer);

    float getGR() const { return gainReductionDb; }

private:
    double sr = 48000.0;
    int ch = 2;

    float env = 0.0f;
    float memory = 0.0f;
    float rms = 0.0f;

    float gainReductionDb = 0.0f;

    Params params;
};
```

---

## 4.3 File: `OptoCompressor.cpp`

```cpp
#include "OptoCompressor.h"

void OptoCompressor::prepare(double sampleRate, int channels)
{
    sr = sampleRate;
    ch = channels;
    reset();
}

void OptoCompressor::reset()
{
    env = 0.0f;
    memory = 0.0f;
    rms = 0.0f;
}

void OptoCompressor::setParams(const Params& p)
{
    params = p;
}

void OptoCompressor::process(juce::AudioBuffer<float>& buffer)
{
    const int samples = buffer.getNumSamples();
    const int channels = buffer.getNumChannels();

    float thresholdDb = juce::jmap(params.peakReduction, 0.0f, 1.0f, 6.0f, -36.0f);
    float ratio = params.vocalMode ? 3.5f : 3.0f;

    float attackA = std::exp(-1.0f / (0.01f * sr));
    float rmsA = std::exp(-1.0f / (0.03f * sr));

    float grSum = 0.0f;

    for (int i = 0; i < samples; ++i)
    {
        float mono = 0.0f;
        for (int c = 0; c < channels; ++c)
            mono += buffer.getReadPointer(c)[i];
        mono /= channels;

        rms = rmsA * rms + (1.0f - rmsA) * mono * mono;
        float level = std::sqrt(std::max(rms, 1e-12f));

        if (level > env)
        {
            env = attackA * env + (1.0f - attackA) * level;
            memory = std::min(1.0f, memory + 0.003f);
        }
        else
        {
            float slowMs = juce::jmap(memory, 0.0f, 1.0f, 0.5f, 3.0f);
            float relA = std::exp(-1.0f / (slowMs * sr));
            env = relA * env + (1.0f - relA) * level;
            memory *= 0.9995f;
        }

        float envDb = juce::Decibels::gainToDecibels(env, -120.0f);
        float over = envDb - thresholdDb;

        float grDb = 0.0f;
        if (over > 0.0f)
            grDb = over * (1.0f - 1.0f / ratio);

        float gain = juce::Decibels::decibelsToGain(-grDb);
        grSum += grDb;

        for (int c = 0; c < channels; ++c)
            buffer.getWritePointer(c)[i] *= gain;
    }

    gainReductionDb = grSum / samples;
}
```

---

# 5. FINISH PROCESSOR LOGIC

## REQUIRED ORDER

```cpp
opto.process(buffer);

float makeupDb = opto.getGR() * 0.6f;
buffer.applyGain(dbToGain(makeupDb));

applyUserGain();

applyLimiter();
```

---

## Limiter (simple + required)

```cpp
float ceiling = vocalMode ? -1.5f : -1.8f;
float lin = juce::Decibels::decibelsToGain(ceiling);

float peak = buffer.getMagnitude(0, buffer.getNumSamples());
if (peak > lin)
    buffer.applyGain(lin / peak);
```

---

# 6. WHAT MUST NOT BE ADDED

Agent must NOT:

* add EQ in Finish beyond subtle recovery
* add denoise in Finish
* add compression in Cleanup
* add extra hidden stages
* create dual-mono compression
* add lookahead
* add multiband behaviour

---

# 7. ACCEPTANCE CHECKLIST

Agent must verify:

* Cleanup reduces noise but does not change loudness behaviour
* Finish adds compression without removing content
* Gain remains stable when toggling finish
* Limiter only catches peaks
* No pumping or instability
* No denormals
* No clipping

---

# 8. FINAL MENTAL MODEL (for agent)

```text
Cleanup = subtract safely
Finish  = control and polish
```

If any code violates that, it must be rejected.

---

If you want the next step, I can:

* map this exactly into your existing `VxCleanup` and `VxFinish` files
* preserve your analysis system while enforcing this separation

That’s where this becomes drop-in rather than spec.
