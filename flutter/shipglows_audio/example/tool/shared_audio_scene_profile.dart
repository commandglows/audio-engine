import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'dart:math' as math;
import 'dart:typed_data';
import 'dart:ui' show FrameTiming;

import 'package:flutter/material.dart';
import 'package:shipglows_audio/shipglows_audio.dart';
import 'package:shipglows_visualization/shipglows_visualization.dart';

void main() => runApp(const _Harness());

final class _Harness extends StatefulWidget {
  const _Harness();
  @override
  State<_Harness> createState() => _HarnessState();
}

final class _HarnessState extends State<_Harness> {
  final _engine = ShipglowsAudio();
  final _timings = <FrameTiming>[];
  AnalysisController? _controller;
  Timer? _poller;
  String _label = 'Starting Windows Profile audio comparison';

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addTimingsCallback(_timings.addAll);
    WidgetsBinding.instance.addPostFrameCallback((_) => unawaited(_run()));
  }

  Future<void> _run() async {
    final temp = await Directory.systemTemp.createTemp('sg-shared-profile-');
    final wav = File('${temp.path}${Platform.pathSeparator}tone.wav');
    await wav.writeAsBytes(_tone(30));
    final outputDevices = await _engine.getOutputDevices();
    final defaultEndpoint = outputDevices.isEmpty
        ? null
        : outputDevices.first.endpointId;
    final phases = <Map<String, Object?>>[];
    Map<String, Object?>? music16Exercise;
    Map<String, Object?>? speechContextExercise;
    final sampleSeconds =
        int.tryParse(
          Platform.environment['SHIPGLOWS_AUDIO_PROFILE_SECONDS'] ?? '',
        ) ??
        8;
    var generation = 0;
    try {
      final phasesToRun =
          Platform.environment['SHIPGLOWS_MUSIC16_ONLY'] == 'true'
          ? <bool>[true]
          : Platform.environment['SHIPGLOWS_SPEECH_CONTEXT_ONLY'] == 'true'
          ? <bool>[]
          : <bool>[false, true];
      for (final enabled in phasesToRun) {
        generation++;
        await _engine.loadPlayback(
          generation: generation,
          localFilePath: wav.path,
        );
        if (enabled) {
          final source = _TimedSource(MethodChannelAnalysisSource());
          final transform = Map<String, Object?>.unmodifiable(
            defaultVisualTransformConfig,
          );
          final controller = AnalysisController(
            source: source,
            generation: generation,
            transformConfig: transform,
            sceneConfig: SceneConfig(
              mode: SceneMode.spectrumBars,
              profile: AnalysisProfile.music,
              featureMask: 11,
              transformDigest: visualTransformDigest(transform),
              intensity: .65,
              motion: SceneMotion.full,
            ),
          );
          if (mounted) setState(() => _controller = controller);
          await controller.start();
          _poller = Timer.periodic(const Duration(milliseconds: 33), (_) {
            unawaited(controller.poll());
          });
        }
        await _engine.playPlayback(generation: generation);
        await Future<void>.delayed(const Duration(seconds: 2));
        _timings.clear();
        final rssStart = ProcessInfo.currentRss;
        final start = DateTime.now();
        final lagValues = <double>[];
        final lagErrors = <String>[];
        var lagPollInFlight = false;
        Timer? lagTimer;
        if (enabled) {
          lagTimer = Timer.periodic(const Duration(milliseconds: 250), (_) {
            if (lagPollInFlight) return;
            lagPollInFlight = true;
            unawaited(() async {
              try {
                final sampled = await _engine.getPlaybackStatus(
                  generation: generation,
                );
                final source = _timedSourceOf(_controller);
                final center = source?.latestSourceCenterFrame;
                final origin = source?.runOriginFrame;
                if (sampled.sampleRate > 0 &&
                    center != null &&
                    origin != null) {
                  final playedFrame =
                      origin +
                      BigInt.from(
                        (sampled.positionSeconds * sampled.sampleRate).round(),
                      );
                  lagValues.add(
                    (playedFrame - center).toDouble() *
                        1000 /
                        sampled.sampleRate,
                  );
                }
              } on Object catch (error) {
                lagErrors.add(error.toString());
              } finally {
                lagPollInFlight = false;
              }
            }());
          });
        }
        final sampler = await Process.start('powershell.exe', [
          '-NoProfile',
          '-ExecutionPolicy',
          'Bypass',
          '-File',
          '${Directory.current.path}${Platform.pathSeparator}tool${Platform.pathSeparator}shared_audio_process_sample.ps1',
          '-TargetProcessId',
          '$pid',
          '-DurationSeconds',
          '$sampleSeconds',
        ]);
        // PowerShell may inherit a legacy Windows pipe code page; the sampler
        // emits ASCII JSON, so Latin-1 keeps its bytes reversible.
        final samplerOutput = await sampler.stdout
            .transform(latin1.decoder)
            .join();
        final samplerError = await sampler.stderr
            .transform(latin1.decoder)
            .join();
        final samplerExit = await sampler.exitCode;
        lagTimer?.cancel();
        while (lagPollInFlight) {
          await Future<void>.delayed(const Duration(milliseconds: 1));
        }
        if (samplerExit != 0) {
          throw StateError('Windows process sampler failed: $samplerError');
        }
        final cpuStats =
            jsonDecode(samplerOutput.trim()) as Map<String, dynamic>;
        final elapsed = DateTime.now().difference(start).inMilliseconds;
        final status = await _engine.getPlaybackStatus(generation: generation);
        final controller = _controller;
        final instrument = _timedSourceOf(controller);
        final rtts = enabled
            ? (instrument?.rtts ?? const <int>[])
            : const <int>[];
        phases.add({
          'visualizer': enabled ? 'ON' : 'OFF',
          'duration_ms': elapsed,
          'playback_state': status.state,
          'playback_error_code': status.errorCode,
          'sample_rate': status.sampleRate,
          'buffer_frames': status.bufferFrames,
          'endpoint_period_100ns': status.endpointPeriod100ns,
          'callback_p99_us': status.p99CallbackMicroseconds,
          'underruns': status.underruns,
          'method_channel_rtt_ms': _percentiles(rtts),
          'submitted_frame_lag_proxy_ms': enabled
              ? _percentilesDouble(lagValues)
              : null,
          'lag_sample_errors': lagErrors,
          'accepted_sequence': controller?.nextSequenceCursor.toString() ?? '0',
          'scene_status': controller?.state.value.status.name ?? 'off',
          'retained_record_count': controller?.retainedRecordCount ?? 0,
          'scene_label': controller?.state.value.label ?? 'Visualization off',
          'batch_received_record_count': instrument?.receivedRecordCount ?? 0,
          'delivery_resync_count': instrument?.deliveryResyncCount ?? 0,
          'skipped_delivery_record_count':
              instrument?.skippedDeliveryRecordCount ?? 0,
          'pcm_gap_count': instrument?.pcmGapCount ?? 0,
          'source_errors': instrument?.errors ?? const <String>[],
          'frame_timing_ms': _frameStats(_timings),
          'process_rss_start_bytes': rssStart,
          'process_rss_end_bytes': ProcessInfo.currentRss,
          'process_rss_peak_bytes': ProcessInfo.maxRss,
          'process_cpu_one_core_percent': {
            'average': cpuStats['average_one_core_percent'],
            'peak': cpuStats['peak_one_core_percent'],
            'samples': cpuStats['samples'],
          },
          'process_working_set_peak_bytes': cpuStats['working_set_peak_bytes'],
          'process_private_bytes_peak': cpuStats['private_bytes_peak'],
        });
        if (enabled && controller != null && instrument != null) {
          _poller?.cancel();
          _poller = null;
          await controller.poll(); // drain any tick already in flight
          final priorScene = controller.state.value;
          instrument.resetBatchMaximum();
          final resyncsBefore = instrument.deliveryResyncCount;
          final skipsBefore = instrument.skippedDeliveryRecordCount;
          final batchDeadline = DateTime.now().add(const Duration(seconds: 4));
          while (DateTime.now().isBefore(batchDeadline) &&
              instrument.maxBatchRecordCount < 16) {
            await Future<void>.delayed(
              Duration(milliseconds: instrument.batchPollDelayMilliseconds),
            );
            await controller.poll();
            if (instrument.lastBatchRecordCount <= 8) {
              instrument.batchPollDelayMilliseconds =
                  (instrument.batchPollDelayMilliseconds - 10).clamp(80, 180);
            } else if (instrument.lastBatchRecordCount >= 15) {
              instrument.batchPollDelayMilliseconds =
                  (instrument.batchPollDelayMilliseconds + 1).clamp(80, 180);
            } else if (instrument.lastBatchRecordCount >= 12) {
              instrument.batchPollDelayMilliseconds =
                  (instrument.batchPollDelayMilliseconds + 2).clamp(80, 180);
            } else {
              instrument.batchPollDelayMilliseconds =
                  (instrument.batchPollDelayMilliseconds + 4).clamp(80, 180);
            }
          }
          // Read the old scene after publication so it remains a live root
          // across receipt, decoding, installation and notifier replacement.
          music16Exercise = {
            'profile': 'music',
            'feature_mask': 11,
            'incoming_batch_record_count': instrument.maxBatchRecordCount,
            'last_batch_record_count': instrument.lastBatchRecordCount,
            'poll_delay_ms': instrument.batchPollDelayMilliseconds,
            'incoming_payload_bytes': instrument.maxBatchPayloadBytes,
            'incoming_payload_offset_in_bytes':
                instrument.maxBatchPayloadOffset,
            'controller_cursor_after_install': controller.nextSequenceCursor
                .toString(),
            'delivery_resyncs_during_exercise':
                instrument.deliveryResyncCount - resyncsBefore,
            'skipped_delivery_records_during_exercise':
                instrument.skippedDeliveryRecordCount - skipsBefore,
            'old_scene_retained_during_poll': priorScene.label.isNotEmpty,
            'old_scene_label_after_publish': priorScene.label,
            'new_scene_published': !identical(
              priorScene,
              controller.state.value,
            ),
            'published_scene_status': controller.state.value.status.name,
            'retained_record_count': controller.retainedRecordCount,
            'deadline_reached': instrument.maxBatchRecordCount < 16,
            'interpretation': 'Actual 16-record-channel lot installed and published while prior SceneState is held; no allocator accounting.',
          };
        }
        _poller?.cancel();
        _poller = null;
        if (_controller != null) {
          await _controller!.dispose();
          _controller = null;
        }
        await _engine.stopPlayback(generation: generation + 1);
        generation++;
        if (mounted) {
          setState(() => _label = '${enabled ? 'ON' : 'OFF'} phase complete');
        }
      }
      if (Platform.environment['SHIPGLOWS_MUSIC16_ONLY'] != 'true') {
        // Exercise the full speech history (166 compact hops) and then keep
        // accepting 16-record lots while that maximum old context is retained.
        generation++;
        await _engine.loadPlayback(
          generation: generation,
          localFilePath: wav.path,
        );
        final speechTransform = Map<String, Object?>.unmodifiable(
          defaultVisualTransformConfig,
        );
        final speechSource = _TimedSource(MethodChannelAnalysisSource());
        final speechController = AnalysisController(
          source: speechSource,
          generation: generation,
          transformConfig: speechTransform,
          sceneConfig: SceneConfig(
            mode: SceneMode.speechEnvelope,
            profile: AnalysisProfile.speech,
            featureMask: 5,
            transformDigest: visualTransformDigest(speechTransform),
            intensity: .65,
            motion: SceneMotion.full,
          ),
        );
        if (mounted) setState(() => _controller = speechController);
        await speechController.start();
        await _engine.playPlayback(generation: generation);
        final speechDeadline = DateTime.now().add(const Duration(seconds: 8));
        var fullContextBatchSequence = BigInt.zero;
        while (DateTime.now().isBefore(speechDeadline) &&
            (speechController.retainedRecordCount < 166 ||
                speechController.nextSequenceCursor < BigInt.from(182))) {
          await speechController.poll();
          if (speechController.retainedRecordCount == 166 &&
              speechController.nextSequenceCursor > BigInt.from(166)) {
            fullContextBatchSequence = speechController.nextSequenceCursor;
          }
          await Future<void>.delayed(const Duration(milliseconds: 25));
        }
        speechContextExercise = {
          'profile': 'speech',
          'feature_mask': 5,
          'retained_history_capacity': 166,
          'retained_record_count': speechController.retainedRecordCount,
          'accepted_sequence': speechController.nextSequenceCursor.toString(),
          'newer_sequence_while_full_context_retained': fullContextBatchSequence
              .toString(),
          'scene_status': speechController.state.value.status.name,
          'scene_label': speechController.state.value.label,
          'source_errors': speechSource.errors,
          'batch_summaries': speechSource.batchSummaries,
          'deadline_reached':
              !DateTime.now().isBefore(speechDeadline) &&
              (speechController.retainedRecordCount < 166 ||
                  speechController.nextSequenceCursor < BigInt.from(182)),
          'interpretation': 'Full speech history exercised with subsequent real channel lots; not an allocation-overlap measurement.',
        };
        await speechController.dispose();
        _controller = null;
        await _engine.stopPlayback(generation: generation + 1);
        generation++;
      }
      final report = {
        'schema': 'shared-audio-scene-profile-1',
        'runtime': 'Flutter Windows Profile',
        'pid': pid,
        'default_endpoint_available': defaultEndpoint?.isNotEmpty == true,
        'endpoint_identity_logged': false,
        'fixture':
            'generated 48 kHz stereo 440 Hz WAV; default multimedia output',
        'profile': 'music; feature mask 11; same endpoint and transform',
        'phase_sample_seconds': sampleSeconds,
        'phases': phases,
        'music11x16_context_exercise': music16Exercise,
        'speech_context_exercise': speechContextExercise,
        'limits': [
          'OFF means playback with no AnalysisController or SharedSceneView; ON adds both.',
          'Process RSS is whole-process sampling, not allocation-overlap evidence.',
          'FrameTiming and MethodChannel duration are software measurements, not physical audio-to-screen timing.',
          'CPU and memory samples cover the entire Flutter process and include runtime and sampler overhead.',
          'This harness does not prove AC-A4 retained/incoming peak allocation.',
        ],
      };
      final json = jsonEncode(report);
      stdout.writeln('SHARED_AUDIO_SCENE_PROFILE_JSON:$json');
      await stdout.flush();
      final path = Platform.environment['SHIPGLOWS_AUDIO_PROFILE_OUTPUT'];
      if (path != null && path.isNotEmpty) {
        await File(path).writeAsString('$json\n', flush: true);
      }
    } finally {
      _poller?.cancel();
      await _controller?.dispose();
      try {
        await _engine.stopPlayback(generation: generation + 1);
      } on Object {
        // Playback may already have completed or been stopped between phases.
      }
      await temp.delete(recursive: true);
      WidgetsBinding.instance.removeTimingsCallback(_timings.addAll);
    }
  }

  _TimedSource? _timedSourceOf(AnalysisController? controller) =>
      controller?.source is _TimedSource
      ? controller!.source as _TimedSource
      : null;

  @override
  Widget build(BuildContext context) => MaterialApp(
    home: Scaffold(
      body: Center(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Text(_label),
            if (_controller case final controller?)
              SizedBox(
                width: 900,
                height: 500,
                child: SharedSceneView(
                  state: controller.state,
                  palette: _palette,
                ),
              ),
          ],
        ),
      ),
    ),
  );
}

