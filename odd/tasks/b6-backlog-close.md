# Build b6: close our open backlog

Objective: the small items our reviews left open, plus the load-failure half of the
return-to-list story. Branch `feat/b6-backlog-close` off `feat/b5-cover-art`.

## Specs

- S1: verifier/b2 advisories — "riscv64/loongarch64 don't reject option", "A32 fallback tag
  weak", "writable 2 MiB zero chunk", "arm_dynarmic_32.cpp doesn't apply the per-core sizing".
- S2: user, b4 discussion — "Si podemos entender por que pasa eso y volver directamente al
  menu del emulador": a game that fails to load also comes back to the game list.
- S3: build `b6`, CHANGELOG.

## Tasks

- T1 (S1+S2+S3, inline): the five changes, host suites, NRO build. DONE: all host suites
  green; `ninja dynarmic common core` clean; load-failure path relaunches when the option is
  on and still ends the process through ExitProcess (threads already started).

## Log

- L1 (user): "Segui con el plan de mejoras NUESTRO y despues con el del analisis que hiciste
  del fork. No preguntes..." — autonomous run; no interactive permission anywhere (no sudo,
  pushes use the cached gh token).
