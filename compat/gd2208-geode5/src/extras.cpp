// Room extras: history (who changed what), saved bans, chat commands (/mute), mentions, the
// end-of-session summary and the host's restore points (kept in the mod's own folder, never in
// "My Levels" unless the host opens one).
#include "session_manager.hpp"
#include "toast.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace geode::prelude;

namespace nexlevel::mod {

namespace {

constexpr std::size_t kHistoryKeep = 60;
constexpr auto kHistoryEvery = std::chrono::seconds(3);
constexpr auto kSnapshotEvery = std::chrono::minutes(5);
constexpr std::size_t kSnapshotsKept = 10;

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string clockText(std::time_t t, bool withDate) {
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char b[32];
    std::strftime(b, sizeof b, withDate ? "%d/%m %H:%M" : "%H:%M", &tm);
    return b;
}

std::filesystem::path restoreDir(const std::string& levelName) {
    std::string safe;
    for (char c : levelName) safe += std::isalnum(static_cast<unsigned char>(c)) ? c : '_';
    if (safe.empty()) safe = "level";
    return Mod::get()->getSaveDir() / "restore-points" / safe;
}

} // namespace

// ---------------------------------------------------------------- bans (kept between rooms)
std::vector<SessionManager::BanEntry> SessionManager::savedBans() const {
    std::vector<BanEntry> out;
    std::istringstream in(Mod::get()->getSavedValue<std::string>("room-bans", ""));
    std::string line;
    while (std::getline(in, line)) {
        const auto tab = line.find('\t');
        if (tab == std::string::npos) continue;
        BanEntry e;
        try { e.accountId = static_cast<std::uint32_t>(std::stoul(line.substr(0, tab))); } catch (...) { continue; }
        e.name = line.substr(tab + 1);
        out.push_back(std::move(e));
    }
    return out;
}

void SessionManager::persistBans(const std::vector<BanEntry>& list) {
    std::string s;
    for (const auto& e : list) {
        std::string name;
        for (char c : e.name) name += (c == '\n' || c == '\t') ? ' ' : c;
        s += std::to_string(e.accountId) + "\t" + name + "\n";
    }
    Mod::get()->setSavedValue<std::string>("room-bans", s);
}

void SessionManager::unban(const BanEntry& entry) {
    auto list = savedBans();
    std::erase(list, entry);
    persistBans(list);
    if (m_host) m_host->unban(entry);
}

// ---------------------------------------------------------------- chat commands
bool SessionManager::chatCommand(const std::string& text) {
    if (text.empty() || text[0] != '/') return false;
    const auto space = text.find(' ');
    const auto cmd = lower(text.substr(0, space));
    std::string arg = space == std::string::npos ? "" : text.substr(space + 1);
    while (!arg.empty() && arg.back() == ' ') arg.pop_back();
    if (cmd == "/mute" || cmd == "/unmute") {
        if (arg.empty()) {
            toast("Chat", "Write the player's name: " + cmd + " Name", ToastKind::Info);
            return true;
        }
        const auto key = lower(arg);
        if (cmd == "/mute") {
            m_muted.insert(key);
            toast("Muted " + arg, "You won't see their messages (only you). /unmute " + arg + " to undo", ToastKind::Info);
        } else {
            m_muted.erase(key);
            toast("Unmuted " + arg, "", ToastKind::Info);
        }
        return true;
    }
    toast("Chat commands", "/mute Name, /unmute Name. Write @Name to mention someone.", ToastKind::Info);
    return true;
}

bool SessionManager::isMuted(const std::string& name) const { return m_muted.contains(lower(name)); }

bool SessionManager::mentionsMe(const std::string& text) const {
    const auto me = lower(displayName());
    return !me.empty() && lower(text).find("@" + me) != std::string::npos;
}

// ---------------------------------------------------------------- history + session stats
void SessionManager::extrasTick() {
    const auto now = std::chrono::steady_clock::now();
    if (now - m_lastHistory < kHistoryEvery) return;
    m_lastHistory = now;
    std::map<sync::PeerId, session::OpTally> tally;
    if (m_host) tally = m_host->takeOpTally();
    else if (m_client) tally = m_client->takeOpTally();
    const auto clock = clockText(std::time(nullptr), false);
    for (const auto& [peer, t] : tally) {
        m_sessionPlaced[peer] += t.created;
        std::string text;
        auto part = [&](std::uint32_t n, const char* what) {
            if (!n) return;
            if (!text.empty()) text += ", ";
            text += std::to_string(n) + " " + what;
        };
        part(t.created, "placed");
        part(t.deleted, "deleted");
        part(t.edited, "edited");
        if (text.empty()) continue;
        m_history.push_back({clock, peer, peerName(peer), text});
        ++m_historyTotal;
        while (m_history.size() > kHistoryKeep) m_history.pop_front();
    }
}

void SessionManager::sessionSummary() {
    if (!inSession()) {
        m_sessionPlaced.clear();
        m_history.clear();
        return;
    }
    m_lastHistory = {};
    extrasTick();  // last few seconds too
    const int minutes = std::max(1, roomSeconds() / 60);
    std::size_t mine = 0;
    if (auto self = selfId()) mine = m_sessionPlaced[*self];
    sync::PeerId top = 0;
    std::size_t best = 0;
    for (const auto& [peer, n] : m_sessionPlaced) {
        if (n > best) { best = n; top = peer; }
    }
    std::string text = std::to_string(minutes) + " min, you placed " + std::to_string(mine) + " objects";
    if (best > 0) text += ". Top builder: " + peerName(top) + " (" + std::to_string(best) + ")";
    toast("Session over", text, ToastKind::Success);
    m_sessionPlaced.clear();
    m_history.clear();
}

// ---------------------------------------------------------------- restore points (host)
void SessionManager::snapshotIfDue(const std::string& compressedLevel) {
    if (!m_host || !m_roomLevel || compressedLevel.size() < 8) return;
    const auto now = std::chrono::steady_clock::now();
    if (m_lastSnapshot.time_since_epoch().count() != 0 && now - m_lastSnapshot < kSnapshotEvery) return;
    m_lastSnapshot = now;
    std::error_code ec;
    const auto dir = restoreDir(std::string(m_roomLevel->m_levelName));
    std::filesystem::create_directories(dir, ec);
    if (ec) return;
    const auto file = dir / (std::to_string(static_cast<long long>(std::time(nullptr))) + ".nlr");
    {
        std::ofstream out(file, std::ios::binary);
        if (!out) return;
        out.write(compressedLevel.data(), static_cast<std::streamsize>(compressedLevel.size()));
    }
    // Keep only the newest ones.
    std::vector<std::filesystem::path> files;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
        if (e.path().extension() == ".nlr") files.push_back(e.path());
    }
    std::sort(files.begin(), files.end());
    while (files.size() > kSnapshotsKept) {
        std::filesystem::remove(files.front(), ec);
        files.erase(files.begin());
    }
    log::info("NexLevel: restore point saved ({} bytes)", compressedLevel.size());
}

