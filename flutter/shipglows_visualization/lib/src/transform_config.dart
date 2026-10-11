import 'dart:convert';

import 'package:crypto/crypto.dart';

const defaultVisualTransformConfig = <String, Object?>{
  'version': 1,
  'sensitivity_db': 0,
  'amplitude_gamma': 0.8,
  'attack_ms': 20,
  'release_ms': 180,
  'spectrum_floor_db': -80,
  'spectrum_ceiling_db': 0,
};

/// Validates and hashes the effective v1 transform config using sorted keys.
String visualTransformDigest(Map<String, Object?> config) {
  final canonical = visualTransformCanonicalJson(config);
  return sha256.convert(utf8.encode(canonical)).toString();
}

String visualTransformCanonicalJson(Map<String, Object?> config) {
  const expected = <String>{
    'version',
    'sensitivity_db',
    'amplitude_gamma',
    'attack_ms',
    'release_ms',
    'spectrum_floor_db',
    'spectrum_ceiling_db',
  };
  if (config.length != expected.length ||
      !config.keys.toSet().containsAll(expected)) {
    throw const FormatException('Transform config keys do not match v1.');
  }
  if (config['version'] != 1) {
    throw const FormatException('Transform config version must be 1.');
  }
  final sensitivity = _finiteNumber(config, 'sensitivity_db');
  final gamma = _finiteNumber(config, 'amplitude_gamma');
  final attack = _finiteNumber(config, 'attack_ms');
  final release = _finiteNumber(config, 'release_ms');
  final floor = _finiteNumber(config, 'spectrum_floor_db');
  final ceiling = _finiteNumber(config, 'spectrum_ceiling_db');
  if (sensitivity < -24 ||
      sensitivity > 24 ||
      gamma < .25 ||
      gamma > 2 ||
      attack < 1 ||
      attack > 200 ||
      release < 10 ||
      release > 2000 ||
      floor < -120 ||
      floor > -20 ||
      ceiling < -6 ||
      ceiling > 12 ||
      floor >= ceiling) {
    throw const FormatException('Transform config value is outside v1 bounds.');
  }

  final keys = expected.toList()..sort();
  return '{${keys.map((key) => '${jsonEncode(key)}:${_canonicalNumber(config[key] as num)}').join(',')}}';
}

double _finiteNumber(Map<String, Object?> config, String key) {
  final value = config[key];
  if (value is! num || !value.isFinite) {
    throw FormatException('$key must be a finite number.');
  }
  return value.toDouble();
}

String _canonicalNumber(num input) {
  final value = input.toDouble();
  if (!value.isFinite) {
    throw const FormatException('Nonfinite transform number.');
  }
  if (value == 0) return '0';
  if (value == value.truncateToDouble()) return value.toInt().toString();

  var token = value.abs().toString();
  var exponent = 0;
  final exponentIndex = token.indexOf(RegExp('[eE]'));
  if (exponentIndex >= 0) {
    exponent = int.parse(token.substring(exponentIndex + 1));
    token = token.substring(0, exponentIndex);
  }
  final pointIndex = token.indexOf('.');
  var decimalPosition = pointIndex < 0 ? token.length : pointIndex;
  var digits = token.replaceAll('.', '');
  while (digits.length > 1 && digits.startsWith('0')) {
    digits = digits.substring(1);
    decimalPosition--;
  }
  while (digits.length > 1 && digits.endsWith('0')) {
    digits = digits.substring(0, digits.length - 1);
  }
  final scientificExponent = decimalPosition + exponent - 1;
  final magnitude = value.abs();
  late final String body;
  if (magnitude >= 1e-6 && magnitude < 1e21) {
    final point = decimalPosition + exponent;
    if (point <= 0) {
      body = '0.${List<String>.filled(-point, '0').join()}$digits';
    } else if (point >= digits.length) {
      body = '$digits${List<String>.filled(point - digits.length, '0').join()}';
    } else {
      body = '${digits.substring(0, point)}.${digits.substring(point)}';
    }
  } else {
    body =
        '${digits[0]}${digits.length > 1 ? '.${digits.substring(1)}' : ''}'
        'e${scientificExponent >= 0 ? '+' : ''}$scientificExponent';
  }
  return value < 0 ? '-$body' : body;
}
