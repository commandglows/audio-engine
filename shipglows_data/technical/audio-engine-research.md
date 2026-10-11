---
artifact: technical_research_report
metadata_schema_version: "1.0"
artifact_version: "1.2.0"
project: ShipGlows Audio Engine
created: "2026-09-30"
updated: "2026-10-01"
status: reviewed
source_skill: sg-docs
scope: shared-daw-audio-engine-research
owner: ShipGlows Audio Engine maintainers
confidence: medium
risk_level: high
security_impact: none
docs_impact: yes
linked_systems:
  - ../workflow/specs/shared-multi-app-audio-engine.md
  - contracts/juce-decision-matrix.md
  - contracts/engine-ownership-decision.md
  - code-docs-map.md
  - ../../../beatglows/shipglows_data/workflow/specs/beatbox-audio-player-flutter.md
depends_on: []
supersedes: []
evidence:
  - "Operator direction, 2026-09-30: create and reuse one ShipGlows engine across products; build voice effects into it and exclude third-party effect plugins."
  - "Primary-source review, 2026-09-30: official platform documentation, project repositories, READMEs, and license files."
  - "Three focused research tracks reviewed callback safety, platform architecture, and open-source engine examples."
next_review: "2026-12-30"
next_step: "Use this research to implement the first bounded Windows voice-recording/effects milestone in the ShipGlows-owned C++ engine."
---

# Shared audio engine research

## Purpose and scope

This report gathers implementation patterns and source-code examples for a shared, DAW-capable audio engine used by BeatGlows and future ShipGlows apps. The target is an engine we own and can evolve: built-in voice effects, playback, recording, monitoring, device handling, and deterministic offline processing. Third-party effect plugins and plugin hosting are out of scope by operator direction.

On 2026-10-01, the operator chose to build the engine in-house and declined a framework comparison. The research list below remains a source-study index only. No third-party DAW/audio engine or DSP/effect implementation will be integrated. The current Android Oboe bridge is an existing platform adapter; whether it remains the Android I/O layer will be reviewed when that phase starts.

The evidence is a source-level study, not a legal opinion, performance benchmark, or proof that a candidate meets BeatGlows' latency and sound-quality needs. License labels below are repository-level starting points; any reuse still needs exact revision, file-level notices, transitive dependency review, and distribution review.

## Finding

The selected direction is a ShipGlows-owned C++ processing core with a stable C ABI for Flutter, platform-specific I/O behind one engine interface, and DSP implemented and tested inside ShipGlows Audio Engine. Begin with Windows shared-mode WASAPI; carry the existing Android Oboe adapter forward for later physical-device proof unless implementation evidence changes that platform detail. Study other projects for design ideas without incorporating their engine or DSP code.

## How a DAW engine is commonly divided

1. **Product/session layer:** BeatGlows owns projects, arrangement, edits, undo, musical transport, and user workflow. Other apps can use the engine without inheriting DAW concepts.
2. **Engine control plane:** device discovery and lifecycle, capability reporting, commands, source/track routing, immutable graph preparation, error and recovery state. Control operations run away from the audio callback.
3. **Audio core:** sample-frame clock, float processing blocks, mixer, gain/pan, track and master buses, bounded acyclic routing, parameter smoothing, latency reporting, and built-in DSP.
4. **Platform I/O adapters:** Windows WASAPI first; Android Oboe/AAudio later. They negotiate actual device format, period, route, and lifecycle, then exchange PCM with the common core.
5. **Storage and media workers:** decoding, WAV writing, project persistence, waveform/level analysis, and export run outside the device callback. Recording data crosses through a preallocated bounded FIFO.
6. **Flutter boundary:** product UI and commands use a versioned C ABI through `dart:ffi`; no audio buffers or UI work cross into the real-time callback. Expensive control actions remain asynchronous.

The engine's clock should be frames, not Flutter timers. Device timestamps map stream frames to monotonic time; the DAW maps musical time to engine frames. Capture and render clocks may drift independently, so duplex monitoring needs measured synchronization and later bounded resampling or correction.

## Real-time rules for every backend

