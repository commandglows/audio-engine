---
artifact: technical_module_context
metadata_schema_version: "1.0"
artifact_version: "1.0.0"
project: ShipGlows Audio Engine
created: "2026-08-31"
updated: "2026-08-31"
status: reviewed
source_skill: sg-docs
scope: platform-adapters
owner: ShipGlows Audio Engine maintainers
confidence: high
risk_level: high
security_impact: none
docs_impact: yes
linked_systems:
  - contracts/architecture.md
  - ../../flutter/shipglows_audio/windows/
  - ../../flutter/shipglows_audio/android/
  - ../../flutter/shipglows_audio/lib/
depends_on: []
supersedes: []
evidence:
  - "Windows and Android runtime integrations passed on 2026-08-31."
next_review: "2026-11-30"
next_step: "Update after a platform API, route, or Flutter status contract change."
---

# Technical Module Context: Platform Adapters

## Purpose

Platform adapters translate native capture APIs into the portable session
contract. They may manage streams and workers, but never move PCM through
Flutter or perform storage/locking work in their real-time callback path.

## Owned Files

| Path | Role | Edit notes |
| --- | --- | --- |
| `windows/windows_wasapi_capture.cpp` | event-driven WASAPI capture and recovery | Route monitoring stays outside packet consumption. |
| `android/src/main/cpp/android_oboe_capture.cpp` | Oboe input callback and recovery | Do not query timestamps from `onAudioReady`. |
| `windows/shipglows_audio_plugin.cpp` | Windows status serialization | Keep error/recovery fields backward-compatible. |
| `android/.../ShipglowsAudioPlugin.kt` | Android status serialization | Keep pipe-field order synchronized with C++. |
| `lib/shipglows_audio.dart` | Flutter status API | Add default values for additive diagnostics. |

## Control Flow

```text
native packets/callback
  -> preallocated SPSC ring + atomic counters
  -> storage worker
  -> segmented WAV journal
  -> compact Flutter status snapshot
```

## Invariants

- Legacy WASAPI input calls and Oboe use bounded reconnect sequences after eligible device loss.
- Explicit Windows sources (`microphoneEnabled`, `inputEndpointId`, `outputEndpointId`) freeze exact endpoints for the entire take. Missing, inactive or wrong-flow endpoints reject startup; source loss/suspend interrupts the take without selecting another device. Pause does not unlock source selection.
- Route/device recovery creates causal journal checkpoints and lifecycle
  generations rather than appending across an unknown timebase.
- Oboe hardware timestamps are queried on a non-callback worker.
- Flutter receives recoverability information, not raw audio. Device enumeration additionally returns stable local endpoint IDs for explicit selection and preferences; never include these IDs in diagnostics or uploads.

`windows_wasapi_sources.cpp` requests shared float32 stereo 48 kHz conversion for both input and loopback, aligns packets with QPC timestamps, and feeds the existing segmented store through the bounded ring. Dual-source mixing applies half gain per source for headroom. `wasapi_stereo_timeline.h` owns bounded accumulation. Input/output enumeration lists the actual Windows multimedia default first, while saved explicit IDs always remain authoritative.

2026-09-05 proof: seven Flutter API/channel tests and MSVC offline source tests pass. Live Windows source tests passed output-only and microphone-plus-output, elapsed frames, pause/resume and finalization. Subsequent pause-tail and writer-failure review fixes passed MSVC offline tests; physical unplug/suspend across the supported hardware matrix remains a release proof gap.

## Validation

```powershell
Push-Location flutter/shipglows_audio
flutter analyze
flutter test
Pop-Location

Push-Location flutter/shipglows_audio/example
flutter test integration_test/plugin_integration_test.dart -d windows
flutter build windows --debug
flutter build apk --debug --target-platform android-arm,android-arm64,android-x64
Pop-Location
```

## Reader Checklist

- `WASAPI`, `Oboe`, `route`, `device`, `Flutter status`, or `timestamp` ->
  read this file and `recording-recovery-index.md`.

## Maintenance Rule

Update this document when a platform adapter, real-time constraint, recovery
contract, serialized diagnostic, or validation route changes.


### 2026-09-05 system-audio process loopback

Enumeration prepends `shipglows:system-audio` on runtime OS build >=20348. This
virtual capture source is not an IMMDevice ID. Process-loopback activation
excludes the current process tree. System-only bypasses IMMDeviceEnumerator;
mic/system resolves only the selected microphone. Exact endpoint choices retain
no-fallback behavior. The agile COM callback owns its event and parameters,
including after bounded activation timeout. Process capture uses event-based
initialization, zero requested duration and no endpoint NOPERSIST flag.

Output silence uses a separate pre-mix stereo timeline (-60 dBFS peak threshold).
Only recorded frames advance counters; absent packets count as silence. Status
snapshots are mutex-protected. Unsupported OS, activation/capture errors and
suspend remain explicit; no automatic method/endpoint switch occurs.


Verification on Windows build 26200 (2026-09-05): dedicated MSVC native source
harness passes offline cases and live explicit-output/system-only/mixed capture,
pause/resume/finalization. A quiet run reported 614/642 ms system monitoring and
the same consecutive silence. A controlled 440 Hz tone from a sibling process
(outside the excluded capture process tree) reported 630 ms monitored/11 ms
silence for system-only and 668 ms/13 ms for mic+system; both finalized WAV files
contained nonzero samples above 0.005. The fixture waits for playback readiness;
a run without that handshake correctly failed the signal assertions. No device
was disabled or playback setting changed. Physical unplug and no active outputs
remain unverified. Flutter plugin Dart suite: 8 passed; analyzer clean.

Build the native harness using one consistent CMake installation, for example
Visual Studio's bundled CMake with `-S . -B build/process-loopback-msvc -G
"Visual Studio 17 2022" -A x64`, then build target
`shipglows_audio_sources_tests --config Debug`. Invoke without arguments for
offline tests, with `--live` for local capture, and `--live --signal` while a
known external process renders sound to require positive waveform assertions.
Elapsed frames alone intentionally do not satisfy the signal assertions.