std::vector<SessionManager::RestorePoint> SessionManager::restorePoints() const {
    std::vector<RestorePoint> out;
    if (!m_host || !m_roomLevel) return out;
    std::error_code ec;
    const auto dir = restoreDir(std::string(m_roomLevel->m_levelName));
    for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
        if (e.path().extension() != ".nlr") continue;
        long long t = 0;
        try { t = std::stoll(e.path().stem().string()); } catch (...) { continue; }
        out.push_back({e.path(), clockText(static_cast<std::time_t>(t), true), t});
    }
    std::sort(out.begin(), out.end(), [](auto& a, auto& b) { return a.time > b.time; });
    return out;
}

bool SessionManager::openRestorePoint(const RestorePoint& point, std::string* name) {
    std::ifstream in(point.file, std::ios::binary);
    if (!in) return false;
    std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (data.size() < 8) return false;
    auto* level = GameLevelManager::sharedState()->createNewLevel();
    if (!level) return false;
    std::string base = m_roomLevel ? std::string(m_roomLevel->m_levelName) : std::string("Level");
    const auto stamp = clockText(static_cast<std::time_t>(point.time), false);
    if (base.size() > 12) base = base.substr(0, 12);
    level->m_levelName = base + " (" + stamp + ")";
    level->m_levelDesc = "NexLevel restore point";
    level->m_levelString = data;
    if (m_roomLevel) {
        level->m_audioTrack = m_roomLevel->m_audioTrack;
        level->m_songID = m_roomLevel->m_songID;
        level->m_songIDs = m_roomLevel->m_songIDs;
        level->m_sfxIDs = m_roomLevel->m_sfxIDs;
    }
    LocalLevelManager::sharedState()->save();
    if (name) *name = std::string(level->m_levelName);
    return true;
}

} // namespace nexlevel::mod
