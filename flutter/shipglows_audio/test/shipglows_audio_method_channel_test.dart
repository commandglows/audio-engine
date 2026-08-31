import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shipglows_audio/shipglows_audio_method_channel.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  MethodChannelShipglowsAudio platform = MethodChannelShipglowsAudio();
  const MethodChannel channel = MethodChannel('shipglows_audio');

  setUp(() {
    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
        .setMockMethodCallHandler(channel, (MethodCall methodCall) async {
          if (methodCall.method == 'getInputDevices') {
            return <Object?>[
              <Object?, Object?>{
                'id': 42,
                'name': 'Bose QC35',
                'type': 'bluetooth_sco',
                'isExternal': true,
              },
            ];
          }
          if (methodCall.method != 'getEngineInfo') {
            return <Object?, Object?>{
              'state': methodCall.method == 'stopRecording'
                  ? 'stopped'
                  : 'recording',
              'sampleRate': 48000,
              'channelCount': 1,
              'sampleFormat': 'int16',
              'framesCaptured': 480,
              'framesDropped': 0,
              'discontinuities': 0,
              'clippedSamples': 0,
              'deviceRestarts': 0,
              'nativeXruns': 2,
              'ringOverflowFrames': 3,
              'timestampGapFrames': 4,
              'writerStalls': 5,
              'routeChanges': 6,
              'errorCode': '',
            };
          }
          return <Object?, Object?>{
            'name': 'ShipGlows Audio Engine',
            'version': 'test',
            'platform': 'test',
            'backend': 'mock',
            'nativeCoreLoaded': true,
          };
        });
  });

  tearDown(() {
    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
        .setMockMethodCallHandler(channel, null);
  });

  test('getEngineInfo', () async {
    final info = await platform.getEngineInfo();
    expect(info.version, 'test');
    expect(info.backend, 'mock');
    expect(info.nativeCoreLoaded, isTrue);
  });

  test('capture commands decode native status', () async {
    final started = await platform.startRecording(
      sessionDirectory: 'session',
      inputDeviceId: 42,
    );
    final devices = await platform.getInputDevices();
    final switched = await platform.selectInputDevice(42);
    final stopped = await platform.stopRecording();
    expect(started.state, 'recording');
    expect(started.sampleRate, 48000);
    expect(started.nativeXruns, 2);
    expect(started.ringOverflowFrames, 3);
    expect(started.timestampGapFrames, 4);
    expect(started.writerStalls, 5);
    expect(started.routeChanges, 6);
    expect(devices.single.name, 'Bose QC35');
    expect(devices.single.isExternal, isTrue);
    expect(switched.state, 'recording');
    expect(stopped.state, 'stopped');
  });
}
