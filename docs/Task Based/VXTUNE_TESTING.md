## What the MAST dataset gives you

Each example contains:

* A real student singing a short melody
* The piano melody they were attempting to reproduce
* Five expert assessments on a four-point scale
* A majority score and, for some files, a unanimous score
* A precomputed CREPE fundamental-frequency track
* Audio sampled at 8 kHz ([zenodo.org][1])

The scores are:

* **1:** completely off
* **2:** major mistakes
* **3:** minor mistakes
* **4:** perfect ([zenodo.org][1])

That makes it useful for testing whether VX Tune improves genuinely imperfect singing. It is not, by itself, a clean supervised training set showing exactly how every note should be corrected.

## 1. Build a small VX Tune benchmark first

Do not start by feeding all the files into development. Select a representative fixed benchmark of perhaps **40 to 60 performances**.

Use roughly:

| Group                      | Purpose                                                    |
| -------------------------- | ---------------------------------------------------------- |
| 10 score-4 performances    | Check that VX Tune does not damage good singing            |
| 15 score-3 performances    | Test subtle, natural correction                            |
| 15 score-2 performances    | Test clear but recoverable errors                          |
| 10 score-1 performances    | Test when the performance is too wrong to correct reliably |
| Several unanimous examples | Higher-confidence evaluation cases                         |

Keep this set permanently frozen. Every new VX Tune build should process exactly the same files with exactly the same settings.

Where possible, prioritise `fullAgree` examples because all annotators agreed on the severity. The dataset explicitly provides `fullAgree`, `fullAgree_score` and majority-score fields for this purpose. ([zenodo.org][1])

## 2. Run three versions of every performance

For each selected vocal, produce:

1. **Original**
2. **VX Tune natural/default**
3. **VX Tune tighter setting**

You could optionally add a competing pitch-correction product as an external reference, but it should not become your definition of correctness.

The comparison should be level matched and anonymised so listeners do not know which version they are hearing.

## 3. Test three separate parts of VX Tune

### Pitch detection

Before judging the corrected sound, inspect what VX Tune believes the singer is doing.

Compare its detected pitch against:

* The supplied CREPE F0 track
* The piano reference
* Your own visual inspection
* Manual correction on a smaller subset

You are looking for:

* Octave errors
* Lost low-level notes
* Unvoiced consonants interpreted as pitch
* Pitch jumping during breath noise
* Vibrato being split into separate notes
* Slides being interpreted as a succession of target notes

Do not treat the supplied CREPE output as absolute truth. It is another algorithmic estimate, not a manual note-level annotation. Its value is as an independent comparison point. The dataset states that its F0 series were extracted using CREPE. ([zenodo.org][1])

### Note interpretation

This is probably the more important test for your natural-correction concept.

VX Tune needs to distinguish between:

* A note that is centred slightly flat
* A deliberate scoop into the note
* Vibrato around the note
* A passing pitch between notes
* A genuinely wrong note
* A melody that is too far from the reference to interpret safely

The piano reference gives you intended melodic context. You can align the sung pitch contour with the reference and examine where VX Tune selects the right target note but handles the movement badly, versus where it chooses the wrong target altogether.

### Audio correction

Then listen for:

* Whether the final centre pitch is improved
* Whether the original attack is retained
* Whether vibrato remains asymmetric and human
* Whether transitions still feel sung rather than programmed
* Whether formants remain stable
* Whether the correction causes warble or grain
* Whether an already-good performance is made worse

The 8 kHz sampling rate limits how confidently you can assess high-frequency texture, consonants, air and production-quality artefacts. It remains useful for pitch tracking, note decisions and broad correction behaviour. ([zenodo.org][1])

## 4. Define measurable tests

For each detected voiced frame, calculate the distance from the expected target in cents.

Useful metrics include:

### Median pitch error

The typical absolute distance from the target note.

This tells you whether the corrected note centres are closer to the intended pitches.

### Stable-region error

Measure only the central, stable part of each note, excluding attacks and transitions.

That prevents VX Tune from appearing worse merely because it correctly preserves a scoop or transition.

### Transition preservation

Measure how long the singer naturally takes to move between notes before and after correction.

A poor correction engine may improve average pitch error while making every transition abrupt.

### Vibrato preservation

Measure before and after:

* Vibrato rate
* Vibrato depth
* Shape
* Centre pitch
* Onset delay

The ideal result is usually to move the **centre** of the vibrato closer to the target without removing the modulation.

### Correction movement

Measure how far VX Tune moves the original pitch.

This is important because two results may be equally accurate, but one may have used much more intervention and sound less natural.

A useful optimisation target is therefore not simply:

> Minimise corrected pitch error.

It is closer to:

> Minimise perceptually relevant pitch error while minimising unnecessary movement and preserving expressive modulation.

## 5. Add a listening test

Expert scores tell you how poor the original performance was. They do not tell you whether your corrected output sounds natural.

For each example, ask listeners two independent questions:

1. **Is the singing more in tune?**
2. **Does it sound more natural?**

Do not combine those into one score. A correction can improve intonation while damaging naturalness.

A simple blind comparison works:

