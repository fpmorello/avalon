<!--
# SPDX-FileCopyrightText: Copyright 2026 Avalon contributors
# SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

# SPDX-FileCopyrightText: 2018 yuzu Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later
-->
<!-- lang: en-GB -->

# Avalon

Avalon is an experimental Switch 1 emulator for Switch 2 homebrew, run under kaiHEN and
built with [ounce-devkit](https://github.com/ouncebrew/ounce-devkit) (libounce, OunceVK
and libkaihen).

> **Avalon is unofficial and unstable.** Expect crashes, hangs, stutter and missing
> graphics. It is not affiliated with or supported by Eden or Nintendo: report problems
> in [this repository's issues](https://github.com/fpmorello/avalon/issues), never to Eden.

*Español: emulador experimental de Switch 1 para homebrew de Switch 2. Es inestable. No
reportes fallos de Avalon al proyecto Eden; usá los issues de este repositorio.*

Avalon is a fork of [Eden](https://git.eden-emu.dev/eden-emu/eden) (commit `d85bad8`),
started from [Olmectron's Switch 2 proof of concept](https://github.com/Olmectron/eden-switch2-poc).
The emulator core, JIT and renderer come from Eden and, before it, yuzu.

## State

| Game | Result |
|---|---|
| Luigi's Mansion 2 HD | Playable: correct picture, audio, controls, gyro, rumble, saves |
| The Legend of Zelda: Echoes of Wisdom | Playable at 30 fps and above; stutters when entering new areas, rifts are only drawn from the raised camera, and loading a save sometimes hangs |
| Super Mario 3D World + Bowser's Fury | Plays near 60 fps with occasional drops; only tested for a few minutes |
| Mario Strikers: Battle League | Unreliable: needs *Address space* `game` or `full` (`full` is the default); some starts get past the intro and play at 60 fps, others stay on a black screen |
| Kirby and the Forgotten Land | Starts and plays, but drops to 15-20 fps inside levels |
| Little Racer | Plays, but averages about 14 fps in races |
| Super Mario Bros. Wonder (v1.2.1) | Exits to the HOME menu during startup |
| Minecraft (v1.1.3.0) | Reaches the intro, then shows a black picture |

Nothing else has been tested.

What works: the dynarmic JIT (through kaiHEN's JIT memory), Vulkan rendering on NVK,
audio output, one controller with motion and rumble, saves, the disk shader cache, a disk
cache of compiled CPU code, a file picker for the SD card and a small options screen.

Known limits:

- No fastmem and no NCE: kaiHEN does not hand CPU exceptions to homebrew, so every guest
  memory access goes through the page table.
- Memory is tight. The emulator, the guest's 4 GiB and the GPU share one heap of about
  9 GiB, and long sessions can still run out. The guest page table is allocated on
  demand in 2 MiB chunks, which the JIT reads through a two-level lookup (one extra load
  per guest memory access), and the JIT drops its oldest blocks before clearing a whole
  cache, but a full clear remains the last resort.
- New guest code stutters while it is compiled. The CPU code cache removes most of it from
  the second session of a game on, at the cost of a longer start.
- No touch screen, no network, one player. Leaving a game restarts the app into the game
  list ("Return to the game list", a fresh process); it does not go straight back while
  the game's memory is still held.

## Using it

1. Copy `avalon.nro` to `sdmc:/switch/`.
2. Put your own `prod.keys` and `title.keys` in `sdmc:/switch/avalon/.local/share/avalon/keys/`.
3. Start it from the homebrew menu. The game list shows each game's readable name, file
   type and size, and the selected game's cover; folders open with **A**. **X** opens the
   options for every game, and **Y** the options of the game under the cursor, which start
   out inheriting the general ones.
   Covers: put a PNG next to the game, named after the title id in the game's file name
   (`0100e6d00e81c000.png`) or after the game file itself (`Little Racer [....nsp]` with
   the extension changed to `.png`). Up to 2048x2048.
4. Hold **+** and **−** together to leave the game. Avalon then starts again into the game
   list, unless "Return to the game list" is off.

### Local updates and DLC

For games in NSP, NSZ, XCI or XCZ containers, put your dumped `.nsp` or `.nsz` add-ons
in `updates/` and `dlc/` beside the base game:

```text
sdmc:/games/
├── My Game.xci
├── updates/
│   └── My Game Update.nsp
└── dlc/
    ├── My Game DLC 1.nsp
    └── My Game DLC 2.nsz
```

Highlight the base game and press **Y**. Scroll to **Local update** and the **Local DLC**
rows. Use **←/→** to select one update and any number of DLC titles, then **B** to save.
Avalon matches package metadata to the game and ignores unrelated or unreadable add-ons.
It reads the original files from the SD card, without installing them. If a selected file
is moved, renamed or replaced with a different version, select it again. **No local update**
and **Off** leave any separately installed or bundled content to the existing loader.

### If a game does not open

While starting, Avalon shows the current stage. If there are cached shaders to process,
Avalon shows the actual number of cached pipelines processed. The waiting card stays
until a guest frame replaces it; it does not declare the game hung. The log records
whether Vulkan accepted a guest frame, even if the game drew a black frame.

For a game the loader cannot open, Avalon shows the reason, a suggestion, and an error code.
Press and release **A** to reopen the game list through the homebrew loader. If the loader
cannot restart the app, Avalon explains that before asking you to close it. If you replace
your own keys, restart Avalon before trying the game again. The folder for `prod.keys` and
`title.keys` is
`sdmc:/switch/avalon/.local/share/avalon/keys/`.

This screen does not recover renderer failures, crashes, or hangs. Starting with b13,
Avalon keeps the last ten launch logs per game in
`sdmc:/switch/avalon/.local/share/avalon/log/games/`. Look for the latest `run-*.log` in
the game's folder. The picker log, `avalon_log.txt`, also receives messages if the game
log cannot be opened or reaches its size limit.

To check the JIT's two-level page table lookup on your console, create an empty file
`sdmc:/switch/avalon/test_pagetable.txt` and start the emulator. The file is removed, a
short A64 and A32 test runs before the game list, and the result is written to
`sdmc:/switch/avalon/test_pagetable_result.txt`: one PASS/FAIL line per check and
`RESULT PASS` or `RESULT FAIL` at the end. If the file only says `running`, the test
crashed the emulator.

No games, keys or firmware are included, and none will be provided.

## Building

Install devkitPro with the ounce-devkit packages (libounce, ouncevk, libkaihen), the
portlibs `switch-libzstd switch-libopus switch-enet switch-ffmpeg switch-lz4`, and
`glslangValidator` on the host. Then, on Linux or on Windows with MSYS2:

```sh
export DEVKITPRO=/opt/devkitpro
sh tools/switch2/configure.sh
sh tools/switch2/build.sh -j 6 eden-horizon_nro
```

The result is `build/switch2/bin/avalon.nro`. Avalon's own code is in `src/eden_horizon`
and `src/common/horizon`; the rest are changes guarded by `__SWITCH__` or `HORIZON_HOST`.
Host tests are in `tools/switch2/tests`: `sh tools/switch2/tests/run.sh <test name>` runs one
C++ test (for example `game_logs_test`), and `python3 -m unittest discover tools/switch2/tests`
runs the Python ones.

- [CHANGELOG.md](CHANGELOG.md): what each build (`b<N>`, tagged `sw2-fork-b<N>`) changed.
- [docs/Switch2Performance.md](docs/Switch2Performance.md): how to measure FPS and compare runs.
- [odd/](odd): design notes and task records.

The rest of `docs/` is Eden's documentation for the shared emulator core.

Avalon was written with the help of AI coding assistants.

## License

Avalon is licensed under the GPLv3 or any later version, as Eden is. See
[LICENSE.txt](LICENSE.txt) and [LICENSES/](LICENSES). Files keep their original copyright
notices.

## Thanks

- [Eden](https://git.eden-emu.dev/eden-emu/eden) and the projects it builds on: yuzu,
  Ryujinx, Sudachi, Citron, Torzu, Suyu and Ryubing.
- [Olmectron](https://github.com/Olmectron) for the original Switch 2 proof of concept.
- [ounce-devkit](https://github.com/ouncebrew/ounce-devkit) and kaiHEN.
