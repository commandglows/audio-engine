import 'dart:math' as math;

import 'speech_window.dart';
import 'visual_record.dart';

enum SceneMode {
  spectrumBars,
  musicOscilloscope,
  speechEnvelope,
  immersiveRadial,
  particles2d,
}

enum ParticleEnergySource { levels, spectrum }

enum SceneMotion { full, reduced, static }

enum VisualizationStatus {
  live,
  static,
  warmingUp,
  paused,
  ended,
  gap,
  unavailable,
  failed,
  off,
}

enum SceneColorRole { primary, secondary, accent }

final class SceneColor {
  const SceneColor(this.red, this.green, this.blue, [this.alpha = 1]);
  final double red;
  final double green;
  final double blue;
  final double alpha;

  bool get valid => [
    red,
    green,
    blue,
    alpha,
  ].every((value) => value.isFinite && value >= 0 && value <= 1);
}

/// Product-owned shape and spacing tokens, supplied beside the six role colors.
final class SceneGeometry {
  SceneGeometry({
    required this.paddingPx,
    required this.strokeWidthPx,
    required this.barGapFraction,
    required this.barCornerRadiusPx,
    required this.particleRadiusScale,
  }) {
    if (!paddingPx.isFinite || paddingPx < 0 || paddingPx > 128) {
      throw ArgumentError.value(paddingPx, 'paddingPx');
    }
    if (!strokeWidthPx.isFinite || strokeWidthPx < .5 || strokeWidthPx > 8) {
      throw ArgumentError.value(strokeWidthPx, 'strokeWidthPx');
    }
    if (!barGapFraction.isFinite || barGapFraction < 0 || barGapFraction > .8) {
      throw ArgumentError.value(barGapFraction, 'barGapFraction');
    }
    if (!barCornerRadiusPx.isFinite ||
        barCornerRadiusPx < 0 ||
        barCornerRadiusPx > 32) {
      throw ArgumentError.value(barCornerRadiusPx, 'barCornerRadiusPx');
    }
    if (!particleRadiusScale.isFinite ||
        particleRadiusScale < .25 ||
        particleRadiusScale > 2) {
      throw ArgumentError.value(particleRadiusScale, 'particleRadiusScale');
    }
  }

  final double paddingPx;
  final double strokeWidthPx;
  final double barGapFraction;
  final double barCornerRadiusPx;
  final double particleRadiusScale;
}

/// Measured Flutter logical-pixel viewport accepted by the shared renderer.
final class SceneViewport {
  SceneViewport({required this.width, required this.height}) {
    if (!supports(width: width, height: height)) {
      throw ArgumentError('Viewport dimensions are outside supported bounds.');
    }
  }

  final double width;
  final double height;

  double get aspectRatio => width / height;

  static bool supports({required double width, required double height}) =>
      width.isFinite &&
      height.isFinite &&
      width > 0 &&
      height > 0 &&
      width <= 8192 &&
      height <= 8192 &&
      width / height >= .25 &&
      width / height <= 4;

  static bool supportsGeometry({
    required double width,
    required double height,
    required double paddingPx,
  }) =>
      supports(width: width, height: height) &&
      paddingPx.isFinite &&
      paddingPx >= 0 &&
      2 * paddingPx < math.min(width, height);
}

/// Product-owned semantic color bindings; the shared package chooses no palette.
final class ScenePalette {
  ScenePalette({
    required this.background,
    required this.surface,
    required this.primary,
    required this.secondary,
    required this.accent,
    required this.foreground,
    required this.geometry,
  }) {
    if (![
      background,
      surface,
      primary,
      secondary,
      accent,
      foreground,
    ].every((color) => color.valid)) {
      throw ArgumentError('All six semantic roles require valid sRGB colors.');
    }
  }

  final SceneColor background;
  final SceneColor surface;
  final SceneColor primary;
  final SceneColor secondary;
  final SceneColor accent;
  final SceneColor foreground;
  final SceneGeometry geometry;

