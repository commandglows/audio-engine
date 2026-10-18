---
artifact: technical_module_context
metadata_schema_version: "1.0"
artifact_version: "0.4.0"
project: ShipGlows Audio Engine
created: "2026-10-06"
updated: "2026-10-18"
status: draft
source_skill: 300-sg-docs
scope: windows-local-playback
owner: ShipGlows Audio Engine maintainers
confidence: medium
risk_level: high
security_impact: none
docs_impact: yes
linked_systems:
  - flutter/shipglows_audio/windows/windows_wasapi_playback.cpp
  - flutter/shipglows_audio/windows/windows_output_bus.cpp
  - flutter/shipglows_audio/windows/shipglows_audio_plugin.cpp
  - flutter/shipglows_audio/native/engine/src/synth_engine.cpp
  - flutter/shipglows_audio/native/engine/src/dsp_chain.cpp
  - ../workflow/specs/shared-multi-app-audio-engine.md
  - ../../../beatglows/shipglows_data/technical/audio-playback-model.md
depends_on: []
supersedes: []
evidence:
  - "2026-10-06 source review: the Windows plugin exposes local playback, speed and effect methods, and reports WASAPI status and callback metrics through its Flutter channel."
  - "2026-10-06 BeatGlows run: a generated WAV completed ten minutes on the named system-default render endpoint; the product UI reported zero underruns and p99 callback time 127 microseconds. Process-loopback limits are recorded in the BeatGlows feasibility report."
  - "2026-10-06 engine DSP verification: deterministic limiter fixtures passed an independent 64x windowed-sinc meter for high-frequency sine, start/end impulses, Nyquist alternation, multisine, and fixed-seed noise across varied block sizes."
  - "2026-10-06 portable DSP timing probe: optimized GNU C++ measured 3,638 microseconds p99 and 7,437 microseconds maximum per 480-frame block over 5,000 calls. This is DSP-only on a synthetic block, not a WASAPI callback measurement or comparison against a device period."
  - "Engine fixes, 2026-10-13: WASAPI playback now records GetDevicePeriod and the opened endpoint name and reports them with callbacks, maximum callback time and buffer frames; an explicit Stop() never signalled completion, so seek-while-playing and stop would block the platform thread (fixed); a device invalidation now stops the stream on the render thread and keeps the typed failure, and the next play reopens the default endpoint without an implicit switch."
  - "BeatGlows ten-minute post-limiter run, 2026-10-13: Doppler-wrapped `flutter drive --profile` of `app_flutter/integration_test/windows_endpoint_soak_test.dart` played a generated 600 s, 440 Hz stereo PCM16 48 kHz WAV at 0.99 full scale through the real shipglows_audio host to completion on the default endpoint `Headphones (Mon Bose QC35 II tupeupatest)`: 0 underruns, 60,079 callbacks, callback p99 at most 1,023 microseconds (power-of-two bucket bound), maximum 1,171 microseconds, WASAPI device period 10,000 microseconds (minimum 3,000), buffer 1,056 frames. Three 30 s process-loopback captures at 1, 5 and 9 minutes had 0 discontinuities, silent packets and timestamp errors, only the 30-40 ms capture-cutoff tail, RMS 0.594, peak 0.843 (the 0.99 input held at the -1.5 dBTP internal guard) and a 440 Hz estimate. Seek while playing returned in 58 ms and stop in 26 ms. The underrun counter counts ring starvation, not missed WASAPI deadlines; this is process-scoped evidence, not human listening."
  - "Earlier 2026-10-13 run with the unoptimized limiter: also 0 underruns and p99 bound 8,191 microseconds below the 10,000 microsecond period, but maximum callback 73,686 microseconds; the 1-minute capture showed nine inserted 10-40 ms gaps. The limiter detector was then restructured (tap-major coefficients, same per-phase summation order): MSVC Release cost per 1,056-frame block fell from about 7.0 ms to 0.93 ms with byte-identical output over 2,000 blocks, and the DSP CTest still passes."
  - "Decoder trials, 2026-10-13: `tests/decoder_trials` generates original synthetic fixtures with Media Foundation encoders and checks 24 cases (WAV PCM16/24/32/float 8-192 kHz, MP3 44.1/48 kHz, AAC-LC M4A 44.1/48 kHz, and ALAC, 6-channel, 8-bit, out-of-range rate, corrupt, empty and mismatched-extension rejections): 23 pass and the truncated WAV decodes its 0.8 s of valid data. Two decoder bugs were fixed: a rejected Open() left the reader open, and MF-encoded AAC-LC M4A was rejected because MF reports profile indication 0 (now confirmed from the AudioSpecificConfig). FLAC cannot be generated with MF and HE-AAC has no fixture."
  - "Physical device-interruption trial, 2026-10-13: the operator disconnected the Bluetooth `Headphones (Mon Bose QC35 II tupeupatest)` endpoint during BeatGlows playback. Output stopped and the app left the playing state (typed device failure surfaced), but Play restarted the track from 0:00. BeatGlows now keeps the last native position on an output failure and Play reloads at that position on the current default endpoint (decoder failures still restart); unit-tested, operator retest pending."
  - "Operator retest, 2026-10-13: after the resume fix, disconnecting the Bose endpoint mid-track and pressing Play resumed at the stopped position; reported as pass."
  - "Shared output bus, 2026-10-18: playback and the real-time synth now share one WASAPI stream with a master limiter. Probe and gtest evidence (zero underruns, no loopback gap while the synth starts and stops during playback, post-to-mix latency p50 15-21 ms / p95 26 ms on the Bose endpoint) is in `../workflow/evidence/synth-latency/2026-10-18/README.md`. The engine library is now optimized in Debug: unoptimized, the limiter cost about 6.4 ms per 10 ms block, enough to starve debug sessions once playback and the master limiter both run."
