// Sync engine tests: NetIds, op wire format, replica rules, host sequencing, client views,
// and a randomized multi-peer convergence simulation.
#include "harness.hpp"

#include <nexlevel/core/sync/client_replica.hpp>
#include <nexlevel/core/sync/host_sequencer.hpp>

#include <deque>
#include <memory>
#include <random>
#include <string>
#include <vector>
#include <algorithm>

using namespace nexlevel;
using namespace nexlevel::sync;

namespace {

PropMap props(std::initializer_list<std::pair<const int, std::string>> kv) { return PropMap(kv); }

Op roundTrip(const Op& op, bool* ok) {
    ByteWriter w;
    encodeOp(w, op);
    ByteReader r(w.data());
    auto d = decodeOp(r);
    *ok = d.has_value() && r.atEnd();
    return d.value_or(Op{});
}

void testNetId() {
    auto id = NetId::make(7, 123456789);
    CHECK(id.peer() == 7 && id.counter() == 123456789 && id.valid());
    CHECK(!NetId{}.valid());
    NetIdAllocator a(1), b(2);
    auto a1 = a.next(), b1 = b.next();
    CHECK(a1 != b1 && a1.counter() == b1.counter());  // same counter, different peers: no collision
    a.ensureAbove(50);
    CHECK(a.next().counter() == 51);
}

void testOpWire() {
    bool ok = false;
    auto c = Op::create(NetId::make(1, 1), props({{1, "1"}, {2, "15"}, {3, "105"}}));
    c.author = 1; c.clientSeq = 9; c.seq = 42;
    auto c2 = roundTrip(c, &ok);
    CHECK(ok && c2.kind == OpKind::Create && c2.author == 1 && c2.clientSeq == 9 && c2.seq == 42 && c2.props == c.props);

    auto s = Op::setProps(NetId::make(1, 1), PropDiff{{{2, "45"}}, {21}});
    auto s2 = roundTrip(s, &ok);
    CHECK(ok && s2.kind == OpKind::SetProps && s2.props.at(2) == "45" && s2.removedKeys == std::vector<int>{21});

    auto d2 = roundTrip(Op::erase(NetId::make(3, 5)), &ok);
    CHECK(ok && d2.kind == OpKind::Delete && d2.target == NetId::make(3, 5));

    // Malformed / hostile inputs.
    ByteWriter w; encodeOp(w, c);
    auto bytes = w.data();
    for (std::size_t cut = 0; cut < bytes.size(); cut += 3) {  // every truncation fails cleanly
        ByteReader r({bytes.data(), cut});
        CHECK(!decodeOp(r).has_value());
    }
    { auto bad = bytes; bad[0] = 9; ByteReader r(bad); CHECK(!decodeOp(r)); }            // unknown kind
    { roundTrip(Op::setProps(NetId::make(1, 1), PropDiff{}), &ok); CHECK(!ok); }          // empty SetProps
    { auto x = Op::erase(NetId::make(1, 1)); x.props[1] = "1"; roundTrip(x, &ok); CHECK(!ok); }  // Delete with payload
    { roundTrip(Op::create(NetId{}, {}), &ok); CHECK(!ok); }                               // invalid target
    { auto big = Op::create(NetId::make(1, 1), props({{31, std::string(70000, 'A')}})); roundTrip(big, &ok); CHECK(!ok); }
}

void testReplicaRules() {
    Replica r;
    auto id = NetId::make(1, 1);
    CHECK(r.apply(Op::create(id, props({{1, "1"}}))));
    CHECK(!r.apply(Op::create(id, props({{1, "2"}}))));            // duplicate create ignored
    CHECK(r.find(id)->props.at(1) == "1");
    CHECK(r.apply(Op::setProps(id, PropDiff{{{2, "30"}}, {}})));
    CHECK(!r.apply(Op::setProps(id, PropDiff{{{2, "30"}}, {}})));  // no-op change reports false
    CHECK(r.apply(Op::erase(id)));
    CHECK(!r.apply(Op::create(id, props({{1, "1"}}))));            // no resurrection after delete
    CHECK(!r.apply(Op::setProps(id, PropDiff{{{2, "60"}}, {}})));  // edits to deleted objects ignored
    CHECK(r.find(id) == nullptr && r.isDeleted(id));
}

void testHostSequencer() {
    HostSequencer host;
    auto mk = [](PeerId author, std::uint32_t cs, Op op) { op.author = author; op.clientSeq = cs; return op; };
    auto r1 = host.submit(mk(1, 1, Op::create(NetId::make(1, 1), props({{1, "1"}}))));
    CHECK(r1.sequenced && r1.sequenced->seq == 1);
    auto dup = host.submit(mk(1, 1, Op::create(NetId::make(1, 1), props({{1, "1"}}))));
    CHECK(!dup.sequenced && dup.reason == HostSequencer::Reject::Duplicate);
    auto forged = host.submit(mk(2, 1, Op::create(NetId::make(1, 99), props({{1, "1"}}))));
    CHECK(!forged.sequenced && forged.reason == HostSequencer::Reject::ForgedId);
    auto r2 = host.submit(mk(2, 2, Op::setProps(NetId::make(1, 1), PropDiff{{{2, "15"}}, {}})));
    CHECK(r2.sequenced && r2.sequenced->seq == 2);  // other peers may edit any object
    auto late = host.submit(mk(3, 1, Op::setProps(NetId::make(9, 9), PropDiff{{{2, "1"}}, {}})));
    CHECK(late.sequenced && late.sequenced->seq == 3);  // no-op still sequenced (acks the author)
    CHECK(host.opsSince(1).size() == 2 && host.opsSince(3).empty());
    CHECK(host.state().find(NetId::make(1, 1))->props.at(2) == "15");
}

void testClientOptimisticView() {
    HostSequencer host;
    ClientReplica a(1), b(2);
    auto id = NetId::make(1, 1);
    auto broadcast = [&](const Op& op) { a.onSequenced(op); b.onSequenced(op); };

    broadcast(*host.submit(a.local(Op::create(id, props({{1, "1"}, {2, "0"}})))).sequenced);
    CHECK(a.pending().empty() && b.view().find(id) != nullptr);

    // A moves the object (not yet acked); B's conflicting move is sequenced first.
    auto aMove = a.local(Op::setProps(id, PropDiff{{{2, "100"}}, {}}));
    auto bSeq = *host.submit(b.local(Op::setProps(id, PropDiff{{{2, "200"}}, {}}))).sequenced;
    auto changed = a.onSequenced(bSeq);
    CHECK(changed.empty());                                   // A's own pending edit stays visible: no flicker
    CHECK(a.view().find(id)->props.at(2) == "100");
    b.onSequenced(bSeq);
    auto aSeq = *host.submit(aMove).sequenced;                // A's move is sequenced last: it wins
    a.onSequenced(aSeq);
    auto bChanged = b.onSequenced(aSeq);
    CHECK(bChanged.size() == 1);
    CHECK(a.view().fingerprint() == b.view().fingerprint());
    CHECK(a.confirmed().fingerprint() == host.state().fingerprint());
    CHECK(b.view().find(id)->props.at(2) == "100");

    // Rejected op is dropped and the view restored.
    auto bogus = b.local(Op::setProps(id, PropDiff{{{2, "999"}}, {}}));
    CHECK(b.view().find(id)->props.at(2) == "999");
    b.onRejected(bogus.clientSeq);
    CHECK(b.pending().empty() && b.view().find(id)->props.at(2) == "100");
}

// Randomized convergence: several peers edit the same objects concurrently through the host.
// Channels are reliable and FIFO per direction (as TCP/KCP will be), with random latency.
// The host also resends some ops (duplicates) to exercise dedupe.
bool simulate(unsigned seed, int peers, int steps) {
    std::mt19937 rng(seed);
    auto chance = [&](int pct) { return static_cast<int>(rng() % 100) < pct; };

    struct Msg { int deliverAt; Op op; };
    struct Peer {
        ClientReplica replica;
        NetIdAllocator ids;
        std::deque<Msg> toHost, fromHost;
        int lastUp = 0, lastDown = 0;
        std::vector<Op> sentLog;  // for duplicate injection
        Peer(PeerId p) : replica(p), ids(p) {}
    };
    std::vector<std::unique_ptr<Peer>> ps;
    for (int i = 0; i < peers; ++i) ps.push_back(std::make_unique<Peer>(static_cast<PeerId>(i + 1)));
    HostSequencer host;

    auto randomVisible = [&](const ClientReplica& r) -> NetId {
        const auto& objs = r.view().objects();
        if (objs.empty()) return NetId{};
        auto it = objs.begin();
        std::advance(it, rng() % objs.size());
        return it->first;
    };

    int now = 0;
    auto pump = [&]() {
        for (auto& p : ps) {
            while (!p->toHost.empty() && p->toHost.front().deliverAt <= now) {
                auto res = host.submit(p->toHost.front().op);
                p->toHost.pop_front();
                if (res.reason == HostSequencer::Reject::ForgedId) return false;
                if (res.sequenced) {
                    for (auto& q : ps) {
                        q->lastDown = std::max(q->lastDown, now + 1 + static_cast<int>(rng() % 8));
                        q->fromHost.push_back({q->lastDown, *res.sequenced});
                    }
                }
            }
        }
        for (auto& p : ps) {
            while (!p->fromHost.empty() && p->fromHost.front().deliverAt <= now) {
                p->replica.onSequenced(p->fromHost.front().op);
                p->fromHost.pop_front();
            }
        }
        return true;
    };

    for (; now < steps; ++now) {
        for (auto& p : ps) {
            if (!chance(40)) continue;
            Op op;
            NetId target = randomVisible(p->replica);
            int roll = static_cast<int>(rng() % 100);
            if (!target.valid() || roll < 25) {
                op = Op::create(p->ids.next(), props({{1, std::to_string(rng() % 5 + 1)}, {2, "0"}, {3, "0"}}));
            } else if (roll < 35) {
                op = Op::erase(target);
            } else {
                PropDiff d;
                d.changed.push_back({static_cast<int>(rng() % 4 + 2), std::to_string(rng() % 1000)});
                if (chance(10)) d.removed.push_back(21);
                op = Op::setProps(target, d);
            }
            auto sent = p->replica.local(std::move(op));
            p->lastUp = std::max(p->lastUp, now + 1 + static_cast<int>(rng() % 8));
            p->toHost.push_back({p->lastUp, sent});
            p->sentLog.push_back(sent);
            if (chance(3) && !p->sentLog.empty()) {  // resend an old op (e.g. after a reconnect)
                p->toHost.push_back({p->lastUp, p->sentLog[rng() % p->sentLog.size()]});
            }
        }
        if (!pump()) return false;
    }
    // Drain all in-flight messages.
    for (int extra = 0; extra < 100; ++extra, ++now) {
        if (!pump()) return false;
    }

    const auto truth = host.state().fingerprint();
    for (auto& p : ps) {
        if (!p->replica.pending().empty()) return false;
        if (p->replica.confirmed().fingerprint() != truth) return false;
        if (p->replica.view().fingerprint() != truth) return false;
    }
    return !host.state().objects().empty() || steps == 0;
}

void testConvergence() {
    int converged = 0;
    const int runs = 40;
    for (unsigned seed = 1; seed <= runs; ++seed) {
        if (simulate(seed, 2 + static_cast<int>(seed % 5), 400)) ++converged;
        else std::printf("  convergence failed for seed %u\n", seed);
    }
    CHECK(converged == runs);
}

} // namespace

void runSyncTests() {
    testNetId();
    testOpWire();
    testReplicaRules();
    testHostSequencer();
    testClientOptimisticView();
    testConvergence();
}
