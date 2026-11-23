# Changelog — Avalon builds

Avalon builds are identified by a build id (`b<N>`) shown on the
options screen header, logged at startup and written to `crash.txt`. Each build's commit
is tagged `sw2-fork-b<N>`, so `git describe` names the exact source of a binary.

## b13 — Per-game launch logs

- Keep up to ten launch logs per game, including failed launches, under the Avalon log
  directory. Returning to the picker no longer rotates away game logs.

## b12 — 2026-11-22 (local experiment)

- **Async presentation**: optional Vulkan presentation worker on Switch 2, off by default.
  Use **Y** in the game picker to compare modes per game. Both modes wait for in-flight
  GPU work before replacing the swapchain. No performance gain has been verified on hardware.

## b11 — 2026-11-22

- **ARM64 compatibility**: handle indexed FP16 `FCMLA` through scalar half-precision
  fused multiply-add on Switch 2. This targets the Dynarmic crash seen when starting
  Super Mario Bros. Wonder with its local update. Gameplay on hardware is not yet verified.

## b10 — 2026-11-22

- **Startup feedback**: Avalon shows opening and initialization stages, counts processed
  cached shader pipelines, and shows a waiting card until a guest frame replaces it.
  The log distinguishes waiting for a frame from Vulkan accepting one for presentation.

## b9 — 2026-11-22

- **Recoverable launch errors**: when the loader rejects a game, Avalon shows the reason,
  a next step and an error code. Press and release **A** to reopen the game list through
  a supported homebrew loader. If restart is unavailable, Avalon explains why before closing.
  Renderer failures, crashes and hangs are not handled by this screen.

## b8 — 2026-11-22

- **Local game updates and DLC**: press **Y** on a game to choose one update and any
  number of DLC titles from `.nsp` and `.nsz` packages in the adjacent `updates/` and
  `dlc/` folders. Avalon matches package metadata and loads the selection from the SD
  card without installing it. This follows the eden-nx2 adoption plan.

## b7 — 2026-11-22 (`sw2-fork-b7`)

Branch `feat/b7-nsz`. First item of the eden-nx2 adoption plan
(`odd/analysis-eden-nx2.md`): solid NSZ/XCZ support, ported from
`davidkalil10/eden-nx2` (same GPL lineage; files carried over and credited per commit).

- **Solid NSZ/XCZ games load** (`core`): the container scan no longer rejects
  solid-compressed `.nsz`; the decompressor decodes the single zstd stream into a bounded
  cache (pinned head plus LRU, with a keep-tail window so multi-gigabyte seeks do not
  thrash) instead of the whole body. Pre-decrypted NCZ sections skip the AES pass.
- **Fewer open files on Switch 2** (`core`): cached VFS files are closed when their path is
  recreated, moved or deleted, not only when the reference list overflows.
- **Picker**: `.nsz` and `.xcz` files list as games.
- The shared zero chunk stays in `.bss` (b6's const copy cost 2 MiB of NRO for nothing).

## b6 — 2026-11-22 (`sw2-fork-b6`)

Branch `feat/b6-backlog-close`. Closes the review findings left open since b1-b3.

- **A failed game comes back to the game list** (`eden_horizon`): a ROM that fails to load
  now relaunches into the picker when "Return to the game list" is on, instead of ending in
  the HOME menu; the relaunch still goes through a fresh process.
- **Backends without the two-level lookup reject it** (`dynarmic`): the riscv64 and
  loongarch64 backends assert like x64 when `page_table_level2_bits` is set.
- **A32 JIT caches sized per core** (`core`): like A64, from last session's lists.
- **The shared zero chunk is read-only storage** (`common`).
- **Self-test**: the fallback tag's low bits no longer read as a plain address on A32.

## b5 — 2026-11-22 (`sw2-fork-b5`)

Branch `feat/b5-cover-art`.

- **Cover art in the picker** (`eden_horizon`): the selected game's cover shows on the
  right of the list, aspect-fitted in a rounded frame. Covers are user-supplied PNGs next
  to the ROM, named after the title id in the dump's name (`0100e6d00e81c000.png`,
  Eden's own icon convention) or after the game file with a `.png` extension; up to
  2048x2048. Games without art show a placeholder that explains the naming. PNG decoding
  is a vendored stb_image v2.34 (public domain), PNG only, with host tests.
- Repo moved to `~/Proyectos/Personales/avalon`.

## b4 — 2026-11-22 (`sw2-fork-b4`)

Branch `feat/b4-ui`.

- **Graphical menu** (`eden_horizon`): the game picker and the options screen are drawn in
  software into a 1280x720 framebuffer — Avalon logo and build tag, readable game names
  (no more `[TitleID][v0][Base].nsp`), NSP/XCI badges, sizes, folder icons, a scroll bar,
  button hints and the selected option's help text in a side panel. The text console stays
  as a fallback if the framebuffer cannot be opened. Fonts are Noto Sans (OFL-1.1),
  rasterized at build time by `tools/switch2/ui/make_ui_assets.py`; the emblem, logo and
  icon are original artwork. Host preview: `UI_PREVIEW_DIR=... tools/switch2/tests/run.sh
  ui_render_test` writes each screen as an image.
- **App icon** (`eden_horizon`): the NRO carries its own 256x256 icon in the homebrew menu.
- **Back to the game list** (`eden_horizon`): leaving a game with + and - asks the loader
  to start Avalon again (`envSetNextLoad`), so the game list returns in a fresh process;
  option "Return to the game list" (on) turns it off. A close by the system does not
  relaunch.

## b3 — 2026-11-22 (`sw2-fork-b3`)

Branch `feat/b3-robustness-fps-diagnostics`.

- **Robustness** (`common`): `SparseLargeVector` rejected indices greater than its size
  but not the index equal to it, which read one entry past the chunk map (an
  AddressSanitizer heap-buffer-overflow in the host test); every accessor now rejects the
  boundary index. Added a multi-page, cross-chunk `ZeroRegion` test.
- **FPS diagnostics** (`core`, `eden_horizon`): `PerfStats` tracks the slowest system
  frame of each window and how many frames were presented, so the log no longer averages
  `nan` while nothing is drawn. The `Performance` line reports `frame X ms (worst Y ms)`
  and `JIT mem fallbacks N`: how many guest memory accesses the JIT served through the
  callbacks — the visible cost of the two-level lookup and of misaligned or unmapped
  addresses. First data point (b2, Super Mario 3D World, first session in `full`): the
  early fps dips tracked JIT compile time, with zero texture evictions.

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
