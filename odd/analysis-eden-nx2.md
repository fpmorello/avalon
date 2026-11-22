# Analysis: davidkalil10/eden-nx2 vs Avalon (2026-11-22)

Source: clone with full history, GitHub API. Eden-NX2 is very active: five releases in two
days (v0.1.0-alpha 2026-11-21 to v0.1.5 2026-11-22), 15 stars.

## The key fact

Eden-NX2 is **not** a fork of the Olmectron PoC we build on. It diverges from Eden upstream
at `d85bad892` (2026-11-16, the same base commit) and is an **independent parallel port**,
built into the existing CLI frontend (`src/yuzu_cmd`): its whole user-facing layer is
`switch2_launcher.cpp` (a 900+ line file-picker/menu) plus `yuzu.cpp` changes (~830 lines).
Diff vs our base: 107 files, +7969/-414.

So two independent Switch 2 ports of the same emulator now exist, with different strengths.

## What they have that we do not

Ranked by value to us; hashes are their commits.

1. **NSZ/XCZ support** (`2ad946db6`, `748a6d9ce`, `40c0000b5`): solid-compressed games work,
   with an on-the-fly decompressor cache in `src/core/crypto/ncz_decompressor.cpp` (~150
   lines across commits, including an LRU-thrash fix). Users keep NSZ dumps; this removes a
   whole class of "my game is not a file you read" friction.
2. **Update & DLC loading** (`fd286b1f3`, `40c0000b5`): auto-discovery of `updates/` and
   `dlc/` folders next to games, a multi-DLC picker, SD browser. Games that need the update
   NCA to have an ExeFS (our Minecraft Dungeons 2 failure) may start working.
3. **Return to the launcher on load failure** (`748a6d9ce`, 3 lines): the failure path we
   send to the HOME menu (deliberately, to protect the loader's process) they route back to
   the launcher. Same mechanism we already use for clean exits (`envSetNextLoad`).
4. **Timing/perf work in dynarmic** (`fc619dd42`, `1b9165c43`, `e39e7958c`): inline ARM64
   hardware atomics, a WallClock from `cntvct_el0` with a safe fallback (v0.1.4 was a
   hotfix reverting an EL0-trapped variant — adopt only v0.1.5's), "cut Dynarmic
   memory/timer overhead". Small diffs, worth benchmarking against our two-level lookup.
5. **GPU/present tuning** (`4f24972c5`): async present, EDS3/O3 shader flags, no FastLoad
   GPU throttle, JIT and RAM budget knobs. Rationale matches our findings (budget, not
   fps, is the constraint).
6. **Auto-updater** (`fc619dd42`, `switch2_updater.cpp`, 461 lines): GitHub Releases
   self-update. Convenient; also a remote-code vector. Low priority for us; if adopted,
   verify hashes and keep it opt-in.
7. **LSFG frame generation fixes, macro interpreter optimizations** (`2ad946db6`): real
   wins if we ever enable LSFG; their macro-interp optimization is upstreamable.

## What we have that they do not

- **Memory architecture**: chunked on-demand page tables + the two-level JIT lookup
  (S1-S2 proven on device; `page tables 40 MiB` in `full` where a flat table is 1 GiB).
  They still carry the flat table; their "RAM budgets" tune around a cost we removed.
- **Partial JIT eviction under heap pressure** with the b2 correctness fixes; they have no
  equivalent (their fallback is the full cache clear).
- **Diagnostics**: worst-frame and JIT-memory-fallback counters, page-table memory in the
  log, on-device two-level self-test. Their X-ray telemetry is broader, but our numbers
  feed the specific memory story we are optimizing.
- **Graphical UI**: covers, readable names, help panels, framebuffer rendering. Their
  launcher is text-console based.
- **Per-game options** granularity (address space, GPU mode, JIT cache) inherited from the
  Olmectron PoC; they have per-game update/DLC selection instead.
- CPU code cache on disk + per-core sizing, settings atomic-write recovery (from the PoC).

## Recommended adoption order (our next lots)

1. **NSZ/XCZ** — port their `ncz_decompressor.cpp` changes (GPL, same upstream; attribute
   in the commit). Removes user friction, moderate risk (cache correctness).
2. **Updates & DLC** — their discovery + picker logic, adapted to our UI. May fix
   Minecraft Dungeons 2 (ErrorNoExeFS needs the update NCA).
3. **Return to launcher on load failure** — replace our `ExitProcess()`-to-HOME with
   relaunch-to-picker, reusing our b4 relaunch path. Small, but changes the feel.
4. **Dynarmic timing/atomics** — benchmark v0.1.5's approach against our build before
   adopting anything; our lookup changes overlap that code.
5. **GPU tuning flags** — evaluate async present / EDS3 against our VRAM budget work.

Not adopted for now: auto-updater (security surface), telemetry, their launcher UI.

## Risks

- They do not share our base: cherry-picking needs manual adaptation (their diffs are
  against `yuzu_cmd` and upstream files we also modified).
- Both projects will drift; re-evaluate monthly.
- Their release cadence (5/day) suggests limited per-release device validation; adopt
  code, not releases.