  SceneColor color(SceneColorRole role) => switch (role) {
    SceneColorRole.primary => primary,
    SceneColorRole.secondary => secondary,
    SceneColorRole.accent => accent,
  };
}

final class SceneConfig {
  SceneConfig({
    required this.mode,
    required this.profile,
    required this.featureMask,
    required this.transformDigest,
    required this.intensity,
    required this.motion,
    this.seed = 0,
    this.barCount = 64,
    this.spokeCount = 64,
    this.ringCount = 3,
    this.particleCount = 128,
    this.energySource = ParticleEnergySource.levels,
    this.timeSeconds = 0,
  }) {
    if (!intensity.isFinite || intensity < 0 || intensity > 1) {
      throw ArgumentError.value(intensity, 'intensity');
    }
    if (transformDigest.length != 64 ||
        !RegExp(r'^[0-9a-f]{64}$').hasMatch(transformDigest)) {
      throw ArgumentError(
        'Transform digest must be 64 lowercase hex characters.',
      );
    }
    if (seed < 0 || seed > 0xffffffff) {
      throw ArgumentError.value(seed, 'seed');
    }
    if (!timeSeconds.isFinite || timeSeconds < 0) {
      throw ArgumentError.value(timeSeconds, 'timeSeconds');
    }
    final validFeatureMask = profile == AnalysisProfile.music
        ? featureMask == 3 || featureMask == 11
        : featureMask == 5 || featureMask == 13;
    if (!validFeatureMask) {
      throw ArgumentError('Profile and feature mask are not a supported pair.');
    }
    if (mode == SceneMode.spectrumBars &&
        (profile != AnalysisProfile.music ||
            (featureMask & AnalysisMask.spectrum) == 0 ||
            barCount != 64)) {
      throw ArgumentError('Spectrum bars require music spectrum and 64 bars.');
    }
    if (mode == SceneMode.musicOscilloscope &&
        (profile != AnalysisProfile.music ||
            (featureMask & AnalysisMask.oscilloscope) == 0)) {
      throw ArgumentError('Music waveform requires the music envelope.');
    }
    if (mode == SceneMode.speechEnvelope &&
        (profile != AnalysisProfile.speech ||
            (featureMask & AnalysisMask.speechEnvelope) == 0)) {
      throw ArgumentError('Speech waveform requires the speech envelope.');
    }
    if (mode == SceneMode.immersiveRadial &&
        (profile != AnalysisProfile.music ||
            (featureMask & AnalysisMask.spectrum) == 0 ||
            spokeCount != 64 ||
            ringCount != 3)) {
      throw ArgumentError(
        'Radial scenes require music spectrum, 64 spokes, and 3 rings.',
      );
    }
    if (mode == SceneMode.particles2d &&
        (particleCount < 1 ||
            particleCount > 4096 ||
            (profile == AnalysisProfile.music &&
                (featureMask & AnalysisMask.levels) == 0) ||
            (profile == AnalysisProfile.speech &&
                (featureMask & AnalysisMask.levels) == 0) ||
            (energySource == ParticleEnergySource.spectrum &&
                (featureMask & AnalysisMask.spectrum) == 0))) {
      throw ArgumentError('Particle scene features or count are unsupported.');
    }
  }

  final SceneMode mode;
  final AnalysisProfile profile;
  final int featureMask;
  final String transformDigest;
  final double intensity;
  final SceneMotion motion;
  final int seed;
  final int barCount;
  final int spokeCount;
  final int ringCount;
  final int particleCount;
  final ParticleEnergySource energySource;
  final double timeSeconds;
}

final class SceneBar {
  const SceneBar(this.index, this.x, this.height, this.role);
  final int index;
  final double x;
  final double height;
  final SceneColorRole role;
}

