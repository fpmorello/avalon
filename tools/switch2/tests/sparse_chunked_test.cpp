// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Standalone host test for the chunked SparseLargeVector backend that the Switch 2 port
// selects. The backend is chosen by EDEN_CHUNKED_SPARSE_VECTOR, which the Switch build gets
// through __SWITCH__ (see src/common/sparse_large_vector.h); the runner script passes the
// macro so the exact Switch-selected code runs on the host.
//
// Build & run: tools/switch2/tests/run.sh

#include "common/sparse_large_vector.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <string_view>
#include <thread>
#include <vector>

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

void TestRoundtripAndLazyChunks() {
    constexpr std::size_t kCount = 8 * 1024 * 1024; // 64 MiB flat if committed
    SparseLargeVector<u64> v(kCount);

    CHECK(v.size() == kCount);
    CHECK(Common::SparseTestStats::ChunksLive() == 0);

    v.Set(5, 42);
    CHECK(v.GetOrDefault(5) == 42);
    CHECK(v[5] == 42);
    CHECK(Common::SparseTestStats::ChunksLive() == 1);

    // Untouched entries read as zero without allocating anything.
    CHECK(v.GetOrDefault(1) == 0);
    CHECK(v.GetOrDefault(1 << 20) == 0);
    CHECK(v[kCount - 1] == 0);
    CHECK(Common::SparseTestStats::ChunksLive() == 1);
}

void TestChunkGranularity() {
    constexpr std::size_t kChunkEntries = Common::SparseTestStats::ChunkEntriesFor<u64>();
    CHECK(kChunkEntries > 0);

    SparseLargeVector<u64> v(kChunkEntries * 4);
    v.Set(0, 1);                    // chunk 0
    v.Set(kChunkEntries, 2);        // chunk 1
    v.Set(kChunkEntries * 3, 3);    // chunk 3
    CHECK(v.GetOrDefault(0) == 1);
    CHECK(v.GetOrDefault(kChunkEntries) == 2);
    CHECK(v.GetOrDefault(kChunkEntries * 3) == 3);
    CHECK(Common::SparseTestStats::ChunksLive() == 3);
}

void TestFaultingAccessors() {
    constexpr std::size_t kChunkEntries = Common::SparseTestStats::ChunkEntriesFor<u64>();
    SparseLargeVector<u64> v(kChunkEntries * 2);

    u64& faulted = v.GetAndFault(9);
    faulted = 7;
    CHECK(v.GetOrDefault(9) == 7);

    // The write path materializes its chunk on demand.
    v.GetUnchecked(kChunkEntries + 3) = 11;
    CHECK(v[kChunkEntries + 3] == 11);
}

void TestBoundaryIndexIsRejected() {
    // index == size is out of bounds: it must be refused, not read one entry past the end
    // of the chunk map or the committed-page bitmap.
    constexpr std::size_t kChunkEntries = Common::SparseTestStats::ChunkEntriesFor<u64>();
    SparseLargeVector<u64> v(kChunkEntries * 4);
    CHECK(v.size() == kChunkEntries * 4);
    CHECK(Common::SparseTestStats::ChunksLive() == 0);

    v.Set(v.size(), 42);
    CHECK(v.GetOrDefault(v.size()) == 0);
    CHECK(v[v.size()] == 0);
    CHECK(Common::SparseTestStats::ChunksLive() == 0);

    // The legal boundary neighbour still works.
    v.Set(v.size() - 1, 7);
    CHECK(v.GetOrDefault(v.size() - 1) == 7);
}

void TestZeroRegionMultiPage() {
    const std::size_t kChunkEntries = Common::SparseTestStats::ChunkEntriesFor<u64>();
    const std::size_t kPageEntries = 4096 / sizeof(u64); // host page of 8-byte entries
    SparseLargeVector<u64> v(kChunkEntries * 3);

    // A band three host pages wide that straddles a chunk boundary, plus neighbours on
    // both sides that must survive.
    const std::size_t start = kChunkEntries - kPageEntries / 2;
    const std::size_t end = start + 3 * kPageEntries;
    for (std::size_t i = start - 8; i < end + 8; ++i) {
        v.Set(i, i + 1);
    }
    v.ZeroRegion(start, end);
    for (std::size_t i = start; i < end; ++i) {
        CHECK(v.GetOrDefault(i) == 0);
    }
    for (std::size_t i = 1; i <= 8; ++i) {
        CHECK(v.GetOrDefault(start - i) == start - i + 1);
        CHECK(v.GetOrDefault(end + i - 1) == end + i);
    }

    // Zeroing a range nobody wrote to is a no-op, not a crash.
    v.ZeroRegion(2 * kChunkEntries, 2 * kChunkEntries + 5 * kPageEntries);
    CHECK(v.GetOrDefault(2 * kChunkEntries) == 0);
}

