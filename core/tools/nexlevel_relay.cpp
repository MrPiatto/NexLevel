// nexlevel_relay - the Forg3 relay server. Lets NexLevel rooms work from any network
// (mobile data, CGNAT, strict NAT). Run it on a machine with a public address.
//
//   nexlevel_relay [--port 47800] [--max-rooms 5000] [--behind-tunnel]
//
// --behind-tunnel: the relay is reached through a tunnel (e.g. playit.gg) or proxy, so every
// player appears to come from the same local address; per-address limits are relaxed.
//
// It only forwards bytes between players and hosts; session traffic is end-to-end encrypted.
#include <nexlevel/core/relay/protocol.hpp>
#include <nexlevel/core/relay/relay_server.hpp>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <thread>
#include <cstddef>

namespace {
std::atomic<bool> g_stop{false};
void onSignal(int) { g_stop = true; }

void logLine(const char* fmt, auto... args) {
    std::time_t t = std::time(nullptr);
    char ts[32];
    std::strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", std::gmtime(&t));
    std::printf("%s ", ts);
    std::printf(fmt, args...);
    std::printf("\n");
    std::fflush(stdout);
}
} // namespace

int main(int argc, char** argv) {
    std::uint16_t port = nexlevel::relay::kDefaultPort;
    nexlevel::relay::RelayServer::Options opt;
    for (int i = 1; i < argc; ++i) {
        std::string k = argv[i];
        if (k == "--behind-tunnel") {
            opt.maxConnectionsPerIp = 100000;
            opt.maxFailedJoinsPerIpPerMinute = 100000;
            continue;
        }
        if (i + 1 >= argc) { std::printf("usage: nexlevel_relay [--port 47800] [--max-rooms 5000] [--behind-tunnel]\n"); return 2; }
        if (k == "--port") port = static_cast<std::uint16_t>(std::atoi(argv[++i]));
        else if (k == "--max-rooms") opt.maxRooms = static_cast<std::size_t>(std::atoll(argv[++i]));
        else { std::printf("usage: nexlevel_relay [--port 47800] [--max-rooms 5000] [--behind-tunnel]\n"); return 2; }
    }
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    nexlevel::relay::RelayServer server(opt);
    std::string err;
    auto bound = server.start(port, false, &err);
    if (!bound) {
        logLine("cannot listen on port %u: %s", port, err.c_str());
        return 1;
    }
    logLine("NexLevel relay listening on port %u", *bound);
    auto lastLog = std::chrono::steady_clock::now();
    while (!g_stop) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        if (std::chrono::steady_clock::now() - lastLog >= std::chrono::minutes(1)) {
            lastLog = std::chrono::steady_clock::now();
            auto st = server.stats();
            logLine("rooms=%zu players=%zu relayed=%llu refused=%llu", st.rooms, st.pipes,
                static_cast<unsigned long long>(st.bytesRelayed), static_cast<unsigned long long>(st.joinsRefused));
        }
    }
    logLine("%s", "stopping");
    server.stop();
    return 0;
}