final class MusicWaveSpan {
  const MusicWaveSpan(
    this.index,
    this.x,
    this.leftMin,
    this.leftMax,
    this.rightMin,
    this.rightMax,
  );
  final int index;
  final double x;
  final double leftMin;
  final double leftMax;
  final double rightMin;
  final double rightMax;
}

final class SpeechWaveBin {
  const SpeechWaveBin(this.index, this.x, this.rms, this.peak, this.valid);
  final int index;
  final double x;
  final double rms;
  final double peak;
  final bool valid;
}

final class RadialSpoke {
  const RadialSpoke(
    this.index,
    this.angle,
    this.innerRadius,
    this.outerRadius,
    this.opacity,
    this.role,
  );
  final int index;
  final double angle;
  final double innerRadius;
  final double outerRadius;
  final double opacity;
  final SceneColorRole role;
}

final class RadialRing {
  const RadialRing(this.index, this.radius, this.role);
  final int index;
  final double radius;
  final SceneColorRole role;
}

final class ParticleState {
  const ParticleState(
    this.index,
    this.x,
    this.y,
    this.radius,
    this.opacity,
    this.role,
  );
  final int index;
  final double x;
  final double y;
  final double radius;
  final double opacity;
  final SceneColorRole role;
}

final class SceneState {
  SceneState({
    required this.status,
    required this.label,
    required this.intensity,
    required this.validSuffixCount,
    this.baseline = false,
    List<SceneBar> bars = const [],
    List<MusicWaveSpan> musicWave = const [],
    List<SpeechWaveBin> speechWave = const [],
    List<RadialSpoke> radialSpokes = const [],
    List<RadialRing> radialRings = const [],
    List<ParticleState> particles = const [],
    this.radialFieldStrength = 0,
    this.frozenFeatureTimeSeconds,
  }) : bars = List.unmodifiable(bars),
       musicWave = List.unmodifiable(musicWave),
       speechWave = List.unmodifiable(speechWave),
       radialSpokes = List.unmodifiable(radialSpokes),
       radialRings = List.unmodifiable(radialRings),
       particles = List.unmodifiable(particles);

  final VisualizationStatus status;
  final String label;
  final double intensity;
  final int validSuffixCount;
  final bool baseline;
  final List<SceneBar> bars;
  final List<MusicWaveSpan> musicWave;
  final List<SpeechWaveBin> speechWave;
  final List<RadialSpoke> radialSpokes;
  final List<RadialRing> radialRings;
  final List<ParticleState> particles;
  final double radialFieldStrength;
  final double? frozenFeatureTimeSeconds;

  SceneState withLabel(String value) => SceneState(
    status: status,
    label: value,
    intensity: intensity,
    validSuffixCount: validSuffixCount,
    baseline: baseline,
    bars: bars,
    musicWave: musicWave,
    speechWave: speechWave,
    radialSpokes: radialSpokes,
    radialRings: radialRings,
    particles: particles,
    radialFieldStrength: radialFieldStrength,
    frozenFeatureTimeSeconds: frozenFeatureTimeSeconds,
  );
}

/// Stateless scene transform. It consumes only already-transformed records.
final class SceneEvaluator {
  const SceneEvaluator();