final class _TimedSource implements AnalysisSource {
  _TimedSource(this.inner);
  final AnalysisSource inner;
  final rtts = <int>[];
  final errors = <String>[];
  final batchSummaries = <Map<String, Object?>>[];
  int receivedRecordCount = 0;
  int deliveryResyncCount = 0;
  int skippedDeliveryRecordCount = 0;
  int pcmGapCount = 0;
  int maxBatchRecordCount = 0;
  int maxBatchPayloadBytes = 0;
  int maxBatchPayloadOffset = 0;
  int lastBatchRecordCount = 0;
  int batchPollDelayMilliseconds = 145;
  BigInt? latestSourceCenterFrame;
  BigInt? runOriginFrame;

  void resetBatchMaximum() {
    maxBatchRecordCount = 0;
    maxBatchPayloadBytes = 0;
    maxBatchPayloadOffset = 0;
    lastBatchRecordCount = 0;
    batchPollDelayMilliseconds = 145;
  }

  @override
  Future<bool> isAvailable() => inner.isAvailable();
  @override
  Future<AnalysisLease> startAnalysis({
    required int generation,
    required AnalysisProfile profile,
    required int featureMask,
    required Map<String, Object?> transformConfig,
  }) => _start(
    generation: generation,
    profile: profile,
    featureMask: featureMask,
    transformConfig: transformConfig,
  );
  Future<AnalysisLease> _start({
    required int generation,
    required AnalysisProfile profile,
    required int featureMask,
    required Map<String, Object?> transformConfig,
  }) async {
    try {
      return await inner.startAnalysis(
        generation: generation,
        profile: profile,
        featureMask: featureMask,
        transformConfig: transformConfig,
      );
    } on Object catch (error) {
      errors.add(error.toString());
      rethrow;
    }
  }