next_review: "2026-10-16"
next_step: "Run ContentGlows Windows playback of an authorized asset, and human listening on the named endpoint."
---

# Windows Local Playback

## Purpose

This module documents the shared Windows file-playback host used by product
adapters. The native host owns decode buffering, WASAPI output, effect updates,
and playback status; Flutter sends bounded commands and receives status without
owning the audio clock or callback.

## Owned Files

| Path | Role | Edit notes |
| --- | --- | --- |
| `flutter/shipglows_audio/windows/shipglows_audio_plugin.cpp` | Flutter channel, local source session, effect commands, status serialization | Keep source paths local to the host; return typed non-sensitive errors and numeric native HRESULT separately. |
| `flutter/shipglows_audio/windows/windows_output_bus.cpp` | Process-wide output bus: playback input + synth -> master limiter -> WASAPI, analysis tap, synth latency timing | Inputs and the analysis tap are published by atomic pointer and detached with an in-flight fence; never lock or allocate in `Render`. |
| `flutter/shipglows_audio/windows/windows_wasapi_playback.cpp` | Event-driven WASAPI render worker (MMCSS Pro Audio); optional IAudioClient3 period and capped queued fill | Keep callback work bounded; device invalidation must surface as a device error rather than a decode failure. |
| `flutter/shipglows_audio/native/engine/src/synth_engine.cpp` | Portable real-time synth: GM drums, FM piano, click, SPSC event queue, latency histograms | Allocation-free render; timbres track BeatGlows `core/synth/voices.dart`. |
| `flutter/shipglows_audio/native/engine/src/dsp_chain.cpp` | Stateful stereo EQ, gate, compressor, and always-on true-peak limiter | Parameter updates are bounded and applied at a processing-block boundary. The limiter uses 64-phase, 32-tap windowed-sinc peak detection, a -1.5 dBTP internal guard for the -1.0 dBTP contract, and 32 frames of lookahead. |
| `flutter/shipglows_audio/lib/shipglows_audio.dart` | Flutter method/status API | Treat missing status fields as defaults for compatibility. |
| `../beatglows/app_flutter/lib/core/audio_engine/windows_playback_adapter.dart` | BeatGlows transport adapter | Keep the queue and clock in BeatGlows; pass generation-tagged commands to the host. |

## Entrypoints

- `loadPlayback`, `playPlayback`, `pausePlayback`, `seekPlayback`, `setPlaybackSpeed`, `setPlaybackEffects`, `stopPlayback`, and `getPlaybackStatus` are the Flutter channel methods.
- `startSynth`, `noteOn`, `noteOff`, `click`, `setVoice`, `getSynthStatus` and `stopSynth` (`synthApiVersion: 1`) drive the synth on the same output.
- WASAPI playback opens `eRender/eConsole` when no explicit endpoint ID is supplied.
- Device invalidation maps to a typed `wasapi_device_unavailable` error; the status API also reports `nativeErrorHresult`, `underruns`, and `p99CallbackMicroseconds`.

## Control Flow

```text
product adapter
  -> generation-tagged Flutter method channel
  -> Media Foundation decode producer and bounded PCM ring   synth events -> SPSC queue
  -> WindowsOutputBus::Render: playback source (varispeed + DSP) + synth voices
  -> master true-peak limiter -> WASAPI render worker on the default endpoint
  -> analysis tap of the submitted mix; host status and metrics back to Flutter
```

## Invariants

- Flutter is not in the native render callback and does not estimate playback position.
- One WASAPI render stream serves the process. It runs while a playback session is attached (playing) or a synth session is open, and stops otherwise. Play/resume attach the playback source, pause/stop/seek detach it behind a callback fence, so a seek still never races the render thread. While the synth keeps the device running, pause and seek do not restart the device; when nothing else plays, resume opens the device again on the current default endpoint.
- The bus requests the IAudioClient3 minimum shared period and caps the queued fill at one period plus a quarter (600 frames for a 480-frame period); when IAudioClient3 is unavailable it falls back to the legacy shared initialization. The render thread runs under MMCSS "Pro Audio".
- Playback speed is finite and bounded to 0.5x–2.0x; the host applies it to source-frame consumption.
- Effect parameter updates enter a bounded queue and are applied by the DSP consumer between blocks.
- Current DSP source contains EQ, gate, compressor, and an always-on limiter. Its internal target is -1.5 dBTP; the deterministic independent 64x reference test verifies output at or below the contracted -1.0 dBTP ceiling for its sine, impulse, Nyquist-alternating, multisine, and fixed-noise fixtures. This test does not establish compliance for every possible signal or the normative BS.1770 measurement procedure.
- The limiter adds 32 frames of latency (about 0.67 ms at 48 kHz). At clean source EOF the local playback callback sends its already-zeroed output blocks through the DSP, draining those frames; forced stop or teardown does not flush the tail.
- Process-loopback confirms signal emitted by a process, not analog output or human perception. Record endpoint identity and device period separately when comparing callback p99 with the device deadline.

