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
  }) {
    throw UnimplementedError('startRecording() has not been implemented.');
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
}
