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
- S5: "Actualmente cuando cerras el juego se cierra el emulador ... Si podemos entender por que
  pasa eso y volver directamente al menu del emulador, estamos del otro lado."

## Tasks

- T1 (S3, inline): pure display helpers (`ui_text.h`: game display name, size label)
  with RED/GREEN host test. DONE: RED = "ui_text.h file not found"; GREEN ASan+TSan.
- T2 (S1, S2, inline): asset generator (`tools/switch2/ui/make_ui_assets.py`: Noto Sans
  glyph atlases, logo, 256x256 icon) and a platform-independent renderer
  (`ui_render.{h,cpp}`) checked by a host preview test that writes the screens as images.
  DONE: assets generated (font atlases 25-98 KB, logo 16 KB, icon 12 KB, OFL.txt with Noto
  Sans' copyright — Arch's noto-fonts LICENSE is an Apache text, the OFL body is taken from
  noto-fonts-cjk); ui_render_test GREEN (ASan+TSan, guard bytes past the stride intact,
  tiny canvas clipped); previews reviewed: help text re-wrapped and key/value rows laid out
  in two columns after the first preview showed 80-column breaks.
- T3 (S2, S3, inline): `platform.cpp` picker/options on a linear framebuffer, closed
  before Vulkan takes the window; icon wired into `nx_create_nro`. DONE: MenuScreen opens a
  double-buffered linear RGBA8 framebuffer; text console kept as fallback if it fails;
  page jumps use the 9 visible rows; `ninja eden-horizon` clean. Device check pending.
- T4 (S5, inline): after a clean game shutdown Avalon asks the homebrew loader to start it
  again (envSetNextLoad), so the game list comes back in a fresh process; option "Return to
  the game list" (default on) turns it off. DONE: PumpEvents marks QuitByUser() only for
  + and -, so a system-side close does not relaunch; RequestRelaunch checks envHasNextLoad
  and passes argv[0]; `ninja eden-horizon` clean. Device check pending (does the kaiHEN
  loader honour envSetNextLoad?).
- T5 (S4, inline): version b4, CHANGELOG, README, NRO build. Commit: pending.

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
- L4 (user): "Antes de compilar ... cuando cerras el juego se cierra el emulador, se cierra con
  R + L. Si podemos entender por que pasa eso y volver directamente al menu del emulador".
  Cause: main() runs picker -> one game -> clean shutdown -> return 0, which ends the NRO by
  design; the in-game exit is + and - held (PumpEvents), no R + L binding exists in Avalon.
  Load failures call ExitProcess() to HOME on purpose (emulation threads already started).
- L5 (decision, user "dale"): relaunch through the loader (envSetNextLoad/envHasNextLoad, both
  in libounce.a) instead of looping inside the process: a fresh process avoids re-reserving
  the 4 GiB guest backing in a fragmented heap, Vulkan's hold on the window, and global
  state that does not reset between sessions. Load-failure path unchanged.
