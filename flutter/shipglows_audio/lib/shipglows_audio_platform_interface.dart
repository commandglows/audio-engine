import 'package:plugin_platform_interface/plugin_platform_interface.dart';

import 'shipglows_audio.dart';
import 'shipglows_audio_method_channel.dart';

abstract class ShipglowsAudioPlatform extends PlatformInterface {
  /// Constructs a ShipglowsAudioPlatform.
  ShipglowsAudioPlatform() : super(token: _token);

  static final Object _token = Object();

  static ShipglowsAudioPlatform _instance = MethodChannelShipglowsAudio();

  /// The default instance of [ShipglowsAudioPlatform] to use.
  ///
  /// Defaults to [MethodChannelShipglowsAudio].
  static ShipglowsAudioPlatform get instance => _instance;

  /// Platform-specific implementations should set this with their own
  /// platform-specific class that extends [ShipglowsAudioPlatform] when
  /// they register themselves.
  static set instance(ShipglowsAudioPlatform instance) {
    PlatformInterface.verifyToken(instance, _token);
    _instance = instance;
  }

  Future<ShipglowsAudioEngineInfo> getEngineInfo() {
    throw UnimplementedError('getEngineInfo() has not been implemented.');
  }

  Future<ShipglowsAudioCaptureStatus> startRecording({
    required String sessionDirectory,
    int? inputDeviceId,
    bool microphoneEnabled = true,
    String? inputEndpointId,
    String? outputEndpointId,
  }) {
    throw UnimplementedError('startRecording() has not been implemented.');
  }

  Future<List<ShipglowsAudioInputDevice>> getOutputDevices() {
    throw UnimplementedError('getOutputDevices() has not been implemented.');
  }

  Future<List<ShipglowsAudioInputDevice>> getInputDevices() {
    throw UnimplementedError('getInputDevices() has not been implemented.');
  }

  Future<ShipglowsAudioCaptureStatus> selectInputDevice(int inputDeviceId) {
    throw UnimplementedError('selectInputDevice() has not been implemented.');
  }

  Future<ShipglowsAudioCaptureStatus> stopRecording() {
    throw UnimplementedError('stopRecording() has not been implemented.');
  }

  Future<ShipglowsAudioCaptureStatus> pauseRecording() {
    throw UnimplementedError('pauseRecording() has not been implemented.');
  }

  Future<ShipglowsAudioCaptureStatus> resumeRecording() {
    throw UnimplementedError('resumeRecording() has not been implemented.');
  }

  Future<ShipglowsAudioCaptureStatus> getRecordingStatus() {
    throw UnimplementedError('getRecordingStatus() has not been implemented.');
  }

  Future<ShipglowsAudioPlaybackStatus> loadPlayback({
    required int generation,
    required String localFilePath,
    double seekSeconds = 0,
    double playbackSpeed = 1,
    ShipglowsAudioPlaybackEffects effects = const ShipglowsAudioPlaybackEffects(),
  }) => throw UnimplementedError('loadPlayback() has not been implemented.');

  Future<ShipglowsAudioPlaybackStatus> seekPlayback({
    required int generation,
    required double positionSeconds,
  }) => throw UnimplementedError('seekPlayback() has not been implemented.');

  Future<ShipglowsAudioPlaybackStatus> playPlayback({required int generation}) =>
      throw UnimplementedError('playPlayback() has not been implemented.');

  Future<ShipglowsAudioPlaybackStatus> pausePlayback({required int generation}) =>
      throw UnimplementedError('pausePlayback() has not been implemented.');

  /// Stop requires a fresh monotonically increasing session generation.
  Future<ShipglowsAudioPlaybackStatus> stopPlayback({required int generation}) =>
      throw UnimplementedError('stopPlayback() has not been implemented.');

  Future<ShipglowsAudioPlaybackStatus> getPlaybackStatus({required int generation}) =>
      throw UnimplementedError('getPlaybackStatus() has not been implemented.');

  Future<ShipglowsAudioPlaybackStatus> setPlaybackSpeed({
    required int generation,
    required double playbackSpeed,
  }) => throw UnimplementedError('setPlaybackSpeed() has not been implemented.');

  Future<ShipglowsAudioPlaybackStatus> setPlaybackEffects({
    required int generation,
    required ShipglowsAudioPlaybackEffects effects,
  }) => throw UnimplementedError('setPlaybackEffects() has not been implemented.');
}
