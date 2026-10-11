import 'dart:math' as math;

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';

import 'scene.dart';

/// Repaints from the supplied notifier without rebuilding or laying out its shell.
final class SharedSceneView extends StatefulWidget {
  const SharedSceneView({
    required this.state,
    required this.palette,
    super.key,
  });

  final ValueListenable<SceneState> state;
  final ScenePalette palette;

  @override
  State<SharedSceneView> createState() => _SharedSceneViewState();
}

final class _SharedSceneViewState extends State<SharedSceneView> {
  late String _semanticLabel;

  @override
  void initState() {
    super.initState();
    _semanticLabel = widget.state.value.label;
    widget.state.addListener(_stateChanged);
  }

  @override
  void didUpdateWidget(covariant SharedSceneView oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (oldWidget.state != widget.state) {
      oldWidget.state.removeListener(_stateChanged);
      widget.state.addListener(_stateChanged);
      _semanticLabel = widget.state.value.label;
    }
  }

  void _stateChanged() {
    final nextLabel = widget.state.value.label;
    if (nextLabel != _semanticLabel) {
      setState(() => _semanticLabel = nextLabel);
    }
  }

  @override
  void dispose() {
    widget.state.removeListener(_stateChanged);
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => LayoutBuilder(
    builder: (context, constraints) {
      final validViewport =
          constraints.hasBoundedWidth &&
          constraints.hasBoundedHeight &&
          SceneViewport.supportsGeometry(
            width: constraints.maxWidth,
            height: constraints.maxHeight,
            paddingPx: widget.palette.geometry.paddingPx,
          );
      return Semantics(
        container: true,
        label: validViewport
            ? _semanticLabel
            : 'Audio visualization unavailable: unsupported viewport',
        child: CustomPaint(
          painter: validViewport
              ? _SharedScenePainter(widget.state, widget.palette)
              : null,
          size: validViewport ? Size.infinite : const Size(1, 1),
        ),
      );
    },
  );
}

final class _SharedScenePainter extends CustomPainter {
  _SharedScenePainter(this.state, this.palette) : super(repaint: state);

  final ValueListenable<SceneState> state;
  final ScenePalette palette;
  late final Color _backgroundColor = _color(palette.background);
  late final Color _foregroundColor = _color(palette.foreground);
  late final Color _primaryColor = _color(palette.primary);
  late final Color _secondaryColor = _color(palette.secondary);
  late final Color _accentColor = _color(palette.accent);

  // A painter is reused across frame notifications. Keep native Paint handles
  // here instead of allocating one for every bar, wave sample, spoke or dot.
  final Paint _baselinePaint = Paint();
  final Paint _barPaint = Paint();
  final Paint _leftWavePaint = Paint();
  final Paint _rightWavePaint = Paint();
  final Paint _speechFillPaint = Paint();
  final Paint _speechOutlinePaint = Paint();
  final Paint _radialPaint = Paint();
  final Paint _radialLinePaint = Paint();
  final Paint _radialRingPaint = Paint();
  final Paint _particlePaint = Paint();

