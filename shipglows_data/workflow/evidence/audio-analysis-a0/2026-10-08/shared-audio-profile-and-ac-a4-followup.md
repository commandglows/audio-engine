# Shared audio path and AC-A4 follow-up — 2026-10-08

## Disposition

**No new end-to-end Windows Profile OFF/ON measurement was completed. Stage A8
remains NOT READY and AC-A4 remains OPEN.** This follow-up preserves the
existing measurements and records the exact missing proof. No calibrated
equipment was acquired or used. No schema/default freeze or product integration
was performed.

## Initial state and commands

- `git status --short`: worktree was already extensively modified and contained
  untracked Windows/visualizer sources and evidence. Branch was
  `codex/android-capture-levels`, HEAD
  `67e3a0594ae92c8cf0d97f90fd274147acff6012`.
- Read
  `flutter/shipglows_audio/example/build/windows/x64/CMakeCache.txt`:
  `SHIPGLOWS_AUDIO_A0:UNINITIALIZED=OFF`.
- Attempted
  `flutter test integration_test/shared_visualization_integration_test.dart -d windows --profile --no-pub --dart-define=SHIPGLOWS_AUDIO_A0_ENABLED=false`.
  Flutter 3.47.1 rejected `--profile` (exit 64), so this is not a runtime
  result. The integration test uses a 2-second silent WAV and does not report
  frame/bridge performance metrics.
- Ran
  `flutter run --profile --target tool/shared_scene_profile.dart -d windows --no-pub`.
  Build succeeded using the existing OFF cache. After its synthetic renderer
  phases, the process was stopped; the final output reported
  `Lost connection to device` during shutdown. Renderer phase data was
  emitted before that message.
- The required engine commands ran directly. Doppler was not used.

## Profile renderer rerun

The harness updates synthetic precomputed records and invokes the real
`SceneEvaluator`/`SharedSceneView` every 33 ms. It does not load playback,
open WASAPI, receive a MethodChannel batch or poll an `AnalysisController`.
Windows Profile, 1264x681 logical pixels, Impeller OpenGLES:

| Scene | Frames | Build p95 / max | Raster p95 / max | Total span p95 / max |
| --- | ---: | ---: | ---: | ---: |
| Spectrum bars | 305 | 0.445 / 0.780 ms | 1.987 / 3.596 ms | 3.808 / 5.459 ms |
| Music oscilloscope | 304 | 0.711 / 0.956 ms | 4.090 / 8.335 ms | 6.974 / 13.381 ms |
| Speech envelope | 304 | 0.427 / 0.556 ms | 1.369 / 1.881 ms | 3.144 / 4.428 ms |

This is only a renderer harness repeatability check. It has no paired OFF/ON
audio runs, callback period, endpoint underruns, real-batch coverage, RTT, lag,
or per-process CPU/memory sampler. It does not extend the prior renderer
evidence or change A8.

## Real shared path evidence available

`integration_test/shared_visualization_integration_test.dart` starts the real
Windows playback plugin with a generated 2-second silent stereo WAV, starts
`MethodChannelAnalysisSource`, and lets `AnalysisController` own the lease and
polling. It mounts `SharedSceneView`, asserts retained records and live-or-ended
controller state, then checks the view semantics label. It passed once in
Windows Debug in this follow-up; the installed Flutter CLI still has no
`flutter test --profile` mode, so this is not the requested Profile comparison.

The existing `stage-a8-windows-profile-review.md` remains the qualified
same-endpoint, same-configuration comparison: Bose QC35 II, 10 ms period,
mask 11, separate 600 s Profile OFF and ON runs. It reports callback p99
8.191 ms and zero underruns; ON RTT max 23.605 ms, submitted-frame lag proxy
max 108 ms, one no-forward interval of 144.571 ms, CPU 56.05% of one core, and
record coverage 1.0. Its private diagnostic painter is not `SharedSceneView`.
Those metrics are silent synthetic A0 evidence, not physical timing or proof
for the shared client path.

## AC-A4: exact proof gap

AC-A4 scopes 256 KiB retained plus 128 KiB incoming. The prior actual-channel
capture in `native-transport-runtime.json` delivered adjacent 16-record
responses while retaining a 150-hop context; its selected identity-deduplicated
snapshot graph peaked at 284,880 bytes, including 109,824 external bytes, with
109,809 encoded envelope bytes. This is a parked snapshot, not the transient
peak and not a cap pass. The decoder's `decodeTransferred` change reduces copies
structurally but has no allocator measurement.

