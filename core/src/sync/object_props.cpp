#include <nexlevel/core/sync/object_props.hpp>

#include <charconv>
#include <system_error>

namespace nexlevel::sync {

std::optional<PropMap> parseSaveString(std::string_view s) {
    while (!s.empty() && (s.back() == ';' || s.back() == '\0')) s.remove_suffix(1);
    PropMap out;
    if (s.empty()) return out;

    std::size_t pos = 0;
    while (pos <= s.size()) {
        auto keyEnd = s.find(',', pos);
        if (keyEnd == std::string_view::npos) return std::nullopt;  // key without value
        auto keyStr = s.substr(pos, keyEnd - pos);
        int key = 0;
        auto [ptr, ec] = std::from_chars(keyStr.data(), keyStr.data() + keyStr.size(), key);
        if (ec != std::errc{} || ptr != keyStr.data() + keyStr.size() || keyStr.empty()) return std::nullopt;

        auto valStart = keyEnd + 1;
        auto valEnd = s.find(',', valStart);
        if (valEnd == std::string_view::npos) valEnd = s.size();
        if (!out.emplace(key, std::string(s.substr(valStart, valEnd - valStart))).second) return std::nullopt;

        if (valEnd == s.size()) break;
        pos = valEnd + 1;
    }
    return out;
}

std::string toSaveString(const PropMap& props) {
    std::string out;
    bool first = true;
    for (const auto& [k, v] : props) {
        if (!first) out += ',';
        first = false;
        out += std::to_string(k);
        out += ',';
        out += v;
    }
    return out;
}

PropDiff diffProps(const PropMap& before, const PropMap& after) {
    PropDiff d;
    auto b = before.begin();
    auto a = after.begin();
    while (b != before.end() || a != after.end()) {
        if (a == after.end() || (b != before.end() && b->first < a->first)) {
            d.removed.push_back(b->first);
            ++b;
        } else if (b == before.end() || a->first < b->first) {
            d.changed.emplace_back(a->first, a->second);
            ++a;
        } else {
            if (a->second != b->second) d.changed.emplace_back(a->first, a->second);
            ++a;
            ++b;
        }
    }
    return d;
}

void applyDiff(PropMap& props, const PropDiff& diff) {
    for (int k : diff.removed) props.erase(k);
    for (const auto& [k, v] : diff.changed) props[k] = v;
}

std::string describe(const PropDiff& diff, std::size_t maxItems) {
    std::string out;
    std::size_t n = 0;
    auto add = [&](const std::string& item) {
        if (n < maxItems) {
            if (!out.empty()) out += ' ';
            out += item;
        }
        ++n;
    };
    for (const auto& [k, v] : diff.changed) add(std::to_string(k) + "=" + (v.size() > 24 ? v.substr(0, 24) + "..." : v));
    for (int k : diff.removed) add("-" + std::to_string(k));
    if (n > maxItems) out += " (+" + std::to_string(n - maxItems) + " more)";
    return out;
}

} // namespace nexlevel::sync
