# Avalon b9: recoverable launch errors

Baseline: the local b8 diff and `avalon-b8.nro`. Keep the b8 binary. This unit does not add the separately explored loading animation.

## Specs

- S1: “cuando no abre sale un error y se cierra todo me devolve à la home de switch . tenemos que poder stay at esos cierres por falta de keys o algo y qué no mate todo como pasa hoy .”
- S2: “y explicate el errror”

## Tasks

- T1 (S1–S2): DONE — failure screen with useful diagnostics, complete early-failure teardown and return to picker via next-load. Inline; focused tests and targeted independent recheck passed. No commit.
- T2 (S1–S2): DONE — rebuilt `avalon-b9.nro` and inspected packaging; `avalon-b8.nro` checksum unchanged. Loader handoff verified by the user on Minecraft; general coverage remains limited to that run.

## Log

- L1: podemos agregar también que creo que lo tenes en memoria , el Mario no me acuerdo el nombre el otro . cuando no abre sale un error y se cierra todo me devolve à la home de switch . tenemos que poder stay at esos cierres por falta de keys o algo y qué no mate todo como pasa hoy . y explicate el errror
- L2: The reported game and status are unknown. Confirmed the source's forced exit after a returned load error. Limit recovery to known early failures; no guarantee for hard crashes or hangs.
- L3: Independent verification rejected same-process retry (kernel TLS and fiber lifetime), so recovery now schedules a fresh launch without the game argument after a normal teardown and acknowledgement. If scheduling fails, an explicit close screen explains the fallback. Missing `main.npdm` regression RED (exit 134), GREEN in the C++23 status test. Renderer checks passed with ASan/TSan; status tests passed with ASan/UBSan. Independent targeted recheck cleared all blockers; no device run.
- L4: `eden-horizon_nro` exited 0; `avalon-b9.nro` matches the build output, `NRO0` and b9 stamp checked. Size 42,884,935 bytes, SHA-256 `f9da0694d20cf3d189cecd80fa86839ac2c619d519501a3ba0aef09ddbf662c0`. Test a rejected title, A to reopen the list, then a known-good title on the console.
- L5: User reports Minecraft now shows a missing update and returns to the menu. Wonder still shows a black screen; its log is pending. Booting Minecraft with an update was not verified.
