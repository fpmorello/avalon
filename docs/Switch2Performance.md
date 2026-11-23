# Switch 2 performance baseline

Use a game that already runs reliably on Avalon. A black-screen run is useful for startup
triage, but it is not a gameplay performance baseline.

## Collect launch logs in batches (b13 and later)

Avalon keeps the ten latest launch logs per game under
`sdmc:/switch/avalon/.local/share/avalon/log/games/`. Each selected game path has its
own folder; `run-*.log` files sort by launch order. Check the `Option` lines to tell
settings apart. The picker still uses `avalon_log.txt`, independently of game logs.
Copy the `games/` folder after several runs instead of removing the SD between runs.
An interrupted launch may leave a partial log.

## Capture a reference run

1. Launch `avalon-b10.nro` and record the game, scene, handheld/docked mode, resolution,
   GPU accuracy, JIT and shader cache settings, and CPU boost setting.
2. Use the same route or scene for about 60 seconds per run. Match the cache state across
   builds: for a warm comparison, do one unmeasured run before recording each build.
   Keep cold launches separate; do not delete saves or caches for the warm comparison.
3. Exit the game. With b13 or later, collect each run from the game's `log/games/`
   folder after the batch. With older builds, save `avalon_log.txt` before it rotates.
   Label each run with the build and settings, such as `b10-warm-01.log`. Repeat three
   times if the first run is representative.

## Summarize the logs on a computer

From the repo root, with Python 3.8 or newer:

```sh
python3 tools/switch2/perf_summary.py b10-warm-01.log b10-warm-02.log b10-warm-03.log
```

The tool reads the logs without changing them and prints one CSV row per file. It summarizes
complete `Performance:` records: median reported game FPS, median reported **mean** system
frame time, worst reported system frame time, JIT busy time and JIT memory fallbacks. It also
includes the highest logged heap sample (not the true heap peak or VRAM usage) and b10's time from
starting the guest to observing an accepted Vulkan presentation, if reported. An accepted
presentation can still contain a black image. A blank field means there was no applicable
measurement; `windows=0` is not a performance sample.

Compare like-for-like runs and change only one variable at a time. Compare similar numbers
of log windows when reading cumulative JIT fields. For the Dynarmic experiment, keep GPU
settings fixed and check speed, frame-time spikes, JIT activity and boot regressions. For GPU
tuning, keep Dynarmic fixed and also inspect the raw `Memory detail:` lines for texture and
buffer pressure. Do not promote a setting based on a single FPS reading.

## Check JIT memory pressure (b14)

The `Memory detail:` line reads `cache clears C (H for heap, E blocks evicted first,
K low-heap checks held)`. All four are totals since launch. H and E rise when the JIT
drops code because free heap is below 1536 MiB. From b14, K rises instead while a
previous drop freed nothing and the heap has not fallen a further 64 MiB.

Compare with the same scene on b13. Rising K with flat H and steady FPS means the JIT
stopped recompiling for nothing. H climbing quickly while the `Stage:` memory figure
keeps rising means the heap itself is running out, which the JIT cannot fix.

## A/B test asynchronous presentation (b12)

Use `avalon-b12.nro` for both modes. In the picker, highlight the game, press **Y**,
set **Async presentation** explicitly to `off` (A) or `on` (B) with **←/→**, and press
**B** to save. Launch the game again after each change. The default is `off`; the log
records `Option async_presentation: off` or `on` at launch.

1. Start with **Super Mario 3D World** and **Little Racer**. For each title, use the
   same update, scene or route, handheld/docked mode, resolution, GPU accuracy, CPU
   boost, and cache settings. Warm both modes before recording; do not delete saves
   or caches.
2. Record about 60 seconds of comparable gameplay per run, alternating the order
   (`off` → `on`, then `on` → `off`). Save each log before the next launch and label it
   with the game and mode.
3. Compare game FPS, emulation speed, and frame-time spikes, plus logged heap and
   graphics memory. Also note visible startup, black frames or flicker, input feel,
   and clean exit. First Vulkan acceptance is not proof of visible gameplay.

Leave the setting off if there is no repeatable benefit or if presentation or exit
regresses. This experiment does not require installing anything to NAND or changing firmware.

## Measure the host clock without running a game

`avalon-clock-probe.nro` is a standalone diagnostic. Keep it alongside Avalon on the SD;
use the usual Switch 2 homebrew menu to launch the probe by name. It shows the report and
writes `sdmc:/switch/avalon-clock-probe.txt` (replaced on each probe run). Press **+** to
exit. If the report cannot be written, take a photo of the screen instead. The probe does
not read game files or modify Avalon settings, caches, or logs.

The report compares the current `WallClock::GetCNTPCT()` with the libnx counter reading
converted to the same guest tick units. It alternates the order of paired timings and
checks for backward steps and candidate readings outside the surrounding reference
readings. A faster median is not a gameplay speedup or proof that the clocks are
interchangeable; the per-batch timings and agreement checks matter too.

To build the probe in an existing Switch 2 build directory, source the SDK environment
and run `ninja -C build/switch2 avalon-clock-probe_nro`. It emits
`build/switch2/bin/avalon-clock-probe.nro` without replacing the main NRO.
