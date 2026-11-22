# Changelog — Switch 2 fork builds

Fork builds of the Switch 2 port are identified by a build id (`b<N>`) shown on the
options screen header, logged at startup and written to `crash.txt`. Each build's commit
is tagged `sw2-fork-b<N>`, so `git describe` names the exact source of a binary.

## b2 — 2026-11-21 (`sw2-fork-b2`)

Branch `feat/b2-full-space-eviction-fixes`. First build that runs: b1 did not compile for
the console, because its chunked page table left the JIT without the flat table it read.

- **Avalon** (`eden_horizon`, `common`): the homebrew is named Avalon and keeps its data
  under `sdmc:/switch/avalon/` (`.local/share/avalon/`, log `avalon_log.txt`), so it
  installs next to stock Eden without sharing settings, keys or caches.
- **Two-level page table lookup** (`dynarmic`, `core`): the arm64 JIT reads the guest
  page table through the chunk directory (`page_table_level2_bits`), one extra load per
  guest memory access. Unmapped ranges point to one shared all-zero chunk.
- **Full address space by default** (`eden_horizon`, `core`): `Address space` defaults to
  `full` now that the table costs only what the game maps; `compact` and `game` remain
  as per-game options. The JIT code cache is 64 MiB per core in every mode.
- **Eviction fixes** (`dynarmic`): a block invalidated and compiled again is no longer
  evicted through its old queue entry, whose leftover bookkeeping is released instead; an
  evicted block keeps its referrers, so it is linked directly again when recompiled; the
  eviction count resets once the heap is no longer low.
- **Page-table memory in the log**: `Memory detail` starts with `page tables N MiB`.
- **On-device self-test**: an empty `sdmc:/switch/avalon/test_pagetable.txt` runs an A64
  and A32 check of the lookup at start; result in `test_pagetable_result.txt`.
- **Build fixes**: `heapGetTotalSize` declaration in `vulkan_device.cpp`; `build.sh`
  fails when ninja fails and names the NRO `avalon-<build>.nro`.

## b1 — 2026-11-21 (`sw2-fork-b1`)

First identified fork build. Branch `improve/sparse-page-table-jit-eviction`, base
`switch2-poc` (dd463aa5b5) of Olmectron/eden-switch2-poc.

- **On-demand guest page table** (`common`): `SparseLargeVector` is backed by calloc'd
  2 MiB chunks materialized on first write (lock-free publication) instead of one fully
  committed allocation, so a 39-bit address space no longer costs 1 GiB of heap up front.
  The 36-bit "compact" hack and the 38-bit "game" mode are now memory-saving options,
  not necessities — "full" is cheap.
- **Partial JIT eviction** (`dynarmic`): when the heap runs low the JIT drops the oldest
  quarter of its bookkeeping (same teardown as invalidation; emitted code stays valid)
  instead of clearing the whole cache. The full clear remains the fallback after two
  evictions without relief. Logged as `blocks evicted`.
- **Heap-proportional graphics memory** (`video_core`): the GPU cache budget is a quarter
  of the heap (capped at the previous 2 GiB on a 9 GiB console); the per-game "GPU: fast"
  option now also spends a third of the heap (up to 3 GiB) to keep more textures cached.
  Logged at startup as `Graphics memory budget`.
- **Robustness**: `SparseLargeVector::Set` logs a dropped write when a chunk cannot be
  allocated; guest system settings lost in the remove→rename crash window are recovered
  from the finished temporary file on next boot.
- **Host test harness** (`tools/switch2/tests`): standalone tests for the chunked
  backing, its host behavior, and the eviction policy (ASan + TSan), runnable without
  the Switch toolchain.
