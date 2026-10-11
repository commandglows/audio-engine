---
artifact: technical_decision_record
metadata_schema_version: "1.0"
artifact_version: "1.3.2"
project: ShipGlows Audio Engine
created: "2026-10-01"
updated: "2026-10-04"
status: reviewed
source_skill: sg-docs
scope: engine-ownership-and-implementation-route
owner: Diane
confidence: high
risk_level: high
security_impact: none
docs_impact: yes
linked_systems:
  - ../audio-engine-research.md
  - ../../workflow/specs/shared-multi-app-audio-engine.md
  - juce-decision-matrix.md
depends_on: []
supersedes: []
evidence:
  - "Operator decision, 2026-10-01: 'on compare rien on construit notre propre moteur.'"
  - "Primary-source research on callbacks, platform I/O, OSS architecture, and licenses is recorded in ../audio-engine-research.md."
  - "Operator decision, 2026-10-03: validate the common DSP core offline first and include the ContentGlows Linux render worker as a shared-engine consumer."
  - "Operator decision, 2026-10-04: prioritize local Windows file playback with shared built-in effects in BeatGlows and ContentGlows; defer Linux offline export until after Windows playback."
  - "Operator approval, 2026-10-04: start with MP3/M4A/WAV and EQ, gate, compressor, and limiter; add ContentGlows tone/echo afterward. Include playback-speed control and evaluate separate pitch shift if quality and CPU allow."
  - "Operator decision, 2026-10-04: persist ContentGlows playback speed per timeline audio clip; default legacy clips to 1.0 and keep fixed clip timeline bounds."
next_review: "2027-01-01"
next_step: "Implement and prove local playback, effects, and persisted clip-speed behavior in both Windows apps; keep independent pitch shift, M4A/ALAC, broader BeatGlows player parity, and Linux export as later gates."
---

# Decision: own the shared audio engine

## Status

Selected by the operator on 2026-10-01.

## Decision

ShipGlows will implement and maintain its own shared audio engine in C++. The engine owns the real-time core: sample-frame clock, float processing blocks, mixer, routing graph, built-in DSP effects, device lifecycle contract, and recovery signals. Product apps own their projects, arrangement, editing, and UI.

The first product target is local Windows file playback at variable speed with shared EQ, gate, compressor, and limiter in BeatGlows and ContentGlows through Windows WASAPI; playback does not require the web render worker. Speed is a playback/transport control and baseline varispeed changes pitch with speed. A separate pitch-shift processor that preserves duration is optional and must meet agreed listening-quality and CPU criteria. Preserve existing capture behavior and keep file access and decoding outside realtime callbacks. After the Windows playback path is proven, the ContentGlows Linux render worker reuses the same DSP for offline audio export before Remotion composition and muxing. The current Android Oboe code remains the platform I/O adapter for a later physical-device phase; it is not the engine or an effects dependency. Flutter and the render worker use versioned host boundaries around the same C++ core; Flutter never receives callback PCM.

Do not run a JUCE-versus-custom comparison. Do not integrate JUCE, miniaudio, SoLoud, a third-party audio engine, third-party DSP/effect implementation, or third-party effect plugins into the engine. The OSS source survey remains for learning architecture and real-time practices; it is not a dependency shortlist.

## Rationale

- The operator wants one reusable engine that can support different audio experiences across ShipGlows apps.
- BeatGlows is intended to become a DAW with built-in voice effects, and ContentGlows needs shared mix/effect processing for short-video editing and final export. The existing engine provides capture and recovery but no playback/DSP/offline render path.
- Owning the C++ core keeps shared behavior, effect design, API evolution, and product boundaries under ShipGlows control.
- Native platform APIs cover device I/O without adopting a complete third-party audio framework. Existing Android Oboe use is isolated as an adapter and can be reviewed separately from engine ownership.
- The research report defines callback, buffering, clock, recording, and device-recovery invariants that the custom implementation must satisfy.

## Consequences

- ShipGlows must implement and validate the mixer, processing graph, device rendering, parameter updates, built-in effects, and failure recovery itself.
- Begin with local Windows file playback and shared effects in both apps, with real-device playback and app integration proof. Then use deterministic offline DSP/mix tests and the Linux render worker to validate export reuse; offline tests do not prove audible/realtime readiness.
- Keep callbacks bounded and allocation-free, and move graph creation, decoding, storage, and UI work to non-real-time threads.
- After local file playback, prove the common offline DSP graph against ContentGlows audio fixtures, then expose the same processors to the bounded one-microphone Windows monitoring/recording path and staged built-in voice chain in the shared spec.
- Keep Android parity as a later Oboe/AAudio device-validation milestone.
- Revisit this decision only if the operator changes engine ownership or the custom route fails a stated acceptance gate; do not reopen a framework comparison by default.

## References

- [Shared audio engine research](../audio-engine-research.md)
- [Shared multi-app audio runtime spec](../../workflow/specs/shared-multi-app-audio-engine.md)
- [Historical JUCE architecture study](juce-decision-matrix.md)
