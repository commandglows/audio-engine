import 'dart:async';
import 'dart:typed_data';

import '../shipglows_audio_platform_interface.dart';

/// Physical analysis profile; both run at 48 kHz with 480-frame (10 ms) hops.
enum ShipglowsAnalysisProfile {
  /// Levels plus the 42.7 ms oscilloscope envelope (masks 3 and 11).
  music,

  /// Levels only (masks 5 and 13).
  speech;

  String get wireName => this == music ? 'sgav-music-1' : 'sgav-speech-1';
}

/// Shared visual transform (`sgav-visual-transform-1`) applied natively.
class ShipglowsVisualTransform {
  const ShipglowsVisualTransform({
    this.sensitivityDb = 0,
    this.amplitudeGamma = 0.8,
    this.attackMs = 20,
    this.releaseMs = 180,
    this.spectrumFloorDb = -80,
    this.spectrumCeilingDb = 0,
  });

  final double sensitivityDb;
  final double amplitudeGamma;
  final double attackMs;
  final double releaseMs;
  final double spectrumFloorDb;
  final double spectrumCeilingDb;

  Map<String, Object?> toMap() => <String, Object?>{
    'version': 1,
    'sensitivity_db': sensitivityDb,
    'amplitude_gamma': amplitudeGamma,
    'attack_ms': attackMs,
    'release_ms': releaseMs,
    'spectrum_floor_db': spectrumFloorDb,
    'spectrum_ceiling_db': spectrumCeilingDb,
  };
}

/// One 10 ms analysis hop of the real output.
class ShipglowsAnalysisRecord {
  const ShipglowsAnalysisRecord({
    required this.sequence,
    required this.centerFrame,
    required this.continuityId,
    required this.validFrames,
    required this.flags,
    required this.physicalLevels,
    required this.visualLevels,
    this.physicalEnvelope,
    this.visualEnvelope,
    this.physicalSpectrum,
    this.visualBands,
  });

  /// Global stream sequence, starting at 1 and never reused.
  final int sequence;

  /// Hop center in output sample frames (48 kHz).
  final int centerFrame;
  final int continuityId;

  /// Measured frames in this hop, 1 to 480.
  final int validFrames;

  /// Bit 0: first record after a PCM discontinuity. Bit 1: the hop contains
  /// known submitted silence.
  final int flags;

  /// Digital amplitudes: peak left, peak right, peak, RMS left, RMS right, RMS.
  final Float32List physicalLevels;

  /// The same six levels after the visual transform, 0 to 1.
  final Float32List visualLevels;

  /// Music profile only: 64 bins of left min, left max, right min, right max.
  final Float32List? physicalEnvelope;
  final Float32List? visualEnvelope;

  /// Spectrum masks only: 1025 one-sided bin powers (23.4375 Hz spacing).
  final Float32List? physicalSpectrum;

  /// Spectrum masks only: 64 log-spaced bands from 20 Hz to 20 kHz, 0 to 1.
  final Float32List? visualBands;

  double get peak => physicalLevels[2];
  double get rms => physicalLevels[5];
  double get visualPeak => visualLevels[2];
  double get visualRms => visualLevels[5];
  bool get followsDiscontinuity => flags & 1 != 0;
  bool get containsSubmittedSilence => flags & 2 != 0;
}

/// One validated batch of recent records.
class ShipglowsAnalysisBatch {
  const ShipglowsAnalysisBatch({
    required this.state,
    required this.continuityId,
    required this.runOriginFrame,
    required this.firstSequence,
    required this.lastSequence,
    required this.deliveryResync,
    required this.resyncReason,
    required this.skippedDeliveryRecords,
    required this.pcmGap,
    required this.droppedPcmFrames,
    required this.nativeObservationNs,
    required this.records,
  });

  /// `warming_up`, `live`, `paused`, `ended`, `gap` or `failed`.
  final String state;
  final int continuityId;
  final int? runOriginFrame;
  final int firstSequence;
  final int lastSequence;

  /// True when older records were skipped; the caller should restart its
  /// view from [records] instead of appending.
  final bool deliveryResync;

