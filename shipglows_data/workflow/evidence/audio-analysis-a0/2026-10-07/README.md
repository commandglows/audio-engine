# A0 calibration record — 2026-10-07

Operator correction: this engine's Doppler policy is `NONE`. Future native,
private A0 Flutter and sampling commands run directly without Doppler. Earlier
measurements were launched with Doppler in error; retain that historical
provenance, do not rewrite them as secret-free launches. The latest clock-residual
matched comparison runs directly with Doppler NONE, as recorded below.

**Operator disposition (2026-10-07): A0 accepted with a known limitation for the current practical scope.** This is operator acceptance, not a calibrated physical-timing pass; audio-to-photon lag and uncertainty of <=10 ms remain unmeasured. The canonical spec remains draft 1.1.0 and no public protocol is frozen.

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

### C++ allocation probe follow-up

`native-cpp-new-allocations.json` records a GNU x64 Release measurement of
ordinary C++ new/new[] calls on one thread. A positive control observed one
123-byte call. Each of the four masks processed 30 seconds of nonzero PCM
(3,000 records) through the bounded queue and processor, including EOF:
zero observed steady-state calls/bytes. Setup observed two calls requesting
979,696 bytes per mask; optimized allocation elision means this is not an
object count. Doppler beatglows/dev CMake build and both CTest checks passed.

This narrows the missing allocator evidence for the private core only.
Direct malloc/realloc, aligned allocations, other threads, bridge maps/codec,
Dart and the real Windows callback are outside its scope. No whole-process
allocation-free claim, callback-budget pass or A0 acceptance follows.

### Record bridge allocation review

`native-bridge-allocations-before.json` and `native-bridge-allocations-after.json`
measure ordinary C++ new/new[] in GNU x64 Release using the selected Flutter
SDK codec and the serializer now shared with the private Windows probe.
All four masks and batch counts 0/1/8/16 are measured. The SDK's forwarding
EncodableValue constructor initializes its variant from an lvalue and copies
containers; inherited variant assignment moves them. Record vectors/maps,
response containers and list capacity now avoid those redundant copies.

For mask 11 / 16 records, map construction falls from 1,448 calls / 741,892
requested bytes to 274 / 129,616 (81% fewer calls, 83% fewer requested bytes).
One encode still costs 114 calls / 337,079 requested bytes; wire size is
106,832 bytes for records only. Thus codec growth remains substantial and the
instrumentation's extra encode must not be called zero-copy. Requested bytes
are allocation throughput, not peak or retained memory. No timing improvement
or Windows allocator equivalence is asserted from this standalone comparison.

The three CTest checks pass, including byte-for-byte comparison against an
independent legacy field construction. Poll metrics, native dispatch, Dart,
other threads, direct malloc and aligned allocation remain outside this probe.
The shared serializer preserves the private schema; no public protocol freeze.

Windows integration was then compiled and run with Doppler beatglows/dev
`flutter run -d windows --profile`, autorun mask 11, 30 seconds per OFF/ON phase.
Both phases emitted successful stop acknowledgement. The saved ON result in
`windows-30-bridge-move-smoke.json` has 3,003 records, full delivery coverage,
zero queue drops/resets/underruns/resyncs, queue high-water 1/16, no progress
interval over 100 ms, RTT mean 0.563 ms / max 4.891 ms, render p99 bucket upper
bound 8.191 ms and max 6.550 ms on the named 10 ms endpoint. This is a short
silent integration smoke, not a matched ten-minute performance comparison,
audible/clock proof or native/Dart runtime allocation measurement. Previous
long-run callback exceedances remain unresolved; A0 remains open.

### Long-run bridge-move comparison in progress

At 2026-10-07 06:11 UTC a new Doppler Profile run was verified live, configured
for matched 600-second OFF/ON phases. Raw output is in
`build/audio_analysis_a0/windows-600-bridge-move.log`; bounded 45-second VM
snapshots are in `build/audio_analysis_a0/windows-600-bridge-move-memory.jsonl`.
These are pending evidence, not completed results. Revalidate the original
process/session before interpreting or restarting it.

The first OFF snapshot returned 2,900 class statistics, with accumulated size
and instances equal to current values in every class. Thus this Profile API
does not provide trustworthy phase allocation throughput. The enhanced sampler
records the 12 largest current classes, nullable GC/reset dates and this equality
count without forced GC/reset. `sample_vm_series.ps1` holds the original process
handle, caps sample count, and stops after both results or terminal output.
Heap/class snapshots, MemoryUsage and process memory have distinct scopes;
do not equate their totals or infer zero transient allocations.

`review_phase_log.ps1` validates complete OFF/ON output without hiding losses:
duration, Profile mode, phase order, endpoint/mask/silent agreement, worker/stop
acknowledgements, queue/PCM/submission/underrun counters, final delivery coverage,
resync/progress intervals and submission p99 upper bucket below the endpoint
period. A live partial log was verified as `incomplete`, not passed. Its output
is an explicit limited check review, never an A0 acceptance certificate.

Source review confirms `render_p99` stops immediately after Render, whereas
`submission_p99` extends from Render start through PCM copy, ReleaseBuffer and
the optional submitted/tap observer. Use the latter for audio-plus-tap cost.
Neither timer includes the preceding GetBuffer/padding/event wakeup operations;
neither separates execution time from scheduler preemption or measures audible
presentation. Do not infer an exact percentile from power-of-two buckets.

The bridge-move OFF phase subsequently completed in 600.001737 seconds:
60,001 callbacks, zero underruns/submission failures, successful stop, submission
p99 upper bucket 4.095 ms and maximum 8.798 ms on the 10 ms endpoint.
`windows-600-bridge-move-off-partial.json` preserves this phase separately while
ON is pending. RTT maximum was 26.158 ms and raster maximum 151.650 ms despite
raster p95 bucket 2 ms; preserve these outliers rather than reporting an
unqualified smooth run. OFF whole-process CPU averaged 37.19% of one core;
it includes Flutter/diagnostics and is not analyser CPU. No paired conclusion,
allocator-throughput claim or A0 pass follows from this single phase.

### Completed bridge-move comparison

