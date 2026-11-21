# Adaptive VRAM budget + robustness fixes (Switch 2 port)

Objective: one-build batch (same branch `improve/sparse-page-table-jit-eviction`, commits
separate for bisection): (1) make the GPU memory budget adaptive instead of the fixed
2 GiB cap, attacking the Echoes of Wisdom texture-eviction hypothesis; (2) log
SparseLargeVector::Set allocation failures (approved-review advisory R3/R4-silent-set-drop);
(3) make the guest system-settings save not lose the file if the process dies between
remove and rename. Test runner: tools/switch2/tests/run.sh (host) + structural checks.

## Specs

- S1: The GPU memory budget (`vulkan_device.cpp`, fixed 2 GiB) becomes adaptive to the
  host heap on `__SWITCH__`: a quarter of the heap, clamped to [1 GiB, 2 GiB] — a 9 GiB
  heap keeps today's 2 GiB. `VramUsageMode::Aggressive` spends a third, clamped to
  [1.5 GiB, 3 GiB], and the per-game GPU option's "fast" choice selects it (zero new UI;
  description updated). Host builds unchanged. Check: structural + focused reading; no
  host behavior change (guards).
- S2: `SparseLargeVector::Set` (chunked backend) logs a critical error when the chunk
  allocation fails instead of silently dropping the write (review finding). Check:
  existing sparse_chunked_test still green (behavior unchanged apart from the log).
- S3: The settings save in `src/core/hle/service/set/system_settings_server.cpp` (remove +
  rename window) is hardened so a crash between remove and rename leaves the previous file
  or the new file, never nothing. Check: structural reading of the save path; no host
  behavior change.

## Tasks

- T1 (S1, inline): Read both GPU memory sites, implement adaptive budget under `__SWITCH__`
  guards, structural verification. DONE: vulkan_device.cpp budget = clamp(heap/4, 1, 2 GiB)
  normal / clamp(heap/3, 1.5, 3 GiB) Aggressive; "gpu fast" option now sets Aggressive
  (main.cpp) with honest description; texture GC floor left fixed (one variable at a
  time). GpuAccuracy::High on "accurate" matches the existing non-Android default
  (settings.h:566).
- T2 (S2, inline): Set() failure logging in sparse_large_vector.h; re-run sparse_chunked_test.
  DONE: LOG_ERROR on dropped write; all host tests green.
- T3 (S3, inline): Harden the settings write; structural verification. DONE: LoadSettingsFile
  promotes a finished settings.tmp when settings.dat is missing (the remove→rename crash
  window of Save), size-checked, guarded __SWITCH__.
- T4 (all, inline): Focused re-runs (all host tests), commits per unit, doc/Engram/todo update.

## Log

- L1 (2026-11-21, user): "Con que cmabio seguimos ? Hagamos algo mas asi no estamos compilando
  20 versiones." → chose batch "Gráficos + robustez": VRAM adaptativo + Set() logging +
  settings atómicos; one NRO build, same branch, separate commits.
- L2 (design): heap-proportional budget keeps 2 GiB for a 9 GiB heap (no behavior change in
  the default path); the EoW lever is the per-game "gpu: fast" choice now also selecting
  VramUsageMode::Aggressive (3 GiB cap) — opt-in, logged ("Graphics memory budget" line),
  observable via eden_horizon_texture_evictions.
