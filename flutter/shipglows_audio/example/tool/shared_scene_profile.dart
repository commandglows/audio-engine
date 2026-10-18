import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'dart:ui' show FrameTiming;

import 'package:flutter/widgets.dart';
import 'package:shipglows_visualization/shipglows_visualization.dart';

const _digest =
    '0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef';

void main() {
  runApp(const _ProfileHarness());
}

final class _ProfileHarness extends StatefulWidget {
  const _ProfileHarness();

  @override
  State<_ProfileHarness> createState() => _ProfileHarnessState();
}

final class _ProfileHarnessState extends State<_ProfileHarness> {
  late final ValueNotifier<SceneState> _scene = ValueNotifier(_initialScene());
  final List<FrameTiming> _timings = [];
  final List<Map<String, Object?>> _phases = [];
  final SceneEvaluator _evaluator = const SceneEvaluator();
  Timer? _ticker;
  int _updates = 0;
  bool _running = false;
  String _windowLogicalSize = 'unknown';

  static const _modes = [
    SceneMode.spectrumBars,
    SceneMode.musicOscilloscope,
    SceneMode.speechEnvelope,
  ];

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addTimingsCallback(_receiveTimings);
    WidgetsBinding.instance.addPostFrameCallback((_) => unawaited(_run()));
  }

  @override
  void dispose() {
    _ticker?.cancel();
    WidgetsBinding.instance.removeTimingsCallback(_receiveTimings);
    _scene.dispose();
    super.dispose();
  }

  void _receiveTimings(List<FrameTiming> timings) => _timings.addAll(timings);

  Future<void> _run() async {
    if (_running) return;
    _running = true;
    for (final mode in _modes) {
      await _runPhase(mode);
    }
    WidgetsBinding.instance.removeTimingsCallback(_receiveTimings);
    final report = <String, Object?>{
      'schema': 'shared-scene-profile-1',
      'runtime': 'Flutter Windows Profile',
      'pid': pid,
      'fixture': 'synthetic transformed scene states; no audio source',
      'window_logical_px': _windowLogicalSize,
      'warmup_seconds_per_mode': 2,
      'measure_seconds_per_mode': 10,
      'modes': _phases,
      'limits': [
        'No audio callback, endpoint, analysis bridge or physical timing was measured.',
        'FrameTiming reports Flutter frame build/raster timing, not Dart allocation events.',
        'Process CPU and memory require external sampling and include Flutter runtime overhead.',
      ],
    };
    final encoded = jsonEncode(report);
    stdout.writeln('SHARED_SCENE_PROFILE_JSON:$encoded');
    await stdout.flush();
    final output = Platform.environment['SHIPGLOWS_PAINTER_PROFILE_OUTPUT'];
    if (output != null && output.isNotEmpty) {
      await File(output).writeAsString('$encoded\n', flush: true);
    }
    exit(0);
  }

  Future<void> _runPhase(SceneMode mode) async {
    final config = _config(mode);
    final record = _record(mode);
    final speechWindow = mode == SceneMode.speechEnvelope
        ? _speechWindow()
        : null;
    _scene.value = _evaluator.evaluate(
      config: config,
      status: VisualizationStatus.live,
      record: record,
      speechWindow: speechWindow,
    );
    _timings.clear();
    _updates = 0;
    _ticker?.cancel();
    _ticker = Timer.periodic(const Duration(milliseconds: 33), (_) {
      _updates++;
      _scene.value = _evaluator.evaluate(
        config: config,
        status: VisualizationStatus.live,
        record: record,
        speechWindow: speechWindow,
      );
    });
    await Future<void>.delayed(const Duration(seconds: 2));
    _timings.clear();
    _updates = 0;
    await Future<void>.delayed(const Duration(seconds: 10));
    _ticker?.cancel();
    _ticker = null;
    await Future<void>.delayed(const Duration(milliseconds: 150));
    final buildUs = _timings
        .map((timing) => timing.buildDuration.inMicroseconds)
        .toList(growable: false);
    final rasterUs = _timings
        .map((timing) => timing.rasterDuration.inMicroseconds)
        .toList(growable: false);
    final totalUs = _timings
        .map((timing) => timing.totalSpan.inMicroseconds)
        .toList(growable: false);
    _phases.add({
      'mode': mode.name,
      'scene_updates': _updates,
      'frames': _timings.length,
      'build_us': _summary(buildUs),
      'raster_us': _summary(rasterUs),
      'total_span_us': _summary(totalUs),
    });
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
        child: SharedSceneView(state: _scene, palette: _palette),
      ),
    );
  }
}

