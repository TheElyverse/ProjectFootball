#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>

#include "tactic.hpp"

namespace ElyverseFootball::SimTactics {

// Every tactic file starts with this format name and version; see
// docs/tactic-format.md.
inline constexpr std::string_view kTacticFormat = "elyverse-tactic";
inline constexpr int kTacticFormatVersion = 1;

enum class TacticFileErrorCode : std::uint8_t {
  kIoError,
  kMalformed,
  kUnsupportedVersion,
  kInvalidTactic,
};

// message names the file, the offending field and the problem:
// "data/tactics/pressing.json: phases.pressing.lineHeight: 1.3 must lie in
// [0, 1]". A tactic that breaks several rules lists all of them.
struct TacticFileError {
  TacticFileErrorCode code = TacticFileErrorCode::kMalformed;
  std::string message;

  friend bool operator==(const TacticFileError&, const TacticFileError&) = default;
};

// The tactic as a tactic file of the current version. Responsibilities are
// written out, never as a role preset, so reading the text back yields an
// equal tactic.
[[nodiscard]] std::string toTacticJson(const Tactic& tactic);

// Parses a tactic file. source names it in error messages, typically its
// path. Rejects malformed JSON, another format or version, missing, mistyped
// and unknown fields, and everything Tactic::create() rejects. The same
// text always yields an equal tactic.
[[nodiscard]] std::expected<Tactic, TacticFileError> parseTacticJson(std::string_view json,
                                                                     std::string_view source);

// parseTacticJson() of a file's contents, with the path as source.
[[nodiscard]] std::expected<Tactic, TacticFileError> loadTactic(const std::filesystem::path& path);

}  // namespace ElyverseFootball::SimTactics
