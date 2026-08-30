# JUCE-to-ShipGlows decision matrix

This document records architectural inspiration, not source-code derivation.
ShipGlows Audio Engine is independently implemented against operating-system
APIs and does not incorporate JUCE framework code.

| JUCE public design | ShipGlows decision | Rationale |
| --- | --- | --- |
| `AudioDeviceManager` owns discovery, setup, persistence, fallback, restart, and change notification | Adopt the responsibility boundary as `AudioDeviceManager`, with immutable device snapshots and stable IDs | Products must not each reinvent device recovery or persist platform-specific names |
| Device exposes channel names, sample rates, buffer sizes, default buffer size, latency, and xrun count | Adopt and extend with route type, native format, permission state, hardware timestamp support, and last error code | Required for preflight, reproducible diagnostics, and professional device selection |
| Explicit `aboutToStart`, callback, stopped, and error lifecycle | Adopt with `prepared`, `started`, `routeChanged`, `interrupted`, `stopped`, and `failed` events | A recorder must distinguish an orderly stop from an interruption or device loss |
| Callback context may include a host timestamp | Adopt monotonic host time plus continuous engine frame position and segment position | Enables gap detection, A/V alignment, latency measurement, and future musical timing |
| `AudioBuffer<float>` is the common processing representation | Adapt: preserve native lossless capture bytes, then expose a planar float32 worker/DSP bus | Avoid conversion in the real-time capture callback while preparing for musical DSP |
| `ThreadedWriter` drains a FIFO on a background thread | Adopt the separation, retain ShipGlows' stronger immutable segmented journal | A flushed monolithic writer is not sufficient evidence of crash recoverability |
| Saved device state may fall back to the default device | Adopt with explicit `preferred`, `effective`, and `fallbackReason` fields | Silent fallback hides production problems; fallback should work and remain diagnosable |
| CPU load and xrun metrics are exposed by the device manager | Adopt, keeping OS xruns separate from engine ring overflows and timestamp gaps | Conflating these signals prevents reliable root-cause analysis |
| Device setup UI and test sound are reusable components | Adapt into a Flutter preflight surface: input meter, test recording, playback, route and format | ShipGlows products share Flutter UX but the engine remains UI-independent |
| Audio processor graph supports plugins and musical routing | Defer implementation, define stable float32 block and clock contracts now | Avoid building a DAW prematurely while preventing a capture-only dead end |
| Format readers/writers support WAV, AIFF, FLAC and metadata | Adopt self-describing WAV segments first; add non-destructive export later | Any recovered segment should be playable without the manifest or proprietary tooling |
| Public repository, examples, CMake API, and contribution rules | Adopt reproducible examples, verification matrix, release gates, and dependency notices | Engineering process is part of reliability, but JUCE's internal repository remains private |

## Deliberate differences

- ShipGlows treats every finalized segment as an immutable recovery boundary.
- A session journal records gaps, route changes, restarts, and errors instead of
  representing only a list of media files.
- Raw capture masters are never destructively normalised, denoised, resampled,
  or encoded.
- Product analytics never receive audio content or device identifiers; exported
  diagnostics use stable, non-sensitive capability fields.
- Flutter never receives audio buffers over a method channel.

## Public JUCE references reviewed

- `juce_audio_devices`: device lifecycle, setup, xruns, load, workgroups.
- `juce_audio_basics`: typed buffers, FIFO and buffered sources.
- `juce_audio_formats`: asynchronous writing and format abstraction.
- `juce_dsp`: future processing graph and float block conventions.
- Public CMake API, examples, release branches, contribution rules, and SPDX
  dependency inventory.
