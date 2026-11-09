// UPnP port mapping against a mock Internet Gateway Device on localhost (SSDP + HTTP/SOAP),
// plus the online room code format and multi-address connecting.
#include "harness.hpp"

#include <nexlevel/core/net/upnp.hpp>
#include <nexlevel/core/session/client.hpp>
#include <nexlevel/core/session/host.hpp>
#include <nexlevel/core/session/room_code.hpp>

#include <asio.hpp>

#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <mutex>
#include <regex>
#include <set>
#include <thread>
#include <system_error>

using namespace nexlevel;
using asio::ip::tcp;
using asio::ip::udp;

namespace {

struct Mapping {
    int internalPort = 0;
    std::string client;
    int lease = 0;
};

class MockIgd {
public:
    std::string externalIp = "203.0.113.7";
    std::set<int> takenPorts;      // answer 718 for these
    bool permanentOnly = false;    // answer 725 for non-zero leases
    bool chunked = true;           // chunked transfer encoding for the description
    bool ppp = false;              // advertise WANPPPConnection instead of WANIPConnection

    std::mutex mutex;
    std::map<int, Mapping> mappings;
    int addCalls = 0, deleteCalls = 0;

    MockIgd() : m_udp(m_io, udp::endpoint(asio::ip::make_address_v4("127.0.0.1"), 0)),
                m_acceptor(m_io, tcp::endpoint(asio::ip::make_address_v4("127.0.0.1"), 0)) {}
    ~MockIgd() { stop(); }

    std::uint16_t ssdpPort() const { return m_udp.local_endpoint().port(); }

    void start() {
        receive();
        accept();
        m_thread = std::thread([this] { m_io.run(); });
    }
    void stop() {
        m_io.stop();
        if (m_thread.joinable()) m_thread.join();
    }

private:
    asio::io_context m_io;
    udp::socket m_udp;
    tcp::acceptor m_acceptor;
    std::thread m_thread;
    std::array<char, 2048> m_buf{};
    udp::endpoint m_from;

    std::string location() const { return "http://127.0.0.1:" + std::to_string(m_acceptor.local_endpoint().port()) + "/rootDesc.xml"; }

    void receive() {
        m_udp.async_receive_from(asio::buffer(m_buf), m_from, [this](std::error_code ec, std::size_t n) {
            if (ec) return;
            std::string msg(m_buf.data(), n);
            if (msg.rfind("M-SEARCH", 0) == 0) {
                auto reply = std::make_shared<std::string>("HTTP/1.1 200 OK\r\nCACHE-CONTROL: max-age=120\r\nST: x\r\nLocation: " + location() + "\r\n\r\n");
                m_udp.async_send_to(asio::buffer(*reply), m_from, [reply](std::error_code, std::size_t) {});
            }
            receive();
        });
    }

    void accept() {
        m_acceptor.async_accept([this](std::error_code ec, tcp::socket s) {
            if (!ec) serve(std::make_shared<tcp::socket>(std::move(s)));
            accept();
        });
    }

    void serve(std::shared_ptr<tcp::socket> s) {
        auto data = std::make_shared<std::string>();
        auto buf = std::make_shared<std::array<char, 4096>>();
        auto self = this;
        std::shared_ptr<std::function<void()>> readMore = std::make_shared<std::function<void()>>();
        std::weak_ptr<std::function<void()>> weak = readMore;  // no self-reference cycle
        *readMore = [s, data, buf, self, weak] {
            auto readMore = weak.lock();
            if (!readMore) return;
            s->async_read_some(asio::buffer(*buf), [s, data, buf, self, readMore](std::error_code ec, std::size_t n) {
                if (ec) return;
                data->append(buf->data(), n);
                auto he = data->find("\r\n\r\n");
                if (he == std::string::npos) { (*readMore)(); return; }
                std::size_t len = 0;
                std::smatch m;
                std::string head = data->substr(0, he);
                if (std::regex_search(head, m, std::regex("Content-Length: (\\d+)", std::regex::icase))) len = std::stoul(m[1]);
                if (data->size() < he + 4 + len) { (*readMore)(); return; }
                auto resp = std::make_shared<std::string>(self->handle(head, data->substr(he + 4, len)));
                asio::async_write(*s, asio::buffer(*resp), [s, resp](std::error_code, std::size_t) {
                    std::error_code e;
                    s->shutdown(tcp::socket::shutdown_both, e);
                });
            });
        };
        (*readMore)();
    }

