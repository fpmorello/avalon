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

#include <cstdint>
#include <cstdio>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using Dynarmic::Backend::Arm64::EvictBlockCount;
using Dynarmic::Backend::Arm64::HeapReliefBackoff;
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

    // Compiled three times: only the newest entry point is the live block.
    const BlockEntries thrice{{'A', 500}};
    CHECK(!IsLiveEmission(thrice, int{'A'}, 100));
    CHECK(!IsLiveEmission(thrice, int{'A'}, 300));
    CHECK(IsLiveEmission(thrice, int{'A'}, 500));
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

    std::vector<int> all_alive{7};
    CHECK(PruneDeadReferrers(all_alive, [](int) { return true; }) == 0);
    CHECK((all_alive == std::vector<int>{7}));
}

constexpr std::uint64_t MiB = 1024 * 1024;

void TestHeapReliefFirstCheck() {
    // The first check that finds the heap low always relieves it.
    HeapReliefBackoff backoff;
    CHECK(backoff.ShouldRelieve(1400 * MiB));
    HeapReliefBackoff empty;
    CHECK(empty.ShouldRelieve(0));
}

void TestHeapReliefHeldUntilFurtherFall() {
    // b13 Minecraft Dungeons 2: free heap stayed about 1435-1475 MiB while the JIT
    // cleared its cache 119 times for heap. A relief that freed nothing is not repeated
    // while the heap stays where it was.
    HeapReliefBackoff backoff;
    CHECK(backoff.ShouldRelieve(1475 * MiB));
    backoff.Relieved(1475 * MiB);
    CHECK(!backoff.ShouldRelieve(1475 * MiB));
    CHECK(!backoff.ShouldRelieve(1435 * MiB));
    CHECK(!backoff.ShouldRelieve(1475 * MiB - (64 * MiB - 1)));

    // It is repeated once the heap has fallen a further 64 MiB.
    CHECK(backoff.ShouldRelieve(1475 * MiB - 64 * MiB));
    backoff.Relieved(1411 * MiB);
    CHECK(!backoff.ShouldRelieve(1400 * MiB));
    CHECK(backoff.ShouldRelieve(1347 * MiB));
}

void TestHeapReliefFollowsPartialRecovery() {
    // A heap that recovers part of the way (still below the guard) is the new reference:
    // falling back from there by 64 MiB is new pressure.
    HeapReliefBackoff backoff;
    CHECK(backoff.ShouldRelieve(1000 * MiB));
    backoff.Relieved(1000 * MiB);
    CHECK(!backoff.ShouldRelieve(1500 * MiB));
    CHECK(!backoff.ShouldRelieve(1437 * MiB));
    CHECK(backoff.ShouldRelieve(1436 * MiB));
}

void TestHeapReliefNearExhaustion() {
    // Close to running out, every check relieves, as in b13: the heap can no longer
    // fall a further step before allocations fail.
    HeapReliefBackoff backoff;
    CHECK(backoff.ShouldRelieve(60 * MiB));
    backoff.Relieved(50 * MiB);
    CHECK(backoff.ShouldRelieve(50 * MiB));
    CHECK(backoff.ShouldRelieve(0));
    backoff.Relieved(300 * MiB);
    CHECK(backoff.ShouldRelieve(256 * MiB - 1));
    backoff.Relieved(300 * MiB);
    CHECK(!backoff.ShouldRelieve(256 * MiB));
}

void TestHeapReliefReset() {
    // Once the heap is back above the guard, the next low check relieves at once.
    HeapReliefBackoff backoff;
    CHECK(backoff.ShouldRelieve(1400 * MiB));
    backoff.Relieved(1400 * MiB);
    CHECK(!backoff.ShouldRelieve(1400 * MiB));
    backoff.Reset();
    CHECK(backoff.ShouldRelieve(1400 * MiB));
}

} // namespace

int main() {
    TestStaleQueueEntries();
    TestPruneDeadReferrers();
    TestHeapReliefFirstCheck();
    TestHeapReliefHeldUntilFurtherFall();
    TestHeapReliefFollowsPartialRecovery();
    TestHeapReliefNearExhaustion();
    TestHeapReliefReset();

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
