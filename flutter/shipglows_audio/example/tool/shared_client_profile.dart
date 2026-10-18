import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';
import 'dart:ui' show FrameTiming;

import 'package:flutter/widgets.dart';
import 'package:shipglows_visualization/shipglows_visualization.dart';

const _digest =
    '0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef';

void main() => runApp(const _ClientProfileApp());

final class _ClientProfileApp extends StatefulWidget {
  const _ClientProfileApp();

  @override
  State<_ClientProfileApp> createState() => _ClientProfileAppState();
}

final class _ClientProfileAppState extends State<_ClientProfileApp> {
  late final _SyntheticSpeechSource _source = _SyntheticSpeechSource();
  late final AnalysisController _controller = AnalysisController(
    source: _source,
    sceneConfig: SceneConfig(
      mode: SceneMode.speechEnvelope,
      profile: AnalysisProfile.speech,
      featureMask: 5,
      transformDigest: _digest,
      intensity: .8,
      motion: SceneMotion.full,
    ),
    transformConfig: const {},
    generation: 3,
  );
  final List<FrameTiming> _cleanTimings = [];
  final List<FrameTiming> _tracedTimings = [];
  Timer? _ticker;
  String _windowLogicalSize = 'unknown';

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addTimingsCallback(_onTimings);
    WidgetsBinding.instance.addPostFrameCallback((_) => unawaited(_run()));
  }

  void _onTimings(List<FrameTiming> timings) {
    final target = _traceActive ? _tracedTimings : _cleanTimings;
    target.addAll(timings);
  }

  bool _traceActive = false;

  Future<void> _run() async {
    await _controller.start();
    _ticker = Timer.periodic(const Duration(milliseconds: 33), (_) {
      unawaited(_controller.poll());
    });
    await Future<void>.delayed(const Duration(seconds: 2));
    _cleanTimings.clear();
    await Future<void>.delayed(const Duration(seconds: 8));
    _traceActive = true;
    stdout.writeln('SHARED_CLIENT_ALLOCATION_TRACE_BEGIN');
    await stdout.flush();
    await Future<void>.delayed(const Duration(seconds: 20));
    stdout.writeln('SHARED_CLIENT_ALLOCATION_TRACE_END');
    _traceActive = false;
    await stdout.flush();
    _ticker?.cancel();
    _ticker = null;
    await Future<void>.delayed(const Duration(milliseconds: 150));
    WidgetsBinding.instance.removeTimingsCallback(_onTimings);

    final report = <String, Object?>{
      'schema': 'shared-client-profile-1',
      'runtime': 'Flutter Windows Profile',
      'pid': pid,
      'fixture': 'synthetic binary speech batches through AnalysisController',
      'window_logical_px': _windowLogicalSize,
      'poll_interval_ms': 33,
      'warmup_seconds': 2,
      'clean_measure_seconds': 8,
      'trace_measure_seconds': 20,
      'polls': _source.sequence,
      'retained_speech_hops': _controller.retainedRecordCount,
      'valid_suffix_count': _controller.state.value.validSuffixCount,
      'final_status': _controller.state.value.status.name,
      'clean_frames': _timingsSummary(_cleanTimings),
      'allocation_traced_frames': _timingsSummary(_tracedTimings),
      'limits': [
        'The source synthesizes valid MethodChannel-shaped response maps and bytes in-process; no native bridge or audio endpoint is exercised.',
        'Class allocation tracing changes runtime overhead; traced frame timings are diagnostic only.',
        'Class traces count sampled target-class allocations, not total feature-owned bytes or a complete AC-A4 proof.',
        'No calibrated audio-to-screen timing is measured.',
      ],
    };
    final encoded = jsonEncode(report);
    stdout.writeln('SHARED_CLIENT_PROFILE_JSON:$encoded');
    await stdout.flush();
    final output = Platform.environment['SHIPGLOWS_CLIENT_PROFILE_OUTPUT'];
    if (output != null && output.isNotEmpty) {
      await File(output).writeAsString('$encoded\n', flush: true);
    }
    await _controller.dispose();
    exit(0);
  }

  @override
  void dispose() {
    _ticker?.cancel();
    WidgetsBinding.instance.removeTimingsCallback(_onTimings);
    unawaited(_controller.dispose());
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final view = View.of(context);
    final size = view.physicalSize / view.devicePixelRatio;
    _windowLogicalSize =
        '${size.width.toStringAsFixed(1)}x${size.height.toStringAsFixed(1)}';
    return Directionality(
      textDirection: TextDirection.ltr,
      child: SizedBox.expand(
        child: SharedSceneView(state: _controller.state, palette: _palette),
      ),
    );
  }
}

