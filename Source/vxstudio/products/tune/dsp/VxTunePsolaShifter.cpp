#include "VxTunePsolaShifter.h"

#include <algorithm>
#include <cmath>

namespace vxsuite::tune {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kRenderConfidence = 0.6f;

int nextPowerOfTwo(int value) {
    int p = 1;
    while (p < value)
        p <<= 1;
    return p;
}

} // namespace

void PsolaShifter::prepare(const double sampleRate, const int maxBlockSamples,
                           const int numChannels) {
    sr = sampleRate > 1000.0 ? sampleRate : 48000.0;
    latency = static_cast<int>(std::ceil(sr / 80.0));
    maxPeriod = static_cast<int>(std::ceil(sr / 70.0));
    channelsPrepared = std::max(1, numChannels);

    const int ringSize = nextPowerOfTwo(std::max(64, maxBlockSamples) + latency + 6 * maxPeriod);
    mask = ringSize - 1;

    inRing.assign(static_cast<size_t>(channelsPrepared),
                  std::vector<float>(static_cast<size_t>(ringSize), 0.0f));
    outRing.assign(static_cast<size_t>(channelsPrepared),
                   std::vector<float>(static_cast<size_t>(ringSize), 0.0f));
    weightRing.assign(static_cast<size_t>(ringSize), 0.0f);
    lpRing.assign(static_cast<size_t>(ringSize), 0.0f);

    // Default (pre-detection / unvoiced) cutoff; retuned per-hint once a
    // fundamental is known. See header comment on lpCoeff.
    lpCoeff = 1.0f - std::exp(-2.0f * kPi * 400.0f / static_cast<float>(sr));
    mixStep = 1.0f / (0.005f * static_cast<float>(sr));   // ~5 ms crossfade
    parkHoldSamples = static_cast<int>(0.05 * sr);
    reset();
}

void PsolaShifter::reset() {
    for (auto& ring : inRing)
        std::fill(ring.begin(), ring.end(), 0.0f);
    for (auto& ring : outRing)
        std::fill(ring.begin(), ring.end(), 0.0f);
    std::fill(weightRing.begin(), weightRing.end(), 0.0f);
    std::fill(lpRing.begin(), lpRing.end(), 0.0f);
    lp1 = lp2 = 0.0f;
    inputCount = 0;
    emitCursor = 0;
    hintPeriod = 0.0f;
    hintConfidence = 0.0f;
    smoothedPeriod = 0.0f;
    voicedActive = false;
    softHops = 0;
    hardHops = 0;
    epochCount = 0;
    epochPhase = -1.0;
    lastConfirmedEpochPos = -1;
    lastVoicedPeriod = 0.0f;
    lastUsedEpochPos = -1;
    nextSynthCentre = 0.0;
    coverEnd = -1;
    targetCents = 0.0f;
    smoothedCents = 0.0f;
    mix = 0.0f;
    parkedNow = false;
    parkedRun = 0;
}