The current evidence cannot prove the required high-water because it does not
continuously observe and identity-deduplicate all co-resident feature-owned
objects and external stores across these transitions:

1. before and after MethodChannel response receipt (encoded envelope and codec
   map/key/list wrappers);
2. transferred decode (batch/list/record wrappers, float32 views and backing
   store, with the prior retained view still live);
3. installation into the controller (incoming records plus old and new latest
   records/history, including the maximum applicable speech context);
4. publication through `SpeechWindow`/`SceneState` to the view (shared backing
   storage counted once, plus all wrapper/map/list overhead still retained).

The required workload shapes have now been exercised in software. The actual
channel capture for `music11` returned an adjacent 16-record lot while a
150-hop prior context was retained; that lot was decoded and parked, not
installed. A dedicated real-path Windows Profile exercise then returned and
installed an exact 16-record `music11` lot (103,872 payload bytes) through the
controller while a reference to the prior published `SceneState` remained
live. It published a new live scene with zero resyncs or skipped records. The
separate speech exercise filled the real controller history to its maximum
166 records, then accepted newer channel records while the retained history
stayed at 166 and the scene remained live. The paired ON music Profile phase
ran `music11` through the mounted shared view for about 8.5 seconds (1,044
accepted records; zero resyncs, skipped records, PCM gaps, or source errors).
The exact `music11 × 16` installation with the old published scene retained
was completed in the same Profile process.

The remaining proof gap is a coherent, phase-marked high-water accounting of
the same feature-owned object cohort at all four boundaries. There is no
instrumentation currently recording identity-deduplicated roots for the
MethodChannel envelope/map/list, transferred batch, old and new controller
records/history, and published `SpeechWindow`/`SceneState` while also resolving
the external typed-data backing stores. The Windows VM captures available here
are point-in-time heap graphs; the selected parked graph excludes the precise
installation/publication instant. Allocation traces report class samples or
totals, not the live identity union. Process RSS/private bytes and payload
lengths cannot supply that missing cohort or separate retained from incoming
bytes. Therefore the 256 KiB retained + 128 KiB incoming peak is still
unproved and AC-A4 stays open.

The actionable next step is a dedicated observer that marks receipt, decode,
install, and publish and retains explicit roots long enough to map each stage's
object IDs to shallow sizes and external backing stores, with shared stores
deduplicated once and wrappers/maps included. It must report observer-induced
retention/overhead and demonstrate that the measured union bounds production
co-residency. Until such evidence exists, no heap snapshot, working-set sample,
payload sum, or class trace closes AC-A4.

Even a complete software path run is not a physical audio-to-photon/acoustic
timing measurement. The user has chosen to proceed without calibration hardware;
no purchase is needed for this software work, while physical timing and
unconditional Stage A8 readiness remain open.

## Software integration follow-up

The existing Windows integration test was tightened to mount
`SharedSceneView`, let `AnalysisController` own the single MethodChannel lease
and polling path, and check that the mounted scene's semantic label tracks the
controller state. This removes its duplicate direct batch request, which had
read outside the controller and obscured which path the assertion covered.

Command:

```powershell
dart format integration_test/shared_visualization_integration_test.dart
flutter test integration_test/shared_visualization_integration_test.dart -d windows --no-pub
```

Result: Windows Debug integration passed (1 test). The real Windows plugin and
MethodChannel returned analysis records from silent playback; the controller
retained records and the mounted shared view exposed the controller's state
label. The first compile attempt failed on missing `flutter/widgets.dart`
symbols; adding the import fixed the build, and the rerun passed. This verifies
software wiring only: the WAV contains digital silence, Debug has no valid
Profile timing comparison, and this run collected no endpoint period,
underrun/CPU/RTT/lag/memory series or allocation high-water. AC-A4 and Stage A8
dispositions are unchanged.

Additional checks after the integration-test edit:

- `flutter analyze` in `flutter/shipglows_visualization`: clean.
- `flutter test` in `flutter/shipglows_visualization`: all 24 tests pass.
- `flutter analyze` in `flutter/shipglows_audio/example`: clean after replacing
  two redundant nullable assertions reported on the first analysis pass.
