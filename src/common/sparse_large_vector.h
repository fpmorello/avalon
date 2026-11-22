// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* virtual_buffer.h */
// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <atomic>
#include <bit>
#include <cstdlib>
#include <cstring>
#include <utility>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#include <sys/mman.h>
#endif

#include "common/alignment.h"
#include "common/assert.h"

namespace Common {

#ifdef _WIN32
constexpr u64 HostPageSize = 0x1000;
constexpr u64 HostPageBits = 12;
constexpr u64 HostPageMask = ~(HostPageSize - 1);
bool CommitVectorPage(uintptr_t addr, bool write) noexcept;
#else
inline const u64 HostPageSize = sysconf(_SC_PAGESIZE);
inline const u64 HostPageBits = std::countr_zero(HostPageSize);
inline const u64 HostPageMask = ~(HostPageSize - 1);
#endif

// The Switch 2 host has no reserve-on-demand mmap: the port's mmap commits the whole
// range (src/common/horizon/mman.cpp), so a flat reservation pays the full table up
// front — 1 GiB for a 39-bit guest page table. Back the vector with calloc'd chunks
// materialized on first write instead. EDEN_CHUNKED_SPARSE_VECTOR selects the same
// backend for the host test harness (tools/switch2/tests/run.sh).
#if defined(__SWITCH__) || defined(EDEN_CHUNKED_SPARSE_VECTOR)
#define EDEN_SPARSE_CHUNKED_BACKING 1
#endif

void* AllocateMemoryPages(std::size_t size) noexcept;
void FreeMemoryPages(void* base, std::size_t size) noexcept;
void DecommitVectorPage(uintptr_t base) noexcept;

#ifdef EDEN_SPARSE_CHUNKED_BACKING
/// Bytes of one chunk of the chunked backing.
inline constexpr std::size_t SparseChunkBytes = 2 * 1024 * 1024;

/// One all-zero chunk shared by every vector: each directory slot whose chunk is not
/// materialized points here, so dynarmic's two-level page table lookup can load through
/// any slot without a null check (odd/tasks/two-level-page-table.md). Never written; it
/// lives in .bss, so it costs no file size.
// Not const on purpose: a const array would carry its zeros in .rodata and cost 2 MiB of
// NRO file size; in .bss they are free. It is never written (see ZeroChunk).
alignas(64) inline u8 SparseZeroChunk[SparseChunkBytes];

/// Bytes of materialized chunks over every vector: what the page tables really cost.
inline std::atomic<u64> SparseChunkBytesLive{0};
inline u64 SparseChunkBytesInUse() noexcept {
    return SparseChunkBytesLive.load(std::memory_order_relaxed);
}
#endif

#if defined(EDEN_CHUNKED_SPARSE_VECTOR)
// Test-only instrumentation for tools/switch2/tests; never compiled into the NRO.
inline std::atomic<std::size_t> SparseTestChunksLive{0};
inline std::atomic<std::size_t> SparseTestChunksAllocated{0};
#endif

/// A large page-aligned buffer that has optimized memory usage for zero-writes.
template <typename T>
    // MSVC doesn't regard structs with atomics as trivially copyable
    // requires std::is_trivially_copyable_v<T>
class SparseLargeVector final {
public:
    constexpr SparseLargeVector() = default;

    explicit SparseLargeVector(std::size_t count) noexcept
        : alloc_size{count * sizeof(T)}
    {
#ifdef EDEN_SPARSE_CHUNKED_BACKING
        AllocateChunkedBacking();
#else
        base_ptr = static_cast<T*>(AllocateMemoryPages(alloc_size));
#endif

        // each item in vector holds information for 64 pages
        auto denom = HostPageSize * 64;
        committed_pages = std::vector<std::atomic<u64>>((alloc_size + denom - 1) / denom);
    }