* A versus B
* Which sounds more in tune?
* Which sounds more natural?
* Is the difference negligible, useful or excessive?

Include original-versus-original duplicates occasionally. They expose listeners who are making arbitrary selections.

## 6. Use the results to improve the current non-AI engine

The first learning loop does not need machine learning.

Create a results table containing:

* Original expert rating
* Input pitch error
* VX Tune output pitch error
* Amount of pitch movement
* Vibrato change
* Transition change
* Listener preference
* Failure category
* Current parameter values

You can then identify patterns such as:

* Score-3 performances need slower correction on attacks.
* Large initial scoops are being corrected too early.
* Strong vibrato is being mistaken for unstable intonation.
* Corrections over 70 cents are usually judged artificial.
* The target-note logic becomes unreliable when several consecutive notes are wrong.
* Low-confidence regions should receive less correction.

Those findings can directly tune:

* Pitch confidence thresholds
* Note segmentation
* Attack protection
* Correction time constants
* Maximum correction range
* Vibrato detection
* Hysteresis between neighbouring targets
* Rules for declining or reducing correction

This is likely the highest-value first use of MAST for VX Tune.

## 7. How it could help VX Tune learn

There are several levels of learning, and they have different data requirements.

### A. Automated parameter optimisation

This is the safest starting point.

Run VX Tune across a development subset using many combinations of hidden parameters. Score each result using a weighted objective containing:

* Stable-note accuracy
* Correction movement
* Transition preservation
* Vibrato preservation
* Human preference scores

An optimiser can find parameter combinations that outperform manually chosen defaults.

The user still sees only the small number of musical controls you want. The optimiser tunes the hidden engine behaviour.

### B. Learn when and how much to correct

Train a small model to predict a correction confidence or correction amount from features such as:

* Current pitch error
* Pitch confidence
* Note duration
* Pitch slope
* Vibrato rate and depth
* Distance from note onset
* Distance from transition
* Previous and next candidate notes
* Whether the melody context supports the target
* Signal quality

The output would not generate audio. It would control the existing deterministic correction engine:

* Correct fully
* Correct gently
* Preserve the movement
* Do nothing
* Mark as ambiguous

This hybrid approach fits your VX Studio philosophy better than replacing the DSP with a black-box neural pitch corrector.

### C. Preference learning

Your listening tests can generate paired examples:

* Result A was preferred over result B
* A was more natural
* B was more in tune
* Both were worse than the original

A ranking model could then learn which correction behaviour listeners prefer.

This is potentially more valuable than training against mathematically perfect pitch, because perfect cent alignment is not the same as natural singing.

### D. End-to-end supervised correction

MAST is not sufficient for this.

It does not provide a clean corrected version of every imperfect vocal performed by the same singer. The piano reference establishes the melody, but it does not supply the singer’s ideal vocal timbre, phrasing, vibrato and transitions.

Training an audio-in/audio-out model would require pairs such as:

* Naturally imperfect vocal
* Professionally corrected version of that exact vocal

Or:

* Clean vocal
* Many controlled degraded versions
* Original clean vocal as the target

That would be a substantially different dataset.

## 8. Build synthetic training pairs from real vocals

A practical path is:

1. Take naturally good or score-4 vocal performances.
2. Preserve the original as the target.
3. Apply controlled pitch distortions.
4. Train or test VX Tune on recovering the original pitch contour.

Distortions should include:

* Constant offsets of ±10 to ±80 cents
* Gradual drift
* Incorrect vibrato centre
* Excessive vibrato
* Uneven sustained notes
* Slow scoops
* Overshoots
* Late arrival at the target
* One genuinely wrong note
* Pitch instability near note endings

Avoid simply shifting every note by a fixed random amount. That creates technically convenient but musically unrealistic errors.

You could use the score-1 to score-3 MAST performances to model the statistical shapes of real mistakes, then apply similar error patterns to cleaner, higher-resolution vocals.

## 9. Recommended data split

Keep speakers separated between sets wherever the dataset identifiers allow it:

* **Development:** tune rules and hidden parameters
* **Validation:** choose between designs
* **Test:** untouched until meaningful releases

Do not put performances from the same singer or same recording session into both training and test sets. Otherwise the system may learn recording or singer characteristics rather than general correction behaviour.

Also keep the frozen benchmark separate from anything used to optimise parameters.

## Practical first milestone

For the current VX Tune, I would not begin with machine learning. I would build this first:

1. Select 40 high-confidence MAST examples.
2. Create an automated batch processor around the standalone or plugin engine.
3. Save original pitch, chosen target pitch, corrected pitch and confidence per frame.
4. Export original and corrected WAV files.
5. Generate pitch-contour comparison plots.
6. Categorise every obvious failure.
7. Run a small blind listening test.
8. Change the hidden correction rules.
9. Repeat against the same frozen set.

That will tell you whether the current algorithm is actually improving real singers, where it fails, and which behaviours are worth learning. Only then should you decide whether the learning component needs to optimise parameters, estimate correction confidence or perform something more ambitious.

[1]: https://zenodo.org/records/8007358?utm_source=chatgpt.com "MAST melody dataset | Zenodo"
