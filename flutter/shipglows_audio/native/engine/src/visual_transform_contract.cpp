#include "shipglows/audio/visual_transform_contract.hpp"

#include "shipglows/audio/sha256.hpp"

#include <charconv>
#include <cmath>
#include <stdexcept>
#include <span>
#include <string>

namespace shipglows::audio {
namespace {

std::string canonical_number(double value) {
  if (!std::isfinite(value)) throw std::invalid_argument("nonfinite transform number");
  if (value == 0) return "0";
  char buffer[64]{};
  const auto converted = std::to_chars(buffer, buffer + sizeof(buffer), value,
                                       std::chars_format::general);
  if (converted.ec != std::errc{}) {
    throw std::invalid_argument("transform number encoding failed");
  }
  std::string token(buffer, converted.ptr);
  const bool negative = token.front() == '-';
  if (negative) token.erase(token.begin());
  const auto exponent_at = token.find_first_of("eE");
  int exponent = 0;
  if (exponent_at != std::string::npos) {
    exponent = std::stoi(token.substr(exponent_at + 1));
    token.resize(exponent_at);
  }
  const auto decimal_at = token.find('.');
  int decimal_position = decimal_at == std::string::npos
      ? static_cast<int>(token.size())
      : static_cast<int>(decimal_at);
  std::string digits;
  digits.reserve(token.size());
  for (const char character : token) {
    if (character != '.') digits.push_back(character);
  }
  while (digits.size() > 1 && digits.front() == '0') {
    digits.erase(digits.begin());
    --decimal_position;
  }
  while (digits.size() > 1 && digits.back() == '0') digits.pop_back();
  const int scientific_exponent = decimal_position + exponent - 1;
  std::string body;
  const double magnitude = std::abs(value);
  if (magnitude >= 1e-6 && magnitude < 1e21) {
    const int point = decimal_position + exponent;
    if (point <= 0) {
      body = "0." + std::string(static_cast<std::size_t>(-point), '0') + digits;
    } else if (point >= static_cast<int>(digits.size())) {
      body = digits + std::string(
          static_cast<std::size_t>(point - static_cast<int>(digits.size())), '0');
    } else {
      body = digits.substr(0, static_cast<std::size_t>(point)) + "." +
             digits.substr(static_cast<std::size_t>(point));
    }
    if (const auto dot = body.find('.'); dot != std::string::npos) {
      while (body.back() == '0') body.pop_back();
      if (body.back() == '.') body.pop_back();
    }
  } else {
    body.push_back(digits.front());
    if (digits.size() > 1) body += "." + digits.substr(1);
    body += "e";
    if (scientific_exponent >= 0) body += "+";
    body += std::to_string(scientific_exponent);
  }
  return negative ? "-" + body : body;
}

void validate(const VisualAnalysisConfig& config) {
  if (!std::isfinite(config.sensitivity_db) || config.sensitivity_db < -24 ||
      config.sensitivity_db > 24 || !std::isfinite(config.gamma) ||
      config.gamma < .25 || config.gamma > 2 ||
      !std::isfinite(config.attack_ms) || config.attack_ms < 1 ||
      config.attack_ms > 200 || !std::isfinite(config.release_ms) ||
      config.release_ms < 10 || config.release_ms > 2000 ||
      !std::isfinite(config.floor_db) || config.floor_db < -120 ||
      config.floor_db > -20 || !std::isfinite(config.ceiling_db) ||
      config.ceiling_db < -6 || config.ceiling_db > 12 ||
      config.floor_db >= config.ceiling_db) {
    throw std::invalid_argument("transform config is outside v1 bounds");
  }
}

}  // namespace

std::string visual_transform_config_json(const VisualAnalysisConfig& config) {
  validate(config);
  return "{\"amplitude_gamma\":" + canonical_number(config.gamma) +
         ",\"attack_ms\":" + canonical_number(config.attack_ms) +
         ",\"release_ms\":" + canonical_number(config.release_ms) +
         ",\"sensitivity_db\":" + canonical_number(config.sensitivity_db) +
         ",\"spectrum_ceiling_db\":" + canonical_number(config.ceiling_db) +
         ",\"spectrum_floor_db\":" + canonical_number(config.floor_db) +
         ",\"version\":1}";
}

std::string visual_transform_config_digest(const VisualAnalysisConfig& config) {
  const auto canonical = visual_transform_config_json(config);
  Sha256 hash;
  hash.update(std::as_bytes(std::span(canonical.data(), canonical.size())));
  return Sha256::hex(hash.finalize());
}

}  // namespace shipglows::audio
