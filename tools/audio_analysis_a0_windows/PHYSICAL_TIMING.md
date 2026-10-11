# A0 physical audio-to-screen timing protocol

This protocol measures the delay between sound reaching the listening position
and the corresponding visible change on the named Windows endpoint. It also
measures the uncertainty of that delay. It is a calibration procedure, not an
acceptance result; the thresholds below remain provisional until A0 review.

## Practical exploratory check with available devices

A PC, headphones, microphone and phone are enough for a useful qualitative
check; no additional equipment purchase is required. Film the screen while the
headphone earcup is close to the phone microphone, with the phone recording
video and audio together. On the Samsung Galaxy S21, use ordinary **Video**
mode and choose 60 fps if the camera offers it. Play back the saved clip and
confirm that its audio track contains the headphone tone before relying on it.
Samsung's S21 product material lists Super Steady up to 60 fps and its S21
specification lists 240/960 fps slow-motion modes; the sources do not state
whether S21 Slow Motion or Super Slow-mo retains audio. Do not assume either
way. Record the phone's current model label, OS/software version, camera mode,
resolution, frame rate, stabilization setting, microphone permission, and any
changed camera settings. Keep the phone fixed and record display mode, camera
settings, microphone/earcup placement, and the clip filename. Preserve the
original audio/video file without trimming, transcoding, or social-app upload.