Map<String, int> _summary(List<int> input) {
  if (input.isEmpty) return {'count': 0};
  final values = input.toList()..sort();
  int percentile(double p) => values[((values.length - 1) * p).ceil()];
  return {
    'count': values.length,
    'p50': percentile(.50),
    'p95': percentile(.95),
    'p99': percentile(.99),
    'max': values.last,
  };
}

ScenePalette get _palette => ScenePalette(
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

SceneState _initialScene() => SceneState(
  status: VisualizationStatus.warmingUp,
  label: 'Profile starting',
  intensity: 1,
  validSuffixCount: 0,
);

SceneConfig _config(SceneMode mode) => SceneConfig(
  mode: mode,
  profile: mode == SceneMode.speechEnvelope
      ? AnalysisProfile.speech
      : AnalysisProfile.music,
  featureMask: switch (mode) {
    SceneMode.spectrumBars => 11,
    SceneMode.musicOscilloscope => 3,
    SceneMode.speechEnvelope => 5,
    _ => throw ArgumentError.value(mode),
  },
  transformDigest: _digest,
  intensity: .8,
  motion: SceneMotion.full,
);

VisualRecord _record(SceneMode mode) {
  if (mode == SceneMode.speechEnvelope) {
    return VisualRecord(
      streamId: 'profile-stream',
      sourceIdentity: 'synthetic-profile',
      transformDigest: _digest,
      profile: AnalysisProfile.speech,
      featureMask: 5,
      sequence: BigInt.one,
      centerFrame: BigInt.zero,
      originFrame: BigInt.zero,
      continuityId: BigInt.one,
      validFrames: 480,
      physicalLevels: const [.3, .4, .4, .1, .2, .158113883],
      visualLevels: const [0, 0, .5, 0, 0, .25],
    );
  }
  return VisualRecord(
    streamId: 'profile-stream',
    sourceIdentity: 'synthetic-profile',
    transformDigest: _digest,
    profile: AnalysisProfile.music,
    featureMask: mode == SceneMode.spectrumBars ? 11 : 3,
    sequence: BigInt.one,
    centerFrame: BigInt.zero,
    originFrame: BigInt.zero,
    continuityId: BigInt.one,
    validFrames: 480,
    envelopeBins: 64,
    physicalLevels: const [.3, .4, .4, .1, .2, .158113883],
    visualLevels: const [0, 0, .5, 0, 0, .25],
    physicalEnvelope: List.generate(256, (index) => .2 * (index % 7) / 7),
    visualEnvelope: List.generate(256, (index) => .8 * (index % 7) / 7),
    physicalSpectrum: mode == SceneMode.spectrumBars
        ? List.filled(1025, .25)
        : null,
    visualBands: mode == SceneMode.spectrumBars
        ? List.generate(64, (index) => .15 + .8 * (index % 9) / 9)
        : null,
  );
}

SpeechWindow _speechWindow() {
  final history = SpeechHistoryBuffer();
  late VisualRecord latest;
  for (var sequence = 1; sequence <= speechHistoryCapacity; sequence++) {
    latest = VisualRecord(
      streamId: 'profile-stream',
      sourceIdentity: 'synthetic-profile',
      transformDigest: _digest,
      profile: AnalysisProfile.speech,
      featureMask: 5,
      sequence: BigInt.from(sequence),
      centerFrame: BigInt.from((sequence - 1) * 480),
      originFrame: BigInt.zero,
      continuityId: BigInt.one,
      validFrames: 480,
      physicalLevels: const [.3, .4, .4, .1, .2, .158113883],
      visualLevels: [0, 0, .5, 0, 0, .15 + .1 * (sequence % 5)],
    );
    history.add(latest);
  }
  return history.windowAt(latest.centerFrame);
}
