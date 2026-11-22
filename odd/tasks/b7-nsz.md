# Build b7: solid NSZ/XCZ (adopted from eden-nx2)

Objective: first adoption item of odd/analysis-eden-nx2.md. Branch `feat/b7-nsz` off
`feat/b6-backlog-close`.

## Specs

- S1 (analysis): "NSZ/XCZ support — port their ncz_decompressor.cpp changes (GPL, same
  upstream; attribute in the commit)."
- S2: build `b7`, CHANGELOG.

## Tasks

- T1 (S1): port. DONE: our ncz_decompressor.cpp, registered_cache.cpp and vfs_real.{cpp,h}
  were byte-identical to their base (d85bad8924), so their files carry over whole: bounded
  pinned+LRU chunk cache with a 16 MiB keep-tail for seeks, pre-decrypted sections,
  HasDecryptedSections, scan-time solid-NSZ rejection removed, cached-file close on
  recreate/move/delete under __SWITCH2__. Picker lists .nsz/.xcz; the loader already mapped
  NSZ/XCZ to the NSP path from the base.
- T2 (S2): version b7, CHANGELOG, NRO build. DONE below.

## Log

- L1 (user): "Segui con el plan de mejoras NUESTRO y despues con el del analisis..."
- L2: no host test exists for the decompressor (needs zstd fixtures inside a single-TU
  harness); verification is the Switch 2 build plus reading, and the user's device run with
  a real .nsz. Not adopted from them: NUP container, launcher, updater, telemetry.