Samsung references: [Galaxy S21 specifications](https://www.samsung.com/uk/smartphones/galaxy-s21-5g/specs/)
and [Galaxy S21 product information](https://www.samsung.com/us/app/smartphones/galaxy-s21-5g).
Neither source documents the S21 camera's audio/video capture offset or
calibrates it.

This can reveal large or inconsistent delays and is useful for debugging. It
cannot support a <=10 ms uncertainty claim: frame rate quantizes visual timing,
and the phone's audio/video capture offset, exposure/rolling-shutter timing,
and processing delay are unknown unless independently calibrated. The phone
microphone also hears nearby room sound; it is not a calibrated
listening-position measurement. Do not treat this exploratory recording as
formal A0 timing evidence or claim <=10 ms uncertainty from it.

## Formal measurement apparatus

For a defensible physical lag and uncertainty measurement, use a simultaneous,
common-clock acquisition with two channels: a fast photodiode and amplifier
positioned over a known changing screen patch on channel 1, and an acoustic
measurement microphone with preamplifier at the defined listening position on
channel 2. Simultaneous 48 ksample/s audio channels provide adequate time
resolution for millisecond gates; sensor response, channel skew and calibration
usually dominate the uncertainty budget. Electrical line/headphone loopback
alone measures an electrical signal and cannot establish speaker/transducer or
acoustic latency. An optional third simultaneous channel for electrical audio
loopback can help separate device output delay from speaker/acoustic delay, but
does not replace the microphone.

Use a shared trigger or stimulus reference, or a clearly identifiable audio
waveform and screen transition whose source position and presentation frame
are logged by the diagnostic. Do not combine timestamps from independent
clocks without a measured correlation and uncertainty.

Record equipment models, channel/sample-rate configuration, sensor/amplifier
delay calibration, microphone placement and distance, display patch/location,
display refresh mode, endpoint/device identity, and Windows/build identity.

## Procedure

1. Identify the diagnostic stimulus and required event trace before collecting
   formal evidence. Keep ordinary A0 audio processing and the named endpoint
   path enabled. The current
   A0 harness now has an opt-in exploratory stimulus:
   `flutter run -d windows --profile --dart-define=A0_PHYSICAL_STIMULUS=true --dart-define=A0_MASK=11`
   from `beatglows/spikes/audio_analysis_a0`. Press Start ON manually. Its
   native fixture uses 48,000 Hz, 997 Hz, opposite-phase stereo; its source
   gate is on for frames where `source_frame % 48000 < 12000` and off otherwise.
   Nominal source-gate edges are at `48000*n` (on) and `48000*n+12000` (off).
   These are generated-source sample positions, conditional on the normal
   admitted stereo callback path; they are not measured physical output edges.
   The 997 Hz sine begins at a zero-valued sample, so the gate-on control edge
   is not itself an acoustic-onset timestamp.
   The canvas flashes when the latest measured combined physical RMS exceeds
   0.006. The analysis tap is after the always-on limiter, which has a fixed
   32-frame sample delay (0.667 ms) and outputs 32 initial zero frames. This is
   a processing offset, not device or acoustic latency. The prototype now has a
   source-level bounded paint trace in
   `beatglows/spikes/audio_analysis_a0/lib/physical_canvas_trace.dart`, wired
   into `main.dart`; four focused trace tests and the current 21-test/analyze
   checks pass. Its 512-event cap reports `overflow_count`. Each transition row
   links visible state to paint-invocation Stopwatch time, record sequence,
   sample interval, origin, continuity and RMS, plus scheduler timestamp, batch
   poll window, resync, submission-failure and PCM-reset metadata. The fixed top
   strip shows `R<run_id> T<transition_id>`; the nominal 64-by-64 logical-pixel
   patch is top-right below the 28-pixel strip and clips to the canvas. Calibrate
   its phone-video ROI. Preserve the original unedited clip and complete
   `A0_RESULT.physical_canvas_trace`; match its visible R/T values to the trace
   events and report any overflow.

   The physical-stimulus Profile branch has now been rebuilt and launched idle;
   the live VM reports running=false, trace_active=false and zero events. See
   `physical-trace-compile-idle-review.json` in the dated evidence directory.
   No Start ON run, audible recording or filmed timing result is claimed. Scheduler timestamp
   and Dart Stopwatch have different origins and must not be subtracted. Paint
   invocation is not raster completion or photon time. A row with
   `batch_contains_canvas_record` false, resync, submission failure, PCM reset,
   or trace overflow marks an incomplete interval; inspect the complete
   `A0_RESULT` metrics and do not infer omitted transitions.

   `Record.center_frame` is the FIRST sample of the physical RMS window
   `[center_frame, center_frame + valid_frames)`, not the window midpoint or
   flash time. `valid_frames` is at most 480 (10 ms). The 32-frame limiter
   offset maps analyzed post-DSP samples back to generated-source samples, but
   an RMS window that overlaps an on/off edge only bounds the edge to that
   window; an RMS threshold does not identify an exact waveform onset. Mask 11
   also waits for 1,024 frames of spectrum lookahead before publishing a record,
   and Flutter polling/painting adds further delay. The source/submitted frame
   counters align on the admitted stereo path. GetBuffer failure advances
   neither; ReleaseBuffer failure advances the attempted-frame counters but
   skips analysis admission and leaves physical presentation unknown. Queue
   drops/resyncs can omit records; preserve these gaps rather than infer events.
   See `shipglows_data/workflow/evidence/audio-analysis-a0/2026-10-07/physical-stimulus-source-mapping.json`
   for the source-inspection-only mapping audit.
2. Warm up and calibrate the acquisition channels. Measure or obtain traceable
   sensor, amplifier, microphone and recorder channel delays; preserve their
   correction values and uncertainty. Verify channel skew by applying the same
   electrical edge to both inputs. A photodiode measures emitted light at its
   patch; the microphone measures sound at its position, so state those
   positions as the measured endpoints.
3. Rebuild and run the physical-stimulus Windows Profile diagnostic on the
   named endpoint after the trace change; press Start ON manually. Film the
   patch and R/T strip with phone audio enabled, placing the headphone earcup
   near the phone microphone. Save the original clip and complete `A0_RESULT`,
   including `physical_canvas_trace`, then match visible R/T IDs to traced paint
   transitions. The trace provides software record/paint linkage, not physical
   audio/light timestamps. Preserve device-clock/QPC observations, queue and
   release failures, gaps, resyncs, skipped records and dropped trace events.
   Keep audio and screen output on their actual devices.
4. Collect repeated valid isolated transitions over separately started runs,
   including warm and steady operation. Choose enough repeats to make the
   distribution and run-to-run spread interpretable; this proposed protocol
   does not set a normative sample-count or run-count requirement. Keep every
   sample, failed detection, dropped event and over-100ms no-forward interval;
   do not discard timing outliers. Re-run at the same configuration if the
   recording is clipped, saturated, ambiguous or incomplete.
5. For each event, derive the microphone acoustic onset and photodiode light
   transition using a declared, fixed detection rule. Apply calibrated channel
   delay corrections, then compute screen-minus-acoustic lag. Separately
   correlate each event to the known submitted source position and native clock
   observations. Do not substitute the submitted-frame paint proxy for either
   physical edge.

## Result record and decision

Save raw captures and a machine-readable summary keyed by engine/BeatGlows
revision and dirty-source hashes, diagnostic binary hash, OS/toolchain, endpoint,
device period, display mode, stimulus revision, acquisition device/settings,
sensor calibration, and run IDs. Include per-event corrected audio and light
timestamps, lag, detection quality, source position, native clock/RTT/age data,
and all excluded or failed detections with reasons. Report count, median, p95,
maximum, minimum, run-to-run spread, clock correlation/rate/residual, and a
stated uncertainty budget with channel skew, sensor/amplifier delay, onset
detection, sampling interval, placement and repeatability contributions.

Compare the measured presentation result with the provisional A0 candidates:
candidate delay 60 ms, observed lag no greater than 100 ms, uncertainty U no
greater than 10 ms, and normal RTT no greater than 12 ms. These are review
targets, not approved production guarantees. AC-A7 also requires audio-plus-tap
p99 below the measured endpoint period, zero additional OFF/ON underruns in the
10-minute comparison, memory within reviewed caps, and measured optional-FFT CPU
comparison. Preserve all failures and maxima. A measured physical lag alone
does not validate a presentation anchor or clock uncertainty; the clock/source
mapping needs its own defensible correlation and uncertainty bound.

## What this measurement cannot establish

This setup does not prove other endpoints, speakers, listening positions,
display regions, refresh modes or machines. An electrical loopback alone does
not prove audible latency. A camera recording, separate-clock captures,
submitted-frame timestamps, RTT, WPR allocation traces or nominal device/QPC
rate alone do not prove calibrated acoustic-to-photon lag or absolute timing
uncertainty. This procedure does not validate aggregate Dart/native allocation
caps, production protocol readiness, or A0 by itself; protocol/default freeze
still requires review of all applicable A0 evidence.
