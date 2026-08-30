# Verification matrix

This document distinguishes compile/package evidence from actual capture
evidence. Both are required before a backend is promoted to production.

| Backend | Compile/package proof | Native capture proof | Remaining release gate |
| --- | --- | --- | --- |
| Portable C++ core | CMake + MSVC Debug | Unit tests cover state, SPSC transport, segmented persistence, and recovery | Long-duration stress and fault injection |
| Windows WASAPI | Flutter Windows Debug build | Default input captured for one second; complete manifest and non-empty segment verified | Multi-device, unplug, suspend, and long-duration matrix |
| Android Oboe | Debug APK contains JNI libraries for arm64-v8a, armeabi-v7a, and x86_64 | Samsung Android 15 baseline captured and finalized five segments with zero reported loss | Broader OEM matrix, permission UX, interruptions, route changes, unplug, and long-duration tests |

### Samsung SM-G996U1 baseline — 2026-08-30

The Android 15/API 35 arm64 device completed a physical Oboe capture at 48 kHz,
stereo, 16-bit PCM. The engine stored 1,137,312 frames across five immutable
segments with `complete=true`, zero ring-overflow frames, zero discontinuities,
zero clipped samples, and no engine error. This proves basic physical capture
and multi-segment finalization; it does not satisfy the interruption or soak
release gates.

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

Android runtime permission must be granted by the host product before
`startRecording`. Permission handling deliberately remains outside the native
engine so each product controls its own UX.
