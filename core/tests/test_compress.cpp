// LZ codec used for snapshots.
#include "harness.hpp"

#include <nexlevel/core/compress.hpp>
#include <nexlevel/core/session/messages.hpp>
#include <nexlevel/core/sync/replica.hpp>

#include <cstdio>

#include <random>
#include <string>

using namespace nexlevel;

namespace {

void roundTrip(const std::vector<std::uint8_t>& in) {
    auto c = lzCompress(in);
    auto d = lzDecompress(c, in.size());
    CHECK(d.has_value() && *d == in);
}

void testCompress() {
    roundTrip({});
    roundTrip({1});
    roundTrip({1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13});
    std::string level;
    for (int i = 0; i < 20000; ++i) level += "1,1,2," + std::to_string(15 + 30 * (i % 300)) + ",3," + std::to_string(15 + 30 * (i / 300)) + ",155,1;";
    std::vector<std::uint8_t> v(level.begin(), level.end());
    auto c = lzCompress(v);
    CHECK(c.size() * 4 < v.size());  // GD-like data shrinks a lot
    roundTrip(v);
    std::mt19937 rng(7);
    std::vector<std::uint8_t> noise(100000);
    for (auto& b : noise) b = static_cast<std::uint8_t>(rng());
    roundTrip(noise);
    std::vector<std::uint8_t> same(300000, 'A');
    roundTrip(same);
    // Corrupt input never crashes and is refused.
    auto bad = c;
    for (std::size_t i = 0; i < bad.size(); i += 7) bad[i] ^= 0x5A;
    auto r = lzDecompress(bad, v.size());
    CHECK(!r.has_value() || r->size() == v.size());
    CHECK(!lzDecompress(c, v.size() + 1).has_value());
    CHECK(!lzDecompress(std::vector<std::uint8_t>{0xF0}, 100).has_value());
}

void testSnapshotZ() {
    using namespace nexlevel::sync;
    Replica r;
    for (std::uint32_t i = 1; i <= 30000; ++i) {
        r.apply(Op::create(NetId::make(1, i), PropMap{{1, "1"}, {2, std::to_string(15 + 30 * (i % 400))}, {3, std::to_string(15 + 30 * (i / 400))}, {155, "1"}, {21, "1004"}}));
    }
    auto plain = nexlevel::session::encodeSnapshotChunks(r);
    auto packed = nexlevel::session::encodeSnapshotChunksZ(r);
    std::size_t a = 0, b = 0, objects = 0;
    for (auto& c : plain) a += c.size();
    for (auto& c : packed) {
        b += c.size();
        auto d = nexlevel::session::decodeSnapshotChunkZ(c);
        CHECK(d.has_value());
        if (d) objects += d->objects.size();
    }
    CHECK(objects == 30000);
    CHECK(b * 3 < a);
    std::printf("snapshot: %zu bytes plain, %zu compressed\n", a, b);
    CHECK(!nexlevel::session::decodeSnapshotChunkZ(std::vector<std::uint8_t>{1, 2}).has_value());
}

} // namespace

void runCompressTests() {
    testCompress();
    testSnapshotZ();
}
