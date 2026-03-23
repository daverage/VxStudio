# Voice Corpus

Small real-world spoken-word corpus for tuning and evaluating VX Suite `Voice` mode behavior.

## Sources

1. `churchill_be_ye_men_of_valour`
   Source page: <https://commons.wikimedia.org/wiki/File:Winston_Churchill_-_Be_Ye_Men_of_Valour.ogg>
   Local WAV: `/Users/andrzejmarczewski/Documents/GitHub/VxStudio/data/voice_corpus/wav/churchill_be_ye_men_of_valour.wav`

2. `edward_viii_abdication`
   Source page: <https://commons.wikimedia.org/wiki/File:Edward_VIII_abdication_speech.ogg>
   Local WAV: `/Users/andrzejmarczewski/Documents/GitHub/VxStudio/data/voice_corpus/wav/edward_viii_abdication.wav`

3. `princess_elizabeth_21st_birthday`
   Source page: <https://commons.wikimedia.org/wiki/File:Princess_Elizabeth%27s_21st_birthday_speech.oga>
   Local WAV: `/Users/andrzejmarczewski/Documents/GitHub/VxStudio/data/voice_corpus/wav/princess_elizabeth_21st_birthday.wav`

4. `old_letters_librivox`
   Source page: <https://archive.org/details/old_letters_2303.poem_librivox>
   Local WAV: `/Users/andrzejmarczewski/Documents/GitHub/VxStudio/data/voice_corpus/wav/old_letters_librivox.wav`

## Preparation

- Raw downloads live in `/Users/andrzejmarczewski/Documents/GitHub/VxStudio/data/voice_corpus/raw/`
- WAV conversions are `48 kHz`, mono, PCM via `ffmpeg`

## Current Voice-Mode Baseline

Measured with:

```bash
./build/VXLevelerMeasure <input.wav> <output.wav> voice 1.0 1.0
```

Results:

- `churchill_be_ye_men_of_valour`: `22.8432 dB` -> `20.1124 dB`
- `edward_viii_abdication`: `10.1938 dB` -> `7.18846 dB`
- `old_letters_librivox`: `19.8806 dB` -> `18.0528 dB`
- `princess_elizabeth_21st_birthday`: `12.9195 dB` -> `10.9563 dB`

## Intended Use

- Tune shared framework-level vocal analysis
- Evaluate `Vocal Rider` behavior on real speech before tuning mixed material
- Compare future `Voice`-mode rewrites against a repeatable multi-file baseline
