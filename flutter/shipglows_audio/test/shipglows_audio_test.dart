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

  @override
  Future<ShipglowsAudioCaptureStatus> startRecording({
    required String sessionDirectory,
    int? inputDeviceId,
    bool microphoneEnabled = true,
    String? inputEndpointId,
    String? outputEndpointId,
  }) async => _status('recording');

  @override
  Future<List<ShipglowsAudioInputDevice>> getOutputDevices() async => const [];

  @override
  Future<List<ShipglowsAudioInputDevice>> getInputDevices() async => const [
    ShipglowsAudioInputDevice(
      id: 7,
      name: 'External microphone',
      type: 'bluetooth_sco',
      isExternal: true,
    ),
  ];

  @override
  Future<ShipglowsAudioCaptureStatus> selectInputDevice(
    int inputDeviceId,
  ) async => _status('recording');

  @override
  Future<ShipglowsAudioCaptureStatus> stopRecording() async =>
      _status('stopped');

  @override
  Future<ShipglowsAudioCaptureStatus> pauseRecording() async =>
      _status('paused');

  @override
  Future<ShipglowsAudioCaptureStatus> resumeRecording() async =>
      _status('recording');

  @override
  Future<ShipglowsAudioCaptureStatus> getRecordingStatus() async =>
      _status('recording');

  ShipglowsAudioCaptureStatus _status(String state) =>
      ShipglowsAudioCaptureStatus(
        state: state,
        sampleRate: 48000,
        channelCount: 1,
        sampleFormat: 'int16',
        framesCaptured: 480,
        framesDropped: 0,
        discontinuities: 0,
        clippedSamples: 0,
        deviceRestarts: 0,
        nativeXruns: 0,
        ringOverflowFrames: 0,
        timestampGapFrames: 0,
        writerStalls: 0,
        routeChanges: 0,
        peakLevel: 0.5,
        rmsLevel: 0.25,
        errorCode: '',
      );
}

void main() {
  test('output activity is optional and decoded independently of mixed levels', () {
    final legacy = ShipglowsAudioCaptureStatus.fromMap({});
    expect(legacy.outputActiveMilliseconds, isNull);
    expect(legacy.outputSilentMilliseconds, isNull);
    final mixed = ShipglowsAudioCaptureStatus.fromMap({
      'peakLevel': 0.9,
      'outputActiveMilliseconds': 8000,
      'outputSilentMilliseconds': 7000,
    });
    expect(mixed.outputActiveMilliseconds, 8000);
    expect(mixed.outputSilentMilliseconds, 7000);
    expect(mixed.peakLevel, 0.9);
  });
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

  test('input device selection delegates to the platform engine', () async {
    final plugin = ShipglowsAudio();
    final devices = await plugin.getInputDevices();
    final switched = await plugin.selectInputDevice(devices.single.id);
    expect(devices.single.isExternal, isTrue);
    expect(switched.state, 'recording');
  });

  test('capture lifecycle delegates to the platform engine', () async {
    final plugin = ShipglowsAudio();
    final started = await plugin.startRecording(sessionDirectory: 'session');
    final paused = await plugin.pauseRecording();
    final resumed = await plugin.resumeRecording();
    final stopped = await plugin.stopRecording();
    expect(started.state, 'recording');
    expect(paused.state, 'paused');
    expect(resumed.state, 'recording');
    expect(stopped.state, 'stopped');
  });
}
