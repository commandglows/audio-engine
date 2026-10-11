import 'dart:convert';
import 'dart:io';

import 'package:shipglows_visualization/src/scene.dart';
import 'package:shipglows_visualization/src/speech_window.dart';
import 'package:shipglows_visualization/src/visual_record.dart';

VisualRecord readRecord(Map<String, dynamic> value) => VisualRecord(
  streamId: value['streamId'] as String,
  sourceIdentity: value['sourceIdentity'] as String,
  transformDigest: value['transformDigest'] as String,
  profile: AnalysisProfile.values.byName(value['profile'] as String),
  featureMask: value['featureMask'] as int,
  sequence: BigInt.parse(value['sequence'] as String),
  centerFrame: BigInt.parse(value['centerFrame'] as String),
  originFrame: BigInt.parse(value['originFrame'] as String),
  continuityId: BigInt.parse(value['continuityId'] as String),
  validFrames: value['validFrames'] as int,
  envelopeBins: value['envelopeBins'] as int,
  physicalLevels: _doubles(value['physicalLevels']),
  visualLevels: _doubles(value['visualLevels']),
  physicalEnvelope: _optionalDoubles(value['physicalEnvelope']),
  visualEnvelope: _optionalDoubles(value['visualEnvelope']),
  physicalSpectrum: _optionalDoubles(value['physicalSpectrum']),
  visualBands: _optionalDoubles(value['visualBands']),
);

List<double> _doubles(dynamic input) =>
    (input as List).cast<num>().map((v) => v.toDouble()).toList();
List<double>? _optionalDoubles(dynamic input) =>
    input == null ? null : _doubles(input);

Map<String, dynamic> sceneJson(SceneState state) {
  final output = <String, dynamic>{
    'status': state.status.name,
    'label': state.label,
    'intensity': state.intensity,
    'validSuffixCount': state.validSuffixCount,
  };
  if (state.baseline) {
    output['baseline'] = true;
  }
  if (state.bars.isNotEmpty) {
    output['bars'] = state.bars
        .map(
          (v) => {
            'index': v.index,
            'x': v.x,
            'height': v.height,
            'role': v.role.name,
          },
        )
        .toList();
  }
  if (state.musicWave.isNotEmpty) {
    output['musicWave'] = state.musicWave
        .map(
          (v) => {
            'index': v.index,
            'x': v.x,
            'leftMin': v.leftMin,
            'leftMax': v.leftMax,
            'rightMin': v.rightMin,
            'rightMax': v.rightMax,
          },
        )
        .toList();
  }
  if (state.speechWave.isNotEmpty) {
    output['speechWave'] = state.speechWave
        .map(
          (v) => {
            'index': v.index,
            'x': v.x,
            'rms': v.rms,
            'peak': v.peak,
            'valid': v.valid,
          },
        )
        .toList();
  }
  if (state.radialFieldStrength != 0) {
    output['field'] = state.radialFieldStrength;
  }
  if (state.radialSpokes.isNotEmpty) {
    output['spokes'] = state.radialSpokes
        .map(
          (v) => {
            'index': v.index,
            'angle': v.angle,
            'inner': v.innerRadius,
            'outer': v.outerRadius,
            'opacity': v.opacity,
            'role': v.role.name,
          },
        )
        .toList();
  }
  if (state.radialRings.isNotEmpty) {
    output['rings'] = state.radialRings
        .map((v) => {'index': v.index, 'radius': v.radius, 'role': v.role.name})
        .toList();
  }
  if (state.particles.isNotEmpty) {
    output['particles'] = state.particles
        .map(
          (v) => {
            'index': v.index,
            'x': v.x,
            'y': v.y,
            'radius': v.radius,
            'opacity': v.opacity,
            'role': v.role.name,
          },
        )
        .toList();
  }
  return output;
}

Future<void> main(List<String> args) async {
  if (args.length != 2) {
    throw ArgumentError(
      'usage: dart run tool/scene_fixture_parity.dart INPUT.json OUTPUT.json',
    );
  }
  final fixture =
      jsonDecode(await File(args[0]).readAsString()) as Map<String, dynamic>;
  Map<int, VisualRecord> readRecords(String key) => {
    for (final raw in fixture[key] as List)
      (raw as Map<String, dynamic>)['index'] as int: readRecord(
        raw['record'] as Map<String, dynamic>,
      ),
  };
  final music = readRecords('musicRecords');
  final speech = readRecords('speechRecords');
  final scenes = <Map<String, dynamic>>[];
  const evaluator = SceneEvaluator();
  for (final raw in fixture['frames'] as List) {
    final frame = raw as Map<String, dynamic>;
    final source = frame['profile'] == 'music' ? music : speech;
    final leftIndex = frame['leftIndex'] as int,
        rightIndex = frame['rightIndex'] as int;
    final left = source[leftIndex]!, right = source[rightIndex]!;
    final alpha = (frame['alpha'] as num).toDouble();
    final record = alpha == 0
        ? left
        : alpha == 1
        ? right
        : left.interpolateWith(right, alpha);
    SpeechWindow? window;
    if (frame['mode'] == 'speechWave') {
      SpeechWindow at(int index) {
        final history =
            speech.entries
                .where(
                  (entry) => entry.key <= index && entry.key >= index - 149,
                )
                .toList()
              ..sort((left, right) => left.key.compareTo(right.key));
        return SpeechWindow.fromHistory(
          history.map((entry) => entry.value),
          endpointCenterFrame: speech[index]!.centerFrame,
        );
      }

      final low = at(leftIndex), high = at(rightIndex);
      window = alpha == 0
          ? low
          : alpha == 1
          ? high
          : low.interpolateWith(high, alpha);
    }
    final configJson = frame['config'] as Map<String, dynamic>;
    final mode = switch (configJson['mode'] as String) {
      'bars' => SceneMode.spectrumBars,
      'musicWave' => SceneMode.musicOscilloscope,
      'speechWave' => SceneMode.speechEnvelope,
      _ => throw FormatException('Unsupported Stage A mode.'),
    };
    final config = SceneConfig(
      mode: mode,
      profile: AnalysisProfile.values.byName(configJson['profile'] as String),
      featureMask: configJson['featureMask'] as int,
      transformDigest: configJson['transformDigest'] as String,
      intensity: (configJson['intensity'] as num).toDouble(),
      motion: SceneMotion.values.byName(configJson['motion'] as String),
      timeSeconds: (configJson['timeSeconds'] as num).toDouble(),
    );
    scenes.add(
      sceneJson(
        evaluator.evaluate(
          config: config,
          status: VisualizationStatus.live,
          record: record,
          speechWindow: window,
        ),
      ),
    );
  }
  await File(args[1]).writeAsString(jsonEncode(scenes));
}
