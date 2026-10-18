import 'dart:typed_data';

import 'visual_record.dart';

const speechWindowSize = 150;
const speechHistoryCapacity = 166;

/// Fixed-size transformed speech history for live rendering.
///
/// Each slot stores two uint64 values and four float32 values (32 bytes total).
/// The latest complete [VisualRecord] remains owned by the controller for the
/// current scene; older speech records are reduced to this compact ring.
final class SpeechHistoryBuffer {
  final Uint32List _identityAndPosition = Uint32List(speechHistoryCapacity * 4);
  final Float32List _levels = Float32List(speechHistoryCapacity * 4);
  int _next = 0;
  int _count = 0;
  String? _streamId;
  String? _sourceIdentity;
  String? _transformDigest;
  int? _featureMask;
  BigInt? _originFrame;
  BigInt? _continuityId;

  int get length => _count;

  void clear() {
    _identityAndPosition.fillRange(0, _identityAndPosition.length, 0);
    _levels.fillRange(0, _levels.length, 0);
    _next = 0;
    _count = 0;
    _streamId = null;
    _sourceIdentity = null;
    _transformDigest = null;
    _featureMask = null;
    _originFrame = null;
    _continuityId = null;
  }

  void add(VisualRecord record) {
    if (record.profile != AnalysisProfile.speech) {
      throw ArgumentError('Only transformed speech records may be retained.');
    }
    final identityChanged =
        _count > 0 &&
        (_streamId != record.streamId ||
            _sourceIdentity != record.sourceIdentity ||
            _transformDigest != record.transformDigest ||
            _featureMask != record.featureMask ||
            _originFrame != record.originFrame ||
            _continuityId != record.continuityId);
    final previous = _count == 0
        ? null
        : (_next - 1 + speechHistoryCapacity) % speechHistoryCapacity;
    final sequenceGap =
        previous != null &&
        record.sequence != _readUint64(previous * 4) + BigInt.one;
    final centerGap =
        previous != null &&
        record.centerFrame - _readUint64(previous * 4 + 2) != BigInt.from(480);
    if (identityChanged || sequenceGap || centerGap) clear();

    _streamId = record.streamId;
    _sourceIdentity = record.sourceIdentity;
    _transformDigest = record.transformDigest;
    _featureMask = record.featureMask;
    _originFrame = record.originFrame;
    _continuityId = record.continuityId;
    final slot = _next;
    _writeUint64(slot * 4, record.sequence);
    _writeUint64(slot * 4 + 2, record.centerFrame);
    final levelOffset = slot * 4;
    _levels[levelOffset] = record.physicalLevels[5];
    _levels[levelOffset + 1] = record.physicalLevels[2];
    _levels[levelOffset + 2] = record.visualLevels[5];
    _levels[levelOffset + 3] = record.visualLevels[2];
    _next = (_next + 1) % speechHistoryCapacity;
    if (_count < speechHistoryCapacity) _count++;
  }

  SpeechWindow windowAt(BigInt endpointCenterFrame) {
    if (_count == 0) throw StateError('Speech history is empty.');
    var endpointSlot = -1;
    final first =
        (_next - _count + speechHistoryCapacity) % speechHistoryCapacity;
    for (var index = 0; index < _count; index++) {
      final slot = (first + index) % speechHistoryCapacity;
      if (_readUint64(slot * 4 + 2) == endpointCenterFrame) {
        endpointSlot = slot;
        break;
      }
    }
    if (endpointSlot < 0) {
      throw ArgumentError('The requested speech endpoint is not retained.');
    }

    final suffixSlots = <int>[];
    var slot = endpointSlot;
    var expectedCenter = endpointCenterFrame;
    var expectedSequence = _readUint64(slot * 4);
    while (suffixSlots.length < speechWindowSize) {
      if (_readUint64(slot * 4 + 2) != expectedCenter ||
          _readUint64(slot * 4) != expectedSequence) {
        break;
      }
      suffixSlots.add(slot);
      if (suffixSlots.length == speechWindowSize ||
          suffixSlots.length == _count ||
          expectedCenter < BigInt.from(480)) {
        break;
      }
      slot = (slot - 1 + speechHistoryCapacity) % speechHistoryCapacity;
      expectedCenter -= BigInt.from(480);
      expectedSequence -= BigInt.one;
    }

    final physicalRms = List<double>.filled(speechWindowSize, 0);
    final physicalPeak = List<double>.filled(speechWindowSize, 0);
    final visualRms = List<double>.filled(speechWindowSize, 0);
    final visualPeak = List<double>.filled(speechWindowSize, 0);
    final start = speechWindowSize - suffixSlots.length;
    for (var index = 0; index < suffixSlots.length; index++) {
      final source = suffixSlots[suffixSlots.length - index - 1] * 4;
      final target = start + index;
      physicalRms[target] = _levels[source];
      physicalPeak[target] = _levels[source + 1];
      visualRms[target] = _levels[source + 2];
      visualPeak[target] = _levels[source + 3];
    }
    return SpeechWindow._(
      streamId: _streamId!,
      sourceIdentity: _sourceIdentity!,
      continuityId: _continuityId!,
      originFrame: _originFrame!,
      endpointCenterFrame: endpointCenterFrame,
      validSuffixCount: suffixSlots.length,
      physicalRms: List.unmodifiable(physicalRms),
      physicalPeak: List.unmodifiable(physicalPeak),
      visualRms: List.unmodifiable(visualRms),
      visualPeak: List.unmodifiable(visualPeak),
    );
  }