    static std::string value(const std::string& body, const std::string& tag) {
        std::smatch m;
        if (std::regex_search(body, m, std::regex("<" + tag + ">([^<]*)</" + tag + ">"))) return m[1];
        return {};
    }

    std::string service() const { return ppp ? "urn:schemas-upnp-org:service:WANPPPConnection:1" : "urn:schemas-upnp-org:service:WANIPConnection:1"; }

    static std::string ok(const std::string& body, bool chunk = false) {
        if (chunk) {
            std::string out = "HTTP/1.1 200 OK\r\nContent-Type: text/xml\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n";
            for (std::size_t i = 0; i < body.size(); i += 100) {
                auto part = body.substr(i, 100);
                char hex[16];
                std::snprintf(hex, sizeof hex, "%zx", part.size());
                out += std::string(hex) + "\r\n" + part + "\r\n";
            }
            return out + "0\r\n\r\n";
        }
        return "HTTP/1.1 200 OK\r\nContent-Type: text/xml\r\nContent-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
    }
    static std::string fault(int code) {
        std::string body = "<s:Envelope><s:Body><s:Fault><detail><UPnPError><errorCode>" + std::to_string(code) +
                           "</errorCode></UPnPError></detail></s:Fault></s:Body></s:Envelope>";
        return "HTTP/1.1 500 Internal Server Error\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
    }

