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
    addTearDown(() async {
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

    final stopped = await plugin.stopRecording();
    expect(stopped.state, 'stopped');
    expect(stopped.framesCaptured, greaterThan(0));
    expect(stopped.hardwareTimestamps, greaterThan(0));
    expect(stopped.errorCode, isEmpty);

    final journal = File(
      '${directory.path}${Platform.pathSeparator}journal.sga',
    );
    expect(await journal.exists(), isTrue);
    expect(
      await journal.readAsString(),
      contains('event=session_complete'),
    );
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
  });
}
