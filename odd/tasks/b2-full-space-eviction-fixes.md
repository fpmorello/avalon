# Build b2: full address space by default, eviction fixes, page-table memory counter

Objective: one NRO build (`b2`) that uses the cheap two-level page table (default address
space `full`), fixes the two correctness defects of the JIT partial eviction recorded by the
b1 review, makes the page-table memory visible in the log, and documents it. Branch
`feat/b2-full-space-eviction-fixes` off `feat/two-level-page-table`.

## Specs

- S1: "cambiar el default a `full`" — `address_space` defaults to `full`; "hoy el modo también
  elige el tamaño de la caché del JIT (64 MiB en `compact`/`game`, 32 MiB en `full`), así que
  habría que desacoplar eso": the code cache size no longer depends on the address space mode.
- S2: "el contador de RAM" — "agregar a `Memory detail` un contador de memoria de la page
  table, del estilo `page table 12 MiB`".
- S3: "corregir los dos defectos de corrección del descarte (duplicados y enlaces entrantes)" —
  "La lista de orden de emisión puede tener bloques duplicados." / "Al descartar un bloque se
  pierden los enlaces entrantes de otros bloques."
- S4: "actualizar changelog y número de build" — build `b2`, CHANGELOG.md entry.

## Tasks

- T1 (S3, inline): host-testable eviction bookkeeping in `jit_eviction.h` (emission queue that
  tells a live block from a stale entry; dead-referrer pruning), RED/GREEN in
  `tools/switch2/tests/jit_eviction_test.cpp`; use it in `address_space.{h,cpp}`. DONE: RED = only
  "no member named IsLiveEmission/PruneDeadReferrers"; GREEN ASan+TSan; `ninja dynarmic` clean.
- T2 (S2, inline): chunk byte counter in `common/horizon/stats.h` fed by `SparseLargeVector`,
  shown in `Memory detail`. DONE: counter lives in sparse_large_vector.h (SparseChunkBytesInUse),
  not stats.h, so the host test covers it; RED = "no member named SparseChunkBytesInUse"; GREEN.
- T3 (S1, inline): default `full`, code cache 64 MiB in every mode, option help text, README.
  Commit: pending.
- T4 (S4, inline): `version.h` b2, CHANGELOG.md, NRO build. Commit: pending.
- T5 (S1-S4, gentle-ai-verify): independent verification (high risk: JIT bookkeeping).

## Log

- L1 (user): "por que no nos ponemos a implementar las siguientes mejoras del plan."
- L2 (user): "Arranca con el lote. No me preguntes, decidi vos." — lot proposed in chat: "Un solo
  build `b2` con los grupos 1 y 2 y el punto 6: cambiar el default a `full`, el contador de RAM,
  corregir los dos defectos de corrección del descarte (duplicados y enlaces entrantes) y
  actualizar changelog y número de build."
- L3 (evidence, device): two-level lookup self-test RESULT PASS (24 checks) on the console;
  Little Racer ran at 100% speed in `compact`.
- L4 (decisions, mine): (a) emission queue stores the entry point with the descriptor, so an
  entry whose block was invalidated or re-emitted is recognised as stale, releases the old
  block's leftover bookkeeping and never evicts the newer block; (b) eviction keeps the inbound
  referrer list of the evicted block (pruned of dead referrers) so a recompiled block is
  re-linked directly instead of through the dispatcher forever; (c) the eviction counter resets
  when a heap check finds the heap no longer low (review: "eviction counter never decays");
  (d) the counter covers every SparseLargeVector (guest page table, device and GPU tables).