- Treat the device callback as a deadline-bound render function. Do bounded DSP and fixed-size copies only.
- Do not allocate/free, lock or wait, touch disk/network, decode, log, call Flutter, stop/start streams, or do unbounded work in the callback.
- Preallocate buffers and queues. Send commands through bounded nonblocking queues or atomic snapshots. Smooth gain and other continuous changes over audio frames to avoid clicks.
- Build and allocate graph changes off-thread. Publish prepared graph state at a block boundary; reclaim old state on a worker after the audio thread is finished. Never destroy processors in the callback.
- Keep device callback size, DSP quantum, UI update rate, and file-writer chunk size independent. Accept variable callback frame counts where possible and adapt through preallocated FIFOs.
- If recording FIFO capacity is exceeded, never block the callback or silently claim an uninterrupted take. Record a discontinuity/drop count and preserve frame/timestamp metadata.
- Measure callback time against its actual period, xrun/underrun/overrun counts, queue high-water/drop counts, negotiated format and period, device recovery, and round-trip latency. Software-reported latency is not a substitute for physical loopback.
- Stream callbacks may still race with close/stop. Callback-owned objects must stay alive until the platform confirms shutdown.

These constraints are documented by [Oboe's callback contract](https://github.com/google/oboe/blob/main/include/oboe/AudioStreamCallback.h), [PortAudio's callback contract and ring buffer](https://github.com/PortAudio/portaudio/blob/master/include/portaudio.h), and the [Oboe crash-avoidance notes](https://github.com/google/oboe/wiki/TechNote_HowToAvoidCrashes).

## Platform implications

### Windows

- Start with event-driven WASAPI shared mode and query the endpoint's real format and supported engine periods. On Windows 10+, `IAudioClient3` exposes shared-mode period ranges; do not bake in a universal sample rate or 128-frame callback.
- Shared mode keeps system audio available and is the sensible first monitoring target. Exclusive mode is a later opt-in only if a demonstrated workflow requires it.
- Keep device enumeration, stream open/close, endpoint invalidation, default-device changes, and recovery on control threads. Use MMCSS for time-sensitive I/O threads and test unplug/default-route changes.
- Defer ASIO unless the first user workflow proves a driver-specific requirement; it adds installed-driver and support complexity.

References: [Windows audio architecture](https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/windows-audio-architecture), [event-driven shared rendering](https://learn.microsoft.com/en-us/windows/win32/coreaudio/rendersharedeventdriven), [shared-mode engine periods](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclient3-getsharedmodeengineperiod), and [invalid-device recovery](https://learn.microsoft.com/en-us/windows/win32/coreaudio/recovering-from-an-invalid-device-error).

### Android

- Oboe is Google's C++ layer that chooses AAudio on newer Android and OpenSL ES on older API levels, with latency guidance, examples, and OboeTester. It is a platform I/O option, not a DAW or effect suite.
- Request low-latency callback operation, inspect the opened stream's actual rate/format/burst, and measure glitches and round-trip latency on named devices. Android device, route, headset, and OS variation prevent a universal latency promise.
- Prefer one engine output stream that mixes tracks internally; multiple simultaneous low-latency streams can compete for device resources.
- Full-duplex input/output is a separate validation risk: input and output can have different frame counts, startup times, latencies, and clocks. Permission, route, interruption, and reconnect belong in app/device acceptance.

References: [Android low-latency audio guide](https://developer.android.com/games/sdk/oboe/low-latency-audio), [Oboe repository and FXLab example](https://github.com/google/oboe), [Oboe full-duplex API](https://github.com/google/oboe/blob/main/include/oboe/FullDuplexStream.h), and [Android audio input sharing](https://developer.android.com/media/platform/sharing-audio-input).

### Flutter boundary

Flutter's [native-code integration guidance](https://docs.flutter.dev/platform-integration/bind-native-code) supports C/C++ libraries through a C ABI and `dart:ffi`; its current guidance favors `package_ffi`/build hooks in supported Flutter versions. Keep callback PCM native. Flutter sends control commands and receives snapshots/events asynchronously; UI meter updates use reduced telemetry rather than crossing the real-time boundary.

## Open-source code worth studying

| Project | Declared license / scope | What to study | Reuse posture for ShipGlows |
| --- | --- | --- | --- |
| [Ardour](https://github.com/Ardour/ardour) | GPL-2.0-or-later; full DAW | Mature sessions, routing, automation, track graph, transport and recovery | Architecture reference only for now; do not copy code into the private engine without a deliberate copyleft/business review. |
| [Audacity](https://github.com/audacity/audacity) | GPL family; editor/recorder; repository also contains third-party components | Non-destructive editing, real-time track/master effects, effect lifecycle and offline processing | Study workflows and source boundaries; per-file/dependency licensing makes selective reuse a separate audit. |
| [LMMS](https://github.com/LMMS/lmms) | GPL-2.0-or-later; music production and mixing | Mixer channels, effect chains, automation, project representation | Useful routing/composition reference; more MIDI/instrument-centered than voice recording; study-only under current private-engine direction. |
| [Tracktion Engine](https://github.com/Tracktion/tracktion_engine) | GPL-3.0-or-later or commercial; C++ DAW engine; Windows and Android listed | Engine/session APIs, rendering, tracks, device integration | Study architecture and lifecycle only; no engine adoption or comparison planned. |
| [JUCE](https://github.com/juce-framework/JUCE) | JUCE 9 has AGPLv3 or JUCE commercial terms | Device manager, audio graph, typed buffers, processor lifecycle, DSP modules | Historical source reference only. The custom engine direction is selected; JUCE is not planned for adoption or comparison. |
| [miniaudio](https://github.com/mackron/miniaudio) | Public domain or MIT-0; C playback/capture, WASAPI and AAudio | Compact device abstraction, callback, capture/playback, ring buffers, format conversion | Source study only; ShipGlows will own its engine and DSP implementation. |
| [Oboe](https://github.com/google/oboe) | Apache-2.0; Android-only I/O; includes FXLab sample | Android stream lifecycle, low-latency tuning, glitches, duplex and test tooling | Study the current adapter and examples. Oboe is platform I/O, not the engine; Android adapter continuation is reviewed in that implementation phase. |
| [Signalsmith Basics](https://github.com/Signalsmith-Audio/basics) | MIT; small audio building blocks and effects | Gate/compressor/limiter/reverb/chorus examples, block APIs and smoothing | Clean DSP reference. Effects should be specified and implemented as ShipGlows-owned code unless code reuse is explicitly chosen and tracked. |
| [Signalsmith DSP](https://github.com/Signalsmith-Audio/dsp) | MIT; C++11 header-only signal-processing blocks | Delay lines, envelopes, filters, tested signal characteristics | Useful mathematical and test reference; separate repo/license from Basics. No code adoption selected. |
| [SoLoud](https://github.com/jarikomppa/soloud) | zlib/libpng; game-oriented audio engine | Compact mixer, sources, buses and embedded effects | Source study only; game playback evidence does not demonstrate DAW session, editing, recording, or low-latency voice monitoring. |
| [PortAudio](https://github.com/PortAudio/portaudio) | MIT; cross-platform callback API | Callback rules, ring-buffer producer/consumer pattern, device abstraction | Source study only; ShipGlows will use its own engine and platform I/O. |
| [RtAudio](https://github.com/thestk/rtaudio) | Repository describes a custom license similar to MIT; device I/O abstraction | Smaller device callback abstraction and WASAPI path | Source study only; it is not a DSP or DAW layer and will not be integrated. |

Large DAWs teach routing and session architecture; small projects teach narrow mechanisms. Their GitHub activity and popularity are discovery signals, not performance or fitness proof. GPL/AGPL sources remain valuable to read, but copying their implementation into a private product changes the licensing analysis. Permissive labels also do not remove attribution, notices, or transitive-dependency obligations.

## Built-in effects: sensible first sequence

Start with a bounded voice chain whose latency and CPU can be measured: input trim, high-pass/EQ, gate/expander, compressor, output limiter, then optional reverb/delay on a send bus. Implement parameter smoothing, gain staging, bypass behavior, peak protection, and stable state serialization with the effects. Defer pitch correction, denoising/source separation, and other high-cost or quality-sensitive restoration until a specific user workflow and acceptance test exist.

Each processor needs offline deterministic tests across sample rates and varying block sizes, impulse/sine/silence inputs, automation transitions, finite-output checks, declared latency/tail behavior, and CPU budget. Hearing quality still requires human voice and monitoring acceptance; DSP unit tests cannot prove it.

## Implementation gates

1. **Windows device path:** real shared-mode WASAPI render and capture, device timestamps/frame accounting, endpoint change/unplug recovery, and negotiated format/period.
2. **Voice monitoring:** microphone through the first built-in chain to output; measure physical round-trip latency, callback timing, xruns, CPU, and audible quality on named hardware.
3. **Offline DSP:** deterministic render at variable block sizes; verify parameter smoothing, finite values, reported latency/tail, and bounded CPU.
4. **Recording integrity:** long take under CPU/disk stress; exact frame/file count, FIFO high-water and drops, explicit gap metadata, recoverable finalization.
5. **Flutter integration:** versioned C ABI and lifecycle/concurrency tests; no buffer marshalling through Flutter channels or callback-to-Dart calls.
6. **Android follow-up:** one output plus input using Oboe; permission, route/interruption/reconnect, actual round-trip latency and xrun measurements on named physical devices. Do not claim parity before this proof.

Implement the selected custom engine against the bounded Windows workflow. Measure latency/glitches/CPU, device recovery, effect quality, and consumer API stability as acceptance evidence. Do not treat a synthetic playback probe, library age, or star count as readiness proof.

## Current implementation gap and immediate relevance

The repository currently provides native capture/recovery foundations, including WASAPI and Oboe capture, a ring buffer, and recording storage. It does not yet provide a DAW playback renderer, track mixer, effects graph, or product session. The current Android Oboe capture callback does level/clipping analysis that is also repeated in the worker; measure and remove avoidable callback load before adding DSP. Existing capture and recovery contracts remain assets to preserve, not proof of DAW readiness.

The existing [JUCE architecture study](contracts/juce-decision-matrix.md) predates the clarified DAW scope. It records useful design inspiration and had deferred a processor graph. The operator has since selected a custom ShipGlows C++ engine; the [engine ownership decision](contracts/engine-ownership-decision.md) is now canonical. JUCE and other projects remain study references only, not candidates for a comparison or integration.

## Primary source index

- Platform/callback: [Oboe callback contract](https://github.com/google/oboe/blob/main/include/oboe/AudioStreamCallback.h), [Oboe low-latency guide](https://developer.android.com/games/sdk/oboe/low-latency-audio), [WASAPI architecture](https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/windows-audio-architecture), [Flutter native integration](https://docs.flutter.dev/platform-integration/bind-native-code).
- Open-source code and repository license declarations: [Ardour](https://github.com/Ardour/ardour), [Audacity](https://github.com/audacity/audacity), [LMMS](https://github.com/LMMS/lmms), [Tracktion Engine](https://github.com/Tracktion/tracktion_engine), [JUCE](https://github.com/juce-framework/JUCE), [miniaudio](https://github.com/mackron/miniaudio), [Oboe](https://github.com/google/oboe), [Signalsmith Basics](https://github.com/Signalsmith-Audio/basics), [Signalsmith DSP](https://github.com/Signalsmith-Audio/dsp), [SoLoud](https://github.com/jarikomppa/soloud), [PortAudio](https://github.com/PortAudio/portaudio), [RtAudio](https://github.com/thestk/rtaudio).
- Framework/graph and license context: [JUCE audio graph tutorial](https://juce.com/tutorials/tutorial_audio_processor_graph/), [JUCE DSP tutorial](https://juce.com/tutorials/tutorial_dsp_introduction/), [JUCE 9 license](https://juce.com/legal/juce-9-licence/), [Tracktion Engine repository license](https://github.com/Tracktion/tracktion_engine/blob/develop/LICENSE.md), [miniaudio license section](https://github.com/mackron/miniaudio#license), [Signalsmith Basics license](https://github.com/Signalsmith-Audio/basics/blob/main/LICENSE.txt), [Signalsmith DSP README/license](https://github.com/Signalsmith-Audio/dsp/blob/main/README.md).

## Maintenance rule

Refresh licenses and project status before any adoption decision. Update this report when a prototype produces measured evidence, when the first DAW workflow/effect chain is agreed, or when a dependency or platform target changes. Keep the report as source research; put durable selections in a decision record and behavior contracts in the engine spec.
