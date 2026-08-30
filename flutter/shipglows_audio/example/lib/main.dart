import 'package:flutter/material.dart';
import 'package:shipglows_audio/shipglows_audio.dart';

void main() {
  runApp(const MyApp());
}

class MyApp extends StatefulWidget {
  const MyApp({super.key});

  @override
  State<MyApp> createState() => _MyAppState();
}

class _MyAppState extends State<MyApp> {
  String _engineStatus = 'Loading native audio engine…';
  final _shipglowsAudioPlugin = ShipglowsAudio();

  @override
  void initState() {
    super.initState();
    _loadEngine();
  }

  // Platform messages are asynchronous, so we initialize in an async method.
  Future<void> _loadEngine() async {
    String engineStatus;
    try {
      final info = await _shipglowsAudioPlugin.getEngineInfo();
      engineStatus =
          '${info.name} ${info.version}\n'
          '${info.platform} · ${info.backend}\n'
          'Native core: ${info.nativeCoreLoaded ? 'loaded' : 'pending'}';
    } on Object catch (error) {
      engineStatus = 'Failed to load the audio engine: $error';
    }

    // If the widget was removed from the tree while the asynchronous platform
    // message was in flight, we want to discard the reply rather than calling
    // setState to update our non-existent appearance.
    if (!mounted) return;

    setState(() {
      _engineStatus = engineStatus;
    });
  }

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      home: Scaffold(
        appBar: AppBar(title: const Text('ShipGlows Audio Engine')),
        body: Center(child: Text(_engineStatus, textAlign: TextAlign.center)),
      ),
    );
  }
}
