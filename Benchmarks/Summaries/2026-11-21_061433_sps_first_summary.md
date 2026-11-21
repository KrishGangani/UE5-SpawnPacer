# Benchmark batch 2026-11-21_061433_sps_first

## Environment

```json
{
  "date": "2026-11-21T06:14:33",
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
| SPS | 20000 | 1 | W | 720 | 3.75 [3.75-3.75] | 5.88 [5.88-5.88] | 6.59 [6.59-6.59] | 0 / 0 | 152 | 2.18 [2.18-2.18] | 2.35 [2.35-2.35] | 2 / 2 | 2 / 0 |
| SPS | 20000 | 1 | D | 360 | 2.82 [2.82-2.82] | 6.28 [6.28-6.28] | 7.27 [7.27-7.27] | 0 / 0 | 14 | 2.03 [2.03-2.03] | 2.03 [2.03-2.03] | 0 / 0 | 0 / 0 |
| SPS | 20000 | 1 | GC | 9 | 4.96 [4.96-4.96] | 7.90 [7.90-7.90] | 7.92 [7.92-7.92] | 0 / 0 | 0 | n/a | n/a | n/a | n/a |
| SPS | 20000 | 1 | PRE | 120 | 2.81 [2.81-2.81] | 3.51 [3.51-3.51] | 8.90 [8.90-8.90] | 0 / 0 | 0 | n/a | n/a | n/a | n/a |
| SPS | 20000 | 1 | latency | | wave latency, frames (per-run median): 47.00 [47.00-47.00] | | | | | | | | |

Excluded runs (non-zero exit, timeout, bad capture or log, wrong wave count, or a wave that never completed; their files stay in Raw): none.

Raw files: `Benchmarks/Raw/2026-11-21_061433_sps_first_*` (CSV, run report and log per run) and `2026-11-21_061433_sps_first_manifest.json`.