- Re-ran the Windows Debug integration command above after that cleanup: 1 test
  passes.

## Static and zero-intensity lifecycle follow-up

The controller now avoids acquiring or polling analysis while the scene is
static or intensity is zero. Selecting static retains the latest validated
record and complete prepared speech window, labels the continuity-local source
time, and releases the lease. Missing input yields a labeled unavailable
baseline. Returning to reactive motion reacquires and warms up. A failed lease
release remains visible as `lease_release_uncertain`; polling stays disabled
and a subsequent static configuration retries the release.

Commands and results (Doppler NONE):

```powershell
dart format lib/src/analysis_controller.dart lib/src/scene.dart lib/src/painters.dart test/shared_visualization_test.dart
flutter analyze
flutter test
flutter analyze # from flutter/shipglows_audio/example
flutter test integration_test/shared_visualization_integration_test.dart -d windows --no-pub
Select-String -Path build/windows/x64/CMakeCache.txt -Pattern '^SHIPGLOWS_AUDIO_A0:'
```

The visualization package analysis is clean and all 28 tests pass. Example
analysis is clean. The Windows Debug integration passes (1 test) through the
real plugin, MethodChannel, controller and mounted SharedSceneView. Cache
remains `SHIPGLOWS_AUDIO_A0:UNINITIALIZED=OFF`. The integration uses silent
playback; this establishes software wiring and lifecycle behavior only. It is
not Profile performance or physical timing. No process CPU/RTT/lag/memory
series or allocator high-water was collected. AC-A4 remains OPEN and Stage A8
remains NOT READY.

## Scene parity and deterministic particle follow-up

An implementation audit found the Dart and TypeScript particle generators used
a different uint32 hash from the formula specified in the draft. Both now use
the specified wrapped LCG/xor sequence, with a shared known-coordinate vector
in their unit tests. The A7 Dart parity reader also sorted selected speech
records by cache insertion order; random frame access could therefore add a
non-contiguous history. It now orders records by feature index before building
each speech window.

Commands and results:

```powershell
flutter analyze
flutter test
node --experimental-strip-types --test tools/visualization_fixture/scene.test.ts
node --experimental-strip-types tools/visualization_fixture/render_fixture.ts
```

Flutter analysis is clean; all 29 package tests and all 8 TypeScript tests pass.
The local A7 fixture renderer completed 180 frames at 30 fps, with maximum
Dart/TypeScript scene delta 0, a decoded 640x360 WebM containing one audio track
and one video track, and the generated final WAV as the sole audio source.
Source WAV SHA-256: `a135163a62da1ad1fcb8091b1ec3fb914f5c15a4d6ee90848f002967d1d56f7d`;
WebM SHA-256: `4b2ba6f6f0a11b993b46d34d8c43f2be10fa07728af0c0a49e8527a2a6ba3539`.
The artifact is a private generated fixture, not a product export or physical
timing result. It does not measure Windows Profile OFF/ON behavior or AC-A4
allocation overlap; those dispositions remain unchanged.

## Pause rendering follow-up

The scene evaluator previously treated `paused` as an empty status and the
shared painter therefore cleared the last frame. It now keeps painting the
last validated transformed record while paused, labels the semantic state, and
the controller captures the explicit scene time at the pause transition. Scene
only updates while paused keep that captured time; resume clears it. Static
motion still takes precedence and keeps its separate frozen feature semantics.

Validation after this change: `flutter analyze` clean, all 30 visualization
package tests pass, and all 9 TypeScript fixture tests pass. The controller
test pauses a real decoded music batch, advances configured scene time, and
checks that the rendered particles remain identical. The painter widget test
exercises the paused state. `flutter analyze` in the audio example is clean and
the Windows Debug `flutter test integration_test/shared_visualization_integration_test.dart -d windows --no-pub`
passes (1 test) through the native plugin, MethodChannel, controller and
SharedSceneView. The generated CMake cache still reports
`SHIPGLOWS_AUDIO_A0:UNINITIALIZED=OFF`.

The A7 fixture renderer was rerun against this source state:

- Command: `node --experimental-strip-types tools/visualization_fixture/render_fixture.ts`
- 180 frames at 30 fps; Dart/TypeScript maximum scene delta 0.
- Decoded private 640x360 WebM: one video track, one audio track, sole audio
  source is the generated final WAV; duration 6.073321 seconds.
