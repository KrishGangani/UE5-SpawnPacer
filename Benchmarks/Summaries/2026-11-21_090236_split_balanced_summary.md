# Benchmark batch 2026-11-21_090236_split_balanced

## Environment

```json
{
  "date": "2026-11-21T09:02:36",
  "engine": "5.8.4 (CL 58902052)",
  "build_config": "Development (packaged)",
  "compiler": "MSVC 14.44.35229, Windows SDK 10.0.22621.0",
  "cpu": "Intel(R) Core(TM) i7-14650HX",
  "gpus": "Intel(R) UHD Graphics (driver 32.0.101.7088); NVIDIA GeForce RTX 5070 Laptop GPU (driver 32.0.16.1047)",
  "ram_gb": "31.6",
  "os": "Microsoft Windows 11 Home Single Language build 26200",
  "power_plan": "Power Scheme GUID: 6fecc5ae-f350-48a5-b669-b472cb895ccf  (Turbo)",
  "on_ac_power": true,
  "resolution": "1280x720 windowed, default scalability",
  "common_args": "-windowed -ResX=1280 -ResY=720 -nosound -unattended -traceautostart=0 -deterministic -FPS=60 -novsync -SPExit -csvStartOnEvent=BenchStart -csvStopOnEvent=BenchEnd",
  "cpu_affinity": "0xFFFF (performance cores only; hybrid CPU)"
}
```

## Results per analysis window (01_Problem_Analysis.md 6.3)

FrameTime cells: median across runs of each run's statistic, then [min-max across runs], in ms. Hitch columns: frames over 33.3 ms, summed over runs, and the largest count in one run.

