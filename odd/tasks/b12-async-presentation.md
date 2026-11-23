# Avalon b12: asynchronous presentation experiment

Baseline: b11. Preserve b10 and b11 NROs. Local experiment only; no commits or SD modifications by the agent.

## Specs

- S1: “Investiga, proba, hace algo disruptivo (sin romperme la nand)”

## Tasks

- T1 (S1): DONE — map the Vulkan worker, startup handoff, and shutdown risks. Read-only explore; no commit.
- T2 (S1): DONE — worker lifecycle, WSI fault checks and Switch 2 `video_core` cross-build. Inline; no commit.
- T3 (S1): DONE — default-off selector, standalone b12 NRO inspection and A/B instructions. Inline; no commit. Hardware A/B still pending.

## Log

- L1: Si dale, tenemos que casi poder emular 1:1 la potencia de Siwtch 2 . No puede ser que juegos de switch 1 funcionen lento en Switch 2.  
  Investiga, proba, hace algo disruptivo (sin romperme la nand)

  Tenemos que tenre el mejor emulador de la tierra.
- L2: Agreed first experiment: compare sync and async Vulkan presentation with the option off by default; do not change NAND, firmware, CPU clocks, GPU accuracy, or game files. Device performance is a separate measurement, not promised.
- L3: Read-only exploration found concurrent producer reads of swapchain formats, an incomplete scheduler/present drain sequence, and an implicit worker teardown hazard. Keep synchronous mode available and preserve startup-card ownership and guest-frame notification semantics.
- L4: Observed structural RED and GREEN. Independent host fault probe passed surface-loss and out-of-date retirement with a pending copy in both modes after the final correction. `video_core` cross-build job 21 exited 0.
- L5: Eight Python tests passed. Full Switch 2 NRO cross-build job 22 exited 0. `avalon-b12.nro` matches the build output, has `NRO0` magic and the b12 stamp, and hashes to `210fd95555f2b58117f812b4924fde06efa2e4e056a2c2265481291349316507` (42,893,127 bytes). b8–b11 SHA-256 hashes still match. Hardware A/B procedure: `docs/Switch2Performance.md`. No device results, SD or NAND changes, commits or tags.
