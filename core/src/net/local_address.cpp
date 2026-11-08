#include <nexlevel/core/net/local_address.hpp>

#include <asio.hpp>

namespace nexlevel::net {

std::optional<std::string> primaryLocalIPv4() {
    try {
        asio::io_context io;
        asio::ip::udp::socket s(io);
        std::error_code ec;
        s.open(asio::ip::udp::v4(), ec);
        if (ec) return std::nullopt;
        s.connect(asio::ip::udp::endpoint(asio::ip::make_address_v4("8.8.8.8", ec), 53), ec);
        if (ec) return std::nullopt;
        auto addr = s.local_endpoint(ec).address();
        if (ec || addr.is_unspecified()) return std::nullopt;
        return addr.to_string();
    } catch (...) {
        return std::nullopt;
    }
}

} // namespace nexlevel::net
