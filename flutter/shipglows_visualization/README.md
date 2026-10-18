# ShipGlows Visualization

Private Flutter package for shared, source-neutral audio visualization.
It consumes immutable, already-transformed `VisualRecord` values; it does not
capture audio, run DSP, smooth features, or own a product palette.

Stage A scenes are music spectrum bars, a music oscilloscope, and a rolling
speech envelope. Stage B adds deterministic radial geometry and seeded 2D
particles. `VisualRecordCodec` decodes compact records, and
`AnalysisBatch.decode` validates a complete batch before exposing a cursor
update. `AnalysisController` provides single-flight polling and lease
acquisition/release, atomic cursor commits, resync handling, and bounded client
history over an `AnalysisSource` adapter. `SceneEvaluator` is stateless. The
live controller retains one current record and a fixed 166-hop speech ring;
each published speech view uses the
latest 150 contiguous transformed hops. The ring stores two uint64 positions
and four float32 levels per hop (32 bytes). `SpeechWindow.fromHistory` remains
a bounded fixture/helper path and only interpolates the common contiguous
suffix.
`SceneConfig` rejects unsupported profile/mask pairs before lease acquisition
(music 3/11, speech 5/13); each mode checks its required transformed data.
Selecting static motion freezes the current validated record and complete
speech window, labels its continuity-local source time, and releases the
analysis lease. Static mode without a current record shows a labeled
unavailable baseline. Zero intensity draws its fixed baseline without
acquiring or polling analysis; returning to reactive motion reacquires a fresh
lease and warms up.
`tools/visualization_fixture/scene.ts` is a private TypeScript mirror of the
scene evaluator and prepared speech windows. Its A7 runner reads native offline
sidecars at random frame positions, checks every Stage A frame against the Dart
evaluator, and renders a local sole-WAV fixture WebM. It does not analyze or
smooth audio, and does not connect to a product exporter.
`SharedSceneView` paints all four scene families and accepts six semantic
colors and product-owned geometry tokens from its caller.
It validates the measured logical viewport (dimensions up to 8192 and aspect
ratio 0.25–4) together with padding; unsupported layouts do not paint a scene
and expose an unavailable semantics label from a finite-size placeholder, so
unbounded scroll layouts remain safe.
Its painter retains reusable `Paint` handles and cached semantic colors across
bars, waveform samples, spokes, rings and particles instead of allocating a
`Paint` for each drawn element.

`MethodChannelAnalysisSource` implements the draft v2 lease protocol for the
Windows playback output source. It queries capabilities without activating
capture, hashes the canonical transform config, and exchanges bounded binary
batches through the `shipglows_audio` channel. The native Windows plugin taps
PCM after playback DSP and after WASAPI reports submission.

The API is still draft. A Windows integration test exercises a generated
nonzero 440 Hz stereo PCM WAV through playback, native analysis, the MethodChannel
adapter, batch validation, the shared controller, and `SharedSceneView`; it
asserts non-baseline waveform geometry after at least 32 accepted feature
sequences and checks playback underruns. The native engine now has a private bounded WAV
analyzer, paged sidecar writer, and strict random-access sidecar reader. A local
draft manifest runner adds cancellation cleanup and atomic no-replace package
publication. It assumes the caller already authorized a private job root; it
does not supply server identity or tenant authorization. Fixture video,
BeatGlows/ContentGlows exporter integrations, calibrated presentation timing
and A0 readiness remain open. The fixture video proves offline scene parity and
local mux behavior only; it does not establish physical audio-to-photon timing
or production readiness.

Run package checks from this directory:

```powershell
flutter analyze
flutter test
```

The standalone Windows Profile renderer harness is in
`flutter/shipglows_audio/example/tool/shared_scene_profile.dart`. Run it from
the example directory with:

```powershell
flutter run --profile --target tool/shared_scene_profile.dart -d windows --no-pub
```

It paints three synthetic Stage A scenes for 10 seconds each after a 2-second
warmup and prints per-frame timing summaries. It has no audio source and does
not measure allocation events, bridge cost, or physical timing; see the dated
profile review under `shipglows_data/workflow/evidence/audio-analysis-a0/`.
