import 'dart:async';
import 'dart:io';

import 'package:flutter/material.dart';
import 'package:shipglows_audio/shipglows_audio.dart';
import 'package:shipglows_visualization/shipglows_visualization.dart';

final _visualPalette = ScenePalette(
  background: const SceneColor(.035, .045, .07),
  surface: const SceneColor(.08, .1, .14),
  primary: const SceneColor(.25, .82, .95),
  secondary: const SceneColor(.38, .45, .98),
  accent: const SceneColor(.98, .44, .65),
  foreground: const SceneColor(.9, .94, 1),
  geometry: SceneGeometry(
    paddingPx: 20,
    strokeWidthPx: 2,
    barGapFraction: .2,
    barCornerRadiusPx: 3,
    particleRadiusScale: 1,
  ),
);

void main() => runApp(const MyApp());

class MyApp extends StatefulWidget {
  const MyApp({super.key});

  @override
  State<MyApp> createState() => _MyAppState();
}

class _MyAppState extends State<MyApp> {
  final _engine = ShipglowsAudio();
  final _playbackPath = TextEditingController();
  String _engineStatus = 'Loading native audio engine…';
  ShipglowsAudioCaptureStatus? _capture;
  String? _sessionDirectory;
  String? _failure;
  Timer? _statusTimer;
  List<ShipglowsAudioInputDevice> _inputDevices = const [];
  int? _selectedInputDeviceId;
  ShipglowsAudioPlaybackStatus? _playback;
  AnalysisController? _analysis;
  Timer? _analysisTimer;
  int _generation = 0;
  int _statusPolls = 0;
  bool _playbackStatusInFlight = false;

  bool get _isRecording =>
      _capture?.state == 'recording' || _capture?.state == 'paused';
  bool get _canPause => _capture?.state == 'recording';
  bool get _canResume => _capture?.state == 'paused';

  @override
  void initState() {
    super.initState();
    _loadEngine();
    _loadInputDevices();
  }

  Future<void> _loadInputDevices() async {
    try {
      final devices = await _engine.getInputDevices();
      if (!mounted) return;
      setState(() {
        _inputDevices = devices;
        _selectedInputDeviceId ??= devices
            .where((device) => device.isExternal)
            .firstOrNull
            ?.id;
      });
    } on Object {
      // Device enumeration is currently Android-only. Other platforms retain
      // their native default input without turning this into an engine error.
    }
  }

  @override
  void dispose() {
    _statusTimer?.cancel();
    _analysisTimer?.cancel();
    _playbackPath.dispose();
    final controller = _analysis;
    if (controller != null) unawaited(controller.dispose());
    super.dispose();
  }

  Future<void> _startPlayback() async {
    final path = _playbackPath.text.trim();
    if (path.isEmpty) {
      setState(() => _failure = 'Enter a local audio file path.');
      return;
    }
    await _releaseAnalysis();
    final generation = _generation + 1;
    try {
      final loaded = await _engine.loadPlayback(
        generation: generation,
        localFilePath: path,
      );
      _generation = loaded.generation;
      if (!mounted) return;
      setState(() {
        _playback = loaded;
        _failure = null;
      });

      final transform = Map<String, Object?>.unmodifiable(
        defaultVisualTransformConfig,
      );
      final digest = visualTransformDigest(transform);
      final controller = AnalysisController(
        source: MethodChannelAnalysisSource(),
        generation: loaded.generation,
        transformConfig: transform,
        sceneConfig: SceneConfig(
          mode: SceneMode.spectrumBars,
          profile: AnalysisProfile.music,
          featureMask: 11,
          transformDigest: digest,
          intensity: .65,
          motion: SceneMotion.full,
        ),
      );
      _analysis = controller;
      await controller.start();
      final playing = await _engine.playPlayback(generation: loaded.generation);
      if (!mounted) return;
      setState(() => _playback = playing);
      _analysisTimer?.cancel();
      _statusPolls = 0;
      _analysisTimer = Timer.periodic(const Duration(milliseconds: 33), (_) {
        unawaited(_analysis?.poll());
        _statusPolls++;
        if (_statusPolls >= 15) {
          _statusPolls = 0;
          unawaited(_refreshPlaybackStatus());
        }
      });
    } on Object catch (error) {
      if (mounted) setState(() => _failure = 'Playback start failed: $error');
    }
  }

  Future<void> _refreshPlaybackStatus() async {
    final active = _playback;
    if (active == null || _playbackStatusInFlight) return;
    _playbackStatusInFlight = true;
    try {
      final status = await _engine.getPlaybackStatus(
        generation: active.generation,
      );
      if (!mounted) return;
      setState(() => _playback = status);
      if (status.state == 'completed' || status.state == 'error') {
        await _analysis?.poll();
        _analysisTimer?.cancel();
      }
    } on Object catch (error) {
      if (mounted) setState(() => _failure = 'Playback status failed: $error');
    } finally {
      _playbackStatusInFlight = false;
    }
  }

