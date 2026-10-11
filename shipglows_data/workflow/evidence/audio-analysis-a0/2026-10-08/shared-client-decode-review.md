# Shared client decode review — 2026-10-08

## Change and scope

The private shared visualization package now decodes an `AnalysisBatch` into
read-only float32 views backed by one payload store. The public standalone
record and batch decoders copy input once to preserve independence from caller
mutation. The `AnalysisController` uses the explicit transferred-buffer path
for its local source response; `AnalysisSource` implementations must not mutate
or reuse the `records` payload after returning it. The controller retains the
existing all-or-nothing metadata, payload, record and continuity checks before
committing its cursor.

This removes per-record payload slices, per-field `List<double>.generate` and
immutable-list copies from the decode path. It is a source and test result, not
a measured allocation-event or memory-overlap result. Field validation still
walks each view; validation, wrappers and object overhead still allocate.

## Verification

- `flutter analyze` for `flutter/shipglows_visualization`: clean.
- `flutter test` for `flutter/shipglows_visualization`: 24 tests pass,
  including input-copy independence and immutable decoded views.
- The engine example CMake cache remains `SHIPGLOWS_AUDIO_A0=OFF`.
- No build or runtime command used Doppler.

## Readiness limits

This does not prove the 256 KiB retained plus 128 KiB incoming AC-A4 client
allocation cap, peak decode/install overlap, allocator throughput or the
worst-case music11 batch16 path under a measured allocator. It does not revise
the same-endpoint Stage A8 OFF/ON measurements. That review remains
**NOT READY**: the lag proxy max is 108 ms, RTT max is 23.605 ms, physical
audio-to-screen lag/uncertainty is unmeasured, and AC-A4 overlap is unproved.
The shared renderer Profile sample is synthetic and has no audio or bridge.

The CMake cache at
`flutter/shipglows_audio/example/build/windows/x64/CMakeCache.txt` was checked
as `SHIPGLOWS_AUDIO_A0=OFF`. No product integration, exporter, public schema
freeze or default change is included.