  @override
  Future<Map<Object?, Object?>> getAnalysisBatch({
    required AnalysisLease lease,
    required BigInt afterSequence,
  }) async {
    final watch = Stopwatch()..start();
    try {
      final batch = await inner.getAnalysisBatch(
        lease: lease,
        afterSequence: afterSequence,
      );
      if (batchSummaries.length < 4) {
        final records = batch['records'];
        batchSummaries.add({
          'keys': batch.keys.map((key) => key.toString()).toList()..sort(),
          'state': batch['state'],
          'profile': batch['profile'],
          'feature_mask': batch['feature_mask'],
          'record_count': batch['record_count'],
          'record_bytes': batch['record_bytes'],
          'payload_bytes': records is Uint8List ? records.lengthInBytes : null,
          'payload_offset_in_bytes': records is Uint8List
              ? records.offsetInBytes
              : null,
          'after_sequence': afterSequence.toString(),
          'next_sequence_cursor': batch['next_sequence_cursor'],
          'delivery_resync': batch['delivery_resync'],
          'resync_reason': batch['resync_reason'],
        });
      }
      receivedRecordCount += (batch['record_count'] as int?) ?? 0;
      final count = (batch['record_count'] as int?) ?? 0;
      lastBatchRecordCount = count;
      final recordBytes = (batch['record_bytes'] as int?) ?? 0;
      final responseRecords = batch['records'];
      if (count > 0 && recordBytes > 0 && responseRecords is Uint8List) {
        latestSourceCenterFrame = BigInt.from(
          ByteData.sublistView(responseRecords)
              .getUint64((count - 1) * recordBytes + 8, Endian.little),
        );
        final origin = batch['run_origin_frame'];
        if (origin is int) runOriginFrame = BigInt.from(origin);
      }
      if (count > maxBatchRecordCount) {
        maxBatchRecordCount = count;
        final records = batch['records'];
        maxBatchPayloadBytes = records is Uint8List ? records.lengthInBytes : 0;
        maxBatchPayloadOffset = records is Uint8List
            ? records.offsetInBytes
            : 0;
      }
      if (batch['delivery_resync'] == true) deliveryResyncCount++;
      skippedDeliveryRecordCount +=
          (batch['skipped_delivery_records'] as int?) ?? 0;
      if (batch['pcm_gap'] == true) pcmGapCount++;
      return batch;
    } on Object catch (error) {
      errors.add(error.toString());
      rethrow;
    } finally {
      rtts.add(watch.elapsedMicroseconds);
    }
  }

