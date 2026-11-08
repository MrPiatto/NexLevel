// nexlevel_bot - headless NexLevel player for testing a room without a second PC or a second GD.
//
//   nexlevel_bot join <ROOM-CODE> [seconds]   join a room, build a scripted test, print what others do
//   nexlevel_bot host [seconds]               open a room (port 47777), print its code and what players do
//                                             (also opens the router port via UPnP, like the mod)
//   nexlevel_bot relay [port]                 run a relay server on this PC (default port 47800)
//
// Set NEXLEVEL_RELAY=host:port to use a relay: "host" then also opens the room on the relay
// and prints its 10-character code, and "join" can join 10-character codes.
//
// Speaks exactly the same protocol as the mod (core library), as GD 2.208x.
#include <nexlevel/core/session/client.hpp>
#include <nexlevel/core/session/host.hpp>
#include <nexlevel/core/session/room_code.hpp>
#include <nexlevel/core/net/local_address.hpp>
#include <nexlevel/core/net/upnp.hpp>
#include <nexlevel/core/relay/protocol.hpp>
#include <nexlevel/core/relay/relay_server.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <thread>
#include <vector>

using namespace nexlevel;
using namespace nexlevel::session;
using namespace nexlevel::sync;
using Clock = std::chrono::steady_clock;