## Failure Modes

- Unsupported local media and decode errors return typed playback errors.
- WASAPI device invalidation is surfaced distinctly from source decode failure. The render thread stops the stream and keeps `wasapi_device_unavailable`; the next play reopens the system default endpoint; BeatGlows reloads at the last reported position. No implicit switch happens while playing. The 2026-10-13 Bluetooth disconnect trial confirmed the typed stop; the operator retest confirmed resume at the stopped position.
- An explicit stop completes and clears a previous stream failure; seek while playing and stop return without blocking the platform thread.
- Status reports `devicePeriodMicroseconds`, `minimumDevicePeriodMicroseconds`, `bufferFrames`, `callbacks`, `maxCallbackMicroseconds` and `outputEndpointName`. `underruns` counts this session's ring starvation (the bus then renders the synth alone for that block) plus device-level failures, not missed WASAPI deadlines; compare the callback maximum with the buffer duration too.
- An invalid effect update or full bounded update queue returns an error rather than silently accepting the command.

## Validation

- Run the focused Flutter API/channel and native DSP tests listed in `code-docs-map.md` after source changes. The standalone DSP test is configured from `flutter/shipglows_audio/native/engine/tests`; its Release CTest passes 1/1 on GNU C++ 13.2 with Ninja and on MSVC 2022.
- Decoder changes: run `tests/decoder_trials` (generated MP3/AAC-LC/WAV fixtures and rejections) through CTest; see its README.
- Bus or synth changes: run `shipglows_synth_engine_tests` (CTest), the plugin gtest runner `shipglows_audio_test` (its device tests skip without an endpoint), `example/integration_test/synth_integration_test.dart`, and `tools/synth_latency_probe` with and without `--with-playback` on the named endpoint.
- Build and run the Windows host with a licensed/generated fixture on the named render endpoint.
- Record app-reported underruns, callback p99, endpoint identity, device period, and bounded process-loopback results. Keep physical listening claims separate from process capture.

## Reader Checklist

- Flutter transport or UI -> product-specific adapter and product playback model.
- WASAPI output or device error -> this document and the shared multi-app engine spec.
- DSP effect or true-peak ceiling -> `native/engine/src/dsp_chain.cpp`, deterministic DSP tests, and the shared engine spec.

## Maintenance Rule

Update this doc and the code-docs map when a decoder, effect, status field,
endpoint policy, or platform validation gate changes.

## Private A0 calibration (2026-10-07)

The disposable native analyser is `tools/audio_analysis_a0/`; BeatGlows owns the
separate `spikes/audio_analysis_a0/` diagnostic app. Only its Windows CMake flag
`SHIPGLOWS_AUDIO_A0` registers the private `shipglows_audio/a0` channel. Ordinary
product builds do not instantiate this analyser or synthetic source. The optional
WASAPI observer admits preallocated storage before audio starts, copies successful
submitted PCM (including known silence), and never analyses or transforms on the
callback. Submission failures and queue drops fence continuity; a dropped final
tail is not padded as clean EOF. Stop now acknowledges the completed command.

The probe uses precomputed 997 Hz stereo anti-phase PCM at amplitude 0.025,
speed 1.0 and default shared DSP, including the limiter. It exercises the shared
WASAPI/DSP route, not Media Foundation decoding or either product's player.
Native reference tests and generated 30-second sequences are separate evidence
from actual endpoint runs. Both builds and execution use Doppler beatglows/dev.

Render p99 retains the existing coarse power-of-two upper-bound histogram.
A0 additionally measures render-through-submission/tap; these extra clocks and
buckets execute only with an observer. A bucket straddling the endpoint period
does not prove pass or fail. Process CPU, working set and private bytes include
Flutter; process peak working set is lifetime-wide. `worker_us` includes mutex
wait plus analysis/transform, excludes queue pop; separate pop/copy, FFT,
measurement and transform counters identify their own costs. FFT timing is a
subset of measurement timing, never an additional total.

The private float32 map transfer includes requested physical and visual fields,
not callback PCM. Its bounded extra codec-size probe adds a measured encoding
pass to RTT; this overhead is experimental, not a production bridge promise.
Native context capacity is bounded; native/Dart allocator counts and source-to-
photon/audible lag are separate proof. Submitted-frame lag is only a labelled
proxy. Records, configuration, source hashes and interruptions belong in
`shipglows_data/workflow/evidence/audio-analysis-a0/2026-10-07/`. The canonical
analysis spec remains draft 1.1.0; no schema freeze or Stage A readiness follows
from this prototype.