  Future<void> _pausePlayback() async {
    final active = _playback;
    if (active == null) return;
    try {
      final paused = await _engine.pausePlayback(generation: active.generation);
      if (mounted) setState(() => _playback = paused);
    } on Object catch (error) {
      if (mounted) setState(() => _failure = 'Pause failed: $error');
    }
  }

  Future<void> _resumePlayback() async {
    final active = _playback;
    if (active == null) return;
    try {
      final playing = await _engine.playPlayback(generation: active.generation);
      if (mounted) setState(() => _playback = playing);
      _analysisTimer ??= Timer.periodic(
        const Duration(milliseconds: 33),
        (_) => unawaited(_analysis?.poll()),
      );
    } on Object catch (error) {
      if (mounted) setState(() => _failure = 'Resume failed: $error');
    }
  }

  Future<void> _stopPlayback() async {
    final active = _playback;
    if (active == null) return;
    _analysisTimer?.cancel();
    await _releaseAnalysis();
    try {
      final stopped = await _engine.stopPlayback(
        generation: active.generation + 1,
      );
      _generation = stopped.generation;
      if (mounted) setState(() => _playback = stopped);
    } on Object catch (error) {
      if (mounted) setState(() => _failure = 'Stop failed: $error');
    }
  }

  Future<void> _releaseAnalysis() async {
    _analysisTimer?.cancel();
    _analysisTimer = null;
    final controller = _analysis;
    _analysis = null;
    if (controller != null) await controller.dispose();
  }

  Future<void> _loadEngine() async {
    try {
      final info = await _engine.getEngineInfo();
      if (!mounted) return;
      setState(() {
        _engineStatus =
            '${info.name} ${info.version}\n'
            '${info.platform} · ${info.backend}\n'
            'Native core: ${info.nativeCoreLoaded ? 'loaded' : 'missing'}';
      });
    } on Object catch (error) {
      if (!mounted) return;
      setState(() => _failure = 'Engine load failed: $error');
    }
  }

  Future<void> _start() async {
    final directory = Directory(
      '${Directory.systemTemp.path}${Platform.pathSeparator}'
      'shipglows-audio-${DateTime.now().toUtc().microsecondsSinceEpoch}',
    );
    try {
      await directory.create(recursive: true);
      final status = await _engine.startRecording(
        sessionDirectory: directory.path,
        inputDeviceId: _selectedInputDeviceId,
      );
      if (!mounted) return;
      setState(() {
        _capture = status;
        _sessionDirectory = directory.path;
        _failure = null;
      });
      _statusTimer?.cancel();
      _statusTimer = Timer.periodic(
        const Duration(milliseconds: 500),
        (_) => _refreshStatus(),
      );
    } on Object catch (error) {
      if (!mounted) return;
      setState(() => _failure = 'Start failed: $error');
    }
  }

  Future<void> _selectInputDevice(int? deviceId) async {
    setState(() => _selectedInputDeviceId = deviceId);
    if (!_isRecording || deviceId == null) return;
    try {
      final status = await _engine.selectInputDevice(deviceId);
      if (mounted) setState(() => _capture = status);
    } on Object catch (error) {
      if (mounted) setState(() => _failure = 'Route change failed: $error');
    }
  }

  Future<void> _refreshStatus() async {
    try {
      final status = await _engine.getRecordingStatus();
      if (!mounted) return;
      setState(() => _capture = status);
    } on Object catch (error) {
      if (!mounted) return;
      setState(() => _failure = 'Status failed: $error');
    }
  }

  Future<void> _stop() async {
    _statusTimer?.cancel();
    try {
      final status = await _engine.stopRecording();
      if (!mounted) return;
      setState(() {
        _capture = status;
        _failure = null;
      });
    } on Object catch (error) {
      if (!mounted) return;
      setState(() => _failure = 'Stop failed: $error');
    }
  }

  Future<void> _pause() async {
    final status = await _engine.pauseRecording();
    if (mounted) setState(() => _capture = status);
  }

  Future<void> _resume() async {
    final status = await _engine.resumeRecording();
    if (mounted) setState(() => _capture = status);
  }

