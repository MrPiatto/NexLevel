// Geode APIs: utils::web::WebRequest::get + geode::async::spawn (v5), matjson::Value::parse,
// Mod::getSettingValue / getSavedValue / setSavedValue.
#include "relay_directory.hpp"

#include <Geode/Geode.hpp>
#include <Geode/utils/async.hpp>
#include <Geode/utils/web.hpp>

#include <nexlevel/core/session/room_code.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>

using namespace geode::prelude;

namespace nexlevel::mod {

namespace {

// Official relays shipped with this build (filled in once they are online).
const std::vector<relay::RelayEndpoint>& builtinRelays() {
    static const std::vector<relay::RelayEndpoint> list = {};
    return list;
}

std::vector<relay::RelayEndpoint> parseDirectory(const std::string& text) {
    std::vector<relay::RelayEndpoint> out;
    auto parsed = matjson::Value::parse(text);
    if (!parsed) return out;
    auto root = parsed.unwrap();
    if (!root.contains("relays")) return out;
    auto arr = root["relays"].asArray();
    if (!arr) return out;
    for (auto& r : arr.unwrap()) {
        auto id = r["id"].asInt();
        auto host = r["host"].asString();
        if (!id || !host || id.unwrap() < 0 || id.unwrap() >= session::kCustomRelayId || host.unwrap().empty()) continue;
        relay::RelayEndpoint e;
        e.id = static_cast<std::uint8_t>(id.unwrap());
        e.host = host.unwrap();
        e.name = r.contains("name") ? r["name"].asString().unwrapOr("relay") : "relay";
        auto port = r.contains("port") ? r["port"].asInt().unwrapOr(relay::kDefaultPort) : relay::kDefaultPort;
        if (port <= 0 || port > 65535) continue;
        e.port = static_cast<std::uint16_t>(port);
        if (std::none_of(out.begin(), out.end(), [&](auto& x) { return x.id == e.id; })) out.push_back(std::move(e));
    }
    return out;
}

} // namespace

std::vector<relay::RelayEndpoint> relayDirectory() {
    auto cached = parseDirectory(Mod::get()->getSavedValue<std::string>("relay-directory-cache", ""));
    return cached.empty() ? builtinRelays() : cached;
}

void refreshRelayDirectory() {
    const auto url = Mod::get()->getSettingValue<std::string>("relay-directory");
    if (url.empty()) return;
    static arc::TaskHandle<void> handle = geode::async::spawn(web::WebRequest().timeout(std::chrono::seconds(10)).get(url),
        [](web::WebResponse res) {
            if (!res.ok()) {
                log::warn("NexLevel: relay directory not reachable (HTTP {})", res.code());
                return;
            }
            auto text = res.string().unwrapOr("");
            auto list = parseDirectory(text);
            if (list.empty()) {
                log::warn("NexLevel: relay directory is empty or invalid");
                return;
            }
            Mod::get()->setSavedValue<std::string>("relay-directory-cache", text);
            log::info("NexLevel: relay directory updated ({} relays)", list.size());
        });
    (void)handle;
}

std::optional<std::pair<std::string, std::uint16_t>> customRelay() {
    auto v = Mod::get()->getSettingValue<std::string>("relay-server");
    v.erase(std::remove_if(v.begin(), v.end(), [](char c) { return std::isspace(static_cast<unsigned char>(c)); }), v.end());
    if (v.empty()) return std::nullopt;
    auto colon = v.rfind(':');
    if (colon == std::string::npos) return std::pair{v, relay::kDefaultPort};
    int port = std::atoi(v.substr(colon + 1).c_str());
    if (port <= 0 || port > 65535) return std::nullopt;
    return std::pair{v.substr(0, colon), static_cast<std::uint16_t>(port)};
}

} // namespace nexlevel::mod

$on_mod(Loaded) {
    nexlevel::mod::refreshRelayDirectory();
}
