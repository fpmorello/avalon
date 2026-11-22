# Build b5: game cover art in the picker

Objective: one NRO build (`b5`) that shows the selected game's cover on the right of the
game list. Covers are user-supplied PNGs next to the ROM, matched by convention.
Branch `feat/b5-cover-art` off `feat/b4-ui`. Repo now at /home/franpm/Proyectos/Personales/avalon.

## Specs

- S1 (user): "a nivel UI ... soportar en la pantalla de seleccion, sobre la derecha la
  caratula del juego. Que deberiamos poder subirla aparte en una carpeta del juego y de
  alguna manera relacionarla. El mismo nombre pero en png, no lo se. Debe haber una
  convencion para eso."
- S2: build `b5`, CHANGELOG.md, README documents the convention.

## Convention (decision)

Covers live in the ROM's own folder. For `Little Racer [0100E6D00E81C000][v0][Base].nsp`
the picker looks for, in order:
1. `0100e6d00e81c000.png` — the Title ID in lower case (the convention Eden/yuzu use for
   icons, so existing art often matches), taken from the first `[16 hex]` group;
2. `Little Racer [0100E6D00E81C000][v0][Base].png` — the file name with the extension
   swapped to .png.
Folders show no cover; a game with no cover shows a placeholder with the hint.

## Tasks

- T1 (S1, inline): `UiText::TitleIdOf` + RED/GREEN host test. DONE: RED = missing header;
  GREEN ASan+TSan (16-hex group, case folding, rejects short/long/non-hex).
- T2 (S1, inline): PNG loading (`ui_image.{h,cpp}`, stb_image v2.34 public domain,
  PNG-only) + host test with an embedded image; cover drawing in `ui_render` (aspect-fit,
  rounded frame, placeholder with the convention hint) + preview checks. Loader DONE:
  ui_image_test green (3x2 decode with known pixels, junk/truncated/missing rejected,
  >2048px rejected before decode); stb v2.34 vendored (public domain), PNG-only.
- T3 (S1, S2, inline): picker integration (cover cache, load on selection change), version
  b5, CHANGELOG, README, NRO build. Commit: pending.

## Log

- L1 (user): "Te voy a encargar varias tareas para que avances solo." + S1 quote + repo
  move to /Personales/avalon + analysis of davidkalil10/eden-nx2 + "Avanza sin preguntarme,
  toma las mejores decisiones".
- L2: repo moved from edensw2/eden-switch2-poc-fpmorello (build/ dropped, paths were
  absolute); .gitignore for *.nro.
