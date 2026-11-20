# P02 · Benchmarks

Every number in this project's write-up must trace back to a file in this folder. The method is in [../Explanation/01_Problem_Analysis.md §6–7](../Explanation/01_Problem_Analysis.md). The results, batch by batch, are in [../Explanation/09_Testing_And_Benchmarks.md](../Explanation/09_Testing_And_Benchmarks.md).

**State (2026-11-21):** the S2 calibration and the S3 baseline batch are done (before any SpawnPacer system code existed). The next batches come with SpawnPacer itself (S5 onwards).

## Measurement rules

1. **Baselines first.** B0–B3 are captured before SpawnPacer exists, on the same workload, build and machine as every later run.
2. **Packaged Development build only** (never PIE, never the editor). The launcher engine can't build the Test configuration, and Shipping has no CSV profiler.
3. **Fixed step** (`-deterministic -FPS=60 -novsync`), so every run simulates the same frames while CSV `FrameTime` still records each frame's real cost. The capture window is opened and closed by the game itself (`BenchStart` / `BenchEnd` CSV events), never by a timer. One **uncapped real-time** run per configuration confirms SC1.
4. **Real GPU**, windowed 1280×720, default scalability, no VSync, no sound, `-traceautostart=0`; `GameUserSettings.ini` deleted before every run.
5. **Pinned to the performance cores.** `Tools\run_bench.py` pins **itself** once with `pin_self(performance_core_mask())` (`Tools\perf_cores.py`; 0xFFFF on this machine) before it records the environment or starts anything, so every game it launches inherits the mask from its first instruction. It records `applied_mask()` (the mask read back from Windows) in the environment. It starts the real game executable (`Build\Windows\SpawnPacer\Binaries\Win64\SpawnPacer.exe`) with `subprocess.Popen`, waits with a timeout, and kills its process tree in a `finally` block.
6. **One discarded priming run per batch**, then **interleaved runs with the configuration order rotated each round**. Runs per combination = **the number of configurations** in the batch (and at least 2), so every configuration sits in every order position exactly once. Combinations no success criterion needs are skipped.
7. **Statistics:** `FrameTime` is read one row later than the other columns, because the engine writes a frame's duration into the next row (01b B-7). Per run and per analysis window (wave, despawn, GC, pre-wave; 01 §6.3), the median, p95, p99 (linear-interpolation percentiles) and max frame time, and the count of frames over 33.3 ms and over 16.7 ms. Then the median across runs with the [min–max] range; counts are also summed. An improvement is claimed only when run ranges don't overlap. FPS is never used as evidence.
8. **Memory runs are separate** (`-LLM`), because LLM adds its own cost.
9. **The environment is recorded with every batch** (below), automatically, by `Tools\run_bench.py`.
10. **Every benchmark batch is one job in the machine-wide queue** (`tools\ue_queue.py`), so no build or other project's job runs beside it.

## Folders

| Folder | What goes in it | Rule |
|---|---|---|
| `Raw\` | CSV profiler captures (`.csv`), per-run reports, game logs (`.log`), Unreal Insights traces (`.utrace`), one `_manifest.json` per batch | **Never edited.** Anything re-processed is written to `Summaries\`. Only the manifests are in git (exact command lines, environment, per-run statistics). Captures are too large for git and UE logs hold machine-specific paths, so every batch cited in the docs is attached as `<batch>_raw.zip` to this repository's **Releases** page |
| `Summaries\` | Computed statistics, SC1–SC7 pass/fail tables, charts | Each summary names the batch it was computed from |

## File naming

`<batch>_<Config>[_W<work>][_B<budget>]_r<run>.<ext>`, with `<batch>` = `YYYY-MM-DD_HHMMSS[_label]` (time-stamped, so two batches can never collide; the run script refuses to overwrite).

- `<Config>`: `B0` plain `SpawnActor` · `B1` deferred, count cap · `B2` time cap checked between spawns · `B3` simple pool · `SPS` SpawnPacer spawn path · `SPP` SpawnPacer pool path (plus `SPS-Whole` for the split ablation).
- `_W<work>`: the BeginPlay work (`-SPWork=`) of the run, e.g. `_W20000` (always written; the calibrated value is 20000).
- `_B<budget>` only in budget-sweep batches, e.g. `_B4.0`.
- Example: `2026-11-25_101500_B2_W20000_r3.csv` (+ `_report.csv` + `.log`), `2026-11-25_101500_manifest.json` in `Raw\`, and `2026-11-25_101500_summary.md` in `Summaries\`.
- `_r0_prime` files are the discarded priming run. They are kept for transparency and excluded from every summary.

## Environment record (written into every manifest and summary)

```text
Engine:            UE 5.8.4 (CL 58902052), launcher build
Build:             Development, standalone packaged (never PIE)
Compiler:          MSVC 14.44.35229 (VS 2022), Windows SDK 10.0.22621.0
CPU / GPU / RAM:   i7-14650HX (8P + 8E) / RTX 5070 Laptop (driver 610.47) / 32 GB
CPU affinity:      performance cores only: applied_mask() read back after pin_self (0xFFFF on this machine)
Storage:           project on D: (Kingston NVMe), engine on C: (WD NVMe)
OS:                Windows 11 build 26200
Power:             AC, power plan recorded per batch
Resolution:        1280x720 windowed, default scalability, no VSync, no sound
Command line:      exact, per run, in the manifest
Warm-up:           5 s (pool fill for B3/SPP)     Capture: 3 wave cycles x 12 s     Runs: = number of configs, interleaved + 1 priming run
```

## Run log

| Batch | Configs | Runs | Purpose | Summary |
|---|---|---|---|---|
| `2026-11-21_035912_baseline` | B0, B1 (K = 14), B2 (2 ms), B3; work 20000 | 4 each + priming | **The baseline matrix** SpawnPacer must beat, captured before SpawnPacer code existed (09 S3) | [summary](Summaries/2026-11-21_035912_baseline_summary.md) |
| `2026-11-21_040413_spawntimer` | B0 with `-SPSpawnTimer` | 1 | The engine's per-class spawn time (H2), in the run's log | [summary](Summaries/2026-11-21_040413_spawntimer_summary.md) |
| `2026-11-21_031105_calib` | B0, BeginPlay work 0 / 2000 / 4000 / 8000 | 2 each + priming | S2 calibration of the BeginPlay work (chosen: 20000) and the S1 checks (same wave hash in all runs, B0 hitches on every wave) | [summary](Summaries/2026-11-21_031105_calib_summary.md) |