`windows-600-bridge-move-review.json` now preserves both completed 600-second
silent Profile phases. Its listed checks pass: no extra underruns, submission
failures, queue drops, PCM resets, resyncs or missing delivery; ON delivers all
60,001 published records and reports no progress interval over 100 ms (maximum
60.051 ms). Submission p99 upper bucket is 4.095 ms in both phases, below the
10 ms period. ON maximum is nevertheless 25.397 ms versus OFF 8.798 ms.
Retain that scheduling/callback outlier; a p99 observation is not a bound on
every callback. The run was launched under Doppler before the operator's
correction; future commands use NONE.

Whole-process one-core CPU is 37.19% OFF and 43.29% ON, a 6.10 percentage-point
difference between sequential observations, not isolated causal analyser CPU.
ON RTT mean is 0.415 ms / max 33.223 ms, raster p95 bucket 2 ms / max 29.180 ms,
and paint submitted-frame lag proxy max 44 ms. Audible lag and clock uncertainty
remain null. Native context is 1,474,488 bytes, queue high-water 2/16; maximum
encoded response is 54,929 bytes. Zero PCM input does not exercise nonzero
spectral transform values and cannot validate audible latency.

`windows-600-bridge-move-memory.jsonl` records bounded snapshots. Thirteen ON
snapshots observed heap usage 8,985,200–10,704,032 bytes, working set
160,899,072–166,739,968 and private bytes 194,359,296–201,109,504. OFF startup
working set increased from 125,661,184 to 160,284,672; do not claim an entirely
flat whole-process lifetime or conflate it with bounded native context. These
are sampled ranges, not true phase peaks or transient allocator-event counts.
All results remain partial A0 calibration; public protocol stays provisional.

After both phases, a read-only `getAllocationTraces` capability probe returned
`CpuSamples` with zero samples and no RPC error. This proves the retrieval RPC
is callable in this Profile runtime; tracing was not enabled, so it establishes
no event coverage or zero-allocation behavior. The sampler ended after observing
both phase results, and the diagnostic window was closed normally.

### Targeted Dart allocation traces

A separate 120-second ON-only Profile diagnostic was launched directly with
`flutter run`, Doppler NONE. `trace_dart_allocations.ps1` enables tracing only
for AnalysisRecord and CompactHop and disables both in finally. The actual
`dart-profile-targeted-allocation-trace.json` reports no RPC/restore errors:
1,502 CompactHop traces over 15.006871 seconds and 2,714 AnalysisRecord traces
over 27.678739 seconds. The different returned windows reflect sequential RPC
enabling/retrieval; the requested 15-second sleep is not a common observation
window. Do not sum them as a single 15-second allocation total.

This proves targeted transient allocation traces are available in this Profile
runtime and these transport objects allocate during the diagnostic. It is not
an exhaustive all-class event count, allocated-byte measurement or profiling-free
performance comparison. Typed vectors, codec/maps/lists, painting and other
classes remain outside the trace. Keep the prior class-memory snapshots distinct.

The targeted trace run completed and stopped successfully; its full result is
`windows-120-targeted-allocation-trace.json`. It had zero queue drops/PCM resets/
underruns, but two delivery resyncs, 106 missed records and two no-forward
intervals over 100 ms (maximum 627.416 ms). Working-set lifetime peak reached
295,870,464 bytes. These are retained profiling-run failures/overheads, not
omitted outliers or a clean performance pass; tracing/RPCs perturb the runtime
and these observations alone do not isolate their causal contribution.

### Parser vector copy counters

The private Dart parser now counts each successfully constructed visual vector
and its numeric elements, resetting on start and including final EOF poll.
`flutter analyze` and eight tests passed directly (Doppler NONE), including the
four masks. Counts describe logical parser work, not codec allocations, object
headers, boxing, map/list copies or actual allocator bytes. A vector completed
before a later parse failure remains counted; incomplete rejected vectors do not.

The Profile observation `windows-parser-copy-observation.json` verified mask11:
841 received records, 2,523 vector copies and 274,166 numeric elements, exactly
3 vectors / 326 elements per record. Logical float64 payload is 2,193,328 bytes.
The requested 30-second diagnostic stopped at 8.374002 seconds and recorded a
148.089 ms progress interval; its early-stop cause was not independently proven.
Keep this as counter integration evidence, not a 30-second performance pass.
The existing clean ten-minute comparison is not replaced by this observation.

### Sparse band-overlap preparation

The private Processor now prepares bounded nonzero overlap coefficients once
off the callback, then sums only each band's contiguous nonzero spectrum span.
It removes the 64-by-1024 overlap recomputation from every spectral hop without
changing FFT, transform defaults, recurrence, record layout or feature masks.
Processor size increases by 66,048 fixed bytes to 984,304; history is unchanged.
The fixed table reserves space for all masks in the prototype. All three CTest
checks pass directly with Doppler NONE, including exact float equality against
the original full-bin transform for both spectral masks and partial EOF, and
zero observed steady-state ordinary C++ new/new[] calls for all masks.

`native-band-weights-alternating.json` retains three alternated baseline/optimized
30-second generated-PCM runs per variant, compiled with identical GNU Release
options and calibration source. Baseline uses HEAD's original analysis header.
Mask11 transform elapsed cost is 297–448 us/record baseline versus 49–108 us
optimized; mask13 is 255–451 versus 11–27 us. Non-spectral measurements overlap
and vary with scheduling, so this supports the spectral optimization without
claiming an isolated CPU reduction or a particular Windows timing improvement.
The previous ten-minute Windows binary predates this change; Windows integration
and post-change endpoint performance remain unverified. A0 stays open.

### A0 performance/allocation review disposition

