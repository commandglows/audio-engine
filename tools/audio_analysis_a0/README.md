# A0 private native calibration prototype

Disposable worker-side prototype, not a production API, sidecar writer, or protocol freeze. Implements the four masks against 48 kHz stereo float PCM and the draft default native transform. Its fixed in-memory `Record` reserves optional arrays for all masks; bridge serialization must omit unrequested fields. Speech has no rolling arrays and keeps one compact logical hop. Physical values and visual values have distinct fields.

`Processor` must be heap allocated (918,256 bytes in the tested GNU x64 Release build). Worker owns it exclusively. Its fixed 128-record history uses approximately 832 KiB and is independent of stream duration. History overwrite preserves transform state; PCM position gaps reset continuity/history/transform. `finish()` is only for known clean EOF; `end_gap()` closes an uncertain/drop tail without publishing or padding pending work. Invalid input throws on the worker, never on the callback.

After sparse band-overlap preparation, the GNU Release processor is 984,304
bytes (+66,048 fixed bytes). The 128-record history and wire layout are unchanged.
Only spectral masks prepare coefficients at construction; per-hop band mapping
uses their nonzero contiguous spans. Independent full-bin reference tests require
exact float equality including recurrence and partial EOF. The extra fixed table
is currently reserved for all masks in this disposable prototype.

`BlockQueue` allocates its actual 16-slot negotiated maximum off callback, admits up to 16,384 whole stereo frames per slot, and exposes nonblocking SPSC copy/publish and copy/pop. One producer and one consumer only. Adapter must establish successful submission before publish; release failure must appear as a PCM discontinuity. Full blocks are dropped entirely; subsequent sample positions reveal gaps. Adapter owns final-drop marker, shutdown/join fence, endpoint negotiation, submitted-silence metadata and optionality. No claim that the prototype alone supplies these adapter guarantees.

## Reproduce

`audio_analysis_a0_allocations` is a separate Release probe replacing ordinary
C++ `new/new[]` on the measured thread, with a positive control. It counts
requested bytes, not retained memory or allocator overhead. For each mask it
measures setup separately from 3,000 nonzero hops through queue push/pop,
processor push and EOF. CTest rejects any observed steady-state allocation.
It excludes direct malloc/realloc, aligned allocation, other threads, native
Flutter codec/maps, Dart and actual WASAPI callbacks. Compiler allocation
elision can affect setup counts; these are observed calls, not object counts.
Run directly; this project's Doppler configuration is `NONE`.

For the optional bridge allocation probe, configure with an explicit SDK wrapper:
`'-DA0_FLUTTER_WRAPPER=C:/Users/Diane/ShipGlows/beatglows/spikes/audio_analysis_a0/windows/flutter/ephemeral/cpp_client_wrapper'`.
This adds `audio_analysis_a0_bridge_allocations` and a CTest check. It compiles
the selected SDK's actual `standard_codec.cc` and shared `bridge_record.hpp`.
It counts record-map/list construction separately from one success-envelope
encoding for masks 3/11/5/13 and batches 0/1/8/16. It excludes Poll metrics,
WASAPI, channel dispatch, Dart, direct malloc and aligned allocation. An
independent legacy construction must produce identical encoded bytes.
The Windows probe performs two encodes (size instrumentation and dispatch);
the standalone result reports one encode, not the total Poll cost. Its GNU
Release allocator result is not an MSVC/Profile runtime allocation measurement.

The allocation probe also follows observed ordinary-new pointers in a fixed
4,096-slot table to measure live requested payload and its peak. It aborts on
tracker exhaustion and excludes its own table storage. Positive controls check
allocate/free peaks and freeing a previous-scope object without corrupting the
current scope's counters. It never prepends metadata to returned allocations:
GNU runtime allocations can cross the interception boundary. Requested payload
excludes allocator metadata, unobserved allocation/free paths and aligned calls.
Map-plus-one-encode peak adds the unchanged response's retained requested bytes
to the measured encoder peak; it excludes Poll metrics and native dispatch.

`clock_review.hpp` is a fixed single-writer diagnostic accumulator. It separates
S_OK readings from S_FALSE/errors/invalid query brackets, rejects observed
position/QPC regression or frequency changes for rate review, and compares
elapsed device ticks/frequency against elapsed QPC (100 ns units). Its relative
rate is an observation over the first-to-last valid sample interval, including
startup behavior; it is not a bound on drift, timestamp accuracy or acoustic lag.