    ~SparseLargeVector() noexcept {
#ifdef EDEN_SPARSE_CHUNKED_BACKING
        FreeChunkedBacking();
#else
        FreeMemoryPages(base_ptr, alloc_size);
#endif
    }

    SparseLargeVector(const SparseLargeVector&) = delete;
    SparseLargeVector& operator=(const SparseLargeVector&) = delete;
    SparseLargeVector(SparseLargeVector&& other) = delete;
    SparseLargeVector& operator=(SparseLargeVector&& other) = delete;

    void ResizeAndClear(std::size_t count) noexcept {
        if (auto const new_size = count * sizeof(T); new_size != alloc_size) {
#ifdef EDEN_SPARSE_CHUNKED_BACKING
            FreeChunkedBacking();
            alloc_size = new_size;
            AllocateChunkedBacking();
#else
            FreeMemoryPages(base_ptr, alloc_size);
            alloc_size = new_size;
            base_ptr = static_cast<T*>(AllocateMemoryPages(alloc_size));
#endif

            auto denom = HostPageSize * 64;
            committed_pages = std::vector<std::atomic<u64>>((alloc_size + denom - 1) / denom);
        }
    }

    /// Returns a reference to the value of the requested index and allocates memory if needed.
    T& GetAndFault(std::size_t index) noexcept {
        if (index >= alloc_size / sizeof(T)) {
            UNREACHABLE_MSG("Out of bounds RW access on SparseLargeVector @ {}", index);
        }

        if (!IsCommittedPage(index) && !CommitPage(index)) {
            UNREACHABLE_MSG("Cannot access SparseLargeVector index {} with RW permission", index);
        }
#ifdef EDEN_SPARSE_CHUNKED_BACKING
        T* chunk = EnsureChunk(index);
        if (chunk == nullptr) {
            UNREACHABLE_MSG("Cannot access SparseLargeVector index {} with RW permission", index);
        }
        return chunk[EntryInChunk(index)];
#else
        return base_ptr[index];
#endif
    }

    /// Returns a reference to the value of the requested index if initialized, or will otherwise return a zero-initialized object.
    const T& GetOrDefault(std::size_t index) const {
#if defined(_WIN32) || defined(EDEN_SPARSE_CHUNKED_BACKING)
        if (!IsCommittedPage(index)) {
            return *reinterpret_cast<const T*>(&default_val);
        }
#endif
#ifdef EDEN_SPARSE_CHUNKED_BACKING
        // A committed page whose chunk is not materialized has never been written.
        if (T* chunk = ChunkSlotRead(index)) {
            return chunk[EntryInChunk(index)];
        }
        return *reinterpret_cast<const T*>(&default_val);
#else
        // On non-Windows, OS page table should optimize this by pointing to a zero page if unallocated.
        return base_ptr[index];
#endif
    }

    void Set(std::size_t index, const T& value) noexcept {
        if (index >= alloc_size / sizeof(T)) {
            LOG_CRITICAL(Common_Memory, "Out of bounds write on SparseLargeVector @ {}", index);
            return;
        }
#ifdef EDEN_SPARSE_CHUNKED_BACKING
        if (T* chunk = EnsureChunk(index); chunk != nullptr) {
            MarkPageCommitted(index);
            chunk[EntryInChunk(index)] = value;
        } else {
            LOG_ERROR(Common_Memory,
                      "Dropping write to SparseLargeVector index {}: out of memory for its chunk",
                      index);
        }
#else
        if (IsCommittedPage(index) || CommitPage(index))
            base_ptr[index] = value;
#endif
    }

