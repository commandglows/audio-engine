---
artifact: technical_module_context
metadata_schema_version: "1.0"
artifact_version: "1.0.0"
project: ShipGlows Audio Engine
created: "2026-08-31"
updated: "2026-08-31"
status: reviewed
source_skill: sg-docs
scope: recording-recovery-behavior
owner: ShipGlows Audio Engine maintainers
confidence: high
risk_level: high
security_impact: none
docs_impact: yes
linked_systems:
  - platform-adapters.md
  - session-integrity.md
  - ../../tests/interruption_bench.cpp
depends_on: []
supersedes: []
evidence:
  - "Automated interruption bench and platform integration evidence were reviewed on 2026-08-31."
next_review: "2026-11-30"
next_step: "Add physical interruption evidence when hardware becomes available."
---

# Technical Behavior Index: Recording Recovery

## Purpose

This file owns recovery of operator terms around interrupted recording, device
loss, routes, timestamps, and recoverable errors.

## Operator Terms And Aliases

| Term | Meaning | Status | Notes |
| --- | --- | --- | --- |
| interruption | causal session boundary | canonical | Includes platform loss, device loss, and suspension. |
| route change | effective default input changed | canonical | Windows monitor or Android recovery may detect it. |
| reconnect | bounded stream/device replacement | canonical | Five attempts with exponential delays. |
| gap | timeline discontinuity | canonical | Distinct from an intentional pause. |
| recoverable error | Flutter-visible transient condition | canonical | `automatic_reconnect` communicates expected recovery. |

## Behaviors

### `device-loss-recovery`

- Entrypoints: `WindowsWasapiCapture::CaptureWorker`,
  `AndroidOboeCapture::onErrorAfterClose`, `RecoveryWorker`.
- Key symbols: `AudioLifecycle`, `TimestampTracker`, `SubmitStorageCommand`.
- Tests: `tests/interruption_bench.cpp`; platform integration test.
- Decisions: no durable decision record needed — bounded retries and causal
  segmentation are local engine contracts documented in `contracts/v0.2-contract.md`.

### `intentional-pause`

- Entrypoints: platform `Pause` and `Resume` methods.
- Key symbols: `CaptureSession::pause`, `SegmentedWavStore::checkpoint`.
- Tests: interruption bench and platform integration test.
- Failure/drift signals: pause no longer closes a segment, or resume does not
  advance the lifecycle generation.

### `hardware-timestamp-continuity`

- Entrypoints: WASAPI packet timestamp handling and Oboe storage worker.
- Key symbols: `TimestampTracker::observe`, `observe_position`.
- Tests: engine tests, interruption bench, platform integration test.
- Failure/drift signals: timestamp query returns to a callback path, gap counts
  grow under continuous capture, or a new generation is treated as one clock.

## Recovery Path

```text
interruption / route / reconnect / timestamp
  -> named behavior above
  -> platform adapter and portable lifecycle
  -> interruption bench and platform integration proof
  -> contracts/verification.md for physical-gate status
```

## Maintenance Rule

Update this index when recovery reasons, backoff policy, Flutter recovery
fields, timestamp strategy, relevant tests, or physical-gate status changes.
