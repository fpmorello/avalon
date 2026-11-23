// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Figures the Horizon frontend logs to see where memory goes on a host where one
// 9 GiB heap holds everything. Plain C globals so that dynarmic, video_core and the
// frontend can share them without depending on each other. Defined in mman.cpp.
extern "C" {
extern unsigned long long eden_horizon_texture_bytes; ///< Texture cache's own estimate
extern unsigned long long eden_horizon_buffer_bytes;  ///< Buffer cache's own estimate
extern unsigned long long eden_horizon_texture_evictions; ///< Textures the collector dropped
extern unsigned long long eden_horizon_jit_blocks;    ///< Compiled blocks alive, all cores
extern unsigned long long eden_horizon_jit_cache_clears;
extern unsigned long long eden_horizon_jit_heap_clears;     ///< Those made to free heap
extern unsigned long long eden_horizon_jit_partial_evictions; ///< Blocks dropped by the heap-pressure eviction
extern unsigned long long eden_horizon_jit_heap_holds; ///< Low-heap checks that dropped nothing: the heap had not fallen further since the last drop
extern unsigned long long eden_horizon_jit_mem_fallbacks; ///< Guest memory accesses the JIT served through the callbacks
extern unsigned long long eden_horizon_jit_compile_micros;  ///< Compiling and publishing
/// Sizes in bytes of the mappings of 8 MiB and more made through the mmap() shim
extern unsigned long long eden_horizon_large_mmaps[64];
extern int eden_horizon_large_mmap_count;
/// Set by the frontend, for 39-bit programs: 0 leaves them their address space, 1 gives
/// them a 36-bit one, 2 a 38-bit one with the 39-bit layout
extern int eden_horizon_compact_address_space;
/// Set by the frontend: how many blocks the JIT compiles ahead of the one it needs
extern int eden_horizon_jit_blocks_ahead;
/// JIT cache: the blocks each core had compiled when the last session ended are compiled
/// again in one go when the core starts, instead of one by one as the game reaches them.
/// The frontend sets the path prefix of the game's lists; empty turns the cache off.
extern char eden_horizon_jit_cache_prefix[160];
extern unsigned long long eden_horizon_jit_warm_blocks; ///< Compiled from the lists
extern unsigned long long eden_horizon_jit_warm_micros;
/// Writes every core's list. Call with the emulated cores stopped. Returns the blocks written.
unsigned long long eden_horizon_jit_save_caches(void);
/// The code cache a core should get, in MiB, going by how much it compiled last session;
/// zero when the game has no lists yet.
unsigned eden_horizon_jit_cache_megabytes(int core);
}
