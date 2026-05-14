# State of the Art: Subtractive Noise Removal in VST / Audio Effects

*Research document — May 2026. Excludes AI/ML inference-based approaches (RNN, deep learning). Focuses on classical signal-processing methods.*

---

## 1. Foundations

Subtractive noise removal is built on a single idea: estimate what the noise looks like, then remove it from the signal. All classical methods are variations on this theme. The field traces to Boll (1979), whose FFT-based spectral subtraction paper remains the canonical reference.

**Core pipeline:**
1. Buffer incoming audio into overlapping frames (Hann or Hamm window, typically 50% overlap)
2. Transform each frame to the frequency domain via FFT
3. Estimate the noise spectrum (from a captured profile or adaptively)
4. Subtract (or attenuate) noise energy per frequency bin
5. IFFT back, overlap-add to reconstruct the time-domain signal

---

## 2. Core Algorithm Families

### 2.1 Spectral Subtraction (SS)

The original and most widely deployed approach. For each FFT bin *k*:

```
|X_hat(k)|² = |Y(k)|² − α · |N(k)|²
```

where `Y` is the noisy signal, `N` is the estimated noise, `α` is the **oversubtraction factor**, and negative results are floored to `β · |N(k)|²` (the **spectral floor**).

**α and β are the primary user-facing controls in almost every commercial plugin:**
- `α` high → more noise removed, more musical noise artifacts
- `β` high → fewer "birdies" but more residual broadband noise
- The perceptual sweet spot is a narrow range that depends heavily on input SNR

**Variants:**
- **Power spectral subtraction** — subtracts power spectra; less artifact-prone than magnitude subtraction but introduces phase distortion if not handled
- **Magnitude spectral subtraction** — subtracts magnitude directly; simpler, slightly more musical noise
- **Multiband SS** — different `α`/`β` per sub-band, allowing more aggressive removal in noisy bands while leaving clean ones untouched
- **Geometric spectral subtraction** — subtracts in log-spectral domain; perceptually better behaved

### 2.2 Wiener Filtering

Computes an optimal gain function per bin based on estimated SNR:

```
H(k) = SNR(k) / (1 + SNR(k))
```

Requires a noise estimate and a signal estimate. Less prone to musical noise than raw SS because the gain function is smooth and bounded, but still introduces artifacts at low SNR. Wiener is the theoretical optimum under the assumption of Gaussian stationary signals — real speech/music violates both assumptions.

**Variants:**
- **MMSE-STSA** (Ephraim & Malah 1984) — minimises mean-square error of the spectral amplitude; widely cited as the benchmark for classical speech enhancement
- **Log-MMSE** — minimises log-spectral distortion; perceptually better than MMSE-STSA, especially for music
- **Parametric Wiener** — introduces a free parameter to trade between noise reduction and distortion

### 2.3 Subspace Methods

Decompose the noisy signal covariance matrix via SVD or eigendecomposition into signal and noise subspaces, project the signal onto the clean subspace. Computationally expensive; used in high-quality offline processing (Cedar, some iZotope modes). Handles non-stationary noise better than SS.

### 2.4 Minimum Statistics / Noise Tracking

The noise estimator is as important as the subtraction algorithm. Classic approaches:

- **VAD-based** — detect speech pauses, update noise model during silence. Fails on continuous noise with no pauses.
- **Minimum Statistics (Martin 2001)** — tracks running minimum of the smoothed power spectrum; noise estimate follows the minimum envelope. Works without a VAD and handles slowly varying non-stationary noise.
- **IMCRA (Cohen 2003)** — Improved Minima Controlled Recursive Averaging; finer conditional update logic, better for rapidly changing noise floors. Used in modern implementations including adaptive modes of commercial plugins.

---

## 3. The Musical Noise Problem

Musical noise is the primary unsolved quality ceiling of all classical subtractive methods. It arises from frame-to-frame fluctuation of per-bin gain values, creating isolated spectral peaks that appear and disappear randomly — perceived as metallic "tinkles" or "birdies."

