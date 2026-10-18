import 'dart:async';
import 'dart:typed_data';

import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shipglows_audio/shipglows_audio.dart';
import 'package:shipglows_audio/shipglows_audio_platform_interface.dart';

class FakePlatform extends ShipglowsAudioPlatform {
  final calls = <String>[];
  final noteOns =
      <({int generation, int midi, double velocity, String? instrument})>[];
  final analysisStarts = <Map<String, Object?>>[];
  final batchRequests = <int>[];
  final stoppedLeases = <String>[];
  final batches = <Map<Object?, Object?>>[];
  Completer<void>? holdBatch;
  bool acceptCommands = true;
  Object? commandError;

  ShipglowsSynthStatus _status(int generation, String state) =>
      ShipglowsSynthStatus(generation: generation, state: state);

  @override
  Future<ShipglowsSynthStatus> startSynth({
    required int generation,
    required ShipglowsSynthInstrument instrument,
  }) async {
    calls.add('startSynth:$generation:${instrument.wireName}');
    return _status(generation, 'running');
  }

  Future<bool> _command(String name) async {
    calls.add(name);
    final error = commandError;
    if (error != null) throw error;
    return acceptCommands;
  }

  @override
  Future<bool> synthNoteOn({
    required int generation,
    required int midi,
    required double velocity,
    ShipglowsSynthInstrument? instrument,
  }) {
    noteOns.add((
      generation: generation,
      midi: midi,
      velocity: velocity,
      instrument: instrument?.wireName,
    ));
    return _command('noteOn');
  }

  @override
  Future<bool> synthNoteOff({
    required int generation,
    required int midi,
    ShipglowsSynthInstrument? instrument,
  }) => _command('noteOff:$midi');

  @override
  Future<bool> synthClick({required int generation, required bool accent}) =>
      _command('click:$accent');

  @override
  Future<bool> synthSetVoice({
    required int generation,
    required ShipglowsSynthInstrument instrument,
  }) => _command('setVoice:${instrument.wireName}');

  @override
  Future<ShipglowsSynthStatus> getSynthStatus({
    required int generation,
  }) async => _status(generation, 'running');

  @override
  Future<ShipglowsSynthStatus> stopSynth({required int generation}) async {
    calls.add('stopSynth:$generation');
    return _status(generation, 'stopped');
  }

  @override
  Future<Map<Object?, Object?>> startAnalysis({
    required int generation,
    required String source,
    required String profile,
    required int featureMask,
    required Map<String, Object?> transformConfig,
  }) async {
    analysisStarts.add({
      'generation': generation,
      'source': source,
      'profile': profile,
      'featureMask': featureMask,
      'transformConfig': transformConfig,
    });
    return <Object?, Object?>{
      'analysis_api_version': 2,
      'generation': generation,
      'lease_id': 'lease-1',
      'stream_id': 'stream-1',
      'source_identity': 'windows-output-mix-v1-$generation',
      'profile': profile,
      'feature_mask': featureMask,
      'transform_digest': 'digest',
      'max_block_frames': 16384,
      'max_batch_records': 16,
      'max_response_bytes': 131072,
    };
  }

  @override
  Future<Map<Object?, Object?>> getAnalysisBatch({
    required int generation,
    required String leaseId,
    required int afterSequence,
  }) async {
    batchRequests.add(afterSequence);
    final hold = holdBatch;
    if (hold != null) await hold.future;
    return batches.removeAt(0);
  }

  @override
  Future<void> stopAnalysis({
    required int generation,
    required String leaseId,
  }) async {
    stoppedLeases.add(leaseId);
  }
}

/// Encodes records exactly like the native `encode_visual_analysis_record`.
Uint8List encodeRecords(
  int mask,
  List<({int sequence, int center, double peak})> records,
) {
  final music = mask & 2 != 0;
  final spectrum = mask & 8 != 0;
  final size = 40 + 48 + (music ? 2048 : 0) + (spectrum ? 4356 : 0);
  final bytes = ByteData(size * records.length);
  var at = 0;
  void f32(double value) {
    bytes.setFloat32(at, value, Endian.little);
    at += 4;
  }

  for (final record in records) {
    bytes.setUint64(at, record.sequence, Endian.little);
    bytes.setUint64(at + 8, record.center, Endian.little);
    bytes.setUint64(at + 16, 1, Endian.little);
    bytes.setUint32(at + 24, 480, Endian.little);
    bytes.setUint32(at + 28, music ? 64 : 0, Endian.little);
    bytes.setUint32(at + 32, 0, Endian.little);
    bytes.setUint32(at + 36, 0, Endian.little);
    at += 40;
    for (var i = 0; i < 6; i++) {
      f32(i == 2 ? record.peak : 0.1);
    }
    for (var i = 0; i < 6; i++) {
      f32(0.5);
    }
    if (music) {
      for (var i = 0; i < 512; i++) {
        f32(0);
      }
    }
    if (spectrum) {
      for (var i = 0; i < 1025; i++) {
        f32(0);
      }
      for (var i = 0; i < 64; i++) {
        f32(i / 64);
      }
    }
  }
  return bytes.buffer.asUint8List();
}

