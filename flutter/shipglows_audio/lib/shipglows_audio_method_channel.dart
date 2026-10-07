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
    int? inputDeviceId,
    bool microphoneEnabled = true,
    String? inputEndpointId,
    String? outputEndpointId,
  }) => _captureCall('startRecording', <String, Object?>{
    'sessionDirectory': sessionDirectory,
    'inputDeviceId': inputDeviceId,
    'microphoneEnabled': microphoneEnabled,
    'inputEndpointId': inputEndpointId,
    'outputEndpointId': outputEndpointId,
  });

  @override
  Future<List<ShipglowsAudioInputDevice>> getInputDevices() async {
    final result = await methodChannel.invokeListMethod<Map<Object?, Object?>>(
      'getInputDevices',
    );
    return (result ?? const <Map<Object?, Object?>>[])
        .map(ShipglowsAudioInputDevice.fromMap)
        .toList(growable: false);
  }

  @override
  Future<List<ShipglowsAudioInputDevice>> getOutputDevices() async {
    final result = await methodChannel.invokeListMethod<Map<Object?, Object?>>(
      'getOutputDevices',
    );
    return (result ?? const <Map<Object?, Object?>>[])
        .map(ShipglowsAudioInputDevice.fromMap)
        .toList(growable: false);
  }

  @override
  Future<ShipglowsAudioCaptureStatus> selectInputDevice(int inputDeviceId) =>
      _captureCall('selectInputDevice', <String, Object?>{
        'inputDeviceId': inputDeviceId,
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

  @override
  Future<ShipglowsAudioPlaybackStatus> loadPlayback({
    required int generation,
    required String localFilePath,
    double seekSeconds = 0,
    double playbackSpeed = 1,
    ShipglowsAudioPlaybackEffects effects = const ShipglowsAudioPlaybackEffects(),
  }) => _playbackCall('loadPlayback', <String, Object?>{
    'apiVersion': ShipglowsAudio.playbackApiVersion,
    'generation': generation,
    'localFilePath': localFilePath,
    'seekSeconds': seekSeconds,
    'playbackSpeed': playbackSpeed,
    'effects': effects.toMap(),
  });

  @override
  Future<ShipglowsAudioPlaybackStatus> seekPlayback({
    required int generation,
    required double positionSeconds,
  }) => _playbackCall('seekPlayback', <String, Object?>{
    'apiVersion': ShipglowsAudio.playbackApiVersion,
    'generation': generation,
    'positionSeconds': positionSeconds,
  });

  @override
  Future<ShipglowsAudioPlaybackStatus> playPlayback({required int generation}) =>
      _playbackCall('playPlayback', _playbackControlArgs(generation));

  @override
  Future<ShipglowsAudioPlaybackStatus> pausePlayback({required int generation}) =>
      _playbackCall('pausePlayback', _playbackControlArgs(generation));

  @override
  Future<ShipglowsAudioPlaybackStatus> stopPlayback({required int generation}) =>
      _playbackCall('stopPlayback', _playbackControlArgs(generation));

  @override
  Future<ShipglowsAudioPlaybackStatus> getPlaybackStatus({required int generation}) =>
      _playbackCall('getPlaybackStatus', _playbackControlArgs(generation));

  @override
  Future<ShipglowsAudioPlaybackStatus> setPlaybackSpeed({
    required int generation,
    required double playbackSpeed,
  }) => _playbackCall('setPlaybackSpeed', <String, Object?>{
    ..._playbackControlArgs(generation),
    'playbackSpeed': playbackSpeed,
  });

  @override
  Future<ShipglowsAudioPlaybackStatus> setPlaybackEffects({
    required int generation,
    required ShipglowsAudioPlaybackEffects effects,
  }) => _playbackCall('setPlaybackEffects', <String, Object?>{
    ..._playbackControlArgs(generation),
    'effects': effects.toMap(),
  });

  Map<String, Object?> _playbackControlArgs(int generation) =>
      <String, Object?>{
        'apiVersion': ShipglowsAudio.playbackApiVersion,
        'generation': generation,
      };

  Future<ShipglowsAudioPlaybackStatus> _playbackCall(
    String method,
    Map<String, Object?> arguments,
  ) async {
    final result = await methodChannel.invokeMethod<Map<Object?, Object?>>(
      method,
      arguments,
    );
    if (result == null) {
      throw PlatformException(
        code: 'playback_status_missing',
        message: 'The native audio engine returned no playback status.',
      );
    }
    return ShipglowsAudioPlaybackStatus.fromMap(result);
  }
}
