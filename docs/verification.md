# Verification matrix

This document distinguishes compile/package evidence from actual capture
evidence. Both are required before a backend is promoted to production.

| Backend | Compile/package proof | Native capture proof | Remaining release gate |
| --- | --- | --- | --- |
| Portable C++ core | CMake + MSVC Debug | Unit tests plus the interruption bench cover lifecycle, bounded backoff, timestamps, persistence, recovery, low-space preflight, and injected write failure | Two-hour physical soak |
| Windows WASAPI | Flutter Windows Debug build | Default input capture, five pause/resume cycles, hardware timestamps, complete journal, and playable segment verified | Physical unplug, default-route switch, suspend, and two-hour soak |
| Android Oboe | Debug APK contains JNI libraries for arm64-v8a, armeabi-v7a, and x86_64 | Samsung Android 15 baseline plus API 36 emulator capture, pause/resume, hardware timestamps, journal, and WAV verification | Broader OEM matrix and physical interruption/route/unplug testing |

### Samsung SM-G996U1 baseline — 2026-08-30

The Android 15/API 35 arm64 device completed a physical Oboe capture at 48 kHz,
stereo, 16-bit PCM. The engine stored 1,137,312 frames across five immutable
segments with `complete=true`, zero ring-overflow frames, zero discontinuities,
zero clipped samples, and no engine error. This proves basic physical capture
and multi-segment finalization; it does not satisfy the interruption or soak
release gates.

The same device subsequently validated session format v2 through the real Oboe
backend: 573,024 frames across three independently playable RIFF/WAVE segments,
an append-only journal with continuous frame ranges and three SHA-256 digests,
and a final `session_complete` record. The UI reported zero dropped frames,
zero discontinuities, zero clipped samples, and no engine error.

## Repeatable checks

```powershell
cmake -S . -B build/native -DSHIPGLOWS_AUDIO_BUILD_TESTS=ON
cmake --build build/native --config Debug
ctest --test-dir build/native -C Debug --output-on-failure

Push-Location flutter/shipglows_audio
flutter analyze
flutter test
Pop-Location

Push-Location flutter/shipglows_audio/example
flutter build windows --debug
flutter build apk --debug
Pop-Location
```

## Automated interruption bench

`shipglows_audio_interruption_bench` deterministically exercises microphone
disconnect/reconnect, default-route change, platform audio loss, repeated
pause/resume, application suspension, low-space preflight, injected write
failure, and stop during reconnection. It asserts causal journal records,
preservation of committed segments, five-attempt backoff (100, 200, 400, 800,
and 1600 ms), and timestamp continuity across ordinary segment rotation.

The bench proves engine policy without pretending to unplug physical hardware.
Physical route, unplug, and suspend checks remain explicitly separate gates.

## Two-hour soak

Set `SHIPGLOWS_AUDIO_SOAK_SECONDS=7200` and run the Windows integration test.
It samples frame progress, dropped frames, timestamp gaps, native xruns, and
process RSS every 30 seconds, retains the session directory, and prints a final
machine-readable result. After completion, verify every journal digest and WAV
header before deleting the retained session.

Android runtime permission must be granted by the host product before
`startRecording`. Permission handling deliberately remains outside the native
engine so each product controls its own UX.
