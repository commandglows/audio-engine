---
artifact: technical_module_context
metadata_schema_version: "1.0"
artifact_version: "1.2.0"
project: ShipGlows Audio Engine
created: "2026-08-31"
updated: "2026-10-18"
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
  - ../workflow/evidence/synth-latency/2026-10-18/README.md
depends_on: []
supersedes: []
evidence:
  - "Current code and validation evidence were reviewed on 2026-08-31."
  - "2026-10-06: a distinct Windows local-playback surface and documentation route now sit beside capture; BeatGlows completed a ten-minute generated-WAV run, while device-period comparison, ContentGlows runtime proof, and full DSP verification remain open."
  - "2026-10-18: a real-time synthesizer and one shared Windows output bus (playback + synth -> master true-peak limiter -> WASAPI) were added, with an analysis tap of the submitted mix; latency, coexistence and integration evidence is in the synth-latency record. Android synth and wired-endpoint acoustic latency remain open."
next_review: "2026-11-30"
next_step: "Maintain the separate capture and playback contracts as new platform adapters and validation evidence are added."
---

# System Context

ShipGlows Audio Engine captures native PCM through Windows WASAPI and Android
Oboe, transfers it through a preallocated SPSC ring, and persists immutable
WAV segments with an append-only journal. A separate Windows playback host now
decodes local files and a real-time synthesizer plays notes on touch; both
render through one shared WASAPI output and report typed status and callback
metrics to Flutter. Capture recovery and output have distinct contracts and
validation gates.

## Major Surfaces

- Portable core: lifecycle, session state, timestamps, ring transport, WAV
  persistence, preflight, and metrics.
- Windows adapter: event-driven capture, default-route monitoring, bounded
  reopening, and storage control.
- Windows playback: local file decode, bounded PCM transfer, varispeed,
  EQ/gate/compressor updates, the always-on true-peak limiter, and
  error/underrun status.
- Real-time synth: portable `SynthEngine` (General MIDI drums, FM piano,
  metronome click) fed by a preallocated SPSC event queue; Windows only so far.
- Windows output bus: one process-wide WASAPI render stream mixing the attached
  playback source and the synth through a master limiter, with low-latency
  fill, MMCSS scheduling and an analysis tap of the exact submitted mix.
- Live analysis: bounded batches of levels, envelope and log spectrum bands
  for the playback or mixed output, polled from Dart with one request in flight.
- Android adapter: Oboe callback, bounded stream recovery, and hardware
  timestamp polling outside the callback.
- Flutter bridge: recording, playback and synth commands, analysis leases, and
  recoverable diagnostic snapshots.
- Validation: dependency-free C++ tests, interruption bench, Flutter tests,
  platform integration tests, builds, and physical/soak evidence.

## System Invariants

- The audio callback/capture packet loop never performs filesystem work,
  Flutter calls, encoding, allocation, or application locking. Inputs and the
  analysis tap attach to the render callback through atomics and detach behind
  an in-flight fence.
- Previously finalized segments are immutable and journaled before recovery.
- Recovery uses bounded retries and preserves an explicit causal event trail.
- Physical validation gates are distinct from simulation and package proof.

## Reader Checklist

- Device recovery or timestamps -> `recording-recovery-index.md`.
- Segment/journal integrity -> `session-integrity.md` and `contracts/session-format.md`.
- Platform adapter work -> `platform-adapters.md` and `contracts/architecture.md`.
- Windows local playback, effects, or playback failure status -> `windows-playback.md` and the shared multi-app engine spec.
- Real-time synth or the shared output bus -> `windows-playback.md` and the shared multi-app engine spec (Real-time Synthesizer).
- Live analysis sources and batches -> the shared audio analysis and visualization spec.
- Release evidence -> `contracts/verification.md`.

## Maintenance Rule

Update this overview when a major subsystem, ownership boundary, invariant, or
validation category changes.
