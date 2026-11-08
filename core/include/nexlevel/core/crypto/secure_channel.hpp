#pragma once
// Encrypted, tamper-proof session traffic.
//
// Key exchange: each side makes a fresh X25519 key pair per connection (client key in Hello,
// host key in Secure). Both derive two 32-byte keys with BLAKE2b(shared || client_pk || host_pk),
// one per direction, as the Monocypher manual recommends.
// Frames: XChaCha20-Poly1305 (Monocypher crypto_aead_lock). The nonce is a per-direction counter
// that is never sent: a replayed, dropped, reordered or modified frame fails to decrypt and the
// connection is closed.
//
// Limitation (V1): the exchange is not authenticated, so it stops anyone from reading or changing
// traffic they only observe (public Wi-Fi, ISP), but not an attacker who actively intercepts the
// connection from the start.
#include <nexlevel/core/frame.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace nexlevel::crypto {

using Key32 = std::array<std::uint8_t, 32>;

// Operating-system CSPRNG (BCryptGenRandom / getrandom). False only if the OS refuses.
bool secureRandom(std::span<std::uint8_t> out) noexcept;
std::uint64_t secureRandomU64();  // falls back to std::random_device only if the OS RNG fails

struct KeyPair {
    Key32 secret{};
    Key32 publicKey{};
    KeyPair() = default;
    KeyPair(const KeyPair&) = default;
    KeyPair& operator=(const KeyPair&) = default;
    ~KeyPair();  // wipes the secret
};
std::optional<KeyPair> generateKeyPair();

enum class Side : std::uint8_t { Client, Host };

inline constexpr std::size_t kSealOverhead = 16 + 2;  // MAC + inner type

class SecureChannel {
public:
    // nullopt if their key is invalid (low-order point: all-zero shared secret).
    static std::optional<SecureChannel> establish(Side side, const KeyPair& mine, const Key32& theirPublic);

    SecureChannel(const SecureChannel&) = default;
    SecureChannel& operator=(const SecureChannel&) = default;
    ~SecureChannel();

    // Payload of a Sealed frame carrying (type, payload).
    std::vector<std::uint8_t> seal(std::uint16_t type, std::span<const std::uint8_t> payload);
    // The frame inside a Sealed payload; nullopt if it was tampered with, replayed or out of order.
    std::optional<Frame> open(std::span<const std::uint8_t> sealed);

private:
    SecureChannel() = default;
    Key32 m_sendKey{};
    Key32 m_recvKey{};
    std::uint64_t m_sendCounter = 0;
    std::uint64_t m_recvCounter = 0;
    bool m_broken = false;  // after a failed open() nothing more is accepted
};

} // namespace nexlevel::crypto
