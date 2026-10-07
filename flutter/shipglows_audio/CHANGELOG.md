## Unreleased

* Add stable Windows endpoint IDs and independent microphone/render-loopback source selection.
* Align and mix selected sources into one segmented WAV; lock sources throughout each take and preserve audio on source interruption.

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

- Windows: supported-build process loopback system audio, independent of selected
  output endpoint, excluding the recording app process tree.
- Pre-mix output monitoring/consecutive silence milliseconds; endpoint choices,
  disabled sources, pause locks and recoverable storage remain available.
