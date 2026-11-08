#include <nexlevel/core/net/upnp.hpp>

#include <nexlevel/core/net/local_address.hpp>
#include <nexlevel/core/session/room_code.hpp>

#include <asio.hpp>

#include <algorithm>
#include <cctype>
#include <condition_variable>
#include <cstring>
#include <functional>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

namespace nexlevel::net {

using asio::ip::tcp;
using asio::ip::udp;

const char* toString(MapState s) noexcept {
    switch (s) {
        case MapState::Idle: return "idle";
        case MapState::Working: return "working";
        case MapState::Mapped: return "mapped";
        case MapState::NoGateway: return "no UPnP router";
        case MapState::DoubleNat: return "double NAT";
        case MapState::Failed: return "refused by router";
    }
    return "?";
}

bool isPrivateIPv4(const std::array<std::uint8_t, 4>& ip) noexcept {
    if (ip[0] == 10 || ip[0] == 127 || ip[0] == 0) return true;
    if (ip[0] == 172 && ip[1] >= 16 && ip[1] <= 31) return true;
    if (ip[0] == 192 && ip[1] == 168) return true;
    if (ip[0] == 169 && ip[1] == 254) return true;
    if (ip[0] == 100 && ip[1] >= 64 && ip[1] <= 127) return true;  // CGNAT
    return ip[0] >= 224;                                           // multicast / reserved
}

namespace {

constexpr std::size_t kMaxResponse = 128 * 1024;

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// ---- tiny HTTP/1.1 client (IPv4 literal hosts only) ----

struct HttpResponse {
    int status = 0;
    std::map<std::string, std::string> headers;  // lower-case names
    std::string body;
};

bool dechunk(const std::string& in, std::string& out) {
    out.clear();
    std::size_t pos = 0;
    while (true) {
        auto eol = in.find("\r\n", pos);
        if (eol == std::string::npos) return false;
        std::size_t size = 0;
        try { size = std::stoul(in.substr(pos, eol - pos), nullptr, 16); } catch (...) { return false; }
        pos = eol + 2;
        if (size == 0) return true;
        if (pos + size > in.size()) return false;
        out.append(in, pos, size);
        pos += size + 2;
    }
}

// Splits a raw response; returns false if the head is incomplete. complete=true when the whole
// body has arrived according to Content-Length / chunked framing.
bool parseResponse(const std::string& raw, HttpResponse& r, bool& complete) {
    complete = false;
    auto headEnd = raw.find("\r\n\r\n");
    if (headEnd == std::string::npos) return false;
    std::string head = raw.substr(0, headEnd);
    std::string rest = raw.substr(headEnd + 4);
    auto firstEol = head.find("\r\n");
    std::string statusLine = head.substr(0, firstEol);
    auto sp = statusLine.find(' ');
    if (sp == std::string::npos) return false;
    try { r.status = std::stoi(statusLine.substr(sp + 1, 3)); } catch (...) { return false; }
    r.headers.clear();
    std::size_t pos = firstEol == std::string::npos ? head.size() : firstEol + 2;
    while (pos < head.size()) {
        auto eol = head.find("\r\n", pos);
        if (eol == std::string::npos) eol = head.size();
        std::string line = head.substr(pos, eol - pos);
        auto colon = line.find(':');
        if (colon != std::string::npos) {
            std::string v = line.substr(colon + 1);
            v.erase(0, v.find_first_not_of(" \t"));
            r.headers[lower(line.substr(0, colon))] = v;
        }
        pos = eol + 2;
    }
    auto te = r.headers.find("transfer-encoding");
    if (te != r.headers.end() && lower(te->second).find("chunked") != std::string::npos) {
        complete = dechunk(rest, r.body);
        return true;
    }
    auto cl = r.headers.find("content-length");
    if (cl != r.headers.end()) {
        std::size_t n = 0;
        try { n = std::stoul(cl->second); } catch (...) { return false; }
        if (rest.size() >= n) { r.body = rest.substr(0, n); complete = true; }
        return true;
    }
    r.body = rest;  // until the server closes
    return true;
}

std::optional<HttpResponse> httpRequest(const std::string& host, std::uint16_t port, const std::string& request,
                                        std::chrono::milliseconds timeout, std::string* localAddr, std::string& err) {
    std::error_code ec;
    auto addr = asio::ip::make_address_v4(host, ec);
    if (ec) { err = "bad host " + host; return std::nullopt; }

    asio::io_context io;
    tcp::socket sock(io);
    std::string raw;
    std::array<char, 4096> buf{};
    bool done = false, gotAll = false;
    std::error_code failure;
    HttpResponse resp;

    std::function<void()> readMore = [&] {
        sock.async_read_some(asio::buffer(buf), [&](std::error_code rec, std::size_t n) {
            raw.append(buf.data(), n);
            if (raw.size() > kMaxResponse) { failure = asio::error::message_size; done = true; return; }
            bool complete = false;
            if (parseResponse(raw, resp, complete) && complete) { gotAll = true; done = true; return; }
            if (rec) {
                if (rec == asio::error::eof) gotAll = parseResponse(raw, resp, complete);
                else failure = rec;
                done = true;
                return;
            }
            readMore();
        });
    };
    sock.async_connect(tcp::endpoint(addr, port), [&](std::error_code cec) {
        if (cec) { failure = cec; done = true; return; }
        if (localAddr) {
            std::error_code lec;
            auto le = sock.local_endpoint(lec);
            if (!lec) *localAddr = le.address().to_string();
        }
        asio::async_write(sock, asio::buffer(request), [&](std::error_code wec, std::size_t) {
            if (wec) { failure = wec; done = true; return; }
            readMore();
        });
    });
    io.run_for(timeout);
    sock.close(ec);
    if (!done) { err = "timeout talking to " + host; return std::nullopt; }
    if (!gotAll) { err = failure ? failure.message() : "incomplete response"; return std::nullopt; }
    return resp;
}

struct Url {
    std::string host;
    std::uint16_t port = 80;
    std::string path = "/";
};

std::optional<Url> parseUrl(std::string s) {
    const std::string scheme = "http://";
    if (lower(s.substr(0, scheme.size())) != scheme) return std::nullopt;
    s = s.substr(scheme.size());
    auto slash = s.find('/');
    std::string hostPort = s.substr(0, slash);
    Url u;
    u.path = slash == std::string::npos ? "/" : s.substr(slash);
    auto colon = hostPort.find(':');
    u.host = hostPort.substr(0, colon);
    if (colon != std::string::npos) {
        int p = 0;
        try { p = std::stoi(hostPort.substr(colon + 1)); } catch (...) { return std::nullopt; }
        if (p <= 0 || p > 65535) return std::nullopt;
        u.port = static_cast<std::uint16_t>(p);
    }
    // Only private-network IPv4 literals: a gateway is always on the local network, and this keeps
    // a hostile SSDP answer from sending us anywhere else.
    auto ip = session::parseIPv4(u.host);
    if (!ip || !isPrivateIPv4(*ip) || (*ip)[0] >= 224) return std::nullopt;
    return u;
}

std::string resolveUrl(const Url& base, const std::string& ref) {
    if (lower(ref.substr(0, 7)) == "http://") return ref;
    std::string root = "http://" + base.host + ":" + std::to_string(base.port);
    if (!ref.empty() && ref[0] == '/') return root + ref;
    auto dir = base.path.substr(0, base.path.rfind('/') + 1);
    return root + dir + ref;
}

std::string xmlUnescape(std::string s) {
    const std::pair<const char*, const char*> ents[] = {{"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&apos;", "'"}, {"&amp;", "&"}};
    for (auto [from, to] : ents) {
        std::size_t p = 0;
        while ((p = s.find(from, p)) != std::string::npos) { s.replace(p, std::strlen(from), to); p += std::strlen(to); }
    }
    return s;
}

// Text of the first <name> or <prefix:name> element (no regex: std::regex recursion can overflow
// the stack on large router descriptions).
std::optional<std::string> tagValue(const std::string& xml, const std::string& name) {
    std::size_t pos = 0;
    while ((pos = xml.find('<', pos)) != std::string::npos) {
        ++pos;
        if (pos >= xml.size() || xml[pos] == '/' || xml[pos] == '?' || xml[pos] == '!') continue;
        auto nameEnd = xml.find_first_of(" \t\r\n/>", pos);
        if (nameEnd == std::string::npos) return std::nullopt;
        std::string tag = xml.substr(pos, nameEnd - pos);
        auto colon = tag.find(':');
        if (colon != std::string::npos) tag = tag.substr(colon + 1);
        if (tag != name) continue;
        auto open = xml.find('>', nameEnd);
        if (open == std::string::npos) return std::nullopt;
        if (xml[open - 1] == '/') return std::string();  // <tag/>
        auto close = xml.find('<', open + 1);
        if (close == std::string::npos) return std::nullopt;
        std::string text = xml.substr(open + 1, close - open - 1);
        const auto first = text.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return std::string();
        const auto last = text.find_last_not_of(" \t\r\n");
        return xmlUnescape(text.substr(first, last - first + 1));
    }
    return std::nullopt;
}

// ---- IGD ----

struct Gateway {
    Url control;
    std::string serviceType;
};

std::vector<std::string> discoverLocations(const PortMapper::Options& o, std::string& err) {
    std::vector<std::string> out;
    std::error_code ec;
    auto target = asio::ip::make_address_v4(o.ssdpHost, ec);
    if (ec) { err = "bad SSDP address"; return out; }

    asio::io_context io;
    udp::socket sock(io);
    sock.open(udp::v4(), ec);
    if (ec) { err = "udp: " + ec.message(); return out; }
    if (target.is_multicast()) {
        sock.set_option(asio::ip::multicast::hops(2), ec);
        if (auto lan = primaryLocalIPv4()) {
            auto lanAddr = asio::ip::make_address_v4(*lan, ec);
            if (!ec) sock.set_option(asio::ip::multicast::outbound_interface(lanAddr), ec);
        }
    }
    const udp::endpoint dest(target, o.ssdpPort);
    const char* targets[] = {
        "urn:schemas-upnp-org:device:InternetGatewayDevice:1",
        "urn:schemas-upnp-org:device:InternetGatewayDevice:2",
        "urn:schemas-upnp-org:service:WANIPConnection:1",
        "urn:schemas-upnp-org:service:WANPPPConnection:1",
    };
    std::vector<std::string> packets;
    for (auto st : targets) {
        packets.push_back(std::string("M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\nMX: 2\r\nST: ") + st + "\r\n\r\n");
    }
    for (auto& p : packets) sock.send_to(asio::buffer(p), dest, 0, ec);

    std::array<char, 2048> buf{};
    udp::endpoint from;
    std::function<void()> receive = [&] {
        sock.async_receive_from(asio::buffer(buf), from, [&](std::error_code rec, std::size_t n) {
            if (rec) return;
            std::string msg(buf.data(), n);
            std::string low = lower(msg);
            auto p = low.find("\nlocation:");
            if (p != std::string::npos) {
                p += 10;
                auto e = msg.find("\r\n", p);
                std::string loc = msg.substr(p, e == std::string::npos ? std::string::npos : e - p);
                loc.erase(0, loc.find_first_not_of(" \t"));
                while (!loc.empty() && std::isspace(static_cast<unsigned char>(loc.back()))) loc.pop_back();
                if (std::find(out.begin(), out.end(), loc) == out.end() && out.size() < 16) out.push_back(loc);
            }
            receive();
        });
    };
    receive();
    io.run_for(o.discoverTimeout);
    sock.close(ec);
    if (out.empty()) err = "no UPnP router answered";
    return out;
}

std::optional<Gateway> readDescription(const std::string& location, const PortMapper::Options& o, std::string& err) {
    auto url = parseUrl(location);
    if (!url) { err = "ignored location " + location; return std::nullopt; }
    std::string req = "GET " + url->path + " HTTP/1.1\r\nHost: " + url->host + ":" + std::to_string(url->port) +
                      "\r\nConnection: close\r\n\r\n";
    auto resp = httpRequest(url->host, url->port, req, o.requestTimeout, nullptr, err);
    if (!resp) return std::nullopt;
    if (resp->status != 200) { err = "description HTTP " + std::to_string(resp->status); return std::nullopt; }

    Url base = *url;
    if (auto ub = tagValue(resp->body, "URLBase"); ub && !ub->empty()) {
        if (auto b = parseUrl(*ub)) base = *b;
    }
    const std::string& xml = resp->body;
    // Prefer WANIPConnection:2, then :1, then PPP.
    std::optional<Gateway> best;
    int bestRank = -1;
    std::size_t pos = 0;
    while (true) {
        auto s = xml.find("<service>", pos);
        if (s == std::string::npos) break;
        auto e = xml.find("</service>", s);
        if (e == std::string::npos) break;
        std::string block = xml.substr(s, e - s);
        pos = e;
        auto type = tagValue(block, "serviceType");
        auto ctl = tagValue(block, "controlURL");
        if (!type || !ctl) continue;
        int rank = -1;
        if (type->find(":service:WANIPConnection:2") != std::string::npos) rank = 3;
        else if (type->find(":service:WANIPConnection:1") != std::string::npos) rank = 2;
        else if (type->find(":service:WANPPPConnection:1") != std::string::npos) rank = 1;
        if (rank <= bestRank) continue;
        auto control = parseUrl(resolveUrl(base, *ctl));
        if (!control) continue;
        best = Gateway{*control, *type};
        bestRank = rank;
    }
    if (!best) err = "router has no WAN connection service";
    return best;
}

struct SoapResult {
    bool ok = false;
    int upnpError = 0;  // e.g. 718 ConflictInMappingEntry, 725 OnlyPermanentLeasesSupported
    std::string body;
};

SoapResult soap(const Gateway& gw, const std::string& action, const std::string& args, const PortMapper::Options& o,
                std::string* localAddr, std::string& err) {
    std::string body = "<?xml version=\"1.0\"?>\r\n"
                       "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
                       "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body>"
                       "<u:" + action + " xmlns:u=\"" + gw.serviceType + "\">" + args + "</u:" + action +
                       "></s:Body></s:Envelope>\r\n";
    std::string req = "POST " + gw.control.path + " HTTP/1.1\r\nHost: " + gw.control.host + ":" +
                      std::to_string(gw.control.port) +
                      "\r\nContent-Type: text/xml; charset=\"utf-8\"\r\nSOAPAction: \"" + gw.serviceType + "#" + action +
                      "\"\r\nContent-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
    SoapResult r;
    auto resp = httpRequest(gw.control.host, gw.control.port, req, o.requestTimeout, localAddr, err);
    if (!resp) return r;
    r.body = resp->body;
    if (resp->status == 200) { r.ok = true; return r; }
    if (auto code = tagValue(resp->body, "errorCode")) {
        try { r.upnpError = std::stoi(*code); } catch (...) {}
    }
    err = action + " failed (HTTP " + std::to_string(resp->status) +
          (r.upnpError ? ", UPnP error " + std::to_string(r.upnpError) : std::string()) + ")";
    return r;
}

std::string mappingArgs(std::uint16_t ext, std::uint16_t internal, const std::string& client, const std::string& desc,
                        std::uint32_t lease) {
    return "<NewRemoteHost></NewRemoteHost><NewExternalPort>" + std::to_string(ext) +
           "</NewExternalPort><NewProtocol>TCP</NewProtocol><NewInternalPort>" + std::to_string(internal) +
           "</NewInternalPort><NewInternalClient>" + client + "</NewInternalClient><NewEnabled>1</NewEnabled>"
           "<NewPortMappingDescription>" + desc + "</NewPortMappingDescription><NewLeaseDuration>" +
           std::to_string(lease) + "</NewLeaseDuration>";
}

} // namespace

struct PortMapper::Impl {
    Options opts;
    mutable std::mutex mutex;
    std::condition_variable cv;
    std::thread worker;
    bool stopping = false;