void TestZeroRegion() {
    SparseLargeVector<u64> v(4096);
    for (std::size_t i = 0; i < 100; ++i) {
        v.Set(i, u64(i) + 1);
    }
    v.ZeroRegion(10, 50);
    for (std::size_t i = 10; i < 50; ++i) {
        CHECK(v.GetOrDefault(i) == 0);
    }
    // Outside the region the values survive.
    CHECK(v.GetOrDefault(9) == 10);
    CHECK(v.GetOrDefault(50) == 51);
    CHECK(v.GetOrDefault(99) == 100);
}

void TestResizeAndClear() {
    SparseLargeVector<u64> v(8192);
    v.Set(100, 5);
    CHECK(Common::SparseTestStats::ChunksLive() >= 1);

    v.ResizeAndClear(4096);
    CHECK(v.size() == 4096);
    CHECK(v.GetOrDefault(100) == 0);
    CHECK(Common::SparseTestStats::ChunksLive() == 0);

    // Usable after the resize.
    v.Set(1, 3);
    CHECK(v.GetOrDefault(1) == 3);
}

void TestDestructorFrees() {
    {
        SparseLargeVector<u64> v(1 << 16);
        v.Set(3, 1);
        v.Set((1 << 15) + 3, 2);
    }
    CHECK(Common::SparseTestStats::ChunksLive() == 0);
}

void TestChunkBytesCountedOncePerChunk() {
    // Threads racing to materialize the same chunk publish it once, and it is counted once.
    const u64 before = Common::SparseChunkBytesInUse();
    {
        SparseLargeVector<u64> v(Common::SparseTestStats::ChunkEntriesFor<u64>() * 2);
        std::vector<std::thread> threads;
        for (std::size_t t = 0; t < 8; ++t) {
            threads.emplace_back([&v, t] { v.Set(t, t + 1); }); // same chunk, own entry
        }
        for (auto& thread : threads) {
            thread.join();
        }
        CHECK(Common::SparseChunkBytesInUse() == before + 2 * 1024 * 1024);
    }
    CHECK(Common::SparseChunkBytesInUse() == before);
}

void TestConcurrentSet() {
    constexpr std::size_t kChunkEntries = Common::SparseTestStats::ChunkEntriesFor<u64>();
    constexpr int kThreads = 4;
    constexpr std::size_t kPerThread = 50000;

    // One dedicated chunk per thread plus one shared chunk everyone writes to, with
    // disjoint indices inside it: the race exercised is chunk publication (the emulator
    // writes page-table entries under a lock, never the same entry concurrently).
    SparseLargeVector<u64> v(kChunkEntries * (kThreads + 1));
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&v, t] {
            const std::size_t base = kChunkEntries * (std::size_t(t) + 1);
            for (std::size_t i = 0; i < kPerThread; ++i) {
                v.Set(base + i, u64(t) * kPerThread + i);
            }
            // Distinct indices of the shared chunk zero.
            for (std::size_t i = 0; i < kPerThread; ++i) {
                v.Set(std::size_t(t) * kPerThread + i, u64(t) * kPerThread + i);
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    for (int t = 0; t < kThreads; ++t) {
        const std::size_t base = kChunkEntries * (std::size_t(t) + 1);
        bool ok = true;
        for (std::size_t i = 0; i < kPerThread; ++i) {
            ok = ok && v.GetOrDefault(base + i) == u64(t) * kPerThread + i;
            ok = ok && v.GetOrDefault(std::size_t(t) * kPerThread + i) == u64(t) * kPerThread + i;
        }
        CHECK(ok);
    }
    CHECK(Common::SparseTestStats::ChunksLive() == std::size_t(kThreads + 1));
}

void TestOtherTypes() {
    // The Switch build instantiates the vector with u32 and small structs, not only u64.
    SparseLargeVector<u32> v32(1 << 20);
    v32.Set(7, 0xdeadbeef);
    CHECK(v32.GetOrDefault(7) == 0xdeadbeefu);
    CHECK(v32.GetOrDefault(8) == 0);

    struct TrackedEntry {
        u64 compressed_physical_ptr;
        u64 cpu_backing_address;
    };
    SparseLargeVector<TrackedEntry> vtracked(1 << 16);
    vtracked.GetUnchecked(4).cpu_backing_address = 0x1234;
    CHECK(vtracked.GetOrDefault(4).cpu_backing_address == 0x1234);
    CHECK(vtracked.GetOrDefault(5).cpu_backing_address == 0);
}

} // namespace

int main() {
    TestRoundtripAndLazyChunks();
    TestChunkGranularity();
    TestFaultingAccessors();
    TestBoundaryIndexIsRejected();
    TestZeroRegionMultiPage();
    TestZeroRegion();
    TestResizeAndClear();
    TestDestructorFrees();
    TestChunkBytesCountedOncePerChunk();
    TestConcurrentSet();
    TestOtherTypes();

    if (failures == 0) {
        std::printf("sparse_chunked_test: all checks passed\n");
        return 0;
    }
    std::printf("sparse_chunked_test: %d checks failed\n", failures);
    return 1;
}
