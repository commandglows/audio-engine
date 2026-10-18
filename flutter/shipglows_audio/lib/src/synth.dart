import 'dart:async';

import '../shipglows_audio_platform_interface.dart';
import 'output_analysis.dart';

/// Melodic target of a synthesizer note.
enum ShipglowsSynthInstrument {
  /// FM piano: notes sustain while held and release on note-off.
  piano,

  /// General MIDI drum kit: 36 bass drum, 38 snare, 39 clap, 42/44 closed
  /// hi-hat, 46 open hi-hat (choked by any later hi-hat). Drums ring out and
  /// ignore note-off.
  drums;

  /// Wire name used by the native synth API.
  String get wireName => name;

  static ShipglowsSynthInstrument fromWire(Object? value) => value == 'drums'
      ? ShipglowsSynthInstrument.drums
      : ShipglowsSynthInstrument.piano;
}

/// Latency distribution measured natively, in microseconds. Percentiles are
/// upper bounds of 250 µs histogram buckets.
class ShipglowsSynthLatency {
  const ShipglowsSynthLatency({
    this.count = 0,
    this.minMicroseconds = 0,
    this.meanMicroseconds = 0,
    this.p50Microseconds = 0,
    this.p95Microseconds = 0,
    this.p99Microseconds = 0,
    this.maxMicroseconds = 0,
  });

  factory ShipglowsSynthLatency.fromMap(Object? value) {
    final map = value is Map<Object?, Object?>
        ? value
        : const <Object?, Object?>{};
    int read(String key) => (map[key] as num?)?.toInt() ?? 0;
    return ShipglowsSynthLatency(
      count: read('count'),
      minMicroseconds: read('minMicroseconds'),
      meanMicroseconds: read('meanMicroseconds'),
      p50Microseconds: read('p50Microseconds'),
      p95Microseconds: read('p95Microseconds'),
      p99Microseconds: read('p99Microseconds'),
      maxMicroseconds: read('maxMicroseconds'),
    );
  }

  final int count;
  final int minMicroseconds;
  final int meanMicroseconds;
  final int p50Microseconds;
  final int p95Microseconds;
  final int p99Microseconds;
  final int maxMicroseconds;
}

/// Snapshot of the native synthesizer and of the shared output it plays on.
class ShipglowsSynthStatus {
  const ShipglowsSynthStatus({
    required this.generation,
    required this.state,
    this.errorCode = '',
    this.nativeErrorHresult = 0,
    this.instrument = ShipglowsSynthInstrument.piano,
    this.activeVoices = 0,
    this.peakActiveVoices = 0,
    this.eventsPosted = 0,
    this.eventsRejected = 0,
    this.eventsApplied = 0,
    this.eventsIgnored = 0,
    this.stolenVoices = 0,
    this.queueLatency = const ShipglowsSynthLatency(),
    this.outputLatency = const ShipglowsSynthLatency(),
    this.playbackAttached = false,
    this.lowLatency = false,
    this.sampleRate = 0,
    this.bufferFrames = 0,
    this.enginePeriodFrames = 0,
    this.streamLatencyMicroseconds = 0,
    this.queuedFrames,
    this.callbacks = 0,
    this.underruns = 0,
    this.p99CallbackMicroseconds = 0,
    this.maxCallbackMicroseconds = 0,
    this.outputEndpointName = '',
  });