**Mitigation strategies (without AI):**

| Technique | Mechanism | Trade-off |
|---|---|---|
| Higher spectral floor β | Prevents deep over-subtraction | More residual noise |
| Temporal smoothing of gain | Low-pass filter the gain function over time | Smears transients, slower release |
| 2D spectral smoothing | Smooth gain both in frequency and time (Lukin & Todd) | Broadens artifacts spatially |
| Perceptual weighting | Shape suppression to auditory masking thresholds | Requires accurate masking model |
| Oversubtraction with non-linearity | Soft-knee gain curve instead of hard floor | More natural but less noise removed |
| Joint time-frequency analysis | Analyse coherence across neighbouring bins/frames before subtraction | Higher CPU, lower latency constraints |

iZotope's ADVANCED/EXTREME modes use joint time-frequency analysis explicitly to reduce musical noise, at the cost of higher latency. Their SIMPLE mode is equivalent to per-bin independent spectral gating — fast, real-time, more artifacts.

---

## 4. Commercial Plugin Landscape

### iZotope RX Spectral De-noise
The market benchmark. Four quality tiers:
- **Simple** — per-bin independent spectral gating, real-time capable, lowest latency
- **Standard** — adds smoothing and inter-bin coherence
- **Advanced** — joint TF analysis, significantly fewer musical noise artifacts
- **Extreme** — maximum quality, high CPU, suitable for offline or mastering

Adaptive mode continuously updates the noise profile using IMCRA-style tracking. Manual profile capture is also supported.

### Waves Z-Noise
Real-time noise profiling. Noted for **transient preservation** — a separate path detects and protects transient energy from being suppressed by the gain function. Perceptually this significantly improves the naturalness of reduced audio. Close in output quality to RX in A/B comparisons.

### Sonnox Oxford DeNoiser
Strong adaptive noise floor tracking; well-regarded for automatically following a varying noise floor without manual intervention. Includes a "learn" mode and continuous tracking mode with user-controlled tracking speed.

### Cedar (DNS series, hardware + software)
Professional broadcast standard. Uses subspace decomposition algorithms with proprietary noise estimation. Historically the highest quality non-AI classical system, especially for dialogue. Extremely low artifact rate. High latency (not suitable for real-time monitoring).

### Acon Digital DeNoise
Lower-cost alternative. Straightforward Wiener-filter-based approach with adaptive noise estimation. Regarded as clean and musical-noise-free for stationary sources but struggles with non-stationary noise.

### Blue Lab Audio Denoiser (free)
Simple spectral subtraction, per-bin gating. Useful as a reference implementation. Musical noise visible at aggressive settings.

---

## 5. Key Implementation Parameters

Every subtractive denoiser exposes some or all of these:

| Parameter | Effect | Notes |
|---|---|---|
| **Noise reduction amount / strength** | Maps to α (oversubtraction) | Primary quality/artifact trade-off control |
| **Residual noise / floor** | Maps to β | Set too high = noise remains; too low = musical noise |
| **Attack / capture** | How quickly the noise model updates | Fast = adapts to changing noise, can chase signal |
| **Release** | How quickly gain recovery occurs after a noise burst | Slow release = more smearing, fewer artifacts |
| **FFT size / quality mode** | Frequency resolution vs. latency | Larger FFT = better frequency selectivity, more latency |
| **Threshold** | Minimum SNR below which suppression is applied | Prevents attenuating loud signal accidentally |

---

## 6. Stationarity Assumptions and Their Consequences

All classical methods degrade as noise becomes less stationary:

| Noise type | Classical SS performance |
|---|---|
| Constant broadband hiss | Excellent |
| Slow HVAC rumble | Good |
| Intermittent clicks/crackle | Poor (needs separate declicker) |
| Crowd/traffic | Moderate — adaptive tracking helps |
| Reverb tail | Poor — reverb is signal-correlated, not additive noise |
| Microphone self-noise | Excellent — highly stationary |