  /// `none`, `initial_window`, `pcm_discontinuity`, `history_overwrite` or
  /// `lag_window`.
  final String resyncReason;
  final int skippedDeliveryRecords;
  final bool pcmGap;
  final int droppedPcmFrames;
  final int nativeObservationNs;
  final List<ShipglowsAnalysisRecord> records;
}

const _batchKeys = <String>{
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
const _states = {'warming_up', 'live', 'paused', 'ended', 'gap', 'failed'};
const _reasons = {
  'none',
  'initial_window',
  'pcm_discontinuity',
  'history_overwrite',
  'lag_window',
};
const _recordLayout = 'sgav-record-2-compact-speech-1';
const _maxBatchRecords = 16;
const _hopFrames = 480;

int _recordBytes(int mask) => switch (mask) {
  3 => 2136,
  11 => 6492,
  5 => 88,
  13 => 4444,
  _ => throw ArgumentError.value(
    mask,
    'featureMask',
    'unsupported analysis mask',
  ),
};

/// Pollable analysis stream of the native output (analysis API v2).
///
/// At most one native request is in flight: [poll] returns the pending
/// future while a request runs, and [batches] skips ticks while busy. The
/// native side analyses the exact submitted output off the audio callback
/// and never blocks it; stopping only releases this optional work.
class ShipglowsOutputAnalysis {
  // The public parameter name stays non-private.
  ShipglowsOutputAnalysis({ShipglowsAudioPlatform? platform})
    // ignore: prefer_initializing_formals
    : _platform = platform;

  /// Mixed output of the synth and file playback, fenced by the synth session.
  static const String outputMixSource = 'windows_output_mix_v1';

  /// File-playback session output, fenced by the playback generation.
  static const String playbackOutputSource = 'windows_playback_output_v1';

  final ShipglowsAudioPlatform? _platform;
  ShipglowsAudioPlatform get _channel =>
      _platform ?? ShipglowsAudioPlatform.instance;

  int? _generation;
  String? _leaseId;
  String? _streamId;
  String? _profile;
  int _mask = 0;
  String? _digest;
  int _cursor = 0;
  Future<ShipglowsAnalysisBatch?>? _inFlight;
  bool _failed = false;

  bool get isActive => _leaseId != null;
  bool get isPolling => _inFlight != null;

  /// Whether a malformed batch released the lease; restart to recover.
  bool get failed => _failed;
  int get nextSequenceCursor => _cursor;

  /// Acquires a lease for [generation] (the synth generation for the mix
  /// source, the playback generation for the playback source).
  Future<void> start({
    required int generation,
    String source = outputMixSource,
    ShipglowsAnalysisProfile profile = ShipglowsAnalysisProfile.music,
    int featureMask = 11,
    ShipglowsVisualTransform transform = const ShipglowsVisualTransform(),
  }) async {
    final allowed = profile == ShipglowsAnalysisProfile.music
        ? const {3, 11}
        : const {5, 13};
    if (!allowed.contains(featureMask)) {
      throw ArgumentError.value(
        featureMask,
        'featureMask',
        'not allowed for ${profile.name}',
      );
    }
    if (isActive) await stop();
    final lease = await _channel.startAnalysis(
      generation: generation,
      source: source,
      profile: profile.wireName,
      featureMask: featureMask,
      transformConfig: transform.toMap(),
    );
    if (lease['analysis_api_version'] != 2 ||
        lease['generation'] != generation ||
        lease['profile'] != profile.wireName ||
        lease['feature_mask'] != featureMask ||
        lease['lease_id'] is! String ||
        lease['stream_id'] is! String ||
        lease['transform_digest'] is! String) {
      throw const FormatException(
        'Native analysis lease does not match its request.',
      );
    }
    _generation = generation;
    _leaseId = lease['lease_id']! as String;
    _streamId = lease['stream_id']! as String;
    _profile = profile.wireName;
    _mask = featureMask;
    _digest = lease['transform_digest']! as String;
    _cursor = 0;
    _failed = false;
  }

  /// Fetches the records published since the last accepted batch, or returns
  /// the request already in flight. Null when no lease is active.
  Future<ShipglowsAnalysisBatch?> poll() {
    final pending = _inFlight;
    if (pending != null) return pending;
    if (!isActive) return Future.value();
    final request = _pollOnce();
    _inFlight = request;
    request.whenComplete(() {
      if (identical(_inFlight, request)) _inFlight = null;
    }).ignore();
    return request;
  }

  /// Polls every [interval] while listened to, never overlapping requests.
  /// The stream closes when the lease ends or a batch is malformed.
  Stream<ShipglowsAnalysisBatch> batches({
    Duration interval = const Duration(milliseconds: 33),
  }) {
    late final StreamController<ShipglowsAnalysisBatch> controller;
    Timer? timer;
    void tick(Timer _) {
      if (isPolling || controller.isClosed) return;
      if (!isActive) {
        timer?.cancel();
        controller.close();
        return;
      }
      poll().then(
        (batch) {
          if (batch != null && !controller.isClosed) controller.add(batch);
        },
        onError: (Object error, StackTrace stack) {
          if (controller.isClosed) return;
          controller.addError(error, stack);
          if (_failed) {
            timer?.cancel();
            controller.close();
          }
        },
      );
    }

    controller = StreamController<ShipglowsAnalysisBatch>(
      onListen: () => timer = Timer.periodic(interval, tick),
      onCancel: () => timer?.cancel(),
    );
    return controller.stream;
  }

  /// Releases the lease. Audio is unaffected.
  Future<void> stop() async {
    final leaseId = _leaseId;
    final generation = _generation;
    _leaseId = null;
    if (leaseId == null || generation == null) return;
    final pending = _inFlight;
    if (pending != null) await pending.then((_) {}, onError: (Object _) {});
    await _channel.stopAnalysis(generation: generation, leaseId: leaseId);
  }

  Future<ShipglowsAnalysisBatch?> _pollOnce() async {
    final leaseId = _leaseId!;
    final generation = _generation!;
    final after = _cursor;
    final response = await _channel.getAnalysisBatch(
      generation: generation,
      leaseId: leaseId,
      afterSequence: after,
    );
    if (_leaseId != leaseId) return null; // Stopped or restarted meanwhile.
    try {
      final batch = _validate(response, after);
      _cursor = response['next_sequence_cursor']! as int;
      return batch;
    } on FormatException {
      // An invalid packet leaves the cursor unchanged and releases this
      // optional lease; recovery requires an explicit restart.
      _failed = true;
      _leaseId = null;
      await _channel
          .stopAnalysis(generation: generation, leaseId: leaseId)
          .then((_) {}, onError: (Object _) {});
      rethrow;
    }
  }

  ShipglowsAnalysisBatch _validate(Map<Object?, Object?> r, int after) {
    Never fail(String reason) =>
        throw FormatException('Invalid analysis batch: $reason.');
    int integer(String key) {
      final value = r[key];
      if (value is! int || value < 0) fail('$key is not a nonnegative integer');
      return value;
    }

    if (r.length != _batchKeys.length ||
        !r.keys.every((k) => k is String && _batchKeys.contains(k))) {
      fail('unexpected keys');
    }
    if (r['analysis_api_version'] != 2 ||
        r['generation'] != _generation ||
        r['lease_id'] != _leaseId ||
        r['stream_id'] != _streamId ||
        r['profile'] != _profile ||
        r['feature_mask'] != _mask ||
        r['transform_digest'] != _digest ||
        r['record_layout'] != _recordLayout) {
      fail('identity mismatch');
    }
    final state = r['state'];
    final reason = r['resync_reason'];
    final resync = r['delivery_resync'];
    final pcmGap = r['pcm_gap'];
    if (state is! String || !_states.contains(state)) fail('unknown state');
    if (reason is! String || !_reasons.contains(reason)) {
      fail('unknown resync reason');
    }
    if (resync is! bool || pcmGap is! bool) fail('flags are not booleans');
    final bytesPerRecord = _recordBytes(_mask);
    final count = integer('record_count');
    if (integer('record_bytes') != bytesPerRecord || count > _maxBatchRecords) {
      fail('record size');
    }
    final payload = r['records'];
    if (payload is! Uint8List || payload.length != count * bytesPerRecord) {
      fail('payload length');
    }
    final continuity = integer('continuity_id');
    final origin = r['run_origin_frame'];
    if (origin != null && (origin is! int || origin < 0)) fail('run origin');
    final first = integer('first_sequence');
    final last = integer('last_sequence');
    final next = integer('next_sequence_cursor');
    final skipped = integer('skipped_delivery_records');

    final records = <ShipglowsAnalysisRecord>[];
    final data = ByteData.sublistView(payload);
    for (var i = 0; i < count; i++) {
      records.add(_decode(data, i * bytesPerRecord, bytesPerRecord));
    }
    if (count == 0) {
      if (first != 0 || last != 0 || skipped != 0 || next != after) {
        fail('empty batch cursor');
      }
    } else {
      if (origin == null) fail('missing run origin');
      if (records.first.sequence != first ||
          records.last.sequence != last ||
          next != last) {
        fail('sequence endpoints');
      }
      for (var i = 0; i < count; i++) {
        final record = records[i];
        if (record.sequence != first + i ||
            record.continuityId != continuity ||
            record.centerFrame < (origin as int) ||
            (record.centerFrame - origin) % _hopFrames != 0 ||
            (i > 0 &&
                record.centerFrame !=
                    records[i - 1].centerFrame + _hopFrames)) {
          fail('record $i is not contiguous');
        }
      }
      if (first <= after) fail('sequence rewinds');
      if (!resync && (first != after + 1 || skipped != 0)) {
        fail('gap without resync');
      }
      if (skipped != first - after - 1) fail('skipped count');
    }
    return ShipglowsAnalysisBatch(
      state: state,
      continuityId: continuity,
      runOriginFrame: origin as int?,
      firstSequence: first,
      lastSequence: last,
      deliveryResync: resync,
      resyncReason: reason,
      skippedDeliveryRecords: skipped,
      pcmGap: pcmGap,
      droppedPcmFrames: integer('dropped_pcm_frames'),
      nativeObservationNs: integer('native_observation_ns'),
      records: List.unmodifiable(records),
    );
  }

  ShipglowsAnalysisRecord _decode(ByteData data, int offset, int length) {
    var at = offset;
    int u64() {
      final value = data.getUint64(at, Endian.little);
      at += 8;
      return value;
    }

    int u32() {
      final value = data.getUint32(at, Endian.little);
      at += 4;
      return value;
    }

    Float32List floats(int count) {
      final values = Float32List(count);
      for (var i = 0; i < count; i++) {
        final value = data.getFloat32(at, Endian.little);
        if (!value.isFinite) {
          throw const FormatException(
            'Invalid analysis batch: nonfinite value.',
          );
        }
        values[i] = value;
        at += 4;
      }
      return values;
    }

    final sequence = u64();
    final center = u64();
    final continuity = u64();
    final validFrames = u32();
    final envelopeBins = u32();
    final flags = u32();
    final reserved = u32();
    final music = _mask & 2 != 0;
    if (validFrames < 1 ||
        validFrames > _hopFrames ||
        envelopeBins != (music ? 64 : 0) ||
        flags & ~3 != 0 ||
        reserved != 0) {
      throw const FormatException('Invalid analysis batch: record header.');
    }
    final physical = floats(6);
    final visual = floats(6);
    final physicalEnvelope = music ? floats(256) : null;
    final visualEnvelope = music ? floats(256) : null;
    final spectrum = _mask & 8 != 0 ? floats(1025) : null;
    final bands = _mask & 8 != 0 ? floats(64) : null;
    if (at - offset != length) {
      throw const FormatException('Invalid analysis batch: record length.');
    }
    return ShipglowsAnalysisRecord(
      sequence: sequence,
      centerFrame: center,
      continuityId: continuity,
      validFrames: validFrames,
      flags: flags,
      physicalLevels: physical,
      visualLevels: visual,
      physicalEnvelope: physicalEnvelope,
      visualEnvelope: visualEnvelope,
      physicalSpectrum: spectrum,
      visualBands: bands,
    );
  }
}
