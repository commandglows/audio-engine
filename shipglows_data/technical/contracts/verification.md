---
artifact: verification_record
metadata_schema_version: "1.0"
artifact_version: "1.3.0"
project: ShipGlows Audio Engine
created: "2026-08-30"
updated: "2026-08-31"
status: reviewed
source_skill: sg-docs
scope: release-candidate-verification
owner: ShipGlows Audio Engine maintainers
confidence: high
risk_level: high
security_impact: none
docs_impact: yes
linked_systems:
  - ../code-docs-map.md
  - ../../../../tests/
depends_on: []
supersedes: []
evidence:
  - "Final validation matrix and two-hour Windows soak recorded on 2026-08-31."
  - "Samsung/Bose physical interruption matrix completed on commit b5a2038d089f02fa5dab01af39d9744d5befc524."
  - "Windows/Senary physical unplug, default-route switch, and suspend/resume matrix completed on the local working tree based on b5a2038d089f02fa5dab01af39d9744d5befc524."
next_review: "2026-09-30"
next_step: "Review and deliver the validated local WASAPI corrections and documentary evidence without absorbing unrelated changes."
---

# Verification Matrix

This document distinguishes compile/package evidence from actual capture
evidence. Both are required before a backend is promoted to production.

| Backend | Compile/package proof | Native capture proof | Remaining release gate |
| --- | --- | --- | --- |
| Portable C++ core | CMake + MSVC Debug | Unit tests plus the interruption bench cover lifecycle, bounded backoff, timestamps, persistence, recovery, low-space preflight, and injected write failure | Physical interruption matrix |
| Windows WASAPI | Flutter Windows Debug build | Default input capture, five pause/resume cycles, hardware timestamps, complete journal, playable segments, two-hour soak, physical unplug, default-route switch, and suspend/resume verified | Broader Windows hardware coverage; no remaining blocker for the Windows release candidate |
| Android Oboe | Debug APK contains JNI libraries for arm64-v8a, armeabi-v7a, and x86_64 | Samsung Android 15 baseline, API 36 emulator capture, and the Samsung/Bose physical interruption matrix pass | Broader OEM coverage; no remaining blocker for the Android release candidate |

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

### Samsung/Bose physical interruption matrix — 2026-08-31

Android release-candidate verdict: **yes** for the validated Samsung
`SM-G996U1` / Android 15 / Bose QC35 II Bluetooth SCO scope at commit
`b5a2038d089f02fa5dab01af39d9744d5befc524`.

- Disconnect/reconnect session `1788184835698458` recovered twice through
  `device_restart`, retained Bluetooth SCO, recorded 5,040,960 frames across 22
  non-silent WAV segments, and completed without drops, xruns, overflow,
  timestamp gaps, writer stalls, or internal-microphone fallback.
- Stop-during-reconnect session `1788186891168657` recorded non-silent audio,
  then a causal `device_disconnected` interruption followed by bounded
  `session_complete`, without reconnect exhaustion.
- Reconnect-exhausted session `1788187088099304` recorded non-silent audio
  before disconnection, remained recoverable through the 60-second window, and
  terminated as `device_reconnect_exhausted` with `start_new_session` only after
  the selected Bose endpoint remained unavailable past the deadline.

These results clear the remaining Android physical gates. They do not clear the
separate Windows physical gates by themselves. Those gates are recorded below.

### Windows/Senary physical interruption matrix — 2026-08-31

Windows release-candidate verdict: **yes** for the physically validated local
working tree based on commit
`b5a2038d089f02fa5dab01af39d9744d5befc524`. The WASAPI corrections remain
uncommitted and unpushed, so this evidence does not claim that `origin/main`
contains them.

