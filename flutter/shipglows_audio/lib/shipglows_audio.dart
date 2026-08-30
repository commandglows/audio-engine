import 'shipglows_audio_platform_interface.dart';

class ShipglowsAudioEngineInfo {
  const ShipglowsAudioEngineInfo({
    required this.name,
    required this.version,
    required this.platform,
    required this.backend,
    required this.nativeCoreLoaded,
  });

  factory ShipglowsAudioEngineInfo.fromMap(Map<Object?, Object?> map) {
    return ShipglowsAudioEngineInfo(
      name: map['name'] as String? ?? 'ShipGlows Audio Engine',
      version: map['version'] as String? ?? 'unknown',
      platform: map['platform'] as String? ?? 'unknown',
      backend: map['backend'] as String? ?? 'unknown',
      nativeCoreLoaded: map['nativeCoreLoaded'] as bool? ?? false,
    );
  }

  final String name;
  final String version;
  final String platform;
  final String backend;
  final bool nativeCoreLoaded;
}

class ShipglowsAudioCaptureStatus {
  const ShipglowsAudioCaptureStatus({
    required this.state,
    required this.sampleRate,
    required this.channelCount,
    required this.sampleFormat,
    required this.framesCaptured,
    required this.framesDropped,
    required this.discontinuities,
    required this.clippedSamples,
    required this.deviceRestarts,
    required this.nativeXruns,
    required this.ringOverflowFrames,
    required this.timestampGapFrames,
    required this.writerStalls,
    required this.routeChanges,
    required this.errorCode,
  });

  factory ShipglowsAudioCaptureStatus.fromMap(Map<Object?, Object?> map) {
    return ShipglowsAudioCaptureStatus(
      state: map['state'] as String? ?? 'unknown',
      sampleRate: map['sampleRate'] as int? ?? 0,
      channelCount: map['channelCount'] as int? ?? 0,
      sampleFormat: map['sampleFormat'] as String? ?? 'unknown',
      framesCaptured: map['framesCaptured'] as int? ?? 0,
      framesDropped: map['framesDropped'] as int? ?? 0,
      discontinuities: map['discontinuities'] as int? ?? 0,
      clippedSamples: map['clippedSamples'] as int? ?? 0,
      deviceRestarts: map['deviceRestarts'] as int? ?? 0,
      nativeXruns: map['nativeXruns'] as int? ?? 0,
      ringOverflowFrames: map['ringOverflowFrames'] as int? ?? 0,
      timestampGapFrames: map['timestampGapFrames'] as int? ?? 0,
      writerStalls: map['writerStalls'] as int? ?? 0,
      routeChanges: map['routeChanges'] as int? ?? 0,
      errorCode: map['errorCode'] as String? ?? '',
    );
  }

  final String state;
  final int sampleRate;
  final int channelCount;
  final String sampleFormat;
  final int framesCaptured;
  final int framesDropped;
  final int discontinuities;
  final int clippedSamples;
  final int deviceRestarts;
  final int nativeXruns;
  final int ringOverflowFrames;
  final int timestampGapFrames;
  final int writerStalls;
  final int routeChanges;
  final String errorCode;
}

class ShipglowsAudio {
  Future<ShipglowsAudioEngineInfo> getEngineInfo() =>
      ShipglowsAudioPlatform.instance.getEngineInfo();

  Future<ShipglowsAudioCaptureStatus> startRecording({
    required String sessionDirectory,
  }) => ShipglowsAudioPlatform.instance.startRecording(
    sessionDirectory: sessionDirectory,
  );

  Future<ShipglowsAudioCaptureStatus> stopRecording() =>
      ShipglowsAudioPlatform.instance.stopRecording();

  Future<ShipglowsAudioCaptureStatus> getRecordingStatus() =>
      ShipglowsAudioPlatform.instance.getRecordingStatus();
}
