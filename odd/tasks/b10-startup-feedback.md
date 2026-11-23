# Avalon b10: startup feedback

Baseline: local b9, keep its NRO unchanged. This build advances startup UX first; Dynarmic and GPU tuning remain measurement-gated follow-ups.

## Specs

- S1: “Cuando un juego se esta abriendo, suele demorar unos segundos mas/menos con la pantalla en negro. Tenemos que informar ahi que se esta haciendo, alguna animacion de loading o algo, o mostrar el progreso de carga de lo que se este cargando.”
- S2: “Me paso con el Super Mario Wonder, que se quedo en negro (no funciono) pero no tenia feedback de si estaba cargando o habia muerto.”
- S3: “Dale, mientras pruebo vos segui avanzando con el plan.”

## Tasks

- T1 (S1–S2): DONE — startup card, window handoff, and real shader count. Host renderer tests and independent targeted recheck passed.
- T2 (S1–S3): DONE — first accepted guest presentation, cold/warm/disabled path verification, independent review, and inspected b10 NRO. No commit. Console acceptance and Wonder diagnosis require device evidence.

## Log

- L1: Dale, mientras pruebo vos segui avanzando con el plan.
- L2: te paso update de la b9 probe 2 juegos . el wonder sigue con la pantalla en negro . después te paso el log , el Minecraft ya no rompe mas y me dice que falta un update pudiendo volver al menu .
- L3: User verified b9 recoverable error and menu return on Minecraft; actual install/boot with update and Wonder diagnosis remain open. b10 does not diagnose a hang automatically. `OnFrameDisplayed` is composition, not accepted presentation; qualify the actual Vulkan present result and do not describe it as proof of physical scanout.
- L4: Renderer and startup-retry tests RED before implementation; GREEN under ASan/TSan (renderer) and ASan/UBSan (retry). Independent recheck passed async/sync acceptance, discard, retry, bounded failure and descriptor completion. The software card is closed before Vulkan owns the window. Startup submission uses `Finish` before descriptor reuse, filters synthetic frames from the guest signal, and starts the guest if both waiting cards were rejected.
- L5: Corrected write `avalon-b10.nro` matches build output, NRO0/b10 stamp checked. Size 42,893,127 bytes, SHA-256 `7864d048f23b16e862015a61e9d85b5f7b989f5547eb360e4319704cea0f26b3`. b9 checksum unchanged. Next: console check of handoff, wait screen, and Wonder log; measure baseline before changing Dynarmic/GPU settings.
