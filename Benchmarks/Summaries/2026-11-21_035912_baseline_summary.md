# Benchmark batch 2026-11-21_035912_baseline

## Environment

```json
{
  "date": "2026-11-21T03:59:12",
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

Rebuilt with `run_bench.py --resummarize` from the raw CSVs and the unchanged manifest.

## Results per analysis window (01_Problem_Analysis.md 6.3)

FrameTime cells: median across runs of each run's statistic, then [min-max across runs], in ms. Hitch columns: frames over 33.3 ms, summed over runs, and the largest count in one run.

| Config | Work | Runs | Window | Frames/run | FrameTime median | p99 | max | >33.3 ms (sum / worst run) | Spawn frames/run | Spawn work p99 (ms) | Spawn work max (ms) | Spawn frames > 1.1 x budget (sum / worst run) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| B0 | 20000 | 4 | W | 720 | 3.99 [3.77-4.36] | 5.76 [5.38-6.58] | 84.17 [75.59-97.56] | 12 / 3 | 3 | 79.52 [71.62-90.82] | 79.61 [71.63-90.84] | 12 / 3 |
| B0 | 20000 | 4 | D | 360 | 3.26 [3.11-3.29] | 9.23 [8.79-10.07] | 12.91 [11.30-15.51] | 0 / 0 | 3 | 8.86 [7.46-11.15] | 8.88 [7.46-11.22] | 12 / 3 |
| B0 | 20000 | 4 | GC | 9 | 7.19 [5.44-8.68] | 8.98 [8.15-10.61] | 9.01 [8.18-10.66] | 0 / 0 | 0 | n/a | n/a | n/a |
| B0 | 20000 | 4 | PRE | 120 | 3.08 [3.00-3.21] | 4.29 [3.70-4.90] | 7.87 [7.08-9.03] | 0 / 0 | 0 | n/a | n/a | n/a |
| B0 | 20000 | 4 | latency | | wave latency, frames (per-run median): 0.00 [0.00-0.00] | | | | | | | |
| B1 | 20000 | 4 | W | 720 | 4.15 [4.04-4.36] | 7.72 [7.06-8.42] | 8.81 [7.95-11.33] | 0 / 0 | 108 | 4.40 [3.83-4.85] | 4.63 [4.35-5.71] | 320 / 86 |
| B1 | 20000 | 4 | D | 360 | 3.33 [3.07-3.42] | 5.10 [4.45-5.84] | 5.48 [5.02-6.33] | 0 / 0 | 108 | 0.42 [0.41-0.56] | 0.58 [0.44-0.81] | 0 / 0 |
| B1 | 20000 | 4 | GC | 9 | 6.61 [5.51-7.20] | 8.90 [8.64-9.53] | 8.95 [8.70-9.56] | 0 / 0 | 0 | n/a | n/a | n/a |
| B1 | 20000 | 4 | PRE | 120 | 3.11 [2.77-3.30] | 4.44 [4.15-5.06] | 7.66 [7.06-8.21] | 0 / 0 | 0 | n/a | n/a | n/a |
| B1 | 20000 | 4 | latency | | wave latency, frames (per-run median): 35.00 [35.00-35.00] | | | | | | | |
| B2 | 20000 | 4 | W | 720 | 4.12 [4.04-4.32] | 6.42 [5.98-6.73] | 7.57 [6.40-8.07] | 0 / 0 | 132 | 2.60 [2.51-2.69] | 2.75 [2.64-2.86] | 197 / 57 |
| B2 | 20000 | 4 | D | 360 | 3.10 [2.94-3.25] | 6.80 [6.25-7.36] | 8.07 [7.49-8.94] | 0 / 0 | 13 | 2.03 [2.02-2.05] | 2.03 [2.02-2.05] | 0 / 0 |
| B2 | 20000 | 4 | GC | 9 | 6.43 [5.83-6.91] | 8.88 [8.23-9.60] | 8.93 [8.25-9.79] | 0 / 0 | 0 | n/a | n/a | n/a |
| B2 | 20000 | 4 | PRE | 120 | 3.08 [2.94-3.33] | 3.96 [3.84-4.76] | 7.04 [6.90-8.34] | 0 / 0 | 0 | n/a | n/a | n/a |
| B2 | 20000 | 4 | latency | | wave latency, frames (per-run median): 42.00 [41.00-43.00] | | | | | | | |
| B3 | 20000 | 4 | W | 720 | 4.10 [4.05-4.16] | 6.28 [6.10-6.72] | 17.37 [16.53-17.84] | 0 / 0 | 3 | 10.87 [9.81-11.38] | 10.92 [9.85-11.43] | 12 / 3 |
| B3 | 20000 | 4 | D | 360 | 3.12 [2.99-3.16] | 5.97 [5.82-6.26] | 13.12 [12.38-13.51] | 0 / 0 | 3 | 7.40 [7.18-8.08] | 7.41 [7.19-8.09] | 12 / 3 |
| B3 | 20000 | 4 | GC | 9 | 5.07 [4.15-5.75] | 8.79 [7.77-12.70] | 8.91 [7.97-12.86] | 0 / 0 | 0 | n/a | n/a | n/a |
| B3 | 20000 | 4 | PRE | 120 | 3.10 [2.97-3.48] | 4.17 [3.75-5.21] | 7.00 [6.83-9.60] | 0 / 0 | 0 | n/a | n/a | n/a |
| B3 | 20000 | 4 | latency | | wave latency, frames (per-run median): 0.00 [0.00-0.00] | | | | | | | |

Excluded runs (non-zero exit, timeout, bad capture or log, wrong wave count, or a wave that never completed; their files stay in Raw): none.

Raw files: `Benchmarks/Raw/2026-11-21_035912_baseline_*` (CSV, run report and log per run) and `2026-11-21_035912_baseline_manifest.json`.
