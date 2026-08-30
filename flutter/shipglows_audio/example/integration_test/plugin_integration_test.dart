// This is a basic Flutter integration test.
//
// Since integration tests run in a full Flutter application, they can interact
// with the host side of a plugin implementation, unlike Dart unit tests.
//
// For more information about Flutter integration tests, please see
// https://flutter.dev/to/integration-testing

import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:integration_test/integration_test.dart';

import 'package:shipglows_audio/shipglows_audio.dart';

void main() {
  IntegrationTestWidgetsFlutterBinding.ensureInitialized();

  testWidgets('native engine identity is available', (tester) async {
    final plugin = ShipglowsAudio();
    final info = await plugin.getEngineInfo();
    expect(info.name, 'ShipGlows Audio Engine');
    expect(info.version, '0.1.0');
  });

  testWidgets('native backend records recoverable WAV v2 segments', (
    tester,
  ) async {
    if (!Platform.isWindows && !Platform.isAndroid) return;
    final directory = await Directory.systemTemp.createTemp(
      'shipglows-audio-integration-',
    );
    final soakSeconds = int.tryParse(
      Platform.environment['SHIPGLOWS_AUDIO_SOAK_SECONDS'] ?? '',
    );
    final isSoak = soakSeconds != null && soakSeconds > 0;
    addTearDown(() async {
      if (isSoak) return;
      if (await directory.exists()) {
        await directory.delete(recursive: true);
      }
    });

    final plugin = ShipglowsAudio();
    final started = await plugin.startRecording(
      sessionDirectory: directory.path,
    );
    expect(started.state, 'recording');
    expect(started.sampleRate, greaterThan(0));
    await Future<void>.delayed(const Duration(milliseconds: 300));
    for (var iteration = 0; iteration < 5; iteration++) {
      final paused = await plugin.pauseRecording();
      expect(paused.state, 'paused');
      await Future<void>.delayed(const Duration(milliseconds: 40));
      final resumed = await plugin.resumeRecording();
      expect(resumed.state, 'recording');
      await Future<void>.delayed(const Duration(milliseconds: 100));
    }

    if (isSoak) {
      final deadline = DateTime.now().add(Duration(seconds: soakSeconds));
      var previousFrames = 0;
      var maximumRss = ProcessInfo.currentRss;
      while (DateTime.now().isBefore(deadline)) {
        await Future<void>.delayed(const Duration(seconds: 30));
        final status = await plugin.getRecordingStatus();
        maximumRss = ProcessInfo.currentRss > maximumRss
            ? ProcessInfo.currentRss
            : maximumRss;
        expect(status.state, 'recording');
        expect(status.framesCaptured, greaterThan(previousFrames));
        expect(status.errorCode, isEmpty);
        previousFrames = status.framesCaptured;
        // Kept machine-readable so the external soak runner can retain samples.
        // ignore: avoid_print
        print(
          'SOAK_SAMPLE frames=${status.framesCaptured} '
          'dropped=${status.framesDropped} '
          'gaps=${status.timestampGapFrames} '
          'xruns=${status.nativeXruns} rss=$maximumRss',
        );
      }
    }

    final stopped = await plugin.stopRecording();
    expect(stopped.state, 'stopped');
    expect(stopped.framesCaptured, greaterThan(0));
    expect(stopped.hardwareTimestamps, greaterThan(0));
    expect(stopped.errorCode, isEmpty);

    final journal = File(
      '${directory.path}${Platform.pathSeparator}journal.sga',
    );
    expect(await journal.exists(), isTrue);
    expect(await journal.readAsString(), contains('event=session_complete'));
    final segments = await directory
        .list()
        .where((entry) => entry.path.endsWith('.wav'))
        .toList();
    expect(segments, isNotEmpty);
    final first = File(segments.first.path);
    expect(await first.length(), greaterThan(44));
    expect(
      String.fromCharCodes((await first.openRead(0, 4).first)),
      equals('RIFF'),
    );
    if (isSoak) {
      // ignore: avoid_print
      print(
        'SOAK_RESULT directory=${directory.path} '
        'frames=${stopped.framesCaptured} dropped=${stopped.framesDropped} '
        'gaps=${stopped.timestampGapFrames} xruns=${stopped.nativeXruns} '
        'timestamps=${stopped.hardwareTimestamps}',
      );
    }
  }, timeout: const Timeout(Duration(hours: 3)));
}
