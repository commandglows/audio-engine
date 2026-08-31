---
artifact: technical_module_context
metadata_schema_version: "1.0"
artifact_version: "1.0.0"
project: ShipGlows Audio Engine
created: "2026-08-31"
updated: "2026-08-31"
status: reviewed
source_skill: sg-docs
scope: session-integrity
owner: ShipGlows Audio Engine maintainers
confidence: high
risk_level: high
security_impact: none
docs_impact: yes
linked_systems:
  - contracts/session-format.md
  - contracts/v0.2-contract.md
  - ../../flutter/shipglows_audio/native/engine/
depends_on: []
supersedes: []
evidence:
  - "The two-hour WASAPI soak on 2026-08-31 verified 1,446 SHA-256-checked WAV segments."
next_review: "2026-11-30"
next_step: "Recheck after any journal or segment-format change."
---

# Technical Module Context: Session Integrity

## Purpose

The portable core turns frame-aligned PCM into independently playable WAV
segments and an append-only `journal.sga`. It owns state transitions, storage
preflight, segment checksums, causal checkpoints, and integrity metrics.

## Owned Files

| Path | Role | Edit notes |
| --- | --- | --- |
| `native/engine/src/segmented_wav_store.cpp` | segment and journal persistence | Never alter already finalized segment records. |
| `native/engine/src/capture_session.cpp` | lock-free session metrics | Keep counters source-specific. |
| `native/engine/src/audio_lifecycle.cpp` | lifecycle and timestamp tracking | Keep generations and retry bounds explicit. |
| `native/engine/src/recording_preflight.cpp` | writable-storage preflight | Preserve stable failure codes. |

## Invariants

- Finalized segments have a closed RIFF header, byte size, SHA-256 digest, and
  journal record before later recovery can rely on them.
- Checkpoints close the active segment and append a causal event; they do not
  erase preceding evidence.
- Audio blocks are frame-aligned; malformed blocks fail rather than truncating
  confirmed data.
- Analysis runs on storage workers, never in a real-time callback.

## Failure Modes

- `insufficient_storage` prevents recording before capture starts.
- A storage write failure marks the session failed while already committed
  segments remain recoverable.
- Timestamp gaps are measured separately from discontinuity and ring-overflow
  counters.

## Validation

```powershell
ctest --test-dir build/native -C Debug --output-on-failure
```

## Reader Checklist

- `journal`, `segment`, `checksum`, `storage`, or `recovery` -> read this file
  and `contracts/session-format.md`.

## Maintenance Rule

Update this document and its linked contracts when persistence, recovery,
metrics, or event semantics change.
