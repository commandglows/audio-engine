import 'dart:async';
import 'dart:math' as math;
import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';
import 'package:flutter/material.dart';
import 'package:shipglows_visualization/shipglows_visualization.dart';

const digest =
    '0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef';

Map<Object?, Object?> batchResponse({
  int count = 0,
  int firstSequence = 1,
  Uint8List? payload,
}) => {
  'analysis_api_version': 2,
  'generation': 3,
  'lease_id': 'lease-1',
  'stream_id': 'stream-1',
  'source_identity': 'source-1',
  'profile': 'sgav-speech-1',
  'feature_mask': 5,
  'transform_digest': digest,
  'state': count == 0 ? 'warming_up' : 'live',
  'continuity_id': count == 0 ? 0 : 1,
  'run_origin_frame': count == 0 ? null : 0,
  'record_layout': recordLayoutV2,
  'first_sequence': count == 0 ? 0 : firstSequence,
  'last_sequence': count == 0 ? 0 : firstSequence + count - 1,
  'next_sequence_cursor': count == 0 ? 0 : firstSequence + count - 1,
  'delivery_resync': false,
  'resync_reason': 'none',
  'skipped_delivery_records': 0,
  'pcm_gap': false,
  'dropped_pcm_frames': 0,
  'native_observation_ns': 123,
  'timing': null,
  'record_bytes': 88,
  'record_count': count,
  'records': payload ?? Uint8List(count * 88),
};

Uint8List encodedSpeechRecords(List<int> sequences) {
  final bytes = Uint8List(sequences.length * 88);
  final data = ByteData.sublistView(bytes);
  for (var index = 0; index < sequences.length; index++) {
    final offset = index * 88;
    data.setUint64(offset, sequences[index], Endian.little);
    data.setUint64(offset + 8, (sequences[index] - 1) * 480, Endian.little);
    data.setUint64(offset + 16, 1, Endian.little);
    data.setUint32(offset + 24, 480, Endian.little);
  }
  return bytes;
}

Map<Object?, Object?> musicBatchResponse({
  required String state,
  required int count,
  BigInt? afterSequence,
}) {
  const recordBytes = 6492;
  const firstSequence = 1;
  final payload = Uint8List(count * recordBytes);
  final data = ByteData.sublistView(payload);
  for (var index = 0; index < count; index++) {
    final offset = index * recordBytes;
    data.setUint64(offset, firstSequence + index, Endian.little);
    data.setUint64(offset + 8, index * 480, Endian.little);
    data.setUint64(offset + 16, 1, Endian.little);
    data.setUint32(offset + 24, 480, Endian.little);
    data.setUint32(offset + 28, 64, Endian.little);
  }
  final lastSequence = count == 0 ? 0 : firstSequence + count - 1;
  return {
    ...batchResponse(),
    'profile': 'sgav-music-1',
    'feature_mask': 11,
    'state': state,
    'continuity_id': 1,
    'run_origin_frame': 0,
    'first_sequence': count == 0 ? 0 : firstSequence,
    'last_sequence': lastSequence,
    'next_sequence_cursor': count == 0
        ? (afterSequence ?? BigInt.zero).toInt()
        : lastSequence,
    'skipped_delivery_records': 0,
    'pcm_gap': false,
    'dropped_pcm_frames': 0,
    'record_bytes': recordBytes,
    'record_count': count,
    'records': payload,
  };
}

final class FakeAnalysisSource implements AnalysisSource {
  FakeAnalysisSource(this.responses, {AnalysisLease? lease})
    : lease =
          lease ??
          const AnalysisLease(
            generation: 3,
            leaseId: 'lease-1',
            streamId: 'stream-1',
            sourceIdentity: 'source-1',
            profile: AnalysisProfile.speech,
            featureMask: 5,
            transformDigest: digest,
          );

  final List<Map<Object?, Object?>> responses;
  final AnalysisLease lease;
  Completer<void>? gate;
  Completer<void>? startGate;
  Completer<void>? stopGate;
  int polls = 0;
  int activePolls = 0;
  int maximumActivePolls = 0;
  int stops = 0;
  int activeStops = 0;
  int maximumActiveStops = 0;
  int starts = 0;
  int failedStops = 0;

  @override
  Future<bool> isAvailable() async => true;

  @override
  Future<AnalysisLease> startAnalysis({
    required int generation,
    required AnalysisProfile profile,
    required int featureMask,
    required Map<String, Object?> transformConfig,
  }) async {
    starts++;
    await startGate?.future;
    return lease;
  }

  @override
  Future<Map<Object?, Object?>> getAnalysisBatch({
    required AnalysisLease lease,
    required BigInt afterSequence,
  }) async {
    polls++;
    activePolls++;
    if (activePolls > maximumActivePolls) maximumActivePolls = activePolls;
    try {
      await gate?.future;
      return responses.removeAt(0);
    } finally {
      activePolls--;
    }
  }

  @override
  Future<void> stopAnalysis(AnalysisLease lease) async {
    stops++;
    activeStops++;
    if (activeStops > maximumActiveStops) {
      maximumActiveStops = activeStops;
    }
    try {
      await stopGate?.future;
      if (failedStops > 0) {
        failedStops--;
        throw StateError('simulated stop failure');
      }
    } finally {
      activeStops--;
    }
  }
}

VisualRecord speechRecord({
  required int sequence,
  required int center,
  required double rms,
  required double peak,
  int continuity = 1,
  int origin = 0,
}) => VisualRecord(
  streamId: 'stream-1',
  sourceIdentity: 'source-1',
  transformDigest: digest,
  profile: AnalysisProfile.speech,
  featureMask: 5,
  sequence: BigInt.from(sequence),
  centerFrame: BigInt.from(center),
  originFrame: BigInt.from(origin),
  continuityId: BigInt.from(continuity),
  validFrames: 480,
  physicalLevels: [peak, peak, peak, rms, rms, rms],
  visualLevels: [0, 0, peak / 8, 0, 0, rms / 8],
);

VisualRecord musicRecord({double band = .5, double level = .25}) =>
    VisualRecord(
      streamId: 'stream-1',
      sourceIdentity: 'source-1',
      transformDigest: digest,
      profile: AnalysisProfile.music,
      featureMask: 11,
      sequence: BigInt.one,
      centerFrame: BigInt.zero,
      originFrame: BigInt.zero,
      continuityId: BigInt.one,
      validFrames: 480,
      envelopeBins: 64,
      physicalLevels: List.filled(6, .2),
      visualLevels: List.filled(6, level),
      physicalEnvelope: List.filled(256, 0),
      visualEnvelope: List.filled(256, 0),
      physicalSpectrum: List.filled(1025, .1),
      visualBands: List.filled(64, band),
    );

