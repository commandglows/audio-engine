import 'dart:convert';
import 'dart:typed_data';

import 'record_codec.dart';
import 'visual_record.dart';

const _maxSafeInteger = 9007199254740991;
const _maxResponseBytes = 131072;
const _maxMetadataBytes = 4096;

final class AnalysisBatchContext {
  const AnalysisBatchContext({
    required this.generation,
    required this.leaseId,
    required this.streamId,
    required this.sourceIdentity,
    required this.profile,
    required this.featureMask,
    required this.transformDigest,
  });

  final int generation;
  final String leaseId;
  final String? streamId;
  final String sourceIdentity;
  final AnalysisProfile profile;
  final int featureMask;
  final String transformDigest;
}

/// Fully validated all-or-nothing AnalysisBatch response.
final class AnalysisBatch {
  AnalysisBatch._({
    required this.state,
    required this.streamId,
    required this.continuityId,
    required this.originFrame,
    required this.nextSequenceCursor,
    required this.records,
    required this.deliveryResync,
    required this.resyncReason,
    required this.pcmGap,
    required this.droppedPcmFrames,
    required this.nativeObservationNs,
    required this.timing,
  });

  final String state;
  final String streamId;
  final BigInt continuityId;
  final BigInt? originFrame;
  final BigInt nextSequenceCursor;
  final List<VisualRecord> records;
  final bool deliveryResync;
  final String resyncReason;
  final bool pcmGap;
  final BigInt droppedPcmFrames;
  final BigInt nativeObservationNs;
  final Map<String, BigInt>? timing;

  static const _keys = <String>{
    'analysis_api_version',
    'generation',
    'lease_id',
    'stream_id',
    'source_identity',
    'profile',
    'feature_mask',
    'transform_digest',
    'state',
    'continuity_id',
    'run_origin_frame',
    'record_layout',
    'first_sequence',
    'last_sequence',
    'next_sequence_cursor',
    'delivery_resync',
    'resync_reason',
    'skipped_delivery_records',
    'pcm_gap',
    'dropped_pcm_frames',
    'native_observation_ns',
    'timing',
    'record_bytes',
    'record_count',
    'records',
  };

  static AnalysisBatch decode({
    required Map<Object?, Object?> response,
    required AnalysisBatchContext context,
    required BigInt afterSequence,
  }) => _decode(
    response: response,
    context: context,
    afterSequence: afterSequence,
    transferPayload: false,
  );

  /// Decodes a transport response by adopting its payload buffer. The source
  /// must not mutate or reuse `records` after returning this response.
  static AnalysisBatch decodeTransferred({
    required Map<Object?, Object?> response,
    required AnalysisBatchContext context,
    required BigInt afterSequence,
  }) => _decode(
    response: response,
    context: context,
    afterSequence: afterSequence,
    transferPayload: true,
  );