  SceneState evaluate({
    required SceneConfig config,
    required VisualizationStatus status,
    VisualRecord? record,
    SpeechWindow? speechWindow,
    double? timeSeconds,
  }) {
    final effectiveStatus =
        config.motion == SceneMotion.static &&
            (status == VisualizationStatus.live ||
                status == VisualizationStatus.paused)
        ? VisualizationStatus.static
        : status;
    if (effectiveStatus == VisualizationStatus.off) {
      return SceneState(
        status: effectiveStatus,
        label: 'Visualization off',
        intensity: config.intensity,
        validSuffixCount: 0,
      );
    }
    if (config.intensity == 0) {
      return _baseline(
        config,
        effectiveStatus,
        speechWindow?.validSuffixCount ?? 0,
      );
    }
    if (config.motion == SceneMotion.static && record == null) {
      return _baseline(
        config,
        VisualizationStatus.unavailable,
        0,
        label: 'user_static_unavailable',
      );
    }
    if ((effectiveStatus != VisualizationStatus.live &&
            effectiveStatus != VisualizationStatus.static &&
            effectiveStatus != VisualizationStatus.paused) ||
        record == null) {
      return SceneState(
        status: effectiveStatus,
        label: _statusLabel(effectiveStatus),
        intensity: config.intensity,
        validSuffixCount: speechWindow?.validSuffixCount ?? 0,
      );
    }
    if (record.profile != config.profile ||
        record.featureMask != config.featureMask ||
        record.transformDigest != config.transformDigest) {
      if (effectiveStatus == VisualizationStatus.static) {
        return _baseline(
          config,
          VisualizationStatus.unavailable,
          0,
          label: 'user_static_unavailable',
        );
      }
      return SceneState(
        status: VisualizationStatus.unavailable,
        label: 'Visualization data unavailable',
        intensity: config.intensity,
        validSuffixCount: 0,
      );
    }

    final featureTime = effectiveStatus == VisualizationStatus.static
        ? (record.centerFrame - record.originFrame).toDouble() / 48000
        : null;
    String sceneLabel(String value) => switch (effectiveStatus) {
      VisualizationStatus.static =>
        'user_static at ${featureTime!.toStringAsFixed(3)} seconds · $value',
      VisualizationStatus.paused => 'Audio visualization paused · $value',
      _ => value,
    };
    SceneState unavailableState() =>
        effectiveStatus == VisualizationStatus.static
        ? _baseline(
            config,
            VisualizationStatus.unavailable,
            0,
            label: 'user_static_unavailable',
          )
        : _unavailable(config);

    switch (config.mode) {
      case SceneMode.spectrumBars:
        final bands = record.visualBands;
        if (bands == null) return unavailableState();
        return SceneState(
          status: effectiveStatus,
          label: sceneLabel('Audio spectrum bars'),
          intensity: config.intensity,
          validSuffixCount: 0,
          frozenFeatureTimeSeconds: featureTime,
          bars: List.generate(
            64,
            (index) => SceneBar(
              index,
              (index + .5) / 64,
              config.intensity * bands[index],
              SceneColorRole.values[index % 3],
            ),
            growable: false,
          ),
        );
      case SceneMode.musicOscilloscope:
        final envelope = record.visualEnvelope;
        if (envelope == null) return unavailableState();
        return SceneState(
          status: effectiveStatus,
          label: sceneLabel('Music waveform, 42.7 millisecond oscilloscope'),
          intensity: config.intensity,
          validSuffixCount: 0,
          frozenFeatureTimeSeconds: featureTime,
          musicWave: List.generate(64, (index) {
            final offset = index * 4;
            final scale = .20 * config.intensity;
            return MusicWaveSpan(
              index,
              (index + .5) / 64,
              .25 - scale * envelope[offset],
              .25 - scale * envelope[offset + 1],
              .75 - scale * envelope[offset + 2],
              .75 - scale * envelope[offset + 3],
            );
          }, growable: false),
        );
      case SceneMode.speechEnvelope:
        if (speechWindow == null ||
            speechWindow.streamId != record.streamId ||
            speechWindow.sourceIdentity != record.sourceIdentity ||
            speechWindow.originFrame != record.originFrame ||
            speechWindow.continuityId != record.continuityId ||
            speechWindow.endpointCenterFrame != record.centerFrame) {
          return unavailableState();
        }
        return SceneState(
          status: effectiveStatus,
          label: sceneLabel('Speech envelope, 1.5 second window'),
          intensity: config.intensity,
          validSuffixCount: speechWindow.validSuffixCount,
          frozenFeatureTimeSeconds: featureTime,
          speechWave: List.generate(speechWindowSize, (index) {
            final valid =
                index >= speechWindowSize - speechWindow.validSuffixCount;
            return SpeechWaveBin(
              index,
              (index + .5) / speechWindowSize,
              .35 * config.intensity * speechWindow.visualRms[index],
              .35 * config.intensity * speechWindow.visualPeak[index],
              valid,
            );
          }, growable: false),
        );
      case SceneMode.immersiveRadial:
        final bands = record.visualBands;
        if (bands == null) return unavailableState();
        final level = record.visualLevels[5];
        final time = config.motion == SceneMotion.full
            ? timeSeconds ?? config.timeSeconds
            : 0.0;
        return SceneState(
          status: effectiveStatus,
          label: sceneLabel('Immersive radial audio scene'),
          intensity: config.intensity,
          validSuffixCount: 0,
          frozenFeatureTimeSeconds: featureTime,
          radialFieldStrength:
              .10 + .20 * config.intensity * clampVisual(level),
          radialSpokes: List.generate(config.spokeCount, (index) {
            final energy = clampVisual(bands[index]);
            return RadialSpoke(
              index,
              2 * math.pi * index / config.spokeCount,
              .18,
              .18 + .18 * config.intensity * energy,
              .25 + .75 * config.intensity * energy,
              SceneColorRole.values[index % 3],
            );
          }, growable: false),
          radialRings: List.generate(config.ringCount, (index) {
            final radius =
                .10 +
                .07 * index +
                (config.motion == SceneMotion.full
                    ? .025 *
                          config.intensity *
                          clampVisual(level) *
                          math.sin(
                            2 * math.pi * time / 4 + 2 * math.pi * index / 3,
                          )
                    : 0);
            return RadialRing(
              index,
              math.max(.05, math.min(.45, radius)),
              SceneColorRole.values[index % 3],
            );
          }, growable: false),
        );
      case SceneMode.particles2d:
        final bands = record.visualBands;
        if (config.energySource == ParticleEnergySource.spectrum &&
            bands == null) {
          return unavailableState();
        }
        final time = config.motion == SceneMotion.full
            ? timeSeconds ?? config.timeSeconds
            : 0.0;
        final rms = clampVisual(record.visualLevels[5]);
        return SceneState(
          status: effectiveStatus,
          label: sceneLabel('Procedural audio particles'),
          intensity: config.intensity,
          validSuffixCount: 0,
          frozenFeatureTimeSeconds: featureTime,
          particles: List.generate(config.particleCount, (index) {
            final energy = config.energySource == ParticleEnergySource.levels
                ? rms
                : clampVisual(bands![index % 64]);
            return _particle(config, index, energy, time);
          }, growable: false),
        );
    }
  }

