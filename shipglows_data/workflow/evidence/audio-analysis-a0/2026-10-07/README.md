# A0 calibration record — 2026-10-07

**Partial calibration, not A0 passed or Stage A ready.** Canonical spec remains
draft 1.1.0. The prototype is disposable and no public protocol is frozen.

## Identity and scope

`manifest.json` identifies revisions, source/binary SHA-256, hardware, build,
configuration and named endpoint. Engine base: `853509a25bacd0cfed31408e8ef374626e75cd9b`;
BeatGlows base: `0799318eee5bcedac9de75dd2b6f1b4246e9667e`. Both worktrees are dirty.
Pre-existing engine work was README, JUCE decision matrix, Windows playback docs,
untracked research/ownership docs and the canonical analysis spec. BeatGlows had
a deleted older shared spec and untracked exploration. Those changes were
preserved; no commit, push or publication was performed.

Native owns `tools/audio_analysis_a0/`; Flutter owns sibling
`beatglows/spikes/audio_analysis_a0/`; integration owns optional Windows hooks,
private channel and this evidence. Three delegated agents supplied native,
Flutter and independent read-only review. Only the diagnostic enables
`SHIPGLOWS_AUDIO_A0`. No product placement, activation, persistence or palette
was selected. No Stage B, Android, microphone, production exporter or service
limit changed.

Final Windows setup: Ryzen 7 7730U, 16 logical processors, 31,031,443,456 bytes
physical RAM, Windows 11 Pro build 26300; Flutter 3.47.1 / Dart 3.13.1, Windows
MSVC Profile, Impeller OpenGLES. Bose QC35 II endpoint identity in the manifest,
period 10 ms, maximum negotiated output block 1056 stereo frames at 48 kHz.
Source is a precomputed one-second repeat of 997 Hz, amplitude 0.025, stereo
anti-phase; default shared DSP including limiter, speed 1.0. This exercises
shared WASAPI/DSP, not Media Foundation decode, product transport, loopback,
analog output or human listening quality.

## Native evidence

Native Release GNU 13.2 reference CTest passed. Tests include four masks,
partition/EOF/origin/reset, first/last over-unity impulses, initial attack and
independent release/partial-hop recurrence, independent sampled direct DFT and
anti-phase Parseval, whole queue overflow and invalid PCM rejection.
They are bounded A0 tests, not exhaustive A2 acceptance.

`native-30s.json` processes 30 seconds of generated PCM per mask in accelerated
worker time. Each produces 3000 records. No spectrum means zero FFT calls;
spectral masks perform 6000 per-channel FFTs. Fixed private processor storage is
918,256 bytes for every mask; this is not compact retained native speech storage.
Input copy is 11,520,000 bytes per mask. History overwrites 2872 records while
preserving transform state.

| Mask | Measurement µs/record, including FFT | Transform µs/record | FFT subset µs/record | Aggregate push p99 µs |
| --- | ---: | ---: | ---: | ---: |
| music3 | 14.426 | 66.393 | 0 | 391.9 |
| music11 | 214.118 | 470.893 | 193.616 | 2739.8 |
| speech5 | 5.686 | 2.244 | 0 | 27.6 |
| speech13 | 240.160 | 478.427 | 230.554 | 5315.9 |

Separated totals include EOF; aggregate push excludes final EOF flush. Timers
measure elapsed costs, not CPU utilization. Concurrent compilation/background
load was present; all outliers remain in JSON. FFT is included in measurement,
so the FFT column must not be added again. Repeated band mapping is a candidate
for subsequent optimization, not a reason to change the public contract now.

## Flutter and Windows evidence

Flutter static analysis and four focused tests passed under Doppler. Tests
cover bounded speech history, loss/reset distinction, atomic admission,
oversized/nonfinite input and final batch coverage. Renderers use native visual
fields without recurrent filters. Real Profile smoke OFF30/ON30 completed on
the named endpoint, zero underruns/queue drops/resync/no-forward intervals.
The earlier smoke predates the final coverage/codec-size instrumentation and
must not be combined with final-run metrics. Its coarse OFF p99 upper bound
16,383 µs straddles the 10 ms period and proves neither deadline pass nor fail.

The first long attempt was stopped after the operator reported discomfort.
Music played concurrently during an unspecified initial interval. The second
attempt stopped after 6.873895 seconds because desktop focus loss was treated
as backgrounding. That diagnostic defect was corrected: inactive alone keeps
a visible window running; hidden/paused/detached stop it. Neither attempt is a
completed comparison. The final restart follows the operator's instruction to
run without concurrent music, OFF600 then ON600 on the same source/endpoint.

Both final phases completed and are retained in `windows-600.json`: OFF
600.001617 seconds, ON 600.001104 seconds, same endpoint/configuration.

