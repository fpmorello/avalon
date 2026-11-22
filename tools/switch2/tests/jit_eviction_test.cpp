// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Standalone host test for the JIT partial-eviction policy of the Switch 2 port
// (src/dynarmic/src/dynarmic/backend/arm64/jit_eviction.h). The policy is pure and
// platform-independent; the AddressSpace integration that calls it is guarded by
// __SWITCH__ and verified structurally (no dynarmic build is possible on a host
// without its full dependency tree).
//
// Build & run: tools/switch2/tests/run.sh jit_eviction_test

#include "dynarmic/backend/arm64/jit_eviction.h"

#include <cstdio>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using Dynarmic::Backend::Arm64::EvictBlockCount;
using Dynarmic::Backend::Arm64::IsLiveEmission;
using Dynarmic::Backend::Arm64::PruneDeadReferrers;

namespace {

int failures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
            ++failures;                                                        \
        }                                                                      \
    } while (false)

/// Guest location -> host entry point, as AddressSpace::block_entries.
using BlockEntries = std::unordered_map<int, int>;

void TestStaleQueueEntries() {
    // Emission queue: A emitted at 100, B at 200; A invalidated and emitted again at 300;
    // C emitted at 400 and then invalidated.
    const BlockEntries entries{{'A', 300}, {'B', 200}};

    // The first A is not the block that is alive now: evicting through it would drop the
    // newest copy of A as if it were the oldest block.
    CHECK(!IsLiveEmission(entries, int{'A'}, 100));
    CHECK(IsLiveEmission(entries, int{'B'}, 200));
    CHECK(IsLiveEmission(entries, int{'A'}, 300));
    // An invalidated block has no entry at all.
    CHECK(!IsLiveEmission(entries, int{'C'}, 400));
    CHECK(!IsLiveEmission(BlockEntries{}, int{'A'}, 100));
}

void TestPruneDeadReferrers() {
    // Blocks that jump to a target, by entry point; 20 and 40 were evicted.
    const std::unordered_set<int> alive{10, 30, 50};
    const auto is_alive = [&](int entry_point) { return alive.contains(entry_point); };

    std::vector<int> referrers{10, 20, 30, 40, 50};
    CHECK(PruneDeadReferrers(referrers, is_alive) == 2);
    // The live referrers stay, in order: they are what a recompiled target is linked to.
    CHECK((referrers == std::vector<int>{10, 30, 50}));
    CHECK(PruneDeadReferrers(referrers, is_alive) == 0);

    std::vector<int> all_dead{20, 40};
    CHECK(PruneDeadReferrers(all_dead, is_alive) == 2);
    CHECK(all_dead.empty());

    std::vector<int> none;
    CHECK(PruneDeadReferrers(none, is_alive) == 0);
}

} // namespace

int main() {
    TestStaleQueueEntries();
    TestPruneDeadReferrers();

    // The heap-pressure trigger only fires with at least this many live blocks.
    constexpr std::size_t kTriggerFloor = 16384;

    // Below the trigger floor the policy evicts nothing.
    CHECK(EvictBlockCount(0) == 0);
    CHECK(EvictBlockCount(1) == 0);
    CHECK(EvictBlockCount(kTriggerFloor - 1) == 0);

    // A quarter of the live blocks...
    CHECK(EvictBlockCount(40000) == 10000);
    CHECK(EvictBlockCount(100000) == 25000);

    // ...but never less than the minimum that relieves anything: a quarter of the
    // floor itself is 4096, which is too small to matter.
    CHECK(EvictBlockCount(kTriggerFloor) == 8192);
    CHECK(EvictBlockCount(20000) == 8192);

    // The policy never asks to evict everything, and never returns zero once the
    // trigger floor is reached.
    for (std::size_t live = kTriggerFloor; live <= 1000000; live += 9973) {
        const std::size_t evict = EvictBlockCount(live);
        CHECK(evict != 0);
        CHECK(evict < live);
    }

    if (failures == 0) {
        std::printf("jit_eviction_test: all checks passed\n");
        return 0;
    }
    std::printf("jit_eviction_test: %d checks failed\n", failures);
    return 1;
}