Map<Object?, Object?> batch({
  required int generation,
  required int after,
  required List<int> sequences,
  int mask = 11,
  bool resync = false,
  String reason = 'none',
  Uint8List? payload,
}) {
  final records = [
    for (final sequence in sequences)
      (sequence: sequence, center: (sequence - 1) * 480, peak: 0.25),
  ];
  return <Object?, Object?>{
    'analysis_api_version': 2,
    'generation': generation,
    'lease_id': 'lease-1',
    'stream_id': 'stream-1',
    'source_identity': 'windows-output-mix-v1-$generation',
    'profile': 'sgav-music-1',
    'feature_mask': mask,
    'transform_digest': 'digest',
    'state': 'live',
    'continuity_id': 1,
    'run_origin_frame': sequences.isEmpty ? null : 0,
    'record_layout': 'sgav-record-2-compact-speech-1',
    'first_sequence': sequences.isEmpty ? 0 : sequences.first,
    'last_sequence': sequences.isEmpty ? 0 : sequences.last,
    'next_sequence_cursor': sequences.isEmpty ? after : sequences.last,
    'delivery_resync': resync,
    'resync_reason': reason,
    'skipped_delivery_records': sequences.isEmpty
        ? 0
        : sequences.first - after - 1,
    'pcm_gap': false,
    'dropped_pcm_frames': 0,
    'native_observation_ns': 123,
    'timing': null,
    'record_bytes': mask == 11 ? 6492 : 2136,
    'record_count': sequences.length,
    'records': payload ?? encodeRecords(mask, records),
  };
}