| Requirement | Current evidence | Disposition |
| --- | --- | --- |
| Four masks, optional FFT, bounded streaming numerical values | Native generated 30-second runs, independent DFT/Parseval, full-bin band reference, chunk/EOF/reset tests | Supported within prototype; not exhaustive production acceptance |
| Callback has no analysis/transform/FFT or application allocation/lock | Source path, bounded SPSC queue, targeted core ordinary-new probe | Source plus targeted allocator evidence; no all-allocator Windows callback tracing |
| Named-endpoint matched 10-minute OFF/ON | Current sparse/clock-residual binary completes both phases, zero underruns/loss/resync, p99 submission upper bounds 4.095/8.191 ms below 10 ms | Observed pass for listed checks on that binary/run; maxima 21.947/28.389 ms and earlier failures retained; later local-clock/view instrumentation has short-run proof only |
| Retained/transient memory and copies | Fixed native context/history, 25 process/VM snapshots, GNU and MSVC map/codec allocator probes, targeted Dart traces, parser copies and view retention counters | Native requested payload and retained view counts measured; actual feature-owned retained/incoming client allocation overlap under AC-A4 remains unproven. Entire Flutter/VM allocation throughput is not the 384 KiB cap's scope |
| RTT/progress/resync | Matched-run coverage and distributions, maxima, failed profiling/early-stop runs retained | Observed within private harness; profiling changes workload |
| Presentation lag and uncertainty | Long-run nominal clock residual 39.75/90.13 ms; local one-second residual measured only in short runs; submitted-frame proxy, audible lag and uncertainty null | Fixed-start nominal mapping unsupported; local timestamp uncertainty and acoustic/display lag still unproved; no candidate delay/U approval |
| Protocol/default readiness | Private map harness and provisional numeric candidates | Not frozen; A1/A2-A7 are not implied complete |

Keep 16-block queue, bounded history and 128 KiB response safety ceilings;
do not enlarge them to hide stalls. Adopt sparse overlap preparation only in
the disposable prototype based on exact reference equality and measured cost.
Retain bridge move assignments; their allocation reduction is directly measured.
Do not approve the candidate presentation delay/uncertainty or claim a whole
feature-owned retained/incoming client budget from current data. Sparse optimization Windows integration and
current-binary matched performance review are now supported. Remaining work is
defensible local presentation-clock uncertainty/acoustic-display measurement
and aggregate transient transport accounting before public schema review.
No further generic calibration loop is needed to rediscover the already measured
core/map/parser behavior. A0 remains incomplete; Stage B and production exporter
gates stay separate from this foundation review.

The sparse-optimization Windows smoke subsequently completed in 30.001208 s
using direct `flutter run` Profile, Doppler NONE. Result is preserved as
`windows-30-sparse-band-smoke.json`: 3,004 records/full coverage, zero queue
drops/PCM resets/underruns/resyncs/over-100 ms progress intervals and successful
stop; submission p99 bucket <=4.095 ms, maximum 6.433 ms, native context 1,540,536
bytes. Native transform elapsed total is 46.0495 ms (15.329 us/record), but this
silent short run is not a causal comparison with previous long phases. Raster
maximum is 71.541 ms despite p95 bucket 4 ms; retain that outlier. Parser counts
are 9,012 copies / 979,304 elements, exactly the mask11 formula. This proves
Windows compilation/integration for the change, not long-run or A0 readiness.

### Bridge live-payload review

`native-bridge-peak-payload.json` separates cumulative requested bytes from
simultaneously live requested payload for the standalone GNU Release record-map
and actual Flutter SDK codec. For 16 records, observed response plus one encoder
peak is 139,144 bytes (mask3), 299,820 (mask11), 26,470 (mask5), 207,948 (mask13).
Mask11 response retains 129,616 requested bytes; the encoder's peak is 170,204
although its cumulative allocation requests are 337,079. Therefore the previous
cumulative values must not be called simultaneous memory peaks.

The fixed tracking table has 4,096 slots and fails visibly on exhaustion. It
does not alter allocated-pointer layouts; positive controls validate live/free
accounting and cross-scope frees. An initial header-prefixed tracker crashed
the bridge probe (heap corruption, 0xc0000374) due to allocation paths crossing
the interception boundary; it was replaced, and all three CTest checks now pass.
This tooling failure is distinct from engine/runtime playback evidence.

These are observed ordinary-new requested payloads only: native Poll metrics,
allocator overhead, unobserved/other-thread/aligned paths, dispatch and Dart are
excluded. The numbers concern native bridge memory, not the Dart retained-context
cap; do not compare them as if they were the same allocation domain. The private
map bridge remains a calibration implementation, not proof that the production
binary protocol meets the aggregate client memory ceilings. No safety cap changed.

### Raw WASAPI clock observation

The optional private observer now acquires IAudioClock/GetFrequency at stream
initialization and records GetPosition once per diagnostic submission, without
retrying S_FALSE. Production callers with no clock observer do not query it.
The observation is published through fixed sequentially consistent atomics;
Poll attempts at most three snapshots and reports unavailable on contention.
Raw ticks, frequency, QPC in 100 ns, HRESULT and steady-clock query begin/end
are separate fields; no public presentation anchor or uncertainty is invented.
Clock-query work is included in diagnostic submission timing. COM clock ownership
is confined to the WASAPI worker and released on stop/failure/teardown.

Parallel discovery used the approved global research environment dotfiles/dev;
engine builds/runs used Doppler NONE. Microsoft primary pages confirm [device
position must be interpreted relative to frequency, and correlated QPC uses
100 ns units](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclock-getposition),
while [GetFrequency reports a nominal constant and does not measure clock drift](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclock-getfrequency).
S_OK/S_FALSE is API reading quality, not proof of the proposed 2 ms uncertainty.

`windows-30-raw-clock-observation.json` records the compiled Windows Profile
30.002325-second silent ON smoke, 3,004 records, full coverage and zero queue
drops/PCM resets/underruns/resyncs/progress intervals over 100 ms. Last raw clock
frequency is 384,000 units/s (not 48,000 frames/s), HRESULT S_OK. Last QPC
converted to ns lies inside the 3,100 ns native query bracket; the final response
observes it 4,601,100 ns after query end. This single correlation cannot establish
an uncertainty/drift bound or acoustic latency. Submission p99 upper bucket is
8.191 ms, max 8.144 ms; raster maximum 40.998 ms remains reported. Public timing
uncertainty stays unknown and A0 remains incomplete.

### Device/QPC relative-rate review

`clock_review.hpp` now accumulates fixed-memory single-writer counts and
first/last S_OK observations. It records inaccurate/error readings, regressions,
frequency changes and maximum query duration. Native tests validate unavailable
startup, device units versus QPC 100 ns, a known +1000 ppm reference, S_FALSE,
invalid brackets, regressions and changed frequency. All three CTest checks pass
directly without Doppler. Snapshot publication remains bounded and atomic.

