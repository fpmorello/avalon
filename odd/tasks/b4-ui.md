# Build b4: friendlier UI (logo, graphical menu)

Objective: one NRO build (`b4`) with the same emulation core as b3 and a friendlier front
end: an app icon and logo, and a graphical game picker and options menu drawn into a
1280x720 framebuffer instead of the 80x45 text console. Branch `feat/b4-ui` off
`feat/b3-robustness-fps-diagnostics`.

## Specs

- S1: "Ponele un logo" — the NRO has its own icon in the homebrew menu, and the picker
  shows the Avalon logo.
- S2: "Un menu" — a graphical menu: game list and options screen with highlighted
  selection, button hints and the options' help text.
- S3: "Algo mas amigable ?" — readable game names (no `[TitleID][v0][Base].nsp` noise),
  file type and size, folders told apart from games, an empty-folder message, and the same
  controls as before (A open, B back, X options, Y game options, + exit).
- S4: build `b4`, CHANGELOG.md, README.

## Tasks

- T1 (S3, inline): pure display helpers (`ui_text.h`: game display name, size label)
  with RED/GREEN host test. DONE: RED = "ui_text.h file not found"; GREEN ASan+TSan.
- T2 (S1, S2, inline): asset generator (`tools/switch2/ui/make_ui_assets.py`: Noto Sans
  glyph atlases, logo, 256x256 icon) and a platform-independent renderer
  (`ui_render.{h,cpp}`) checked by a host preview test that writes the screens as images.
  Commit: pending.
- T3 (S2, S3, inline): `platform.cpp` picker/options on a linear framebuffer, closed
  before Vulkan takes the window; icon wired into `nx_create_nro`. Commit: pending.
- T4 (S4, inline): version b4, CHANGELOG, README, NRO build. Commit: pending.

## Log

- L1 (user): "Te animas a hacer una b4 igual a esta pero con mejoras de UI . Ponele un logo
  Un menu Algo mas amigable ?"
- L2 (evidence): picker/options are `\x1b` text on libounce's console (platform.cpp
  Draw/DrawOptions); Switch 2 framebuffer is linear-only via framebufferMakeLinear on
  nwindowGetDefault, one framebuffer per window; nx_create_nro accepts ICON (256x256 JPEG).
  Font: Noto Sans (OFL-1.1) from the host, rasterized at build time into the source so the
  device needs no font library; the OFL text ships next to it.
- L3 (decisions, mine): logo and icon are an original design (island emblem, "Avalon"
  wordmark); dark theme; behavior and settings files unchanged.
