# Rubber Band comparator

Rubber Band is intentionally not vendored into the production plugin tree here.
The upstream project is GPL-2.0-or-later unless a commercial licence is used,
so VX Tune should only link it in a development benchmark build after the
licensing decision is explicit.

Use it as a comparator for the renderer acceptance harness:

- feed the same dry input buffers;
- feed the same requested correction envelope;
- report the same latency and applied-cents diagnostics;
- compare against the default Signalsmith backend using the same correction envelope.

Do not make Rubber Band a shipped backend through package-manager discovery by
accident. Add an explicit CMake option and a separate benchmark target when the
benchmark harness lands.
