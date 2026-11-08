#pragma once
// INTERNAL (core/src only): one framed TCP connection, driven entirely on the network thread.
#include <nexlevel/core/frame.hpp>

#include <asio.hpp>

#include <array>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace nexlevel::net {

template <class T>
class EventQueue {
public:
    void push(T v) { std::lock_guard lk(m_mutex); m_items.push_back(std::move(v)); }
    std::deque<T> drain() { std::lock_guard lk(m_mutex); std::deque<T> out; out.swap(m_items); return out; }
private:
    std::mutex m_mutex;
    std::deque<T> m_items;
};

class Connection : public std::enable_shared_from_this<Connection> {
public:
    using OnFrame = std::function<void(std::uint32_t id, Frame f)>;
    using OnClose = std::function<void(std::uint32_t id, std::string reason)>;

    Connection(asio::ip::tcp::socket socket, std::uint32_t id, OnFrame onFrame, OnClose onClose)
        : m_socket(std::move(socket)), m_id(id), m_onFrame(std::move(onFrame)), m_onClose(std::move(onClose)) {}

    std::uint32_t id() const noexcept { return m_id; }

    // Network thread only.
    void start() {
        std::error_code ec;
        m_socket.set_option(asio::ip::tcp::no_delay(true), ec);
        readMore();
    }
    void send(std::vector<std::uint8_t> bytes) {
        if (m_closed) return;
        m_writeQueue.push_back(std::move(bytes));
        if (m_writeQueue.size() == 1) writeNext();
    }
    // Send a final message (e.g. Refuse), then close once it has been written.
    void sendAndClose(std::vector<std::uint8_t> bytes, std::string reason) {
        if (m_closed) return;
        m_closeReason = std::move(reason);
        m_closeWhenFlushed = true;
        send(std::move(bytes));
    }
    void close(std::string reason) {
        if (m_closed) return;
        m_closed = true;
        std::error_code ec;
        m_socket.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
        m_socket.close(ec);
        if (m_onClose) m_onClose(m_id, std::move(reason));
    }

private:
    void readMore() {
        auto self = shared_from_this();
        m_socket.async_read_some(asio::buffer(m_readBuf), [this, self](std::error_code ec, std::size_t n) {
            if (m_closed) return;
            if (ec) { close(ec == asio::error::eof ? "peer closed" : ec.message()); return; }
            m_decoder.push({m_readBuf.data(), n});
            while (auto f = m_decoder.next()) {
                if (m_onFrame) m_onFrame(m_id, std::move(*f));
                if (m_closed) return;
            }
            if (m_decoder.error() != FrameError::None) { close("malformed frame"); return; }
            readMore();
        });
    }
    void writeNext() {
        auto self = shared_from_this();
        asio::async_write(m_socket, asio::buffer(m_writeQueue.front()), [this, self](std::error_code ec, std::size_t) {
            if (m_closed) return;
            if (ec) { close(ec.message()); return; }
            m_writeQueue.pop_front();
            if (!m_writeQueue.empty()) writeNext();
            else if (m_closeWhenFlushed) close(m_closeReason);
        });
    }

    asio::ip::tcp::socket m_socket;
    std::uint32_t m_id;
    OnFrame m_onFrame;
    OnClose m_onClose;
    std::array<std::uint8_t, 16 * 1024> m_readBuf{};
    FrameDecoder m_decoder{1024 * 1024};  // backlog/welcome can be large; per-frame cap
    std::deque<std::vector<std::uint8_t>> m_writeQueue;
    bool m_closed = false;
    bool m_closeWhenFlushed = false;
    std::string m_closeReason;
};

// An io_context running on its own thread until stop().
class NetThread {
public:
    NetThread() : m_guard(asio::make_work_guard(m_io)) {}
    ~NetThread() { stop(); }
    void start() { m_thread = std::thread([this] { m_io.run(); }); }
    void stop() {
        if (m_stopped) return;
        m_stopped = true;
        m_guard.reset();
        m_io.stop();
        if (m_thread.joinable()) m_thread.join();
    }
    asio::io_context& io() noexcept { return m_io; }
    template <class F> void post(F&& f) { asio::post(m_io, std::forward<F>(f)); }

private:
    asio::io_context m_io;
    asio::executor_work_guard<asio::io_context::executor_type> m_guard;
    std::thread m_thread;
    bool m_stopped = false;
};

} // namespace nexlevel::net
