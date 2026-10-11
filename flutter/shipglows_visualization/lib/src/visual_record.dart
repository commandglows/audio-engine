import 'dart:collection';
import 'dart:math' as math;
import 'dart:typed_data';

enum AnalysisProfile { music, speech }

abstract final class AnalysisMask {
  static const levels = 1;
  static const oscilloscope = 2;
  static const speechEnvelope = 4;
  static const spectrum = 8;
}

/// Read-only float32 storage shared by decoded views into one wire batch.
///
/// [copy] gives the store independent ownership. [transfer] adopts an aligned
/// input buffer and makes one alignment copy when the input begins at a byte
/// offset unsuitable for float32 views. Callers must not mutate or reuse the
/// input buffer after transfer.
final class VisualRecordFloat32Store {
  VisualRecordFloat32Store.copy(Uint8List bytes)
    : _bytes = Uint8List.fromList(bytes);

  factory VisualRecordFloat32Store.transfer(Uint8List bytes) =>
      VisualRecordFloat32Store._(
        bytes.offsetInBytes % Float32List.bytesPerElement == 0
            ? bytes
            : Uint8List.fromList(bytes),
      );

  VisualRecordFloat32Store._(this._bytes);

  final Uint8List _bytes;

  int get lengthInBytes => _bytes.length;

  int byteAt(int offset) => _bytes[offset];

  int uint32At(int offset) =>
      ByteData.sublistView(_bytes).getUint32(offset, Endian.little);

  List<double> view({
    required int byteOffset,
    required int length,
    required double minimum,
    required double maximum,
    required double tolerance,
    required String field,
  }) {
    final byteLength = length * Float32List.bytesPerElement;
    if (byteOffset < 0 ||
        length < 0 ||
        byteOffset % Float32List.bytesPerElement != 0 ||
        byteOffset > _bytes.length - byteLength) {
      throw const FormatException('Float32 field exceeds its record buffer.');
    }
    final raw = Float32List.sublistView(
      _bytes,
      byteOffset,
      byteOffset + byteLength,
    );
    var clampOnRead = false;
    for (final value in raw) {
      if (!value.isFinite ||
          value < minimum - tolerance ||
          value > maximum + tolerance) {
        throw FormatException('$field value is outside the wire tolerance.');
      }
      clampOnRead |= value < minimum || value > maximum;
    }
    return _ImmutableFloat32View(raw, minimum, maximum, clampOnRead);
  }
}

final class _ImmutableFloat32View extends ListBase<double> {
  _ImmutableFloat32View(
    this._values,
    this._minimum,
    this._maximum,
    this._clamp,
  );

  final Float32List _values;
  final double _minimum;
  final double _maximum;
  final bool _clamp;

  @override
  int get length => _values.length;

  @override
  set length(int value) => throw UnsupportedError('Float32 view is immutable.');

  @override
  double operator [](int index) {
    final value = _values[index];
    return _clamp ? value.clamp(_minimum, _maximum).toDouble() : value;
  }

  @override
  void operator []=(int index, double value) =>
      throw UnsupportedError('Float32 view is immutable.');
}

/// Immutable decoded visual record. Values are already measured and transformed
/// by the shared native processor; consumers must not smooth them again.
final class VisualRecord {
  VisualRecord({
    required String streamId,
    required String sourceIdentity,
    required String transformDigest,
    required AnalysisProfile profile,
    required int featureMask,
    required BigInt sequence,
    required BigInt centerFrame,
    required BigInt originFrame,
    required BigInt continuityId,
    required int validFrames,
    int flags = 0,
    int envelopeBins = 0,
    required List<double> physicalLevels,
    required List<double> visualLevels,
    List<double>? physicalEnvelope,
    List<double>? visualEnvelope,
    List<double>? physicalSpectrum,
    List<double>? visualBands,
  }) : this._(
         streamId: streamId,
         sourceIdentity: sourceIdentity,
         transformDigest: transformDigest,
         profile: profile,
         featureMask: featureMask,
         sequence: sequence,
         centerFrame: centerFrame,
         originFrame: originFrame,
         continuityId: continuityId,
         validFrames: validFrames,
         flags: flags,
         envelopeBins: envelopeBins,
         physicalLevels: List.unmodifiable(physicalLevels),
         visualLevels: List.unmodifiable(visualLevels),
         physicalEnvelope: physicalEnvelope == null
             ? null
             : List.unmodifiable(physicalEnvelope),
         visualEnvelope: visualEnvelope == null
             ? null
             : List.unmodifiable(visualEnvelope),
         physicalSpectrum: physicalSpectrum == null
             ? null
             : List.unmodifiable(physicalSpectrum),
         visualBands: visualBands == null
             ? null
             : List.unmodifiable(visualBands),
       );