  static SceneState _unavailable(SceneConfig config) => SceneState(
    status: VisualizationStatus.unavailable,
    label: 'Requested audio feature unavailable',
    intensity: config.intensity,
    validSuffixCount: 0,
  );

  static SceneState _baseline(
    SceneConfig config,
    VisualizationStatus status,
    int validSuffixCount, {
    String label = 'intensity_zero',
  }) {
    switch (config.mode) {
      case SceneMode.spectrumBars:
        return SceneState(
          status: status,
          label: label,
          intensity: 0,
          validSuffixCount: validSuffixCount,
          baseline: true,
          bars: List.generate(
            64,
            (index) => SceneBar(
              index,
              (index + .5) / 64,
              0,
              SceneColorRole.values[index % 3],
            ),
            growable: false,
          ),
        );
      case SceneMode.musicOscilloscope:
        return SceneState(
          status: status,
          label: label,
          intensity: 0,
          validSuffixCount: validSuffixCount,
          baseline: true,
          musicWave: List.generate(
            64,
            (index) =>
                MusicWaveSpan(index, (index + .5) / 64, .25, .25, .75, .75),
            growable: false,
          ),
        );
      case SceneMode.speechEnvelope:
        return SceneState(
          status: status,
          label: label,
          intensity: 0,
          validSuffixCount: validSuffixCount,
          baseline: true,
          speechWave: List.generate(
            speechWindowSize,
            (index) => SpeechWaveBin(
              index,
              (index + .5) / speechWindowSize,
              0,
              0,
              false,
            ),
            growable: false,
          ),
        );
      case SceneMode.immersiveRadial:
        return SceneState(
          status: status,
          label: label,
          intensity: 0,
          validSuffixCount: validSuffixCount,
          baseline: true,
          radialFieldStrength: .10,
          radialSpokes: List.generate(
            config.spokeCount,
            (index) => RadialSpoke(
              index,
              2 * math.pi * index / config.spokeCount,
              .18,
              .18,
              .25,
              SceneColorRole.values[index % 3],
            ),
            growable: false,
          ),
          radialRings: List.generate(
            config.ringCount,
            (index) => RadialRing(
              index,
              .10 + .07 * index,
              SceneColorRole.values[index % 3],
            ),
            growable: false,
          ),
        );
      case SceneMode.particles2d:
        return SceneState(
          status: status,
          label: label,
          intensity: 0,
          validSuffixCount: validSuffixCount,
          baseline: true,
          particles: List.generate(
            config.particleCount,
            (index) => _particle(config, index, 0, 0, intensityZero: true),
            growable: false,
          ),
        );
    }
  }

