# shipglows_audio

Private Flutter bridge for the ShipGlows native audio engine.

Product applications communicate with this package. The real-time capture and
storage implementation lives in the repository's platform-neutral C++ engine.

## Getting Started

This project is a starting point for a Flutter
[plug-in package](https://flutter.dev/to/develop-plugins),
a specialized package that includes platform-specific implementation code for
Android and/or iOS.

For help getting started with Flutter development, view the
[online documentation](https://docs.flutter.dev), which offers tutorials,
samples, guidance on mobile development, and a full API reference.

## Capture recovery contract

`ShipglowsAudioCaptureStatus` exposes `errorCode`, `errorRecoverable`, and
`recoveryAction`. Transient device-loss and route-change errors use
`automatic_reconnect`; terminal errors use `start_new_session`. The current
session directory remains the recovery source of truth: finalized WAV segments
and their SHA-256 journal records are never replaced during reconnection.

Integrity diagnostics include captured/dropped frames, discontinuities, native
xruns, ring overflow, route changes, device restarts, timestamp gaps,
`hardwareTimestamps`, and `timestampQueryFailures`. Treat a non-empty terminal
error or rising loss counters as an incident even when earlier segments remain
recoverable.

Hardware timestamp queries are performed outside Oboe's data callback. The
callback only copies PCM into the preallocated SPSC buffer and updates lock-free
counters.