The review additionally reports the interval beginning at the first accurate
reading at least one QPC second after the first reading. Both full/startup and
subsequent interval results and endpoint pairs remain visible. This one-second
diagnostic split does not prove settling or permit dropping failed full-run data.

`clock_candidate.hpp` validates a private raw-device/submitted-frame
correspondence. It rejects zero-position startup, non-S_OK/zero frequency,
inactive/inconsistent streams, overflow, QPC outside the native query bracket,
future/stale observations and positions beyond submitted PCM. Device frame
conversion preserves the fractional component separately; the raw correlated
QPC time belongs to that fractional position, not automatically to its floor.
It never publishes the spec's integer-frame presentation anchor or a known
uncertainty. Nominal 2 ms query/250 ms age checks are only diagnostic candidate
filters and do not measure the device timestamp error.

Run from engine root without Doppler (`NONE`):

```powershell
cmake -S tools/audio_analysis_a0 -B build/audio_analysis_a0 -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/audio_analysis_a0
ctest --test-dir build/audio_analysis_a0 --output-on-failure
build/audio_analysis_a0/audio_analysis_a0.exe 30
```

The final command processes 30 seconds of generated PCM for each mask as fast as possible, not 30 seconds of real-time playback. Per-hop timers include worker measurement/transform/copy, exclude input generation, and omit the separately performed final EOF flush. Wall duration includes generation. They are elapsed-cost measurements rather than CPU utilization, callback cost, RTT, UI latency, endpoint proof or ten-minute playback. Input-copy counter counts only the processor's 8-byte/frame stereo copy; queue producer/consumer, record overwrite, bridge serialization and Flutter copies require their own counters. FFT counter counts per-channel transforms (two per spectral record). Working-set/process memory and allocator measurements are separate from `sizeof`.

The Windows diagnostic request sets `silent_output=true`: it still runs the deterministic source and DSP, then submits digital zeroes to the endpoint and analyses zero-valued PCM. This keeps the callback, queue, worker, FFT, transform and bridge workload without audible output. It does not validate feature values for the tone or represent post-limiter analysis of non-silent PCM. Native worker metrics now separate total mutex wait, maximum mutex wait, processor push time/max, Poll mutex hold/wait, queue high-water occupancy and the last dropped block's frame position/elapsed time. These metrics identify correlation; they do not by themselves prove a scheduling root cause. Stop acknowledgement failure is surfaced in the native response.

After the 2026-10-07 v2 paired run, Poll was changed to copy up to 16 fixed-size records plus counters while holding the worker mutex, then build Flutter maps/vectors after releasing it. Use `--dart-define=A0_AUTORUN_ON_ONLY=true --dart-define=A0_SECONDS=600` for a single-phase follow-up of this lock-scope change; the default autorun still performs matched OFF/ON phases.

The Windows bridge still allocates map/list/vector objects and performs an extra bounded codec encode for response-size instrumentation. Counters cover known PCM/float copies and encoded bytes, not every native or Dart allocation. Working-set and private-byte snapshots are retained separately; no total allocation count is claimed.

Reference tests cover all masks, first/last impulses, unclamped finite amplitude above one, lengths 1/479/480/481/1023/1024/2048/3001, chunk partitions, absolute nonzero origins, partial EOF with no synthetic decay, initial EMA from zero, declared gap reset, stereo anti-phase Parseval and sampled independent direct DFT, no unrequested FFT, queue overflow and NaN rejection. They are bounded A0 reference checks, not complete A2â€“A7 acceptance.

Worker-only timing counters separate physical measurement (including FFT and spectral power formation), transformation (including frequency-band mapping) and FFT (a subset of physical measurement; do not sum it again). Totals include final EOF flush. They exclude record clearing/history bookkeeping, PCM copying and input validation, which remain included in the aggregate push elapsed timer except EOF. Instrumentation itself has timing overhead. Independent release with a one-frame partial final hop is also checked.

The root agent owns Windows/plugin/Flutter integration and the identified calibration report. Public record layout, canonical config/hash, strict bridge validation, production sidecars and negotiated quality remain deferred.
