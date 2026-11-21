// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Behavior test of SparseLargeVector on its default (non-chunked) host backend. Used for
// the differential check: run against a scratch copy of the base commit and against the
// working tree, without EDEN_CHUNKED_SPARSE_VECTOR, and compare outputs — the chunked
// backend must not change the default backend's observable behavior.
//
// Build & run: tools/switch2/tests/run.sh sparse_host_behavior_test

#include "common/sparse_large_vector.h"

#include <cstdio>
#include <cstdlib>
#include <format>
#include <string_view>

// Standalone link stubs; the real implementations live in the emulator's common library.
namespace Common::Log {
void FmtLogMessageImpl(Class, Level, const char*, unsigned int, const char*, std::string_view,
                       const std::format_args&) {}
} // namespace Common::Log
void AssertFailSoftImpl() {}
void AssertFatalImpl() {
    std::abort();
}

using Common::SparseLargeVector;

int main() {
    int line = 0;
    const auto check = [&](auto&& condition) {
        std::printf("%d %s\n", ++line, condition ? "ok" : "FAIL");
        if (!condition) {
            std::printf("sparse_host_behavior_test: FAILED at line %d\n", line);
            return 1;
        }
        return 0;
    };

    SparseLargeVector<u64> v(1 << 14);
    check(v.size() == (1 << 14));

    v.Set(100, 0x1234);
    check(v.GetOrDefault(100) == 0x1234);
    check(v[100] == 0x1234);
    check(v.GetOrDefault(101) == 0);

    v.GetAndFault(200) = 0x5678;
    check(v.GetOrDefault(200) == 0x5678);

    v.GetUnchecked(300) = 0x9abc;
    check(v.GetOrDefault(300) == 0x9abc);

    v.CommitRegion(400, 500);
    v.GetUnchecked(450) = 7;
    check(v.GetOrDefault(450) == 7);

    for (std::size_t i = 0; i < 64; ++i) {
        v.Set(1024 + i, u64(i) + 1);
    }
    v.ZeroRegion(1040, 1060);
    bool zeroed = true;
    for (std::size_t i = 1040; i < 1060; ++i) {
        zeroed = zeroed && v.GetOrDefault(i) == 0;
    }
    check(zeroed);
    check(v.GetOrDefault(1039) == 16);
    check(v.GetOrDefault(1060) == 37);

    v.ResizeAndClear(1 << 12);
    check(v.size() == (1 << 12));
    check(v.GetOrDefault(100) == 0);

    v.Set(1, 42);
    check(v.GetOrDefault(1) == 42);

    std::printf("sparse_host_behavior_test: all checks passed\n");
    return 0;
}
