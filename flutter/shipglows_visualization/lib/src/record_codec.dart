import 'dart:typed_data';

import 'visual_record.dart';

const recordLayoutV2 = 'sgav-record-2-compact-speech-1';
final _uint64Max = (BigInt.one << 64) - BigInt.one;

/// Decoder for the fixed little-endian `sgav-record-2` live/offline payload.
/// It validates float fields and exposes read-only views into one owned buffer.
final class VisualRecordCodec {
  const VisualRecordCodec._();

  static int recordBytes(AnalysisProfile profile, int featureMask) {
    final valid = profile == AnalysisProfile.music
        ? featureMask == 3 || featureMask == 11
        : featureMask == 5 || featureMask == 13;
    if (!valid) throw const FormatException('Unsupported profile/mask pair.');
    final envelopeBytes = profile == AnalysisProfile.music ? 2048 : 0;
    final spectrumBytes = featureMask == 11 || featureMask == 13 ? 4356 : 0;
    return 40 + 48 + envelopeBytes + spectrumBytes;
  }

  /// Decodes an independent record. The record bytes are copied once so later
  /// mutations of [bytes] cannot change the immutable record.
  static VisualRecord decode({
    required Uint8List bytes,
    required String streamId,
    required String sourceIdentity,
    required String transformDigest,
    required AnalysisProfile profile,
    required int featureMask,
    required BigInt originFrame,
  }) {
    final expectedBytes = recordBytes(profile, featureMask);
    if (bytes.length != expectedBytes) {
      throw const FormatException('Record has an invalid byte length.');
    }
    return decodeStored(
      store: VisualRecordFloat32Store.copy(bytes),
      byteOffset: 0,
      recordLength: expectedBytes,
      streamId: streamId,
      sourceIdentity: sourceIdentity,
      transformDigest: transformDigest,
      profile: profile,
      featureMask: featureMask,
      originFrame: originFrame,
    );
  }

  /// Decodes one record from an owned or transferred batch store.
  ///
  /// A transferred store must not be modified or reused until every record
  /// view backed by it has been released.
  static VisualRecord decodeStored({
    required VisualRecordFloat32Store store,
    required int byteOffset,
    required int recordLength,
    required String streamId,
    required String sourceIdentity,
    required String transformDigest,
    required AnalysisProfile profile,
    required int featureMask,
    required BigInt originFrame,
  }) {
    final expectedBytes = recordBytes(profile, featureMask);
    if (recordLength != expectedBytes ||
        byteOffset < 0 ||
        byteOffset > store.lengthInBytes - expectedBytes) {
      throw const FormatException('Record has an invalid byte length.');
    }
    var offset = byteOffset;
    BigInt uint64() {
      var value = BigInt.zero;
      for (var index = 7; index >= 0; index--) {
        value = (value << 8) | BigInt.from(store.byteAt(offset + index));
      }
      offset += 8;
      return value;
    }

    int uint32() {
      final value = store.uint32At(offset);
      offset += 4;
      return value;
    }

    final sequence = uint64();
    final centerFrame = uint64();
    final continuityId = uint64();
    final validFrames = uint32();
    final envelopeBins = uint32();
    final flags = uint32();
    final reserved = uint32();
    if (sequence == BigInt.zero ||
        continuityId == BigInt.zero ||
        centerFrame < originFrame ||
        centerFrame > _uint64Max - BigInt.from(479) ||
        (centerFrame - originFrame) % BigInt.from(480) != BigInt.zero ||
        validFrames < 1 ||
        validFrames > 480 ||
        envelopeBins != (profile == AnalysisProfile.music ? 64 : 0) ||
        flags > 3 ||
        reserved != 0) {
      throw const FormatException('Invalid fixed record header.');
    }

    return VisualRecord.fromFloat32Store(
      store: store,
      byteOffset: byteOffset,
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
    );
  }
}
