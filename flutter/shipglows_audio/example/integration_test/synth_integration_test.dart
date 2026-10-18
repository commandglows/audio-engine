// Real-time synth through the shared Windows output, driven from Dart.
//
// Plays short drum hits on the default render endpoint. Reports the Dart
// call cost of fire-and-forget notes, the method-channel round trip and the
// native latency histograms as one JSON line for evidence capture.

import 'dart:convert';
import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:integration_test/integration_test.dart';
import 'package:shipglows_audio/shipglows_audio.dart';
import 'package:shipglows_audio/shipglows_audio_platform_interface.dart';

Map<String, num> _summary(List<int> micros) {
  final sorted = [...micros]..sort();
  int at(double q) =>
      sorted[((q * sorted.length).ceil() - 1).clamp(0, sorted.length - 1)];
  return {
    'count': sorted.length,
    'p50_us': at(0.50),
    'p95_us': at(0.95),
    'max_us': sorted.last,
  };
}

Map<String, int> _latency(ShipglowsSynthLatency latency) => {
  'count': latency.count,
  'mean_us': latency.meanMicroseconds,
  'p50_us': latency.p50Microseconds,
  'p95_us': latency.p95Microseconds,
  'p99_us': latency.p99Microseconds,
  'max_us': latency.maxMicroseconds,
};

void main() {
  IntegrationTestWidgetsFlutterBinding.ensureInitialized();

  testWidgets('synth plays on the shared output and feeds mix analysis', (
    tester,
  ) async {
    if (!Platform.isWindows) return;
    final synth = ShipglowsSynth();
    final started = await synth.start(
      instrument: ShipglowsSynthInstrument.drums,
    );
    expect(started.state, 'running');
    final analysis = await synth.startAnalysis();

    final platform = ShipglowsAudioPlatform.instance;
    final callCost = <int>[];
    final roundTrip = <int>[];
    var records = 0;
    var peak = 0.0;
    var bandEnergy = 0.0;
    var resyncs = 0;
    for (var i = 0; i < 48; i++) {
      final watch = Stopwatch()..start();
      if (i < 24) {
        // Fire-and-forget cost on the calling isolate.
        synth.noteOn(i.isEven ? 36 : 42, velocity: 0.8);
        callCost.add(watch.elapsedMicroseconds);
      } else {
        // Full method-channel round trip (Dart -> platform thread -> Dart).
        final accepted = await platform.synthClick(
          generation: synth.generation!,
          accent: i % 4 == 0,
        );
        roundTrip.add(watch.elapsedMicroseconds);
        expect(accepted, isTrue);
      }
      final batch = await analysis.poll();
      if (batch != null) {
        if (batch.deliveryResync) resyncs++;
        records += batch.records.length;
        for (final record in batch.records) {
          if (record.peak > peak) peak = record.peak;
          bandEnergy += record.visualBands!.reduce((a, b) => a + b);
        }
      }
      await Future<void>.delayed(const Duration(milliseconds: 40));
    }
    final status = await synth.status();
    await analysis.stop();
    final stopped = await synth.stop();

    expect(
      records,
      greaterThan(50),
      reason: 'mix analysis publishes 10 ms records',
    );
    expect(
      peak,
      greaterThan(0.05),
      reason: 'analysis observes the synth output',
    );
    expect(bandEnergy, greaterThan(0), reason: 'log bands are populated');
    expect(status.eventsRejected, 0);
    expect(synth.failedCommands, 0);
    expect(stopped?.state, 'stopped');

    final evidence = {
      'endpoint': status.outputEndpointName,
      'low_latency': status.lowLatency,
      'engine_period_frames': status.enginePeriodFrames,
      'buffer_frames': status.bufferFrames,
      'underruns': status.underruns,
      'p99_callback_us': status.p99CallbackMicroseconds,
      'dart_note_call': _summary(callCost),
      'channel_round_trip': _summary(roundTrip),
      'native_queue_wait': _latency(status.queueLatency),
      'native_clock_output': _latency(status.outputLatency),
      'analysis_records': records,
      'analysis_resyncs': resyncs,
      'analysis_peak': peak,
    };
    // ignore: avoid_print
    print('SYNTH_EVIDENCE ${jsonEncode(evidence)}');
  });
}