    std::string handle(const std::string& head, const std::string& body) {
        if (head.rfind("GET /rootDesc.xml", 0) == 0) {
            std::string desc =
                "<?xml version=\"1.0\"?><root xmlns=\"urn:schemas-upnp-org:device-1-0\"><device>"
                "<deviceType>urn:schemas-upnp-org:device:InternetGatewayDevice:1</deviceType><serviceList>"
                "<service><serviceType>urn:schemas-upnp-org:service:Layer3Forwarding:1</serviceType><controlURL>/l3f</controlURL></service>"
                "</serviceList><deviceList><device><deviceList><device><serviceList>"
                "<service>\n  <serviceType>" + service() + "</serviceType>\n  <serviceId>x</serviceId>\n"
                "  <controlURL>ctl/IPConn</controlURL>\n</service>"
                "</serviceList></device></deviceList></device></deviceList></device></root>";
            return ok(desc, chunked);
        }
        if (head.rfind("POST /ctl/IPConn", 0) != 0) return "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
        std::lock_guard lk(mutex);
        if (head.find("#GetExternalIPAddress") != std::string::npos) {
            return ok("<s:Envelope><s:Body><u:GetExternalIPAddressResponse xmlns:u=\"" + service() +
                      "\"><NewExternalIPAddress>" + externalIp + "</NewExternalIPAddress></u:GetExternalIPAddressResponse></s:Body></s:Envelope>");
        }
        if (head.find("#AddPortMapping") != std::string::npos) {
            ++addCalls;
            int ext = std::stoi(value(body, "NewExternalPort"));
            int lease = std::stoi(value(body, "NewLeaseDuration"));
            if (takenPorts.count(ext)) return fault(718);
            if (permanentOnly && lease != 0) return fault(725);
            if (value(body, "NewProtocol") != "TCP") return fault(402);
            mappings[ext] = {std::stoi(value(body, "NewInternalPort")), value(body, "NewInternalClient"), lease};
            return ok("<s:Envelope><s:Body><u:AddPortMappingResponse/></s:Body></s:Envelope>");
        }
        if (head.find("#DeletePortMapping") != std::string::npos) {
            ++deleteCalls;
            mappings.erase(std::stoi(value(body, "NewExternalPort")));
            return ok("<s:Envelope><s:Body><u:DeletePortMappingResponse/></s:Body></s:Envelope>");
        }
        return fault(401);
    }
};

net::PortMapper::Options mockOptions(const MockIgd& igd) {
    net::PortMapper::Options o;
    o.ssdpHost = "127.0.0.1";
    o.ssdpPort = igd.ssdpPort();
    o.discoverTimeout = std::chrono::milliseconds(400);
    o.requestTimeout = std::chrono::milliseconds(1500);
    return o;
}

bool waitSettled(const net::PortMapper& pm) {
    for (int i = 0; i < 300; ++i) {
        if (pm.state() != net::MapState::Working) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

void testMapAndRelease() {
    MockIgd igd;
    igd.start();
    {
        net::PortMapper pm(mockOptions(igd));
        pm.start(47777);
        CHECK(waitSettled(pm));
        CHECK(pm.state() == net::MapState::Mapped);
        CHECK(pm.externalPort() == 47777);
        auto ip = pm.externalIPv4();
        CHECK(ip && (*ip == std::array<std::uint8_t, 4>{203, 0, 113, 7}));
        std::lock_guard lk(igd.mutex);
        CHECK(igd.mappings.count(47777) == 1);
        CHECK(igd.mappings[47777].internalPort == 47777);
        CHECK(igd.mappings[47777].client == "127.0.0.1");
        CHECK(igd.mappings[47777].lease == 3600);
    }  // destructor removes the mapping
    std::lock_guard lk(igd.mutex);
    CHECK(igd.mappings.empty());
    CHECK(igd.deleteCalls == 1);
}

void testPortTakenAndPermanentLease() {
    MockIgd igd;
    igd.takenPorts = {47777, 47778};
    igd.permanentOnly = true;
    igd.chunked = false;
    igd.ppp = true;
    igd.start();
    net::PortMapper pm(mockOptions(igd));
    pm.start(47777);
    CHECK(waitSettled(pm));
    CHECK(pm.state() == net::MapState::Mapped);
    CHECK(pm.externalPort() == 47779);
    {
        std::lock_guard lk(igd.mutex);
        CHECK(igd.mappings.count(47779) == 1 && igd.mappings[47779].internalPort == 47777);
        CHECK(igd.mappings[47779].lease == 0);
    }
    pm.stop();
    std::lock_guard lk(igd.mutex);
    CHECK(igd.mappings.empty());
}

void testDoubleNatAndNoGateway() {
    MockIgd igd;
    igd.externalIp = "100.72.10.5";  // CGNAT range
    igd.start();
    net::PortMapper pm(mockOptions(igd));
    pm.start(47777);
    CHECK(waitSettled(pm));
    CHECK(pm.state() == net::MapState::DoubleNat);
    pm.stop();

    net::PortMapper::Options o;
    o.ssdpHost = "127.0.0.1";
    o.ssdpPort = 9;  // nothing answers
    o.discoverTimeout = std::chrono::milliseconds(200);
    net::PortMapper none(o);
    none.start(47777);
    CHECK(waitSettled(none));
    CHECK(none.state() == net::MapState::NoGateway);

    CHECK(net::isPrivateIPv4({192, 168, 1, 2}) && net::isPrivateIPv4({10, 0, 0, 1}) && net::isPrivateIPv4({172, 20, 0, 1}));
    CHECK(!net::isPrivateIPv4({8, 8, 8, 8}) && !net::isPrivateIPv4({172, 32, 0, 1}) && !net::isPrivateIPv4({100, 128, 0, 1}));
}

void testOnlineRoomCode() {
    session::RoomAddress a;
    a.ipv4 = {203, 0, 113, 7};
    a.port = 47779;
    a.key = 1234;
    a.lanOctet = 42;
    auto code = session::encodeRoomCode(a);
    CHECK(code.size() == 17);  // 15 symbols + 2 dashes
    auto back = session::decodeRoomCode(code);
    CHECK(back && *back == a);
    // A typo is caught.
    std::string bad = code;
    bad[3] = bad[3] == 'A' ? 'B' : 'A';
    CHECK(!session::decodeRoomCode(bad));
    // Short codes still work unchanged.
    a.lanOctet = 0;
    CHECK(session::encodeRoomCode(a).size() == 15);
    CHECK(session::decodeRoomCode(session::encodeRoomCode(a)) == a);
}

void testConnectRacesAddresses() {
    session::SessionHost host(GameTarget::GD2208, "Host");
    std::string err;
    CHECK(host.start(0, true, &err));
    session::SessionClient client(GameTarget::GD2208, "Guest");
    // First address goes nowhere (closed port), second is the real room: the client must still join.
    client.connectTargets({{"127.0.0.1", 1}, {"127.0.0.1", host.port()}}, host.roomKey());
    bool joined = false;
    for (int i = 0; i < 400 && !joined; ++i) {
        host.pump();
        client.pump();
        joined = client.state() == session::ClientState::Joined;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(joined);
    host.stop();
}

} // namespace

void runUpnpTests() {
    testOnlineRoomCode();
    testMapAndRelease();
    testPortTakenAndPermanentLease();
    testDoubleNatAndNoGateway();
    testConnectRacesAddresses();
}