  @override
  void paint(Canvas canvas, Size size) {
    if (!SceneViewport.supportsGeometry(
      width: size.width,
      height: size.height,
      paddingPx: palette.geometry.paddingPx,
    )) {
      return;
    }
    final scene = state.value;
    canvas.drawColor(_backgroundColor, BlendMode.src);
    final padding = palette.geometry.paddingPx;
    final contentSize = Size(
      size.width - 2 * padding,
      size.height - 2 * padding,
    );
    canvas.save();
    canvas.translate(padding, padding);
    canvas.clipRect(Offset.zero & contentSize);
    if (scene.baseline) {
      if (scene.radialSpokes.isNotEmpty) {
        _paintRadial(canvas, contentSize, scene);
        canvas.restore();
        return;
      }
      if (scene.particles.isNotEmpty) {
        _paintParticles(canvas, contentSize, scene);
        canvas.restore();
        return;
      }
      final line = _baselinePaint
        ..color = _foregroundColor
        ..strokeWidth = 1;
      switch (scene.speechWave.isNotEmpty
          ? SceneMode.speechEnvelope
          : scene.musicWave.isNotEmpty
          ? SceneMode.musicOscilloscope
          : scene.radialSpokes.isNotEmpty
          ? SceneMode.immersiveRadial
          : scene.particles.isNotEmpty
          ? SceneMode.particles2d
          : SceneMode.spectrumBars) {
        case SceneMode.spectrumBars:
          canvas.drawLine(
            Offset(0, contentSize.height),
            Offset(contentSize.width, contentSize.height),
            line,
          );
          break;
        case SceneMode.musicOscilloscope:
          canvas.drawLine(
            Offset(0, contentSize.height * .25),
            Offset(contentSize.width, contentSize.height * .25),
            line,
          );
          canvas.drawLine(
            Offset(0, contentSize.height * .75),
            Offset(contentSize.width, contentSize.height * .75),
            line,
          );
          break;
        case SceneMode.speechEnvelope:
          canvas.drawLine(
            Offset(0, contentSize.height * .5),
            Offset(contentSize.width, contentSize.height * .5),
            line,
          );
          break;
        case SceneMode.immersiveRadial:
        case SceneMode.particles2d:
          break;
      }
      canvas.restore();
      return;
    }
    if (scene.status != VisualizationStatus.live &&
        scene.status != VisualizationStatus.static &&
        scene.status != VisualizationStatus.paused) {
      canvas.restore();
      return;
    }
    if (scene.radialSpokes.isNotEmpty) {
      _paintRadial(canvas, contentSize, scene);
    } else if (scene.particles.isNotEmpty) {
      _paintParticles(canvas, contentSize, scene);
    } else if (scene.bars.isNotEmpty) {
      _paintBars(canvas, contentSize);
    } else if (scene.musicWave.isNotEmpty) {
      _paintMusicWave(canvas, contentSize);
    } else {
      _paintSpeechWave(canvas, contentSize);
    }
    canvas.restore();
  }

  void _paintBars(Canvas canvas, Size size) {
    final scene = state.value;
    if (scene.bars.isEmpty) return;
    final step = size.width / scene.bars.length;
    final barWidth = step * (1 - palette.geometry.barGapFraction);
    for (final bar in scene.bars) {
      final height = math.max(
        0.0,
        math.min(size.height, bar.height * size.height),
      );
      final rect = Rect.fromLTWH(
        bar.x * size.width - barWidth / 2,
        size.height - height,
        barWidth,
        height,
      );
      canvas.drawRRect(
        RRect.fromRectAndRadius(
          rect,
          Radius.circular(
            math.min(
              palette.geometry.barCornerRadiusPx,
              math.min(barWidth / 2, height / 2),
            ),
          ),
        ),
        _barPaint..color = _roleColor(bar.role),
      );
    }
  }

  void _paintMusicWave(Canvas canvas, Size size) {
    final scene = state.value;
    final left = _leftWavePaint
      ..color = _primaryColor
      ..strokeWidth = palette.geometry.strokeWidthPx
      ..strokeCap = StrokeCap.round;
    final right = _rightWavePaint
      ..color = _secondaryColor
      ..strokeWidth = palette.geometry.strokeWidthPx
      ..strokeCap = StrokeCap.round;
    for (final span in scene.musicWave) {
      canvas.drawLine(
        Offset(span.x * size.width, span.leftMin * size.height),
        Offset(span.x * size.width, span.leftMax * size.height),
        left,
      );
      canvas.drawLine(
        Offset(span.x * size.width, span.rightMin * size.height),
        Offset(span.x * size.width, span.rightMax * size.height),
        right,
      );
    }
  }

  void _paintSpeechWave(Canvas canvas, Size size) {
    final scene = state.value;
    if (scene.speechWave.isEmpty) return;
    final step = size.width / scene.speechWave.length;
    final fill = _speechFillPaint..color = _primaryColor;
    final outline = _speechOutlinePaint
      ..color = _secondaryColor
      ..strokeWidth = palette.geometry.strokeWidthPx
      ..strokeCap = StrokeCap.round;
    for (final bin in scene.speechWave) {
      if (!bin.valid) continue;
      final x = bin.x * size.width;
      final rms = bin.rms * size.height;
      final peak = bin.peak * size.height;
      canvas.drawRect(
        Rect.fromLTRB(
          x - step * .36,
          size.height * .5 - rms,
          x + step * .36,
          size.height * .5 + rms,
        ),
        fill,
      );
      canvas.drawLine(
        Offset(x, size.height * .5 - peak),
        Offset(x, size.height * .5 + peak),
        outline,
      );
    }
  }