void PsolaShifter::setPeriodHint(const float periodSamples,
                                 const float voicedConfidence) noexcept {
    if (voicedConfidence >= kRenderConfidence && periodSamples > 0.0f) {
        const bool returningFromUncertainGap = softHops >= 2 || hardHops > 0;

        // Rate-limit the period so a single octave-error frame cannot
        // scatter the epoch grid.
        float period = periodSamples;
        if (voicedActive && hintPeriod > 0.0f)
            period = std::clamp(period, hintPeriod * 0.75f, hintPeriod * 1.33f);
        hintPeriod = std::min(period, static_cast<float>(maxPeriod));
        hintConfidence = voicedConfidence;
        lastVoicedPeriod = hintPeriod;

        // Smoothed period feeds ONLY grain window length (see header
        // comment and Epoch::period below) - never grid advance. Grid
        // advance must track the true instantaneous period tightly
        // (that's what the peak-search nudge is for); smoothing it here
        // was tried and measured worse (grid drifts away from real peaks
        // faster than the now-tight nudge clamp can correct).
        smoothedPeriod = smoothedPeriod > 0.0f
            ? smoothedPeriod + 0.25f * (hintPeriod - smoothedPeriod)
            : hintPeriod;

        // Track the epoch-search low-pass near this fundamental so the
        // peak search sees one dominant lobe per period instead of
        // formant-driven false peaks (see header comment).
        const float f0 = static_cast<float>(sr) / smoothedPeriod;
        const float cutoff = std::clamp(2.2f * f0, 150.0f, 900.0f);
        lpCoeff = 1.0f - std::exp(-2.0f * kPi * cutoff / static_cast<float>(sr));
        // Returning from a gap: re-seed the grid rather than marching epoch
        // searches across unvoiced/uncertain audio. A few medium-confidence
        // hops are enough for old vowel epochs to smear into consonants on
        // real vocals, so drop the continuity chain before rendering resumes.
        if ((!voicedActive || returningFromUncertainGap)
            && epochPhase >= 0.0
            && epochPhase < static_cast<double>(inputCount) - 2.0 * hintPeriod) {
            epochCount = 0;
            epochPhase = -1.0;
            smoothedPeriod = hintPeriod;   // don't glide in from a stale pre-gap value
            lastConfirmedEpochPos = -1;
            lastUsedEpochPos = -1;
        }
        voicedActive = true;
        softHops = 0;
        hardHops = 0;
        return;
    }

    // Medium confidence with a plausible grid: the voice is still periodic,
    // the detector is momentarily unsure — flywheel instead of dropping out
    // (mode flapping mid-note is an audible bubble source).
    if (voicedConfidence >= 0.35f && periodSamples > 0.0f && voicedActive) {
        hintConfidence = voicedConfidence;
        hardHops = 0;
        if (++softHops > 10)            // ~53 ms of doubt: give up
            voicedActive = false;
        return;
    }

    hintConfidence = 0.0f;
    ++hardHops;
    if (hardHops >= 2)                  // ~10 ms of hard evidence: unvoiced
        voicedActive = false;
    if (hardHops >= 24) {               // ~130 ms: a real pause, not a consonant.
        // Old epochs point at audio the ring will overwrite; drop them.
        epochCount = 0;
        epochPhase = -1.0;
        hintPeriod = 0.0f;
        lastUsedEpochPos = -1;
    }
}

void PsolaShifter::appendInput(const float* const* channels, const int numChannels,
                               const int offset, const int count) noexcept {
    for (int i = 0; i < count; ++i) {
        const int slot = static_cast<int>((inputCount + i) & mask);
        float mono = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch) {
            const float v = channels[ch][offset + i];
            inRing[static_cast<size_t>(ch)][static_cast<size_t>(slot)] = v;
            mono += v;
        }
        mono /= static_cast<float>(numChannels);
        lp1 += lpCoeff * (mono - lp1);
        lp2 += lpCoeff * (lp1 - lp2);
        lpRing[static_cast<size_t>(slot)] = lp2;
    }
    inputCount += count;
}

void PsolaShifter::trackEpochs() noexcept {
    if (!voicedActive || hintPeriod <= 0.0f || hintConfidence < kRenderConfidence)
        return;
    // advancePeriod steps the grid and MUST track the true instantaneous
    // period tightly (smoothing this drifts the grid away from real peaks
    // faster than the tight nudge clamp below can correct - measured
    // regression). windowPeriod is stored per-epoch for grain length only:
    // smoothing IT (not grid position) is what damps overlap-add unity-gain
    // ripple from cycle-to-cycle window-length jitter.
    const double advancePeriod = std::min(hintPeriod, static_cast<float>(maxPeriod));
    const double windowPeriod = std::min(smoothedPeriod > 0.0f ? smoothedPeriod : hintPeriod,
                                         static_cast<float>(maxPeriod));
    // Search window matches the nudge clamp below: no point finding a
    // candidate the clamp will immediately reject.
    const int search = std::max(2, static_cast<int>(std::min(advancePeriod * 0.02, 3.0)) + 1);

    // Reference-window half-length for NCC matching (and the seed's
    // amplitude-peak bootstrap window).
    const int halfLen = std::max(4, static_cast<int>(windowPeriod / 2.0));

    if (epochPhase < 0.0) {
        // Seed: no prior cycle exists to match against yet, so bootstrap
        // with the strongest low-passed peak in the last period of input.
        // Every epoch after this one is matched by shape (below), so a
        // single ambiguous bootstrap peak self-corrects within a cycle or
        // two rather than propagating.
        const std::int64_t from = std::max<std::int64_t>(0,
            inputCount - static_cast<std::int64_t>(advancePeriod) - 1);
        std::int64_t best = from;
        float bestValue = -1.0f;
        for (std::int64_t p = from; p < inputCount; ++p) {
            const float v = std::abs(lpRing[static_cast<size_t>(p & mask)]);
            if (v > bestValue) {
                bestValue = v;
                best = p;
            }
        }
        const int index = epochCount % kMaxEpochs;
        epochs[index] = { best, static_cast<float>(windowPeriod) };
        ++epochCount;
        lastConfirmedEpochPos = best;
        epochPhase = static_cast<double>(best) + advancePeriod;
        return;
    }

    // Advance the grid by the fractional period; a shape match against the
    // previous confirmed cycle may only NUDGE the phase, clamped tightly.
    // Even with shape matching a generous clamp would let a strong but
    // wrong-lobe candidate win on an unlucky cycle; capping at a few
    // samples keeps the worst case bounded and the highest rendered
    // harmonics coherent.
    const bool correcting = hintConfidence >= kRenderConfidence;
    const double maxNudge = std::min(3.0, advancePeriod * 0.02);
    while (epochPhase + search + halfLen < static_cast<double>(inputCount)) {
        const std::int64_t predicted =
            static_cast<std::int64_t>(std::llround(epochPhase));
        double nudge = 0.0;
        if (correcting && lastConfirmedEpochPos - halfLen >= 0) {
            std::int64_t best = predicted;
            float bestScore = -1.0e9f;
            for (std::int64_t p = predicted - search; p <= predicted + search; ++p) {
                if (p - halfLen < 0)
                    continue;
                double num = 0.0, da = 0.0, db = 0.0;
                for (int k = -halfLen; k <= halfLen; ++k) {
                    const float a = lpRing[static_cast<size_t>(
                        (lastConfirmedEpochPos + k) & mask)];
                    const float b = lpRing[static_cast<size_t>((p + k) & mask)];
                    num += static_cast<double>(a) * b;
                    da += static_cast<double>(a) * a;
                    db += static_cast<double>(b) * b;
                }
                const float score = static_cast<float>(num / std::sqrt(da * db + 1e-12));
                if (score > bestScore) {
                    bestScore = score;
                    best = p;
                }
            }
            nudge = std::clamp(static_cast<double>(best - predicted),
                               -maxNudge, maxNudge);
        }
        const std::int64_t confirmed =
            static_cast<std::int64_t>(std::llround(epochPhase + nudge));
        const int index = epochCount % kMaxEpochs;
        epochs[index] = { confirmed, static_cast<float>(windowPeriod) };
        ++epochCount;
        lastConfirmedEpochPos = confirmed;
        epochPhase += advancePeriod + nudge;
    }
}