`windows-30-clock-rate-review.json` records a complete 30.0022-second silent
Profile run: 3,001 accurate clock readings, zero inaccurate readings/regressions/
frequency changes, maximum query duration 42,300 ns. Observed relative rate over
first-to-last accurate reading is -269.193807 ppm. This exceeds the provisional
100 ppm assumption and does not validate that bound. Startup behavior is included;
do not identify oscillator drift, startup delay or driver jitter as the proven
cause from this aggregate. S_OK does not certify an absolute timestamp error.

Delivery is complete (3,004 records), with zero drops/resets/underruns/resyncs/
over-100 ms progress intervals; submission p99 bucket <=4.095 ms, maximum
4.305 ms. These runtime observations do not accept a presentation uncertainty.
Keep public timing unknown; next investigation must separate startup and stable
clock intervals and expose their endpoint pairs for auditable rate comparison,
rather than substituting a larger drift allowance without evidence. A0 is open.

### Startup versus subsequent clock interval

The fixed accumulator now preserves both full and post-first-QPC-second rates,
with raw endpoint ticks/QPC pairs exported for independent recomputation.
The one-second diagnostic split is explicit; it is not a settling guarantee and
does not hide the full-run result. A native reference test distinguishes a
fixed startup offset from equal subsequent device/QPC rate; all CTest checks pass.

`windows-30-clock-interval-review.json` records a complete 30.00158-second direct
Profile run, Doppler NONE: 3,001 S_OK observations, no inaccurate readings,
regressions or frequency changes, 2,901 observations after the first second.
Full interval rate is -279.704628 ppm; post-first-second interval is +0.029885 ppm.
Both are independently recomputed from the exported pairs with agreement within
0.001 ppm. The first-second device/QPC offset is approximately 8.392 ms, while
the subsequent 29-second interval shows nearly equal rates. This supports startup
offset as the aggregate-rate explanation for this run, not proof of oscillator
drift, timestamp accuracy, acoustic delay or a universal 100 ppm bound.

Delivery remains complete (3,004 records), with zero drops/resets/underruns/resyncs/
over-100 ms progress intervals. Submission p99 bucket <=4.095 ms, maximum 3.686 ms;
raster max 30.266 ms remains retained. Presentation uncertainty stays unknown.
Next timing work must reject zero-position startup as a ready presentation anchor,
map nonzero device samples to the same submitted-source domain, and admit an
anchor only with defensible clock correlation/age/uncertainty evidence. A0 is open.

### Private source/device correspondence candidate

`clock_candidate.hpp` now validates the diagnostic stream's raw clock against
the submitted sample domain (including the adapter's explicitly submitted
bootstrap silence). Conversion uses checked quotient/remainder arithmetic,
retaining fractional frame separately. Zero-position startup, S_FALSE/errors,
missing frequency, overflow, out-of-bracket QPC, future/stale/slow queries,
inactive/inconsistent streams and positions beyond submitted PCM are rejected.
No second queue/device-latency offset is added. A correlated time at a fractional
position is not silently assigned to the floor integer frame. Poll serialization
also checks signed-integer admission. Query/age cutoffs are diagnostic filters,
not a calibrated device-error bound. Public timing uncertainty stays unknown.

All three native CTest checks pass with new conversion/adversarial cases.
`windows-30-clock-candidate.json` preserves the direct Profile 30.00134-second
run (Doppler NONE): 1,872 candidate-valid polls / four unavailable polls; final
candidate is correctly unavailable with reason inactive after stop. Coverage is
complete (3,004 records), no drops/resets/underruns/resyncs/over-100 ms intervals,
submission p99 bucket <=4.095 ms, maximum 5.554 ms. Full/startup rate is
-255.756967 ppm, post-first-second rate -0.008046 ppm; both remain recorded.
This verifies candidate integration/lifecycle, not an accepted presentation
anchor, acoustic lag, drift bound or A0 readiness.

### Latest-binary long-run timing review in progress

The accumulator now records the maximum absolute post-first-second nominal
residual between elapsed device ticks/frequency and elapsed QPC, in ns. This
keeps timestamp variation separate from average rate and startup offset; it is
an observed residual, not an absolute device-error bound. Native tests verify
zero residual for a constant initial offset and an explicit 10,000 ns timestamp
perturbation. All three CTest checks pass, Doppler NONE.

A new direct Profile matched 600-second OFF/ON comparison was started with
the current sparse transform, bridge/parser counters and clock instrumentation.
Engine revision at launch: 67e3a0594ae92c8cf0d97f90fd274147acff6012; BeatGlows:
936855f3ff9c76140ada600e35666bd0e0f9707a. Dirty source hashes are captured in
`build/audio_analysis_a0/windows-600-clock-residual-source-hashes.json`.
Raw output is `build/audio_analysis_a0/windows-600-clock-residual.log`.
Do not reuse the older binary's long-run verdict for this code. Pending results
must be revalidated against the original process/session; no pass is asserted.

The current process is 10588, started 2026-10-07 at 09:05:48 local time.
`windows-600-clock-residual-source-hashes.json` and
`windows-600-clock-residual-binary-hashes.json` preserve the source and executable
identities in this evidence directory. Bounded VM sampling runs against the
original process handle, without forced GC or allocation accumulator reset,
into `windows-600-clock-residual-memory.jsonl`.

`review_clock_log.ps1` independently recomputes the post-startup rate from raw
integer deltas using decimal arithmetic, checks agreement within 0.001 ppm,
and retains missing metrics, inaccurate observations and clock discontinuities
as issues. A known raw-pair fixture agrees; a deliberate one-ppm corruption is
rejected. The maximum residual remains a native aggregate, not independently
replayed per-callback data or an absolute uncertainty bound.
`review_memory_series.ps1` reports observed ranges and first/last differences,
plus whole-process sampled CPU. Phase-count groups can straddle boundaries;
neither sparse memory snapshots nor class accumulated fields establish
allocation throughput, a hard memory cap or flat working set. Pending reviews
remain build artifacts until the matched comparison completes.