void main() {
  group('ShipglowsSynth', () {
    test('drops commands without an open session', () {
      final platform = FakePlatform();
      final synth = ShipglowsSynth(platform: platform);
      synth.noteOn(60);
      synth.noteOff(60);
      synth.click(accent: true);
      synth.setVoice(ShipglowsSynthInstrument.drums);
      expect(synth.droppedCommands, 4);
      expect(platform.calls, isEmpty);
    });

    test('sends fenced commands without waiting for the platform', () async {
      final platform = FakePlatform();
      final synth = ShipglowsSynth(platform: platform);
      final status = await synth.start(
        instrument: ShipglowsSynthInstrument.drums,
      );
      final generation = synth.generation!;
      expect(status.state, 'running');
      expect(platform.calls.single, 'startSynth:$generation:drums');

      synth.noteOn(36, velocity: 1.7);
      synth.noteOn(
        64,
        velocity: 0.5,
        instrument: ShipglowsSynthInstrument.piano,
      );
      synth.noteOff(64);
      synth.click(accent: false);
      synth.setVoice(ShipglowsSynthInstrument.piano);
      expect(() => synth.noteOn(128), throwsRangeError);
      await pumpEventQueue();

      expect(
        platform.noteOns.first.velocity,
        1.0,
        reason: 'velocity is clamped',
      );
      expect(platform.noteOns.first.generation, generation);
      expect(platform.noteOns.last.instrument, 'piano');
      expect(platform.calls.skip(1), [
        'noteOn',
        'noteOn',
        'noteOff:64',
        'click:false',
        'setVoice:piano',
      ]);
    });

    test('counts rejected and failed commands without throwing', () async {
      final platform = FakePlatform();
      final synth = ShipglowsSynth(platform: platform);
      await synth.start();
      platform.acceptCommands = false;
      synth.noteOn(60);
      await pumpEventQueue();
      platform.commandError = PlatformException(code: 'stale_synth_generation');
      synth.click(accent: true);
      await pumpEventQueue();
      expect(synth.rejectedCommands, 1);
      expect(synth.failedCommands, 1);
    });

    test('each session gets a newer generation', () async {
      final platform = FakePlatform();
      final synth = ShipglowsSynth(platform: platform);
      await synth.start();
      final first = synth.generation!;
      final stopped = await synth.stop();
      expect(stopped?.state, 'stopped');
      expect(synth.isOpen, isFalse);
      expect(await synth.stop(), isNull);
      await synth.start();
      expect(synth.generation!, greaterThan(first));
      expect(ShipglowsSynth(platform: platform).generation, isNull);
    });

    test('opens a mix analysis fenced by the synth generation', () async {
      final platform = FakePlatform();
      final synth = ShipglowsSynth(platform: platform);
      await expectLater(synth.startAnalysis(), throwsStateError);
      await synth.start();
      final analysis = await synth.startAnalysis();
      expect(analysis.isActive, isTrue);
      expect(platform.analysisStarts.single['source'], 'windows_output_mix_v1');
      expect(platform.analysisStarts.single['generation'], synth.generation);
      expect(platform.analysisStarts.single['featureMask'], 11);
      expect(
        (platform.analysisStarts.single['transformConfig']! as Map)['version'],
        1,
      );
    });
  });

  group('ShipglowsOutputAnalysis', () {
    Future<(FakePlatform, ShipglowsOutputAnalysis)> started() async {
      final platform = FakePlatform();
      final analysis = ShipglowsOutputAnalysis(platform: platform);
      await analysis.start(generation: 5);
      return (platform, analysis);
    }

    test('decodes records and advances the cursor', () async {
      final (platform, analysis) = await started();
      platform.batches
        ..add(batch(generation: 5, after: 0, sequences: [1, 2, 3]))
        ..add(batch(generation: 5, after: 3, sequences: []));
      final first = (await analysis.poll())!;
      expect(first.records.length, 3);
      expect(first.records.first.peak, closeTo(0.25, 1e-6));
      expect(first.records.first.visualRms, 0.5);
      expect(first.records[1].centerFrame, 480);
      expect(first.records.first.visualBands!.length, 64);
      expect(first.records.first.physicalSpectrum!.length, 1025);
      expect(first.records.first.visualEnvelope!.length, 256);
      expect(analysis.nextSequenceCursor, 3);
      final empty = (await analysis.poll())!;
      expect(empty.records, isEmpty);
      expect(platform.batchRequests, [0, 3]);
    });

    test('keeps a single request in flight', () async {
      final (platform, analysis) = await started();
      platform.holdBatch = Completer<void>();
      platform.batches.add(batch(generation: 5, after: 0, sequences: [1]));
      final a = analysis.poll();
      final b = analysis.poll();
      expect(identical(a, b), isTrue);
      expect(analysis.isPolling, isTrue);
      platform.holdBatch!.complete();
      platform.holdBatch = null;
      await a;
      expect(platform.batchRequests, [0]);
      expect(analysis.isPolling, isFalse);
    });

    test('accepts a declared resync and rejects a silent gap', () async {
      final (platform, analysis) = await started();
      platform.batches
        ..add(
          batch(
            generation: 5,
            after: 0,
            sequences: [40, 41],
            resync: true,
            reason: 'lag_window',
          ),
        )
        ..add(batch(generation: 5, after: 41, sequences: [50]));
      final resynced = (await analysis.poll())!;
      expect(resynced.deliveryResync, isTrue);
      expect(resynced.skippedDeliveryRecords, 39);
      await expectLater(analysis.poll(), throwsFormatException);
      expect(analysis.failed, isTrue);
      expect(analysis.isActive, isFalse);
      expect(
        analysis.nextSequenceCursor,
        41,
        reason: 'invalid packets do not move the cursor',
      );
      expect(platform.stoppedLeases, ['lease-1']);
    });

    test('rejects a payload that does not match its record count', () async {
      final (platform, analysis) = await started();
      platform.batches.add(
        batch(
          generation: 5,
          after: 0,
          sequences: [1, 2],
          payload: encodeRecords(11, [(sequence: 1, center: 0, peak: 0.1)]),
        ),
      );
      await expectLater(analysis.poll(), throwsFormatException);
      expect(analysis.failed, isTrue);
    });

    test('rejects masks outside the profile', () async {
      final analysis = ShipglowsOutputAnalysis(platform: FakePlatform());
      expect(
        () => analysis.start(generation: 1, featureMask: 5),
        throwsArgumentError,
      );
    });

    test('streams batches until cancelled', () async {
      final (platform, analysis) = await started();
      for (var i = 0; i < 3; i++) {
        platform.batches.add(
          batch(generation: 5, after: i, sequences: [i + 1]),
        );
      }
      final received = await analysis
          .batches(interval: const Duration(milliseconds: 1))
          .take(3)
          .toList();
      expect(received.map((b) => b.firstSequence), [1, 2, 3]);
      await analysis.stop();
      expect(platform.stoppedLeases, ['lease-1']);
      expect(await analysis.poll(), isNull);
    });
  });
}
