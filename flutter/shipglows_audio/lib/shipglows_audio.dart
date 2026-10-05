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

class ShipglowsAudioInputDevice {
  const ShipglowsAudioInputDevice({
    required this.id,
    required this.name,
    required this.type,
    required this.isExternal,
    this.endpointId,
  });

  factory ShipglowsAudioInputDevice.fromMap(Map<Object?, Object?> map) =>
      ShipglowsAudioInputDevice(
        endpointId: map['endpointId'] as String?,
        id: map['id'] as int? ?? 0,
        name: map['name'] as String? ?? 'Unknown input',
        type: map['type'] as String? ?? 'unknown',
        isExternal: map['isExternal'] as bool? ?? false,
      );

  final String? endpointId;
  final int id;
  final String name;
  final String type;
  final bool isExternal;
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
    this.hardwareTimestamps = 0,
    this.timestampQueryFailures = 0,
    required this.peakLevel,
    required this.rmsLevel,
    required this.errorCode,
    this.errorRecoverable = false,
    this.recoveryAction = 'none',
    this.outputActiveMilliseconds,
    this.outputSilentMilliseconds,
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
      hardwareTimestamps: map['hardwareTimestamps'] as int? ?? 0,
      timestampQueryFailures: map['timestampQueryFailures'] as int? ?? 0,
      peakLevel: (map['peakLevel'] as num?)?.toDouble() ?? 0,
      rmsLevel: (map['rmsLevel'] as num?)?.toDouble() ?? 0,
      errorCode: map['errorCode'] as String? ?? '',
      errorRecoverable: map['errorRecoverable'] as bool? ?? false,
      recoveryAction: map['recoveryAction'] as String? ?? 'none',
      outputActiveMilliseconds: map['outputActiveMilliseconds'] as int?,
      outputSilentMilliseconds: map['outputSilentMilliseconds'] as int?,
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
  final int hardwareTimestamps;
  final int timestampQueryFailures;
  final double peakLevel;
  final double rmsLevel;
  final String errorCode;
  final bool errorRecoverable;
  final String recoveryAction;
  /// Output monitoring time excluding pauses; null when output is disabled.
  final int? outputActiveMilliseconds;
  /// Consecutive output silence, measured before microphone mixing.
  final int? outputSilentMilliseconds;
}

/// Version 1 host-controlled local-file playback session.
class ShipglowsAudioPlaybackStatus {
  const ShipglowsAudioPlaybackStatus({
    required this.generation,
    required this.state,
    required this.positionSeconds,
    required this.durationSeconds,
    required this.playbackSpeed,
    this.errorCode = '',
  });

  factory ShipglowsAudioPlaybackStatus.fromMap(Map<Object?, Object?> map) =>
      ShipglowsAudioPlaybackStatus(
        generation: map['generation'] as int? ?? 0,
        state: map['state'] as String? ?? 'unknown',
        positionSeconds: (map['positionSeconds'] as num?)?.toDouble() ?? 0,
        durationSeconds: (map['durationSeconds'] as num?)?.toDouble() ?? 0,
        playbackSpeed: (map['playbackSpeed'] as num?)?.toDouble() ?? 1,
        errorCode: map['errorCode'] as String? ?? '',
      );

  final int generation;
  final String state;
  final double positionSeconds;
  final double durationSeconds;
  final double playbackSpeed;
  final String errorCode;
}

/// Bounded, optional DSP controls for the playback host API v1.
class ShipglowsAudioPlaybackEffects {
  const ShipglowsAudioPlaybackEffects({
    this.eqEnabled = false,
    this.lowGainDb = 0,
    this.midGainDb = 0,
    this.highGainDb = 0,
    this.gateEnabled = false,
    this.gateThresholdDbfs = -60,
    this.gateReleaseMs = 120,
    this.compressorEnabled = false,
    this.compressorThresholdDbfs = -18,
    this.compressorRatio = 4,
    this.compressorAttackMs = 10,
    this.compressorReleaseMs = 100,
  });

  final bool eqEnabled;
  final double lowGainDb, midGainDb, highGainDb;
  final bool gateEnabled;
  final double gateThresholdDbfs, gateReleaseMs;
  final bool compressorEnabled;
  final double compressorThresholdDbfs, compressorRatio;
  final double compressorAttackMs, compressorReleaseMs;