  static AnalysisBatch _decode({
    required Map<Object?, Object?> response,
    required AnalysisBatchContext context,
    required BigInt afterSequence,
    required bool transferPayload,
  }) {
    Never fail(String message) => throw FormatException(message);
    if (response.length != _keys.length ||
        response.keys.any((key) => key is! String || !_keys.contains(key))) {
      fail('Analysis response keys do not match API v2.');
    }
    final metadata = <String, Object?>{
      for (final key in _keys)
        if (key != 'records') key: response[key],
    };
    final metadataBytes = utf8.encode(jsonEncode(metadata)).length;
    if (metadataBytes > _maxMetadataBytes) {
      fail('Analysis response metadata exceeds 4096 bytes.');
    }

    int intValue(String key, {int max = _maxSafeInteger}) {
      final value = response[key];
      if (value is! int || value < 0 || value > max) {
        fail('$key is not a safe nonnegative integer.');
      }
      return value;
    }

    BigInt uint64Value(String key, {bool nullable = false}) {
      final value = response[key];
      if (nullable && value == null) return BigInt.zero;
      final result = value is int
          ? BigInt.from(value)
          : value is BigInt
          ? value
          : BigInt.from(-1);
      if (result < BigInt.zero || result > ((BigInt.one << 64) - BigInt.one)) {
        fail('$key is not an unsigned 64-bit integer.');
      }
      return result;
    }

    String stringValue(String key, {int maxLength = 128}) {
      final value = response[key];
      if (value is! String || value.isEmpty || value.length > maxLength) {
        fail('$key is not a valid bounded string.');
      }
      return value;
    }

    bool boolValue(String key) {
      final value = response[key];
      if (value is! bool) fail('$key must be boolean.');
      return value;
    }

    if (intValue('analysis_api_version') != 2 ||
        intValue('generation') != context.generation ||
        stringValue('lease_id') != context.leaseId ||
        stringValue('source_identity') != context.sourceIdentity ||
        stringValue('transform_digest') != context.transformDigest ||
        stringValue('record_layout') != recordLayoutV2) {
      fail('Analysis response identity or version does not match the lease.');
    }
    final profileText = stringValue('profile');
    final profile = switch (profileText) {
      'sgav-music-1' => AnalysisProfile.music,
      'sgav-speech-1' => AnalysisProfile.speech,
      _ => throw const FormatException('Unsupported analysis profile.'),
    };
    final featureMask = intValue('feature_mask', max: 13);
    if (profile != context.profile || featureMask != context.featureMask) {
      fail('Analysis profile or feature mask changed within the lease.');
    }
    final streamId = stringValue('stream_id', maxLength: 64);
    if (context.streamId != null && streamId != context.streamId) {
      fail('Analysis stream changed within the lease.');
    }
    final state = stringValue('state', maxLength: 32);
    const states = {
      'disabled',
      'warming_up',
      'live',
      'paused',
      'ended',
      'gap',
      'unavailable',
      'failed',
    };
    if (!states.contains(state)) fail('Unknown analysis state.');
    final continuityId = uint64Value('continuity_id');
    final rawOrigin = response['run_origin_frame'];
    final originFrame = rawOrigin == null
        ? null
        : uint64Value('run_origin_frame');
    if (state != 'warming_up' && originFrame == null) {
      fail('Run origin is required after PCM position admission.');
    }
    final recordBytes = VisualRecordCodec.recordBytes(profile, featureMask);
    if (intValue('record_bytes', max: 6492) != recordBytes) {
      fail('record_bytes does not match the selected feature layout.');
    }
    final recordCount = intValue('record_count', max: 16);
    final recordsPayload = response['records'];
    if (recordsPayload is! Uint8List) {
      fail('records must be a binary Uint8List.');
    }
    final payload = recordsPayload;
    final expectedPayload = recordCount * recordBytes;
    if (payload.length != expectedPayload ||
        metadataBytes + payload.length > _maxResponseBytes) {
      fail('Record count and exact payload length do not match.');
    }
    final firstSequence = uint64Value('first_sequence');
    final lastSequence = uint64Value('last_sequence');
    final cursor = uint64Value('next_sequence_cursor');
    final skipped = uint64Value('skipped_delivery_records');
    final resync = boolValue('delivery_resync');
    final reason = stringValue('resync_reason', maxLength: 32);
    const reasons = {
      'none',
      'initial_window',
      'pcm_discontinuity',
      'history_overwrite',
      'lag_window',
    };
    if (!reasons.contains(reason) || resync != (reason != 'none')) {
      fail('Delivery resync metadata is inconsistent.');
    }
    final pcmGap = boolValue('pcm_gap');
    final droppedPcmFrames = uint64Value('dropped_pcm_frames');
    final nativeObservationNs = uint64Value('native_observation_ns');
    final timingValue = response['timing'];
    Map<String, BigInt>? timing;
    if (timingValue != null) {
      if (timingValue is! Map<Object?, Object?> ||
          timingValue.length != 3 ||
          timingValue.keys.any((key) => key is! String) ||
          !const {
            'anchor_output_frame',
            'anchor_presentation_ns',
            'anchor_uncertainty_ns',
          }.containsAll(timingValue.keys.cast<String>())) {
        fail('Timing anchor does not match the closed timing schema.');
      }
      final timingMap = timingValue;
      timing = {
        for (final entry in timingMap.entries)
          entry.key as String: _asUint64(entry.value, fail),
      };
    }
    if (recordCount == 0) {
      if (payload.isNotEmpty ||
          firstSequence != BigInt.zero ||
          lastSequence != BigInt.zero ||
          skipped != BigInt.zero ||
          cursor != afterSequence ||
          (resync && reason != 'pcm_discontinuity') ||
          (!resync && reason != 'none')) {
        fail('Empty batch metadata is inconsistent.');
      }
    } else {
      if (originFrame == null ||
          recordCount > 8 && resync ||
          firstSequence == BigInt.zero ||
          lastSequence - firstSequence + BigInt.one !=
              BigInt.from(recordCount) ||
          cursor != lastSequence ||
          skipped != _maxZero(firstSequence - afterSequence - BigInt.one) ||
          (!resync && firstSequence != afterSequence + BigInt.one) ||
          (resync && reason == 'none')) {
        fail('Nonempty batch sequence/resync metadata is inconsistent.');
      }
    }

    final store = transferPayload
        ? VisualRecordFloat32Store.transfer(payload)
        : VisualRecordFloat32Store.copy(payload);
    final decoded = <VisualRecord>[];
    for (var index = 0; index < recordCount; index++) {
      final start = index * recordBytes;
      final record = VisualRecordCodec.decodeStored(
        store: store,
        byteOffset: start,
        recordLength: recordBytes,
        streamId: streamId,
        sourceIdentity: context.sourceIdentity,
        transformDigest: context.transformDigest,
        profile: profile,
        featureMask: featureMask,
        originFrame: originFrame!,
      );
      if (record.continuityId != continuityId ||
          (index == 0 && record.sequence != firstSequence) ||
          (index == recordCount - 1 && record.sequence != lastSequence) ||
          (index > 0 &&
              (record.sequence != decoded.last.sequence + BigInt.one ||
                  record.centerFrame - decoded.last.centerFrame !=
                      BigInt.from(480)))) {
        fail('Decoded records are not contiguous in one continuity.');
      }
      decoded.add(record);
    }
    return AnalysisBatch._(
      state: state,
      streamId: streamId,
      continuityId: continuityId,
      originFrame: originFrame,
      nextSequenceCursor: cursor,
      records: List.unmodifiable(decoded),
      deliveryResync: resync,
      resyncReason: reason,
      pcmGap: pcmGap,
      droppedPcmFrames: droppedPcmFrames,
      nativeObservationNs: nativeObservationNs,
      timing: timing == null ? null : Map.unmodifiable(timing),
    );
  }

  static BigInt _asUint64(Object? value, Never Function(String) fail) {
    final result = value is int
        ? BigInt.from(value)
        : value is BigInt
        ? value
        : BigInt.from(-1);
    if (result < BigInt.zero || result > ((BigInt.one << 64) - BigInt.one)) {
      fail('Timing field is not an unsigned 64-bit integer.');
    }
    return result;
  }

  static BigInt _maxZero(BigInt value) =>
      value < BigInt.zero ? BigInt.zero : value;
}
