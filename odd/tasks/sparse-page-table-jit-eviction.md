# Sparse guest page table + partial JIT eviction (Switch 2 port)

Objective: cut the two confirmed root causes of the port's memory pressure and JIT stutter:
(1a) `SparseLargeVector` commits its whole (up to 1 GiB) backing on Switch; (2a) `AddressSpace`
clears the whole JIT cache when the heap runs low. Branch `improve/sparse-page-table-jit-eviction`
off `switch2-poc` in the fork `fpmorello/eden-switch2-poc`. Delivery: work-unit commits on
the feature branch (push/PR/merge are the user's decisions). Test runner: standalone `clang++`
harness under `tools/switch2/tests/` (host; the NRO build needs devkitPro + ounce-devkit, only
documented for Windows/MSYS2, unavailable here).

## Specs

- S1 (1a): On `__SWITCH__`, `Common::SparseLargeVector` must be backed by calloc'd chunks
  allocated on demand instead of one fully-committed, fully-memset allocation. Public behavior
  preserved: `Set`/`GetOrDefault`/`GetAndFault`/`ZeroRegion`/`CommitRegion`/`GetUnchecked`/
  `ResizeAndClear`/`operator[]` keep their semantics; untouched indices read as zero without
  allocating; `GetUnchecked` (write path) materializes its chunk. Selected by
  `#if defined(__SWITCH__) || defined(EDEN_CHUNKED_SPARSE_VECTOR)` so the same code runs in the
  host test harness. Check: standalone test (roundtrip, zero-default, chunk-count grows only on
  touch, ZeroRegion, ResizeAndClear, concurrent Set from threads) run with clang++ + ASan on host.
- S2 (2a): Under heap pressure (free heap < 1.5 GiB, >= 16384 blocks), `AddressSpace` evicts the
  oldest quarter of blocks from `block_entries`, `block_infos`, `reverse_block_entries`,
  `block_references` and the lookup cache — with `RelinkForDescriptor(desc, nullptr)` per block —
  instead of `ClearCache()`. Code memory is not reclaimed (no cursor rewind), so executing code
  stays valid. Full `ClearCache()` remains as fallback if a later check still finds the heap low.
  Eviction order is real emission order. Check: host unit test of the selection policy; structural
  verification of the teardown (mirrors `InvalidateBasicBlocks` + frees the extra maps, which it
  does not).
- S3: The change is observable on device: a `eden_horizon_jit_partial_evictions` counter is
  reported wherever `eden_horizon_jit_cache_clears` is, and the README "Known limits" memory
  bullet is updated honestly (page table committed on demand; full-clear still exists).

## Tasks

- T1 (S1, inline): Standalone test harness `tools/switch2/tests/` + RED test for chunked
  `SparseLargeVector` (compile-selected via `EDEN_CHUNKED_SPARSE_VECTOR`). Evidence: RED run —
  only errors were `no member named 'SparseTestStats'` (missing backend). DONE.
- T2 (S1, inline): Implement chunked backing in `src/common/sparse_large_vector.h`; GREEN test;
  commit C1 "common: on-demand chunked backing for SparseLargeVector on Switch 2". DONE:
  commit ec36f9edd0. GREEN: ASan + TSan all checks passed; differential check (base vs current,
  no macro, POSIX backend): stdout+stderr+exit identical; probe: page_table.h compiles with the
  chunked instantiation.
- T3 (S2, inline): RED policy test + implement `EvictBlockCount` policy header and
  `EvictOldestBlocks` integration in `src/dynarmic/src/dynarmic/backend/arm64/address_space.{h,cpp}`;
  GREEN; structural checks. DONE: RED = missing jit_eviction.h; GREEN = policy test ASan+TSan
  all passed; integration structurally verified (teardown mirrors InvalidateBasicBlocks + frees
  block_infos/reverse/block_references; code cursor untouched; ClearCache fallback after 2
  evictions; emission_order pushed in Emit, cleared in ClearCache).
- T4 (S3, inline): Eviction counter + stats wiring in `src/eden_horizon` log + README tweak;
  focused re-runs; commit C2 "dynarmic: partial eviction under heap pressure instead of full
  cache clear". DONE: counter `eden_horizon_jit_partial_evictions` (stats.h + mman.cpp +
  main.cpp log), README Known-limits bullet reworded honestly; all three host tests re-run
  green (ASan+TSan); C2 = 26e5145f97.

## Log

