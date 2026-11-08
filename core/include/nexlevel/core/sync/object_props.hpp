#pragma once
// GD object state as key/value properties, parsed from GameObject save strings ("k,v,k,v,...").
// Property-level diffs are the unit of sync: only changed keys travel over the network.
#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace nexlevel::sync {

using PropMap = std::map<int, std::string>;

// Returns nullopt for malformed input (odd field count, non-numeric or duplicate key).
// A trailing ';' (level-string object separator) is ignored.
std::optional<PropMap> parseSaveString(std::string_view s);

// Inverse of parseSaveString, keys in ascending order.
std::string toSaveString(const PropMap& props);

struct PropDiff {
    std::vector<std::pair<int, std::string>> changed;  // added or modified, ascending key
    std::vector<int> removed;                          // ascending key
    bool empty() const noexcept { return changed.empty() && removed.empty(); }
};

PropDiff diffProps(const PropMap& before, const PropMap& after);

// Applies a diff produced by diffProps(before, after) to before, giving after.
void applyDiff(PropMap& props, const PropDiff& diff);

// Short human-readable form for logs: "2=15.5 3=105 -21", at most maxItems entries.
std::string describe(const PropDiff& diff, std::size_t maxItems = 8);

} // namespace nexlevel::sync
