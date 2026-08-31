---
artifact: technical_contract
metadata_schema_version: "1.0"
artifact_version: "1.1.0"
project: ShipGlows Audio Engine
created: "2026-08-30"
updated: "2026-08-31"
status: reviewed
source_skill: sg-docs
scope: audio-engine-architecture
owner: ShipGlows Audio Engine maintainers
confidence: high
risk_level: high
security_impact: none
docs_impact: yes
linked_systems:
  - ../platform-adapters.md
  - ../session-integrity.md
depends_on: []
supersedes: []
evidence:
  - "Aligned with Windows and Android adapter behavior on 2026-08-31."
next_review: "2026-11-30"
next_step: "Update after an adapter or real-time ownership change."
---

# Audio Engine Architecture

## Ownership boundary

The C++ engine owns capture timing, PCM transport, integrity metrics, segmented
storage, and recovery. Platform adapters own only the operating-system audio
API. Flutter sends commands and receives small state snapshots; it never
receives audio frames on the real-time path.

## Windows capture path

The Windows adapter uses shared-mode, event-driven WASAPI on the default
multimedia capture endpoint. It records the initially negotiated session
format; when a replacement default endpoint differs, the shared audio engine
converts into that stable session format rather than mixing incompatible data
inside one WAV session.

1. WASAPI wakes a multimedia-class capture thread when packets are available.
2. The capture thread copies packets into a preallocated SPSC ring buffer.
3. A normal-priority storage thread drains the ring, measures clipping and
   levels, and writes five-second immutable WAV segments.
4. Each finalized segment is SHA-256 hashed and appended to `journal.sga`.
5. A route-monitor worker observes the default input outside the packet loop;
   loss or a route change checkpoints the session and starts bounded reopening.
6. Stop drains the ring before appending `event=session_complete`.

The capture thread performs no filesystem access, encoding, Flutter calls,
memory allocation in its packet loop, or application-level locking.

## Integrity semantics

- `framesCaptured` counts frames accepted by the engine ring buffer.
- `framesDropped` counts frames rejected because the ring buffer was full.
- `discontinuities` counts discontinuity flags reported by the operating
  system. It does not invent a lost-frame estimate.
- `clippedSamples` is measured off the real-time thread from stored PCM.
- `errorCode` contains a stable, non-sensitive machine code.

A clean stop means the ring was drained and `event=session_complete` was
committed to the append-only journal. An interrupted session remains
recoverable from finalized WAV segments even when no completion event exists.

## Android capture path

The Android adapter uses Oboe with a shared, low-latency input stream. It asks
for 16-bit PCM and permits Oboe's format conversion so the real-time callback
always receives one stable format. It first requests the `Unprocessed` input
preset and retries with `Generic` when a device does not expose that path.

The callback obeys the same real-time invariants as Windows and feeds the same
ten-second SPSC buffer, five-second segmented WAV store, journal, recovery,
and integrity metrics. Hardware timestamps are queried by the non-callback
storage worker. Oboe stream disconnects are exposed as a stable
`device_disconnected` error with a bounded automatic recovery attempt.

The Android native libraries compile and package for arm64-v8a, armeabi-v7a,
and x86_64. Real-device acoustic and interruption validation remains a release
gate; an APK build alone cannot prove microphone routing on every OEM device.

## Planned platform parity

Apple platforms can later implement the same contract with Core Audio without
changing the Flutter product API or session format.
