# ShipGlows Audio Engine

Private native audio infrastructure shared by ShipGlows products.

The repository deliberately separates the real-time C++ engine from product
interfaces. Flutter is a client of the engine, never part of the audio callback.

## Repository layout

- `flutter/shipglows_audio/native/engine/`: portable C++ session, buffering,
  storage, and diagnostics core, packaged with the plugin but independent of
  Flutter APIs.
- `flutter/shipglows_audio/`: private Flutter plugin and example application.
- `tests/`: dependency-free native tests.
- `docs/`: session integrity and platform architecture contracts.

## Native checks

```powershell
cmake -S . -B build/native -DSHIPGLOWS_AUDIO_BUILD_TESTS=ON
cmake --build build/native --config Debug
ctest --test-dir build/native -C Debug --output-on-failure
```

## Real-time invariants

The platform audio callback must not allocate memory, acquire locks, touch the
filesystem, call Flutter, encode compressed media, or perform network work.
It may only copy PCM frames into a preallocated single-producer/single-consumer
buffer and update lock-free counters.

## Flutter API

The private plugin exposes engine identity plus `startRecording`,
`getRecordingStatus`, and `stopRecording`. A product supplies a private session
directory and receives negotiated format and integrity metrics. Windows uses
WASAPI directly; Android reports a pending backend until the Oboe milestone is
compiled and validated.