  @override
  Future<void> stopAnalysis(AnalysisLease lease) => inner.stopAnalysis(lease);
}

Map<String, double> _percentiles(List<int> values) {
  if (values.isEmpty) return {'count': 0};
  final sorted = [...values]..sort();
  int at(double q) => sorted[((sorted.length - 1) * q).ceil()];
  return {
    'count': sorted.length.toDouble(),
    'p50': at(.50) / 1000,
    'p95': at(.95) / 1000,
    'p99': at(.99) / 1000,
    'max': sorted.last / 1000,
  };
}

Map<String, double> _percentilesDouble(List<double> values) {
  if (values.isEmpty) return {'count': 0};
  final sorted = [...values]..sort();
  double at(double q) => sorted[((sorted.length - 1) * q).ceil()];
  return {
    'count': sorted.length.toDouble(),
    'p50': at(.50),
    'p95': at(.95),
    'p99': at(.99),
    'max': sorted.last,
    'min': sorted.first,
  };
}

Map<String, double> _frameStats(List<FrameTiming> timings) {
  if (timings.isEmpty) return {'count': 0};
  final values = timings.map((t) => t.totalSpan.inMicroseconds / 1000).toList()
    ..sort();
  return {
    'count': values.length.toDouble(),
    'p95': values[((values.length - 1) * .95).ceil()],
    'max': values.last,
  };
}

