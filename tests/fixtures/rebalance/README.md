# ReBalance Listening Fixtures

This folder holds the generated listening-test fixtures for the ReBalance protocol.

## Source strategy

- Music fixtures come from the official 7-second MUSDB preview dataset, downloaded via the `musdb` Python package.
- The phone speech case uses local macOS `say` speech so we can keep the fixture small and fully reproducible.
- The live-room and phone variants are derived from the source stems so they still remain compatible with `VXRebalanceMeasure`.

## Cases

- `studio_vocal_guitar`
- `live_room_band`
- `phone_speech_acoustic_guitar`
- `drum_heavy_loop`
- `bass_heavy_mix`

## Prepare fixtures

```bash
python3 tools/rebalance_listening_protocol.py prepare
```

This downloads the MUSDB preview pack into:

```text
tests/fixtures/rebalance/musdb_preview/
```

and writes generated case folders plus a manifest into:

```text
tests/fixtures/rebalance/listening_cases/
```

## Run the measure harness

Build the binary first:

```bash
cmake --build build --target VXRebalanceMeasure -j4
```

Then generate the report:

```bash
python3 tools/rebalance_listening_protocol.py report
```

The report is written to:

```text
tasks/reports/rebalance_listening_protocol_report.md
```

## Notes

- The studio and live guitar cases use a guitar-dominant accompaniment proxy from MUSDB's `other` stem because the preview dataset is 4-stem, not dedicated-guitar.
- The resulting folders still match the filename contract expected by `VXRebalanceMeasure`:
  `_original`, `_vocals`, `_drums`, `_bass`, `_guitar`, `_piano`, `_other`.