  factory ShipglowsSynthStatus.fromMap(Map<Object?, Object?> map) {
    int read(String key) => (map[key] as num?)?.toInt() ?? 0;
    return ShipglowsSynthStatus(
      generation: read('generation'),
      state: map['state'] as String? ?? 'unknown',
      errorCode: map['errorCode'] as String? ?? '',
      nativeErrorHresult: read('nativeErrorHresult'),
      instrument: ShipglowsSynthInstrument.fromWire(map['instrument']),
      activeVoices: read('activeVoices'),
      peakActiveVoices: read('peakActiveVoices'),
      eventsPosted: read('eventsPosted'),
      eventsRejected: read('eventsRejected'),
      eventsApplied: read('eventsApplied'),
      eventsIgnored: read('eventsIgnored'),
      stolenVoices: read('stolenVoices'),
      queueLatency: ShipglowsSynthLatency.fromMap(map['queueLatency']),
      outputLatency: ShipglowsSynthLatency.fromMap(map['outputLatency']),
      playbackAttached: map['playbackAttached'] as bool? ?? false,
      lowLatency: map['lowLatency'] as bool? ?? false,
      sampleRate: read('sampleRate'),
      bufferFrames: read('bufferFrames'),
      enginePeriodFrames: read('enginePeriodFrames'),
      streamLatencyMicroseconds: read('streamLatencyMicroseconds'),
      queuedFrames: (map['queuedFrames'] as num?)?.toInt(),
      callbacks: read('callbacks'),
      underruns: read('underruns'),
      p99CallbackMicroseconds: read('p99CallbackMicroseconds'),
      maxCallbackMicroseconds: read('maxCallbackMicroseconds'),
      outputEndpointName: map['outputEndpointName'] as String? ?? '',
    );
  }

  final int generation;

  /// `running`, `stopped` or `error` (shared output failure).
  final String state;
  final String errorCode;
  final int nativeErrorHresult;
  final ShipglowsSynthInstrument instrument;
  final int activeVoices;
  final int peakActiveVoices;
  final int eventsPosted;

  /// Events refused because the native event queue was full.
  final int eventsRejected;
  final int eventsApplied;

  /// Drum notes the kit does not render and invalid values.
  final int eventsIgnored;
  final int stolenVoices;

  /// From the native post to the start of the audio block that applied it.
  final ShipglowsSynthLatency queueLatency;

  /// From the native post to the endpoint presentation of the onset, as
  /// estimated by the device clock. Includes driver and Bluetooth delays.
  final ShipglowsSynthLatency outputLatency;

  /// Whether a file-playback session is mixed into the same output.
  final bool playbackAttached;

  /// Whether the output uses the IAudioClient3 low-latency engine period.
  final bool lowLatency;
  final int sampleRate;
  final int bufferFrames;
  final int enginePeriodFrames;
  final int streamLatencyMicroseconds;

  /// Frames submitted but not yet presented at the last device clock sample.
  final int? queuedFrames;
  final int callbacks;
  final int underruns;
  final int p99CallbackMicroseconds;
  final int maxCallbackMicroseconds;
  final String outputEndpointName;
}

/// Real-time synthesizer session on the shared native output.
///
/// Note, click and voice commands return immediately: they are sent without
/// waiting for the platform reply, so they are safe to call from timers. The
/// native side mixes the voices with any file playback and limits the sum.
/// Commands issued while no session is open are dropped and counted.
class ShipglowsSynth {
  // The public parameter name stays non-private.
  // ignore: prefer_initializing_formals
  ShipglowsSynth({ShipglowsAudioPlatform? platform}) : _platform = platform;

  static const int synthApiVersion = 1;
  static int _lastGeneration = 0;

  final ShipglowsAudioPlatform? _platform;
  int? _generation;
  int _droppedCommands = 0;
  int _failedCommands = 0;
  int _rejectedCommands = 0;

  ShipglowsAudioPlatform get _channel =>
      _platform ?? ShipglowsAudioPlatform.instance;

  /// Generation of the open session, or null when none is open.
  int? get generation => _generation;
  bool get isOpen => _generation != null;

  /// Commands issued without an open session.
  int get droppedCommands => _droppedCommands;

  /// Commands the platform answered with an error (for example a stale
  /// session after the output failed).
  int get failedCommands => _failedCommands;

  /// Commands refused because the native queue was full.
  int get rejectedCommands => _rejectedCommands;

  // Wall-clock seeding keeps generations increasing across hot restarts,
  // which reset Dart statics but not the native plugin.
  static int _nextGeneration() {
    final now = DateTime.now().microsecondsSinceEpoch;
    _lastGeneration = now > _lastGeneration ? now : _lastGeneration + 1;
    return _lastGeneration;
  }

