#include <nexlevel/core/crypto/secure_channel.hpp>

#include <monocypher.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <random>
#include <cstddef>

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#elif defined(__APPLE__)
#include <stdlib.h>  // arc4random_buf (macOS / iOS have no getrandom)
#else
#include <sys/random.h>
#endif

namespace nexlevel::crypto {

bool secureRandom(std::span<std::uint8_t> out) noexcept {
#ifdef _WIN32
    return BCryptGenRandom(nullptr, out.data(), static_cast<ULONG>(out.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) >= 0;
#elif defined(__APPLE__)
    arc4random_buf(out.data(), out.size());  // system CSPRNG, cannot fail
    return true;
#else
    std::size_t done = 0;
    while (done < out.size()) {
        auto n = getrandom(out.data() + done, out.size() - done, 0);
        if (n <= 0) return false;
        done += static_cast<std::size_t>(n);
    }
    return true;
#endif
}

std::uint64_t secureRandomU64() {
    std::array<std::uint8_t, 8> b{};
    std::uint64_t v = 0;
    if (secureRandom(b)) {
        for (auto x : b) v = (v << 8) | x;
        return v;
    }
    std::random_device rd;
    return (static_cast<std::uint64_t>(rd()) << 32) ^ rd();
}

KeyPair::~KeyPair() { crypto_wipe(secret.data(), secret.size()); }

std::optional<KeyPair> generateKeyPair() {
    KeyPair kp;
    if (!secureRandom(kp.secret)) return std::nullopt;
    crypto_x25519_public_key(kp.publicKey.data(), kp.secret.data());
    return kp;
}

SecureChannel::~SecureChannel() {
    crypto_wipe(m_sendKey.data(), m_sendKey.size());
    crypto_wipe(m_recvKey.data(), m_recvKey.size());
}

std::optional<SecureChannel> SecureChannel::establish(Side side, const KeyPair& mine, const Key32& theirPublic) {
    std::uint8_t shared[32];
    crypto_x25519(shared, mine.secret.data(), theirPublic.data());
    const std::uint8_t zero[32] = {};
    if (crypto_verify32(shared, zero) == 0) {  // low-order point: no contributory key
        crypto_wipe(shared, sizeof shared);
        return std::nullopt;
    }
    const Key32& clientPk = side == Side::Client ? mine.publicKey : theirPublic;
    const Key32& hostPk = side == Side::Client ? theirPublic : mine.publicKey;

    std::uint8_t keys[64];
    crypto_blake2b_ctx ctx;
    crypto_blake2b_init(&ctx, 64);
    crypto_blake2b_update(&ctx, shared, 32);
    crypto_blake2b_update(&ctx, clientPk.data(), 32);
    crypto_blake2b_update(&ctx, hostPk.data(), 32);
    crypto_blake2b_final(&ctx, keys);
    crypto_wipe(shared, sizeof shared);

    SecureChannel ch;
    // keys[0..32): client -> host, keys[32..64): host -> client.
    const std::uint8_t* c2h = keys;
    const std::uint8_t* h2c = keys + 32;
    std::memcpy(ch.m_sendKey.data(), side == Side::Client ? c2h : h2c, 32);
    std::memcpy(ch.m_recvKey.data(), side == Side::Client ? h2c : c2h, 32);
    crypto_wipe(keys, sizeof keys);
    return ch;
}

namespace {
void counterNonce(std::uint64_t counter, std::uint8_t nonce[24]) {
    std::memset(nonce, 0, 24);
    for (int i = 0; i < 8; ++i) nonce[i] = static_cast<std::uint8_t>(counter >> (8 * i));
}
} // namespace

std::vector<std::uint8_t> SecureChannel::seal(std::uint16_t type, std::span<const std::uint8_t> payload) {
    std::vector<std::uint8_t> plain(2 + payload.size());
    plain[0] = static_cast<std::uint8_t>(type);
    plain[1] = static_cast<std::uint8_t>(type >> 8);
    if (!payload.empty()) std::memcpy(plain.data() + 2, payload.data(), payload.size());

    std::vector<std::uint8_t> out(16 + plain.size());
    std::uint8_t nonce[24];
    counterNonce(m_sendCounter++, nonce);
    crypto_aead_lock(out.data() + 16, out.data(), m_sendKey.data(), nonce, nullptr, 0, plain.data(), plain.size());
    crypto_wipe(plain.data(), plain.size());
    return out;
}

std::optional<Frame> SecureChannel::open(std::span<const std::uint8_t> sealed) {
    if (m_broken || sealed.size() < kSealOverhead) { m_broken = true; return std::nullopt; }
    const std::size_t textSize = sealed.size() - 16;
    std::vector<std::uint8_t> plain(textSize);
    std::uint8_t nonce[24];
    counterNonce(m_recvCounter, nonce);
    if (crypto_aead_unlock(plain.data(), sealed.data(), m_recvKey.data(), nonce, nullptr, 0, sealed.data() + 16, textSize) != 0) {
        m_broken = true;
        return std::nullopt;
    }
    ++m_recvCounter;
    Frame f;
    f.type = static_cast<std::uint16_t>(plain[0] | (plain[1] << 8));
    f.payload.assign(plain.begin() + 2, plain.end());
    return f;
}

} // namespace nexlevel::crypto