| Observed metric | OFF | ON music11 |
| --- | ---: | ---: |
| Audio underruns / failed submission | 0 / 0 | 0 / 0 |
| Total callback p99 upper bound | 8191 µs | 8191 µs |
| Total callback maximum | 27.643 ms | 87.338 ms |
| Process CPU, percent of one core | 60.226% | 89.051% |
| End working set | 148,103,168 bytes | 157,499,392 bytes |
| Lifetime peak working set at observation | 153,886,720 bytes | 162,512,896 bytes |
| Accounted native context | 403,528 bytes | 1,474,360 bytes |
| Dropped PCM queue blocks / core resets | 0 / 0 | 4 / 2 |
| Delivery resync / missing published records | 0 / 0 | 2 / 20 |
| Received / published records | N/A | 59,797 / 59,817 |
| Published sequence coverage | N/A | 99.9665647% |
| Final undelivered tail | 0 | 0 |
| RTT p99 1 ms bucket / maximum | [2,3) ms / 18.634 ms | [3,4) ms / 77.706 ms |
| UI raster p95 bucket / maximum | [5,6) ms / 143.851 ms | [8,9) ms / 71.893 ms |
| Poll spacing maximum | 109.690 ms | 138.854 ms |
| Submitted-frame lag mean / maximum | N/A | 27.398 ms / 258 ms |
| Historical no-forward counter | 0, disabled | 2, potentially undercounted |

The callback p99 bound is below the measured 10 ms period in both completed
phases, independently of the retained maximum outliers. There are zero extra
audio underruns. This does **not** validate analysis delivery: four PCM blocks
were dropped, two continuity resets occurred, and 20 published records were
missed by the bridge. The submitted-frame proxy exceeds the provisional 100 ms
target at its maximum; actual audible/source-to-photon timing remains unknown.
CPU increases by 28.825 percentage points of one core for the whole Flutter
process; end working set increases by 8.961 MiB. These are mixed UI/bridge/native
process costs, not isolated FFT CPU or a proof of flat memory.

ON has 119,634 FFT calls, zero worker failure and no unknown final PCM tail.
Elapsed averages per published record: measurement 244.594 µs (FFT subset
205.690 µs), transform 379.114 µs, worker including mutex wait 652.804 µs.
Producer copy is 229,709,568 bytes; worker pop plus processor copy is
459,419,136 bytes; bridge float copy is 385,861,860 bytes. Encoded responses
total 443,743,426 bytes with a largest response of 87,977 bytes. The extra codec
size-probe costs 1.776405 seconds in ON, included in the measured process/RTT.
Producer queue-copy/reservation time is 97.893 ms and worker pop/copy time is
388.744 ms over ON; these are totals, not per-block costs or allocator counts.

Review found that instrumentation v1 resets its forward-progress anchor before
checking a resumed fresh batch; it may undercount stalls. Thus its counter of
two is not a complete freeze count. Valid poll/raster/lag maxima are retained.
After the run, v2 fixes that ordering, latches each interval until recovery and
excludes native teardown from freeze timing. Static analysis and seven focused
Flutter tests pass for v2; it was not rebuilt or recalibrated on Windows. The
manifest binds the old measured source/binaries; `post-calibration-source.json`
separately identifies the tested later instrumentation. No result is rewritten
as if measured with v2.

## Windows comparison with v2 — 2026-10-07

`windows-600-instrumentation-v2.json` is a separate matched Profile run on the
same named Bose QC35 II endpoint: 600.001725 s OFF and 600.002327 s ON, mask 11.
The diagnostic source/DSP still ran, but `silent_output=true` zeroed the samples
submitted to WASAPI and the analyser. This avoids audible output and retains
callback/queue/worker/FFT scheduling load; FFT and transform values are therefore
not measurements on the earlier 997 Hz signal and do not validate non-silent
features.

Both phases had zero underruns and a callback p99 upper bucket of 8.191 ms for
the 10 ms endpoint period. Max callback duration was 57.206 ms OFF and 196.087
ms ON. OFF process CPU was 52.17% of one core; ON was 68.81%. ON had 59,851 of
59,851 records delivered, zero PCM queue drops/resets, zero resyncs, and queue
high-water 7/16 blocks. The v2 tracker observed five no-forward intervals over
100 ms, maximum 283.478 ms; this is Flutter-observed record progress, not an
audible freeze measurement.

The additional counters measured worker mutex wait (224.934 ms total, 18.945 ms
maximum), processor push (28.158 s total, 33.430 ms maximum), Poll mutex hold
(2.513 s total) and Poll mutex wait (0.788 s total). This supports mutex
contention as one contributor but does not explain every long callback or UI
no-forward interval. The endpoint callback itself remains unblocked by this
mutex. UI raster max was 183.304 ms; RTT max 88.197 ms.