    void ZeroRegion(std::size_t start, std::size_t end_) noexcept {
#ifdef EDEN_SPARSE_CHUNKED_BACKING
        const u64 start_byte = static_cast<u64>(start) * sizeof(T);
        const u64 end_byte = static_cast<u64>(end_) * sizeof(T);

        const u64 end_page = AlignUp(start_byte, HostPageSize);

        if (IsCommittedPage(start)) {
            ZeroChunkedBytes(start_byte, (std::min)(end_page, end_byte));
        }

        if (end_byte <= end_page)
            return;

        for (u64 page = end_page; page < end_byte; page += HostPageSize) {
            auto index = static_cast<std::size_t>(page / sizeof(T));
            if (!IsCommittedPage(index)) {
                continue;
            }

            if (end_byte - page >= HostPageSize) {
                DecommitPage(index);
            } else {
                ZeroChunkedBytes(page, end_byte);
            }
        }
#else
        u64 base = reinterpret_cast<u64>(&base_ptr[start]);
        const u64 end = reinterpret_cast<u64>(&base_ptr[end_]);

        const u64 end_page = AlignUp(base, HostPageSize);
        const u64 first_size = (std::min)(end_page, end) - base;

        if (IsCommittedPage(start)) {
            std::memset(reinterpret_cast<void*>(base), 0, first_size);
        }

        if (end <= end_page)
            return;

        base = end_page;

        for (u64 page = base; page < end; page += HostPageSize) {
            auto index = (page - reinterpret_cast<u64>(base_ptr)) / sizeof(T);
            if (!IsCommittedPage(index)) {
                continue;
            }

            if (end - page >= HostPageSize) {
                DecommitPage(index);
            } else {
                std::memset(reinterpret_cast<void*>(page), 0, end - page);
            }
        }
#endif
    }

    constexpr void CommitRegion(size_t index, size_t end_) {
        const u64 base = static_cast<u64>(index) * sizeof(T);
        const u64 end = static_cast<u64>(end_) * sizeof(T);

        for (u64 page = AlignDown(base, HostPageSize); page < end; page += HostPageSize) {
            if (!IsCommittedPage(page / sizeof(T))) {
                CommitPage(page / sizeof(T));
            }
        }
    }

#ifdef EDEN_SPARSE_CHUNKED_BACKING
    /// Entries per chunk; exposed for the host test harness.
    static constexpr std::size_t ChunkEntriesPerChunk() noexcept {
        return ChunkBytes / sizeof(T);
    }

    /// log2 of the entries per chunk: entry i lives at ChunkDirectory()[i >> bits][i & mask].
    static constexpr std::size_t ChunkIndexBits() noexcept {
        static_assert(std::has_single_bit(ChunkBytes / sizeof(T)),
                      "two-level lookup needs a power-of-two number of entries per chunk");
        return std::countr_zero(ChunkBytes / sizeof(T));
    }

    /// The chunk directory, for lookups that bypass this class (dynarmic's JIT). Every slot
    /// points to readable memory: its chunk, or the shared zero chunk. The array is stable
    /// until ResizeAndClear or destruction.
    [[nodiscard]] const T* const* ChunkDirectory() const noexcept {
        static_assert(sizeof(std::atomic<T*>) == sizeof(T*) && std::atomic<T*>::is_always_lock_free,
                      "the directory is read as a plain pointer array");
        return reinterpret_cast<const T* const*>(chunk_map);
    }
#endif

    T& GetUnchecked(size_t index) {
#ifdef EDEN_SPARSE_CHUNKED_BACKING
        T* chunk = EnsureChunk(index);
        ASSERT_MSG(chunk != nullptr, "Out of memory materializing a chunk for index {}", index);
        MarkPageCommitted(index);
        return chunk[EntryInChunk(index)];
#else
        return base_ptr[index];
#endif
    }

    [[nodiscard]] constexpr const T& operator[](std::size_t index) const noexcept {
        return GetOrDefault(index);
    }

#ifndef EDEN_SPARSE_CHUNKED_BACKING
    [[nodiscard]] constexpr const T* data() const noexcept {
        return base_ptr;
    }
#endif