  @override
  Widget build(BuildContext context) {
    final capture = _capture;
    return MaterialApp(
      home: Scaffold(
        appBar: AppBar(title: const Text('ShipGlows Audio Engine Lab')),
        body: SafeArea(
          child: ListView(
            padding: const EdgeInsets.all(24),
            children: [
              Text(_engineStatus, textAlign: TextAlign.center),
              const SizedBox(height: 24),
              TextField(
                controller: _playbackPath,
                decoration: const InputDecoration(
                  labelText: 'Local audio file path',
                  hintText: r'C:\Audio\track.wav',
                  border: OutlineInputBorder(),
                ),
              ),
              const SizedBox(height: 8),
              Wrap(
                spacing: 8,
                children: [
                  FilledButton.icon(
                    onPressed: _playback?.state == 'playing'
                        ? null
                        : _startPlayback,
                    icon: const Icon(Icons.graphic_eq),
                    label: const Text('Load and visualize'),
                  ),
                  OutlinedButton(
                    onPressed: _playback?.state == 'playing'
                        ? _pausePlayback
                        : null,
                    child: const Text('Pause playback'),
                  ),
                  OutlinedButton(
                    onPressed: _playback?.state == 'paused'
                        ? _resumePlayback
                        : null,
                    child: const Text('Resume playback'),
                  ),
                  OutlinedButton(
                    onPressed:
                        _playback == null || _playback!.state == 'stopped'
                        ? null
                        : _stopPlayback,
                    child: const Text('Stop playback'),
                  ),
                ],
              ),
              if (_playback != null) ...[
                const SizedBox(height: 8),
                Text(
                  'Playback: ${_playback!.state} · '
                  '${_playback!.positionSeconds.toStringAsFixed(1)} / '
                  '${_playback!.durationSeconds.toStringAsFixed(1)} s · '
                  'underruns ${_playback!.underruns}',
                ),
              ],
              if (_analysis case final controller?) ...[
                const SizedBox(height: 12),
                SizedBox(
                  height: 240,
                  child: SharedSceneView(
                    state: controller.state,
                    palette: _visualPalette,
                  ),
                ),
                Text(controller.state.value.label),
              ],
              const Divider(height: 32),
              if (_inputDevices.isNotEmpty) ...[
                DropdownButtonFormField<int?>(
                  initialValue: _selectedInputDeviceId,
                  decoration: const InputDecoration(labelText: 'Input device'),
                  items: <DropdownMenuItem<int?>>[
                    const DropdownMenuItem<int?>(
                      value: null,
                      child: Text('System default'),
                    ),
                    ..._inputDevices.map(
                      (device) => DropdownMenuItem<int?>(
                        value: device.id,
                        child: Text(
                          '${device.name} · ${device.type}',
                          overflow: TextOverflow.ellipsis,
                        ),
                      ),
                    ),
                  ],
                  onChanged: _selectInputDevice,
                ),
                const SizedBox(height: 16),
              ],
              FilledButton.icon(
                onPressed: _isRecording ? _stop : _start,
                icon: Icon(_isRecording ? Icons.stop : Icons.mic),
                label: Text(_isRecording ? 'Stop' : 'Record'),
              ),
              Row(
                children: [
                  Expanded(
                    child: OutlinedButton(
                      onPressed: _canPause ? _pause : null,
                      child: const Text('Pause'),
                    ),
                  ),
                  const SizedBox(width: 12),
                  Expanded(
                    child: OutlinedButton(
                      onPressed: _canResume ? _resume : null,
                      child: const Text('Resume'),
                    ),
                  ),
                ],
              ),
              const SizedBox(height: 24),
              if (_failure != null)
                Text(_failure!, style: const TextStyle(color: Colors.red)),
              if (capture != null) ...[
                Text('State: ${capture.state}'),
                Text(
                  'Format: ${capture.sampleRate} Hz · '
                  '${capture.channelCount} ch · ${capture.sampleFormat}',
                ),
                Text('Frames captured: ${capture.framesCaptured}'),
                Text('Frames dropped: ${capture.framesDropped}'),
                Text('Discontinuities: ${capture.discontinuities}'),
                Text('Clipped samples: ${capture.clippedSamples}'),
                Text('Device restarts: ${capture.deviceRestarts}'),
                Text('Native xruns: ${capture.nativeXruns}'),
                Text('Ring overflow frames: ${capture.ringOverflowFrames}'),
                Text('Timestamp gap frames: ${capture.timestampGapFrames}'),
                Text('Writer stalls: ${capture.writerStalls}'),
                Text('Route changes: ${capture.routeChanges}'),
                Text('Hardware timestamps: ${capture.hardwareTimestamps}'),
                Text(
                  'Timestamp query failures: '
                  '${capture.timestampQueryFailures}',
                ),
                Text(
                  'Error: ${capture.errorCode.isEmpty ? 'none' : capture.errorCode}',
                ),
                Text('Recovery: ${capture.recoveryAction}'),
              ],
              if (_sessionDirectory != null) ...[
                const SizedBox(height: 16),
                SelectableText('Session: $_sessionDirectory'),
              ],
            ],
          ),
        ),
      ),
    );
  }
}