Process CPU includes Flutter, polling, serialization and the probe. Known copy
bytes and working-set/private-byte snapshots are recorded, but total native and
Dart allocation events are still unmeasured. Audible/presentation lag and clock
uncertainty remain null. The clean queue/delivery result for this silent run
does not invalidate the earlier v1 losses or declare A0 passed. Keep numeric
defaults and protocol provisional; review scheduling, allocation counts and
non-silent feature correctness before advancing.

## Poll lock-scope follow-up — 2026-10-07

After the paired run, `A0WindowsProbe::Poll` was changed to copy at most 16
fixed native records and worker counters while holding the shared mutex, then
construct Flutter maps/vectors after releasing it.
`windows-600-post-snapshot-lock-v2.json` records a 600.002226 s ON-only silent
Profile run on the same endpoint.

Compared with the prior silent ON phase, Poll mutex hold fell from 2.513 s to
0.267 s (about 89%); worker mutex wait fell from 225 ms to 116 ms total and its
maximum fell from 18.945 ms to 9.518 ms. The progress tracker recorded two
intervals over 100 ms (maximum 110.365 ms), versus five and 283.478 ms before.
This supports the lock-scope change; the runs do not isolate all scheduling
variance, so it is not a causal guarantee. There were zero queue drops/resets,
zero delivery loss, and queue high-water 8/16.

The callback p99 upper bucket was 16.383 ms, greater than the 10 ms endpoint
period, with a 104.360 ms maximum and zero underruns. Do not call the callback
budget passed. The previous silent run had a lower p99 bucket; retain both
results and investigate scheduler/callback peaks rather than smoothing them.
Silent input does not exercise nonzero spectral band mapping. Allocation-event
totals, actual audible lag, clock uncertainty and non-silent feature correctness
remain open; A0 and protocol freeze remain unapproved.

Disposition: keep all four masks and current numeric candidates provisional;
do not freeze the protocol. Investigate scheduling/worker contention and the
two PCM gaps before another claimed clean run. Precomputing visual-band overlap
weights is supported as an optimization candidate by transform cost, not yet
implemented or remeasured. The bounded queue must not be enlarged beyond the
approved safety ceilings to hide losses. Timing anchors and allocator evidence
remain required. No further audible run was started after this comparison.

`vm-memory.jsonl` samples the current main-isolate memory/class statistics via
the local VM Service, without forced GC or accumulator reset. Class
accumulated fields equal current fields in observed Profile snapshots and are
not reliable whole-phase allocation-throughput evidence. Process working set,
private bytes and CPU include Flutter; lifetime peak is not a phase-local peak.

## Interpretation and remaining proof

- Callback work is only render/copy/queue publication; analysis, transform and
  FFT execute on the worker. Allocation-free callback is source evidence, not
  a global allocator-tracing result. Stop acknowledgement was repaired and
  observed in real OFF/ON smoke closure. Admission exceptions cannot escape
  the WASAPI worker. A final dropped PCM tail is not padded as clean EOF.
- History is fixed 128 records below 1 MiB; native analysis context below
  8 MiB. Queue admission is whole blocks, maximum 16 slots/16384 frames.
  Responses contain at most 16 records, resync newest eight, and an explicit
  codec-size ceiling 128 KiB. Request/lease validation is private A0, not the
  exhaustive production A4 protocol.
- Transfer includes requested physical and visual float32 fields; callback PCM
  never reaches Flutter. Measured producer/pop/processor/bridge float-copy
  counters have distinct scopes. Codec-size probing deliberately performs an
  extra encoding pass; its cost is reported and included in RTT. Dart parsing
  and codec allocations are not zero-copy and remain separately profiled work.
- `worker_us` includes worker mutex wait plus analysis/transform, excludes queue
  pop; individual worker timers identify measurement, transform and FFT.
  Producer/pop timers include queue overhead. Do not add overlapping timers.
- Native p99 is a power-of-two bucket upper bound; Flutter percentile buckets
  are 1 ms intervals. Keep maxima and all freezes/resyncs. The actual p99 must
  be below the measured device period before claiming callback-budget pass.
- Published sequence coverage includes final EOF records. PCM queue loss is
  distinct from delivery loss. A finalization error must not count as success.
- Submitted-frame lag is a proxy, not source-to-photon or audible lag.
  Presentation anchor uncertainty, drift bound, U admission, real audible
  latency and actual native/Dart transient allocator counts remain open.
- Do not select a production delay/budget, freeze schema, declare A0/A8 passed,
  or advance A2–A7 solely from this partial prototype. Review the named-endpoint
  results and outstanding proof before the next stage.

Builds, tests and runtime commands use explicit Doppler beatglows/dev. Reproduce
native checks using `tools/audio_analysis_a0/README.md`; reproduce the private
Windows run using the sibling diagnostic README. No external research was
needed or performed in this implementation run.