namespace {

constexpr std::uint16_t kPort = 47777;
constexpr GameTarget kTarget = GameTarget::GD2208;

// NEXLEVEL_RELAY=host[:port]
std::optional<std::pair<std::string, std::uint16_t>> relayFromEnv() {
    const char* v = std::getenv("NEXLEVEL_RELAY");
    if (!v || !*v) return std::nullopt;
    std::string s = v;
    auto colon = s.rfind(':');
    if (colon == std::string::npos) return std::pair{s, relay::kDefaultPort};
    return std::pair{s.substr(0, colon), static_cast<std::uint16_t>(std::atoi(s.substr(colon + 1).c_str()))};
}

std::string describeObject(const Replica& view, NetId id) {
    const auto* o = view.find(id);
    if (!o) return "deleted";
    std::string out = "props {" + toSaveString(o->props) + "}";
    return out.size() > 90 ? out.substr(0, 90) + "...}" : out;
}

void printRoster(const std::vector<PeerInfo>& peers) {
    std::printf("[bot] room: ");
    for (std::size_t i = 0; i < peers.size(); ++i) {
        std::printf("%s%s%s", i ? ", " : "", peers[i].name.c_str(), peers[i].id == SessionHost::kHostPeer ? " (host)" : "");
    }
    std::printf("\n");
}

int runJoin(const std::string& code, int seconds) {
    auto addr = decodeRoomCode(code);
    if (!addr) {
        std::printf("[bot] '%s' is not a valid room code (check for typos)\n", code.c_str());
        return 1;
    }
    SessionClient client(kTarget, "NexBot");
    if (auto r = relayFromEnv()) client.setRelayServer(r->first, r->second);
    if (!client.connectCode(code)) {
        std::printf("[bot] cannot join %s: %s (set NEXLEVEL_RELAY=host:port for 10-character codes)\n", code.c_str(), client.lastError().c_str());
        return 1;
    }
    if (addr->relayRoom) std::printf("[bot] room %s -> connecting through the relay ...\n", code.c_str());
    else std::printf("[bot] room %s -> connecting to %s:%u ...\n", code.c_str(), addr->ipString().c_str(), addr->port);

    std::vector<NetId> mine;
    std::size_t lastRoster = 0;
    int step = 0;
    auto start = Clock::now();
    auto nextAction = start;
    ClientState last = ClientState::Idle;

    while (Clock::now() - start < std::chrono::seconds(seconds)) {
        auto changed = client.pump();
        auto st = client.state();
        if (st != last) {
            last = st;
            switch (st) {
                case ClientState::Joined:
                    std::printf("[bot] joined as peer %u - %zu objects in the room\n", *client.peerId(),
                        client.replica()->view().objects().size());
                    nextAction = Clock::now() + std::chrono::seconds(2);
                    changed.clear();
                    break;
                case ClientState::Refused:
                    std::printf("[bot] refused (reason %d, host runs %s)\n", static_cast<int>(client.refuseReason().value_or(RefuseReason::BadHello)),
                        std::string(toString(client.hostTarget().value_or(GameTarget::Unknown))).c_str());
                    return 2;
                case ClientState::Kicked: std::printf("[bot] kicked by the host\n"); return 0;
                case ClientState::RoomClosed: std::printf("[bot] the host closed the room\n"); return 0;
                case ClientState::Disconnected: std::printf("[bot] disconnected: %s\n", client.lastError().c_str()); return 1;
                default: break;
            }
        }
        if (st != ClientState::Joined) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        if (client.peers().size() != lastRoster) {
            lastRoster = client.peers().size();
            printRoster(client.peers());
        }
        for (auto id : changed) {
            std::printf("[bot] remote change: object %u:%llu -> %s\n", id.peer(), static_cast<unsigned long long>(id.counter()),
                describeObject(client.replica()->view(), id).c_str());
        }

        // Scripted building, one action every 1.5 s, near the start of the level.
        if (Clock::now() >= nextAction && step < 11) {
            nextAction = Clock::now() + std::chrono::milliseconds(1500);
            if (step < 5) {
                auto id = *client.newId();
                int x = 255 + step * 30;
                client.submitLocal(Op::create(id, PropMap{{1, "1"}, {2, std::to_string(x)}, {3, "135"}}));
                mine.push_back(id);
                std::printf("[bot] placed block %d at x=%d y=135\n", step + 1, x);
            } else if (step == 5) {
                client.submitLocal(Op::setProps(mine[0], PropDiff{{{3, "225"}}, {}}));
                std::printf("[bot] moved block 1 up\n");
            } else if (step == 6) {
                client.submitLocal(Op::setProps(mine[1], PropDiff{{{6, "45"}}, {}}));
                std::printf("[bot] rotated block 2 by 45 degrees\n");
            } else if (step == 7) {
                client.submitLocal(Op::setProps(mine[2], PropDiff{{{21, "1"}}, {}}));
                std::printf("[bot] set block 3 to colour channel 1\n");
            } else if (step == 8) {
                client.submitLocal(Op::erase(mine[4]));
                std::printf("[bot] deleted block 5\n");
            } else if (step == 9) {
                std::printf("[bot] script done - now edit in GD and watch the changes arrive here\n");
            }
            ++step;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    std::printf("[bot] time is up, leaving (%zu objects in the room)\n", client.replica() ? client.replica()->view().objects().size() : 0);
    return 0;
}

int runHost(int seconds) {
    SessionHost host(kTarget, "NexBot Host");
    std::string err;
    auto port = host.start(kPort, false, &err);
    if (!port) {
        std::printf("[bot] cannot open port %u: %s\n", kPort, err.c_str());
        return 1;
    }
    auto ip = parseIPv4(net::primaryLocalIPv4().value_or("127.0.0.1")).value_or(std::array<std::uint8_t, 4>{127, 0, 0, 1});
    RoomAddress addr{ip, *port, host.roomKey()};
    std::printf("[bot] room open (GD 2.208x) for %d s\n", seconds);
    std::printf("[bot] same-network code: %s\n", encodeRoomCode(addr).c_str());
    std::printf("[bot] opening the router port (UPnP)...\n");
    net::PortMapper mapper;
    mapper.start(*port);
    while (mapper.state() == net::MapState::Working) std::this_thread::sleep_for(std::chrono::milliseconds(50));
    if (mapper.state() == net::MapState::Mapped) {
        RoomAddress online = addr;
        online.ipv4 = *mapper.externalIPv4();
        online.port = mapper.externalPort();
        online.lanOctet = ip[3] == 0 ? 1 : ip[3];
        std::printf("[bot] UPnP OK: public %s port %u\n", online.ipString().c_str(), online.port);
        std::printf("[bot] ONLINE code (works anywhere): %s\n", encodeRoomCode(online).c_str());
    } else {
        std::printf("[bot] UPnP: %s (%s) - only the same-network code works\n", toString(mapper.state()), mapper.lastError().c_str());
    }
    if (auto r = relayFromEnv()) {
        host.startRelay(r->first, r->second, kCustomRelayId);
        auto until = Clock::now() + std::chrono::seconds(10);
        while (host.relayState() != RelayState::Ready && Clock::now() < until) {
            host.pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        if (host.relayState() == RelayState::Ready) {
            RoomAddress ra;
            ra.relayRoom = host.relayRoomId();
            ra.relayId = host.relayId();
            ra.key = host.roomKey();
            std::printf("[bot] RELAY code (works from any network): %s\n", encodeRoomCode(ra).c_str());
        } else {
            std::printf("[bot] relay %s:%u not reachable: %s\n", r->first.c_str(), r->second, host.relayError().c_str());
        }
    }
    std::size_t lastRoster = 1;
    auto start = Clock::now();
    while (Clock::now() - start < std::chrono::seconds(seconds)) {
        for (auto id : host.pump()) {
            std::printf("[bot] change: object %u:%llu -> %s\n", id.peer(), static_cast<unsigned long long>(id.counter()),
                describeObject(host.state(), id).c_str());
        }
        if (host.peers().size() != lastRoster) {
            lastRoster = host.peers().size();
            printRoster(host.peers());
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    std::printf("[bot] closing the room\n");
    host.stop();
    mapper.stop();
    return 0;
}

int runRelay(std::uint16_t port) {
    relay::RelayServer server;
    std::string err;
    auto bound = server.start(port, false, &err);
    if (!bound) {
        std::printf("[relay] cannot open port %u: %s\n", port, err.c_str());
        return 1;
    }
    std::printf("[relay] running on port %u - set NEXLEVEL_RELAY=127.0.0.1:%u (or this PC's address) and use 'host'/'join'\n", *bound, *bound);
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(10));
        auto st = server.stats();
        std::printf("[relay] %zu room(s), %zu player(s) connected, %llu bytes relayed\n", st.rooms, st.pipes,
            static_cast<unsigned long long>(st.bytesRelayed));
    }
}

} // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc >= 3 && std::string(argv[1]) == "join") return runJoin(argv[2], argc >= 4 ? std::atoi(argv[3]) : 300);
    if (argc >= 2 && std::string(argv[1]) == "host") return runHost(argc >= 3 ? std::atoi(argv[2]) : 300);
    if (argc >= 2 && std::string(argv[1]) == "relay") return runRelay(static_cast<std::uint16_t>(argc >= 3 ? std::atoi(argv[2]) : relay::kDefaultPort));
    std::printf("usage:\n  nexlevel_bot join <ROOM-CODE> [seconds]\n  nexlevel_bot host [seconds]\n  nexlevel_bot relay [port]\n");
    return 1;
}