  void _paintRadial(Canvas canvas, Size size, SceneState scene) {
    final center = Offset(size.width / 2, size.height / 2);
    final unit = math.min(size.width, size.height);
    final field = (scene.radialFieldStrength).clamp(0.0, 1.0).toDouble();
    canvas.drawRect(
      Offset.zero & size,
      _radialPaint
        ..shader = RadialGradient(
          center: Alignment.center,
          radius: .5,
          colors: List.generate(
            17,
            (index) => _blendLinear(
              palette.background,
              palette.accent,
              field * (1 - index / 16),
            ),
            growable: false,
          ),
          stops: List.generate(17, (index) => index / 16, growable: false),
        ).createShader(Offset.zero & size),
    );
    for (final spoke in scene.radialSpokes) {
      final inner =
          center +
          Offset(
            math.cos(spoke.angle) * spoke.innerRadius * unit,
            math.sin(spoke.angle) * spoke.innerRadius * unit,
          );
      final outer =
          center +
          Offset(
            math.cos(spoke.angle) * spoke.outerRadius * unit,
            math.sin(spoke.angle) * spoke.outerRadius * unit,
          );
      canvas.drawLine(
        inner,
        outer,
        _radialLinePaint
          ..color = _roleColor(spoke.role).withValues(alpha: spoke.opacity)
          ..strokeWidth = palette.geometry.strokeWidthPx,
      );
    }
    for (final ring in scene.radialRings) {
      canvas.drawCircle(
        center,
        ring.radius * unit,
        _radialRingPaint
          ..color = _roleColor(ring.role).withValues(alpha: .8)
          ..style = PaintingStyle.stroke
          ..strokeWidth = palette.geometry.strokeWidthPx,
      );
    }
  }

  void _paintParticles(Canvas canvas, Size size, SceneState scene) {
    final unit = math.min(size.width, size.height);
    for (final particle in scene.particles) {
      canvas.drawCircle(
        Offset(particle.x * size.width, particle.y * size.height),
        particle.radius * unit * palette.geometry.particleRadiusScale,
        _particlePaint
          ..color = _roleColor(particle.role)
              .withValues(alpha: particle.opacity.clamp(0.0, 1.0)),
      );
    }
  }

  static Color _color(SceneColor value) => Color.fromARGB(
    (value.alpha * 255).round(),
    (value.red * 255).round(),
    (value.green * 255).round(),
    (value.blue * 255).round(),
  );

  Color _roleColor(SceneColorRole role) => switch (role) {
    SceneColorRole.primary => _primaryColor,
    SceneColorRole.secondary => _secondaryColor,
    SceneColorRole.accent => _accentColor,
  };

  static Color _blendLinear(
    SceneColor background,
    SceneColor foreground,
    double t,
  ) {
    double linear(double value) => value <= .04045
        ? value / 12.92
        : math.pow((value + .055) / 1.055, 2.4).toDouble();
    double encoded(double value) => value <= .0031308
        ? value * 12.92
        : 1.055 * math.pow(value, 1 / 2.4).toDouble() - .055;
    final bgAlpha = background.alpha * (1 - t);
    final fgAlpha = foreground.alpha * t;
    final alpha = bgAlpha + fgAlpha;
    if (alpha == 0) return const Color(0x00000000);
    double channel(double bg, double fg) =>
        encoded((linear(bg) * bgAlpha + linear(fg) * fgAlpha) / alpha)
            .clamp(0.0, 1.0)
            .toDouble();
    return Color.fromARGB(
      (alpha * 255).round(),
      (channel(background.red, foreground.red) * 255).round(),
      (channel(background.green, foreground.green) * 255).round(),
      (channel(background.blue, foreground.blue) * 255).round(),
    );
  }

  @override
  bool shouldRepaint(covariant _SharedScenePainter oldDelegate) =>
      oldDelegate.state != state || oldDelegate.palette != palette;
}