- Source WAV SHA-256:
  `a135163a62da1ad1fcb8091b1ec3fb914f5c15a4d6ee90848f002967d1d56f7d`.
- WebM SHA-256:
  `627c343a1d903333292e2dbe0cd9a59a5b6dd85cf16f8881af0fcd6229211360`.

This closes the paused software-rendering defect and confirms offline fixture
parity. It remains separate from a live Windows Profile OFF/ON comparison,
physical timing, product integration, and AC-A4 allocation high-water.

## Measured viewport validation follow-up

The previous `SceneConfig.aspectRatio` was range-checked but never affected
evaluation or painting. It has been removed. `SceneViewport` now validates the
measured Flutter logical dimensions (finite, positive, at most 8192 per side,
aspect ratio 0.25 through 4), and applies the geometry padding constraint.
`SharedSceneView` measures its actual layout constraints; an unsupported
viewport suppresses scene painting and exposes an explicit unavailable
semantics label. The Windows integration assertion now locates that accessible
label directly.

Validation after the viewport change:

- `flutter analyze` and `flutter test` in `flutter/shipglows_visualization`:
  clean; all 32 tests pass, including ratio bounds, unsupported viewport
  semantics, pause, and static/I0 lifecycle.
- `dart analyze tool/scene_fixture_parity.dart`: clean.
- `node --experimental-strip-types --test tools/visualization_fixture/scene.test.ts`:
  all 9 tests pass.
- `flutter analyze` in `flutter/shipglows_audio/example`: clean.
- `flutter test integration_test/shared_visualization_integration_test.dart -d windows --no-pub`:
  Windows Debug integration passes (1 test) through playback/plugin/channel/
  controller/SharedSceneView and accessible label.
- CMake cache remains `SHIPGLOWS_AUDIO_A0:UNINITIALIZED=OFF`.

The same audit found `SceneConfig` only checked whether required feature bits
were present. It now rejects every profile/mask pair outside music 3/11 and
speech 5/13 before a controller can request a lease. Tests cover invalid mixed
pairs and valid supported pairs. The visualization package remains analyze
clean and now has 33 passing tests.

Re-ran the private A7 renderer on this source state. It completed 180 frames at
30 fps with maximum Dart/TypeScript state delta 0; the decoded 640x360 WebM
contains one video track and one audio track sourced only from the generated
final WAV (duration 5.956044 seconds). Source WAV SHA-256:
`a135163a62da1ad1fcb8091b1ec3fb914f5c15a4d6ee90848f002967d1d56f7d`; WebM
SHA-256: `624d5c40269c8a54730bd5e8de6197eea8cff76dc57ddcb814438d3cafdd7de2`.

The Windows fixture is Debug and digitally silent. None of these checks is a
Profile comparison, physical timing result, product integration or AC-A4
allocation high-water. AC-A4 remains OPEN and Stage A8 remains NOT READY.

## Nonzero Windows shared-path integration

The integration fixture now writes a deterministic 440 Hz, 0.45-amplitude,
stereo signed-16-bit PCM WAV and plays it through the Windows playback plugin.
The same test opens the native analysis lease through
`MethodChannelAnalysisSource`, polls with `AnalysisController`, mounts
`SharedSceneView`, and asserts the published waveform differs from its fixed
baseline. This guards the real software path against a test that only passes
because valid silent batches were delivered.

Commands and results:

```powershell
flutter analyze
flutter test integration_test/shared_visualization_integration_test.dart -d windows --no-pub
```

Example analysis is clean; the Windows Debug integration passes (1 test). The
test polls until sequence 32, reached accepted sequence 35 in the latest run,
and observed playback state `playing`, zero underruns, and non-baseline shared
waveform geometry. This confirms multiple actual MethodChannel batch replies
cross the controller into the shared view. An initial compile caught incorrect
test-only field names in the waveform assertion; those were corrected before
the passing run. CMake emitted existing GoogleTest minimum-version
deprecation warnings. This is digital software-path evidence: it does not
collect Profile CPU/RTT/lag/memory series, device physical audio-to-screen
timing, or the AC-A4 temporal allocation peak. AC-A4 remains OPEN and Stage A8
remains NOT READY.

