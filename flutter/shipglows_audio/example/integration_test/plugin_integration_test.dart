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

  testWidgets('WASAPI records recoverable PCM segments', (tester) async {
    if (!Platform.isWindows) return;
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
    await Future<void>.delayed(const Duration(seconds: 1));

    final stopped = await plugin.stopRecording();
    expect(stopped.state, 'stopped');
    expect(stopped.framesCaptured, greaterThan(0));
    expect(stopped.errorCode, isEmpty);

    final manifest = File(
      '${directory.path}${Platform.pathSeparator}manifest.sga',
    );
    expect(await manifest.exists(), isTrue);
    expect(await manifest.readAsString(), contains('complete=true'));
    final segments = await directory
        .list()
        .where((entry) => entry.path.endsWith('.pcm'))
        .toList();
    expect(segments, isNotEmpty);
    expect(await File(segments.first.path).length(), greaterThan(0));
  });
}
