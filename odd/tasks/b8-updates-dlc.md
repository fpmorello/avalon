# Avalon b8: local game updates and DLC

Baseline: `sw2-fork-b7` (`8f50ddebc7`). Follow the local adoption plan in `odd/analysis-eden-nx2.md`. Loading-screen feedback is a separate, read-only investigation.

## Specs

- S1: “Retomá Avalon desde b7 y seguí con actualizaciones y DLC”
- S2 (roadmap): “**Updates & DLC** — their discovery + picker logic, adapted to our UI.”

## Tasks

- T1 (S1–S2): DONE — local discovery, selection and launch integration. Inline; host checks and targeted independent recheck passed. No commit.
- T2 (S1–S2): DONE — documented SD layout and built and inspected `avalon-b8.nro`. On-console acceptance of the feature is unverified.

## Log

- L1: Retomá Avalon desde b7 y seguí con actualizaciones y DLC
- L2: Keep original packages read-only; use presence, type and title metadata rather than the filename to associate packages. No push or commit.
- L3: Host selection test: RED (new API absent); GREEN with ASan/UBSan. The initial NRO compiled, but independent verification blocked it on non-replacing SD rename, VFS ownership, pinning semantics, metadata identity and options visibility. Corrections applied; renderer repro RED (row 19's value changed no pixels), GREEN with ASan/TSan after viewport fix.
- L4: Separate read-only startup investigation found the existing `Stage:` log, the discarded shader progress callback, and the framebuffer-to-Vulkan handoff. `OnFrameDisplayed()` in Vulkan marks composition, not confirmed presentation. No device evidence for the reported Wonder run.
- L5: All five blockers passed independent targeted recheck (synthetic packages/providers, non-replacing rename and failure recovery, framebuffer, static lifetime). Corrected `eden-horizon_nro` exit 0; `avalon-b8.nro` matches the build output, `NRO0` and b8 stamp verified. Size 42,868,551 bytes, SHA-256 `f92fe9e266df284367d9eb681c71ee0dd31ea0266d60abcef5abd1436da96e75`. No SD copy, no tag or publication. Try base only, selected update, and multiple selected DLC on Switch 2.
