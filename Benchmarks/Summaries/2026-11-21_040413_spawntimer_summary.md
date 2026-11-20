# Benchmark batch 2026-11-21_040413_spawntimer

## Environment

```json
{
  "date": "2026-11-21T04:04:13",
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

| Config | Work | Runs | Window | Frames/run | FrameTime median | p99 | max | >33.3 ms (sum / worst run) | Spawn work max (ms) |
|---|---|---|---|---|---|---|---|---|---|
| B0 | 20000 | 1 | W | 720 | 4.04 [4.04-4.04] | 6.16 [6.16-6.16] | 85.42 [85.42-85.42] | 3 / 3 | 80.71 [80.71-80.71] |
| B0 | 20000 | 1 | D | 360 | 3.06 [3.06-3.06] | 9.34 [9.34-9.34] | 11.35 [11.35-11.35] | 0 / 0 | 7.68 [7.68-7.68] |
| B0 | 20000 | 1 | GC | 9 | 5.84 [5.84-5.84] | 7.84 [7.84-7.84] | 7.85 [7.85-7.85] | 0 / 0 | 0.00 [0.00-0.00] |
| B0 | 20000 | 1 | PRE | 120 | 3.04 [3.04-3.04] | 3.88 [3.88-3.88] | 7.13 [7.13-7.13] | 0 / 0 | 0.00 [0.00-0.00] |
| B0 | 20000 | 1 | latency | | wave latency, frames (per-run median): 0.00 [0.00-0.00] | | | | |

Excluded runs (non-zero exit, timeout, bad capture or log, wrong wave count, or a wave that never completed; their files stay in Raw): none.

Raw files: `Benchmarks/Raw/2026-11-21_040413_spawntimer_*` (CSV, run report and log per run) and `2026-11-21_040413_spawntimer_manifest.json`.
