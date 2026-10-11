import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shipglows_visualization/shipglows_visualization.dart';

const _channel = MethodChannel('shipglows_audio_test');

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  test('transform digest uses the canonical v1 defaults', () {
    expect(
      visualTransformCanonicalJson(defaultVisualTransformConfig),
      '{"amplitude_gamma":0.8,"attack_ms":20,"release_ms":180,'
      '"sensitivity_db":0,"spectrum_ceiling_db":0,'
      '"spectrum_floor_db":-80,"version":1}',
    );
    expect(
      visualTransformDigest(defaultVisualTransformConfig),
      '74dc6923a924808157182ff39fcd281ef9560184235e4b48d084341f47784ef5',
    );
    expect(
      () => visualTransformDigest({
        ...defaultVisualTransformConfig,
        'unknown': 1,
      }),
      throwsFormatException,
    );
    expect(
      visualTransformCanonicalJson({
        ...defaultVisualTransformConfig,
        'amplitude_gamma': 1.0,
        'sensitivity_db': 1e-7,
      }),
      '{"amplitude_gamma":1,"attack_ms":20,"release_ms":180,'
      '"sensitivity_db":1e-7,"spectrum_ceiling_db":0,'
      '"spectrum_floor_db":-80,"version":1}',
    );
  });

  test(
    'method channel adapter discovers, opens, polls, and releases a lease',
    () async {
      final messenger =
          TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger;
      final digest = visualTransformDigest(defaultVisualTransformConfig);
      final calls = <String>[];
      messenger.setMockMethodCallHandler(_channel, (call) async {
        calls.add(call.method);
        switch (call.method) {
          case 'getAnalysisCapabilities':
            return <String, Object?>{
              'analysis_api_version': 2,
              'available': true,
              'sources': [
                {'id': 'windows_playback_output_v1', 'available': true},
              ],
            };
          case 'startAnalysis':
            return <String, Object?>{
              'analysis_api_version': 2,
              'generation': 7,
              'lease_id': 'lease-7',
              'stream_id': 'stream-7',
              'source_identity': 'windows-playback-v1-7',
              'profile': 'sgav-music-1',
              'feature_mask': 11,
              'transform_digest': digest,
              'max_block_frames': 16384,
              'max_batch_records': 16,
              'max_response_bytes': 131072,
            };
          case 'getAnalysisBatch':
            return <Object?, Object?>{'records': Uint8List(0)};
          case 'stopAnalysis':
            if ((call.arguments! as Map<Object?, Object?>)['leaseId'] ==
                'stale') {
              throw PlatformException(code: 'stale_analysis_lease');
            }
            return true;
        }
        throw MissingPluginException(call.method);
      });

      final source = MethodChannelAnalysisSource(channel: _channel);
      expect(await source.isAvailable(), isTrue);
      final lease = await source.startAnalysis(
        generation: 7,
        profile: AnalysisProfile.music,
        featureMask: 11,
        transformConfig: defaultVisualTransformConfig,
      );
      expect(lease.transformDigest, digest);
      final batch = await source.getAnalysisBatch(
        lease: lease,
        afterSequence: BigInt.zero,
      );
      expect(batch['records'], isA<Uint8List>());
      await source.stopAnalysis(lease);
      await source.stopAnalysis(
        AnalysisLease(
          generation: lease.generation,
          leaseId: 'stale',
          streamId: lease.streamId,
          sourceIdentity: lease.sourceIdentity,
          profile: lease.profile,
          featureMask: lease.featureMask,
          transformDigest: lease.transformDigest,
        ),
      );
      expect(calls, [
        'getAnalysisCapabilities',
        'startAnalysis',
        'getAnalysisBatch',
        'stopAnalysis',
        'stopAnalysis',
      ]);

      messenger.setMockMethodCallHandler(_channel, null);
    },
  );
}
