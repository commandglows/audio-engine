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

- WASAPI and Oboe use bounded reconnect sequences after eligible device loss.
- Route/device recovery creates causal journal checkpoints and lifecycle
  generations rather than appending across an unknown timebase.
- Oboe hardware timestamps are queried on a non-callback worker.
- Flutter receives recoverability information, not raw audio or private device
  identity.

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