  static ParticleState _particle(
    SceneConfig config,
    int index,
    double energy,
    double time, {
    bool intensityZero = false,
  }) {
    final seed = config.seed & 0xffffffff;
    final phi = _hash32(seed ^ ((index + 1) & 0xffffffff)) / 4294967296.0;
    final full = config.motion == SceneMotion.full && !intensityZero;
    final z = full ? (time / 2 + phi).floor() : 0;
    final q = full ? time / 2 + phi - z : phi;
    final h = _hash32(
      seed ^ ((index * 0x9e3779b9) & 0xffffffff) ^ (z & 0xffffffff),
    );
    final u = _hash32(h ^ 1) / 4294967296.0;
    final v = _hash32(h ^ 2) / 4294967296.0;
    final influence = intensityZero ? 0.0 : config.intensity * energy;
    if (full) {
      // Full size and opacity over most of the rise; fades only at the edges.
      final pulse = math.min(1.0, 3 * math.sin(math.pi * q));
      final x = (u + (v - .5) * .12 * q) % 1.0;
      return ParticleState(
        index,
        x < 0 ? x + 1 : x,
        1 - q,
        (.002 + .006 * influence) * pulse,
        influence * pulse * pulse,
        SceneColorRole.values[index % 3],
      );
    }
    return ParticleState(
      index,
      u,
      1 - phi,
      intensityZero ? .002 : .002 + .004 * influence,
      intensityZero ? 0 : influence,
      SceneColorRole.values[index % 3],
    );
  }

  static int _hash32(int value) {
    var hash = ((value & 0xffffffff) * 1664525 + 1013904223) & 0xffffffff;
    hash = (hash ^ (hash >> 16)) & 0xffffffff;
    hash = (hash * 22695477 + 1) & 0xffffffff;
    return (hash ^ (hash >> 13)) & 0xffffffff;
  }

  static String _statusLabel(VisualizationStatus status) => switch (status) {
    VisualizationStatus.live => 'Audio visualization live',
    VisualizationStatus.static => 'user_static',
    VisualizationStatus.warmingUp => 'Audio visualization warming up',
    VisualizationStatus.paused => 'Audio visualization paused',
    VisualizationStatus.ended => 'Audio visualization ended',
    VisualizationStatus.gap => 'Audio signal gap',
    VisualizationStatus.unavailable => 'Audio visualization unavailable',
    VisualizationStatus.failed => 'Audio visualization failed',
    VisualizationStatus.off => 'Visualization off',
  };
}