The current OFF phase completed in 600.001738 seconds; the original process
continues into ON. `windows-600-clock-residual-off.json` preserves the full
phase, and `windows-600-clock-residual-off-clock-review.json` preserves the
independent rate check. OFF has zero underruns/submission failures/queue drops;
submission p99 upper bound is 4.095 ms against a 10 ms period, with a retained
21.947 ms maximum. Process CPU is 41.4196% of one core (whole-process observation).
All 59,994 clock readings are S_OK, without regressions/frequency changes.
Post-startup rate is -52.424688 ppm, independently confirmed, but maximum nominal
device/QPC residual reaches 39.746833 ms over ten minutes. This materially
exceeds the short-run residual scale and does not justify a 10 ms uncertainty
claim. Native/raw agreement verifies the arithmetic, not timestamp accuracy.
Timing admission remains unknown pending review of the long-run clock behavior;
the ON phase and complete comparison are still pending.

### Completed current-binary matched comparison

Both direct Profile phases now completed: OFF 600.001738 s and ON 600.001399 s,
same endpoint/mask11/silent source. `windows-600-clock-residual-review.json`
preserves full phase results; its listed checks pass with zero issues, including
parser copy counters. `windows-600-clock-residual-clock-review.json` and
`windows-600-clock-residual-memory-review.json` preserve separate timing/memory
reviews. The original diagnostic window was closed after the final VM sample,
using its checked PID/start time. No restart or Doppler wrapper was used.

| Observation | OFF | ON |
| --- | ---: | ---: |
| Submission p99 bucket upper bound | 4.095 ms | 8.191 ms |
| Submission maximum | 21.947 ms | 28.389 ms |
| Underruns / submission failures / PCM queue drops | 0 / 0 / 0 | 0 / 0 / 0 |
| Whole-process CPU, one-core percent | 41.4196% | 50.8134% |
| RTT mean / maximum | 0.413 / 6.896 ms | 0.555 / 14.445 ms |
| Raster p95 bucket / maximum | [2,3) / 41.112 ms | [5,6) / 45.962 ms |
| Post-startup nominal clock rate | -52.424688 ppm | -150.443052 ppm |
| Maximum nominal clock residual | 39.746833 ms | 90.130733 ms |

ON received all 59,994 published records, accepted the final batch, and has zero
missing/tail records, resets, resyncs or no-forward intervals above 100 ms.
Queue high-water is four blocks; native context is 1,540,832 bytes and maximum
encoded response 56,097 bytes. Parser copies are 179,982 vectors / 19,558,044
numeric elements / 156,464,352 cumulative logical bytes, exactly the mask11
formula. These cumulative bytes are not simultaneously retained memory.
Native transform elapsed is 840.3664 ms total (14.0075 us/record), measurement
including FFT is 7,236.9981 ms; FFT is an overlapping subset, not additive.
The submitted-frame paint lag proxy peaks at 56 ms; audible lag and timestamp
uncertainty remain null. All outliers remain in the full records.

There are 25 sparse memory samples, without forced GC/reset; observed ranges
remain snapshots, not allocation event throughput or a hard client cap.
Sequential process CPU differs by 9.3938 percentage points of one core; no
isolated causal claim follows. Clock rate arithmetic agrees with raw pairs,
with no S_FALSE/regression/frequency changes, but long-run nominal residuals
do not establish U <= 10 ms or a fixed-rate presentation mapping. Current-binary
continuity and callback p99 checks pass for this run; A0 remains incomplete
because timing admission/acoustic lag and aggregate transport allocation proof
remain open. No public protocol/default freeze or A2-A7 advancement is implied.

### Local clock diagnostic after long-run review

The private accumulator now also measures nominal residual within successive
approximately one-second windows, including each closing observation before
refreshing its origin. Global cumulative residual remains preserved separately.
Neither metric is an absolute timestamp-error bound. A synthetic 150 ppm clock
test yields 150,000 ns local residual versus 90,000,000 ns over 600 seconds;
regression stops subsequent local accumulation. All three CTest checks pass.

`windows-30-local-clock.json` preserves the direct Profile integration run,
Doppler NONE: 30.002249 seconds, 28 completed local windows, 35,400 ns maximum
local residual, 33,400 ns cumulative post-startup residual, -0.067840 ppm
post-startup rate. All 3,004 records arrive with no drops/resets/underruns/resyncs
or over-100 ms progress intervals; submission p99 bucket <=4.095 ms. Raster
maximum 32.460 ms remains retained. `local-clock-source-hashes.json` binds the
modified sources. The window was closed after completed output. This proves
short-run integration only; it cannot replace the long-run result or admit a
public timing anchor. Local refresh addresses the measurement distinction,
not yet device-error calibration or acoustic/source-to-photon lag.

### MSVC allocation review

The same standalone reference/core/bridge probes were configured with Visual
Studio 17 2022, MSVC 19.44.35229.0, Release x64, Windows SDK 10.0.26100.0,
using the diagnostic's actual Flutter SDK standard codec source. Direct CMake
build and all three CTest targets pass, Doppler NONE. Alignment padding warnings
C4324 identify the deliberately cache-line-aligned queue, not build failures.
`native-msvc-allocation-manifest.json` binds sources/compiler/probe binaries;
`native-msvc-cpp-new-allocations.json` and `native-msvc-bridge-allocations.json`
preserve results. Core steady-state ordinary new/new[] calls remain zero for
3,000 records per mask, with positive interception controls.

| Mask, 16 records | Map retained requested payload | Map + one encoder peak requested payload |
| --- | ---: | ---: |
| music3 | 63,888 bytes | 137,276 bytes |
| music11 | 140,352 bytes | 382,248 bytes |
| speech5 | 24,976 bytes | 31,382 bytes |
| speech13 | 101,440 bytes | 234,271 bytes |

For music11, maps request 145,984 cumulative bytes in 356 calls and one encode
requests 525,198 cumulative bytes in 119 calls; wire records-only is 106,832
bytes, identical to the reference serializer. Cumulative requests are distinct
from retained/peak values. MSVC container/encoder growth differs from GNU, so
the GNU 299,820-byte combined peak must not stand in for this Windows result.
These are requested payload bytes observed through the scoped ordinary-new
probe, not allocator committed bytes or actual Flutter channel/Dart peak.
Aligned/direct-malloc/other-thread paths, root metrics and transport scheduling
remain outside this probe. This strengthens the native transport review without
certifying a complete production client cap or A0 acceptance.

### Dart view retained payload

