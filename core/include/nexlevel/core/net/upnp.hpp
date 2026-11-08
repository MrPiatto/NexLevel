#pragma once
// Automatic router port opening (UPnP IGD), so players outside the host's network can join
// without anyone touching router settings.
//
// Implemented directly on the standard protocols (SSDP discovery, IGD device description, SOAP
// AddPortMapping / GetExternalIPAddress / DeletePortMapping on WANIPConnection:1/2 or
// WANPPPConnection:1). No extra dependency, no admin rights, nothing changed on the PC.
//
// It only works when the router has UPnP enabled (most home routers do by default). When it does
// not, or the router is itself behind another NAT (common with mobile/CGNAT providers), state()
// says so and the room stays reachable on the local network only.
#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace nexlevel::net {

enum class MapState : std::uint8_t {
    Idle,       // start() not called
    Working,    // looking for the router / opening the port
    Mapped,     // port open: externalIPv4():externalPort() reaches us from the internet
    NoGateway,  // no UPnP router answered (UPnP off, or not supported)
    DoubleNat,  // port opened, but the router's own "public" address is private: still not reachable
    Failed,     // the router refused the mapping
};

const char* toString(MapState s) noexcept;

// True for addresses that are not reachable from the internet (RFC 1918, CGNAT 100.64/10,
// loopback, link-local, 0/8).
bool isPrivateIPv4(const std::array<std::uint8_t, 4>& ip) noexcept;

class PortMapper {
public:
    struct Options {
        std::string ssdpHost = "239.255.255.250";  // tests point this at a mock gateway
        std::uint16_t ssdpPort = 1900;
        std::chrono::milliseconds discoverTimeout{2500};
        std::chrono::milliseconds requestTimeout{3000};
        std::uint32_t leaseSeconds = 3600;  // renewed every lease/2; 0 = permanent
        std::string description = "NexLevel (Geometry Dash)";
        int portAttempts = 8;  // if the external port is taken, try the next ones
    };

    PortMapper();
    explicit PortMapper(Options opts);
    ~PortMapper();  // same as stop()
    PortMapper(const PortMapper&) = delete;
    PortMapper& operator=(const PortMapper&) = delete;

    // Starts mapping internalPort in the background (never blocks the caller).
    void start(std::uint16_t internalPort);
    // Removes the mapping from the router and stops renewing it. Blocks for at most a few seconds.
    void stop();

    MapState state() const;
    std::optional<std::array<std::uint8_t, 4>> externalIPv4() const;  // set in Mapped and DoubleNat
    std::uint16_t externalPort() const;                               // set in Mapped and DoubleNat
    std::string lastError() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m;
};

} // namespace nexlevel::net
