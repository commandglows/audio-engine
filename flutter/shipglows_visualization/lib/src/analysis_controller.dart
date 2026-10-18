// ignore_for_file: prefer_initializing_formals

import 'dart:async';

import 'package:flutter/foundation.dart';

import 'analysis_batch.dart';
import 'scene.dart';
import 'speech_window.dart';
import 'visual_record.dart';

/// Product/platform adapter boundary. Implementations own MethodChannel and
/// lease transport; this controller never opens capture or transports PCM.
abstract interface class AnalysisSource {
  Future<bool> isAvailable();

  Future<AnalysisLease> startAnalysis({
    required int generation,
    required AnalysisProfile profile,
    required int featureMask,
    required Map<String, Object?> transformConfig,
  });

  /// Returns an owned response map. After returning, the source must not
  /// mutate or reuse the `records` Uint8List payload.
  Future<Map<Object?, Object?>> getAnalysisBatch({
    required AnalysisLease lease,
    required BigInt afterSequence,
  });

  Future<void> stopAnalysis(AnalysisLease lease);
}

final class AnalysisLease {
  const AnalysisLease({
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
  final String streamId;
  final String sourceIdentity;
  final AnalysisProfile profile;
  final int featureMask;
  final String transformDigest;
}

/// Bounded single-flight poll controller shared by live and fixture sources.
/// The caller owns its poll cadence (normally about 30 Hz).
final class AnalysisController {
  AnalysisController({
    required this.source,
    required SceneConfig sceneConfig,
    required Map<String, Object?> transformConfig,
    required this.generation,
  }) : // ignore: prefer_initializing_formals; the public parameter stays non-private.
       _sceneConfig = sceneConfig,
       _transformConfig = Map.unmodifiable(transformConfig),
       state = ValueNotifier(
         SceneState(
           status: VisualizationStatus.off,
           label: 'Visualization off',
           intensity: 0,
           validSuffixCount: 0,
         ),
       );

  final AnalysisSource source;
  SceneConfig _sceneConfig;
  final Map<String, Object?> _transformConfig;
  final int generation;
  final SceneEvaluator _evaluator = const SceneEvaluator();
  final ValueNotifier<SceneState> state;
  final SpeechHistoryBuffer _speechHistory = SpeechHistoryBuffer();
  VisualRecord? _latestRecord;
  AnalysisLease? _lease;
  VisualRecord? _frozenRecord;
  SpeechWindow? _frozenSpeechWindow;
  BigInt _cursor = BigInt.zero;
  BigInt? _continuity;
  Future<void>? _inFlight;
  Future<void>? _startInFlight;
  Future<void>? _startRequestInFlight;
  Future<void>? _stopInFlight;
  Future<void>? _leaseReleaseInFlight;
  String? _startRequestKey;
  int _epoch = 0;
  int _sceneUpdateRevision = 0;
  bool _disposed = false;
  bool _leaseReleaseUncertain = false;
  double? _pausedTimeSeconds;
  String? _lastSourceState;

  SceneConfig get sceneConfig => _sceneConfig;

  int get retainedRecordCount => _latestRecord == null
      ? 0
      : _latestRecord!.profile == AnalysisProfile.speech
      ? _speechHistory.length
      : 1;
  BigInt get nextSequenceCursor => _cursor;

  /// Updates pure scene inputs. Profile/mask/transform changes acquire a new
  /// lease; time and other scene-only inputs do not restart analysis.
  Future<void> updateSceneConfig(SceneConfig next) async {
    _ensureOpen();
    final updateRevision = ++_sceneUpdateRevision;
    final wasAnalysisRequired = _requiresAnalysis(_sceneConfig);
    final isAnalysisRequired = _requiresAnalysis(next);
    final leaseBoundary =
        next.profile != _sceneConfig.profile ||
        next.featureMask != _sceneConfig.featureMask ||
        next.transformDigest != _sceneConfig.transformDigest;
    _sceneConfig = next;
    if (wasAnalysisRequired && !isAnalysisRequired) {
      if (next.motion == SceneMotion.static && next.intensity > 0) {
        _captureStaticFeature();
      } else {
        _clearFrozenFeature();
      }
      await _releaseLeasePreservingHistory();
      if (_disposed || updateRevision != _sceneUpdateRevision) return;
      _publish(_lastSourceState ?? 'warming_up');
    } else if (!wasAnalysisRequired && isAnalysisRequired ||
        leaseBoundary && isAnalysisRequired) {
      _clearFrozenFeature();
      await start();
    } else {
      if (leaseBoundary ||
          next.intensity == 0 ||
          next.motion != SceneMotion.static) {
        _clearFrozenFeature();
      }
      if (!isAnalysisRequired && _lease != null) {
        await _releaseLeasePreservingHistory();
      }
      if (_disposed || updateRevision != _sceneUpdateRevision) return;
      _publish(_lastSourceState ?? 'warming_up');
    }
  }

  Future<void> start() {
    _ensureOpen();
    final requestKey = _leaseRequestKey(sceneConfig);
    final active = _startRequestInFlight;
    if (active != null) {
      if (_startRequestKey == requestKey) return active;
      final updateRevision = _sceneUpdateRevision;
      return active.then<void>((_) async {
        if (!_disposed && updateRevision == _sceneUpdateRevision) {
          await start();
        }
      });
    }
    final requestEpoch = ++_epoch;
    late final Future<void> request;
    request = _startAfterStop(requestEpoch).whenComplete(() {
      if (identical(_startRequestInFlight, request)) {
        _startRequestInFlight = null;
        _startRequestKey = null;
      }
    });
    _startRequestKey = requestKey;
    _startRequestInFlight = request;
    return request;
  }

  Future<void> _startAfterStop(int requestEpoch) async {
    await stop();
    // A stop requested while this start was waiting cancels its acquisition.
    if (_disposed || _epoch != requestEpoch + 1) return;
    // Do not acquire a second native lease while a failed stop leaves the old
    // lease's ownership uncertain. A later start can retry the release.
    if (_lease != null) return;
    final epoch = ++_epoch;
    _setStatus(VisualizationStatus.warmingUp);
    if (!_requiresAnalysis(_sceneConfig)) {
      _publish('warming_up');
      return;
    }
    late final Future<void> opening;
    opening = _openLease(epoch).whenComplete(() {
      if (identical(_startInFlight, opening)) _startInFlight = null;
    });
    _startInFlight = opening;
    await opening;
  }

  String _leaseRequestKey(SceneConfig config) =>
      '${_requiresAnalysis(config)}:${config.profile.name}:'
      '${config.featureMask}:${config.transformDigest}';

  Future<void> _openLease(int epoch) async {
    try {
      final available = await source.isAvailable();
      if (_disposed || epoch != _epoch) return;
      if (!available) {
        _setStatus(VisualizationStatus.unavailable);
        return;
      }
      final lease = await source.startAnalysis(
        generation: generation,
        profile: sceneConfig.profile,
        featureMask: sceneConfig.featureMask,
        transformConfig: _transformConfig,
      );
      if (_disposed || epoch != _epoch) {
        await _releaseOpenedLease(lease);
        return;
      }
      if (lease.generation != generation ||
          lease.profile != sceneConfig.profile ||
          lease.featureMask != sceneConfig.featureMask ||
          lease.transformDigest != sceneConfig.transformDigest) {
        await _releaseOpenedLease(lease);
        throw const FormatException(
          'Started analysis lease does not match scene.',
        );
      }
      _lease = lease;
      _cursor = BigInt.zero;
      _continuity = null;
      _clearHistory();
      _setStatus(VisualizationStatus.warmingUp);
    } catch (_) {
      if (epoch == _epoch && !_disposed) _setStatus(VisualizationStatus.failed);
    }
  }

  /// Concurrent requests share one future: no overlapping transport polls.
  Future<void> poll() {
    _ensureOpen();
    if (!_requiresAnalysis(_sceneConfig)) return Future.value();
    final active = _inFlight;
    if (active != null) return active;
    final lease = _lease;
    if (lease == null) return Future.value();
    final epoch = _epoch;
    late final Future<void> request;
    request = _pollOnce(lease, epoch).whenComplete(() {
      if (identical(_inFlight, request)) _inFlight = null;
    });
    _inFlight = request;
    return request;
  }

  Future<void> _pollOnce(AnalysisLease lease, int epoch) async {
    try {
      final raw = await source.getAnalysisBatch(
        lease: lease,
        afterSequence: _cursor,
      );
      if (_disposed || epoch != _epoch || !identical(_lease, lease)) return;
      final batch = AnalysisBatch.decodeTransferred(
        response: raw,
        context: AnalysisBatchContext(
          generation: lease.generation,
          leaseId: lease.leaseId,
          streamId: lease.streamId,
          sourceIdentity: lease.sourceIdentity,
          profile: lease.profile,
          featureMask: lease.featureMask,
          transformDigest: lease.transformDigest,
        ),
        afterSequence: _cursor,
      );
      final reset =
          batch.pcmGap ||
          batch.deliveryResync ||
          (_continuity != null && _continuity != batch.continuityId);
      if (reset) _clearHistory();
      _continuity = batch.continuityId;
      if (batch.records.isNotEmpty) {
        for (final record in batch.records) {
          if (record.profile == AnalysisProfile.speech) {
            _speechHistory.add(record);
          }
          _latestRecord = record;
        }
      }
      // Commit only after metadata and every record have passed validation.
      _cursor = batch.nextSequenceCursor;
      _publish(batch.state);
    } catch (_) {
      if (_disposed || epoch != _epoch || !identical(_lease, lease)) return;
      _lastSourceState = 'failed';
      _setStatus(VisualizationStatus.failed);
      _clearHistory();
      _continuity = null;
      _lease = null;
      try {
        await source.stopAnalysis(lease);
      } catch (_) {
        // Optional analysis release failure must not affect audio ownership.
      }
    }
  }

  void _publish(String sourceState) {
    if (sourceState == 'paused') {
      _pausedTimeSeconds ??= sceneConfig.timeSeconds;
    } else {
      _pausedTimeSeconds = null;
    }
    _lastSourceState = sourceState;
    final staticMode =
        sceneConfig.motion == SceneMotion.static && sceneConfig.intensity > 0;
    final record = staticMode ? _frozenRecord : _latestRecord;
    final status = switch (sourceState) {
      _ when staticMode && record != null => VisualizationStatus.static,
      _ when staticMode => VisualizationStatus.unavailable,
      'live' when _latestRecord != null => VisualizationStatus.live,
      'paused' => VisualizationStatus.paused,
      'ended' => VisualizationStatus.ended,
      'gap' => VisualizationStatus.gap,
      'unavailable' => VisualizationStatus.unavailable,
      'failed' => VisualizationStatus.failed,
      'disabled' => VisualizationStatus.off,
      _ => VisualizationStatus.warmingUp,
    };
    final speechWindow = staticMode
        ? _frozenSpeechWindow
        : record?.profile == AnalysisProfile.speech
        ? _speechHistory.windowAt(record!.centerFrame)
        : null;
    state.value = _evaluator.evaluate(
      config: sceneConfig,
      status: status,
      record: record,
      speechWindow: speechWindow,
      timeSeconds: _pausedTimeSeconds,
    );
    if (_leaseReleaseUncertain && !_requiresAnalysis(sceneConfig)) {
      state.value = state.value.withLabel(
        '${state.value.label} · lease_release_uncertain',
      );
    }
  }

  Future<void> stop() {
    if (_disposed) return Future.value();
    ++_epoch;
    ++_sceneUpdateRevision;
    final active = _stopInFlight;
    if (active != null) return active;
    late final Future<void> stopping;
    stopping = _stopInternal().whenComplete(() {
      if (identical(_stopInFlight, stopping)) _stopInFlight = null;
    });
    _stopInFlight = stopping;
    return stopping;
  }

  Future<void> _stopInternal() async {
    final lease = _lease;
    _lease = null;
    _clearHistory();
    _cursor = BigInt.zero;
    _continuity = null;
    _lastSourceState = null;
    _setStatus(VisualizationStatus.off);
    final pendingStart = _startInFlight;
    if (pendingStart != null) await pendingStart;
    final previousPoll = _inFlight;
    if (previousPoll != null) await previousPoll;
    final previousRelease = _leaseReleaseInFlight;
    if (previousRelease != null) await previousRelease;
    // A stale acquisition can finish after the initial detach above. It holds
    // its lease while attempting cleanup so a failed release remains retryable.
    final leaseToRelease = _lease ?? lease;
    _lease = null;
    if (leaseToRelease != null) {
      try {
        await source.stopAnalysis(leaseToRelease);
        _leaseReleaseUncertain = false;
      } catch (_) {
        _lease = leaseToRelease;
        _leaseReleaseUncertain = true;
        if (!_disposed) _setStatus(VisualizationStatus.failed);
      }
    }
  }

  Future<void> _releaseOpenedLease(AnalysisLease lease) async {
    // Keep ownership until native release succeeds. In particular, stop()
    // awaits the opening future and can retry this handle if cleanup fails.
    _lease = lease;
    try {
      await source.stopAnalysis(lease);
      if (identical(_lease, lease)) _lease = null;
      _leaseReleaseUncertain = false;
    } catch (_) {
      _leaseReleaseUncertain = true;
      rethrow;
    }
  }

  bool _requiresAnalysis(SceneConfig config) =>
      config.intensity > 0 && config.motion != SceneMotion.static;

  void _captureStaticFeature() {
    final record = _latestRecord;
    if (record == null ||
        record.profile != sceneConfig.profile ||
        record.featureMask != sceneConfig.featureMask ||
        record.transformDigest != sceneConfig.transformDigest) {
      _clearFrozenFeature();
      return;
    }
    _frozenRecord = record;
    _frozenSpeechWindow = record.profile == AnalysisProfile.speech
        ? _speechHistory.windowAt(record.centerFrame)
        : null;
  }

  void _clearFrozenFeature() {
    _frozenRecord = null;
    _frozenSpeechWindow = null;
  }

  Future<void> _releaseLeasePreservingHistory() {
    final active = _leaseReleaseInFlight;
    if (active != null) return active;
    late final Future<void> release;
    release = _releaseLeasePreservingHistoryOnce().whenComplete(() {
      if (identical(_leaseReleaseInFlight, release)) {
        _leaseReleaseInFlight = null;
      }
    });
    _leaseReleaseInFlight = release;
    return release;
  }

  Future<void> _releaseLeasePreservingHistoryOnce() async {
    ++_epoch;
    final lease = _lease;
    _lease = null;
    final previousPoll = _inFlight;
    if (previousPoll != null) await previousPoll;
    if (lease != null) {
      try {
        await source.stopAnalysis(lease);
        _leaseReleaseUncertain = false;
      } catch (_) {
        _lease = lease;
        _leaseReleaseUncertain = true;
        _lastSourceState = 'failed';
      }
    } else {
      _leaseReleaseUncertain = false;
    }
  }

  void _setStatus(VisualizationStatus status) {
    if (_disposed) return;
    state.value = _evaluator.evaluate(config: sceneConfig, status: status);
  }

  void _clearHistory() {
    _speechHistory.clear();
    _latestRecord = null;
    _clearFrozenFeature();
  }

  void _ensureOpen() {
    if (_disposed) throw StateError('AnalysisController is disposed.');
  }

  Future<void> dispose() async {
    if (_disposed) return;
    await stop();
    _disposed = true;
    state.dispose();
  }
}
