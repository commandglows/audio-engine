import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shipglows_audio/shipglows_audio_method_channel.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  MethodChannelShipglowsAudio platform = MethodChannelShipglowsAudio();
  const MethodChannel channel = MethodChannel('shipglows_audio');

  setUp(() {
    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
        .setMockMethodCallHandler(channel, (MethodCall methodCall) async {
          return <Object?, Object?>{
            'name': 'ShipGlows Audio Engine',
            'version': 'test',
            'platform': 'test',
            'backend': 'mock',
            'nativeCoreLoaded': true,
          };
        });
  });

  tearDown(() {
    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
        .setMockMethodCallHandler(channel, null);
  });

  test('getEngineInfo', () async {
    final info = await platform.getEngineInfo();
    expect(info.version, 'test');
    expect(info.backend, 'mock');
    expect(info.nativeCoreLoaded, isTrue);
  });
}
