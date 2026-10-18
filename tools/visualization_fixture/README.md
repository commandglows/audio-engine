# Private visualization fixture harness

`scene.ts` is a stateless TypeScript mirror of the shared Dart scene evaluator
and prepared speech-window path. It consumes already transformed fixture
records and covers bars, music and speech waveforms, radial geometry, seeded
particles, random-order seeking, and common-suffix interpolation. It performs
no audio analysis, filtering, or smoothing.

Run the focused checks and local A7 video fixture from the engine repository
root with Node 22.6 or later:

```powershell
node --experimental-strip-types --test tools/visualization_fixture/scene.test.ts
node --experimental-strip-types tools/visualization_fixture/render_fixture.ts
```

The renderer requires the local native CLI at `build/tools/audio_analysis_cli/`
and Edge at the default Windows path. Override `SHIPGLOWS_EDGE_PATH` or
`SHIPGLOWS_DART_PATH` when the local installations differ. It creates a six
second float WAV, writes and validates music and speech sidecars, fetches records
randomly, compares all 180 TypeScript scenes with Dart to `1e-5`, and muxes one
WebM audio track from that same WAV. It decodes the resulting WebM and writes
four PNG review frames beside the output under `build/visualization_fixture/`.

This is a private fixture renderer at 640x360/30fps. Its palette, generated WAV,
container and quality are test inputs, not product decisions or guarantees. It
does not prove calibrated physical audio-to-photon timing, production exporter
integration, hour-scale resource behavior, A0 readiness, or a frozen schema.
