# Verification matrix

This document distinguishes compile/package evidence from actual capture
evidence. Both are required before a backend is promoted to production.

| Backend | Compile/package proof | Native capture proof | Remaining release gate |
| --- | --- | --- | --- |
| Portable C++ core | CMake + MSVC Debug | Unit tests cover state, SPSC transport, segmented persistence, and recovery | Long-duration stress and fault injection |
| Windows WASAPI | Flutter Windows Debug build | Default input captured for one second; complete manifest and non-empty segment verified | Multi-device, unplug, suspend, and long-duration matrix |
| Android Oboe | Debug APK contains JNI libraries for arm64-v8a, armeabi-v7a, and x86_64 | Not yet device-validated | Physical-device OEM matrix, permission, interruptions, route changes, unplug, and long-duration tests |

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