void PsolaShifter::placeOneGrain(const std::int64_t synthCentre,
                                 const std::int64_t analysisEpoch,
                                 const float subSampleShift,
                                 const int halfLength, const float gain,
                                 const int numChannels) noexcept {
    const float invHalf = 1.0f / static_cast<float>(halfLength);
    // The true (fractional) synthesis centre was rounded to synthCentre to
    // index the ring buffer; subSampleShift is that rounding error
    // (writePos_int - true centre). Shifting the READ position by the
    // opposite amount, with cubic interpolation, reconstructs the grain
    // as if it had been written at its true fractional position instead
    // of snapping to the nearest sample - the error is small per grain
    // (<=0.5 samples) but compounds audibly at higher pitches, where half
    // a sample is a much larger fraction of the period (confirmed by the
    // review: "even a one-sample error is a significant fraction of the
    // pitch period" at high frequencies). Cubic (Catmull-Rom), not linear:
    // linear interpolation is a mild low-pass filter, and applying it to
    // every grain sample measurably cost high-harmonic preservation
    // (worse than plain integer reads on the harmonic-preservation test).
    // Catmull-Rom has a much flatter passband for the same 1-sample-or-
    // better timing accuracy.
    for (int k = -halfLength; k < halfLength; ++k) {
        const std::int64_t writePos = synthCentre + k;
        if (writePos < emitCursor)         // never touch emitted samples
            continue;
        const float window = 0.5f * (1.0f + std::cos(kPi * static_cast<float>(k) * invHalf));
        const float readPosF = static_cast<float>(analysisEpoch + k) - subSampleShift;
        const std::int64_t readBase = static_cast<std::int64_t>(std::floor(readPosF));
        const float frac = readPosF - static_cast<float>(readBase);
        const int readSlotM1 = static_cast<int>((readBase - 1) & mask);
        const int readSlot0 = static_cast<int>(readBase & mask);
        const int readSlot1 = static_cast<int>((readBase + 1) & mask);
        const int readSlot2 = static_cast<int>((readBase + 2) & mask);
        const int writeSlot = static_cast<int>(writePos & mask);
        for (int ch = 0; ch < numChannels; ++ch) {
            auto& in = inRing[static_cast<size_t>(ch)];
            const float ym1 = in[static_cast<size_t>(readSlotM1)];
            const float y0 = in[static_cast<size_t>(readSlot0)];
            const float y1 = in[static_cast<size_t>(readSlot1)];
            const float y2 = in[static_cast<size_t>(readSlot2)];
            // Catmull-Rom cubic Hermite spline.
            const float a0 = -0.5f * ym1 + 1.5f * y0 - 1.5f * y1 + 0.5f * y2;
            const float a1 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
            const float a2 = -0.5f * ym1 + 0.5f * y1;
            const float a3 = y0;
            const float sample = ((a0 * frac + a1) * frac + a2) * frac + a3;
            outRing[static_cast<size_t>(ch)][static_cast<size_t>(writeSlot)] +=
                gain * window * sample;
        }
        weightRing[static_cast<size_t>(writeSlot)] += window;
    }
    coverEnd = std::max(coverEnd, synthCentre + halfLength - 1);
}

