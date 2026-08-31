## Unreleased

* Add bounded WASAPI default-device recovery and causal route checkpoints.
* Add Oboe hardware timestamps outside the audio callback and expose timestamp
  coverage to Flutter.
* Add structured recoverability diagnostics and an automated interruption
  bench; physical interruption gates remain open.

## 0.1.0

* Add the portable C++ session, SPSC buffer, segmented PCM store, and recovery.
* Add a native Windows WASAPI event-driven capture backend.
* Add an Android Oboe low-latency capture backend with an input-preset fallback.
* Expose capture state, negotiated format, dropouts, discontinuities, clipping,
  device restarts, and stable errors to Flutter.