Uint8List _tone(int seconds) {
  const rate = 48000, channels = 2, bits = 16;
  final frames = rate * seconds, dataSize = frames * channels * bits ~/ 8;
  final bytes = Uint8List(44 + dataSize), data = ByteData.sublistView(bytes);
  void tag(int offset, String value) {
    for (var i = 0; i < value.length; i++) {
      bytes[offset + i] = value.codeUnitAt(i);
    }
  }

  tag(0, 'RIFF');
  data.setUint32(4, 36 + dataSize, Endian.little);
  tag(8, 'WAVE');
  tag(12, 'fmt ');
  data.setUint32(16, 16, Endian.little);
  data.setUint16(20, 1, Endian.little);
  data.setUint16(22, channels, Endian.little);
  data.setUint32(24, rate, Endian.little);
  data.setUint32(28, rate * channels * bits ~/ 8, Endian.little);
  data.setUint16(32, channels * bits ~/ 8, Endian.little);
  data.setUint16(34, bits, Endian.little);
  tag(36, 'data');
  data.setUint32(40, dataSize, Endian.little);
  for (var i = 0; i < frames; i++) {
    final sample = (12000 * math.sin(2 * math.pi * 440 * i / rate)).round();
    final at = 44 + i * 4;
    data.setInt16(at, sample, Endian.little);
    data.setInt16(at + 2, sample, Endian.little);
  }
  return bytes;
}

final _palette = ScenePalette(
  background: const SceneColor(.035, .045, .07),
  surface: const SceneColor(.08, .1, .14),
  primary: const SceneColor(.25, .82, .95),
  secondary: const SceneColor(.38, .45, .98),
  accent: const SceneColor(.98, .44, .65),
  foreground: const SceneColor(.9, .94, 1),
  geometry: SceneGeometry(
    paddingPx: 20,
    strokeWidthPx: 2,
    barGapFraction: .2,
    barCornerRadiusPx: 3,
    particleRadiusScale: 1,
  ),
);
