#pragma once

// Procedural, deterministic test corpus for the VX Width ADT-seed /
// decorrelator-tap offline optimiser (2026-08-07 imbalance investigation).
// Every generator here is a closed-form or seeded-RNG synthesis - no
// externally sourced audio, no network fetch, nothing committed with
// unclear licensing. Reproducible byte-for-byte given (sampleRate, seconds,
// variantSeed). All buffers are TRUE MONO (L=R) by construction, matching
// how the existing L/R-imbalance regression test isolates the mechanism
// under test (generated Side content correlating with Mid) - a buffer that
// already has independent L/R content would confound that measurement.
//
// Categories cover the corpus brief agreed with the user: male/female
// speech, sung vocal, clean/distorted guitar, piano, synth pad,
// bass-heavy, drums/transients, pink noise, multitone/sine sweep - plus
// real sung-vocal anchors loaded separately from data/vxtune/ (see
// loadRealVocalAnchors() below).

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>
#include <random>
#include <string>
#include <vector>

namespace vxsuite::width::corpus {

struct CorpusItem {
    std::string label;
    juce::AudioBuffer<float> audio;
    double sampleRate = 48000.0;
};

namespace detail {

inline void makeTrueMono(juce::AudioBuffer<float>& buf) {
    for (int i = 0; i < buf.getNumSamples(); ++i)
        buf.setSample(1, i, buf.getSample(0, i));
}

// Simple Karplus-Strong plucked string: noise-seeded delay loop with a
// one-pole averaging filter in the feedback path (decay + damping).
// `drive` > 0 applies tanh soft-clipping post-synthesis (guitar distortion).
inline void plucked(juce::AudioBuffer<float>& buf, const double sr, const float freqHz,
                     const int startSample, const int lengthSamples, const float decay,
                     std::mt19937& rng, const float drive = 0.0f) {
    const int period = std::max(2, static_cast<int>(sr / std::max(20.0f, freqHz)));
    std::vector<float> ring(static_cast<size_t>(period), 0.0f);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    for (auto& v : ring) v = dist(rng);
    int pos = 0;
    float prev = 0.0f;
    const int n = buf.getNumSamples();
    for (int i = 0; i < lengthSamples && (startSample + i) < n; ++i) {
        float v = ring[static_cast<size_t>(pos)];
        const float filtered = decay * 0.5f * (v + prev);
        prev = v;
        ring[static_cast<size_t>(pos)] = filtered;
        pos = (pos + 1) % period;
        float sample = v;
        if (drive > 0.0f)
            sample = std::tanh(sample * (1.0f + drive * 6.0f));
        buf.setSample(0, startSample + i, buf.getSample(0, startSample + i) + sample);
    }
}

} // namespace detail

// --- Speech-like (male/female differ by fundamental + formant register) ---
inline juce::AudioBuffer<float> makeSpeechVariant(const double sr, const float seconds,
                                                   const std::uint32_t seed,
                                                   const float fundamentalHz,
                                                   const float formant1, const float formant2) {
    const int n = static_cast<int>(sr * seconds);
    juce::AudioBuffer<float> buf(2, n);
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> jitter(-0.08f, 0.08f);
    const float f0 = fundamentalHz * (1.0f + jitter(rng));
    const float phraseRateHz = 2.4f + jitter(rng) * 5.0f;
    for (int i = 0; i < n; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(sr);
        float env = 0.5f + 0.35f * std::sin(2.0f * juce::MathConstants<float>::pi * phraseRateHz * t);
        env = juce::jlimit(0.05f, 1.0f, env);
        const float voiced = 0.40f * std::sin(2.0f * juce::MathConstants<float>::pi * f0 * t)
                           + 0.20f * std::sin(2.0f * juce::MathConstants<float>::pi * formant1 * t)
                           + 0.10f * std::sin(2.0f * juce::MathConstants<float>::pi * formant2 * t);
        const float sample = env * voiced;
        buf.setSample(0, i, sample);
    }
    detail::makeTrueMono(buf);
    return buf;
}

inline juce::AudioBuffer<float> makeMaleSpeech(const double sr, const float s, const std::uint32_t seed) {
    return makeSpeechVariant(sr, s, seed, 110.0f, 700.0f, 1900.0f);
}
inline juce::AudioBuffer<float> makeFemaleSpeech(const double sr, const float s, const std::uint32_t seed) {
    return makeSpeechVariant(sr, s, seed, 210.0f, 900.0f, 2600.0f);
}

// --- Sung vocal: sustained pitch with vibrato, legato envelope ---
inline juce::AudioBuffer<float> makeSungVocal(const double sr, const float seconds, const std::uint32_t seed) {
    const int n = static_cast<int>(sr * seconds);
    juce::AudioBuffer<float> buf(2, n);
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> pitchDist(180.0f, 320.0f);
    std::uniform_real_distribution<float> vibDepthDist(4.0f, 14.0f);
    const float baseHz = pitchDist(rng);
    const float vibRateHz = 5.2f;
    const float vibDepthCents = vibDepthDist(rng);
    for (int i = 0; i < n; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(sr);
        const float vibrato = vibDepthCents * std::sin(2.0f * juce::MathConstants<float>::pi * vibRateHz * t);
        const float hz = baseHz * std::pow(2.0f, vibrato / 1200.0f);
        const float env = juce::jlimit(0.0f, 1.0f, std::min(t / 0.15f, (seconds - t) / 0.4f));
        const float sample = env * (0.35f * std::sin(2.0f * juce::MathConstants<float>::pi * hz * t)
                                  + 0.15f * std::sin(2.0f * juce::MathConstants<float>::pi * hz * 2.0f * t)
                                  + 0.07f * std::sin(2.0f * juce::MathConstants<float>::pi * hz * 3.0f * t));
        buf.setSample(0, i, sample);
    }
    detail::makeTrueMono(buf);
    return buf;
}

// --- Guitar (clean/distorted): sequence of Karplus-Strong plucked notes ---
inline juce::AudioBuffer<float> makeGuitar(const double sr, const float seconds, const std::uint32_t seed,
                                            const bool distorted) {
    const int n = static_cast<int>(sr * seconds);
    juce::AudioBuffer<float> buf(2, n);
    std::mt19937 rng(seed);
    const float scale[] = { 82.4f, 110.0f, 146.8f, 196.0f, 246.9f, 329.6f }; // E A D G B E (open)
    std::uniform_int_distribution<int> noteIdx(0, 5);
    std::uniform_real_distribution<float> noteLenDist(0.35f, 0.9f);
    float tCursor = 0.0f;
    while (tCursor < seconds) {
        const float noteLen = noteLenDist(rng);
        const int startSample = static_cast<int>(tCursor * sr);
        const int lenSamples = static_cast<int>(noteLen * sr);
        detail::plucked(buf, sr, scale[static_cast<size_t>(noteIdx(rng))], startSample, lenSamples,
                         distorted ? 0.996f : 0.9985f, rng, distorted ? 0.7f : 0.0f);
        tCursor += noteLen * 0.8f;
    }
    detail::makeTrueMono(buf);
    return buf;
}

// --- Piano: additive decaying-harmonic notes, softer attack than guitar ---
inline juce::AudioBuffer<float> makePiano(const double sr, const float seconds, const std::uint32_t seed) {
    const int n = static_cast<int>(sr * seconds);
    juce::AudioBuffer<float> buf(2, n);
    std::mt19937 rng(seed);
    const float scale[] = { 130.8f, 164.8f, 196.0f, 220.0f, 261.6f, 329.6f, 392.0f };
    std::uniform_int_distribution<int> noteIdx(0, 6);
    std::uniform_real_distribution<float> noteLenDist(0.6f, 1.4f);
    float tCursor = 0.0f;
    while (tCursor < seconds) {
        const float noteLen = noteLenDist(rng);
        const float hz = scale[static_cast<size_t>(noteIdx(rng))];
        const int startSample = static_cast<int>(tCursor * sr);
        const int lenSamples = std::min(static_cast<int>(noteLen * sr * 1.6f), n - startSample);
        for (int i = 0; i < lenSamples; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(sr);
            const float decay = std::exp(-t * 2.2f);
            const float sample = decay * (0.30f * std::sin(2.0f * juce::MathConstants<float>::pi * hz * t)
                                        + 0.14f * std::sin(2.0f * juce::MathConstants<float>::pi * hz * 2.01f * t)
                                        + 0.06f * std::sin(2.0f * juce::MathConstants<float>::pi * hz * 4.02f * t));
            if (startSample + i < n)
                buf.setSample(0, startSample + i, buf.getSample(0, startSample + i) + sample);
        }
        tCursor += noteLen;
    }
    detail::makeTrueMono(buf);
    return buf;
}

// --- Synth pad: detuned multi-oscillator sustain with slow filter sweep ---
inline juce::AudioBuffer<float> makeSynthPad(const double sr, const float seconds, const std::uint32_t seed) {
    const int n = static_cast<int>(sr * seconds);
    juce::AudioBuffer<float> buf(2, n);
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> chordBaseDist(110.0f, 220.0f);
    const float base = chordBaseDist(rng);
    const float ratios[] = { 1.0f, 1.25f, 1.5f, 2.0f };
    float lp = 0.0f;
    for (int i = 0; i < n; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(sr);
        const float attack = juce::jlimit(0.0f, 1.0f, t / 0.8f);
        float sample = 0.0f;
        for (const auto r : ratios)
            sample += 0.12f * std::sin(2.0f * juce::MathConstants<float>::pi * base * r * t
                                        * (1.0f + 0.002f * std::sin(2.0f * juce::MathConstants<float>::pi * 0.3f * t)));
        const float cutoffMod = 0.5f + 0.5f * std::sin(2.0f * juce::MathConstants<float>::pi * 0.08f * t);
        const float alpha = 0.90f + 0.09f * cutoffMod;
        lp += (sample - lp) * (1.0f - alpha);
        buf.setSample(0, i, attack * lp);
    }
    detail::makeTrueMono(buf);
    return buf;
}

// --- Bass-heavy: low fundamental sine+saw with rhythmic gating ---
inline juce::AudioBuffer<float> makeBassHeavy(const double sr, const float seconds, const std::uint32_t seed) {
    const int n = static_cast<int>(sr * seconds);
    juce::AudioBuffer<float> buf(2, n);
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> hzDist(38.0f, 75.0f);
    const float hz = hzDist(rng);
    const float gateHz = 2.0f;
    for (int i = 0; i < n; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(sr);
        const float phase = std::fmod(hz * t, 1.0f);
        const float saw = 2.0f * phase - 1.0f;
        const float sine = std::sin(2.0f * juce::MathConstants<float>::pi * hz * t);
        const float gate = 0.5f + 0.5f * std::sin(2.0f * juce::MathConstants<float>::pi * gateHz * t
                                                    - juce::MathConstants<float>::halfPi);
        const float gateShaped = std::pow(juce::jlimit(0.0f, 1.0f, gate), 0.4f);
        buf.setSample(0, i, gateShaped * (0.5f * sine + 0.25f * saw));
    }
    detail::makeTrueMono(buf);
    return buf;
}

// --- Drums/transients: kick/snare/hat-like noise-burst sequence ---
inline juce::AudioBuffer<float> makeDrums(const double sr, const float seconds, const std::uint32_t seed) {
    const int n = static_cast<int>(sr * seconds);
    juce::AudioBuffer<float> buf(2, n);
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    const float hitIntervalS = 0.28f;
    float tCursor = 0.0f;
    int hitIndex = 0;
    while (tCursor < seconds) {
        const int startSample = static_cast<int>(tCursor * sr);
        const bool isKick = (hitIndex % 2) == 0;
        const float decayRate = isKick ? 18.0f : 40.0f;
        const int lenSamples = std::min(static_cast<int>(sr * 0.12), n - startSample);
        float hpState = 0.0f;
        for (int i = 0; i < lenSamples; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(sr);
            const float env = std::exp(-t * decayRate);
            const float noise = dist(rng);
            hpState += (noise - hpState) * 0.3f;
            const float body = isKick
                ? 0.6f * std::sin(2.0f * juce::MathConstants<float>::pi * (90.0f * std::exp(-t * 20.0f) + 45.0f) * t)
                : 0.5f * (noise - hpState);
            if (startSample + i < n)
                buf.setSample(0, startSample + i, buf.getSample(0, startSample + i) + env * body);
        }
        tCursor += hitIntervalS;
        ++hitIndex;
    }
    detail::makeTrueMono(buf);
    return buf;
}

// --- Pink noise: Voss-McCartney (sum of octave-spaced updating random rows) ---
inline juce::AudioBuffer<float> makePinkNoise(const double sr, const float seconds, const std::uint32_t seed,
                                               const float gain = 0.12f) {
    const int n = static_cast<int>(sr * seconds);
    juce::AudioBuffer<float> buf(2, n);
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    constexpr int kRows = 16;
    std::vector<float> rows(kRows, 0.0f);
    std::vector<int> counters(kRows, 0);
    for (int i = 0; i < n; ++i) {
        float sum = 0.0f;
        int idx = i;
        for (int r = 0; r < kRows; ++r) {
            if ((idx & 1) == 0) {
                rows[static_cast<size_t>(r)] = dist(rng);
            }
            sum += rows[static_cast<size_t>(r)];
            idx >>= 1;
            if (idx == 0) break;
        }
        buf.setSample(0, i, gain * sum / static_cast<float>(kRows) * 3.0f);
    }
    detail::makeTrueMono(buf);
    return buf;
}

// --- Multitone/sine sweep: logarithmic chirp + fixed multitone, alternating by seed parity ---
inline juce::AudioBuffer<float> makeMultitoneSweep(const double sr, const float seconds, const std::uint32_t seed) {
    const int n = static_cast<int>(sr * seconds);
    juce::AudioBuffer<float> buf(2, n);
    if ((seed & 1u) == 0) {
        const float f0 = 80.0f, f1 = 14000.0f;
        const float k = std::log(f1 / f0);
        for (int i = 0; i < n; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(sr);
            const float phase = 2.0f * juce::MathConstants<float>::pi * f0 * (std::exp(k * t / seconds) - 1.0f) / (k / seconds);
            buf.setSample(0, i, 0.2f * std::sin(phase));
        }
    } else {
        const float tones[] = { 220.0f, 440.0f, 880.0f, 1760.0f, 3300.0f };
        for (int i = 0; i < n; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(sr);
            float sample = 0.0f;
            for (const auto hz : tones)
                sample += 0.08f * std::sin(2.0f * juce::MathConstants<float>::pi * hz * t);
            buf.setSample(0, i, sample);
        }
    }
    detail::makeTrueMono(buf);
    return buf;
}

using GeneratorFn = juce::AudioBuffer<float> (*)(double, float, std::uint32_t);

inline std::vector<std::pair<std::string, GeneratorFn>> allGenerators() {
    return {
        { "maleSpeech",    &makeMaleSpeech },
        { "femaleSpeech",  &makeFemaleSpeech },
        { "sungVocal",     &makeSungVocal },
        { "guitarClean",   [](double sr, float s, std::uint32_t seed) { return makeGuitar(sr, s, seed, false); } },
        { "guitarDist",    [](double sr, float s, std::uint32_t seed) { return makeGuitar(sr, s, seed, true); } },
        { "piano",         &makePiano },
        { "synthPad",      &makeSynthPad },
        { "bassHeavy",     &makeBassHeavy },
        { "drums",         &makeDrums },
        { "pinkNoise",     [](double sr, float s, std::uint32_t seed) { return makePinkNoise(sr, s, seed); } },
        { "multitoneSweep",&makeMultitoneSweep },
    };
}

// Builds a corpus split. `seedBase` and `sampleRates`/`durations` are the
// only knobs - training and holdout MUST use disjoint seedBase ranges (and
// ideally at least one disjoint sample rate) or holdout validation is
// meaningless (the winning config would just be evaluated on data it was
// implicitly tuned against).
inline std::vector<CorpusItem> buildProceduralCorpus(const std::vector<double>& sampleRates,
                                                      const std::vector<float>& durations,
                                                      const int variantsPerCategory,
                                                      const std::uint32_t seedBase) {
    std::vector<CorpusItem> items;
    const auto gens = allGenerators();
    std::uint32_t seedCounter = seedBase;
    for (const auto& [label, fn] : gens) {
        for (int v = 0; v < variantsPerCategory; ++v) {
            const std::uint32_t seed = seedCounter++;
            for (const auto sr : sampleRates) {
                for (const auto dur : durations) {
                    CorpusItem item;
                    item.label = label + "_v" + std::to_string(v) + "_sr" + std::to_string(static_cast<int>(sr));
                    item.audio = fn(sr, dur, seed);
                    item.sampleRate = sr;
                    items.push_back(std::move(item));
                }
            }
        }
    }
    return items;
}

// Loads the real sung-vocal anchor recordings already committed under
// data/vxtune/ (from prior VXTune work) - genuine real-world material,
// downmixed to true mono to match the procedural corpus's convention and
// isolate the Mid-correlation mechanism under test. Training-only, per the
// user's corpus split (real anchors are not a holdout - the holdout is
// separate procedural seeds specifically so validation isn't just "did it
// overfit the one fixture we already knew about").
inline std::vector<CorpusItem> loadRealVocalAnchors(const juce::File& repoRoot) {
    std::vector<CorpusItem> items;
    const juce::StringArray paths {
        "data/vxtune/listening_packs/phase2_manual_loap_render/loap_dry/loap_dry_tight.wav",
        "data/vxtune/listening_packs/phase2_manual_loap_render/loap_dry/loap_dry_natural.wav",
    };
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    for (const auto& p : paths) {
        const auto file = repoRoot.getChildFile(p);
        if (!file.existsAsFile())
            continue;
        std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(file));
        if (reader == nullptr)
            continue;
        const int n = static_cast<int>(reader->lengthInSamples);
        juce::AudioBuffer<float> stereo(static_cast<int>(reader->numChannels), n);
        reader->read(&stereo, 0, n, 0, true, true);
        juce::AudioBuffer<float> mono(2, n);
        for (int i = 0; i < n; ++i) {
            float m = stereo.getSample(0, i);
            if (stereo.getNumChannels() > 1)
                m = 0.5f * (stereo.getSample(0, i) + stereo.getSample(1, i));
            mono.setSample(0, i, m);
            mono.setSample(1, i, m);
        }
        CorpusItem item;
        item.label = "realVocal_" + file.getFileNameWithoutExtension().toStdString();
        item.audio = std::move(mono);
        item.sampleRate = reader->sampleRate;
        items.push_back(std::move(item));
    }
    return items;
}

} // namespace vxsuite::width::corpus
