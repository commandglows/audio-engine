# Audio engine architecture

## Ownership boundary

The C++ engine owns capture timing, PCM transport, integrity metrics, segmented
storage, and recovery. Platform adapters own only the operating-system audio
API. Flutter sends commands and receives small state snapshots; it never
receives audio frames on the real-time path.

## Windows capture path

The Windows adapter uses shared-mode, event-driven WASAPI on the default
multimedia capture endpoint. It accepts the endpoint's native PCM or float mix
format rather than forcing a resampling path.

1. WASAPI wakes a multimedia-class capture thread when packets are available.
2. The capture thread copies packets into a preallocated SPSC ring buffer.
3. A normal-priority storage thread drains the ring, measures clipping, and
   writes five-second immutable PCM segments.
4. Each completed segment causes a transactional manifest replacement.
5. Stop drains the ring before marking the manifest complete.

The capture thread performs no filesystem access, encoding, Flutter calls,
memory allocation in its packet loop, or application-level locking.

## Integrity semantics

- `framesCaptured` counts frames accepted by the engine ring buffer.
- `framesDropped` counts frames rejected because the ring buffer was full.
- `discontinuities` counts discontinuity flags reported by the operating
  system. It does not invent a lost-frame estimate.
- `clippedSamples` is measured off the real-time thread from stored PCM.
- `errorCode` contains a stable, non-sensitive machine code.

A clean stop means the ring was drained and `complete=true` was committed to
the manifest. An interrupted session remains recoverable from its frame-aligned
segments even when its manifest says `complete=false`.

## Planned platform parity

Android will implement the same contract with Oboe/AAudio. Apple platforms can
later implement it with Core Audio without changing the Flutter product API or
session format.
