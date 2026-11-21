# Two-level guest page table lookup in dynarmic (Switch 2 port, Avalon)

Objective: keep the chunked `SparseLargeVector` backing (ec36f9edd0) for the guest page table and
make dynarmic's arm64 JIT read it through a chunk directory instead of a flat array, so the NRO
builds again. Branch `feat/two-level-page-table` off `switch2-poc` in `fpmorello/eden-switch2-poc`.

## Specs

- S1: "Lookup de dos niveles en dynarmic" — "Modificar el emisor in-tree: directorio de chunks +
  chunk cero compartido. Ahorra casi toda la tabla incluso en 38/39 bits, pero +1 LDR por acceso a
  memoria (A64 y A32) y cambio delicado en el JIT. Trabajo de varias sesiones."
- S2: "Self-test en la consola" — "Modo diagnóstico en Avalon (como test_exception.txt): si existe
  sdmc:/switch/avalon/test_pagetable.txt, corre un JIT A64 y A32 contra la tabla de dos niveles y
  escribe PASS/FAIL. Sin instalar nada; vos lo corrés en la Switch 2."
- S3: "hacer merge de la rama , asi compilamos y probamos todo." / "commit y compilamos." — the
  Switch 2 NRO (`avalon-b1.nro`) builds with `tools/switch2/build.sh`.

## Tasks

- T1 (S1, inline): `SparseLargeVector` chunked backing: every unmaterialized directory slot points
  to one shared read-only zero chunk (never null); expose the directory and chunk index bits for
  the JIT; RED/GREEN host test via `tools/switch2/tests/run.sh`. DONE: commit aad080bb58.
  RED = only "no member named 'ChunkIndexBits'/'ChunkDirectory'"; GREEN = sparse_directory_test
  + sparse_chunked/sparse_host_behavior/jit_eviction tests all pass (ASan + TSan).
- T2 (S1, inline): dynarmic arm64: optional two-level page table config (A64 + A32 UserConfig ->
  EmitConfig), two-level `InlinePageTableEmitVAddrLookup`; other backends reject the option.
  DONE: commit below (see git log). Check: `ninja dynarmic` for Switch 2 builds clean; no host
  RED possible (x86_64, no qemu) — runtime proof is T4's on-device self-test.
- T3 (S1, S3, inline): `arm_dynarmic_64/32.cpp` pass the directory on `__SWITCH__`; full NRO build
  green. DONE: full `tools/switch2/build.sh` exit 0, built avalon-b1.nro (42604741 bytes).
- T4 (S2, inline): on-device self-test triggered by `sdmc:/switch/avalon/test_pagetable.txt`
  (A64 + A32 JIT loads/stores through the two-level table, PASS/FAIL file); README docs.
  Commit: pending.
- T5 (S1-S3, gentle-ai-verify): independent verification (high risk: JIT memory path,
  concurrency); device run of S2 is the user's. Commit: n/a.

## Log

- L1 (user, original request): "cambiemosle el nombre a la app ... Avalon pongamosle" → "commit y
  compilamos." The build then failed at `arm_dynarmic_64.cpp:205` / `arm_dynarmic_32.cpp:165`:
  "'class Common::SparseLargeVector<Common::PageTable::PageEntryData>' has no member named 'data'"
  — the chunked backing from ec36f9edd0 removed the flat array dynarmic's JIT indexes.
- L2 (user decision): chose "Lookup de dos niveles en dynarmic" over the hybrid (flat PageTable)
  and the truncated-table options.
- L3 (user decision): chose "Self-test en la consola" over installing qemu-user + aarch64 toolchain.
- L4 (evidence): host is x86_64 without qemu-aarch64; dynarmic only supports a flat one-level
  table (`emit_arm64_memory.cpp:257-298`); Horizon gives homebrew no page faults (README:43);
  table sizes flat: 36 bits 128 MiB (default "compact"), 38 bits 512 MiB, 39 bits 1 GiB.
- L5 (design): 2 MiB chunks of 8-byte entries = 2^18 entries per chunk, so level-1 index =
  page_index >> 18 and the directory is 64 (36 bits) to 512 (39 bits) pointers. Unmaterialized
  slots hold a shared zero chunk so the existing `CBZ -> fallback` keeps working. Writers publish
  chunks with release CAS; the JIT's address-dependent LDR pair is ordered on arm64.
  The directory pointer must stay stable while a JIT holds it (same constraint the flat
  `base_ptr` had across `ResizeAndClear`).
- L6 (prior fixes on switch2-poc): d65d14ea96 vulkan_device heapGetTotalSize declaration;
  8b9edfafda build.sh fails when ninja fails.
