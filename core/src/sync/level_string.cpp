#include <nexlevel/core/sync/level_string.hpp>

#include <nexlevel/core/sync/level_settings.hpp>

#include <utility>
#include <vector>

namespace nexlevel::sync {

namespace {

std::vector<std::pair<std::string, std::string>> splitHeader(std::string_view header) {
    std::vector<std::string> tokens;
    std::size_t start = 0;
    while (start <= header.size()) {
        auto comma = header.find(',', start);
        if (comma == std::string_view::npos) comma = header.size();
        tokens.emplace_back(header.substr(start, comma - start));
        start = comma + 1;
    }
    std::vector<std::pair<std::string, std::string>> out;
    for (std::size_t i = 0; i + 1 < tokens.size(); i += 2) out.emplace_back(std::move(tokens[i]), std::move(tokens[i + 1]));
    return out;
}

} // namespace

std::string_view levelStringHeader(std::string_view levelString) noexcept {
    auto semi = levelString.find(';');
    return semi == std::string_view::npos ? levelString : levelString.substr(0, semi);
}

std::string buildLevelString(const Replica& view, std::string_view baseHeader) {
    auto header = baseHeader.empty() ? std::vector<std::pair<std::string, std::string>>{} : splitHeader(baseHeader);
    if (const auto* settings = view.find(kLevelSettingsId)) {
        for (auto& [key, value] : propsToLevelSettings(settings->props).header) {
            bool found = false;
            for (auto& entry : header) {
                if (entry.first == key) { entry.second = value; found = true; break; }
            }
            if (!found) header.emplace_back(key, value);
        }
    }
    std::string out;
    for (const auto& [k, v] : header) {
        if (!out.empty()) out += ',';
        out += k;
        out += ',';
        out += v;
    }
    out += ';';
    for (const auto& [id, state] : view.objects()) {
        if (isLevelSettings(id)) continue;
        out += toSaveString(state.props);
        out += ';';
    }
    return out;
}

} // namespace nexlevel::sync
