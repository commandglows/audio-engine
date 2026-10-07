# A0 private native calibration prototype

Disposable worker-side prototype, not a production API, sidecar writer, or protocol freeze. Implements the four masks against 48 kHz stereo float PCM and the draft default native transform. Its fixed in-memory `Record` reserves optional arrays for all masks; bridge serialization must omit unrequested fields. Speech has no rolling arrays and keeps one compact logical hop. Physical values and visual values have distinct fields.

`Processor` must be heap allocated (918,256 bytes in the tested GNU x64 Release build). Worker owns it exclusively. Its fixed 128-record history uses approximately 832 KiB and is independent of stream duration. History overwrite preserves transform state; PCM position gaps reset continuity/history/transform. `finish()` is only for known clean EOF; `end_gap()` closes an uncertain/drop tail without publishing or padding pending work. Invalid input throws on the worker, never on the callback.

`BlockQueue` allocates its actual 16-slot negotiated maximum off callback, admits up to 16,384 whole stereo frames per slot, and exposes nonblocking SPSC copy/publish and copy/pop. One producer and one consumer only. Adapter must establish successful submission before publish; release failure must appear as a PCM discontinuity. Full blocks are dropped entirely; subsequent sample positions reveal gaps. Adapter owns final-drop marker, shutdown/join fence, endpoint negotiation, submitted-silence metadata and optionality. No claim that the prototype alone supplies these adapter guarantees.

## Reproduce

Run from engine root; retain the application's explicit Doppler selection:

```powershell
doppler run --project beatglows --config dev -- cmake -S tools/audio_analysis_a0 -B build/audio_analysis_a0 -G Ninja -DCMAKE_BUILD_TYPE=Release
doppler run --project beatglows --config dev -- cmake --build build/audio_analysis_a0
doppler run --project beatglows --config dev -- ctest --test-dir build/audio_analysis_a0 --output-on-failure
doppler run --project beatglows --config dev -- build/audio_analysis_a0/audio_analysis_a0.exe 30
```

The final command processes 30 seconds of generated PCM for each mask as fast as possible, not 30 seconds of real-time playback. Per-hop timers include worker measurement/transform/copy, exclude input generation, and omit the separately performed final EOF flush. Wall duration includes generation. They are elapsed-cost measurements rather than CPU utilization, callback cost, RTT, UI latency, endpoint proof or ten-minute playback. Input-copy counter counts only the processor's 8-byte/frame stereo copy; queue producer/consumer, record overwrite, bridge serialization and Flutter copies require their own counters. FFT counter counts per-channel transforms (two per spectral record). Working-set/process memory and allocator measurements are separate from `sizeof`.

The Windows diagnostic request sets `silent_output=true`: it still runs the deterministic source and DSP, then submits digital zeroes to the endpoint and analyses zero-valued PCM. This keeps the callback, queue, worker, FFT, transform and bridge workload without audible output. It does not validate feature values for the tone or represent post-limiter analysis of non-silent PCM. Native worker metrics now separate total mutex wait, maximum mutex wait, processor push time/max, Poll mutex hold/wait, queue high-water occupancy and the last dropped block's frame position/elapsed time. These metrics identify correlation; they do not by themselves prove a scheduling root cause. Stop acknowledgement failure is surfaced in the native response.

After the 2026-10-07 v2 paired run, Poll was changed to copy up to 16 fixed-size records plus counters while holding the worker mutex, then build Flutter maps/vectors after releasing it. Use `--dart-define=A0_AUTORUN_ON_ONLY=true --dart-define=A0_SECONDS=600` for a single-phase follow-up of this lock-scope change; the default autorun still performs matched OFF/ON phases.

The Windows bridge still allocates map/list/vector objects and performs an extra bounded codec encode for response-size instrumentation. Counters cover known PCM/float copies and encoded bytes, not every native or Dart allocation. Working-set and private-byte snapshots are retained separately; no total allocation count is claimed.

Reference tests cover all masks, first/last impulses, unclamped finite amplitude above one, lengths 1/479/480/481/1023/1024/2048/3001, chunk partitions, absolute nonzero origins, partial EOF with no synthetic decay, initial EMA from zero, declared gap reset, stereo anti-phase Parseval and sampled independent direct DFT, no unrequested FFT, queue overflow and NaN rejection. They are bounded A0 reference checks, not complete A2â€“A7 acceptance.

Worker-only timing counters separate physical measurement (including FFT and spectral power formation), transformation (including frequency-band mapping) and FFT (a subset of physical measurement; do not sum it again). Totals include final EOF flush. They exclude record clearing/history bookkeeping, PCM copying and input validation, which remain included in the aggregate push elapsed timer except EOF. Instrumentation itself has timing overhead. Independent release with a one-frame partial final hop is also checked.

The root agent owns Windows/plugin/Flutter integration and the identified calibration report. Public record layout, canonical config/hash, strict bridge validation, production sidecars and negotiated quality remain deferred.
