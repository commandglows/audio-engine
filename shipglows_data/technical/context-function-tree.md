---
artifact: technical_module_context
metadata_schema_version: "1.0"
artifact_version: "1.0.0"
project: ShipGlows Audio Engine
created: "2026-08-31"
updated: "2026-08-31"
status: reviewed
source_skill: sg-docs
scope: structural-navigation
owner: ShipGlows Audio Engine maintainers
confidence: high
risk_level: high
security_impact: none
docs_impact: yes
linked_systems:
  - code-docs-map.md
  - ../../flutter/shipglows_audio/native/engine/
  - ../../flutter/shipglows_audio/windows/
  - ../../flutter/shipglows_audio/android/
depends_on: []
supersedes: []
evidence:
  - "Repository entrypoints inspected on 2026-08-31."
next_review: "2026-11-30"
next_step: "Update when entrypoint ownership changes."
---

# Context Function Tree

```text
Flutter ShipglowsAudio API
  -> MethodChannel platform bridge
    -> WindowsWasapiCapture | AndroidOboeCapture
      -> CaptureSession + AudioLifecycle + TimestampTracker
      -> SpscAudioRingBuffer
        -> SegmentedWavStore + journal.sga
          -> integrity metrics and Flutter status snapshot

Tests
  -> engine_tests
  -> interruption_bench
  -> Flutter unit and platform integration tests
```

## Entrypoints

- `flutter/shipglows_audio/lib/shipglows_audio.dart`: Flutter public API and
  status model.
- `flutter/shipglows_audio/windows/windows_wasapi_capture.cpp`: Windows
  capture, route monitoring, recovery, and storage workers.
- `flutter/shipglows_audio/android/src/main/cpp/android_oboe_capture.cpp`:
  Android callback, recovery, and timestamp polling.
- `flutter/shipglows_audio/native/engine/src/`: portable session and storage
  primitives.
- `tests/interruption_bench.cpp`: deterministic interruption policy proof.

## Maintenance Rule

Update this tree when an entrypoint, worker boundary, or platform adapter is
added, removed, or materially rerouted.
