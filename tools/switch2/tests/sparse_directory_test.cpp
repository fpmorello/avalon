// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Standalone host test for the chunk directory that dynarmic's two-level page table lookup
// reads on Switch 2 (odd/tasks/two-level-page-table.md, T1). The JIT computes
//   entry = directory[page >> ChunkIndexBits()][page & ((1 << ChunkIndexBits()) - 1)]
// with plain loads and no null check on the directory slot, so every slot must point to
// readable memory: an unmaterialized slot points to one shared all-zero chunk.
//
// Build & run: tools/switch2/tests/run.sh sparse_directory_test

#include "common/sparse_large_vector.h"

#include <cstdio>
#include <cstdlib>
#include <format>
#include <string_view>

// Standalone link: assertion and log statements instantiate these; the real
// implementations live in the emulator's common library, which this test does not build.
namespace Common::Log {
void FmtLogMessageImpl(Class, Level, const char*, unsigned int, const char*, std::string_view,
                       const std::format_args&) {}
} // namespace Common::Log
void AssertFailSoftImpl() {}
void AssertFatalImpl() {
    std::abort();
}

using Common::SparseLargeVector;

namespace {

int failures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
            ++failures;                                                        \
        }                                                                      \
    } while (false)

using Vector = SparseLargeVector<u64>;

constexpr std::size_t kBits = Vector::ChunkIndexBits();
constexpr std::size_t kEntries = std::size_t{1} << kBits;

/// What the JIT reads for index, through the directory only.
u64 JitRead(const Vector& v, std::size_t index) {
    return v.ChunkDirectory()[index >> kBits][index & (kEntries - 1)];
}

bool ChunkIsZero(const u64* chunk) {
    for (std::size_t i = 0; i < kEntries; ++i) {
        if (chunk[i] != 0) {
            return false;
        }
    }
    return true;
}

void TestIndexBits() {
    // 2 MiB chunks of 8-byte entries.
    CHECK(kBits == 18);
    CHECK(kEntries == Vector::ChunkEntriesPerChunk());
}

void TestFreshDirectoryPointsToSharedZeroChunk() {
    Vector v(kEntries * 4);
    const u64* const* dir = v.ChunkDirectory();
    CHECK(dir != nullptr);
    for (std::size_t slot = 0; slot < 4; ++slot) {
        CHECK(dir[slot] != nullptr);
        CHECK(dir[slot] == dir[0]);
    }
    CHECK(ChunkIsZero(dir[0]));
    CHECK(JitRead(v, 0) == 0);
    CHECK(JitRead(v, kEntries * 4 - 1) == 0);
    // Reading through the directory never allocates.
    CHECK(Common::SparseTestStats::ChunksLive() == 0);

    // Every vector shares the same zero chunk.
    Vector other(kEntries);
    CHECK(other.ChunkDirectory()[0] == dir[0]);
}

void TestWritesAreVisibleThroughDirectory() {
    Vector v(kEntries * 4);
    const u64* const zero = v.ChunkDirectory()[0];

    v.Set(kEntries + 7, 0x1234);
    v.GetUnchecked(kEntries * 3) = 0x5678;
    v.GetAndFault(kEntries * 3 + 1) = 0x9abc;

    const u64* const* dir = v.ChunkDirectory();
    CHECK(dir[0] == zero);
    CHECK(dir[1] != zero);
    CHECK(dir[2] == zero);
    CHECK(dir[3] != zero);
    CHECK(dir[1] != dir[3]);
    CHECK(JitRead(v, kEntries + 7) == 0x1234);
    CHECK(JitRead(v, kEntries * 3) == 0x5678);
    CHECK(JitRead(v, kEntries * 3 + 1) == 0x9abc);
    CHECK(JitRead(v, kEntries + 8) == 0);
    CHECK(v.GetOrDefault(kEntries + 7) == 0x1234);
    CHECK(Common::SparseTestStats::ChunksLive() == 2);

    // The shared zero chunk is never written.
    CHECK(ChunkIsZero(zero));
}

void TestZeroRegionClearsThroughDirectory() {
    Vector v(kEntries * 2);
    for (std::size_t i = 0; i < 1024; ++i) {
        v.Set(i, i + 1);
    }
    v.ZeroRegion(0, kEntries);
    for (std::size_t i = 0; i < 1024; ++i) {
        CHECK(JitRead(v, i) == 0);
    }
    CHECK(ChunkIsZero(v.ChunkDirectory()[1]));
}

void TestResizeResetsDirectory() {
    Vector v(kEntries * 2);
    v.Set(3, 9);
    const u64* const zero = v.ChunkDirectory()[1];
    v.ResizeAndClear(kEntries * 8);
    const u64* const* dir = v.ChunkDirectory();
    for (std::size_t slot = 0; slot < 8; ++slot) {
        CHECK(dir[slot] == zero);
    }
    CHECK(JitRead(v, 3) == 0);
    CHECK(Common::SparseTestStats::ChunksLive() == 0);
    CHECK(ChunkIsZero(zero));
}

void TestChunkBytesCounter() {
    // What the frontend logs as page-table memory: bytes of materialized chunks, all vectors.
    constexpr u64 kChunk = 2 * 1024 * 1024;
    CHECK(Common::SparseChunkBytesInUse() == 0);
    {
        Vector a(kEntries * 4);
        Vector b(kEntries * 2);
        CHECK(Common::SparseChunkBytesInUse() == 0);
        a.Set(0, 1);
        a.Set(1, 2); // same chunk
        CHECK(Common::SparseChunkBytesInUse() == kChunk);
        a.Set(kEntries * 3, 3);
        b.GetUnchecked(kEntries) = 4;
        CHECK(Common::SparseChunkBytesInUse() == 3 * kChunk);
        (void)a[kEntries * 2]; // reads never allocate
        CHECK(Common::SparseChunkBytesInUse() == 3 * kChunk);
        a.ResizeAndClear(kEntries * 8);
        CHECK(Common::SparseChunkBytesInUse() == kChunk);
    }
    CHECK(Common::SparseChunkBytesInUse() == 0);
}

void TestDirectoryCoversEveryPageIndex() {
    // 32/36/38/39-bit tables with 4 KiB pages: the JIT's last level-1 index must be in range.
    for (std::size_t bits : {32u, 36u, 38u, 39u}) {
        const std::size_t entries = std::size_t{1} << (bits - 12);
        Vector v(entries);
        v.Set(entries - 1, 0x77);
        CHECK(JitRead(v, entries - 1) == 0x77);
        CHECK(JitRead(v, entries - 2) == 0);
    }
    CHECK(Common::SparseTestStats::ChunksLive() == 0);
}

} // namespace

int main() {
    TestIndexBits();
    TestFreshDirectoryPointsToSharedZeroChunk();
    TestWritesAreVisibleThroughDirectory();
    TestZeroRegionClearsThroughDirectory();
    TestResizeResetsDirectory();
    TestChunkBytesCounter();
    TestDirectoryCoversEveryPageIndex();
    CHECK(Common::SparseTestStats::ChunksLive() == 0);
    if (failures != 0) {
        std::printf("sparse_directory_test: %d check(s) failed\n", failures);
        return 1;
    }
    std::printf("sparse_directory_test: all checks passed\n");
    return 0;
}