- L1 (2026-11-21, user): "Quiero hace un fork para revisar como lo podemos mejorar. Entender que
  esta fallando, que falta." → fork created (`fpmorello/eden-switch2-poc`), full read-only
  analysis delivered (architecture, failures, gaps, ranked improvements).
- L2 (2026-11-21, user): "Dale seguimos con tu recomendacion" → implements recommendation
  1a (sparse chunked page-table backing) + 2a (partial JIT eviction), priority chosen by user:
  "Memoria + stutter (#1+#2)".
- L3: Analysis evidence (verified by spot checks): guest page table = `Common::PageTable::entries`
  (`src/common/page_table.h:152`) = `SparseLargeVector<PageEntryData>` (8 B/page; 39-bit = 1 GiB,
  38-bit = 512 MiB, 36-bit = 128 MiB); `AllocateMemoryPages` (`sparse_large_vector.cpp:105`) calls
  mmap → port's fake `mmap` (`src/common/horizon/mman.cpp:31`) = `aligned_alloc` + full memset.
  Heap-pressure clear at `address_space.cpp` `GetOrEmit` (`HeapIsRunningLow` < 1536 MiB, every
  1024 emits, >= 16384 blocks) calls `ClearCache()` (all four maps + cursor rewind) → recompile
  storms = README stutter. `InvalidateBasicBlocks` erases only `block_entries` + lookup cache;
  full-clear is the only place `block_infos`/`reverse_block_entries`/`block_references` are freed.
  `ReverseGet*` has no callers outside dynarmic (safe to erase on eviction). Precedent for chunks:
  `MultiLevelPageTable` on `__SWITCH__` (`multi_level_page_table.inc:20-60`). Pre-existing (not in
  scope, observed): `IsCommittedPage` bounds check off-by-one (`index >` should be `>=`,
  `sparse_large_vector.h:172`).
- L4: Verification limits: no Switch 2 build possible on this machine (devkitPro +
  ounce-devkit, documented for Windows/MSYS2 only; `tools/switch2/configure.sh` requires
  `$DEVKITPRO`). Host standalone tests cover the exact code selected on Switch via
  `EDEN_CHUNKED_SPARSE_VECTOR` (S1) and the pure policy (S2); the S2 teardown integration gets
  structural verification only. On-device validation (NRO build + game test) remains for the
  user's build setup.
- L5 (evidence): C1 = ec36f9edd0 (chunked SparseLargeVector + harness, 598 lines);
  C2 = 26e5145f97 (partial eviction + policy + stats + README, 178 lines). Branch total:
  772 insertions / 14 deletions over 13 files. Suite: sparse_chunked_test (ASan+TSan),
  sparse_host_behavior_test (chunked via run.sh + POSIX differential vs base commit:
  identical), jit_eviction_test (ASan+TSan). Design note: eviction order is emission order
  (oldest-first); warm-compiled blocks are oldest but may be hot — strictly better than the
  previous full clear, fallback bounded at 2 evictions; recency-aware policy is follow-up.
  Open: on-device NRO validation; git identity of both commits is the auto-detected
  `Franco <franco@MacBook-Pro-de-Franco.local>` (fix with git config + rebase --reset-author
  before pushing, if desired).
- L6 (RDD): assess (baseRef switch2-poc, committedOnly): tier medium, nativeRisk high
  (run.sh executable mode + shell process signals) → reviewDue. Ordinary review started
  (lineage review-0d060ebf8c43cd81, lenses risk/resilience/reliability, committed range
  af9267d1..710a7d53, 801 changed lines, correction budget 200). Group forecast: 3 model
  runs via pi host relay, acknowledged and executed. Outcome: **approved**, no correction
  required; 12 advisory findings, all informational (follow-up work, listed below);
  acknowledgement burned the authority (burn evidence gentle-ai.review-acknowledged/v1).
  Delivery follows ordinary repository policy (push/PR = user's decision).
  Advisory findings (non-blocking): eviction emission_order may hold duplicates of
  re-emitted blocks (address_space.cpp:382-399); eviction drops inbound link registrations
  (:395-398); eviction integration only structurally tested (:374-411); fallback full clear
  delayed under real pressure (:285-289); SparseLargeVector::Set silently drops on calloc
  failure (sparse_large_vector.h:155-158); ZeroRegion multi-page path untested (:179-189);
  sparse_host_behavior_test via run.sh always runs chunked, the POSIX variant only exists in
  the differential (run.sh:19-21); evicted code space not reclaimed (known design,
  address_space.cpp:390-399); eviction counter never decays (:285-289); feature-doc line 88
  flagged by risk lens (informational).
