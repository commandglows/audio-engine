---
artifact: evidence
metadata_schema_version: "1.0"
artifact_version: "1.0.0"
project: ShipGlows Audio Engine
created: "2026-10-18"
updated: "2026-10-18"
status: recorded
source_skill: sg-development
scope: windows-real-time-synth-latency
owner: Diane
confidence: medium
risk_level: medium
security_impact: none
docs_impact: yes
linked_systems:
  - tools/synth_latency_probe/synth_latency_probe.cpp
  - flutter/shipglows_audio/windows/windows_output_bus.cpp
  - flutter/shipglows_audio/windows/windows_wasapi_playback.cpp
  - flutter/shipglows_audio/native/engine/src/synth_engine.cpp
  - flutter/shipglows_audio/example/integration_test/synth_integration_test.dart
  - ../../specs/shared-multi-app-audio-engine.md
  - ../../specs/shared-audio-analysis-and-visualization.md
---

# Windows real-time synth latency, 2026-10-18

Engine commit under test: `4002901af8a07a71823cea2ff8c87d07f0f1d552` (main).
Host: Windows 11 Pro 10.0.26300. The only active render endpoint was
`Headphones (Mon Bose QC35 II tupeupatest)` (Bluetooth A2DP); the wired
`Headphones (Senary Audio)` jack and the HDMI monitor endpoint were inactive.
No physical (acoustic) measurement was made.

## What is measured

`shipglows_synth_latency_probe` (Debug configuration; the engine library is
optimized in every configuration) drives the real `WindowsOutputBus` and posts
accented clicks 450-510 ms apart. A WASAPI loopback client on the same
endpoint records the engine mix.

- `queue_wait`: post to the start of the render block that applied the event
  (native histogram, 250 µs buckets).
- `loopback_onset_mixed`: post to the first loopback frame above threshold,
  timed from when the probe read the packet. This is the software path up to
  the Windows audio engine mix; 1 ms polling makes it an upper bound.
- `loopback_onset_presented` and `clock_estimated_output`: post to the
  presentation time reported by the driver (loopback packet QPC stamp and
  IAudioClock position). They include the Bluetooth link, which varied
  between runs (about 26 ms to 190 ms) and is outside the engine.

## Results

| Run (file) | Shared period / fill | queue wait p50 / max | mix onset p50 / p95 / p99 / max | presented p50 | underruns |
| --- | --- | --- | --- | --- | --- |
| `low-latency-100.json`, 100 clicks | 480 frames, fill 600 | 4.75 / 9.85 ms | 17.2 / 26.4 / 33.7 / 41.7 ms | 178.6 ms | 0 |
| `default-period-100.json`, 100 clicks | 480 frames, full 1,056 buffer | 4.75 / 14.5 ms | 21.0 / 25.9 / 29.9 / 30.7 ms | 39.3 ms | 0 |
| `with-playback-60.json`, 60 clicks + tone | 480 frames, fill 600 | 4.75 / 9.71 ms | 15.5 / 26.3 / 37.5 / 37.5 ms | 178.7 ms | 0 |

Coexistence (`with-playback-60.json`): a 220 Hz tone stayed attached as the
playback input while the synth started, played 60 clicks and stopped. Over
6,264 loopback windows of 5 ms there were 0 windows without the tone, 0
playback starvations and 0 device underruns. Callback p99 bucket bound
2,047-4,095 µs, maximum 2,687-3,768 µs across the three runs, against a 10 ms period.

The driver granted IAudioClient3 but its minimum shared period equals the
default 480 frames, so the low-latency gain comes from capping the queued
fill at one period plus a quarter (about 4 ms at the median).

Dart side (`synth_integration_test.dart`, debug build, printed as
`SYNTH_EVIDENCE`): fire-and-forget `noteOn` returned in p50 0.77 ms / p95
1.9 ms on the calling isolate; a full method-channel round trip took p50
1.6 ms / p95 2.5 ms; native queue wait p50 4.5 ms; clock-estimated output
p50 26.5 ms / p95 32 ms in that run; 248 analysis records of the mix with
peak 0.841 (the -1.5 dBTP limiter ceiling) and no delivery resync.

## Verdict

- Target < 30 ms on Windows: met at the median and p95 for the engine path
  (post to the Windows mix, 17-21 ms p50, 26 ms p95); p99 reaches 30-37 ms.
  Adding the Dart hop (about 1 ms one way) keeps p95 below 30 ms.
- Not proven: end-to-end acoustic latency on a wired endpoint. Bluetooth
  presentation adds a device-dependent delay the engine cannot remove.
- Next: repeat the probe on a wired or USB endpoint whose driver offers a
  smaller shared period (IAudioClient3 minimum below 480 frames).

## Reproduce

```powershell
cmake -S . -B build/msvc -G "Visual Studio 17 2022" -A x64
cmake --build build/msvc --config Debug --target shipglows_synth_latency_probe
build\msvc\tools\synth_latency_probe\Debug\shipglows_synth_latency_probe.exe --clicks 100
build\msvc\tools\synth_latency_probe\Debug\shipglows_synth_latency_probe.exe --clicks 100 --default-period
build\msvc\tools\synth_latency_probe\Debug\shipglows_synth_latency_probe.exe --clicks 60 --with-playback
```

The probe plays audible clicks (and a quiet tone with `--with-playback`) on
the default endpoint and exits 1 when a click is missing in loopback or the
tone shows a gap.
