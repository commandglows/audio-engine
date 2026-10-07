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

### Windows source selection

`getInputDevices()` and `getOutputDevices()` expose persistent Windows endpoint IDs
as `endpointId`. For an explicitly selected take, pass `inputEndpointId` together
with `microphoneEnabled` and optional `outputEndpointId` to `startRecording`.
A null output disables system audio; an enabled microphone requires its exact
input endpoint ID. Both sources disabled is rejected before capture starts.

Explicit takes capture WASAPI microphone and/or render-loopback streams in shared
48 kHz stereo float format. Packets are aligned by QPC timestamp; a fixed 0.5 gain
per source gives headroom when combining two sources. Silent render periods retain
elapsed silence. Selections stay fixed through pauses. Missing/disconnected devices
and suspend end the take with an error while the segmented store preserves captured
samples; the engine never substitutes another endpoint. Legacy index-only calls
continue through the existing capture implementation.

Native source checks: configure the root CMake project with MSVC, build
`shipglows_audio_sources_tests`, and run it. Passing `--live` additionally checks
current Windows output-only and microphone-plus-output capture and pause/resume.

### Windows system audio independent of output endpoints

`getOutputDevices()` prepends `System audio` with stable endpoint ID
`shipglows:system-audio` on Windows builds 20348 and later. Pass that ID as
`outputEndpointId` to capture rendering processes except this application's
process tree. It uses process-loopback activation, independently of physical
endpoints. System-only capture bypasses IMMDeviceEnumerator. Existing endpoint
IDs still capture only audio sent to that exact output. Neither method silently
substitutes another source on error. The OS check uses `RtlGetVersion`.

Nullable status fields `outputActiveMilliseconds` (monitored recording time) and
`outputSilentMilliseconds` (consecutive silence) use the output timeline before
microphone mixing. Pause is excluded. Either output channel above -60 dBFS resets
silence; absent packets count as silence. Null means monitoring unavailable or
output disabled. Consumers can warn without interrupting the take.

Microsoft promises silence when target processes have no rendering streams; it
does not promise that every application keeps rendering without an active output.
See the [official sample](https://learn.microsoft.com/en-us/samples/microsoft/windows-classic-samples/applicationloopbackaudio-sample/).
A successful activation or elapsed WAV frame count alone is not positive signal
proof. Physical unplug/no-device behavior remains a hardware matrix check.
