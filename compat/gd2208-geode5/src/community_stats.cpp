#include "community_stats.hpp"

#include "relay_directory.hpp"

#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>

#include <nexlevel/core/relay/protocol.hpp>

#include <chrono>
#include <thread>

using namespace geode::prelude;

namespace nexlevel::mod {

namespace {

CommunityStats g_stats;
int g_githubDownloads = -1;
int g_indexDownloads = -1;
std::chrono::steady_clock::time_point g_lastRefresh{};
bool g_refreshedOnce = false;
arc::TaskHandle<void> g_github;
arc::TaskHandle<void> g_index;

void updateDownloads() {
    if (g_githubDownloads < 0 && g_indexDownloads < 0) return;
    g_stats.downloads = std::max(0, g_githubDownloads) + std::max(0, g_indexDownloads);
}

} // namespace

const CommunityStats& communityStats() { return g_stats; }

void refreshCommunityStats() {
    const auto now = std::chrono::steady_clock::now();
    if (g_refreshedOnce && now - g_lastRefresh < std::chrono::minutes(1)) return;
    g_refreshedOnce = true;
    g_lastRefresh = now;

    // Downloads of the official .geode on GitHub (every release).
    g_github = geode::async::spawn(
        web::WebRequest().timeout(std::chrono::seconds(10)).header("User-Agent", "NexLevel-mod")
            .get("https://api.github.com/repos/MrPiatto/NexLevel/releases?per_page=100"),
        [](web::WebResponse res) {
            if (!res.ok()) return;
            auto json = res.json();
            if (json.isErr()) return;
            const matjson::Value releases = std::move(json).unwrap();
            auto list = releases.asArray();
            if (list.isErr()) return;
            int total = 0;
            for (const auto& r : list.unwrap()) {
                auto assets = r["assets"].asArray();
                if (assets.isErr()) continue;
                for (const auto& a : assets.unwrap()) {
                    if (a["name"].asString().unwrapOr("") == "forg3studios.nexlevel.geode") {
                        total += static_cast<int>(a["download_count"].asInt().unwrapOr(0));
                    }
                }
            }
            g_githubDownloads = total;
            updateDownloads();
        });
    // Downloads through the Geode index (once the mod is listed there).
    g_index = geode::async::spawn(
        web::WebRequest().timeout(std::chrono::seconds(10)).get("https://api.geode-sdk.org/v1/mods/forg3studios.nexlevel"),
        [](web::WebResponse res) {
            if (!res.ok()) return;
            auto json = res.json();
            if (json.isErr()) return;
            const matjson::Value j = std::move(json).unwrap();
            auto n = j["payload"]["download_count"].asInt();
            if (n.isErr()) return;
            g_indexDownloads = static_cast<int>(n.unwrap());
            updateDownloads();
        });
    // Live players and rooms: ask every official relay (blocking query, so on a worker thread).
    std::thread([relays = relayDirectory()] {
        auto counters = relay::queryRelayStats(relays, std::chrono::milliseconds(3000));
        int online = 0, rooms = 0;
        bool any = false;
        for (auto& c : counters) {
            if (!c) continue;
            any = true;
            online += static_cast<int>(c->players);
            rooms += static_cast<int>(c->rooms);
        }
        geode::queueInMainThread([any, online, rooms] {
            if (!any) return;
            g_stats.online = online;
            g_stats.rooms = rooms;
        });
    }).detach();
}

} // namespace nexlevel::mod