The diagnostic reports the actual view-owned latest record's vector lengths and
compact history count/high-water after bounded installation. Direct Flutter
analysis and all eight tests pass. `windows-30-retained-view.json` preserves
the 30.002185-second Windows Profile run, Doppler NONE: one retained latest
record, 326 numeric vector elements / 2,608 logical float64 bytes, 150 compact
hops with high-water 150 (750 numeric fields). `retained-view-source-hashes.json`
binds the Dart changes. This is retained logical payload/object count, not Dart
heap bytes: current batch/codec/painter storage, list capacity, object headers,
boxing and allocator overhead are explicitly excluded. The transient 151st hop
before trimming is not represented by the retained high-water counter.
All 3,004 records arrive with no loss/reset/underrun/resync/progress failure;
submission p99 upper bound is 4.095 ms, raster maximum 24.874 ms. The original
window was closed after the completed result. No claim about production
166-slot interpolation storage follows from the prototype's 150-hop view.

### Targeted WPR preparation (not a collected trace)

Local WPR 10.0.26100, WPA and xperf are present. The current session is not an
administrator; WPR reports no active recording and heap tracing disabled for
the diagnostic image. `wpr-capability-review.json` preserves this read-only
check. The built-in Heap profile configures 1,479 MiB system collector buffers,
plus event/internal heap collectors and unrelated providers; it was not started.

`tools/audio_analysis_a0_windows/heap_profile.wprp.template` instead contains
only one HeapEventProvider with an explicit diagnostic PID and a heap collector
with 128 KiB x 64 nominal buffers (8 MiB). There is no system/event collector or
allocation stack collection in this profile. Local WPR accepts profile listing
and detail inspection; validation against the installed WPRControlProfiles.xsd
passes with an inert PID fixture. `new_heap_profile.ps1` generates a profile only
for a currently live `audio_analysis_a0` process, refuses existing output, and
rejects the PowerShell process in a negative check. It performs no elevation,
registry change, trace start or process launch.

This is preparation, not heap-event evidence. Nominal buffers are not a trace
process/OS memory cap; memory-mode retention can overwrite older events, so
loss/coverage and positive allocation controls must be verified before using a
future trace. Windows heap events also do not enumerate Dart managed objects
inside VM arenas. Administrative trace setup, actual collection and scoped
analysis remain unexecuted; no aggregate Flutter/Dart cap is certified.

A subsequent admission attempt targeted the live private diagnostic PID 44436
with the custom profile. WPR start failed with Access is denied, 0x80070005;
`wpr-admission-result.json` records the actual command result. WPR status then
confirmed no recording. No elevation/registry change was attempted; the
diagnostic was closed after the admission check. This is evidence of unavailable
trace privileges in the current session, not heap allocation evidence.

During the later administrator handoff, the generated profile passed
`wpr -profiles` and `wpr -profiledetails`, but the first start command used
`A0Heap.Verbose.Memory` as the CLI profile selector. WPR requires the profile
Name plus detail (`A0Heap.Verbose`); `.Memory` is part of the XML Id that
selects LoggingMode. The incorrect command returned 0xc5600611 and WPR status
confirmed no recording. A second command used the PowerShell name `$profile`,
which did not resolve to the generated profile file; WPR returned 0x80070002.
The diagnostic's 300-second autorun had already completed, although its window
process remained open. No trace was collected by those failed attempts. A fresh ON-only run needed a new
PID-scoped profile, with the generated command invoked using a task-specific
variable or literal full path. See `wpr-profile-name-correction.json` and the
generator's emitted command.

The subsequent administrator capture succeeded with the PID 20868 profile.
`A0Heap-previous-20261007-130306.etl` preserves the pre-existing recording
separately; its target is not verified. The new
`A0Heap-20261007-130059-20868.etl` spans 60.6378956 seconds, but xperf reports
only 1.7309957 seconds between its first and last retained events. The memory
ring therefore does not demonstrate minute-long coverage despite zero lost
buffers/events. It contains 50,889 Heap Alloc, 346 Realloc and 51,569 Free
events. The allocation rows resolve to Unknown (-1); PID-filtered heap totals
are empty, so event attribution and live heap peaks remain unproven. See
`wpr-20868-trace-review.json` for the hash and limits. Administrator tracing
is now demonstrated; scoped client allocation caps and A0 remain unvalidated.

The subsequent file-mode capture `A0Heap-file-20261007-131213-20868.etl`
preserves the full 67.4308516-second event span, with zero reported lost
events/buffers. It contains 4,625,598 Heap Alloc, 29,050 Realloc and 4,619,670
Free events, and occupies 569,245,696 bytes. This resolves the circular-buffer
coverage failure. However, local xperf heap totals still report Unknown (-1)
and zero allocation totals, inconsistent with the event counts; these totals
cannot validate allocation sizes or live heap peaks. The configured PID is
20868; independently decoded attribution and an A0_RESULT identifying the ON
phase remain absent. Admin collection is complete for this sample; analysis
and the independent physical timing proof remain open. See
`wpr-file-20868-trace-review.json`. A0 remains unvalidated.

Offline native ETW decoding now resolves the allocation event header PID to
20868. `wpr-file-20868-decoded.json` records exact count agreement with xperf
for Alloc/Realloc/Free. The reader obtains the property layout from TDH and
validates each payload against it. This trace emits Alloc/Free as well as
Realloc for each reallocation: all 29,050 reallocations have a previously
observed new address and an already-removed old address. The ledger avoids
counting those new blocks twice. It reports 1,524,291,936 requested bytes for
observed new blocks, 70,298,184 bytes described by realloc events separately,
largest request 98,152 bytes, and an observed-live peak of 1,250,739 bytes.
There are zero duplicate live addresses and zero rejected payload layouts;
32,137 frees have no observed allocation, consistent with allocations live
before tracing. Thus the observed-live peak excludes that initial population
and non-heap paths, and is not the global/native/Dart peak. Snapshot events
have recorder PIDs and are not silently attributed to the target. Reader
positive controls pass; source/binary hashes are in `wpr-etl-reader-hashes.json`.
Event PID attribution is now proven; phase pairing and aggregate Dart/Flutter
transient accounting remain open.

