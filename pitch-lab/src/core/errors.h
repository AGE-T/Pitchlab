#pragma once

// Pitch Lab — error vocabulary (implementation specification §4.8 / §4.2 / §9).
//
// v0.1 error classes (frozen semantics):
//   * ConfigError  — hard configuration/input errors. Thrown by the
//     ExperimentCompiler on bad TOML, by the WAV reader on invalid input
//     files ("rejects truncated/invalid files with CONFIG ERROR mapping",
//     spec §4.9), and by primitives on invalid configuration values. Fields
//     {file, field, reason} per spec §4.8. An experiment hitting this class
//     is rejected at compile/validation time (never partially rendered).
//   * WavSizeLimitError — the RIFF 32-bit size cap (spec §9: hitting the
//     4 GiB cap is a JOB FAILURE with an explicit message; W64 upgrade is
//     open decision OD-14). Distinct from ConfigError because the failure
//     class at harness level is job-failure, not config-error: the
//     OfflineRenderer (implemented in a later cycle) will catch this type
//     and mark the job failed while the run continues (architecture §K).
//   * EngineException — thrown ONLY by engines (spec §4.2); any other
//     exception escaping an engine is a JOB FAILURE. Not yet defined as a
//     class because no engine exists (spec §17 step 3 introduces it with
//     the varispeed engine); deliberately not pre-built here.

#include <stdexcept>
#include <string>
#include <utility>

namespace pitchlab {

class ConfigError : public std::runtime_error {
 public:
  ConfigError(std::string file, std::string field, std::string reason)
      : std::runtime_error(buildMessage(file, field, reason)),
        file_(std::move(file)),
        field_(std::move(field)),
        reason_(std::move(reason)) {}

  [[nodiscard]] const std::string& file() const { return file_; }
  [[nodiscard]] const std::string& field() const { return field_; }
  [[nodiscard]] const std::string& reason() const { return reason_; }

 private:
  static std::string buildMessage(const std::string& file, const std::string& field,
                                  const std::string& reason) {
    std::string msg = "CONFIG ERROR";
    if (!file.empty()) msg += " [" + file + "]";
    if (!field.empty()) msg += " field '" + field + "'";
    msg += ": " + reason;
    return msg;
  }

  std::string file_;
  std::string field_;
  std::string reason_;
};

class WavSizeLimitError : public std::runtime_error {
 public:
  explicit WavSizeLimitError(const std::string& reason) : std::runtime_error(reason) {}
};

}  // namespace pitchlab
