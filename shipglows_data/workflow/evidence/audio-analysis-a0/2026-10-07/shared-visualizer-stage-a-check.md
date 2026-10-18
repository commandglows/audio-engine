# Shared visualizer Stage A check — 2026-10-07

## Implemented paths

The private Flutter package contains immutable transformed records, compact
record decoding and strict batch validation, a single-flight bounded-history
controller, speech-window preparation, stateless scene evaluation, and shared
painters. Stage A covers bars, music waveform, and speech envelope. Stage B
radial and deterministic particle geometry is also present as a separate
renderer path.

`SharedSceneView` now reuses painter `Paint` handles and cached semantic colors
across per-element draw loops, removing repeated `Paint` construction for bars,
music/speech waveform samples, radial geometry and particles. The live
controller retains one current full record and a fixed 166-hop speech ring
(5,312 bytes of typed-array slot storage); published speech scenes expose the
latest 150 contiguous hops. A wraparound controller test fills the full window.
Flutter analyze and all 23 package tests pass. The ring gives a structural
retention bound; painter `Paint` reuse is source-level allocation reduction.
The separate Windows Profile FrameTiming run below measures actual renderer
frames, but not allocation-event throughput. The decoded incoming batch and
scene projection overlap have not been measured against AC-A4.

`MethodChannelAnalysisSource` connects the draft v2 Windows playback-analysis
bridge to the controller. `flutter test integration_test/shared_visualization_integration_test.dart -d windows`
passes with a generated silent WAV through Windows playback, native analysis,
binary channel transport, decoding, and the controller. The opt-in
`windows_a0_diagnostic_integration_test.dart` start/poll/stop path also passes
when built with `SHIPGLOWS_AUDIO_A0=ON`; the generated CMake cache was restored
to OFF.

The private offline path now includes bounded WAV analysis, paged sidecar
writer and strict random-access reader, a manifest CLI with cancellation
cleanup and atomic no-replace publication, and an A7 fixture renderer. The
fixture checks all 180 global frames across all scene modes against the Dart
evaluator (maximum delta 0), then muxes the same generated final WAV into a
640x360, 30fps, 6-second WebM. Local Edge decoding and four sampled frames were
reviewed. Artifact: `build/visualization_fixture/final-reviewed.webm`,
SHA-256 `9becbf807c7a151adfb4fce9d281ea0f5d9f4798e355c323c9a43368dfe6bf6f`.

## Current validation

Flutter analyze and 23 package tests pass; six focused CTest targets pass;
seven TypeScript fixture tests and strict TypeScript checking pass. The default
A6 synthetic scale smoke covers 6s/180s music and speech. A one-hour speech
run published 360,000 records, 3,600 chunks and 15 pages in 24.728s; process
peak working set was 6,676,480 bytes under an 8 MiB analyzer/writer budget.
This is offline synthetic input and process working set, not total allocation
throughput, live device memory, or production cost evidence.

The standalone Windows Profile harness also exercised the actual
`SceneEvaluator` and `SharedSceneView` with precomputed synthetic scenes at
about 30 updates/s: 304 spectrum, 305 music-wave and 306 speech-wave frames.
FrameTiming total-span p95 was 5.157 ms, 4.220 ms and 2.838 ms respectively;
the spectrum-bars maximum was 21.376 ms. Whole-process working set peaked at
107,945,984 bytes and CPU averaged 18.49% of one core during the 39.2 s run.
This fills the shared-renderer runtime observation gap only; it does not measure
audio/bridge load, allocations or physical timing. Details and hashes:
[`shared-scene-view-profile-review.md`](../2026-10-08/shared-scene-view-profile-review.md)
and its JSON companion.

## Remaining gates

The operator accepted the A0 decision for the current practical scope with an
explicit measurement limitation. The measured Stage A8 review is complete, with
the verdict **NOT READY for an unconditional Stage A runtime-readiness claim**.
The named Windows Profile comparison records callback p99 below the 10 ms
endpoint period and zero OFF/ON underruns. It also records ON RTT max 23.605 ms,
submitted-frame lag proxy max 108 ms and one no-forward interval of 144.571 ms.
Process CPU/working-set snapshots do not isolate FFT cost or establish flat
memory; feature-owned AC-A4 retained/incoming allocation overlap is unproved.
Calibrated physical audio-to-photon lag and uncertainty remain null. The A0
diagnostic renders its private `AnalysisPainter`, not the shared package's
`SharedSceneView`; its endpoint figures do not profile the shared painter. A
separate synthetic SharedSceneView profile is linked above, but does not close
the audio-path or readiness gates. See
[`stage-a8-windows-profile-review.md`](../2026-10-08/stage-a8-windows-profile-review.md)
and its JSON companion for endpoint identity, periods, metrics, provenance and
limits.

The one-hour offline smoke remains synthetic. BeatGlows and ContentGlows
exporter and product integrations, authorization/retention policy, product
placement and palette bindings, and public schema/default freeze remain
separate work. The current API and defaults remain private draft; this review
does not make a production readiness claim.
