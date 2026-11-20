# P02 · Benchmarks

Every number in this project's write-up must trace back to a file in this folder. The method is in [../Explanation/01_Problem_Analysis.md §6–7](../Explanation/01_Problem_Analysis.md). The results, batch by batch, are in [../Explanation/09_Testing_And_Benchmarks.md](../Explanation/09_Testing_And_Benchmarks.md).

**State (2026-11-21):** no batch has been run yet. The first one is the calibration batch in step S2; the baseline batch (B0–B3) follows in S3, before any SpawnPacer system code exists.

## Measurement rules

1. **Baselines first.** B0–B3 are captured before SpawnPacer exists, on the same workload, build and machine as every later run.
2. **Packaged Development build only** (never PIE, never the editor). The launcher engine can't build the Test configuration, and Shipping has no CSV profiler.
3. **Fixed step** (`-deterministic -FPS=60 -novsync`), so every run simulates the same frames while CSV `FrameTime` still records each frame's real cost. The capture window is opened and closed by the game itself (`BenchStart` / `BenchEnd` CSV events), never by a timer. One **uncapped real-time** run per configuration confirms SC1.
4. **Real GPU**, windowed 1280×720, default scalability, no VSync, no sound, `-traceautostart=0`; `GameUserSettings.ini` deleted before every run.
5. **Pinned to the performance cores.** `Tools\run_bench.py` pins **itself** once with `pin_self(performance_core_mask())` (`Tools\perf_cores.py`; 0xFFFF on this machine) before it records the environment or starts anything, so every game it launches inherits the mask from its first instruction. It records `applied_mask()` (the mask read back from Windows) in the environment. It starts the real game executable (`Build\Windows\SpawnPacer\Binaries\Win64\SpawnPacer.exe`) with `subprocess.Popen`, waits with a timeout, and kills its process tree in a `finally` block.
6. **One discarded priming run per batch**, then **interleaved runs with the configuration order rotated each round**. Runs per combination = **the number of configurations** in the batch, so every configuration sits in every order position exactly once. Combinations no success criterion needs are skipped.
7. **Statistics:** per run and per analysis window (wave, despawn, GC, pre-wave; 01 §6.3), the median, p95, p99 (linear-interpolation percentiles) and max frame time, and the count of frames over 33.3 ms and over 16.7 ms. Then the median across runs with the [min–max] range; counts are also summed. An improvement is claimed only when run ranges don't overlap. FPS is never used as evidence.
8. **Memory runs are separate** (`-LLM`), because LLM adds its own cost.
9. **The environment is recorded with every batch** (below), automatically, by `Tools\run_bench.py`.
10. **Every benchmark batch is one job in the machine-wide queue** (`tools\ue_queue.py`), so no build or other project's job runs beside it.

## Folders

| Folder | What goes in it | Rule |
|---|---|---|
| `Raw\` | CSV profiler captures (`.csv`), per-run reports, game logs (`.log`), Unreal Insights traces (`.utrace`), one `_manifest.json` per batch | **Never edited.** Anything re-processed is written to `Summaries\`. Only the manifests are in git (exact command lines, environment, per-run statistics). Captures are too large for git and UE logs hold machine-specific paths, so every batch cited in the docs is attached as `<batch>_raw.zip` to this repository's **Releases** page |
| `Summaries\` | Computed statistics, SC1–SC7 pass/fail tables, charts | Each summary names the batch it was computed from |

## File naming

`<batch>_<Config>[_B<budget>]_r<run>.<ext>`, with `<batch>` = `YYYY-MM-DD_HHMMSS[_label]` (time-stamped, so two batches can never collide; the run script refuses to overwrite).

- `<Config>`: `B0` plain `SpawnActor` · `B1` deferred, count cap · `B2` time cap checked between spawns · `B3` simple pool · `SPS` SpawnPacer spawn path · `SPP` SpawnPacer pool path (plus `SPS-Whole` for the split ablation).
- `_B<budget>` only in budget-sweep batches, e.g. `_B4.0`.
- Example: `2026-11-25_101500_B2_r3.csv` (+ `_report.csv` + `.log`), `2026-11-25_101500_manifest.json` in `Raw\`, and `2026-11-25_101500_summary.md` in `Summaries\`.
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
| *(none yet)* | | | The first batch is the S2 calibration | |
