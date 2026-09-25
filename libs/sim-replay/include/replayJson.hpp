#pragma once

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>

#include "replay.hpp"

namespace ElyverseFootball::SimReplay {

// The replay as a JSON document in the format of docs/replay-format.md,
// schema version kReplaySchemaVersion.
[[nodiscard]] std::string toReplayJson(const Replay& replay);

// Parses a replay document. Rejects malformed JSON, a schema version other
// than kReplaySchemaVersion, a file from another core version, and content
// that breaks the format's rules -- a missing or mistyped field, commands out
// of execution order, an invalid initial state -- with a message naming the
// offending field.
[[nodiscard]] std::expected<Replay, ReplayError> parseReplayJson(std::string_view json);

// toReplayJson() written to a file.
[[nodiscard]] std::expected<void, ReplayError> saveReplay(const Replay& replay,
                                                          const std::filesystem::path& path);

// parseReplayJson() of a file's contents.
[[nodiscard]] std::expected<Replay, ReplayError> loadReplay(const std::filesystem::path& path);

}  // namespace ElyverseFootball::SimReplay
