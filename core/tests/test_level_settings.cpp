// Level settings (colours, background, song, ...) as one synced object.
#include "harness.hpp"

#include <nexlevel/core/sync/client_replica.hpp>
#include <nexlevel/core/sync/host_sequencer.hpp>
#include <nexlevel/core/sync/level_settings.hpp>
#include <nexlevel/core/sync/op.hpp>

using namespace nexlevel::sync;

namespace {

const char* kHeader =
    "kS38,1_40_2_125_3_255_11_255_12_255_13_255_4_-1_6_1000_7_1_15_1_18_0_8_1|"
    "1_0_2_102_3_255_11_255_12_255_13_255_4_-1_6_1001_7_1_15_1_18_0_8_1|"
    "1_255_2_0_3_0_11_255_12_255_13_255_4_-1_6_1_7_1_15_1_18_0_8_1|,"
    "kA13,0.25,kA15,0,kA16,0,kA14,,kA6,3,kA7,2,kA17,0,kA18,0,kS39,2,kA2,0,kA3,0,kA8,0,kA4,1,kA9,0,kA10,0,kA22,1,kA23,0";

std::string rebuild(const LevelSettingsData& d) {
    std::string out;
    for (auto& [k, v] : d.header) out += (out.empty() ? "" : ",") + k + "," + v;
    return out;
}

void testRoundTrip() {
    SongFields song{0, 1234567, "1234567,7654321", "", };
    auto props = levelSettingsToProps(kHeader, song);
    CHECK(props.size() == 3 + 16 + 4);  // 3 colour channels, 16 header keys (kS39 dropped), 4 song
    auto d = propsToLevelSettings(props);
    CHECK(d.song == song);
    auto again = levelSettingsToProps(rebuild(d), d.song);
    CHECK(again == props);
    // kS38 comes back with every channel, ordered, '|'-terminated.
    bool hasColors = false;
    for (auto& [k, v] : d.header) {
        if (k == "kS38") {
            hasColors = true;
            CHECK(v.find("_6_1_") != std::string::npos && v.find("_6_1000_") != std::string::npos && v.back() == '|');
        }
        CHECK(k != "kS39");
    }
    CHECK(hasColors);
}

void testIndependentSettings() {
    auto base = levelSettingsToProps(kHeader, {});
    // Player A changes the background, player B changes the BG colour channel: both survive.
    std::string a = kHeader;
    a.replace(a.find("kA6,3"), 5, "kA6,9");
    std::string b = kHeader;
    b.replace(b.find("1_40_2_125_3_255"), 16, "1_10_2_200_3_30");
    auto da = diffProps(base, levelSettingsToProps(a, {}));
    auto db = diffProps(base, levelSettingsToProps(b, {}));
    CHECK(da.changed.size() == 1 && da.removed.empty() && db.changed.size() == 1 && db.removed.empty());

    HostSequencer host;
    CHECK(host.submit([&] { Op o = Op::create(kLevelSettingsId, base); o.author = 1; o.clientSeq = 1; return o; }()).sequenced);
    CHECK(host.submit([&] { Op o = Op::setProps(kLevelSettingsId, da); o.author = 2; o.clientSeq = 1; return o; }()).sequenced);
    CHECK(host.submit([&] { Op o = Op::setProps(kLevelSettingsId, db); o.author = 3; o.clientSeq = 1; return o; }()).sequenced);
    const auto* s = host.state().find(kLevelSettingsId);
    CHECK(s != nullptr);
    if (!s) return;
    auto merged = propsToLevelSettings(s->props);
    auto text = rebuild(merged);
    CHECK(text.find("kA6,9") != std::string::npos);
    CHECK(text.find("1_10_2_200_3_30") != std::string::npos);

    // Reserved id: never produced by an allocator, and only the host may create it.
    NetIdAllocator alloc(1);
    CHECK(alloc.next() != kLevelSettingsId);
    Op forged = Op::create(kLevelSettingsId, base);
    forged.author = 2;
    forged.clientSeq = 2;
    CHECK(!host.submit(forged).sequenced);
}

} // namespace

void runLevelSettingsTests() {
    testRoundTrip();
    testIndependentSettings();
}
