# Stage A8 Windows Profile readiness review — 2026-10-08

## Verdict

**Review complete; Stage A8 readiness is not accepted.** The Stage A source,
offline path, fixture and Windows diagnostic are implemented and their reported
checks remain valid. The named-profile comparison now supplies current,
bounded runtime evidence, but it also records threshold misses and leaves the
physical timing and AC-A4 allocation-overlap gates unproved. This is not a
product integration or public API/default approval.

## Measured setup

- Windows 11 Pro build 26300; AMD Ryzen 7 7730U, 16 logical processors;
  Flutter 3.47.1 / Dart 3.13.1.
- Flutter Windows `Profile` build, private A0 diagnostic instrumentation v2,
  mask 11, 600 seconds per phase.
- Same endpoint for OFF and ON: `Headphones (Mon Bose QC35 II tupeupatest)`,
  WASAPI endpoint ID `{0.0.0.00000000}.{7db05587-6538-4609-9228-06838579b1fe}`.
  Reported audio period is 10 ms; negotiated output block is 1,056 frames.
- Deterministic 997 Hz, 0.025-amplitude, stereo anti-phase fixture through the
  default DSP and speed 1.0. `silent_output=true` submits digital zeroes and
  analyses zero-valued PCM. This retains callback/analysis/transport load but
  does not measure audible playback or nonzero spectral behavior.
- The diagnostic app paints with its private spike `AnalysisPainter`; it does
  not instantiate the shared package's `SharedSceneView`. Its RTT/lag/UI metrics
  are harness measurements, not runtime profiling of the shared painter.
  A separate synthetic Windows Profile run later exercised `SceneEvaluator`
  and `SharedSceneView`; its frame and process metrics are documented in
  [`shared-scene-view-profile-review.md`](shared-scene-view-profile-review.md).
  That renderer-only run has no audio source and does not change this A8
  OFF/ON disposition.
- The initial autorun completed OFF in 600.000718 s and ON in 600.001830 s.
  The OFF summary was captured. Because the console clipped the ON JSON line,
  an ON-only 600.001605 s repeat on the same endpoint, mask, Profile setup and
  unchanged sources was written to
  `build/stage-a8-20261008-profile-on-only.log`; its parsed result is
  `build/stage-a8-20261008-profile-on-only.json`. The table uses the initial
  OFF phase and the fully captured ON repeat, so it is a matched-configuration
  comparison, not two phases from one continuous process.
- The engine example CMake cache remained
  `SHIPGLOWS_AUDIO_A0=OFF` at
  `flutter/shipglows_audio/example/build/windows/x64/CMakeCache.txt`. The
  separate private A0 spike has its own opt-in ON cache, required by that
  diagnostic. Doppler was not used.

## Results

| Metric | OFF | ON | Interpretation |
| --- | ---: | ---: | --- |
| Audio period | 10 ms | 10 ms | Same measured endpoint |
| Render/callback p99 upper bound | 8.191 ms | 8.191 ms | Below the 10 ms period in both phases |
| Render/callback maximum | 72.351 ms | 134.708 ms | Retained maxima; p99 does not erase these outliers |
| Underruns / submission failures | 0 / 0 | 0 / 0 | No added underrun observed |
| Process CPU, one-core percentage | 42.12% | 56.05% | +13.93 percentage points; includes Flutter, polling and serialization, not isolated FFT CPU |
| RTT mean / p99 bucket / maximum | 0.435 ms / 1 ms / 20.648 ms | 0.699 ms / 2 ms / 23.605 ms | Typical buckets are low; both maxima exceed the provisional 12 ms review target |
| Submitted-frame lag proxy, mean / p95 bucket / p99 bucket / maximum | N/A (analysis off) | 25.692 ms / 34 ms / 40 ms / 108 ms | ON maximum exceeds the provisional 100 ms target; this is not audible or photon lag |
| No-forward intervals over 100 ms | N/A (analysis off) | 1; maximum 144.571 ms | Flutter-observed analysis progress, not an audible freeze measure |
| UI raster p95 / p99 bucket / maximum | 2 ms / 4 ms / 50.475 ms | 3 ms / 5 ms / 44.134 ms | p95 is below the provisional 60 fps 16.7 ms target |
| Process working set / peak working set | 145,874,944 / 147,521,536 B | 150,642,688 / 151,896,064 B | Peak snapshot delta +4,374,528 B; not a flat-memory or allocation-cap proof |
| Process private bytes | 179,093,504 B | 184,156,160 B | Whole-process snapshot; not feature-owned retained/incoming overlap |
| Accounted native context | 403,936 B | 1,540,304 B | Diagnostic counter; distinct from process memory and client AC-A4 budget |

ON delivered 59,842 records with reported delivery coverage 1.0, zero missing
records, zero resynchronizations, zero PCM continuity changes, zero queue
drops, and an accepted final batch with no undelivered tail. Its render p99
upper bound meets the period check. These observations apply only to this
silent synthetic run.

The ON parser reported 179,526 completed vector copies and 156,067,936 logical
float64 payload bytes. This is a copy-volume counter, not an allocation-event
count. The retained-view probe reported one 2,608-byte logical latest-record
vector and 150 compact hops (storage capacity 151; observed transient high-water
151). Those selected roots exclude codec/list/map/object overhead, the actual
incoming batch overlap, allocator arenas and full VM/process allocation
throughput. **AC-A4's 256 KiB retained plus 128 KiB incoming feature-owned cap
is therefore not proven.**

## Readiness disposition

Passed for this run: named endpoint/period captured; callback p99 upper bound
below the period; zero underruns/submission failures; ON record coverage and
final-batch closure complete; UI raster p95 below the provisional 60 fps
budget.

