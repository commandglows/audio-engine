import 'dart:async';
import 'dart:io';

import 'package:flutter/material.dart';
import 'package:shipglows_audio/shipglows_audio.dart';

void main() => runApp(const MyApp());

class MyApp extends StatefulWidget {
  const MyApp({super.key});

  @override
  State<MyApp> createState() => _MyAppState();
}

class _MyAppState extends State<MyApp> {
  final _engine = ShipglowsAudio();
  String _engineStatus = 'Loading native audio engine…';
  ShipglowsAudioCaptureStatus? _capture;
  String? _sessionDirectory;
  String? _failure;
  Timer? _statusTimer;
  List<ShipglowsAudioInputDevice> _inputDevices = const [];
  int? _selectedInputDeviceId;

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
    super.dispose();
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
