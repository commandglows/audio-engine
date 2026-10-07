---
artifact: technical_module_context
metadata_schema_version: "1.0"
artifact_version: "1.1.0"
project: ShipGlows Audio Engine
created: "2026-08-31"
updated: "2026-10-06"
status: reviewed
source_skill: sg-docs
scope: system-overview
owner: ShipGlows Audio Engine maintainers
confidence: high
risk_level: high
security_impact: none
docs_impact: yes
linked_systems:
  - code-docs-map.md
  - contracts/architecture.md
  - contracts/session-format.md
  - contracts/verification.md
  - windows-playback.md
depends_on: []
supersedes: []
evidence:
  - "Current code and validation evidence were reviewed on 2026-08-31."
  - "2026-10-06: a distinct Windows local-playback surface and documentation route now sit beside capture; BeatGlows completed a ten-minute generated-WAV run, while device-period comparison, ContentGlows runtime proof, and full DSP verification remain open."
next_review: "2026-11-30"
next_step: "Maintain the separate capture and playback contracts as new platform adapters and validation evidence are added."
---

# System Context

ShipGlows Audio Engine captures native PCM through Windows WASAPI and Android
Oboe, transfers it through a preallocated SPSC ring, and persists immutable
WAV segments with an append-only journal. A separate Windows playback host now
decodes local files, renders through WASAPI, and reports typed status and
callback metrics to Flutter. Capture recovery and playback output have distinct
contracts and validation gates.

## Major Surfaces

- Portable core: lifecycle, session state, timestamps, ring transport, WAV
  persistence, preflight, and metrics.
- Windows adapter: event-driven capture, default-route monitoring, bounded
  reopening, and storage control.
- Windows playback: local file decode, bounded PCM transfer, WASAPI render,
  varispeed, EQ/gate/compressor updates, and error/underrun status. The
  true-peak limiter and full playback verification remain open.
- Android adapter: Oboe callback, bounded stream recovery, and hardware
  timestamp polling outside the callback.
- Flutter bridge: recording commands and recoverable diagnostic snapshots.
- Validation: dependency-free C++ tests, interruption bench, Flutter tests,
  platform integration tests, builds, and physical/soak evidence.

## System Invariants

- The audio callback/capture packet loop never performs filesystem work,
  Flutter calls, encoding, or application locking.
- Previously finalized segments are immutable and journaled before recovery.
- Recovery uses bounded retries and preserves an explicit causal event trail.
- Physical validation gates are distinct from simulation and package proof.

## Reader Checklist

- Device recovery or timestamps -> `recording-recovery-index.md`.
- Segment/journal integrity -> `session-integrity.md` and `contracts/session-format.md`.
- Platform adapter work -> `platform-adapters.md` and `contracts/architecture.md`.
- Windows local playback, effects, or playback failure status -> `windows-playback.md` and the shared multi-app engine spec.
- Release evidence -> `contracts/verification.md`.

## Maintenance Rule

Update this overview when a major subsystem, ownership boundary, invariant, or
validation category changes.