  Map<String, Object?> toMap() => <String, Object?>{
    'eqEnabled': eqEnabled,
    'lowGainDb': lowGainDb,
    'midGainDb': midGainDb,
    'highGainDb': highGainDb,
    'gateEnabled': gateEnabled,
    'gateThresholdDbfs': gateThresholdDbfs,
    'gateReleaseMs': gateReleaseMs,
    'compressorEnabled': compressorEnabled,
    'compressorThresholdDbfs': compressorThresholdDbfs,
    'compressorRatio': compressorRatio,
    'compressorAttackMs': compressorAttackMs,
    'compressorReleaseMs': compressorReleaseMs,
  };
}

class ShipglowsAudio {
  static const int playbackApiVersion = 1;

  Future<ShipglowsAudioEngineInfo> getEngineInfo() =>
      ShipglowsAudioPlatform.instance.getEngineInfo();

  Future<ShipglowsAudioCaptureStatus> startRecording({
    required String sessionDirectory,
    int? inputDeviceId,
    bool microphoneEnabled = true,
    String? inputEndpointId,
    String? outputEndpointId,
  }) => ShipglowsAudioPlatform.instance.startRecording(
    sessionDirectory: sessionDirectory,
    inputDeviceId: inputDeviceId,
    microphoneEnabled: microphoneEnabled,
    inputEndpointId: inputEndpointId,
    outputEndpointId: outputEndpointId,
  );

  Future<List<ShipglowsAudioInputDevice>> getOutputDevices() =>
      ShipglowsAudioPlatform.instance.getOutputDevices();

  Future<List<ShipglowsAudioInputDevice>> getInputDevices() =>
      ShipglowsAudioPlatform.instance.getInputDevices();

  Future<ShipglowsAudioCaptureStatus> selectInputDevice(int inputDeviceId) =>
      ShipglowsAudioPlatform.instance.selectInputDevice(inputDeviceId);

  Future<ShipglowsAudioCaptureStatus> stopRecording() =>
      ShipglowsAudioPlatform.instance.stopRecording();

  Future<ShipglowsAudioCaptureStatus> pauseRecording() =>
      ShipglowsAudioPlatform.instance.pauseRecording();

  Future<ShipglowsAudioCaptureStatus> resumeRecording() =>
      ShipglowsAudioPlatform.instance.resumeRecording();

  Future<ShipglowsAudioCaptureStatus> getRecordingStatus() =>
      ShipglowsAudioPlatform.instance.getRecordingStatus();

  Future<ShipglowsAudioPlaybackStatus> loadPlayback({
    required int generation,
    required String localFilePath,
    double seekSeconds = 0,
    double playbackSpeed = 1,
    ShipglowsAudioPlaybackEffects effects = const ShipglowsAudioPlaybackEffects(),
  }) => ShipglowsAudioPlatform.instance.loadPlayback(
    generation: generation,
    localFilePath: localFilePath,
    seekSeconds: seekSeconds,
    playbackSpeed: playbackSpeed,
    effects: effects,
  );

  Future<ShipglowsAudioPlaybackStatus> seekPlayback({
    required int generation,
    required double positionSeconds,
  }) => ShipglowsAudioPlatform.instance.seekPlayback(
    generation: generation,
    positionSeconds: positionSeconds,
  );

  Future<ShipglowsAudioPlaybackStatus> playPlayback({required int generation}) =>
      ShipglowsAudioPlatform.instance.playPlayback(generation: generation);

  Future<ShipglowsAudioPlaybackStatus> pausePlayback({required int generation}) =>
      ShipglowsAudioPlatform.instance.pausePlayback(generation: generation);

  /// Stops the current session and fences it with a fresh generation greater
  /// than the generation returned by [loadPlayback] or the previous stop.
  Future<ShipglowsAudioPlaybackStatus> stopPlayback({required int generation}) =>
      ShipglowsAudioPlatform.instance.stopPlayback(generation: generation);

  Future<ShipglowsAudioPlaybackStatus> getPlaybackStatus({required int generation}) =>
      ShipglowsAudioPlatform.instance.getPlaybackStatus(generation: generation);

  Future<ShipglowsAudioPlaybackStatus> setPlaybackSpeed({
    required int generation,
    required double playbackSpeed,
  }) => ShipglowsAudioPlatform.instance.setPlaybackSpeed(
    generation: generation,
    playbackSpeed: playbackSpeed,
  );

  Future<ShipglowsAudioPlaybackStatus> setPlaybackEffects({
    required int generation,
    required ShipglowsAudioPlaybackEffects effects,
  }) => ShipglowsAudioPlatform.instance.setPlaybackEffects(
    generation: generation,
    effects: effects,
  );
}
