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

using Dynarmic::Backend::Arm64::EvictBlockCount;

namespace {

int failures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
            ++failures;                                                        \
        }                                                                      \
    } while (false)

} // namespace

int main() {
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
