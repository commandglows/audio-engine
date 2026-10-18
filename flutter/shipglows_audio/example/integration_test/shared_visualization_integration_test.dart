import 'dart:io';
import 'dart:math' as math;
import 'dart:typed_data';

import 'package:flutter/widgets.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:integration_test/integration_test.dart';
import 'package:shipglows_audio/shipglows_audio.dart';
import 'package:shipglows_visualization/shipglows_visualization.dart';

void main() {
  IntegrationTestWidgetsFlutterBinding.ensureInitialized();

  testWidgets('Windows playback feeds the shared scene view through analysis', (
    tester,
  ) async {
    if (!Platform.isWindows) return;
    final semantics = tester.ensureSemantics();
    final directory = await Directory.systemTemp.createTemp(
      'shipglows-visualization-integration-',
    );
    final wav = File('${directory.path}${Platform.pathSeparator}tone.wav');
    await wav.writeAsBytes(_stereoToneWav(seconds: 2));

    final engine = ShipglowsAudio();
    const generation = 1;
    var playbackLoaded = false;
    AnalysisController? controller;
    addTearDown(() async {
      if (controller != null) await controller.dispose();
      if (playbackLoaded) {
        try {
          await engine.stopPlayback(generation: generation + 1);
        } on Object {
          // The integration assertion owns the original failure.
        }
      }
      if (await directory.exists()) await directory.delete(recursive: true);
    });

    final source = MethodChannelAnalysisSource();
    expect(await source.isAvailable(), isTrue);
    await engine.loadPlayback(generation: generation, localFilePath: wav.path);
    playbackLoaded = true;

    final transform = Map<String, Object?>.unmodifiable(
      defaultVisualTransformConfig,
    );
    final activeController = AnalysisController(
      source: source,
      generation: generation,
      transformConfig: transform,
      sceneConfig: SceneConfig(
        mode: SceneMode.musicOscilloscope,
        profile: AnalysisProfile.music,
        featureMask: 3,
        transformDigest: visualTransformDigest(transform),
        intensity: .65,
        motion: SceneMotion.full,
      ),
    );
    controller = activeController;
    await tester.pumpWidget(
      Directionality(
        textDirection: TextDirection.ltr,
        child: SizedBox.expand(
          child: SharedSceneView(
            state: activeController.state,
            palette: _palette,
          ),
        ),
      ),
    );
    await activeController.start();
    await engine.playPlayback(generation: generation);

    for (
      var attempt = 0;
      attempt < 120 &&
          activeController.nextSequenceCursor < BigInt.from(32);
      attempt++
    ) {
      await activeController.poll();
      await tester.pump();
      await Future<void>.delayed(const Duration(milliseconds: 25));
    }
    final playbackStatus = await engine.getPlaybackStatus(
      generation: generation,
    );
    expect(
      activeController.retainedRecordCount,
      greaterThan(0),
      reason:
          'visualizer=${activeController.state.value.status}/'
          '${activeController.state.value.label}, playback=${playbackStatus.state}/'
          '${playbackStatus.errorCode}, underruns=${playbackStatus.underruns}',
    );
    expect(
      activeController.state.value.status,
      anyOf(VisualizationStatus.live, VisualizationStatus.ended),
    );
    expect(
      activeController.nextSequenceCursor,
      greaterThanOrEqualTo(BigInt.from(32)),
      reason: 'the real MethodChannel path must deliver multiple analysis lots',
    );
    expect(
      playbackStatus.underruns,
      0,
      reason: 'the tone playback must not underrun during shared analysis',
    );
    expect(
      activeController.state.value.musicWave.any(
        (span) =>
            (span.leftMin - .25).abs() > .01 ||
            (span.leftMax - .25).abs() > .01 ||
            (span.rightMin - .75).abs() > .01 ||
            (span.rightMax - .75).abs() > .01,
      ),
      isTrue,
      reason: 'a non-zero WAV must produce non-baseline shared scene geometry',
    );
    // Keep the observed software-path coverage visible in integration logs.
    // This Debug fixture is not a timing or physical audio-to-screen measure.
    // ignore: avoid_print
    print(
      'shared_visualization_debug '
      'accepted_sequence=${activeController.nextSequenceCursor} '
      'playback_state=${playbackStatus.state} '
      'underruns=${playbackStatus.underruns} '
      'scene_status=${activeController.state.value.status.name}',
    );
    expect(find.byType(SharedSceneView), findsOneWidget);
    expect(
      find.bySemanticsLabel(activeController.state.value.label),
      findsOneWidget,
    );
    semantics.dispose();
  });
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

Uint8List _stereoToneWav({required int seconds}) {
  const sampleRate = 48000;
  const channels = 2;
  const bitsPerSample = 16;
  final frameCount = sampleRate * seconds;
  final dataBytes = frameCount * channels * bitsPerSample ~/ 8;
  final bytes = Uint8List(44 + dataBytes);
  final data = ByteData.sublistView(bytes);

  void text(int offset, String value) {
    for (var index = 0; index < value.length; index++) {
      bytes[offset + index] = value.codeUnitAt(index);
    }
  }

  text(0, 'RIFF');
  data.setUint32(4, 36 + dataBytes, Endian.little);
  text(8, 'WAVE');
  text(12, 'fmt ');
  data.setUint32(16, 16, Endian.little);
  data.setUint16(20, 1, Endian.little);
  data.setUint16(22, channels, Endian.little);
  data.setUint32(24, sampleRate, Endian.little);
  data.setUint32(28, sampleRate * channels * bitsPerSample ~/ 8, Endian.little);
  data.setUint16(32, channels * bitsPerSample ~/ 8, Endian.little);
  data.setUint16(34, bitsPerSample, Endian.little);
  text(36, 'data');
  data.setUint32(40, dataBytes, Endian.little);
  for (var frame = 0; frame < frameCount; frame++) {
    final sample = (0.45 * 32767 *
            math.sin(2 * math.pi * 440 * frame / sampleRate))
        .round();
    final offset = 44 + frame * channels * bitsPerSample ~/ 8;
    data.setInt16(offset, sample, Endian.little);
    data.setInt16(offset + 2, sample, Endian.little);
  }
  return bytes;
}