Not passed or not established: submitted-frame lag maximum is 108 ms; RTT
maximum is 23.605 ms; the observed process CPU is not isolated optional-FFT
cost; process working-set snapshots do not establish flat memory; and
feature-owned transient allocation overlap remains unmeasured. Audible
audio-to-screen lag and its uncertainty remain null. The accepted A0 operator
decision remains qualified and does not change that physical-measurement
boundary.

**A8 outcome: NOT READY for an unconditional Stage A runtime-readiness claim.**
The private Stage A implementation and synthetic/offline evidence remain
usable within their stated scope. Keep the API/defaults provisional, keep
physical timing and AC-A4 open, and leave BeatGlows/ContentGlows integration,
exporters and product rollout outside this review.

The 2026-10-08 shared-client decoder follow-up changes the allocation shape
structurally but adds no allocator measurement. It therefore does not close
the AC-A4 gap or change this verdict; see
[`shared-client-decode-review.md`](shared-client-decode-review.md).

## Supplementary shared-path Profile harness

A short paired Windows Profile harness now exercises real default-output WAV
playback, and in ON also real MethodChannel delivery, `AnalysisController`,
and `SharedSceneView`. It accepted 1,037 analysis sequences in ON with zero
underruns; CPU averaged 16.23% OFF and 27.77% ON (one core), while MethodChannel
RTT p99 was 0.434 ms. See
[`shared-audio-profile-and-ac-a4-followup.md`](shared-audio-profile-and-ac-a4-followup.md)
and its JSON companion for all values.

This software-only ~8-second-per-phase comparison supplements the 600-second
private A0 comparison and does not replace it. It does not measure acoustic to
screen lag, and process RSS/private bytes do not prove feature-owned AC-A4
overlap. The prior 600-second ON RTT/lag maxima and missing physical and
allocation evidence remain. **A8 remains NOT READY; AC-A4 remains OPEN.**

## Operator practical disposition — 2026-10-08

The operator confirms that calibrated photodiode/microphone acquisition
equipment is not currently available and chooses to proceed without acquiring
it for now. The existing Windows Profile results and the qualified A0 practical
decision remain the basis for practical work. This is a scope decision, not a
measurement waiver or evidence that physical audio-to-screen lag is acceptable:
that lag and its uncertainty remain unmeasured, and the unconditional A8
verdict remains **NOT READY**. AC-A4's measured client allocation overlap also
remains open.

## Provenance and limits

The engine worktree was dirty at HEAD `67e3a0594ae92c8cf0d97f90fd274147acff6012`;
the run used local current sources, without staging or committing them. The
Profile executable was built successfully; MSVC emitted C4324 alignment
padding warnings for `AnalysisBlockQueue`. No tests were rerun in this
measurement pass. The companion JSON contains the full captured ON result and
normalized OFF metrics, plus source/binary hashes. A local Tee log remains in
the private spike's ignored
`C:/Users/Diane/ShipGlows/beatglows/spikes/audio_analysis_a0/build/stage-a8-20261008-profile-on-only.log`;
its temporary local VM-service path was redacted, and the log is not part of
the committed evidence artifact.

## Shared-path Profile rerun with mounted view — 2026-10-08

The follow-up harness was corrected to mount `SharedSceneView` before the ON
phase. The prior short run quoted earlier in the shared-path follow-up had
mounted the view only after measurement and is not valid as shared-renderer
evidence. In the corrected Windows Profile run, the same default multimedia
output, generated nonzero WAV, transform and music mask 11 were used for both
phases. OFF/ON callback p99 was 4.095 ms in both, with zero underruns. ON
accepted 1,047 records, with zero resync, skipped delivery records, PCM gaps or
source errors; MethodChannel RTT p99 was 0.591 ms, FrameTiming p95 3.514 ms,
and process CPU averaged 29.33% of one core (15.13% OFF). These ~8.5-second
software measurements do not displace the 600-second A8 review or establish
physical audio-to-screen timing.

The same run filled the speech controller's maximum 166-record history and
then accepted newer records through sequence 184 while the history remained at
166 and the scene stayed live. Unaligned speech-channel buffer offsets were
observed at 411 and 413 bytes; the shared decoder now makes a single alignment
copy when necessary, and the targeted regression test passes. Neither the
short profile nor the alignment fix measures the AC-A4 retained/incoming
allocation overlap. **A8 remains NOT READY; AC-A4 remains OPEN.**

## Final mounted-view comparison and memory exercises — 2026-10-08

The preceding supplementary metrics are superseded by the final paired run in
[`shared-audio-profile-and-ac-a4-followup.md`](shared-audio-profile-and-ac-a4-followup.md):
the view was mounted before ON measurement; ON accepted 1,044 records with no
resync, skips, PCM gaps or source errors; RTT p99 was 0.922 ms, FrameTiming
p95 3.761 ms and process CPU averaged 32.53% of one core. Callback p99 was
4.095 ms in both phases and underruns were zero. The ON frame maximum was
4.685 ms; submitted-frame lag proxy p50/p95/max was 50/70/70 ms. That proxy is
not acoustic lag. In the same Profile process, an exact 16-record music11 batch
(103,872 payload bytes) was installed while keeping the prior `SceneState`
referenced, then a new live scene was published. The speech context exercise
retained 166 records and accepted newer channel lots while staying live.

These software exercises confirm workload and publication paths. They do not
identity-deduplicate the live object/backing-store union or prove the 256 KiB
retained + 128 KiB incoming cap. Physical audio-to-screen timing and the
unconditional A8 gate also remain open; **A8 NOT READY, AC-A4 OPEN**.
