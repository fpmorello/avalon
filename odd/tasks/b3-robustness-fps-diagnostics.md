# Build b3: SparseLargeVector robustness + FPS drop diagnostics

Objective: one NRO build (`b3`) that (1) fixes the recorded SparseLargeVector boundary
defects, and (2) makes FPS drops attributable: the log averages frametime (it even prints
`nan` before the first frame) and never says how bad the worst frame was or how often the
JIT fell back to memory callbacks. Branch `feat/b3-robustness-fps-diagnostics` off
`feat/b2-full-space-eviction-fixes`.

## Specs

- S1: robustness — "Chequeo de límites con error de uno (`index >` debería ser `>=`)" and
  "El camino de `ZeroRegion` de varias páginas no tiene test."
- S2: FPS diagnostics — user: "Podemos tambien explorar esas caidas de FPS? Deberiamos poder
  mantener 60 fps siempre." The Performance line must show the worst frame of the window
  instead of only the average, never print `nan`, and report how many guest memory accesses
  the JIT served through the slow callback path (the direct cost of the two-level lookup and
  of misaligned/unmapped accesses).
- S3: build `b3`, CHANGELOG.md.

## Tasks

- T1 (S1, inline): `>=` boundary fix in `SparseLargeVector` guards + RED/GREEN host tests
  (index == size must not allocate nor touch chunk_map out of bounds; ZeroRegion over
  several pages and a cross-chunk tail). DONE: RED = ASan heap-buffer-overflow at
  index == size (chunk_map read one past its end); GREEN = all four suites pass ASan+TSan.
- T2 (S2, inline): worst-frame and frame-count tracking in `PerfStats` (all platforms),
  `nan` fix and richer Performance line in the frontend, `eden_horizon_jit_mem_fallbacks`
  counter in the A64/A32 memory callbacks. Commit: pending.
- T3 (S3, inline): `version.h` b3, CHANGELOG.md, NRO build. Commit: pending.
- T4 (S1-S3, gentle-ai-verify): independent verification.

## Log

- L1 (user): "Seguimos con el proximo lote. Podemos tambien explorar esas caidas de FPS ?
  Deberiamos poder mantener 60 fps siempre."
- L2 (evidence, b2 device session, SM3DW ~2.5 min in `full`): 52-59 fps steady, speed 100%,
  drops only in the first ~35 s alongside JIT busy 16-23% of wall time (first session in
  `full`, cold caches: 162k blocks compiled, 0 texture evictions, 0 cache clears). So the
  observed drops are compile stutter, not texture pressure; the graphics backlog group is
  deferred until a session shows texture eviction. `frame nan ms` = frametime divided by
  zero system frames during loading.
