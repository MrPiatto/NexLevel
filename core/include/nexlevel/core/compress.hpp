#pragma once
// Small, dependency-free LZ77 codec in the LZ4 block format. Used for the level snapshot sent to
// joining players: GD object data is very repetitive, so it shrinks several times and big levels
// join much faster (especially through a relay or on mobile data).
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace nexlevel {

std::vector<std::uint8_t> lzCompress(std::span<const std::uint8_t> in);

// rawSize: the exact decompressed size (sent alongside). Returns nullopt for corrupt input.
std::optional<std::vector<std::uint8_t>> lzDecompress(std::span<const std::uint8_t> in, std::size_t rawSize);

} // namespace nexlevel
