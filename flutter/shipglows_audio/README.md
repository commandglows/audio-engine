# shipglows_audio

Private Flutter bridge for the ShipGlows native audio engine.

Product applications communicate with this package. The real-time capture and
storage implementation lives in the repository's platform-neutral C++ engine.

## Supported platforms

The package currently bridges Windows WASAPI and Android Oboe. It is private
infrastructure and expects a host product to provide a writable private session
directory and Android microphone permission.

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

## Validation boundary

Automated and available runtime evidence is recorded in
`../../shipglows_data/technical/contracts/verification.md`. Physical device unplug, default-route switching,
system suspension, and broader Android OEM evidence remain required before a
release-ready declaration.
