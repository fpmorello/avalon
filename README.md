<!--
# SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

# SPDX-FileCopyrightText: 2018 yuzu Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later
-->
<!-- lang: en-GB -->

# Eden on Switch 2 — proof of concept

> **This is an unofficial, experimental proof of concept. It is unstable.**
> It has improved a lot, but expect crashes, hangs, stutter and missing graphics.
> It is not affiliated with or supported by the Eden project: please do **not** report
> problems with this port to Eden. Use this repository's issues instead.

A port of the [Eden](https://git.eden-emu.dev/eden-emu/eden) Switch 1 emulator to Switch 2
homebrew, built with [ounce-devkit](https://github.com/ouncebrew/ounce-devkit) (libounce,
OunceVK and libkaihen) and run under kaiHEN. It is based on Eden commit `d85bad8`; the
original README follows below.

*Español: prueba de concepto no oficial de Eden para homebrew de Switch 2. Sigue siendo
inestable, aunque ya mejoró bastante. No reportes fallos de este port al proyecto Eden.*

## State

| Game | Result |
|---|---|
| Luigi's Mansion 2 HD | Playable: correct picture, audio, controls, gyro, rumble, saves |
| The Legend of Zelda: Echoes of Wisdom | Playable at 30 fps and above; stutters when entering new areas, rifts are only drawn from the raised camera, and loading a save sometimes hangs |
| Super Mario 3D World + Bowser's Fury | Plays near 60 fps with occasional drops; only tested for a few minutes |
| Mario Strikers: Battle League | Unreliable: needs *Address space* `game` or `full`; some starts get past the intro and play at 60 fps, others stay on a black screen |
| Kirby and the Forgotten Land | Starts and plays, but drops to 15-20 fps inside levels |

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
- No touch screen, no network, one player, and no way back to the game list without
  leaving the emulator.

## Using it

1. Copy `avalon.nro` to `sdmc:/switch/`.
2. Put your own `prod.keys` and `title.keys` in `sdmc:/switch/avalon/.local/share/eden/keys/`.
3. Start it from the homebrew menu and choose a game file. **X** opens the options for
   every game, and **Y** the options of the game under the cursor, which start out
   inheriting the general ones.
4. Hold **+** and **−** together to leave the game.

The log is written to `sdmc:/switch/avalon/.local/share/eden/log/eden_log.txt`.

To check the JIT's two-level page table lookup on your console, create an empty file
`sdmc:/switch/avalon/test_pagetable.txt` and start the emulator. The file is removed, a
short A64 and A32 test runs before the game list, and the result is written to
`sdmc:/switch/avalon/test_pagetable_result.txt`: one PASS/FAIL line per check and
`RESULT PASS` or `RESULT FAIL` at the end. If the file only says `running`, the test
crashed the emulator.

No games, keys or firmware are included, and none will be provided.

## Building

On Windows with MSYS2 and devkitPro's ounce-devkit packages installed:

```sh
sh tools/switch2/configure.sh
sh tools/switch2/build.sh eden-horizon_nro
```

The result is `build/switch2/bin/avalon.nro`. The port's own code is in `src/eden_horizon`
and `src/common/horizon`; the rest are changes guarded by `__SWITCH__` or `HORIZON_HOST`.

This port was written with the help of an AI coding assistant.

---

<h1 align="center">
  <br>
  <a href="https://git.eden-emu.dev/eden-emu/eden"><img src="./dist/qt_themes/default/icons/256x256/eden.png" alt="Eden" width="200"></a>
  <br>
  <b>Eden</b>
  <br>
</h1>

<h4 align="center"><b>Eden</b> is a free and open-source (FOSS) Switch 1 emulator started by developer Camille LaVey.
<br>
Written in C++, with builds for Windows, Linux, macOS, Android, FreeBSD and more.
</h4>

<p align="center">
    </a>
    <a href="https://discord.gg/HstXbPch7X">
        <img src="https://img.shields.io/discord/1367654015269339267?color=5865F2&label=Eden&logo=discord&logoColor=white"
            alt="Discord">
    </a>
    <a href="https://stt.gg/qKgFEAbH">
        <img src="https://img.shields.io/revolt/invite/qKgFEAbH?color=d61f3a&label=Stoat"
            alt="Stoat">
    </a>
</p>

<p align="center">
  <a href="#compatibility">Compatibility</a> |
  <a href="#contribute">Contribute</a> |
  <a href="#building">Building</a> |
  <a href="#download">Download</a> |
  <a href="#license">License</a>
</p>

## Compatibility

The emulator is capable of running most commercial games at full speed, provided you meet the necessary hardware requirements.

A list of supported games will be available in future. Please be patient.

Check out our [website](https://eden-emu.dev) for the latest news on exciting features, monthly progress reports, and more!

[![Packaging status](https://repology.org/badge/vertical-allrepos/eden-emulator.svg)](https://repology.org/project/eden-emulator/versions)

## Contribute

To contribute to Eden; be it financially, code, bug reports, or otherwise, see our [Contributing guidelines](./CONTRIBUTING.md).

## Documentation

We have a user manual! See our [User Handbook](./docs/user/README.md).

## Building

See the [General Build Guide](docs/Build.md)

For information on provided development tooling, see the [Tools directory](./tools)

## Download

You can download the latest releases from [our release page](https://git.eden-emu.dev/eden-emu/eden/releases).

Save us some bandwidth! We have [mirrors available](./docs/user/ThirdParty.md#mirrors) as well.

## License

Eden is licensed under the GPLv3 (or any later version). Refer to the [LICENSE.txt](https://git.eden-emu.dev/eden-emu/eden/src/branch/master/LICENSE.txt) file.

## Special thanks

Super special thanks to Cloudflare for preventing the git server from blowing up.

- Yuzu
- Ryujinx
- Sudachi
- Citron
- Torzu
- Suyu
- Ryubing

And everyone who continues or had contributed to the project! <3
