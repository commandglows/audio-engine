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

  @override
  Future<ShipglowsAudioCaptureStatus> startRecording({
    required String sessionDirectory,
  }) => _captureCall('startRecording', <String, Object?>{
    'sessionDirectory': sessionDirectory,
  });

  @override
  Future<ShipglowsAudioCaptureStatus> stopRecording() =>
      _captureCall('stopRecording');

  @override
  Future<ShipglowsAudioCaptureStatus> pauseRecording() =>
      _captureCall('pauseRecording');

  @override
  Future<ShipglowsAudioCaptureStatus> resumeRecording() =>
      _captureCall('resumeRecording');

  @override
  Future<ShipglowsAudioCaptureStatus> getRecordingStatus() =>
      _captureCall('getRecordingStatus');

  Future<ShipglowsAudioCaptureStatus> _captureCall(
    String method, [
    Map<String, Object?>? arguments,
  ]) async {
    final result = await methodChannel.invokeMethod<Map<Object?, Object?>>(
      method,
      arguments,
    );
    if (result == null) {
      throw PlatformException(
        code: 'capture_status_missing',
        message: 'The native audio engine returned no capture status.',
      );
    }
    return ShipglowsAudioCaptureStatus.fromMap(result);
  }
}
