import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';

import 'shipglows_audio.dart';
import 'shipglows_audio_platform_interface.dart';

/// An implementation of [ShipglowsAudioPlatform] that uses method channels.
class MethodChannelShipglowsAudio extends ShipglowsAudioPlatform {
  /// The method channel used to interact with the native platform.
  @visibleForTesting
  final methodChannel = const MethodChannel('shipglows_audio');

  @override
  Future<ShipglowsAudioEngineInfo> getEngineInfo() async {
    final result = await methodChannel.invokeMethod<Map<Object?, Object?>>(
      'getEngineInfo',
    );
    if (result == null) {
      throw PlatformException(
        code: 'engine_info_missing',
        message: 'The native audio engine returned no identity.',
      );
    }
    return ShipglowsAudioEngineInfo.fromMap(result);
  }
}
