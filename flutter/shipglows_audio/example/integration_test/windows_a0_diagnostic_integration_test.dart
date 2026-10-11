import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:integration_test/integration_test.dart';

const _a0Enabled = bool.fromEnvironment('SHIPGLOWS_AUDIO_A0_ENABLED');
const _channel = MethodChannel('shipglows_audio/a0');

void main() {
  IntegrationTestWidgetsFlutterBinding.ensureInitialized();

  testWidgets(
    'A0 diagnostic channel remains callable for the plugin lifetime',
    (_) async {
      final start = await _channel.invokeMapMethod<String, Object?>(
        'start',
        const {'mask': 3, 'enabled': false, 'silent_output': true},
      );
      expect(start?['mask'], 3);
      expect(start?['enabled'], false);
      expect(start?['silent_output'], true);
      expect(start?['endpoint_name'], isA<String>());

      await Future<void>.delayed(const Duration(milliseconds: 250));
      final poll = await _channel.invokeMapMethod<String, Object?>(
        'poll',
        const {'after_sequence': 0},
      );
      expect(poll, isNotNull);

      final stopped = await _channel.invokeMapMethod<String, Object?>('stop');
      expect(stopped, isNotNull);
    },
    skip: !_a0Enabled,
  );
}
