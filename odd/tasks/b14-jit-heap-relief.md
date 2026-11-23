# Avalon b14: JIT heap-pressure relief that does not loop

Baseline: b13 (`sw2-fork-b13`). Generic fixes from the b13 Little Racer and Minecraft Dungeons 2
logs; no per-game settings. The NRO is built on the devkitPro machine; no SD or NAND writes.

## Specs

- S1: “y dale empeza . Veamos si podemos hacer algo generico que funcione para otros juegos. Por que si vamos actualizando de juego en juego no se termina mas. No digo algo super estable, peroooo .”
- S2: “Y si ves que no existe oportunidad de mejora por las limittaciones , decime y no intentamos mas.”
- S3: “si implementa ambas mejoras y todo lo que identifiques”

## Tasks

- T1 (S1, S3): DONE — repeat a heap-pressure eviction or clear only after free heap has fallen a further 64 MiB from the highest it has been since the last one, always below 256 MiB free; count held checks in `Memory detail:`. Inline; host policy test; independent verification and recheck. Commit `7d9613dded`.
- T2 (S2, S3): DONE — Vulkan device loss in Little Racer: no generic change is supported by the evidence (L4, L6). Inline; read-only; no commit.
- T3 (S1, S3): DONE — b14 version, changelog, performance guide and handoff. Inline. Commit: the one that adds this document.
- T4 (S1): BLOCKED — build b14 with devkitPro and check T1 on Minecraft Dungeons 2. Waits for the user.

## Log

- L1: “y dale empeza . Veamos si podemos hacer algo generico que funcione para otros juegos. Por que si vamos actualizando de juego en juego no se termina mas. No digo algo super estable, peroooo .

  Y si ves que no existe oportunidad de mejora por las limittaciones , decime y no intentamos mas.”
- L2: “si implementa ambas mejoras y todo lo que identifiques”
- L3: b13 Minecraft Dungeons 2 (local update 1.1.3.0, docked, 1×, accurate GPU, boost on): about 30 FPS, then 6.1 and 0.2–0.3 FPS for more than 70 s. JIT cache clears rose from 6 to 127 (119 for heap) and partial evictions to 2,102,549 blocks, while logged heap use kept rising from 7,416 to 7,566 of 9,001 MiB. Free heap stayed below the 1,536 MiB JIT guard, so every check dropped code again without freeing heap.
- L4: b13 Little Racer at 1× and 0.75× (boost on, async off): both runs end with `Uncaught exception: VK_ERROR_DEVICE_LOST` and identical backtraces when level 2 starts. `Device loss occurred!` is absent, so the loss did not pass through `Device::ReportLoss()`; the earlier claim of a 15 s wait on this path was wrong. No b13 ELF is available here to resolve the offsets.
- L5: T1 RED (policy missing), then GREEN in `jit_eviction_test` under ASan and TSan; eight Python tests passed. Independent verification passed with two advisories: relief could stop near real exhaustion, and a partially recovered heap was not tracked. Both fixed through `HeapReliefBackoff` (RED, GREEN); targeted recheck passed with no blockers. The Switch-only integration cannot be compiled here: dynarmic dependencies are fetched at configure time and devkitPro is absent.
- L6: T2 decision: returning to the game list after a device loss would reuse a process that did not tear down normally, which b9 verification rejected. Without the b13 ELF the failing Vulkan call is unknown, so there is no evidence for a renderer change. Next step: resolve the backtraces on the build machine (handoff, Next 2).
