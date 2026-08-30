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

class ShipglowsAudio {
  Future<ShipglowsAudioEngineInfo> getEngineInfo() =>
      ShipglowsAudioPlatform.instance.getEngineInfo();
}