## In-flight poll shutdown fence

`AnalysisController.stop()` now invalidates the lifecycle epoch, detaches the
lease from new polls, and waits for both a pending lease opening and any
in-flight batch request before native lease release. A lease opened after stop
begins is released by the stale acquisition path; a late poll cannot advance
the cursor or republish retained data. Gated controller tests hold each phase,
verify no overlapping release, then check the lease is released once and the
controller remains off with an empty history/cursor.

Validation: `flutter analyze` clean; all 35 visualization package tests pass.
The Windows Debug real playback/plugin/MethodChannel/controller/view
integration also passes (1 test) on this code. CMake cache remains
`SHIPGLOWS_AUDIO_A0:UNINITIALIZED=OFF`. This is controller ordering proof; it
does not measure native callback quiescence duration, Profile performance, or
the AC-A4 memory high-water.

## Controller acquisition and release concurrency

The controller now coalesces same-configuration concurrent starts and shares
an in-progress stop future, preventing duplicate native lease acquisitions or
overlapping releases. An explicit stop waits for a mode-transition release
already in flight. A scene-update revision fence prevents a delayed mode
transition from publishing a warming/static state after a newer stop.
It also cancels a queued restart when stop arrives during a lease configuration
change.
Deterministic tests cover concurrent starts, concurrent stops, a mode-change
release held behind a gate, retry after stale-lease release failure, and
cancellation of a queued restart. A widget test swaps the scene notifier and
proves `SharedSceneView` follows the replacement while detached from the old.

Commands and results:

```powershell
flutter analyze
flutter test
flutter test integration_test/shared_visualization_integration_test.dart -d windows --no-pub
```

Visualization package analysis is clean and all 43 package tests pass. The
Windows Debug integration passes with a generated nonzero 440 Hz WAV and
asserts non-baseline waveform geometry at `SharedSceneView`. CMake retains
`SHIPGLOWS_AUDIO_A0=OFF`. This proves controller ordering and nonzero software
signal propagation only; it does not establish Profile performance, physical
audio-to-screen timing, or AC-A4 retained/incoming allocation high-water.
AC-A4 remains OPEN and Stage A8 remains NOT READY.

## Unbounded viewport layout safety

A widget regression test placed `SharedSceneView` in a vertical scroll view.
The original invalid-viewport branch still passed `Size.infinite` to
`CustomPaint`; Flutter reproduced an infinite-height `RenderCustomPaint`
layout assertion. Unsupported constraints now create a finite 1×1 semantic
placeholder with no scene painter. The unavailable label remains exposed, and
bounded supported viewports retain the normal painter.

Commands and results:

```powershell
flutter analyze
flutter test
flutter analyze # audio example
flutter test integration_test/shared_visualization_integration_test.dart -d windows --no-pub
```

Visualization package analysis is clean and all 43 package tests pass. The
focused regression now reports the unavailable semantics label with no layout
exception; example analysis is clean and the Windows Debug tone integration
passes (1 test). CMake emitted existing GoogleTest minimum-version
deprecation warnings. This is layout/software evidence only; no Profile,
physical timing or AC-A4 measurement. AC-A4 remains OPEN and Stage A8 remains
NOT READY.

## I0 speech baseline validity metadata

The fixed speech baseline previously marked all 150 generated flat bins as
valid, which could be misread by a downstream consumer as measured history.
Baseline bins now use `valid=false`; `SceneState.validSuffixCount` remains the
separate count from actual retained history when available. The painter still
draws one fixed centerline and does not use those bin flags for I0 geometry.

Validation: `flutter analyze` and all 43 package tests pass; example analysis
is clean; Windows Debug nonzero-tone integration passes (1 test). CMake remains
`SHIPGLOWS_AUDIO_A0=OFF`. This is software semantics validation only; Profile
timing, physical timing, and AC-A4 overlap remain unmeasured. AC-A4 remains OPEN
and Stage A8 remains NOT READY.

## Shared audio path Windows Profile comparison (supersedes initial harness gap)

