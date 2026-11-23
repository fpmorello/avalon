# Avalon Switch 2 handoff — b14

## Repository state

- Repository: https://github.com/fpmorello/avalon (renamed from `eden-switch2-poc`). `main` holds b13, tagged `sw2-fork-b13`. b14 is on the local branch `feat/b14-jit-heap-relief`, not pushed or tagged; scope and evidence are in `odd/tasks/b14-jit-heap-relief.md`.
- Do not commit `.codegraph/`, `.pi/`, `build/` (includes local backups), NROs, console logs or saves. Follow `AGENTS.md`: publishing needs the maintainer's explicit request.
- `avalon-b14.nro` is not built yet: the session that wrote b14 had no devkitPro. The policy host test passed under ASan and TSan and the eight Python tests passed; the Switch-only JIT integration has not been compiled. Last built NRO: `avalon-b13.nro`, 42,905,415 bytes, SHA-256 `ecbe710269a59dae6be652ff6c06ee3f720633b6de83c2940cab6f54f8064a19`.
- Build: `DEVKITPRO=/opt/devkitpro sh tools/switch2/build.sh -j 6 eden-horizon_nro`. The script copies the result to the versioned local NRO. Preserve prior NROs and user saves; no agent writes to SD or NAND.

## Next

1. **Build b14 and check it on Minecraft Dungeons 2** with the local update, b13's options (docked, `1×`, accurate GPU, boost on, JIT and shader caches on) and the menu left running for several minutes. b13 fell to 0.2–0.3 FPS with 127 cache clears. Read `Memory detail:` as described in `docs/Switch2Performance.md`: b14 should show held checks instead of clears climbing every window. If heap use keeps rising until it runs out, the JIT is not the cause.
2. **Resolve the Little Racer `VK_ERROR_DEVICE_LOST` backtraces** with the b13 ELF on the build machine (`find build/switch2 -name 'eden-horizon*.elf'`). The ELF must come from the same build as the NRO that crashed. Both b13 runs 3 and 4 logged the same two backtraces:
   - `0x814c758 0x814cba0 0x8f98a0c 0x809c3f0 0x8f98bdc 0x878ae44 0x87877a0 0x8787f50 0x8fd37fc 0x9922764 0x9930928`
   - `0x814c758 0x814cba0 0x8f98a0c 0x809c3f0 0x8f98bdc 0x878ae44 0x87650f8 0x8769b90 0x86b6128 0x86b49bc 0x86b5b8c 0x868a478 0x868eb30 0x868ef44 0x86939e0 0x86940c4 0x8fd37fc 0x9922764 0x9930928`

   Use `aarch64-none-elf-addr2line -f -C -e <elf> <offsets>`. Decide a renderer change only from the resolved call.
3. Little Racer FPS: `0.75×` against `1×` (boost on, async off) gave no sustained gain (slow windows 13.6 vs 14.0 FPS). Do not change resolution defaults or port the Eden-NX2 Vulkan memory budgets on this evidence.

## Evidence and boundaries

- b13 Minecraft Dungeons 2: about 30 FPS, then 6.1 and 0.2–0.3 FPS for more than 70 s while JIT heap clears rose to 119 and logged heap use kept rising (7,416 to 7,566 of 9,001 MiB). The game shut down cleanly when quit.
- b13 Little Racer: both runs crash at the start of level 2 with identical backtraces. `Device loss occurred!` is absent: the loss did not pass through `Device::ReportLoss()`. Returning to the game list from this crash is not safe: b9 verification rejected reuse of a process that did not tear down normally.
- The clock replacement was rejected by hardware measurement. Keep clocks, unsafe atomics, GPU accuracy and game-specific compatibility changes out of performance A/B runs.
- Wonder b11 exits to Switch HOME without a surviving log; do not ask for another unchanged run.
- Earlier scope and evidence: `odd/tasks/b8-updates-dlc.md`, `odd/tasks/b9-launch-errors.md`, `odd/tasks/b10-startup-feedback.md`, `odd/tasks/b12-async-presentation.md`. Engram project: `eden-switch2-poc`.