---

## 7. Perceptual Extensions

The strongest non-AI quality improvement available in the classical domain is **perceptual weighting** — shaping the suppression gain to follow auditory masking thresholds. Bins where the noise is already masked by loud neighbouring signal components receive little or no additional suppression, saving processing for bins where noise is perceptually audible. This reduces both musical noise and over-processing artefacts without sacrificing noise reduction where it matters.

Practical implementation requires an auditory masking model (e.g., adapted from MPEG psychoacoustic model), adding significant complexity but producing clearly superior perceptual results.

---

## 8. Practical Limits of Classical Subtraction

The fundamental limit is the noise estimator, not the subtraction algorithm. If the noise estimate is wrong — due to non-stationarity, signal-noise correlation, or insufficient silence — the subtraction will produce artifacts regardless of how sophisticated the suppression gain function is.

**Practical ceiling (without AI):**
- Stationary noise: ~20–25 dB reduction with acceptable musical noise
- Non-stationary noise: ~10–15 dB before artifacts become objectionable
- Musical noise is irreducible below a threshold without either AI or human intervention (manual spectral repair)

Beyond this ceiling, classical methods hit diminishing returns and AI inference takes over. This is exactly why all major vendors (iZotope, Waves, Cedar) have AI-based modes sitting on top of classical engines.

---

## 9. Open-Source Reference Implementations

- **noisereduce (Python)** — spectral gating, stationary and non-stationary modes. Clean reference for the core algorithm. [github.com/timsainb/noisereduce](https://github.com/timsainb/noisereduce)
- **Boll 1979 PDF** — original spectral subtraction paper; still the clearest explanation of the mechanism. [Boll79.pdf](https://course.ece.cmu.edu/~ece491/homework/Boll79.pdf)

---

## Sources

- [iZotope RX Spectral De-noise Documentation](https://downloads.izotope.com/docs/rx6/34-spectral-de-noise/index.html)
- [iZotope RX 11 Spectral De-noise Feature Page](https://www.izotope.com/en/products/rx/features/spectral-de-noise)
- [Spectral Subtraction Overview — ScienceDirect Topics](https://www.sciencedirect.com/topics/computer-science/spectral-subtraction)
- [Suppression of acoustic noise in speech using spectral subtraction — Boll 1979, IEEE](https://ieeexplore.ieee.org/document/1163209/)
- [Musical Noise in Acoustic Noise Reduction — VOCAL Technologies](https://vocal.com/vocal-com/spectral-subtraction/)
- [The Artifacts of Spectral Subtraction — VOCAL Technologies](https://vocal.com/noise-reduction/spectral-subtraction/)
- [Suppression of Musical Noise Artifacts by Adaptive 2D Filtering — Lukin & Todd, Semantic Scholar](https://www.semanticscholar.org/paper/Suppression-of-Musical-Noise-Artifacts-in-Audio-by-Lukin-Todd/f3fb77ba4f4eac59102d7e57835d7ad74c3f1c25)
- [An improved spectral subtraction method using a perceptual weighting filter — ResearchGate](https://www.researchgate.net/publication/222753864_An_improved_spectral_subtraction_method_for_speech_enhancement_using_a_perceptual_weighting_filter)
- [Multi-band Spectral Subtraction Based on Adaptive Noise Estimation — ACM DL](https://dl.acm.org/doi/10.1145/3488933.3488983)
- [A noise-estimation algorithm for highly non-stationary environments — UTDallas / Loizou](https://ecs.utdallas.edu/loizou/speech/noise_estim_article_feb2006.pdf)
- [noisereduce Python library — PyPI](https://pypi.org/project/noisereduce/)
- [Boll 1979 original paper — CMU](https://course.ece.cmu.edu/~ece491/homework/Boll79.pdf)
