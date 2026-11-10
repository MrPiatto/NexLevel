// Discord Rich Presence through Discord's local IPC pipe (\\.\pipe\discord-ipc-N): no library.
// Frames: u32 opcode, u32 length, JSON. Opcode 0 = handshake, 1 = command (SET_ACTIVITY).
// Shown only while the "Discord status" setting is on and the Discord app is running.
#include "discord_presence.hpp"

#include "session_manager.hpp"

#include <Geode/Geode.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef GEODE_IS_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

using namespace geode::prelude;

namespace nexlevel::mod {

namespace {

// Discord application of NexLevel (developer portal -> Application ID). Not a secret.
constexpr const char* kDiscordAppId = "NEXLEVEL_DISCORD_APP_ID";

std::mutex g_mutex;
std::string g_wanted;  // activity JSON wanted (empty = clear)
bool g_threadStarted = false;
std::int64_t g_roomStartEpoch = 0;

#ifdef GEODE_IS_WINDOWS
class Pipe {
public:
    ~Pipe() { close(); }
    bool open() {
        for (int i = 0; i < 10; ++i) {
            const std::wstring name = L"\\\\.\\pipe\\discord-ipc-" + std::to_wstring(i);
            m_h = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
            if (m_h != INVALID_HANDLE_VALUE) return true;
        }
        m_h = INVALID_HANDLE_VALUE;
        return false;
    }
    void close() {
        if (m_h != INVALID_HANDLE_VALUE) CloseHandle(m_h);
        m_h = INVALID_HANDLE_VALUE;
    }
    bool isOpen() const { return m_h != INVALID_HANDLE_VALUE; }
    bool send(std::uint32_t op, const std::string& json) {
        std::vector<char> buf(8 + json.size());
        const auto len = static_cast<std::uint32_t>(json.size());
        std::memcpy(buf.data(), &op, 4);
        std::memcpy(buf.data() + 4, &len, 4);
        std::memcpy(buf.data() + 8, json.data(), json.size());
        DWORD written = 0;
        return WriteFile(m_h, buf.data(), static_cast<DWORD>(buf.size()), &written, nullptr) && written == buf.size();
    }
    // Reads and discards whatever Discord sent back (replies), without blocking.
    bool drain() {
        DWORD avail = 0;
        while (PeekNamedPipe(m_h, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
            char tmp[4096];
            DWORD got = 0;
            if (!ReadFile(m_h, tmp, (std::min)(avail, static_cast<DWORD>(sizeof(tmp))), &got, nullptr)) return false;
            avail -= got;
        }
        return GetLastError() != ERROR_BROKEN_PIPE;
    }

private:
    HANDLE m_h = INVALID_HANDLE_VALUE;
};

void ipcThread() {
    Pipe pipe;
    std::string sent = "\x01";  // nothing sent yet
    auto lastTry = std::chrono::steady_clock::time_point{};
    int nonce = 0;
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        std::string wanted;
        {
            std::lock_guard lk(g_mutex);
            wanted = g_wanted;
        }
        if (!pipe.isOpen()) {
            if (wanted.empty()) continue;
            if (std::chrono::steady_clock::now() - lastTry < std::chrono::seconds(15)) continue;
            lastTry = std::chrono::steady_clock::now();
            if (!pipe.open()) continue;  // Discord is not running
            if (!pipe.send(0, std::string(R"({"v":1,"client_id":")") + kDiscordAppId + "\"}")) {
                pipe.close();
                continue;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            sent = "\x01";
        }
        if (!pipe.drain()) {
            pipe.close();
            continue;
        }
        if (wanted == sent) continue;
        const std::string activity = wanted.empty() ? "null" : wanted;
        const std::string cmd = R"({"cmd":"SET_ACTIVITY","args":{"pid":)" + std::to_string(GetCurrentProcessId()) +
                                R"(,"activity":)" + activity + R"(},"nonce":")" + std::to_string(++nonce) + "\"}";
        if (!pipe.send(1, cmd)) {
            pipe.close();
            continue;
        }
        sent = wanted;
    }
}
#endif

std::string ascii(const std::string& s, std::size_t max) {
    std::string out;
    for (char c : s) {
        if (out.size() >= max) break;
        const auto u = static_cast<unsigned char>(c);
        out += (u >= 32 && u < 127) ? c : '?';
    }
    return out;
}

} // namespace

void updateDiscordPresence() {
#ifdef GEODE_IS_WINDOWS
    if (std::string(kDiscordAppId) == "NEXLEVEL_DISCORD_APP_ID") return;  // app id not set yet
    if (!g_threadStarted) {
        g_threadStarted = true;
        std::thread(ipcThread).detach();
    }
    auto* sm = SessionManager::get();
    std::string wanted;
    if (Mod::get()->getSettingValue<bool>("discord-status")) {
        const bool inEditor = LevelEditorLayer::get() != nullptr;
        std::string details, state;
        int size = 0, max = 0;
        if (sm->inSession()) {
            const auto players = static_cast<int>(sm->peers().size());
            size = players;
            max = sm->role() == Role::Host ? sm->maxPlayers() + 1 : 16;
            details = sm->role() == Role::Host ? "Hosting a collab room" : "In " + sm->peerName(1) + "'s room";
            state = inEditor ? "Building together" : "In the menus";
            if (g_roomStartEpoch == 0) g_roomStartEpoch = static_cast<std::int64_t>(std::time(nullptr)) - sm->roomSeconds();
        } else {
            g_roomStartEpoch = 0;
            details = inEditor ? "In the editor" : "In the menus";
            state = "Not in a room";
        }
        auto activity = matjson::makeObject({
            {"details", ascii(details, 120)},
            {"state", ascii(state, 120)},
            {"assets", matjson::makeObject({{"large_image", "nexlevel"},
                                            {"large_text", "NexLevel " + Mod::get()->getVersion().toVString()}})},
            {"buttons", std::vector<matjson::Value>{matjson::makeObject(
                            {{"label", "Get NexLevel"}, {"url", "https://github.com/MrPiatto/NexLevel/releases/latest"}})}},
        });
        if (size > 0) activity["party"] = matjson::makeObject({{"id", "nexlevel-room"}, {"size", std::vector<matjson::Value>{size, max}}});
        if (g_roomStartEpoch != 0) activity["timestamps"] = matjson::makeObject({{"start", g_roomStartEpoch}});
        wanted = activity.dump(matjson::NO_INDENTATION);
    }
    std::lock_guard lk(g_mutex);
    g_wanted = wanted;
#endif
}

} // namespace nexlevel::mod