  factory VisualRecord.fromFloat32Store({
    required VisualRecordFloat32Store store,
    required int byteOffset,
    required String streamId,
    required String sourceIdentity,
    required String transformDigest,
    required AnalysisProfile profile,
    required int featureMask,
    required BigInt sequence,
    required BigInt centerFrame,
    required BigInt originFrame,
    required BigInt continuityId,
    required int validFrames,
    int flags = 0,
    int envelopeBins = 0,
  }) {
    final physicalLevels = store.view(
      byteOffset: byteOffset + 40,
      length: 6,
      minimum: 0,
      maximum: 8,
      tolerance: 1e-6,
      field: 'Physical level',
    );
    final visualLevels = store.view(
      byteOffset: byteOffset + 64,
      length: 6,
      minimum: 0,
      maximum: 1,
      tolerance: 1e-6,
      field: 'Visual level',
    );
    final music = profile == AnalysisProfile.music;
    final spectrum = (featureMask & AnalysisMask.spectrum) != 0;
    final physicalEnvelope = music
        ? store.view(
            byteOffset: byteOffset + 88,
            length: 256,
            minimum: -8,
            maximum: 8,
            tolerance: 1e-6,
            field: 'Physical envelope',
          )
        : null;
    final visualEnvelope = music
        ? store.view(
            byteOffset: byteOffset + 88 + 1024,
            length: 256,
            minimum: -1,
            maximum: 1,
            tolerance: 1e-6,
            field: 'Visual envelope',
          )
        : null;
    final spectrumOffset = byteOffset + 88 + (music ? 2048 : 0);
    final physicalSpectrum = spectrum
        ? store.view(
            byteOffset: spectrumOffset,
            length: 1025,
            minimum: 0,
            maximum: 64,
            tolerance: 1e-5,
            field: 'Physical spectrum',
          )
        : null;
    final visualBands = spectrum
        ? store.view(
            byteOffset: spectrumOffset + 4100,
            length: 64,
            minimum: 0,
            maximum: 1,
            tolerance: 1e-6,
            field: 'Visual band',
          )
        : null;
    return VisualRecord._(
      streamId: streamId,
      sourceIdentity: sourceIdentity,
      transformDigest: transformDigest,
      profile: profile,
      featureMask: featureMask,
      sequence: sequence,
      centerFrame: centerFrame,
      originFrame: originFrame,
      continuityId: continuityId,
      validFrames: validFrames,
      flags: flags,
      envelopeBins: envelopeBins,
      physicalLevels: physicalLevels,
      visualLevels: visualLevels,
      physicalEnvelope: physicalEnvelope,
      visualEnvelope: visualEnvelope,
      physicalSpectrum: physicalSpectrum,
      visualBands: visualBands,
    );
  }

  VisualRecord._({
    required this.streamId,
    required this.sourceIdentity,
    required this.transformDigest,
    required this.profile,
    required this.featureMask,
    required this.sequence,
    required this.centerFrame,
    required this.originFrame,
    required this.continuityId,
    required this.validFrames,
    required this.physicalLevels,
    required this.visualLevels,
    this.flags = 0,
    this.envelopeBins = 0,
    this.physicalEnvelope,
    this.visualEnvelope,
    this.physicalSpectrum,
    this.visualBands,
  }) {
    if (!RegExp(r'^[!-~]{1,64}$').hasMatch(streamId) ||
        !RegExp(r'^[!-~]{1,128}$').hasMatch(sourceIdentity)) {
      throw ArgumentError('Stream and source identities are required.');
    }
    if (!RegExp(r'^[0-9a-f]{64}$').hasMatch(transformDigest)) {
      throw ArgumentError(
        'Transform digest must be 64 lowercase hexadecimal characters.',
      );
    }
    final maxUint64 = BigInt.parse('18446744073709551615');
    if (sequence <= BigInt.zero ||
        sequence > maxUint64 ||
        centerFrame < originFrame ||
        originFrame < BigInt.zero ||
        centerFrame > maxUint64 - BigInt.from(479) ||
        continuityId <= BigInt.zero ||
        continuityId > maxUint64) {
      throw ArgumentError('Invalid sequence, center, origin, or continuity.');
    }
    if ((centerFrame - originFrame) % BigInt.from(480) != BigInt.zero ||
        validFrames < 1 ||
        validFrames > 480) {
      throw ArgumentError('Record is not aligned to the 480-frame hop.');
    }
    if (physicalLevels.length != 6 || visualLevels.length != 6) {
      throw ArgumentError(
        'A record must contain six physical and visual levels.',
      );
    }
    final expectedMask = profile == AnalysisProfile.music ? 3 : 5;
    final legalMask = profile == AnalysisProfile.music ? {3, 11} : {5, 13};
    if (!legalMask.contains(featureMask) ||
        (featureMask & expectedMask) != expectedMask) {
      throw ArgumentError('Profile and feature mask do not match.');
    }
    _validateRange(physicalLevels, 0, 8, 'physical levels');
    _validateRange(visualLevels, 0, 1, 'visual levels');
    final expectedPeak = math.max(physicalLevels[0], physicalLevels[1]);
    final expectedRms = math.sqrt(
      (physicalLevels[3] * physicalLevels[3] +
              physicalLevels[4] * physicalLevels[4]) /
          2,
    );
    if ((physicalLevels[2] - expectedPeak).abs() > 1e-6 ||
        (physicalLevels[5] - expectedRms).abs() > 1e-6) {
      throw ArgumentError('Combined levels do not match channel values.');
    }
    if (profile == AnalysisProfile.music) {
      if (physicalEnvelope == null ||
          physicalEnvelope!.length != 256 ||
          visualEnvelope == null ||
          visualEnvelope!.length != 256 ||
          envelopeBins != 64) {
        throw ArgumentError(
          'Music records require 256-value envelopes and 64 bins.',
        );
      }
      _validateRange(physicalEnvelope!, -8, 8, 'physical envelope');
      _validateRange(visualEnvelope!, -1, 1, 'visual envelope');
    } else if (physicalEnvelope != null ||
        visualEnvelope != null ||
        envelopeBins != 0) {
      throw ArgumentError('Speech records do not contain a music envelope.');
    }
    final spectrumRequested = (featureMask & AnalysisMask.spectrum) != 0;
    if (spectrumRequested != (visualBands != null) ||
        spectrumRequested != (physicalSpectrum != null)) {
      throw ArgumentError(
        'Visual bands must be present exactly when requested.',
      );
    }
    if (visualBands != null) {
      if (visualBands!.length != 64) {
        throw ArgumentError('Spectrum visualization requires 64 bands.');
      }
      _validateRange(visualBands!, 0, 1, 'visual bands');
      if (physicalSpectrum!.length != 1025) {
        throw ArgumentError('Physical spectrum requires 1025 bins.');
      }
      _validateRange(physicalSpectrum!, 0, 64, 'physical spectrum');
    }
    if (flags < 0 || flags > 3) {
      throw ArgumentError.value(flags, 'flags');
    }
  }

