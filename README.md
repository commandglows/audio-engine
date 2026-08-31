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
- `shipglows_data/technical/contracts/`: human-readable session and platform
  contracts.
- `shipglows_data/technical/`: canonical internal code-navigation and
  maintenance map.

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

The private plugin exposes engine identity plus `startRecording`, pause/resume,
`getRecordingStatus`, and `stopRecording`. A product supplies a private session
directory and receives negotiated format, integrity metrics, hardware timestamp
coverage, and a structured recovery hint. Windows uses event-driven WASAPI with
bounded default-device recovery; Android uses Oboe and packages native libraries
for the three Flutter Android ABIs.

See `shipglows_data/technical/contracts/verification.md` for the exact proof
level of each backend. A native build is a compile/package proof, not a
substitute for the device matrix needed before a production release.

For internal maintenance, start with
`shipglows_data/technical/code-docs-map.md`; it routes each code area to its
contract and focused validation.