void PsolaShifter::placeGrains(const int numChannels) noexcept {

    smoothedCents += 0.2f * (targetCents - smoothedCents);   // per grain batch
    const float ratio = std::exp2(smoothedCents / 1200.0f);
    // Park with hysteresis: flapping between snapped and free-running grain
    // scheduling at the dead-zone boundary is itself an artifact source.
    const bool parked = parkedNow ? std::abs(smoothedCents) < 1.5f
                                  : std::abs(smoothedCents) < 0.3f;
    parkedNow = parked;

    // Epoch nearest the requested time whose full window is available and
    // whose audio is still recent (never splice stale or far-future
    // content — both read as pops on real material).
    const std::int64_t oldestAllowed = inputCount - 4 * latency;
    const auto usableEpoch = [&](const std::int64_t target) -> const Epoch* {
        const int stored = std::min(epochCount, kMaxEpochs);
        const Epoch* best = nullptr;
        std::int64_t bestDistance = 0;
        for (int i = 0; i < stored; ++i) {
            const Epoch& e = epochs[i];
            const std::int64_t half = static_cast<std::int64_t>(
                std::min(e.period, static_cast<float>(latency)));
            if (e.position + half >= inputCount)      // window not complete yet
                continue;
            if (e.position < oldestAllowed)           // audio being overwritten
                continue;
            // Prefer epochs at or before the target; an epoch after the
            // target costs double so slight grain repetition beats
            // splicing future content early.
            const std::int64_t distance = e.position <= target
                ? target - e.position
                : 2 * (e.position - target);
            if (best == nullptr || distance < bestDistance) {
                best = &e;
                bestDistance = distance;
            }
        }
        return best;
    };

    // Pseudo-period for stretches without a usable epoch (consonants,
    // breaths, onsets): continue granular rendering rather than crossfading
    // to a different signal mid-phrase. These grains read their own centre
    // minus latency at hop == half, so Hann windows tile to exactly one and
    // the stretch is a transparent delayed passthrough.
    const float pseudo = lastVoicedPeriod > 0.0f
        ? std::clamp(lastVoicedPeriod, 64.0f, static_cast<float>(latency))
        : 0.006f * static_cast<float>(sr);

    // Continuity-preferring selection: default to the successor of the last
    // used epoch and only resync to the nearest-to-target epoch when the
    // successor has drifted a full period away. A stable analysis sequence
    // keeps the synthesis-to-analysis offset constant, which is what makes
    // overlapping grains sum coherently.
    const auto chooseEpoch = [&](const std::int64_t target,
                                 const double period) -> const Epoch* {
        const Epoch* nearest = usableEpoch(target);
        if (nearest == nullptr || lastUsedEpochPos < 0)
            return nearest;
        const Epoch* successor = usableEpoch(
            lastUsedEpochPos + static_cast<std::int64_t>(std::llround(period)));
        if (successor != nullptr
            && successor->position >= lastUsedEpochPos
            && std::llabs(successor->position - target)
                   < static_cast<std::int64_t>(period))
            return successor;
        return nearest;
    };

    while (true) {
        const Epoch* chosen = nullptr;
        if (voicedActive && hintConfidence >= kRenderConfidence && epochCount > 0) {
            const double guessPeriod = smoothedPeriod > 0.0f ? smoothedPeriod
                                                             : static_cast<double>(pseudo);
            chosen = chooseEpoch(
                static_cast<std::int64_t>(nextSynthCentre) - latency, guessPeriod);
        }

        if (chosen != nullptr) {
            const double period = chosen->period;
            if (nextSynthCentre > static_cast<double>(inputCount - 1) + period)
                break;

            std::int64_t centre = static_cast<std::int64_t>(std::llround(nextSynthCentre));
            float subSampleShift =
                static_cast<float>(static_cast<double>(centre) - nextSynthCentre);
            if (parked) {
                // Snap onto the analysis grid so equal-period Hann grains
                // tile to exactly one: transparent delayed passthrough.
                // No sub-sample compensation here - this path must stay
                // bit-exact, not merely close.
                const std::int64_t snapped = chosen->position + latency;
                if (snapped >= centre - static_cast<std::int64_t>(period) / 2) {
                    centre = snapped;
                    subSampleShift = 0.0f;
                }
                nextSynthCentre = static_cast<double>(centre) + period;
            } else {
                nextSynthCentre += std::max(8.0, period / ratio);
            }

            const int half = std::max(8, static_cast<int>(
                std::min(chosen->period, static_cast<float>(latency))));
            placeOneGrain(centre, chosen->position, subSampleShift, half,
                         1.0f / ratio, numChannels);
            lastUsedEpochPos = chosen->position;
        } else {
            const int half = std::max(8, static_cast<int>(pseudo));
            if (nextSynthCentre > static_cast<double>(inputCount - 1 + latency - half))
                break;
            const std::int64_t centre =
                static_cast<std::int64_t>(std::llround(nextSynthCentre));
            const float subSampleShift =
                static_cast<float>(static_cast<double>(centre) - nextSynthCentre);
            placeOneGrain(centre, centre - latency, subSampleShift, half, 1.0f,
                         numChannels);
            nextSynthCentre += half;
        }
    }
}