void main() {
  test('SceneViewport accepts bounded logical dimensions and ratios', () {
    expect(SceneViewport(width: 1080, height: 1920).aspectRatio, .5625);
    expect(
      SceneViewport(width: 1920, height: 1080).aspectRatio,
      closeTo(1.7778, .001),
    );
    expect(SceneViewport(width: 5, height: 20).aspectRatio, .25);
    expect(SceneViewport(width: 20, height: 5).aspectRatio, 4);
    expect(() => SceneViewport(width: 8193, height: 1000), throwsArgumentError);
    expect(() => SceneViewport(width: 4.9, height: 20), throwsArgumentError);
    expect(() => SceneViewport(width: 20.1, height: 5), throwsArgumentError);
  });

  group('AnalysisController', () {
    final config = SceneConfig(
      mode: SceneMode.speechEnvelope,
      profile: AnalysisProfile.speech,
      featureMask: 5,
      transformDigest: digest,
      intensity: .65,
      motion: SceneMotion.full,
    );

    test('serializes polls and commits only a fully valid cursor', () async {
      final source = FakeAnalysisSource([
        batchResponse(count: 1, payload: encodedSpeechRecords([1])),
        batchResponse(count: 2, payload: encodedSpeechRecords([2, 4])),
      ]);
      source.gate = Completer<void>();
      final controller = AnalysisController(
        source: source,
        sceneConfig: config,
        transformConfig: const {},
        generation: 3,
      );
      await controller.start();
      final first = controller.poll();
      final overlap = controller.poll();
      expect(identical(first, overlap), isTrue);
      expect(source.polls, 1);
      source.gate!.complete();
      await first;
      expect(controller.nextSequenceCursor, BigInt.one);
      expect(controller.state.value.status, VisualizationStatus.live);
      expect(source.maximumActivePolls, 1);

      source.gate = null;
      await controller.poll();
      expect(controller.nextSequenceCursor, BigInt.one);
      expect(controller.retainedRecordCount, 0);
      expect(controller.state.value.status, VisualizationStatus.failed);
      expect(source.stops, 1);
      await controller.dispose();
    });

    test(
      'stop waits for the in-flight poll before releasing its lease',
      () async {
        final source = FakeAnalysisSource([
          batchResponse(count: 1, payload: encodedSpeechRecords([1])),
        ])..gate = Completer<void>();
        final controller = AnalysisController(
          source: source,
          sceneConfig: config,
          transformConfig: const {},
          generation: 3,
        );
        await controller.start();
        final poll = controller.poll();
        final stopping = controller.stop();
        await Future<void>.delayed(Duration.zero);
        expect(source.stops, 0);
        expect(controller.state.value.status, VisualizationStatus.off);

        source.gate!.complete();
        await poll;
        await stopping;
        expect(source.stops, 1);
        expect(controller.nextSequenceCursor, BigInt.zero);
        expect(controller.retainedRecordCount, 0);
        await controller.dispose();
      },
    );

    test('stop fences a lease that is still opening', () async {
      final source = FakeAnalysisSource([])..startGate = Completer<void>();
      final controller = AnalysisController(
        source: source,
        sceneConfig: config,
        transformConfig: const {},
        generation: 3,
      );
      final starting = controller.start();
      await Future<void>.delayed(Duration.zero);
      expect(source.starts, 1);

      final stopping = controller.stop();
      await Future<void>.delayed(Duration.zero);
      expect(source.stops, 0);
      source.startGate!.complete();
      await starting;
      await stopping;

      expect(source.stops, 1);
      expect(controller.state.value.status, VisualizationStatus.off);
      expect(controller.retainedRecordCount, 0);
      await controller.dispose();
    });

    test('stop retries a stale lease when its first release fails', () async {
      final source = FakeAnalysisSource([])
        ..startGate = Completer<void>()
        ..failedStops = 1;
      final controller = AnalysisController(
        source: source,
        sceneConfig: config,
        transformConfig: const {},
        generation: 3,
      );
      final starting = controller.start();
      await Future<void>.delayed(Duration.zero);
      final stopping = controller.stop();
      source.startGate!.complete();
      await starting;
      await stopping;

      expect(source.stops, 2);
      expect(controller.state.value.status, VisualizationStatus.off);
      await controller.dispose();
    });

    test('concurrent starts share one lease acquisition', () async {
      final source = FakeAnalysisSource([])..startGate = Completer<void>();
      final controller = AnalysisController(
        source: source,
        sceneConfig: config,
        transformConfig: const {},
        generation: 3,
      );
      final firstStart = controller.start();
      await Future<void>.delayed(Duration.zero);
      final secondStart = controller.start();
      expect(source.starts, 1);
      source.startGate!.complete();
      await Future.wait([firstStart, secondStart]);
      expect(source.starts, 1);
      expect(source.stops, 0);
      await controller.dispose();
    });

    test(
      'stop cancels a queued restart for a changed scene configuration',
      () async {
        final source = FakeAnalysisSource([])..startGate = Completer<void>();
        final controller = AnalysisController(
          source: source,
          sceneConfig: config,
          transformConfig: const {},
          generation: 3,
        );
        final starting = controller.start();
        await Future<void>.delayed(Duration.zero);
        final changing = controller.updateSceneConfig(
          SceneConfig(
            mode: SceneMode.musicOscilloscope,
            profile: AnalysisProfile.music,
            featureMask: 3,
            transformDigest: digest,
            intensity: .65,
            motion: SceneMotion.full,
          ),
        );
        final stopping = controller.stop();
        source.startGate!.complete();
        await Future.wait([starting, changing, stopping]);

        expect(source.starts, 1);
        expect(source.stops, 1);
        expect(controller.state.value.status, VisualizationStatus.off);
        await controller.dispose();
      },
    );

    test('concurrent stops share one native lease release', () async {
      final source = FakeAnalysisSource([]);
      final controller = AnalysisController(
        source: source,
        sceneConfig: config,
        transformConfig: const {},
        generation: 3,
      );
      await controller.start();
      source.stopGate = Completer<void>();
      final firstStop = controller.stop();
      await Future<void>.delayed(Duration.zero);
      final secondStop = controller.stop();
      await Future<void>.delayed(Duration.zero);
      expect(source.stops, 1);
      expect(source.maximumActiveStops, 1);
      source.stopGate!.complete();
      await Future.wait([firstStop, secondStop]);
      expect(source.stops, 1);
      await controller.dispose();
    });

    test(
      'stop waits for a mode-change release and stays visually off',
      () async {
        final source = FakeAnalysisSource([]);
        final controller = AnalysisController(
          source: source,
          sceneConfig: config,
          transformConfig: const {},
          generation: 3,
        );
        await controller.start();
        source.stopGate = Completer<void>();
        final update = controller.updateSceneConfig(
          SceneConfig(
            mode: SceneMode.speechEnvelope,
            profile: AnalysisProfile.speech,
            featureMask: 5,
            transformDigest: digest,
            intensity: .65,
            motion: SceneMotion.static,
          ),
        );
        await Future<void>.delayed(Duration.zero);
        expect(source.stops, 1);

        var stopped = false;
        final stopping = controller.stop().then((_) => stopped = true);
        await Future<void>.delayed(Duration.zero);
        expect(stopped, isFalse);
        source.stopGate!.complete();
        await Future.wait([update, stopping]);
        expect(source.stops, 1);
        expect(controller.state.value.status, VisualizationStatus.off);
        await controller.dispose();
      },
    );

    test(
      'retains a full 1.5 second speech window in compact history',
      () async {
        final responses = [
          for (var first = 1; first <= 192; first += 16)
            batchResponse(
              count: 16,
              firstSequence: first,
              payload: encodedSpeechRecords([
                for (var sequence = first; sequence < first + 16; sequence++)
                  sequence,
              ]),
            ),
        ];
        final source = FakeAnalysisSource(responses);
        final controller = AnalysisController(
          source: source,
          sceneConfig: config,
          transformConfig: const {},
          generation: 3,
        );
        await controller.start();
        for (var index = 0; index < 12; index++) {
          await controller.poll();
        }
        expect(controller.retainedRecordCount, speechHistoryCapacity);
        expect(controller.state.value.validSuffixCount, speechWindowSize);
        expect(controller.state.value.speechWave, hasLength(speechWindowSize));
        expect(
          controller.state.value.speechWave.every((bin) => bin.valid),
          isTrue,
        );
        await controller.dispose();
      },
    );

    test(
      'static motion freezes the current scene and releases its lease',
      () async {
        final source = FakeAnalysisSource([
          batchResponse(count: 1, payload: encodedSpeechRecords([1])),
          batchResponse(count: 1, payload: encodedSpeechRecords([1])),
        ]);
        final controller = AnalysisController(
          source: source,
          sceneConfig: config,
          transformConfig: const {},
          generation: 3,
        );
        await controller.start();
        await controller.poll();
        final liveWindow = controller.state.value.speechWave;
        expect(source.starts, 1);
        expect(controller.state.value.status, VisualizationStatus.live);

        await controller.updateSceneConfig(
          SceneConfig(
            mode: SceneMode.speechEnvelope,
            profile: AnalysisProfile.speech,
            featureMask: 5,
            transformDigest: digest,
            intensity: .65,
            motion: SceneMotion.static,
          ),
        );
        final frozenState = controller.state.value;
        expect(source.stops, 1);
        expect(frozenState.status, VisualizationStatus.static);
        expect(frozenState.label, startsWith('user_static'));
        expect(frozenState.frozenFeatureTimeSeconds, 0);
        expect(frozenState.speechWave, hasLength(liveWindow.length));
        expect(
          frozenState.speechWave.map((bin) => [bin.rms, bin.peak, bin.valid]),
          equals(liveWindow.map((bin) => [bin.rms, bin.peak, bin.valid])),
        );

        await controller.poll();
        expect(source.polls, 1);
        expect(
          identical(controller.state.value.speechWave, frozenState.speechWave),
          isTrue,
        );

        await controller.updateSceneConfig(
          SceneConfig(
            mode: SceneMode.speechEnvelope,
            profile: AnalysisProfile.speech,
            featureMask: 5,
            transformDigest: digest,
            intensity: .65,
            motion: SceneMotion.reduced,
          ),
        );
        expect(source.starts, 2);
        expect(controller.state.value.status, VisualizationStatus.warmingUp);
        await controller.poll();
        expect(source.polls, 2);
        expect(controller.state.value.status, VisualizationStatus.live);
        await controller.dispose();
      },
    );

    test(
      'static without a valid feature shows baseline without acquiring',
      () async {
        final source = FakeAnalysisSource([]);
        final controller = AnalysisController(
          source: source,
          sceneConfig: SceneConfig(
            mode: SceneMode.speechEnvelope,
            profile: AnalysisProfile.speech,
            featureMask: 5,
            transformDigest: digest,
            intensity: .65,
            motion: SceneMotion.static,
          ),
          transformConfig: const {},
          generation: 3,
        );
        await controller.start();
        expect(source.starts, 0);
        expect(controller.state.value.status, VisualizationStatus.unavailable);
        expect(controller.state.value.label, 'user_static_unavailable');
        expect(controller.state.value.baseline, isTrue);
        expect(controller.state.value.speechWave, hasLength(speechWindowSize));
        await controller.dispose();
      },
    );

    test(
      'static release failure is visible and retried without polling',
      () async {
        final source = FakeAnalysisSource([
          batchResponse(count: 1, payload: encodedSpeechRecords([1])),
        ])..failedStops = 1;
        final controller = AnalysisController(
          source: source,
          sceneConfig: config,
          transformConfig: const {},
          generation: 3,
        );
        await controller.start();
        await controller.poll();
        final frozenConfig = SceneConfig(
          mode: SceneMode.speechEnvelope,
          profile: AnalysisProfile.speech,
          featureMask: 5,
          transformDigest: digest,
          intensity: .65,
          motion: SceneMotion.static,
        );
        await controller.updateSceneConfig(frozenConfig);
        expect(source.stops, 1);
        expect(controller.state.value.status, VisualizationStatus.static);
        expect(
          controller.state.value.label,
          contains('lease_release_uncertain'),
        );
        await controller.poll();
        expect(source.polls, 1);

        await controller.updateSceneConfig(frozenConfig);
        expect(source.stops, 2);
        expect(
          controller.state.value.label,
          isNot(contains('lease_release_uncertain')),
        );
        await controller.dispose();
      },
    );

    test(
      'zero intensity stays on the fixed baseline without analysis lease',
      () async {
        final source = FakeAnalysisSource([]);
        final controller = AnalysisController(
          source: source,
          sceneConfig: SceneConfig(
            mode: SceneMode.speechEnvelope,
            profile: AnalysisProfile.speech,
            featureMask: 5,
            transformDigest: digest,
            intensity: 0,
            motion: SceneMotion.full,
          ),
          transformConfig: const {},
          generation: 3,
        );
        await controller.start();
        expect(source.starts, 0);
        expect(controller.state.value.label, 'intensity_zero');
        expect(controller.state.value.baseline, isTrue);

        await controller.updateSceneConfig(
          SceneConfig(
            mode: SceneMode.speechEnvelope,
            profile: AnalysisProfile.speech,
            featureMask: 5,
            transformDigest: digest,
            intensity: .65,
            motion: SceneMotion.full,
          ),
        );
        expect(source.starts, 1);
        expect(controller.state.value.status, VisualizationStatus.warmingUp);
        await controller.dispose();
      },
    );

    test('pause holds the last scene time and transformed record', () async {
      const musicLease = AnalysisLease(
        generation: 3,
        leaseId: 'lease-1',
        streamId: 'stream-1',
        sourceIdentity: 'source-1',
        profile: AnalysisProfile.music,
        featureMask: 11,
        transformDigest: digest,
      );
      final source = FakeAnalysisSource([
        musicBatchResponse(state: 'live', count: 1),
        musicBatchResponse(
          state: 'paused',
          count: 0,
          afterSequence: BigInt.one,
        ),
      ], lease: musicLease);
      SceneConfig configAt(double timeSeconds) => SceneConfig(
        mode: SceneMode.particles2d,
        profile: AnalysisProfile.music,
        featureMask: 11,
        transformDigest: digest,
        intensity: .65,
        motion: SceneMotion.full,
        particleCount: 8,
        timeSeconds: timeSeconds,
      );
      final controller = AnalysisController(
        source: source,
        sceneConfig: configAt(1),
        transformConfig: const {},
        generation: 3,
      );
      await controller.start();
      await controller.poll();
      expect(controller.state.value.particles, hasLength(8));
      await controller.poll();
      final paused = controller.state.value;
      expect(paused.status, VisualizationStatus.paused);
      expect(paused.label, startsWith('Audio visualization paused'));
      expect(paused.particles, hasLength(8));

      await controller.updateSceneConfig(configAt(5));
      expect(controller.state.value.status, VisualizationStatus.paused);
      expect(
        controller.state.value.particles
            .map(
              (particle) => [
                particle.x,
                particle.y,
                particle.radius,
                particle.opacity,
              ],
            )
            .toList(),
        paused.particles
            .map(
              (particle) => [
                particle.x,
                particle.y,
                particle.radius,
                particle.opacity,
              ],
            )
            .toList(),
      );
      await controller.dispose();
    });
  });

  group('AnalysisBatch', () {
    final context = AnalysisBatchContext(
      generation: 3,
      leaseId: 'lease-1',
      streamId: null,
      sourceIdentity: 'source-1',
      profile: AnalysisProfile.speech,
      featureMask: 5,
      transformDigest: digest,
    );

    test('accepts an honest empty warming-up response without advancing', () {
      final batch = AnalysisBatch.decode(
        response: batchResponse(),
        context: context,
        afterSequence: BigInt.zero,
      );
      expect(batch.records, isEmpty);
      expect(batch.nextSequenceCursor, BigInt.zero);
      expect(batch.state, 'warming_up');
    });

    test('decodes a complete contiguous batch atomically', () {
      final payload = Uint8List(176);
      final data = ByteData.sublistView(payload);
      for (var index = 0; index < 2; index++) {
        final offset = index * 88;
        data.setUint64(offset, index + 1, Endian.little);
        data.setUint64(offset + 8, index * 480, Endian.little);
        data.setUint64(offset + 16, 1, Endian.little);
        data.setUint32(offset + 24, 480, Endian.little);
      }
      final batch = AnalysisBatch.decode(
        response: batchResponse(count: 2, payload: payload),
        context: context,
        afterSequence: BigInt.zero,
      );
      expect(batch.records, hasLength(2));
      expect(batch.records.last.centerFrame, BigInt.from(480));
      expect(batch.nextSequenceCursor, BigInt.from(2));
    });

    test('rejects unknown keys and discontinuous record sequences', () {
      final extra = batchResponse()..['unexpected'] = true;
      expect(
        () => AnalysisBatch.decode(
          response: extra,
          context: context,
          afterSequence: BigInt.zero,
        ),
        throwsFormatException,
      );
      final payload = Uint8List(176);
      final data = ByteData.sublistView(payload);
      for (var index = 0; index < 2; index++) {
        final offset = index * 88;
        data.setUint64(offset, index == 0 ? 1 : 3, Endian.little);
        data.setUint64(offset + 8, index * 480, Endian.little);
        data.setUint64(offset + 16, 1, Endian.little);
        data.setUint32(offset + 24, 480, Endian.little);
      }
      expect(
        () => AnalysisBatch.decode(
          response: batchResponse(count: 2, payload: payload),
          context: context,
          afterSequence: BigInt.zero,
        ),
        throwsFormatException,
      );
    });
  });

  group('VisualRecordCodec', () {
    Uint8List recordBytes({
      required AnalysisProfile profile,
      required int mask,
    }) {
      final bytes = Uint8List(VisualRecordCodec.recordBytes(profile, mask));
      final data = ByteData.sublistView(bytes);
      data.setUint64(0, 1, Endian.little);
      data.setUint64(8, 0, Endian.little);
      data.setUint64(16, 1, Endian.little);
      data.setUint32(24, 480, Endian.little);
      data.setUint32(
        28,
        profile == AnalysisProfile.music ? 64 : 0,
        Endian.little,
      );
      return bytes;
    }

    test('uses compact profile and mask record sizes', () {
      expect(VisualRecordCodec.recordBytes(AnalysisProfile.music, 3), 2136);
      expect(VisualRecordCodec.recordBytes(AnalysisProfile.music, 11), 6492);
      expect(VisualRecordCodec.recordBytes(AnalysisProfile.speech, 5), 88);
      expect(VisualRecordCodec.recordBytes(AnalysisProfile.speech, 13), 4444);
    });

    test('transferred float32 store realigns an unaligned channel view', () {
      final envelope = Uint8List(89);
      final data = ByteData.sublistView(envelope);
      for (var index = 0; index < 6; index++) {
        data.setFloat32(1 + 40 + index * 4, .25, Endian.little);
        data.setFloat32(1 + 64 + index * 4, .5, Endian.little);
      }
      final channelView = Uint8List.view(envelope.buffer, 1, 88);
      expect(channelView.offsetInBytes % Float32List.bytesPerElement, isNot(0));

      final store = VisualRecordFloat32Store.transfer(channelView);
      expect(store.lengthInBytes, 88);
      expect(
        store
            .view(
              byteOffset: 40,
              length: 6,
              minimum: 0,
              maximum: 8,
              tolerance: 1e-6,
              field: 'physical levels',
            )
            .first,
        .25,
      );
    });

    test('decodes records without absent-feature placeholder arrays', () {
      final decoded = VisualRecordCodec.decode(
        bytes: recordBytes(profile: AnalysisProfile.speech, mask: 5),
        streamId: 'stream-1',
        sourceIdentity: 'source-1',
        transformDigest: digest,
        profile: AnalysisProfile.speech,
        featureMask: 5,
        originFrame: BigInt.zero,
      );
      expect(decoded.physicalEnvelope, isNull);
      expect(decoded.visualEnvelope, isNull);
      expect(decoded.physicalSpectrum, isNull);
      expect(decoded.visualBands, isNull);
      expect(decoded.validFrames, 480);
    });

    test(
      'decoded record views are immutable and independent of input bytes',
      () {
        final bytes = recordBytes(profile: AnalysisProfile.speech, mask: 5);
        ByteData.sublistView(bytes).setFloat32(40, .25, Endian.little);
        ByteData.sublistView(bytes).setFloat32(48, .25, Endian.little);
        final decoded = VisualRecordCodec.decode(
          bytes: bytes,
          streamId: 'stream-1',
          sourceIdentity: 'source-1',
          transformDigest: digest,
          profile: AnalysisProfile.speech,
          featureMask: 5,
          originFrame: BigInt.zero,
        );
        bytes[40] = 0x00;
        expect(decoded.physicalLevels.first, .25);
        expect(() => decoded.visualLevels[0] = 1, throwsUnsupportedError);
      },
    );

    test('rejects reserved fields, invalid mask pairs, and truncation', () {
      final bytes = recordBytes(profile: AnalysisProfile.speech, mask: 5);
      ByteData.sublistView(bytes).setUint32(36, 1, Endian.little);
      expect(
        () => VisualRecordCodec.decode(
          bytes: bytes,
          streamId: 'stream-1',
          sourceIdentity: 'source-1',
          transformDigest: digest,
          profile: AnalysisProfile.speech,
          featureMask: 5,
          originFrame: BigInt.zero,
        ),
        throwsFormatException,
      );
      expect(
        () => VisualRecordCodec.recordBytes(AnalysisProfile.music, 5),
        throwsFormatException,
      );
      expect(
        () => VisualRecordCodec.decode(
          bytes: Uint8List(87),
          streamId: 'stream-1',
          sourceIdentity: 'source-1',
          transformDigest: digest,
          profile: AnalysisProfile.speech,
          featureMask: 5,
          originFrame: BigInt.zero,
        ),
        throwsFormatException,
      );
    });
  });

  group('SpeechWindow', () {
    test('keeps only the contiguous retained suffix', () {
      final records = [
        speechRecord(sequence: 1, center: 0, rms: .8, peak: 1.6),
        speechRecord(sequence: 3, center: 960, rms: 2.4, peak: 3.2),
      ];
      final window = SpeechWindow.fromHistory(
        records,
        endpointCenterFrame: BigInt.from(960),
      );

      expect(window.validSuffixCount, 1);
      expect(window.physicalRms.take(149), everyElement(0));
      expect(window.physicalRms.last, closeTo(2.4, 1e-6));
      expect(window.visualPeak.last, closeTo(.4, 1e-6));
    });

    test('fractional interpolation uses only the common suffix', () {
      final left = SpeechWindow.fromHistory([
        speechRecord(sequence: 1, center: 0, rms: .8, peak: 1.6),
      ], endpointCenterFrame: BigInt.zero);
      final right = SpeechWindow.fromHistory([
        speechRecord(sequence: 1, center: 0, rms: .8, peak: 1.6),
        speechRecord(sequence: 2, center: 480, rms: 2.4, peak: 3.2),
      ], endpointCenterFrame: BigInt.from(480));

      final middle = left.interpolateWith(right, .5);
      expect(middle.validSuffixCount, 1);
      expect(middle.physicalRms.take(149), everyElement(0));
      expect(middle.physicalRms.last, closeTo(1.6, 1e-6));
      expect(identical(left.interpolateWith(right, 0), left), isTrue);
      expect(identical(left.interpolateWith(right, 1), right), isTrue);
    });

    test('does not combine windows across a PCM continuity reset', () {
      final left = SpeechWindow.fromHistory([
        speechRecord(sequence: 8, center: 480, rms: 1, peak: 2),
      ], endpointCenterFrame: BigInt.from(480));
      final right = SpeechWindow.fromHistory([
        speechRecord(sequence: 9, center: 0, rms: 1, peak: 2, continuity: 2),
      ], endpointCenterFrame: BigInt.zero);

      expect(() => left.interpolateWith(right, .5), throwsArgumentError);
    });
  });

  group('SceneEvaluator', () {
    final evaluator = const SceneEvaluator();

    test('SceneConfig rejects unsupported profile and feature-mask pairs', () {
      SceneConfig config({
        required SceneMode mode,
        required AnalysisProfile profile,
        required int featureMask,
        ParticleEnergySource energySource = ParticleEnergySource.levels,
      }) => SceneConfig(
        mode: mode,
        profile: profile,
        featureMask: featureMask,
        transformDigest: digest,
        intensity: .65,
        motion: SceneMotion.full,
        energySource: energySource,
      );

      expect(
        () => config(
          mode: SceneMode.musicOscilloscope,
          profile: AnalysisProfile.music,
          featureMask: 5,
        ),
        throwsArgumentError,
      );
      expect(
        () => config(
          mode: SceneMode.speechEnvelope,
          profile: AnalysisProfile.speech,
          featureMask: 3,
        ),
        throwsArgumentError,
      );
      expect(
        () => config(
          mode: SceneMode.speechEnvelope,
          profile: AnalysisProfile.speech,
          featureMask: 15,
        ),
        throwsArgumentError,
      );
      expect(
        () => config(
          mode: SceneMode.spectrumBars,
          profile: AnalysisProfile.music,
          featureMask: 3,
        ),
        throwsArgumentError,
      );
      expect(
        () => config(
          mode: SceneMode.particles2d,
          profile: AnalysisProfile.music,
          featureMask: 3,
          energySource: ParticleEnergySource.spectrum,
        ),
        throwsArgumentError,
      );

      expect(
        () => config(
          mode: SceneMode.musicOscilloscope,
          profile: AnalysisProfile.music,
          featureMask: 3,
        ),
        returnsNormally,
      );
      expect(
        () => config(
          mode: SceneMode.speechEnvelope,
          profile: AnalysisProfile.speech,
          featureMask: 13,
        ),
        returnsNormally,
      );
    });

    test('spectrum bars consume native transformed bands directly', () {
      final record = VisualRecord(
        streamId: 'stream-1',
        sourceIdentity: 'source-1',
        transformDigest: digest,
        profile: AnalysisProfile.music,
        featureMask: 11,
        sequence: BigInt.one,
        centerFrame: BigInt.zero,
        originFrame: BigInt.zero,
        continuityId: BigInt.one,
        validFrames: 480,
        envelopeBins: 64,
        physicalLevels: List.filled(6, .2),
        visualLevels: List.filled(6, .25),
        physicalEnvelope: List.filled(256, .5),
        visualEnvelope: List.filled(256, .5),
        physicalSpectrum: List.filled(1025, .1),
        visualBands: List.generate(64, (index) => index / 64),
      );
      final state = evaluator.evaluate(
        config: SceneConfig(
          mode: SceneMode.spectrumBars,
          profile: AnalysisProfile.music,
          featureMask: 11,
          transformDigest: digest,
          intensity: .5,
          motion: SceneMotion.reduced,
        ),
        status: VisualizationStatus.live,
        record: record,
      );

      expect(state.bars, hasLength(64));
      expect(state.bars.first.height, 0);
      expect(state.bars.last.height, closeTo(63 / 128, 1e-12));
      expect(state.bars[0].role, SceneColorRole.primary);
      expect(state.bars[1].role, SceneColorRole.secondary);
      expect(state.bars[2].role, SceneColorRole.accent);
    });

    test('zero intensity draws a fixed baseline independent of history', () {
      final window = SpeechWindow.fromHistory([
        speechRecord(sequence: 1, center: 0, rms: .8, peak: 1.6),
      ], endpointCenterFrame: BigInt.zero);
      final record = speechRecord(sequence: 1, center: 0, rms: .8, peak: 1.6);
      final state = evaluator.evaluate(
        config: SceneConfig(
          mode: SceneMode.speechEnvelope,
          profile: AnalysisProfile.speech,
          featureMask: 5,
          transformDigest: digest,
          intensity: 0,
          motion: SceneMotion.full,
        ),
        status: VisualizationStatus.live,
        record: record,
        speechWindow: window,
      );

      expect(state.baseline, isTrue);
      expect(state.validSuffixCount, 1);
      expect(state.speechWave, hasLength(150));
      expect(state.speechWave.every((bin) => !bin.valid), isTrue);
      expect(
        state.speechWave.every((bin) => bin.rms == 0 && bin.peak == 0),
        isTrue,
      );
    });

    test('zero intensity baseline does not wait for audio features', () {
      final state = evaluator.evaluate(
        config: SceneConfig(
          mode: SceneMode.spectrumBars,
          profile: AnalysisProfile.music,
          featureMask: 11,
          transformDigest: digest,
          intensity: 0,
          motion: SceneMotion.full,
        ),
        status: VisualizationStatus.warmingUp,
      );
      expect(state.status, VisualizationStatus.warmingUp);
      expect(state.label, 'intensity_zero');
      expect(state.baseline, isTrue);
      expect(state.bars, hasLength(64));
      expect(state.bars.every((bar) => bar.height == 0), isTrue);
    });

    test('mismatched feature profile is unavailable, never drawn as zero', () {
      final record = speechRecord(sequence: 1, center: 0, rms: 0, peak: 0);
      final state = evaluator.evaluate(
        config: SceneConfig(
          mode: SceneMode.spectrumBars,
          profile: AnalysisProfile.music,
          featureMask: 11,
          transformDigest: digest,
          intensity: .65,
          motion: SceneMotion.full,
        ),
        status: VisualizationStatus.live,
        record: record,
      );
      expect(state.status, VisualizationStatus.unavailable);
      expect(state.bars, isEmpty);
    });

    test('records from another transform cannot enter this scene', () {
      final record = VisualRecord(
        streamId: 'stream-1',
        sourceIdentity: 'source-1',
        transformDigest:
            'abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789',
        profile: AnalysisProfile.music,
        featureMask: 11,
        sequence: BigInt.one,
        centerFrame: BigInt.zero,
        originFrame: BigInt.zero,
        continuityId: BigInt.one,
        validFrames: 480,
        envelopeBins: 64,
        physicalLevels: List.filled(6, .2),
        visualLevels: List.filled(6, .25),
        physicalEnvelope: List.filled(256, .5),
        visualEnvelope: List.filled(256, .5),
        physicalSpectrum: List.filled(1025, .1),
        visualBands: List.generate(64, (index) => index / 64),
      );
      final state = evaluator.evaluate(
        config: SceneConfig(
          mode: SceneMode.spectrumBars,
          profile: AnalysisProfile.music,
          featureMask: 11,
          transformDigest: digest,
          intensity: .65,
          motion: SceneMotion.full,
        ),
        status: VisualizationStatus.live,
        record: record,
      );

      expect(state.status, VisualizationStatus.unavailable);
      expect(state.bars, isEmpty);
    });

    test('radial geometry uses the shared level and spectrum transforms', () {
      final record = musicRecord();
      final state = evaluator.evaluate(
        config: SceneConfig(
          mode: SceneMode.immersiveRadial,
          profile: AnalysisProfile.music,
          featureMask: 11,
          transformDigest: digest,
          intensity: .65,
          motion: SceneMotion.full,
          timeSeconds: 1,
        ),
        status: VisualizationStatus.live,
        record: record,
      );
      expect(state.radialSpokes, hasLength(64));
      expect(state.radialRings, hasLength(3));
      expect(state.radialSpokes.first.outerRadius, closeTo(.2385, 1e-12));
      expect(state.radialSpokes.first.opacity, closeTo(.49375, 1e-12));
      expect(state.radialRings.first.radius, closeTo(.1040625, 1e-12));
      expect(state.radialFieldStrength, closeTo(.1325, 1e-12));
    });

    test(
      'particle scenes are deterministic across arbitrary evaluation order',
      () {
        final record = musicRecord();
        final config = SceneConfig(
          mode: SceneMode.particles2d,
          profile: AnalysisProfile.music,
          featureMask: 11,
          transformDigest: digest,
          intensity: .65,
          motion: SceneMotion.full,
          seed: 42,
          particleCount: 128,
          timeSeconds: 7.25,
        );
        final atSeven = evaluator.evaluate(
          config: config,
          status: VisualizationStatus.live,
          record: record,
        );
        evaluator.evaluate(
          config: SceneConfig(
            mode: SceneMode.particles2d,
            profile: AnalysisProfile.music,
            featureMask: 11,
            transformDigest: digest,
            intensity: .65,
            motion: SceneMotion.full,
            seed: 42,
            particleCount: 128,
            timeSeconds: 1,
          ),
          status: VisualizationStatus.live,
          record: record,
        );
        final repeated = evaluator.evaluate(
          config: config,
          status: VisualizationStatus.live,
          record: record,
        );
        expect(
          repeated.particles
              .map((p) => [p.x, p.y, p.radius, p.opacity])
              .toList(),
          atSeven.particles
              .map((p) => [p.x, p.y, p.radius, p.opacity])
              .toList(),
        );
        expect(atSeven.particles, hasLength(128));
        expect(atSeven.particles.first.x, inInclusiveRange(0, 1));
        expect(atSeven.particles.first.opacity, inInclusiveRange(0, 1));
      },
    );

    test('particle coordinates follow the specified uint32 hash vectors', () {
      final state = evaluator.evaluate(
        config: SceneConfig(
          mode: SceneMode.particles2d,
          profile: AnalysisProfile.music,
          featureMask: 11,
          transformDigest: digest,
          intensity: .65,
          motion: SceneMotion.reduced,
          seed: 0,
          particleCount: 1,
        ),
        status: VisualizationStatus.live,
        record: musicRecord(),
      );

      expect(state.particles.single.x, closeTo(.8475585277192295, 1e-15));
      expect(state.particles.single.y, closeTo(.1974032109137625, 1e-15));
    });
  });

  test(
    'VisualRecord interpolation recomputes combined physical peak and RMS',
    () {
      VisualRecord record({
        required int sequence,
        required int center,
        required List<double> levels,
      }) => VisualRecord(
        streamId: 'stream-1',
        sourceIdentity: 'source-1',
        transformDigest: digest,
        profile: AnalysisProfile.music,
        featureMask: 11,
        sequence: BigInt.from(sequence),
        centerFrame: BigInt.from(center),
        originFrame: BigInt.zero,
        continuityId: BigInt.one,
        validFrames: 480,
        envelopeBins: 64,
        physicalLevels: levels,
        visualLevels: List.filled(6, .4),
        physicalEnvelope: List.filled(256, 0),
        visualEnvelope: List.filled(256, 0),
        physicalSpectrum: List.filled(1025, .1),
        visualBands: List.filled(64, .3),
      );
      final left = record(
        sequence: 1,
        center: 0,
        levels: [1.6, 0, 1.6, 1.2, .4, math.sqrt(.8)],
      );
      final right = record(
        sequence: 2,
        center: 480,
        levels: [0, 1.6, 1.6, .4, 1.2, math.sqrt(.8)],
      );
      final middle = left.interpolateWith(right, .5);
      expect(middle.physicalLevels[0], .8);
      expect(middle.physicalLevels[1], .8);
      expect(middle.physicalLevels[2], .8);
      expect(middle.physicalLevels[5], .8);
    },
  );

  testWidgets('shared painter renders radial and particle states', (
    tester,
  ) async {
    final scene = ValueNotifier(
      SceneState(
        status: VisualizationStatus.live,
        label: 'Immersive radial audio scene',
        intensity: .65,
        validSuffixCount: 0,
        radialSpokes: const [
          RadialSpoke(0, 0, .18, .3, .5, SceneColorRole.primary),
        ],
        radialRings: const [RadialRing(0, .1, SceneColorRole.secondary)],
      ),
    );
    final palette = ScenePalette(
      background: const SceneColor(.02, .03, .04),
      surface: const SceneColor(.1, .1, .1),
      primary: const SceneColor(.2, .7, .8),
      secondary: const SceneColor(.9, .3, .5),
      accent: const SceneColor(.8, .6, .2),
      foreground: const SceneColor(1, 1, 1),
      geometry: SceneGeometry(
        paddingPx: 8,
        strokeWidthPx: 2,
        barGapFraction: .2,
        barCornerRadiusPx: 4,
        particleRadiusScale: 1,
      ),
    );
    await tester.pumpWidget(
      Directionality(
        textDirection: TextDirection.ltr,
        child: SizedBox(
          width: 320,
          height: 180,
          child: SharedSceneView(state: scene, palette: palette),
        ),
      ),
    );
    expect(find.byType(CustomPaint), findsOneWidget);
    expect(tester.takeException(), isNull);

    scene.value = SceneState(
      status: VisualizationStatus.live,
      label: 'Procedural audio particles',
      intensity: .65,
      validSuffixCount: 0,
      particles: const [
        ParticleState(0, .5, .5, .005, .7, SceneColorRole.accent),
      ],
    );
    await tester.pump();
    expect(tester.takeException(), isNull);

    scene.value = SceneState(
      status: VisualizationStatus.paused,
      label: 'Audio visualization paused · Procedural audio particles',
      intensity: .65,
      validSuffixCount: 0,
      particles: const [
        ParticleState(0, .5, .5, .005, .7, SceneColorRole.accent),
      ],
    );
    await tester.pump();
    expect(tester.takeException(), isNull);
    scene.dispose();
  });

  testWidgets('shared painter renders Stage A bars and waveform states', (
    tester,
  ) async {
    final scene = ValueNotifier(
      SceneState(
        status: VisualizationStatus.live,
        label: 'Audio spectrum bars',
        intensity: 1,
        validSuffixCount: 0,
        bars: const [
          SceneBar(0, 1 / 6, .75, SceneColorRole.primary),
          SceneBar(1, .5, .5, SceneColorRole.secondary),
          SceneBar(2, 5 / 6, .25, SceneColorRole.accent),
        ],
      ),
    );
    final palette = ScenePalette(
      background: const SceneColor(0, 0, 0),
      surface: const SceneColor(1, 1, 1),
      primary: const SceneColor(1, 0, 0),
      secondary: const SceneColor(0, 1, 0),
      accent: const SceneColor(0, 0, 1),
      foreground: const SceneColor(1, 1, 1),
      geometry: SceneGeometry(
        paddingPx: 0,
        strokeWidthPx: 2,
        barGapFraction: .2,
        barCornerRadiusPx: 0,
        particleRadiusScale: 1,
      ),
    );
    await tester.pumpWidget(
      Directionality(
        textDirection: TextDirection.ltr,
        child: SizedBox(
          width: 120,
          height: 100,
          child: SharedSceneView(state: scene, palette: palette),
        ),
      ),
    );
    expect(tester.takeException(), isNull);

    scene.value = SceneState(
      status: VisualizationStatus.live,
      label: 'Music waveform',
      intensity: 1,
      validSuffixCount: 0,
      musicWave: const [MusicWaveSpan(0, .5, .2, .3, .6, .7)],
    );
    await tester.pump();
    expect(tester.takeException(), isNull);

    scene.value = SceneState(
      status: VisualizationStatus.static,
      label: 'user_static · Music waveform',
      intensity: 1,
      validSuffixCount: 0,
      musicWave: const [MusicWaveSpan(0, .5, .2, .3, .6, .7)],
      frozenFeatureTimeSeconds: 1.25,
    );
    await tester.pump();
    expect(tester.takeException(), isNull);

    scene.value = SceneState(
      status: VisualizationStatus.live,
      label: 'Speech envelope',
      intensity: 1,
      validSuffixCount: 1,
      speechWave: const [SpeechWaveBin(0, .5, .2, .4, true)],
    );
    await tester.pump();
    expect(tester.takeException(), isNull);
    scene.dispose();
  });

  testWidgets('unsupported viewport is labeled and not painted', (
    tester,
  ) async {
    final semantics = tester.ensureSemantics();
    tester.view.physicalSize = const Size(9000, 180);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.resetPhysicalSize);
    addTearDown(tester.view.resetDevicePixelRatio);
    final scene = ValueNotifier(
      SceneState(
        status: VisualizationStatus.live,
        label: 'Audio spectrum bars',
        intensity: 1,
        validSuffixCount: 0,
        bars: const [SceneBar(0, .5, .5, SceneColorRole.primary)],
      ),
    );
    final palette = ScenePalette(
      background: const SceneColor(0, 0, 0),
      surface: const SceneColor(1, 1, 1),
      primary: const SceneColor(1, 0, 0),
      secondary: const SceneColor(0, 1, 0),
      accent: const SceneColor(0, 0, 1),
      foreground: const SceneColor(1, 1, 1),
      geometry: SceneGeometry(
        paddingPx: 8,
        strokeWidthPx: 2,
        barGapFraction: .2,
        barCornerRadiusPx: 4,
        particleRadiusScale: 1,
      ),
    );
    await tester.pumpWidget(
      Directionality(
        textDirection: TextDirection.ltr,
        child: SharedSceneView(state: scene, palette: palette),
      ),
    );
    expect(
      find.bySemanticsLabel(
        'Audio visualization unavailable: unsupported viewport',
      ),
      findsOneWidget,
    );
    expect(tester.takeException(), isNull);
    scene.dispose();
    semantics.dispose();
  });

  testWidgets('shared view rebinds when its scene notifier is replaced', (
    tester,
  ) async {
    final first = ValueNotifier<SceneState>(
      SceneState(
        status: VisualizationStatus.live,
        label: 'First scene',
        intensity: 0,
        validSuffixCount: 0,
        baseline: true,
      ),
    );
    final second = ValueNotifier<SceneState>(
      SceneState(
        status: VisualizationStatus.live,
        label: 'Second scene',
        intensity: 0,
        validSuffixCount: 0,
        baseline: true,
      ),
    );
    final palette = ScenePalette(
      background: const SceneColor(0, 0, 0),
      surface: const SceneColor(1, 1, 1),
      primary: const SceneColor(1, 0, 0),
      secondary: const SceneColor(0, 1, 0),
      accent: const SceneColor(0, 0, 1),
      foreground: const SceneColor(1, 1, 1),
      geometry: SceneGeometry(
        paddingPx: 0,
        strokeWidthPx: 2,
        barGapFraction: .2,
        barCornerRadiusPx: 0,
        particleRadiusScale: 1,
      ),
    );
    await tester.pumpWidget(
      Directionality(
        textDirection: TextDirection.ltr,
        child: SizedBox(
          width: 320,
          height: 180,
          child: SharedSceneView(state: first, palette: palette),
        ),
      ),
    );
    expect(find.bySemanticsLabel('First scene'), findsOneWidget);

    await tester.pumpWidget(
      Directionality(
        textDirection: TextDirection.ltr,
        child: SizedBox(
          width: 320,
          height: 180,
          child: SharedSceneView(state: second, palette: palette),
        ),
      ),
    );
    expect(find.bySemanticsLabel('Second scene'), findsOneWidget);

    first.value = SceneState(
      status: VisualizationStatus.failed,
      label: 'Detached scene changed',
      intensity: 0,
      validSuffixCount: 0,
      baseline: true,
    );
    await tester.pump();
    expect(find.bySemanticsLabel('Second scene'), findsOneWidget);
    expect(find.bySemanticsLabel('Detached scene changed'), findsNothing);

    second.value = SceneState(
      status: VisualizationStatus.paused,
      label: 'Replacement scene changed',
      intensity: 0,
      validSuffixCount: 0,
      baseline: true,
    );
    await tester.pump();
    expect(find.bySemanticsLabel('Replacement scene changed'), findsOneWidget);
    await tester.pumpWidget(const SizedBox.shrink());
    first.dispose();
    second.dispose();
  });

  testWidgets('unbounded viewport stays layout-safe and reports unavailable', (
    tester,
  ) async {
    final semantics = tester.ensureSemantics();
    final scene = ValueNotifier<SceneState>(
      SceneState(
        status: VisualizationStatus.live,
        label: 'Audio spectrum bars',
        intensity: 1,
        validSuffixCount: 0,
        bars: const [SceneBar(0, .5, .5, SceneColorRole.primary)],
      ),
    );
    final palette = ScenePalette(
      background: const SceneColor(0, 0, 0),
      surface: const SceneColor(1, 1, 1),
      primary: const SceneColor(1, 0, 0),
      secondary: const SceneColor(0, 1, 0),
      accent: const SceneColor(0, 0, 1),
      foreground: const SceneColor(1, 1, 1),
      geometry: SceneGeometry(
        paddingPx: 0,
        strokeWidthPx: 2,
        barGapFraction: .2,
        barCornerRadiusPx: 0,
        particleRadiusScale: 1,
      ),
    );
    await tester.pumpWidget(
      Directionality(
        textDirection: TextDirection.ltr,
        child: SingleChildScrollView(
          child: SizedBox(
            width: 320,
            child: SharedSceneView(state: scene, palette: palette),
          ),
        ),
      ),
    );
    expect(
      find.bySemanticsLabel(
        'Audio visualization unavailable: unsupported viewport',
      ),
      findsOneWidget,
    );
    expect(tester.takeException(), isNull);
    scene.dispose();
    semantics.dispose();
  });
}