Map<String, Object?> _timingsSummary(List<FrameTiming> timings) {
  List<int> values(int Function(FrameTiming) read) =>
      timings.map(read).toList(growable: false)..sort();
  Map<String, int> summary(List<int> sorted) {
    if (sorted.isEmpty) return {'count': 0};
    int at(double p) => sorted[((sorted.length - 1) * p).ceil()];
    return {
      'count': sorted.length,
      'p50': at(.50),
      'p95': at(.95),
      'p99': at(.99),
      'max': sorted.last,
    };
  }

  return {
    'build_us': summary(
      values((timing) => timing.buildDuration.inMicroseconds),
    ),
    'raster_us': summary(
      values((timing) => timing.rasterDuration.inMicroseconds),
    ),
    'total_span_us': summary(
      values((timing) => timing.totalSpan.inMicroseconds),
    ),
  };
}

final class _SyntheticSpeechSource implements AnalysisSource {
  int sequence = 0;

  @override
  Future<bool> isAvailable() async => true;

  @override
  Future<AnalysisLease> startAnalysis({
    required int generation,
    required AnalysisProfile profile,
    required int featureMask,
    required Map<String, Object?> transformConfig,
  }) async => const AnalysisLease(
    generation: 3,
    leaseId: 'profile-lease',
    streamId: 'profile-stream',
    sourceIdentity: 'synthetic-profile',
    profile: AnalysisProfile.speech,
    featureMask: 5,
    transformDigest: _digest,
  );

  @override
  Future<Map<Object?, Object?>> getAnalysisBatch({
    required AnalysisLease lease,
    required BigInt afterSequence,
  }) async {
    final next = ++sequence;
    return {
      'analysis_api_version': 2,
      'generation': 3,
      'lease_id': 'profile-lease',
      'stream_id': 'profile-stream',
      'source_identity': 'synthetic-profile',
      'profile': 'sgav-speech-1',
      'feature_mask': 5,
      'transform_digest': _digest,
      'state': 'live',
      'continuity_id': 1,
      'run_origin_frame': 0,
      'record_layout': recordLayoutV2,
      'first_sequence': next,
      'last_sequence': next,
      'next_sequence_cursor': next,
      'delivery_resync': false,
      'resync_reason': 'none',
      'skipped_delivery_records': 0,
      'pcm_gap': false,
      'dropped_pcm_frames': 0,
      'native_observation_ns': next * 10000000,
      'timing': null,
      'record_bytes': 88,
      'record_count': 1,
      'records': _encodeRecord(next),
    };
  }

  @override
  Future<void> stopAnalysis(AnalysisLease lease) async {}
}

Uint8List _encodeRecord(int sequence) {
  final bytes = Uint8List(88);
  final data = ByteData.sublistView(bytes);
  data.setUint64(0, sequence, Endian.little);
  data.setUint64(8, (sequence - 1) * 480, Endian.little);
  data.setUint64(16, 1, Endian.little);
  data.setUint32(24, 480, Endian.little);
  data.setUint32(28, 0, Endian.little);
  data.setUint32(32, 0, Endian.little);
  data.setUint32(36, 0, Endian.little);
  const List<double> physical = [.3, .4, .4, .1, .2, .158113883];
  const List<double> visual = [0, 0, .5, 0, 0, .25];
  for (var index = 0; index < 6; index++) {
    data.setFloat32(40 + index * 4, physical[index], Endian.little);
    data.setFloat32(64 + index * 4, visual[index], Endian.little);
  }
  return bytes;
}

final _palette = ScenePalette(
  background: const SceneColor(.015, .02, .035),
  surface: const SceneColor(.08, .1, .14),
  primary: const SceneColor(.2, .75, .92),
  secondary: const SceneColor(.94, .34, .58),
  accent: const SceneColor(.98, .74, .25),
  foreground: const SceneColor(.92, .96, 1),
  geometry: SceneGeometry(
    paddingPx: 12,
    strokeWidthPx: 2,
    barGapFraction: .18,
    barCornerRadiusPx: 2,
    particleRadiusScale: 1,
  ),
);
