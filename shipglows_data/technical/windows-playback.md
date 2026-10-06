---
artifact: technical_module_context
metadata_schema_version: "1.0"
artifact_version: "0.1.0"
project: ShipGlows Audio Engine
created: "2026-10-06"
updated: "2026-10-06"
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
  - flutter/shipglows_audio/windows/shipglows_audio_plugin.cpp
  - flutter/shipglows_audio/native/engine/src/dsp_chain.cpp
  - ../workflow/specs/shared-multi-app-audio-engine.md
  - ../../../beatglows/shipglows_data/technical/audio-playback-model.md
depends_on: []
supersedes: []
evidence:
  - "2026-10-06 source review: the Windows plugin exposes local playback, speed and effect methods, and reports WASAPI status and callback metrics through its Flutter channel."
  - "2026-10-06 BeatGlows run: a generated WAV completed ten minutes on the named system-default render endpoint; the product UI reported zero underruns and p99 callback time 127 microseconds. Process-loopback limits are recorded in the BeatGlows feasibility report."
next_review: "2026-10-16"
next_step: "Run deterministic DSP/decoder tests and native Windows failure-path checks; finish the true-peak limiter before claiming the shared effect contract complete."
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
| `flutter/shipglows_audio/windows/windows_wasapi_playback.cpp` | Event-driven WASAPI render worker | Keep callback work bounded; device invalidation must surface as a device error rather than a decode failure. |
| `flutter/shipglows_audio/native/engine/src/dsp_chain.cpp` | Stateful stereo EQ, gate, and compressor | Updates are bounded and applied at a processing-block boundary. The true-peak limiter is not implemented. |
| `flutter/shipglows_audio/lib/shipglows_audio.dart` | Flutter method/status API | Treat missing status fields as defaults for compatibility. |
| `../beatglows/app_flutter/lib/core/audio_engine/windows_playback_adapter.dart` | BeatGlows transport adapter | Keep the queue and clock in BeatGlows; pass generation-tagged commands to the host. |

## Entrypoints

- `loadPlayback`, `playPlayback`, `pausePlayback`, `seekPlayback`, `setPlaybackSpeed`, `setPlaybackEffects`, `stopPlayback`, and `getPlaybackStatus` are the Flutter channel methods.
- WASAPI playback opens `eRender/eConsole` when no explicit endpoint ID is supplied.
- Device invalidation maps to a typed `wasapi_device_unavailable` error; the status API also reports `nativeErrorHresult`, `underruns`, and `p99CallbackMicroseconds`.

## Control Flow

```text
product adapter
  -> generation-tagged Flutter method channel
  -> Media Foundation decode producer and bounded PCM ring
  -> WASAPI render worker using the selected/default render endpoint
  -> host status and callback metrics back to Flutter
```

## Invariants

- Flutter is not in the native render callback and does not estimate playback position.
- Playback speed is finite and bounded to 0.5x–2.0x; the host applies it to source-frame consumption.
- Effect parameter updates enter a bounded queue and are applied by the DSP consumer between blocks.
- Current DSP source contains EQ, gate, and compressor stages. There is no true-peak limiter, so this source does not satisfy the `-1.0 dBTP` ceiling contract.
- Process-loopback confirms signal emitted by a process, not analog output or human perception. Record endpoint identity and device period separately when comparing callback p99 with the device deadline.

## Failure Modes

- Unsupported local media and decode errors return typed playback errors.
- WASAPI device invalidation is surfaced distinctly from source decode failure; the current source change has not been revalidated by a fresh native build in this workstream.
- An invalid effect update or full bounded update queue returns an error rather than silently accepting the command.

## Validation

- Run the focused Flutter API/channel and native DSP tests listed in `code-docs-map.md` after source changes.
- Build and run the Windows host with a licensed/generated fixture on the named render endpoint.
- Record app-reported underruns, callback p99, endpoint identity, device period, and bounded process-loopback results. Keep physical listening claims separate from process capture.

## Reader Checklist

- Flutter transport or UI -> product-specific adapter and product playback model.
- WASAPI output or device error -> this document and the shared multi-app engine spec.
- DSP effect or true-peak ceiling -> `native/engine/src/dsp_chain.cpp`, deterministic DSP tests, and the shared engine spec.

## Maintenance Rule

Update this doc and the code-docs map when a decoder, effect, status field,
endpoint policy, or platform validation gate changes.
