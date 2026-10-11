import 'package:flutter/services.dart';

import 'analysis_controller.dart';
import 'transform_config.dart';
import 'visual_record.dart';

const _leaseResponseKeys = <String>{
  'analysis_api_version',
  'generation',
  'lease_id',
  'stream_id',
  'source_identity',
  'profile',
  'feature_mask',
  'transform_digest',
  'max_block_frames',
  'max_batch_records',
  'max_response_bytes',
};

/// Windows audio-engine v2 source adapter. Capability discovery is read-only;
/// it never opens playback or capture.
final class MethodChannelAnalysisSource implements AnalysisSource {
  MethodChannelAnalysisSource({MethodChannel? channel})
    : _channel = channel ?? const MethodChannel('shipglows_audio');

  final MethodChannel _channel;

  @override
  Future<bool> isAvailable() async {
    Map<Object?, Object?>? capabilities;
    try {
      capabilities = await _channel.invokeMapMethod<Object?, Object?>(
        'getAnalysisCapabilities',
        const {'analysisApiVersion': 2},
      );
    } on MissingPluginException {
      return false;
    } on PlatformException {
      return false;
    }
    if (capabilities == null || capabilities['analysis_api_version'] != 2) {
      return false;
    }
    if (capabilities['available'] != true) return false;
    final sources = capabilities['sources'];
    if (sources is! List<Object?>) return false;
    return sources.any(
      (source) =>
          source is Map<Object?, Object?> &&
          source['id'] == 'windows_playback_output_v1' &&
          source['available'] == true,
    );
  }

  @override
  Future<AnalysisLease> startAnalysis({
    required int generation,
    required AnalysisProfile profile,
    required int featureMask,
    required Map<String, Object?> transformConfig,
  }) async {
    final digest = visualTransformDigest(transformConfig);
    final response = await _channel.invokeMapMethod<Object?, Object?>(
      'startAnalysis',
      <String, Object?>{
        'analysisApiVersion': 2,
        'generation': generation,
        'profile': profile == AnalysisProfile.music
            ? 'sgav-music-1'
            : 'sgav-speech-1',
        'featureMask': featureMask,
        'transformConfig': transformConfig,
      },
    );
    if (response == null ||
        response.length != 11 ||
        response.keys.any(
          (key) => key is! String || !_leaseResponseKeys.contains(key),
        ) ||
        response['analysis_api_version'] != 2 ||
        response['generation'] != generation ||
        response['profile'] !=
            (profile == AnalysisProfile.music
                ? 'sgav-music-1'
                : 'sgav-speech-1') ||
        response['feature_mask'] != featureMask ||
        response['transform_digest'] != digest ||
        response['max_block_frames'] is! int ||
        (response['max_block_frames']! as int) < 1 ||
        (response['max_block_frames']! as int) > 16384 ||
        response['max_batch_records'] != 16 ||
        response['max_response_bytes'] != 131072 ||
        response['lease_id'] is! String ||
        response['stream_id'] is! String ||
        response['source_identity'] is! String) {
      throw const FormatException(
        'Native analysis lease does not match its request.',
      );
    }
    return AnalysisLease(
      generation: generation,
      leaseId: response['lease_id']! as String,
      streamId: response['stream_id']! as String,
      sourceIdentity: response['source_identity']! as String,
      profile: profile,
      featureMask: featureMask,
      transformDigest: digest,
    );
  }

  @override
  Future<Map<Object?, Object?>> getAnalysisBatch({
    required AnalysisLease lease,
    required BigInt afterSequence,
  }) async {
    if (afterSequence < BigInt.zero ||
        afterSequence > BigInt.from(9007199254740991)) {
      throw ArgumentError.value(afterSequence, 'afterSequence');
    }
    final response = await _channel.invokeMapMethod<Object?, Object?>(
      'getAnalysisBatch',
      <String, Object?>{
        'analysisApiVersion': 2,
        'generation': lease.generation,
        'leaseId': lease.leaseId,
        'afterSequence': afterSequence.toInt(),
      },
    );
    if (response == null) {
      throw const FormatException('Native analysis returned no batch.');
    }
    return response;
  }

  @override
  Future<void> stopAnalysis(AnalysisLease lease) async {
    bool? stopped;
    try {
      stopped = await _channel.invokeMethod<bool>(
        'stopAnalysis',
        <String, Object?>{
          'analysisApiVersion': 2,
          'generation': lease.generation,
          'leaseId': lease.leaseId,
        },
      );
    } on PlatformException catch (error) {
      if (error.code == 'stale_analysis_lease') return;
      rethrow;
    }
    if (stopped != true) {
      throw const FormatException('Native analysis lease did not stop.');
    }
  }
}
