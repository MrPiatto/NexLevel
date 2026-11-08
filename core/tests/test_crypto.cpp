// Session encryption: channel properties, and that a host only accepts sealed traffic.
#include "harness.hpp"

#include <nexlevel/core/crypto/secure_channel.hpp>
#include <nexlevel/core/frame.hpp>
#include <nexlevel/core/session/client.hpp>
#include <nexlevel/core/session/host.hpp>
#include <nexlevel/core/session/messages.hpp>
#include <nexlevel/core/sync/op.hpp>

#include <asio.hpp>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>

using namespace nexlevel;

namespace {

std::vector<std::uint8_t> bytesOf(const std::string& s) { return {s.begin(), s.end()}; }

bool contains(const std::vector<std::uint8_t>& hay, const std::string& needle) {
    return std::search(hay.begin(), hay.end(), needle.begin(), needle.end()) != hay.end();
}

void testChannel() {
    auto c = crypto::generateKeyPair();
    auto h = crypto::generateKeyPair();
    CHECK(c && h);
    CHECK(c->publicKey != h->publicKey);
    auto cc = crypto::SecureChannel::establish(crypto::Side::Client, *c, h->publicKey);
    auto hc = crypto::SecureChannel::establish(crypto::Side::Host, *h, c->publicKey);
    CHECK(cc && hc);

    // Both directions, many frames, payload hidden on the wire.
    const std::string secret = "obj 1,1,2,255,3,135;secret-level-data";
    bool allOk = true, hidden = true;
    for (int i = 0; i < 200; ++i) {
        auto up = cc->seal(10, bytesOf(secret + std::to_string(i)));
        auto down = hc->seal(11, bytesOf(secret + std::to_string(i)));
        hidden = hidden && !contains(up, "secret-level") && !contains(down, "secret-level");
        auto a = hc->open(up);
        auto b = cc->open(down);
        allOk = allOk && a && b && a->type == 10 && b->type == 11 && a->payload == bytesOf(secret + std::to_string(i)) &&
                b->payload == a->payload;
    }
    CHECK(allOk);
    CHECK(hidden);

    // Same plaintext twice gives different ciphertext (counter nonce).
    CHECK(cc->seal(1, bytesOf("x")) != cc->seal(1, bytesOf("x")));
}

void testTamperReplayReorder() {
    auto c = crypto::generateKeyPair();
    auto h = crypto::generateKeyPair();
    auto make = [&] {
        return std::pair{*crypto::SecureChannel::establish(crypto::Side::Client, *c, h->publicKey),
                         *crypto::SecureChannel::establish(crypto::Side::Host, *h, c->publicKey)};
    };
    {   // modified byte
        auto [cc, hc] = make();
        auto f = cc.seal(10, bytesOf("hello"));
        f[f.size() - 1] ^= 1;
        CHECK(!hc.open(f));
        CHECK(!hc.open(cc.seal(10, bytesOf("next"))));  // broken channel stays closed
    }
    {   // replay
        auto [cc, hc] = make();
        auto f = cc.seal(10, bytesOf("once"));
        CHECK(hc.open(f));
        CHECK(!hc.open(f));
    }
    {   // reorder / drop
        auto [cc, hc] = make();
        auto f1 = cc.seal(10, bytesOf("1"));
        auto f2 = cc.seal(10, bytesOf("2"));
        (void)f1;
        CHECK(!hc.open(f2));
    }
    {   // a frame sealed for the other direction is refused (separate keys)
        auto [cc, hc] = make();
        auto f = cc.seal(10, bytesOf("mine"));
        CHECK(!cc.open(f));
    }
    {   // someone else's keys
        auto e = crypto::generateKeyPair();
        auto [cc, hc] = make();
        auto eve = *crypto::SecureChannel::establish(crypto::Side::Host, *e, c->publicKey);
        CHECK(!eve.open(cc.seal(10, bytesOf("private"))));
    }
    {   // too short
        auto [cc, hc] = make();
        CHECK(!hc.open(std::vector<std::uint8_t>(10, 0)));
    }
    // Low-order public key (all zero) is rejected.
    crypto::Key32 zero{};
    CHECK(!crypto::SecureChannel::establish(crypto::Side::Host, *h, zero));
}

// A raw client that does the key exchange correctly, then sends a plain (unsealed) Submit:
// the host must drop it rather than apply the op.
void testHostRejectsPlainTraffic() {
    session::SessionHost host(GameTarget::GD2208, "Host");
    CHECK(host.start(0, true));

    asio::io_context io;
    asio::ip::tcp::socket sock(io);
    std::error_code ec;
    sock.connect({asio::ip::make_address_v4("127.0.0.1"), host.port()}, ec);
    CHECK(!ec);
    auto keys = crypto::generateKeyPair();
    session::Hello hello{GameTarget::GD2208, host.roomKey(), 0, "Raw", keys->publicKey};
    asio::write(sock, asio::buffer(encodeFrame(static_cast<std::uint16_t>(session::MsgType::Hello), session::encode(hello))), ec);

    // Read until the Secure frame arrives (pumping the host meanwhile).
    FrameDecoder dec(1 << 20);
    std::optional<Frame> secure;
    sock.non_blocking(true);
    std::array<std::uint8_t, 4096> buf{};
    for (int i = 0; i < 400 && !secure; ++i) {
        host.pump();
        auto n = sock.read_some(asio::buffer(buf), ec);
        if (!ec) dec.push(std::span(buf.data(), n));
        while (auto f = dec.next()) {
            if (static_cast<session::MsgType>(f->type) == session::MsgType::Secure) { secure = f; break; }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(secure.has_value());
    CHECK(host.peers().size() == 2);

    sync::Op op = sync::Op::create(sync::NetId::make(2, 1), {{1, "1"}, {2, "50"}, {3, "50"}});
    op.clientSeq = 1;
    asio::write(sock, asio::buffer(encodeFrame(static_cast<std::uint16_t>(session::MsgType::Submit), session::encodeOpMsg(op))), ec);
    bool closed = false;
    for (int i = 0; i < 400 && !closed; ++i) {
        host.pump();
        auto n = sock.read_some(asio::buffer(buf), ec);
        (void)n;
        closed = ec == asio::error::eof || ec == asio::error::connection_reset || ec == asio::error::connection_aborted;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(closed);
    CHECK(host.state().objects().empty());  // the plain op was never applied
    host.stop();
}

} // namespace

void runCryptoTests() {
    testChannel();
    testTamperReplayReorder();
    testHostRejectsPlainTraffic();
}
