# Avalon Switch 2 handoff — b13

## Local state

- Branch: `feat/b7-nsz`; checked-out commit at handoff: `8f50ddebc7` (`sw2-fork-b7`). The b8–b13 source, tests and docs are local changes, not staged or pushed.
- Run `git status --short --branch` and review the tracked diff and new source/test/docs files. Do not bulk-stage: `.codegraph/`, `.pi/`, `build/` (includes local backups), ignored NROs, and any console logs or saves are not part of the source delivery. Follow `AGENTS.md` for human-owned commit messages and publishing.
- Final local `avalon-b13.nro`: 42,905,415 bytes, SHA-256 `ecbe710269a59dae6be652ff6c06ee3f720633b6de83c2940cab6f54f8064a19`. Job 24 passed the Switch 2 build, comparison against build output, NRO magic/version checks, and b8–b12 hash comparison. Focused host retention/cap test, eight Python tests and independent log-fallback verification passed. Real SD retention is still unverified.
- Build: `DEVKITPRO=/opt/devkitpro sh tools/switch2/build.sh -j 6 eden-horizon_nro`. The script copies the result to the versioned local NRO. Preserve prior NROs and user saves; no agent writes to SD or NAND.

## Next: diagnose sustained low FPS, not just startup stalls

1. Use the **same b13** for both Little Racer runs. Same track and settings, warm cache, async and profiler off, first `1×`, then `0.75×`. Capture comparable gameplay and record FPS and emulation speed. Use the first batch to verify that both runs survive a return to the picker. The user only needs to connect the SD after the batch.
2. Read `sdmc:/switch/avalon/.local/share/avalon/log/games/` read-only. b13 is designed to retain the latest ten runs per selected game path. Check the `Option` lines, FPS, speed, and worst frame times. Use `tools/switch2/perf_summary.py` and `docs/Switch2Performance.md` for analysis. The picker log is separate; fallback messages can appear there on log failure or size cap.
3. If lowering resolution improves FPS materially, keep resolution and upscaling isolated. Compare the existing spatial FSR 1 or SGSR against baseline at the **same** reduced resolution before changing any defaults. Do not promise DLSS or assume upscaling itself improves FPS.
4. If resolution barely matters, take a separate diagnostic run at `1×` using **Profiler (diagnostic)**. Match the ELF to the exact NRO before symbolizing `elf` addresses. Use the profile to distinguish CPU/JIT work from renderer waits; the sampler affects timing, so do not use that run for the FPS comparison.
5. Separately trace the non-heap JIT cache clear seen in Mario before changing capacity. The clear and a subsequent 3.7 s worst frame were observed in neighboring log windows; no causal link or exact scene timestamp is known. This does not explain sustained Little Racer performance by itself.

## Evidence and boundaries

- b12 Little Racer, async on: user saw 27 FPS in intro and ~14 average in gameplay, with no perceived improvement. Its log rotated out; no comparable async-off run. Keep async off by default.
- b12 Mario 3D World + Bowser’s Fury v0, handheld, 1×, accurate GPU, boost on, async on: three windows at 60 FPS and a final 26.1 FPS window at reported 100% emulation speed. The user saw ~28 FPS in Bowser’s Fury; the log does not mark scene boundaries.
- The clock replacement was rejected by hardware measurement. Keep clocks, unsafe atomics, GPU accuracy and game-specific compatibility changes out of the performance A/B.
- Wonder b11 exits to Switch HOME without a surviving log; do not ask for another unchanged run. If reprioritized, start with persistent diagnostics and an isolated hardware JIT self-test. Minecraft with a local update reached intro but went black afterward; neither case is a gameplay performance baseline.
- Earlier scope and evidence: `odd/tasks/b8-updates-dlc.md`, `odd/tasks/b9-launch-errors.md`, `odd/tasks/b10-startup-feedback.md`, `odd/tasks/b12-async-presentation.md`. Engram project: `eden-switch2-poc`; async observation `2287`, b13 log retention `2288`.
