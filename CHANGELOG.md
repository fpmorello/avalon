# Changelog — Switch 2 fork builds

Fork builds of the Switch 2 port are identified by a build id (`b<N>`) shown on the
options screen header, logged at startup and written to `crash.txt`. Each build's commit
is tagged `sw2-fork-b<N>`, so `git describe` names the exact source of a binary.

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