void PsolaShifter::emitOutput(float* const* channels, const int numChannels,
                              const int offset, const int count) noexcept {
    for (int i = 0; i < count; ++i) {
        const std::int64_t x = emitCursor + i;
        const int outSlot = static_cast<int>(x & mask);
        const int drySlot = static_cast<int>((x - latency) & mask);
        parkedRun = parkedNow ? parkedRun + 1 : 0;
        const bool dryPark = parkedRun > parkHoldSamples;
        // The grain stream is continuous through voiced and unvoiced alike;
        // the dry path is only used for sustained zero shift (bit-exact) and
        // before coverage exists at stream start.
        const float target = (!dryPark && x <= coverEnd) ? 1.0f : 0.0f;
        mix += std::clamp(target - mix, -mixStep, mixStep);
        // Overlap-add normalisation: the window sum accumulated at this
        // sample is only guaranteed to be 1 (COLA) at ratio 1. Dividing it
        // back out keeps level stable as synthesis spacing varies with
        // the correction ratio, instead of pulsing with it. The Hann
        // window itself tapers to 0 at a grain's edges, so right at
        // coverage boundaries (stream start, a genuine gap, a transition
        // where only one grain's tail lands on a sample instead of the
        // usual two overlapping grains) the accumulated weight can be
        // small without being absent. A low fixed threshold here is a
        // real bug: dividing by e.g. 0.06 is a 16x gain spike, audible as
        // a click/glitch exactly at transitions - measured directly (a
        // fast register-break moment introduced a brief dropout/garbled
        // read that wasn't present before this normalisation was added).
        // Flooring the divisor caps the worst-case boost instead of
        // leaving it unbounded; low-coverage samples get quieter, which
        // is a far safer failure mode than an amplification spike.
        const float weight = weightRing[static_cast<size_t>(outSlot)];
        const float normalise = 1.0f / std::max(weight, 0.3f);
        for (int ch = 0; ch < numChannels; ++ch) {
            auto& out = outRing[static_cast<size_t>(ch)];
            const float wet = out[static_cast<size_t>(outSlot)] * normalise;
            const float dry = x >= latency
                ? inRing[static_cast<size_t>(ch)][static_cast<size_t>(drySlot)]
                : 0.0f;
            channels[ch][offset + i] = mix * wet + (1.0f - mix) * dry;
            out[static_cast<size_t>(outSlot)] = 0.0f;   // slot free for reuse
        }
        weightRing[static_cast<size_t>(outSlot)] = 0.0f;
    }
    emitCursor += count;
}

void PsolaShifter::process(float* const* channels, const int numChannels,
                           const int numSamples) noexcept {
    const int usedChannels = std::min(numChannels, channelsPrepared);
    // Chunk so one call can never lap the rings.
    const int chunkLimit = std::max(64, (mask + 1) - latency - 6 * maxPeriod);
    int offset = 0;
    while (offset < numSamples) {
        const int n = std::min(chunkLimit, numSamples - offset);
        appendInput(channels, usedChannels, offset, n);
        trackEpochs();
        placeGrains(usedChannels);
        emitOutput(channels, usedChannels, offset, n);
        offset += n;
    }
}

} // namespace vxsuite::tune