  void _writeUint64(int offset, BigInt value) {
    const mask = 0xffffffff;
    _identityAndPosition[offset] = (value & BigInt.from(mask)).toInt();
    _identityAndPosition[offset + 1] = (value >> 32).toInt();
  }

  BigInt _readUint64(int offset) =>
      (BigInt.from(_identityAndPosition[offset + 1]) << 32) |
      BigInt.from(_identityAndPosition[offset]);
}

/// Bounded, already-transformed speech history for a single display endpoint.
final class SpeechWindow {
  SpeechWindow._({
    required this.streamId,
    required this.sourceIdentity,
    required this.continuityId,
    required this.originFrame,
    required this.endpointCenterFrame,
    required this.validSuffixCount,
    required this.physicalRms,
    required this.physicalPeak,
    required this.visualRms,
    required this.visualPeak,
  });

  final String streamId;
  final String sourceIdentity;
  final BigInt continuityId;
  final BigInt originFrame;
  final BigInt endpointCenterFrame;
  final int validSuffixCount;
  final List<double> physicalRms;
  final List<double> physicalPeak;
  final List<double> visualRms;
  final List<double> visualPeak;

  factory SpeechWindow.fromHistory(
    Iterable<VisualRecord> records, {
    required BigInt endpointCenterFrame,
  }) {
    final buffer = SpeechHistoryBuffer();
    for (final record in records) {
      if (record.centerFrame > endpointCenterFrame) continue;
      buffer.add(record);
    }
    return buffer.windowAt(endpointCenterFrame);
  }

  SpeechWindow interpolateWith(SpeechWindow right, double alpha) {
    if (!alpha.isFinite || alpha < 0 || alpha > 1) {
      throw ArgumentError.value(alpha, 'alpha', 'Must be within [0, 1].');
    }
    if (streamId != right.streamId ||
        sourceIdentity != right.sourceIdentity ||
        continuityId != right.continuityId ||
        originFrame != right.originFrame ||
        right.endpointCenterFrame - endpointCenterFrame != BigInt.from(480)) {
      throw ArgumentError(
        'Speech windows must be adjacent and share identity.',
      );
    }
    if (alpha == 0) return this;
    if (alpha == 1) return right;

    final suffix = validSuffixCount < right.validSuffixCount
        ? validSuffixCount
        : right.validSuffixCount;
    final first = speechWindowSize - suffix;
    List<double> mix(List<double> leftValues, List<double> rightValues) =>
        List.generate(speechWindowSize, (index) {
          if (index < first) return 0;
          return VisualRecord.interpolate(
            leftValues[index],
            rightValues[index],
            alpha,
          );
        }, growable: false);

    return SpeechWindow._(
      streamId: streamId,
      sourceIdentity: sourceIdentity,
      continuityId: continuityId,
      originFrame: originFrame,
      endpointCenterFrame: endpointCenterFrame,
      validSuffixCount: suffix,
      physicalRms: List.unmodifiable(mix(physicalRms, right.physicalRms)),
      physicalPeak: List.unmodifiable(mix(physicalPeak, right.physicalPeak)),
      visualRms: List.unmodifiable(mix(visualRms, right.visualRms)),
      visualPeak: List.unmodifiable(mix(visualPeak, right.visualPeak)),
    );
  }
}