The reset-window VM experiment is preserved in
`vm-accumulator-reset-observation.json`. The reset timestamp stays unchanged
through the measured 15-second window and both record/hop classes are present,
but zero classes have accumulatedSize above bytesCurrent. The VM reports
16,271,376 bytes for both interpretations; this does not demonstrate retired
allocation throughput. AnalysisRecord reports 17 current/accumulated instances,
CompactHop 1,214 current/accumulated instances. No service GC or allocation
tracing was requested. These observations rule out treating this API response
as a complete transient byte counter on this Profile runtime.

The accompanying new silent binary completes a 60.00056-second ON run with
6,003 records, full coverage, zero missing/resync/over-100ms progression stalls,
zero underruns/queue drops and successful stop. Save:
`windows-60-vm-reset-smoke.json`. Flutter analysis and all eight existing spike
tests pass. This is a short, VM-profile-perturbed run, not the required matched
ten-minute performance comparison. An opt-in physical phone stimulus is now
implemented behind A0_PHYSICAL_STIMULUS: manual ON only, 997 Hz native fixture
gated 250ms/750ms, flash from measured RMS. Default operation remains silent.
No audible/filmed run or calibrated physical lag is claimed. The available
operator equipment is PC/headphones/microphone/phone; the phone protocol in
`tools/audio_analysis_a0_windows/PHYSICAL_TIMING.md` is exploratory until its
A/V bias and uncertainty are independently established.

The opt-in physical-stimulus Dart branch also compiles and launches idle on
Windows. `phone-stimulus-compile-review.json` records the binary hash; the app
was quit without pressing Start ON. This distinguishes opt-in compilation
from the default-silent 60-second runtime proof, and from physical measurement.

Scope correction after the exact AC-A4 review: 256 KiB is the retained client
analysis context, 128 KiB is its incoming encoded response and 384 KiB is the
combined context/incoming budget with no unbounded decoded duplicate. Actual
data-path object/list/buffer capacity and overhead count; unrelated Flutter
framework, VM arenas and whole-process RSS do not belong to this cap. Older
references here to a global Flutter/Dart allocator budget are not an additional
A0 requirement. WPR and VM whole-process observations remain useful context,
but do not replace scoped accounting. The private Map serializer also does not
implement the future binary response contract. Its observed serialized response
101,661 bytes is below 128 KiB; native map+codec peak 382,248 bytes is a distinct
allocation measure and must not be compared as though it were wire size.

Scoped retained-object review: the original growable-list view exposes 12,976
VM object bytes but hides its backing-array capacity. This lower-bound result
is in `vm-analysis-view-growable-review.json`; it is not a cap proof. The private
view now uses `CalibrationHopList` with one fixed 151-slot nullable array,
retains at most 150 hops after trimming, and clears removed/shrunk/reset slots.
Regression checks cover overflow at 152, ordering, clearing/reuse and resync.
The new transient counter observes 151 before trimming; the historical retained
high-water stays 150. Flutter analyze/tests pass.

On the compiled Windows Profile binary, a five-second silent mask11 phase
completes with 503 records/full coverage, no missing/resync/progress stalls,
zero underruns/queue loss and successful stop (`windows-5-fixed-hop-smoke.json`).
After stop, the VM-service graph remains stable at cursor 503. Using a persistent
WebSocket and a Ring/ReuseExisting ID zone avoids double-counting references.
The graph has 490 objects, 790 edges and 14,208 VM-reported shallow bytes:
view128, record80, hop-container32, fixed backing1,232, three immutable vector
lists2,704, 326 double objects5,216, 150 compact hops4,800, Null16, Smi0. There
are no missing sizes or hidden growable backing arrays in these selected roots;
the ID zone is deleted afterward. Summary:
`vm-analysis-view-fixed-review.json`; source/binary/helper hashes:
`fixed-hop-view-source-hashes.json`. This is a point measurement of the private
post-stop retained graph, including its exposed object headers/capacity. It
excludes listeners/framework, allocator arenas/slack, incoming codec/batch
objects and the future production 32-record/166-hop context. It does not prove
the complete AC-A4 retained/incoming overlap or a transient peak.

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

Current engine builds, tests and private A0 runtime commands use Doppler NONE.
Earlier Doppler launches remain historical evidence, not the current recipe. Reproduce
native checks using `tools/audio_analysis_a0/README.md`; reproduce the private
Windows run using the sibling diagnostic README. Initial calibration needed no
external research; the later clock investigation used Parallel discovery and
Microsoft primary documentation as recorded above.

## Synthetic client transport graph, four masks

The private Windows Profile fixture parks five observable data roots per mask:
one same-mask prior visual record, 150 compact hops with fixed 151-slot storage,
exact incoming StandardMethodCodec bytes, their decoded map, and a parsed
16-record batch. It starts silently without the native audio channel. An
observable numeric checksum reads every intended record/hop field and all five
roots, preventing AOT elimination of unused fixture data. Flutter analyze and
all 12 tests pass; the reader's Dart analysis passes.

The complete snapshot and compact source/binary/snapshot-hash review are
`vm-transport-fixture-graph.json` and `vm-transport-fixture-review.json`.
All four cases have uniquely matched roots and complete selected traversal.
Numbers below are identity-deduplicated VM shallow object bytes plus reported
external properties (zero for these unions), excluding 400 bytes of shared
VM/type metadata per case. Columns are distinct scopes: do not sum wire length
to the incoming union, or sum overlapping per-root graphs.

| Mask | Encoded wire bytes | Prior context object bytes | Incoming union object bytes | All five roots union bytes |
| --- | ---: | ---: | ---: | ---: |
| 3 | 36,841 | 14,912 | 158,608 | 173,504 |
| 5 | 3,305 | 8,736 | 20,368 | 29,088 |
| 11 | 107,113 | 16,480 | 256,784 | 273,248 |
| 13 | 73,577 | 10,304 | 121,616 | 131,904 |

Decoded typed views reach a Uint8List backing; union deduplication is consistent
with shared encoded storage. Raw snapshot files remain under build artifacts.
The earlier v2 capture is rejected as incomplete: AOT removed priorRecord and
decodedPacket, and reduced unused hop fields. It cannot support complete
accounting. The corrected fixture's nonzero values and observed fields differ
from the earlier silent post-stop 14,208-byte graph; neither replaces the other.