| Gate | Session | Result | Causal and integrity evidence |
| --- | --- | --- | --- |
| Active microphone unplug | `1788191842854892` | passed | Selected Senary endpoint `{3a9a9b34-8ff4-4419-b1ab-30e61bab6df6}` recorded non-silent audio, then `interruption(device_invalidated)` at frame 3,727,354 with no later frames and no fallback to `{168e3222-f3ab-4799-812e-3d0ce99749a7}`. Terminal state was `failed`, error `device_reconnect_exhausted`, recovery `start_new_session`; drops/restarts/xruns/overflow/timestamp gaps/writer stalls/route changes were all 0 and discontinuities were 1. All 16 WAV files were readable and matched the journal; journal SHA-256 `32B293F285F98F50CA8D3DE56F74D459299636B996EFAFFEBC1D1B8DCB89EA35`. |
| Windows default-input switch | `1788193462771621` | passed | Two explicit default changes produced `route_change -> device_restart` twice, then non-silent audio after return to the physical Senary endpoint and `session_complete` at 18,868,889 frames. Drops/xruns/overflow/timestamp gaps/writer stalls were 0; discontinuities were 3, device restarts 2, and route changes 2. All 79 WAV files were readable and matched the journal; journal SHA-256 `F44522C2D4D742266C4D138C2600213825347D1F5FB2CF3BD9BDFB5EC678F314`. |
| Windows suspend/resume | `1788204357783352` | passed | `interruption(system_suspended) -> device_restart(system_resume)` occurred exactly once at frame 3,195,362. Audio was non-silent before and after wake; the session completed at 173,597,733 frames with one device restart, 3 discontinuities, and zero drops/xruns/overflow/timestamp gaps/writer stalls/route changes. All 725 WAV files were readable and matched the journal; journal SHA-256 `D128E93F7D4191A863F5BE23C91AEE3D39D4AACF822C1523019871EF3E0F9DC9`. |

The gate archives are stored under
`%LOCALAPPDATA%\ShipGlows\AudioValidation\2026-08-31-Windows-Senary\physical-validation-wasapi`
in the session-named `gate-01-active-mic-unplug-green`,
`gate-02-default-input-switch-green`, and
`gate-03-suspend-resume-green` directories. Each archive retains the journal,
WAV segments, and terminal-status screenshot.

The unplug gate first exposed an implicit fallback to the other Senary
endpoint, and the first suspend retest exposed duplicate Windows resume
broadcasts. The final implementation retains the selected endpoint during
loss, follows a default-device change only while the prior endpoint remains
active, creates a fresh native capture for a new terminal session, records
Windows power transitions, and deduplicates paired resume broadcasts.

Global release-candidate verdict: **yes** for the combined Android and Windows
physical scopes recorded here, subject to delivery of the currently local
WASAPI implementation and documentation changes.

## Automated interruption bench

`shipglows_audio_interruption_bench` deterministically exercises microphone
disconnect/reconnect, default-route change, platform audio loss, repeated
pause/resume, application suspension, low-space preflight, injected write
failure, and stop during reconnection. It asserts causal journal records,
preservation of committed segments, the cancellable 60-second Android recovery
deadline, and timestamp continuity across ordinary segment rotation.

The bench proves engine policy without pretending to unplug physical hardware.
Physical route, unplug, and suspend checks remain explicitly separate gates.

## Two-hour soak

Set `SHIPGLOWS_AUDIO_SOAK_SECONDS=7200` and run the Windows integration test.
It samples frame progress, dropped frames, timestamp gaps, native xruns, and
process RSS every 30 seconds, retains the session directory, and prints a final
machine-readable result. After completion, verify every journal digest and WAV
header before deleting the retained session.

### Windows two-hour result — 2026-08-31

The real WASAPI input completed 120 minutes and 2 seconds at 48 kHz with
345,658,995 captured frames, 708,902 hardware timestamp observations, zero
dropped frames, zero timestamp-gap frames, and zero native xruns. The session
finalized 1,446 continuous WAV segments (2,765,335,584 bytes); every recorded
size, RIFF/WAVE identity, and SHA-256 digest matched the append-only journal.

Process RSS rose from 216 MB at the first sample to a maximum of 280 MB in
stepwise plateaus. It did not accelerate near the end, but the 64 MB increase
must be compared in future soaks before calling memory behavior invariant. A
separate two-minute profile of the identical Windows capture path measured
10.13% of one CPU core on average and 13.08% at p95/max; CPU was not sampled
continuously during the two-hour run, so that distinction remains explicit.

At the time of the soak, the available machine had not yet exercised physical
microphone unplug, Windows default-input switch, or system suspend. The later
Windows/Senary matrix above completes those independent gates. The soak remains
the long-duration baseline and is not retroactively treated as interruption
proof.

Android runtime permission must be granted by the host product before
`startRecording`. Permission handling deliberately remains outside the native
engine so each product controls its own UX.