  /// Opens a session, starting the shared output if nothing else plays.
  /// Starting again while open returns the current status.
  Future<ShipglowsSynthStatus> start({
    ShipglowsSynthInstrument instrument = ShipglowsSynthInstrument.piano,
  }) async {
    final generation = _generation ?? _nextGeneration();
    _generation = generation;
    try {
      return await _channel.startSynth(
        generation: generation,
        instrument: instrument,
      );
    } catch (_) {
      if (_generation == generation) _generation = null;
      rethrow;
    }
  }

  /// Starts MIDI note [midi] (0 to 127) at [velocity] (0 to 1). [instrument]
  /// overrides the current voice for this note only.
  void noteOn(
    int midi, {
    double velocity = 0.8,
    ShipglowsSynthInstrument? instrument,
  }) {
    RangeError.checkValueInInterval(midi, 0, 127, 'midi');
    final generation = _generation;
    if (generation == null) {
      _droppedCommands++;
      return;
    }
    final clamped = velocity.isNaN ? 0.0 : velocity.clamp(0.0, 1.0).toDouble();
    _send(
      _channel.synthNoteOn(
        generation: generation,
        midi: midi,
        velocity: clamped,
        instrument: instrument,
      ),
    );
  }

  /// Releases MIDI note [midi]; drum voices ignore note-off.
  void noteOff(int midi, {ShipglowsSynthInstrument? instrument}) {
    RangeError.checkValueInInterval(midi, 0, 127, 'midi');
    final generation = _generation;
    if (generation == null) {
      _droppedCommands++;
      return;
    }
    _send(
      _channel.synthNoteOff(
        generation: generation,
        midi: midi,
        instrument: instrument,
      ),
    );
  }

  /// Plays one metronome click; [accent] is higher and louder.
  void click({required bool accent}) {
    final generation = _generation;
    if (generation == null) {
      _droppedCommands++;
      return;
    }
    _send(_channel.synthClick(generation: generation, accent: accent));
  }

  /// Changes the voice used by notes that do not name an instrument.
  void setVoice(ShipglowsSynthInstrument instrument) {
    final generation = _generation;
    if (generation == null) {
      _droppedCommands++;
      return;
    }
    _send(
      _channel.synthSetVoice(generation: generation, instrument: instrument),
    );
  }

  Future<ShipglowsSynthStatus> status() {
    final generation = _generation;
    if (generation == null) {
      return Future.error(StateError('No synth session is open.'));
    }
    return _channel.getSynthStatus(generation: generation);
  }

  /// Fades every voice out and closes the session. The shared output stops
  /// when no file playback uses it. Returns null when no session was open.
  Future<ShipglowsSynthStatus?> stop() async {
    final generation = _generation;
    if (generation == null) return null;
    _generation = null;
    return _channel.stopSynth(generation: generation);
  }

  /// Opens a pollable analysis stream of the mixed output (synth plus any
  /// playback), fenced by this session.
  Future<ShipglowsOutputAnalysis> startAnalysis({
    ShipglowsAnalysisProfile profile = ShipglowsAnalysisProfile.music,
    int featureMask = 11,
    ShipglowsVisualTransform transform = const ShipglowsVisualTransform(),
  }) async {
    final generation = _generation;
    if (generation == null) throw StateError('Start the synth session first.');
    final analysis = ShipglowsOutputAnalysis(platform: _platform);
    await analysis.start(
      generation: generation,
      source: ShipglowsOutputAnalysis.outputMixSource,
      profile: profile,
      featureMask: featureMask,
      transform: transform,
    );
    return analysis;
  }

  void _send(Future<bool> command) {
    unawaited(
      command.then(
        (accepted) {
          if (!accepted) _rejectedCommands++;
        },
        onError: (Object _) {
          _failedCommands++;
        },
      ),
    );
  }
}
