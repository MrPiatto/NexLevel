// Saving the room into a level string (autosave while the host is outside the editor).
#include "harness.hpp"

#include <nexlevel/core/sync/level_settings.hpp>
#include <nexlevel/core/sync/level_string.hpp>
#include <nexlevel/core/sync/replica.hpp>

using namespace nexlevel::sync;

namespace {

void testBuildFromReplica() {
    Replica r;
    r.apply(Op::create(NetId::make(2, 1), PropMap{{1, "1"}, {2, "15"}, {3, "45"}}));
    r.apply(Op::create(NetId::make(3, 1), PropMap{{1, "8"}, {2, "75"}, {3, "15"}}));
    r.apply(Op::create(kLevelSettingsId, levelSettingsToProps("kA2,1,kA4,2", SongFields{0, 99, "", ""})));
    // Base header: kA2 overridden, kS39 (editor-only) and the empty kA14 kept.
    auto s = buildLevelString(r, "kA2,0,kS39,3,kA14,");
    CHECK(levelStringHeader(s) == "kA2,1,kS39,3,kA14,,kA4,2");
    CHECK(s.find(";1,1,2,15,3,45;") != std::string::npos);
    CHECK(s.find(";1,8,2,75,3,15;") != std::string::npos);
    CHECK(s.back() == ';');
    // Settings object never appears as a game object.
    CHECK(s.find("600002") == std::string::npos);
}

void testEmpty() {
    Replica r;
    CHECK(buildLevelString(r, "") == ";");
    CHECK(buildLevelString(r, "kA2,0") == "kA2,0;");
    CHECK(levelStringHeader("abc") == "abc");
    CHECK(levelStringHeader("a;b;") == "a");
}

} // namespace

void runLevelStringTests() {
    testBuildFromReplica();
    testEmpty();
}