The earlier statement that no end-to-end Profile comparison was completed is
superseded by this software-only harness run. `example/tool/shared_audio_scene_profile.dart`
plays a generated 48 kHz stereo 440 Hz WAV on the default multimedia output.
OFF runs playback alone; ON adds the real MethodChannel source,
`AnalysisController` with music mask 11, and a mounted `SharedSceneView`. Both
phases use the same default output selection, transform, and Profile process.
The endpoint ID is not written to the report. Each phase warms for 2 seconds
and samples for about 8 seconds.

| Measurement | OFF | ON |
| --- | ---: | ---: |
| WASAPI period / buffer | 10 ms / 1,056 frames | 10 ms / 1,056 frames |
| Callback p99 / underruns | 4.095 ms / 0 | 4.095 ms / 0 |
| Accepted sequence / returned records | 0 / 0 (OFF) | 1,044 / 1,044 |
| Delivery resync / skipped / PCM gap / source errors | 0 / 0 / 0 / 0 | 0 / 0 / 0 / 0 |
| Submitted-frame lag proxy p50 / p95 / p99 / max | N/A | 50 / 70 / 70 / 70 ms (33 samples) |
| MethodChannel RTT p50 / p95 / p99 / max | N/A | 0.317 / 0.556 / 0.922 / 1.512 ms |
| FrameTiming p95 / max | N/A | 3.761 / 4.685 ms |
| Process CPU average / peak, one core | 19 / 35% | 32.53 / 53% |
| Process RSS start / end / peak | 114.7 / 114.9 / 114.9 MB | 128.9 / 131.8 / 132.8 MB |
| Process private bytes peak | 172.0 MB | 181.0 MB |

The shared view was mounted before the ON measurement; the ON controller
reported `live` and accepted the listed sequence range. An earlier harness run
mounted it only after measurement and is superseded by these results. CPU, RSS,
working set, and private bytes cover the full Flutter process and include
Flutter/runtime and sampler overhead. The submitted-frame proxy is playback
position frame minus latest feature center frame; it is not acoustic or photon
lag. This short paired run is software path
evidence; it does not replace the existing 600-second A8 decision run, measure
physical acoustic-to-screen timing, or establish AC-A4 allocation overlap.
The exact run output is retained in
[`shared-audio-scene-profile.json`](shared-audio-scene-profile.json). The
PowerShell process sampler is `example/tool/shared_audio_process_sample.ps1`.

**Verdict unchanged: A8 NOT READY; AC-A4 OPEN.**

The same Profile process also exercised speech mask 5 at the maximum controller
history: 166 retained records, then subsequent incoming records through
sequence 182 while history stayed at 166 and scene status stayed `live`. The
actual MethodChannel speech payload views began at byte offsets 411 and 413,
which exposed the float32-alignment defect described below. This context run
exercises retained state plus newer publication; it is not an allocation peak.

## Unaligned MethodChannel typed-data correction

The first speech Profile attempt failed on a channel `Uint8List` view whose
buffer offset was 453: constructing float32 views raised `RangeError` because
the address was not four-byte aligned. `VisualRecordFloat32Store.transfer`
now adopts aligned buffers and makes one owned alignment copy for unaligned
views. A regression test exercises an offset-one transferred speech record.
After the fix, the actual Windows Profile speech stream delivered 166 retained
records and stayed `live` through newer records; the paired `music11` run also
completed with the shared view mounted. The same Profile run installed 16
music11 records (103,872 payload bytes) while holding the old published scene.
This alignment copy is a correctness
fallback and a structural copy reduction, not an AC-A4 allocation measurement.

Validation: plugin `flutter analyze` and 9 tests pass; visualization
`flutter analyze` and 44 tests pass; example `flutter analyze` is clean. The
Windows Debug integration passed with accepted sequence 34, playback `playing`,
zero underruns, and scene `live`. The final Profile harness compiled and
completed both phases with its mounted view, 16-record and full speech-context
exercises using `flutter run --profile --target
tool/shared_audio_scene_profile.dart -d windows --no-pub`. In the paired run,
callback p99 was 4.095 ms with zero underruns in both phases; ON had 33 lag
proxy samples, p50 50 ms, p95/max 70 ms; RTT p99 0.922 ms; FrameTiming p95
3.761 ms; process CPU averaged 32.53% of one core. These are software values,
not physical audio-to-screen timing. The example CMake cache remains
`SHIPGLOWS_AUDIO_A0=OFF`; Doppler was not used. CMake emitted GoogleTest
minimum-version deprecation warnings.
