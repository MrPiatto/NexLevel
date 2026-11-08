// Asio is included only in core .cpp files, never in headers (keeps windows.h/winsock out of mod code).
#include <nexlevel/core/net/loopback_probe.hpp>

#include <nexlevel/core/frame.hpp>

#include <asio.hpp>

#include <array>
#include <functional>
#include <vector>

namespace nexlevel::net {

ProbeResult runLoopbackProbe(std::chrono::milliseconds timeout) {
    using asio::ip::tcp;
    ProbeResult r;

    try {
        asio::io_context io;
        std::error_code ec;

        tcp::acceptor acceptor(io);
        const tcp::endpoint listenAt(asio::ip::address_v4::loopback(), 0);
        acceptor.open(listenAt.protocol(), ec);
        if (!ec) acceptor.bind(listenAt, ec);
        if (!ec) acceptor.listen(asio::socket_base::max_listen_connections, ec);
        if (ec) { r.error = "listen: " + ec.message(); return r; }
        r.port = acceptor.local_endpoint(ec).port();

        tcp::socket serverSock(io), clientSock(io);
        asio::steady_timer timer(io, timeout);
        bool done = false;

        auto finish = [&](bool ok, std::string err) {
            if (done) return;
            done = true;
            r.ok = ok;
            r.error = std::move(err);
            std::error_code ignore;
            timer.cancel();
            acceptor.close(ignore);
            serverSock.close(ignore);
            clientSock.close(ignore);
        };

        timer.async_wait([&](std::error_code e) { if (!e) finish(false, "timeout"); });

        // --- server side: echo every complete frame back ---
        std::array<std::uint8_t, 4096> serverBuf{};
        FrameDecoder serverDec;
        std::vector<std::uint8_t> serverOut;
        std::function<void()> serverRead = [&] {
            serverSock.async_read_some(asio::buffer(serverBuf), [&](std::error_code e, std::size_t n) {
                if (e) { finish(false, "server read: " + e.message()); return; }
                serverDec.push({serverBuf.data(), n});
                if (serverDec.error() != FrameError::None) { finish(false, "server: malformed frame"); return; }
                if (auto f = serverDec.next()) {
                    serverOut = encodeFrame(f->type, f->payload);
                    asio::async_write(serverSock, asio::buffer(serverOut), [&](std::error_code we, std::size_t) {
                        if (we) finish(false, "server write: " + we.message());
                    });
                } else {
                    serverRead();
                }
            });
        };
        acceptor.async_accept(serverSock, [&](std::error_code e) {
            if (e) { finish(false, "accept: " + e.message()); return; }
            serverRead();
        });

        // --- client side: send one frame, expect it back unchanged ---
        std::vector<std::uint8_t> payload(256);
        for (std::size_t i = 0; i < payload.size(); ++i) payload[i] = static_cast<std::uint8_t>(i * 31 + 7);
        const auto clientOut = encodeFrame(kProbeFrameType, payload);

        std::array<std::uint8_t, 4096> clientBuf{};
        FrameDecoder clientDec;
        std::size_t clientReceived = 0;
        auto start = std::chrono::steady_clock::now();

        std::function<void()> clientRead = [&] {
            clientSock.async_read_some(asio::buffer(clientBuf), [&](std::error_code e, std::size_t n) {
                if (e) { finish(false, "client read: " + e.message()); return; }
                clientReceived += n;
                clientDec.push({clientBuf.data(), n});
                if (clientDec.error() != FrameError::None) { finish(false, "client: malformed frame"); return; }
                if (auto f = clientDec.next()) {
                    if (f->type != kProbeFrameType || f->payload != payload) { finish(false, "echo mismatch"); return; }
                    r.roundTripMicros = static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now() - start).count());
                    r.bytesExchanged = clientOut.size() + clientReceived;
                    finish(true, {});
                } else {
                    clientRead();
                }
            });
        };

        clientSock.async_connect(tcp::endpoint(asio::ip::address_v4::loopback(), r.port), [&](std::error_code e) {
            if (e) { finish(false, "connect: " + e.message()); return; }
            start = std::chrono::steady_clock::now();
            asio::async_write(clientSock, asio::buffer(clientOut), [&](std::error_code we, std::size_t) {
                if (we) { finish(false, "client write: " + we.message()); return; }
                clientRead();
            });
        });

        io.run();
    } catch (const std::exception& e) {
        r.ok = false;
        r.error = std::string("exception: ") + e.what();
    }
    return r;
}

} // namespace nexlevel::net
