// Minimal dependency-free test runner for nexlevel_core.
#include <nexlevel/core/bytes.hpp>
#include <nexlevel/core/frame.hpp>
#include <nexlevel/core/version.hpp>

#include <cstdio>
#include <string>

static int g_failed = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failed; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

using namespace nexlevel;

static void testBytesRoundTrip() {
    ByteWriter w;
    w.u8(0xAB); w.u16(0xBEEF); w.u32(0xDEADBEEF); w.u64(0x0123456789ABCDEFull); w.f32(1.5f); w.str("NexLevel");
    ByteReader r(w.data());
    CHECK(r.u8() == 0xAB);
    CHECK(r.u16() == 0xBEEF);
    CHECK(r.u32() == 0xDEADBEEF);
    CHECK(r.u64() == 0x0123456789ABCDEFull);
    CHECK(r.f32() == 1.5f);
    CHECK(r.str(64) == "NexLevel");
    CHECK(r.atEnd());
}

static void testBytesLittleEndian() {
    ByteWriter w; w.u32(0x04030201);
    CHECK(w.data().size() == 4 && w.data()[0] == 1 && w.data()[3] == 4);
}

static void testReaderTruncationIsSticky() {
    std::uint8_t buf[3] = {1, 2, 3};
    ByteReader r(buf);
    CHECK(r.u32() == 0);
    CHECK(!r.ok());
    CHECK(r.u8() == 0);   // stays failed
    CHECK(!r.ok());
}

static void testStringLimit() {
    ByteWriter w; w.str(std::string(100, 'x'));
    ByteReader r(w.data());
    CHECK(r.str(10).empty());
    CHECK(!r.ok());
}

static void testHugeLengthPrefix() {
    ByteWriter w; w.u32(0xFFFFFFFF);  // claims 4 GB string
    ByteReader r(w.data());
    CHECK(r.str(0xFFFFFFFF).empty());
    CHECK(!r.ok());
}

static void testFrameRoundTripSplitFeed() {
    std::vector<std::uint8_t> payload = {10, 20, 30, 40, 50};
    auto a = encodeFrame(7, payload);
    auto b = encodeFrame(8, {});
    std::vector<std::uint8_t> stream(a); stream.insert(stream.end(), b.begin(), b.end());

    FrameDecoder d;
    for (auto byte : stream) d.push(std::span<const std::uint8_t>(&byte, 1));  // worst-case byte-by-byte arrival
    auto f1 = d.next();
    CHECK(f1 && f1->type == 7 && f1->payload == payload);
    auto f2 = d.next();
    CHECK(f2 && f2->type == 8 && f2->payload.empty());
    CHECK(!d.next());
    CHECK(d.error() == FrameError::None);
    CHECK(d.buffered() == 0);
}

static void testFramePartialHeaderWaits() {
    auto a = encodeFrame(1, std::vector<std::uint8_t>{1, 2, 3});
    FrameDecoder d;
    d.push(std::span<const std::uint8_t>(a.data(), 5));
    CHECK(!d.next());
    CHECK(d.error() == FrameError::None);
    d.push(std::span<const std::uint8_t>(a.data() + 5, a.size() - 5));
    CHECK(d.next().has_value());
}

static void testFrameBadMagic() {
    auto a = encodeFrame(1, {});
    a[0] ^= 0xFF;
    FrameDecoder d; d.push(a);
    CHECK(!d.next());
    CHECK(d.error() == FrameError::BadMagic);
    d.push(encodeFrame(1, {}));   // error is permanent
    CHECK(!d.next());
}

static void testFrameTooLarge() {
    FrameDecoder d(16);
    d.push(encodeFrame(1, std::vector<std::uint8_t>(17, 0)));
    CHECK(!d.next());
    CHECK(d.error() == FrameError::TooLarge);
}

static void testSessionCompatibility() {
    CHECK(canShareSession(GameTarget::GD2208, GameTarget::GD2208));
    CHECK(!canShareSession(GameTarget::GD2207, GameTarget::GD2209));
    CHECK(!canShareSession(GameTarget::Unknown, GameTarget::Unknown));
}

int main() {
    testBytesRoundTrip();
    testBytesLittleEndian();
    testReaderTruncationIsSticky();
    testStringLimit();
    testHugeLengthPrefix();
    testFrameRoundTripSplitFeed();
    testFramePartialHeaderWaits();
    testFrameBadMagic();
    testFrameTooLarge();
    testSessionCompatibility();
    std::printf("%d checks, %d failed\n", g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