| Config | Work | Runs | Window | Frames/run | FrameTime median | p99 | max | >33.3 ms (sum / worst run) | Spawn frames/run | Spawn work p99 (ms) | Spawn work max (ms) | Spawn frames > 1.1 x budget (sum / worst run) | SC3: of them unforced / forced frames (sum) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| SPS | 20000 | 6 | W | 720 | 3.94 [3.86-4.72] | 5.71 [5.52-7.01] | 6.47 [6.25-7.60] | 0 / 0 | 166 | 2.06 [2.01-2.11] | 2.22 [2.08-2.33] | 3 / 1 | 3 / 0 |
| SPS | 20000 | 6 | D | 360 | 3.20 [3.10-3.76] | 6.53 [6.06-8.27] | 7.77 [7.34-9.45] | 0 / 0 | 18 | 2.04 [2.03-2.08] | 2.04 [2.03-2.09] | 0 / 0 | 0 / 0 |
| SPS | 20000 | 6 | GC | 9 | 6.55 [5.55-8.27] | 8.86 [8.50-12.77] | 8.91 [8.53-13.02] | 0 / 0 | 0 | n/a | n/a | n/a | n/a |
| SPS | 20000 | 6 | PRE | 120 | 3.19 [3.07-3.34] | 4.12 [3.94-4.50] | 7.15 [6.76-8.23] | 0 / 0 | 1 | 0.04 [0.03-0.05] | 0.04 [0.03-0.05] | 0 / 0 | 0 / 0 |
| SPS | 20000 | 6 | latency | | wave latency, frames (per-run median): 54.00 [50.00-63.00] | | | | | | | | |
| SPS1ms | 20000 | 6 | W | 720 | 3.89 [3.67-3.96] | 5.34 [4.89-5.52] | 5.83 [5.43-6.85] | 0 / 0 | 458 | 1.08 [1.02-1.11] | 1.41 [1.17-1.47] | 25 / 5 | 25 / 8 |
| SPS1ms | 20000 | 6 | D | 360 | 3.21 [3.12-3.38] | 5.56 [5.12-5.98] | 6.16 [5.79-7.07] | 0 / 0 | 34 | 1.03 [1.01-1.16] | 1.03 [1.01-1.18] | 2 / 2 | 2 / 0 |
| SPS1ms | 20000 | 6 | GC | 9 | 6.86 [6.20-7.37] | 8.69 [7.86-9.83] | 8.73 [7.88-9.96] | 0 / 0 | 0 | n/a | n/a | n/a | n/a |
| SPS1ms | 20000 | 6 | PRE | 120 | 3.07 [3.00-3.32] | 4.06 [3.70-4.66] | 7.51 [7.01-8.92] | 0 / 0 | 1 | 0.13 [0.13-0.13] | 0.13 [0.13-0.13] | 0 / 0 | 0 / 0 |
| SPS1ms | 20000 | 6 | latency | | wave latency, frames (per-run median): 153.00 [144.00-162.00] | | | | | | | | |
| SPSBossDeferred | 20000 | 6 | W | 720 | 4.00 [3.83-5.50] | 5.89 [5.61-7.57] | 6.56 [6.31-8.73] | 0 / 0 | 166 | 2.00 [1.98-2.07] | 2.04 [1.99-2.60] | 1 / 1 | 1 / 4 |
| SPSBossDeferred | 20000 | 6 | D | 360 | 3.17 [3.09-3.81] | 6.46 [6.38-7.88] | 8.54 [6.99-8.82] | 0 / 0 | 17 | 2.04 [2.03-2.04] | 2.04 [2.03-2.05] | 0 / 0 | 0 / 0 |
| SPSBossDeferred | 20000 | 6 | GC | 9 | 6.66 [5.83-7.28] | 8.36 [7.50-12.30] | 8.37 [7.51-12.46] | 0 / 0 | 0 | n/a | n/a | n/a | n/a |
| SPSBossDeferred | 20000 | 6 | PRE | 120 | 3.11 [3.02-3.81] | 4.16 [3.84-5.18] | 7.81 [7.50-10.29] | 0 / 0 | 1 | 0.02 [0.02-0.03] | 0.02 [0.02-0.03] | 0 / 0 | 0 / 0 |
| SPSBossDeferred | 20000 | 6 | latency | | wave latency, frames (per-run median): 54.50 [51.00-101.00] | | | | | | | | |
| SPSBossDeferred1ms | 20000 | 6 | W | 720 | 3.83 [3.62-3.94] | 5.30 [4.75-5.41] | 5.94 [5.38-10.77] | 0 / 0 | 459 | 1.03 [0.97-1.11] | 1.32 [1.16-1.45] | 16 / 5 | 16 / 9 |
| SPSBossDeferred1ms | 20000 | 6 | D | 360 | 3.15 [3.07-3.37] | 5.37 [5.14-5.73] | 6.32 [5.57-6.94] | 0 / 0 | 32 | 1.02 [1.02-1.07] | 1.02 [1.02-1.08] | 0 / 0 | 0 / 0 |
| SPSBossDeferred1ms | 20000 | 6 | GC | 9 | 6.08 [5.49-6.56] | 8.47 [7.80-9.22] | 8.48 [7.81-9.32] | 0 / 0 | 0 | n/a | n/a | n/a | n/a |
| SPSBossDeferred1ms | 20000 | 6 | PRE | 120 | 3.17 [3.01-3.33] | 4.19 [3.79-4.23] | 7.86 [6.59-8.83] | 0 / 0 | 0 | n/a | n/a | n/a | n/a |
| SPSBossDeferred1ms | 20000 | 6 | latency | | wave latency, frames (per-run median): 152.00 [142.00-157.00] | | | | | | | | |
| SPSBossWhole | 20000 | 6 | W | 720 | 3.94 [3.83-6.46] | 5.82 [5.62-9.02] | 6.55 [6.34-11.63] | 0 / 0 | 167 | 2.06 [2.01-2.17] | 2.37 [2.29-2.78] | 9 / 3 | 9 / 7 |
| SPSBossWhole | 20000 | 6 | D | 360 | 3.11 [3.06-4.93] | 6.50 [6.40-9.63] | 8.03 [7.48-10.64] | 0 / 0 | 18 | 2.06 [2.03-2.09] | 2.07 [2.03-2.10] | 0 / 0 | 0 / 0 |
| SPSBossWhole | 20000 | 6 | GC | 9 | 5.68 [4.68-12.33] | 8.63 [7.72-14.42] | 8.74 [7.75-14.43] | 0 / 0 | 0 | n/a | n/a | n/a | n/a |
| SPSBossWhole | 20000 | 6 | PRE | 120 | 3.10 [3.02-4.74] | 3.92 [3.86-6.20] | 7.53 [7.07-12.92] | 0 / 0 | 0 | n/a | n/a | n/a | n/a |
| SPSBossWhole | 20000 | 6 | latency | | wave latency, frames (per-run median): 54.00 [49.00-128.00] | | | | | | | | |
| SPSBossWhole1ms | 20000 | 6 | W | 720 | 3.78 [3.64-4.40] | 5.09 [4.89-6.87] | 5.61 [5.45-8.09] | 0 / 0 | 446 | 1.08 [1.00-1.13] | 1.43 [1.23-1.78] | 24 / 8 | 12 / 81 |
| SPSBossWhole1ms | 20000 | 6 | D | 360 | 3.19 [3.11-3.89] | 5.33 [5.16-6.61] | 6.25 [5.53-7.39] | 0 / 0 | 32 | 1.03 [1.02-1.05] | 1.04 [1.02-1.06] | 0 / 0 | 0 / 0 |
| SPSBossWhole1ms | 20000 | 6 | GC | 9 | 6.25 [5.77-7.80] | 8.63 [8.09-9.39] | 8.65 [8.11-9.40] | 0 / 0 | 0 | n/a | n/a | n/a | n/a |
| SPSBossWhole1ms | 20000 | 6 | PRE | 120 | 3.16 [3.03-3.43] | 4.09 [3.82-4.63] | 7.61 [6.88-8.47] | 0 / 0 | 2 | 0.16 [0.16-0.16] | 0.16 [0.16-0.16] | 0 / 0 | 0 / 0 |
| SPSBossWhole1ms | 20000 | 6 | latency | | wave latency, frames (per-run median): 144.00 [139.00-217.00] | | | | | | | | |

Excluded runs (non-zero exit, timeout, bad capture or log, wrong wave count, or a wave that never completed; their files stay in Raw): none.

Raw files: `Benchmarks/Raw/2026-11-21_090236_split_balanced_*` (CSV, run report and log per run) and `2026-11-21_090236_split_balanced_manifest.json`.