  final String streamId;
  final String sourceIdentity;
  final String transformDigest;
  final AnalysisProfile profile;
  final int featureMask;
  final BigInt sequence;
  final BigInt centerFrame;
  final BigInt originFrame;
  final BigInt continuityId;
  final int validFrames;
  final int flags;
  final int envelopeBins;
  final List<double> physicalLevels;
  final List<double> visualLevels;
  final List<double>? physicalEnvelope;
  final List<double>? visualEnvelope;
  final List<double>? physicalSpectrum;
  final List<double>? visualBands;

  static void _validateRange(
    List<double> values,
    double minimum,
    double maximum,
    String name,
  ) {
    for (final value in values) {
      if (!value.isFinite || value < minimum || value > maximum) {
        throw ArgumentError('$name contain an invalid value.');
      }
    }
  }

  bool sameIdentity(VisualRecord other) =>
      streamId == other.streamId &&
      sourceIdentity == other.sourceIdentity &&
      transformDigest == other.transformDigest &&
      profile == other.profile &&
      featureMask == other.featureMask &&
      originFrame == other.originFrame &&
      continuityId == other.continuityId;

  static double interpolate(double left, double right, double alpha) =>
      left + (right - left) * alpha;

  VisualRecord interpolateWith(VisualRecord right, double alpha) {
    if (!alpha.isFinite || alpha < 0 || alpha > 1) {
      throw ArgumentError.value(alpha, 'alpha', 'Must be within [0, 1].');
    }
    if (!sameIdentity(right) ||
        right.centerFrame - centerFrame != BigInt.from(480)) {
      throw ArgumentError(
        'Only adjacent records in one continuity can interpolate.',
      );
    }
    if (alpha == 0) return this;
    if (alpha == 1) return right;
    List<double> mix(List<double> a, List<double> b) => List.generate(
      a.length,
      (index) => interpolate(a[index], b[index], alpha),
      growable: false,
    );
    final interpolatedPhysicalLevels = mix(
      physicalLevels,
      right.physicalLevels,
    );
    interpolatedPhysicalLevels[2] = math.max(
      interpolatedPhysicalLevels[0],
      interpolatedPhysicalLevels[1],
    );
    interpolatedPhysicalLevels[5] = math.sqrt(
      (interpolatedPhysicalLevels[3] * interpolatedPhysicalLevels[3] +
              interpolatedPhysicalLevels[4] * interpolatedPhysicalLevels[4]) /
          2,
    );
    return VisualRecord(
      streamId: streamId,
      sourceIdentity: sourceIdentity,
      transformDigest: transformDigest,
      profile: profile,
      featureMask: featureMask,
      sequence: right.sequence,
      centerFrame: centerFrame,
      originFrame: originFrame,
      continuityId: continuityId,
      validFrames: validFrames,
      flags: right.flags,
      envelopeBins: envelopeBins,
      physicalLevels: interpolatedPhysicalLevels,
      visualLevels: mix(visualLevels, right.visualLevels),
      physicalEnvelope: physicalEnvelope == null
          ? null
          : mix(physicalEnvelope!, right.physicalEnvelope!),
      visualEnvelope: visualEnvelope == null
          ? null
          : mix(visualEnvelope!, right.visualEnvelope!),
      physicalSpectrum: physicalSpectrum == null
          ? null
          : mix(physicalSpectrum!, right.physicalSpectrum!),
      visualBands: visualBands == null
          ? null
          : mix(visualBands!, right.visualBands!),
    );
  }
}

double clampVisual(double value) => math.max(0.0, math.min(1.0, value));