    [[nodiscard]] constexpr std::size_t size() const noexcept {
        return alloc_size / sizeof(T);
    }

private:
    [[nodiscard]] constexpr bool IsCommittedPage(std::size_t index) const noexcept {
        if (index >= alloc_size / sizeof(T)) {
            LOG_CRITICAL(Common_Memory, "Out of bounds access on large vector @ {}", index);
            return false;
        }

        auto page = (index * sizeof(T)) >> HostPageBits;
        auto val = committed_pages[page >> 6].load(std::memory_order_acquire);
        return (val >> (page & 63)) & 1;
    }

    bool CommitPage(std::size_t index) noexcept {
        auto page_index = (index * sizeof(T)) >> HostPageBits;
#ifdef EDEN_SPARSE_CHUNKED_BACKING
        // Chunks are materialized by the write paths; committing only records that the
        // page may be written, so CommitRegion never allocates.
        committed_pages[page_index >> 6].fetch_or(1ULL << (page_index & 63), std::memory_order_release);
        return true;
#else
        auto page = reinterpret_cast<uintptr_t>(base_ptr + index) & HostPageMask;
#if defined(_WIN32)
        if (!CommitVectorPage(page, true)) {
            return false;
        }
#else
        if (mprotect(reinterpret_cast<void*>(page), HostPageSize, PROT_READ | PROT_WRITE) != 0) {
            LOG_ERROR(Common_Memory, "Failed to commit large buffer region at index {}, error {}", index, strerror(errno));
            return false;
        }
#endif

        committed_pages[page_index >> 6].fetch_or(1ULL << (page_index & 63), std::memory_order_release);
        return true;
#endif
    }

    void DecommitPage(std::size_t index) noexcept {
        auto page_index = (index * sizeof(T)) >> HostPageBits;

        committed_pages[page_index >> 6].fetch_and(~(1ULL << (page_index & 63)), std::memory_order_release);
#ifdef EDEN_SPARSE_CHUNKED_BACKING
        // Zero the page inside its chunk; the chunk itself stays allocated until the
        // vector is destroyed or resized (host pages never straddle chunks).
        if (T* chunk = ChunkSlotRead(index)) {
            const u64 page_byte = (static_cast<u64>(index) * sizeof(T)) & HostPageMask;
            std::memset(reinterpret_cast<u8*>(chunk) + (page_byte % ChunkBytes), 0, HostPageSize);
        }
#else
        auto page = reinterpret_cast<uintptr_t>(base_ptr + index) & HostPageMask;
        DecommitVectorPage(page);
#endif
    }

    std::size_t alloc_size{};
#ifdef EDEN_SPARSE_CHUNKED_BACKING
    /// Chunk size in bytes; a multiple of every supported host page size, so a host
    /// page never straddles two chunks.
    static constexpr std::size_t ChunkBytes = SparseChunkBytes;
    static_assert(ChunkBytes % (64 * 1024) == 0, "chunk must hold whole host pages");

    std::size_t chunk_count{};
    std::atomic<T*>* chunk_map{};