These are parked synthetic objects, not an actual native-channel transient
peak, allocation throughput, allocator arenas/slack, or the future production
32-record/166-hop context. Native-shaped metrics are a bounded subset; wire
lengths are lower bounds for full native responses. The heap snapshot may
perturb the VM and trigger collection. Do not infer an AC-A4 cap pass or add
the independent native allocation peak to these measurements.

The operator confirms a PC, headphones, microphone and Samsung Galaxy S21.
`tools/audio_analysis_a0_windows/PHYSICAL_TIMING.md` now documents an ordinary
Video-mode exploratory recording and preservation of the original clip.
Samsung primary pages were discovered and verified through Parallel; they do
not establish camera A/V offset or calibrated uncertainty. No audible/filmed
test has been collected. Administrator WPR collection is complete; no further
admin step is required for this review. Actual channel peak, source/clock
uncertainty and calibrated physical audio/screen timing remain open. A0 remains
unvalidated; protocol/defaults provisional and no A2–A7 admission.

## Actual silent native-channel retained overlap and physical trace

The private capture now retains an actual Windows MethodChannel response before
installing its parsed batch into the existing view. Six selected roots are
parked per mask: exact received ByteData, a diagnostic Uint8List view over its
buffer, decoded map, parsed batch, prior latest record and prior 150-hop context.
No capture byte copy is added; the extra view/header is counted. Each mask is
an independent experiment held for review, not four production contexts.

On Headphones (Senary Audio), all four final captures contain exactly16 adjacent
records with no resync, prior missing records, exceptions, submission failures,
queue drops, worker failure or failed stop. Analysis and all21 Flutter tests
pass. The 120ms pause plus bounded short-reply sampling retains the same cursor;
short candidates are not installed. The first200ms trial accumulated >16 and
correctly triggered newest8 resync; that incomplete attempt is preserved.

| Mask | Exact encoded bytes | Prior shallow bytes | Incoming shallow bytes | Incoming external bytes | Six-root union bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| 3 | 39,601 | 14,912 | 130,688 | 39,616 | 185,200 |
| 5 | 6,065 | 8,736 | 25,984 | 6,080 | 40,784 |
| 11 | 109,809 | 16,480 | 158,592 | 109,824 | 284,880 |
| 13 | 76,273 | 10,304 | 56,960 | 76,288 | 143,536 |

Unions deduplicate object identities; prior/incoming share16 object bytes, so
do not simply add the columns. Reported external allocation sizes are distinct
from logical encoded lengths. Shared VM/type metadata400 bytes per case is
excluded. Original capture byte lengths match the runtime summary per mask.
The VM hides logical length on immutable typed-data wrappers (PlainInstance,
length0, no fields). The earlier v2 reader's substituted zero lengths are
rejected metadata. The final reader uses the directly retained
actualEncodedEnvelopeBytes scalar and rejects absent/zero/conflicting lengths.

Reviewed files: `native-transport-runtime.json`, `vm-native-transport-graph.json`,
`vm-native-transport-review.json`; earlier failures in
`native-transport-first-incomplete.json`, `vm-native-transport-first-incomplete-graph.json`
and `native-transport-v2-runtime.json`. Raw snapshots and the measured capture
binaries remain under build artifacts; hash/provenance paths are in the review.

This is complete selected post-parse co-residency accounting for these four
actual silent replies, not an observed temporal allocation peak, allocator
arenas/RSS/throughput, worst-case future production context, or an AC-A4 pass.
The snapshot may perturb/collect; the source retains one full record plus150
private hops, not production32 records/166 hops. The earlier nonzero synthetic
fixture remains complementary and separate. Do not add native peaks to client
graphs sampled independently.

The opt-in physical mode now has bounded512-event paint tracing, visible
R(run)/T(transition) identifiers and a uniform64x64 logical-pixel top-right
patch below its28-pixel label strip. Rows record software paint/frame times
with distinct origins and raw sample intervals/RMS/continuity/batch metadata;
no inferred acoustic edge is emitted. Source inspection records the32-sample
limiter offset and the fact that private center_frame is the first sample of
its RMS window, not an event timestamp. Source audit and compile/idle proof are
`physical-stimulus-source-mapping.json` and `physical-trace-compile-idle-review.json`.
The rebuilt physical Profile branch launched idle; live VM running/trace_active
are false, run0/events0. No StartON, audible pulse, filmed clip, calibrated
phone A/V offset or absolute clock uncertainty has been demonstrated. Diagnostic
trace overhead is separate from earlier silent OFF/ON performance baselines.

A0 remains unvalidated. Physical/source clock uncertainty and actual temporal
overlap/production representation remain review limits; no protocol/default
freeze or A2–A7 admission follows from the new parked-graph evidence.

## Operator disposition and remaining limitation

Diane accepts the S21 exploratory capture as sufficient for the current A0 decision, with the measurement limitation explicitly retained. The original clip is archived at `s21-exploratory/20261007_152221-original.mp4`; `s21-exploratory/video-review.json` records the file hash, observed audio/video properties and this disposition.

The clip shows the stimulus and contains the 997 Hz tone, but phone A/V offset, exposure, rolling shutter and microphone response were not calibrated. Physical audio-to-photon lag and uncertainty <=10 ms are therefore not established. The complete machine-readable `A0_RESULT` was not retained; only an excerpt is available in the video review. Do not present this operator acceptance as proof that the formal timing target was measured or met.

## Stage A8 supplemental Windows Profile review — 2026-10-08

A fresh private Profile run compared OFF with an ON-only repeat on the same
named Bose QC35 II endpoint and 10 ms period. The engine example CMake cache
remained `SHIPGLOWS_AUDIO_A0=OFF`; the disposable spike used its separate ON
cache. The result is **review complete, not ready**: callback p99 was below the
period and underruns were zero, while RTT max was 23.605 ms, submitted-frame
lag proxy max 108 ms, and one no-forward interval reached 144.571 ms. Physical
audio-to-screen lag/uncertainty and AC-A4 temporal allocation overlap remain
unproved. See
[`stage-a8-windows-profile-review.md`](../2026-10-08/stage-a8-windows-profile-review.md)
and `stage-a8-windows-profile-review.json` for the full captured ON result,
normalized OFF metrics, hashes, and measurement limits. This supplement does
not change the qualified A0 operator decision or freeze schema/defaults.