    // Guarded by mutex.
    MapState state = MapState::Idle;
    std::optional<std::array<std::uint8_t, 4>> extIp;
    std::uint16_t extPort = 0;
    std::string error;

    void set(MapState s, std::string err = {}) {
        std::lock_guard lk(mutex);
        state = s;
        if (!err.empty()) error = std::move(err);
    }

    bool waitStop(std::chrono::seconds d) {
        std::unique_lock lk(mutex);
        // system_clock deadline: maps to pthread_cond_timedwait everywhere (libstdc++'s steady
        // clock path uses pthread_cond_clockwait, which ThreadSanitizer cannot see through).
        return cv.wait_until(lk, std::chrono::system_clock::now() + d, [&] { return stopping; });
    }

    void run(std::uint16_t internalPort) {
        std::string err;
        auto locations = discoverLocations(opts, err);
        std::optional<Gateway> gw;
        std::string lastErr = err;
        for (const auto& loc : locations) {
            if (waitStop(std::chrono::seconds(0))) return;
            std::string e;
            gw = readDescription(loc, opts, e);
            if (gw) break;
            lastErr = e;
        }
        if (!gw) { set(MapState::NoGateway, lastErr); return; }
        if (waitStop(std::chrono::seconds(0))) return;

        // External address.
        std::string localAddr;
        auto ipRes = soap(*gw, "GetExternalIPAddress", "", opts, &localAddr, err);
        if (!ipRes.ok) { set(MapState::Failed, err); return; }
        auto ipText = tagValue(ipRes.body, "NewExternalIPAddress");
        auto ip = ipText ? session::parseIPv4(*ipText) : std::nullopt;
        if (!ip) { set(MapState::Failed, "router did not report its public address"); return; }
        if (waitStop(std::chrono::seconds(0))) return;

        // Open the port, moving to the next one if it is taken by another device.
        std::uint32_t lease = opts.leaseSeconds;
        std::uint16_t ext = 0;
        for (int i = 0; i < opts.portAttempts && ext == 0; ++i) {
            const auto candidate = static_cast<std::uint16_t>(internalPort + i);
            auto r = soap(*gw, "AddPortMapping", mappingArgs(candidate, internalPort, localAddr, opts.description, lease), opts, nullptr, err);
            if (!r.ok && r.upnpError == 725 && lease != 0) {  // only permanent leases supported
                lease = 0;
                r = soap(*gw, "AddPortMapping", mappingArgs(candidate, internalPort, localAddr, opts.description, lease), opts, nullptr, err);
            }
            if (r.ok) ext = candidate;
            else if (r.upnpError != 718) break;  // anything but "port in use" will not get better
        }
        if (ext == 0) { set(MapState::Failed, err); return; }

        {
            std::lock_guard lk(mutex);
            extIp = ip;
            extPort = ext;
            state = isPrivateIPv4(*ip) ? MapState::DoubleNat : MapState::Mapped;
            if (state == MapState::DoubleNat) error = "router's public address " + *ipText + " is private (double NAT)";
        }

        // Keep the lease alive until stop().
        const auto renewEvery = std::chrono::seconds(lease == 0 ? 0 : std::max<std::uint32_t>(lease / 2, 30));
        while (true) {
            bool stopped = lease == 0 ? [&] { std::unique_lock lk(mutex); cv.wait(lk, [&] { return stopping; }); return true; }()
                                      : waitStop(renewEvery);
            if (stopped) break;
            soap(*gw, "AddPortMapping", mappingArgs(ext, internalPort, localAddr, opts.description, lease), opts, nullptr, err);
        }
        auto shortOpts = opts;
        shortOpts.requestTimeout = std::chrono::milliseconds(1500);
        soap(*gw, "DeletePortMapping",
             "<NewRemoteHost></NewRemoteHost><NewExternalPort>" + std::to_string(ext) + "</NewExternalPort><NewProtocol>TCP</NewProtocol>",
             shortOpts, nullptr, err);
    }
};

PortMapper::PortMapper() : PortMapper(Options{}) {}
PortMapper::PortMapper(Options opts) : m(std::make_unique<Impl>()) { m->opts = std::move(opts); }
PortMapper::~PortMapper() { stop(); }

void PortMapper::start(std::uint16_t internalPort) {
    stop();
    {
        std::lock_guard lk(m->mutex);
        m->stopping = false;
        m->state = MapState::Working;
        m->extIp.reset();
        m->extPort = 0;
        m->error.clear();
    }
    m->worker = std::thread([this, internalPort] {
        try {
            m->run(internalPort);
        } catch (const std::exception& e) {
            m->set(MapState::Failed, e.what());
        }
    });
}

void PortMapper::stop() {
    {
        std::lock_guard lk(m->mutex);
        m->stopping = true;
    }
    m->cv.notify_all();
    if (m->worker.joinable()) m->worker.join();
}

MapState PortMapper::state() const { std::lock_guard lk(m->mutex); return m->state; }
std::optional<std::array<std::uint8_t, 4>> PortMapper::externalIPv4() const { std::lock_guard lk(m->mutex); return m->extIp; }
std::uint16_t PortMapper::externalPort() const { std::lock_guard lk(m->mutex); return m->extPort; }
std::string PortMapper::lastError() const { std::lock_guard lk(m->mutex); return m->error; }

} // namespace nexlevel::net
