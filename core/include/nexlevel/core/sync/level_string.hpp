#pragma once
// Builds a GD level string ("<settings header>;<object>;<object>;...") from the shared room state, so
// the room can be saved into the host's level even while nobody has the editor open (players keep
// building while the host is in the menus) and into a player's backup copy.
#include <nexlevel/core/sync/replica.hpp>

#include <string>
#include <string_view>

namespace nexlevel::sync {

// baseHeader: the header the level had before (first ';' segment of its level string, may be empty).
// Its keys are kept (editor-only keys such as kS39 are never synced), synced keys override them,
// synced keys it lacks are appended.
std::string buildLevelString(const Replica& view, std::string_view baseHeader);

// First ';'-separated segment of a level string (the settings header).
std::string_view levelStringHeader(std::string_view levelString) noexcept;

} // namespace nexlevel::sync
