# SharedSceneView Windows Profile review — 2026-10-08

## Scope and verdict

This is a standalone renderer profile of the actual `SceneEvaluator` and
`SharedSceneView` on the named Windows machine. It adds frame evidence that the
A0 audio diagnostic cannot provide because that diagnostic uses its own private
`AnalysisPainter`.

**Renderer profile captured; this does not change Stage A8's NOT READY verdict.**
The run uses synthetic transformed scene records and does not exercise audio,
the native bridge, `AnalysisController` polling, calibrated audio-to-screen
timing, or allocation-event accounting. It is not an OFF/ON audio comparison.

## Setup

- Windows 11 Pro, build 26300; AMD Ryzen 7 7730U, 16 logical processors.
- Flutter 3.47.1 / Dart 3.13.1, Windows Profile mode, Impeller OpenGLES.
- Application window: 1264 × 681 logical pixels.
- CMake cache: `SHIPGLOWS_AUDIO_A0=OFF`.
- Built and launched with `flutter run --profile --target
  tool/shared_scene_profile.dart -d windows --no-pub`.
- Each scene warmed for 2 seconds, then measured for 10 seconds. The harness
  precalculates synthetic transformed records and updates the real evaluator and
  shared widget about every 33 ms. `FrameTiming` samples include Flutter build,
  raster and total-span values.
- A separate PowerShell sampler sampled the process every ~500 ms for 39.2 s,
  spanning startup and all three phases. The process exited after writing its
  result. The companion JSON retains all 75 process samples, scene summaries,
  and source/binary SHA-256 values.

## Results

| Stage A scene | Updates / frames | Build p95 / max | Raster p95 / max | Total span p95 / max |
| --- | ---: | ---: | ---: | ---: |
| Spectrum bars, 64 bars | 303 / 304 | 0.664 / 2.628 ms | 3.055 / 10.125 ms | 5.157 / 21.376 ms |
| Music oscilloscope, 64 spans | 303 / 305 | 0.418 / 0.639 ms | 2.452 / 3.332 ms | 4.220 / 5.845 ms |
| Speech envelope, 150 bins | 303 / 306 | 0.280 / 0.364 ms | 0.937 / 1.372 ms | 2.838 / 3.361 ms |

The measured total-span p95 is below 16.7 ms for each scene in this run. The
spectrum-bars maximum reached 21.376 ms, so the tail is retained and not hidden
by the percentile. Process working set peaked at 107,945,984 bytes; private
bytes peaked at 153,591,808 bytes; whole-process CPU averaged 18.49% of one core
over the 39.2 s sample. These process figures include startup and Flutter
runtime overhead; they are not feature-owned memory or isolated painter CPU.

## Readiness limits

- `FrameTiming` is renderer frame evidence, not a Dart allocation-event count
  or a before/after improvement measurement.
- No audio endpoint, callback period, underruns, analysis RTT or source-frame
  lag was exercised. It does not fill the A8 OFF/ON evidence gaps.
- AC-A4 retained/incoming allocation overlap remains unproven.
- Physical audio-to-screen latency and its uncertainty remain unmeasured.
- The API and defaults remain private and provisional; product integrations
  remain out of this stage.

**Disposition:** useful shared-renderer runtime evidence, with no change to the
Stage A8 verdict. See the JSON companion for complete metrics, sample series,
and hashes.
