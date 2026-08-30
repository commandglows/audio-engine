import 'package:flutter_test/flutter_test.dart';
import 'package:shipglows_audio/shipglows_audio.dart';
import 'package:shipglows_audio/shipglows_audio_platform_interface.dart';
import 'package:shipglows_audio/shipglows_audio_method_channel.dart';
import 'package:plugin_platform_interface/plugin_platform_interface.dart';

class MockShipglowsAudioPlatform
    with MockPlatformInterfaceMixin
    implements ShipglowsAudioPlatform {
  @override
  Future<ShipglowsAudioEngineInfo> getEngineInfo() async =>
      const ShipglowsAudioEngineInfo(
        name: 'ShipGlows Audio Engine',
        version: 'test',
        platform: 'test',
        backend: 'fake',
        nativeCoreLoaded: true,
      );
}

void main() {
  final ShipglowsAudioPlatform initialPlatform =
      ShipglowsAudioPlatform.instance;

  test('$MethodChannelShipglowsAudio is the default instance', () {
    expect(initialPlatform, isInstanceOf<MethodChannelShipglowsAudio>());
  });

  test('getEngineInfo', () async {
    final shipglowsAudioPlugin = ShipglowsAudio();
    final fakePlatform = MockShipglowsAudioPlatform();
    ShipglowsAudioPlatform.instance = fakePlatform;

    final info = await shipglowsAudioPlugin.getEngineInfo();
    expect(info.version, 'test');
    expect(info.nativeCoreLoaded, isTrue);
  });
}