    std::size_t ChunkSlotOf(std::size_t index) const noexcept {
        return (index * sizeof(T)) / ChunkBytes;
    }
    std::size_t EntryInChunk(std::size_t index) const noexcept {
        return ((index * sizeof(T)) % ChunkBytes) / sizeof(T);
    }
    static T* ZeroChunk() noexcept {
        return reinterpret_cast<T*>(SparseZeroChunk);
    }
    /// The materialized chunk holding index, or nullptr if it has none.
    T* ChunkSlotRead(std::size_t index) const noexcept {
        T* const chunk = chunk_map[ChunkSlotOf(index)].load(std::memory_order_acquire);
        return chunk == ZeroChunk() ? nullptr : chunk;
    }
    /// Materializes the chunk holding index. Exactly one thread's allocation is
    /// published per slot; the others reuse the winner's chunk.
    T* EnsureChunk(std::size_t index) noexcept {
        const std::size_t slot = ChunkSlotOf(index);
        if (T* chunk = chunk_map[slot].load(std::memory_order_acquire); chunk != ZeroChunk()) {
            return chunk;
        }
        T* const fresh = static_cast<T*>(std::calloc(ChunkBytes, 1));
        if (fresh == nullptr) {
            return nullptr;
        }
#if defined(EDEN_CHUNKED_SPARSE_VECTOR)
        SparseTestChunksAllocated.fetch_add(1, std::memory_order_relaxed);
        SparseTestChunksLive.fetch_add(1, std::memory_order_relaxed);
#endif
        // Release publishes the zeroed chunk to the JIT, whose address-dependent load of
        // the entry through the slot is ordered after the load of the slot.
        T* expected = ZeroChunk();
        if (chunk_map[slot].compare_exchange_strong(expected, fresh, std::memory_order_acq_rel)) {
            SparseChunkBytesLive.fetch_add(ChunkBytes, std::memory_order_relaxed);
            return fresh;
        }
        std::free(fresh);
#if defined(EDEN_CHUNKED_SPARSE_VECTOR)
        SparseTestChunksLive.fetch_sub(1, std::memory_order_relaxed);
#endif
        return expected;
    }
    void MarkPageCommitted(std::size_t index) noexcept {
        const u64 page_index = (index * sizeof(T)) >> HostPageBits;
        committed_pages[page_index >> 6].fetch_or(1ULL << (page_index & 63), std::memory_order_release);
    }
    void AllocateChunkedBacking() noexcept {
        chunk_count = (alloc_size + ChunkBytes - 1) / ChunkBytes;
        chunk_map = new std::atomic<T*>[chunk_count];
        for (std::size_t i = 0; i < chunk_count; ++i) {
            chunk_map[i].store(ZeroChunk(), std::memory_order_relaxed);
        }
    }
    void FreeChunkedBacking() noexcept {
        if (chunk_map == nullptr) {
            return;
        }
        for (std::size_t i = 0; i < chunk_count; ++i) {
            T* const chunk = chunk_map[i].exchange(ZeroChunk(), std::memory_order_acquire);
            if (chunk == ZeroChunk()) {
                continue;
            }
            std::free(chunk);
            SparseChunkBytesLive.fetch_sub(ChunkBytes, std::memory_order_relaxed);
#if defined(EDEN_CHUNKED_SPARSE_VECTOR)
            SparseTestChunksLive.fetch_sub(1, std::memory_order_relaxed);
#endif
        }
        delete[] chunk_map;
        chunk_map = nullptr;
        chunk_count = 0;
    }
    /// Zeros [from, to) bytes of chunk storage; the range must lie in one chunk.
    void ZeroChunkedBytes(u64 from, u64 to) noexcept {
        T* chunk = ChunkSlotRead(static_cast<std::size_t>(from / sizeof(T)));
        if (chunk == nullptr) {
            return;
        }
        std::memset(reinterpret_cast<u8*>(chunk) + (from % ChunkBytes), 0, to - from);
    }
#else
    T* base_ptr{};
#endif

    std::vector<std::atomic<u64>> committed_pages{};
#if defined(_WIN32) || defined(EDEN_SPARSE_CHUNKED_BACKING)
    const std::array<u8, sizeof(T)> default_val{};
#endif
};

#if defined(EDEN_CHUNKED_SPARSE_VECTOR)
/// Test-only counters for tools/switch2/tests; never compiled into the NRO.
namespace SparseTestStats {
inline std::size_t ChunksLive() noexcept {
    return SparseTestChunksLive.load(std::memory_order_relaxed);
}
inline std::size_t ChunksAllocated() noexcept {
    return SparseTestChunksAllocated.load(std::memory_order_relaxed);
}
template <typename T>
inline constexpr std::size_t ChunkEntriesFor() noexcept {
    return SparseLargeVector<T>::ChunkEntriesPerChunk();
}
} // namespace SparseTestStats
#endif

} // namespace Common
