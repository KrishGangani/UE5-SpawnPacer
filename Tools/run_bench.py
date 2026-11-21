"""P02 SpawnPacer - run a benchmark batch on the packaged build and collect every artefact.

Copied from UE5-UpdateBudget's Tools/run_bench.py and adapted to this project's wave script (01 section 6-7).

Usage (from anywhere; through tools/ue_queue.py on the development machine, never directly):
    python -I -u run_bench.py --configs B0 [--wave-size 500] [--work 2000,4000] [--runs N] [--waves 3]
                              [--warmup 5] [--cycle 12] [--life 6] [--gc-after 2] [--seed 1] [--extra "-LLM"]
                              [--label NAME] [--cooldown 5] [--no-prime] [--dry-run]

What one batch does:
  1. Pins THIS process to the performance cores (perf_cores.pin_self), before anything is recorded or started, so
     every game it starts inherits the mask from its first instruction. Refuses to measure if pinning fails.
  2. Records the environment once (CPU, GPU + driver, OS build, power plan, AC power, engine, the applied mask).
  3. Runs one PRIMING run first (discarded), so file, driver and PSO caches are warm for every measured run.
  4. Runs every (config x work) combination --runs times, INTERLEAVED: the config order rotates every round. The
     default --runs is the number of configs, so every config sits in every order position exactly once.
  5. Before each run, deletes the build's GameUserSettings.ini (it can override VSync / frame-rate caps).
  6. Starts the REAL game exe (Build/Windows/SpawnPacer/Binaries/Win64), waits with a timeout, and kills its process
     tree in a finally block. After each run, moves the CSV capture, the run report and the run's log into
     Benchmarks/Raw as <batch>_<Config>[_W<work>]_r<run>.<ext>; existing files are never overwritten. The manifest is
     rewritten after every run, so a crash loses nothing.
  7. Writes Benchmarks/Summaries/<batch>_summary.md: per combination and analysis window (W, D, GC, PRE), the median
     across runs of each run's statistic [min-max across runs], frames over 33.3 ms, and wave latency.

Every run uses: fixed step (-deterministic -FPS=60), real GPU at 1280x720 windowed, no VSync, no sound,
-traceautostart=0, and a capture window started/stopped by the game's BenchStart/BenchEnd events.
"""
from __future__ import annotations

import argparse
import datetime as dt
import glob
import json
import os
import shutil
import statistics
import subprocess
import sys
import time

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, TOOLS_DIR)
import csv_stats  # noqa: E402  (same folder)
from perf_cores import KERNEL32, applied_mask, performance_core_mask, pin_self  # noqa: E402  (same folder)

PROJECT_DIR = os.path.dirname(TOOLS_DIR)
BUILD_DIR = os.path.join(PROJECT_DIR, "Build", "Windows")
EXE = os.path.join(BUILD_DIR, "SpawnPacer", "Binaries", "Win64", "SpawnPacer.exe")   # the real game, not the bootstrap
SAVED = os.path.join(BUILD_DIR, "SpawnPacer", "Saved")
RAW_DIR = os.path.join(PROJECT_DIR, "Benchmarks", "Raw")
SUMMARY_DIR = os.path.join(PROJECT_DIR, "Benchmarks", "Summaries")
ENGINE_BUILD_VERSION = r"C:\Program Files\Epic Games\UE_5.8\Engine\Build\Build.version"
STARTUP_MARGIN_SECONDS = 240.0  # engine start, warm caches and shutdown (a first run once took 27 s to exit, 09 F-4a)

COMMON_ARGS = ("-windowed -ResX=1280 -ResY=720 -nosound -unattended -traceautostart=0 "
               "-deterministic -FPS=60 -novsync -SPExit -csvStartOnEvent=BenchStart -csvStopOnEvent=BenchEnd")

# Extra command-line arguments per configuration (02_System_Architecture.md section 9). Each configuration is added in
# the step that implements it: B1-B3 in S3, SPS in S5, SPP in S7.
CONFIGS = {
    "B0": "-SPConfig=B0",                    # plain SpawnActor for the whole wave in one frame; Destroy likewise
    # B1, the deferred pattern with a count cap: K = budget / mean per-actor cost. The S2 calibration extrapolates the
    # 500-actor wave at work 20000 to about 70 ms, 0.14 ms per actor, so K = floor(2.0 / 0.14) = 14 (09 S3).
    "B1": "-SPConfig=B1 -SPCountCap=14",
    "B2": "-SPConfig=B2 -SPBudgetMs=2.0",     # time cap checked between items, at SpawnPacer's budget (02 section 7)
    "B3": "-SPConfig=B3",                    # simple pool, filled during the warm-up
    # SpawnPacer, spawn path, same budget as B2 (S5). Since S6 the boss is PerComponent here; the S5 batch
    # 2026-11-21_061433_sps_first ran it Deferred, which is now SPSBossDeferred.
    "SPS": "-SPConfig=SPS -SPBudgetMs=2.0",
    # The split ablation (S6): the boss per component (SPS), deferred and whole, at 2 ms and at 1 ms.
    "SPSBossDeferred": "-SPConfig=SPS -SPBudgetMs=2.0 -SPBossSplit=Deferred",
    "SPSBossWhole": "-SPConfig=SPS -SPBudgetMs=2.0 -SPBossSplit=Whole",
    "SPS1ms": "-SPConfig=SPS -SPBudgetMs=1.0",
    "SPSBossDeferred1ms": "-SPConfig=SPS -SPBudgetMs=1.0 -SPBossSplit=Deferred",
    "SPSBossWhole1ms": "-SPConfig=SPS -SPBudgetMs=1.0 -SPBossSplit=Whole",
    # SpawnPacer, pool path (S7): pre-warmed during the warm-up, acquired at the wave, parked at the despawn.
    "SPP": "-SPConfig=SPP -SPBudgetMs=2.0",
}

SUMMARY_WINDOWS = ("W", "D", "GC", "PRE")
PACER_CONFIGS = ("SPS", "SPSBossDeferred", "SPSBossWhole", "SPS1ms", "SPSBossDeferred1ms", "SPSBossWhole1ms", "SPP")


def work_column(config: str) -> str:
    """The column holding this configuration's own spawn work (never another spawner's, even if one published)."""
    return "SpawnPacer/WorkMs" if config in PACER_CONFIGS else "SPBaseline/WorkMs"


def powershell(command: str) -> str:
    result = subprocess.run(["powershell", "-NoProfile", "-Command", command], capture_output=True, text=True)
    return result.stdout.strip()


def environment_record() -> dict:
    """Facts that must accompany every result (Benchmarks/README.md, environment record)."""
    with open(ENGINE_BUILD_VERSION, encoding="utf-8") as handle:
        build = json.load(handle)
    return {
        "date": dt.datetime.now().isoformat(timespec="seconds"),
        "engine": f"{build['MajorVersion']}.{build['MinorVersion']}.{build['PatchVersion']} (CL {build['Changelist']})",
        "build_config": "Development (packaged)",
        "compiler": "MSVC 14.44.35229, Windows SDK 10.0.22621.0",
        "cpu": powershell("(Get-CimInstance Win32_Processor).Name"),
        "gpus": powershell("(Get-CimInstance Win32_VideoController | ForEach-Object { $_.Name + ' (driver ' + $_.DriverVersion + ')' }) -join '; '"),
        "ram_gb": powershell("[math]::Round((Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory/1GB,1)"),
        "os": powershell("$o=Get-CimInstance Win32_OperatingSystem; $o.Caption + ' build ' + $o.BuildNumber"),
        "power_plan": powershell("powercfg /getactivescheme"),
        # PowerOnline is true on mains power whether the battery is charging or full (Win32_Battery's BatteryStatus
        # is 2 only for "AC, not charging", so it would call a charging laptop "on battery").
        "on_ac_power": powershell("(Get-CimInstance -Namespace root/wmi -ClassName BatteryStatus | Select-Object -First 1).PowerOnline") == "True",
        "resolution": "1280x720 windowed, default scalability",
        "common_args": COMMON_ARGS,
        "cpu_affinity": applied_mask(),
    }


def unique_path(path: str) -> str:
    """Raw data is never overwritten (Benchmarks/README.md)."""
    if os.path.exists(path):
        raise FileExistsError(f"refusing to overwrite raw data: {path}")
    return path


def run_seconds(args) -> float:
    return args.warmup + args.waves * args.cycle


def analysis_windows(args) -> dict:
    """Window lengths (01 6.3) clipped to this batch's script timings, so no window reaches into the next event:
    W ends by the despawn, D by the GC, and PRE starts after the previous cycle's GC frames."""
    gc_frames = 3
    return {
        "fps": 60.0,
        "wave_window": min(4.0, args.life),
        "despawn_window": min(2.0, args.gc_after),
        "gc_frames": gc_frames,
        "pre_window": max(0.0, min(1.0, args.cycle - args.life - args.gc_after - gc_frames / 60.0)),
    }


def run_and_reap(command: str, timeout: float) -> int | None:
    """Runs one command line and waits for it; returns its exit code, or None on a timeout. Whatever happens (timeout,
    Ctrl+C or any other error), a process still running is killed with its tree and waited for, so nothing runs into the
    next job or holds its files. Shared with run_tests.py."""
    process = subprocess.Popen(command)   # inherits this process's CPU affinity (run_bench main: pin_self)
    try:
        return process.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        return None
    finally:
        if process.poll() is None:
            subprocess.run(["taskkill", "/F", "/T", "/PID", str(process.pid)], capture_output=True)
            try:
                process.wait(timeout=30)   # let Windows release the process's files before they are moved or read
            except subprocess.TimeoutExpired:
                pass


def launch(name: str, config: str, work: int | None, args) -> tuple[str, int | None, float, float]:
    """Starts one run and waits for it. Returns (command, exit code or None on timeout, start time, wall seconds)."""
    sp_args = (f"-SPWaveSize={args.wave_size} -SPWaves={args.waves} -SPSeed={args.seed} -SPWarmup={args.warmup} "
               f"-SPCycle={args.cycle} -SPLife={args.life} -SPGCAfter={args.gc_after} -SPRunTag={name}")
    if work is not None:
        sp_args += f" -SPWork={work}"
    # One string, so -ExecCmds="..." and similar reach Unreal exactly as written.
    command = f'"{EXE}" {COMMON_ARGS} {sp_args} {CONFIGS[config]} {args.extra}'.strip()
    if args.dry_run:
        print("DRY:", command)
        return command, 0, time.time(), 0.0

    gus = os.path.join(SAVED, "Config", "Windows", "GameUserSettings.ini")
    if os.path.exists(gus):
        os.remove(gus)
    started = time.time()
    exit_code = run_and_reap(command, run_seconds(args) + STARTUP_MARGIN_SECONDS)
    return command, exit_code, started, time.time() - started


def collect(name: str, started: float, windows: dict) -> dict:
    """Moves this run's artefacts into Benchmarks/Raw and returns what was found. A file that can't be moved (still
    locked after a killed run) is recorded as an error instead of stopping the batch."""
    try:
        return collect_files(name, started, windows)
    except OSError as error:
        return {"collect_error": str(error)}


def collect_files(name: str, started: float, windows: dict) -> dict:
    record: dict = {}
    os.makedirs(RAW_DIR, exist_ok=True)

    csv_files = [p for p in glob.glob(os.path.join(SAVED, "Profiling", "CSV", "*.csv")) if os.path.getmtime(p) >= started]
    if csv_files:
        destination = unique_path(os.path.join(RAW_DIR, name + ".csv"))
        shutil.move(max(csv_files, key=os.path.getmtime), destination)
        record["capture"] = os.path.basename(destination)
        try:
            analysis = csv_stats.analyse(destination, csv_stats.DEFAULT_COLUMNS, **windows)
            for key in ("finalised", "events", "waves", "wave_latency_frames", "windows", "metadata"):
                record[key] = analysis[key]
        except (ValueError, IndexError) as error:
            record["analysis_error"] = str(error)

    report = os.path.join(SAVED, "SPRuns", name + ".csv")
    if os.path.exists(report):
        shutil.move(report, unique_path(os.path.join(RAW_DIR, name + "_report.csv")))
        record["run_report"] = name + "_report.csv"

    log = os.path.join(SAVED, "Logs", "SpawnPacer.log")
    if os.path.exists(log) and os.path.getmtime(log) >= started:   # only this run's log, never a stale one
        with open(log, encoding="utf-8", errors="replace") as handle:
            text = handle.read()
        record["log_errors"] = text.count("Error:")
        record["log_warnings"] = text.count("Warning:")
        shutil.copy(log, unique_path(os.path.join(RAW_DIR, name + ".log")))
    else:
        record["log_missing"] = True
    return record


def is_failure(record: dict, expected_waves: int) -> bool:
    """A run is excluded from the summary if anything about it is off: exit code, timeout, capture, log, wave count,
    or a wave that never completed."""
    return (record.get("exit_code") != 0 or "windows" not in record or record.get("log_errors")
            or record.get("log_missing") or record.get("finalised") is False or "analysis_error" in record
            or "collect_error" in record or record.get("waves") != expected_waves
            or None in record.get("wave_latency_frames", [None]))


def write_manifest(batch: str, environment: dict, args, records: list[dict]) -> None:
    os.makedirs(RAW_DIR, exist_ok=True)
    with open(os.path.join(RAW_DIR, batch + "_manifest.json"), "w", encoding="utf-8") as handle:
        json.dump({"environment": environment, "arguments": vars(args), "analysis_windows": analysis_windows(args),
                   "runs": records}, handle, indent=2)


def across_runs(values: list[float]) -> str:
    """Median across runs, then the [min-max] across runs (Benchmarks/README.md, statistics)."""
    return f"{statistics.median(values):.2f} [{min(values):.2f}-{max(values):.2f}]" if values else "n/a"


def write_summary(batch: str, environment: dict, records: list[dict], expected_waves: int) -> str:
    os.makedirs(SUMMARY_DIR, exist_ok=True)
    groups: dict[tuple, list[dict]] = {}
    excluded = [r["name"] for r in records if not r.get("priming") and is_failure(r, expected_waves)]
    for record in records:
        if not record.get("priming") and record["name"] not in excluded:
            groups.setdefault((record["config"], record["work"]), []).append(record)

    lines = [f"# Benchmark batch {batch}", "", "## Environment", "", "```json", json.dumps(environment, indent=2), "```", "",
             "## Results per analysis window (01_Problem_Analysis.md 6.3)", "",
             "FrameTime cells: median across runs of each run's statistic, then [min-max across runs], in ms. "
             "Hitch columns: frames over 33.3 ms, summed over runs, and the largest count in one run.", ""]
    header = ("| Config | Work | Runs | Window | Frames/run | FrameTime median | p99 | max | >33.3 ms (sum / worst run) | "
              "Spawn frames/run | Spawn work p99 (ms) | Spawn work max (ms) | Spawn frames > 1.1 x budget (sum / worst run) | "
              "SC3: of them unforced / forced frames (sum) |")
    lines += [header, "|" + "---|" * (header.count("|") - 1)]
    for (config, work), group in sorted(groups.items(), key=lambda kv: (kv[0][0], str(kv[0][1]))):
        for window in SUMMARY_WINDOWS:
            stats = [g["windows"][window] for g in group]
            frame = [s["columns"].get("FrameTime") for s in stats]
            frame = [f for f in frame if f]
            active = [s.get("active_work", {}).get(work_column(config)) for s in stats]
            active = [a for a in active if a]
            hitches = [s.get("over_33_3ms", 0) for s in stats]
            over = [a["over_budget"] for a in active]
            spawn_cells = (f"{statistics.median([a['n'] for a in active]):.0f} | {across_runs([a['p99'] for a in active])} | "
                           f"{across_runs([a['max'] for a in active])} | {sum(over)} / {max(over)}") if active else "0 | n/a | n/a | n/a"
            # SpawnPacer only (baselines have no prediction, so nothing is "forced"): SC3 counts forced frames apart.
            paced = [a for a in active if "forced" in a]
            spawn_cells += (f" | {sum(a['over_budget_unforced'] for a in paced)} / {sum(a['forced'] for a in paced)}"
                            if paced else " | n/a")
            lines.append(f"| {config} | {work if work is not None else 'default'} | {len(group)} | {window} | "
                         f"{statistics.median([s['frames'] for s in stats]):.0f} | "
                         f"{across_runs([f['median'] for f in frame])} | {across_runs([f['p99'] for f in frame])} | "
                         f"{across_runs([f['max'] for f in frame])} | {sum(hitches)} / {max(hitches)} | {spawn_cells} |")
        # Per run: the median latency of its waves; then median and [min-max] across runs, like every other cell.
        per_run = [statistics.median(g["wave_latency_frames"]) for g in group if g.get("wave_latency_frames")]
        lines.append(f"| {config} | {work if work is not None else 'default'} | {len(group)} | latency | | "
                     f"wave latency, frames (per-run median): {across_runs([float(v) for v in per_run])} | | | | | | | | |")
    lines += ["", "Excluded runs (non-zero exit, timeout, bad capture or log, wrong wave count, or a wave that never "
              f"completed; their files stay in Raw): {', '.join(excluded) if excluded else 'none'}.",
              "", f"Raw files: `Benchmarks/Raw/{batch}_*` (CSV, run report and log per run) and `{batch}_manifest.json`."]
    path = os.path.join(SUMMARY_DIR, batch + "_summary.md")
    with open(path, "w", encoding="utf-8") as handle:
        handle.write("\n".join(lines) + "\n")
    return path


def resummarize(batch: str) -> int:
    """Rebuilds a batch's summary from its manifest and raw CSVs with the current analysis code."""
    with open(os.path.join(RAW_DIR, batch + "_manifest.json"), encoding="utf-8") as handle:
        manifest = json.load(handle)
    saved = manifest["arguments"]
    windows = manifest.get("analysis_windows") or analysis_windows(argparse.Namespace(**saved))
    records = []
    for record in manifest["runs"]:
        if "capture" in record:
            analysis = csv_stats.analyse(os.path.join(RAW_DIR, record["capture"]), csv_stats.DEFAULT_COLUMNS, **windows)
            record = dict(record, **{k: analysis[k] for k in ("finalised", "waves", "wave_latency_frames", "windows")})
        records.append(record)
    path = write_summary(batch, manifest["environment"], records, saved["waves"])
    with open(path, encoding="utf-8") as handle:
        text = handle.read()
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(text.replace("## Results per analysis window", "Rebuilt with `run_bench.py --resummarize` from the raw "
                                  "CSVs and the unchanged manifest.\n\n## Results per analysis window", 1))
    print("summary:", path)
    return 0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--configs", default="B0")
    parser.add_argument("--wave-size", type=int, default=500)
    # 20000: the S2 calibration (09 S2-c) - the plain-SpawnActor wave frame then lands above 66.7 ms (01 6.1).
    parser.add_argument("--work", default="20000", help="comma list of BeginPlay work iterations (sp.Demo.BeginPlayWork)")
    parser.add_argument("--runs", type=int, default=0,
                        help="runs per combination; 0 (default) = the number of configs, and at least 2")
    parser.add_argument("--waves", type=int, default=3)
    parser.add_argument("--warmup", type=float, default=5.0)
    parser.add_argument("--cycle", type=float, default=12.0)
    parser.add_argument("--life", type=float, default=6.0)
    parser.add_argument("--gc-after", type=float, default=2.0)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--extra", default="", help="extra Unreal arguments for every run, e.g. -LLM")
    parser.add_argument("--cooldown", type=float, default=5.0)
    parser.add_argument("--label", default="")
    parser.add_argument("--no-prime", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--resummarize", metavar="BATCH",
                        help="rebuild BATCH's summary from its manifest and raw CSVs with the current analysis (no runs; "
                             "the manifest is not changed)")
    args = parser.parse_args(argv)
    if args.resummarize:
        return resummarize(args.resummarize)

    configs = [c.strip() for c in args.configs.split(",") if c.strip()]
    unknown = [c for c in configs if c not in CONFIGS]
    if unknown:
        parser.error(f"unknown config(s) {unknown}; available: {sorted(CONFIGS)}")
    if args.runs <= 0:
        # The protocol: runs = number of configs, so the rotation puts every config in every position once; and never
        # a single sample (a median and a range need at least two runs).
        args.runs = max(2, len(configs))
    elif args.runs % len(configs):
        print(f"WARNING: --runs {args.runs} is not a multiple of {len(configs)} configs; the rotation is unbalanced")
    works = [int(w) for w in args.work.split(",") if w.strip()] or [None]
    if not os.path.exists(EXE) and not args.dry_run:
        parser.error(f"packaged build not found: {EXE} (run Tools\\package.bat first)")

    if not args.dry_run and KERNEL32 is not None:
        # Keep the display and the system awake while the batch runs (an occluded or locked desktop can throttle a
        # windowed game). Applies only while this process lives; changes no power settings
        # (SetThreadExecutionState: ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED).
        KERNEL32.SetThreadExecutionState(0x80000000 | 0x00000001 | 0x00000002)
        # Pin once, before anything is recorded or started (perf_cores.py).
        mask = performance_core_mask()
        if mask and not pin_self(mask):
            parser.error(f"could not pin the batch to the performance cores (0x{mask:X}); refusing to measure")

    batch = dt.datetime.now().strftime("%Y-%m-%d_%H%M%S") + (f"_{args.label}" if args.label else "")
    environment = environment_record() if not args.dry_run else {}
    records: list[dict] = []

    def run(config: str, work: int | None, run_index: int, priming: bool = False) -> None:
        name = f"{batch}_{config}" + (f"_W{work}" if work is not None else "") + f"_r{run_index}" + ("_prime" if priming else "")
        command, exit_code, started, wall = launch(name, config, work, args)
        record = {"name": name, "config": config, "work": work, "run": run_index, "priming": priming,
                  "exit_code": exit_code, "wall_seconds": round(wall, 1), "command": command}
        if args.dry_run:
            return
        record.update(collect(name, started, analysis_windows(args)))
        records.append(record)
        write_manifest(batch, environment, args, records)
        wave = record.get("windows", {}).get("W", {})
        frame = wave.get("columns", {}).get("FrameTime", {})
        print(f"  {name}: exit {exit_code}, {wall:.0f} s, waves {record.get('waves')}, W max {frame.get('max', float('nan')):.2f} ms, "
              f">33.3 ms {wave.get('over_33_3ms')}, latency {record.get('wave_latency_frames')}, log errors "
              f"{record.get('log_errors')}" + ("  [PRIMING, discarded]" if priming else ""), flush=True)
        time.sleep(args.cooldown)

    if not args.no_prime:
        run(configs[0], works[0], 0, priming=True)

    for run_index in range(1, args.runs + 1):
        rotation = (run_index - 1) % len(configs)
        round_configs = configs[rotation:] + configs[:rotation]       # interleave: rotate the order every round
        for work in works:
            for config in round_configs:
                run(config, work, run_index)

    if args.dry_run:
        return 0
    print("summary:", write_summary(batch, environment, records, args.waves))
    failures = [r["name"] for r in records if not r.get("priming") and is_failure(r, args.waves)]
    if failures:
        print("ATTENTION - runs with a non-zero exit, timeout, no/partial capture, missing log, log errors or missing waves:", failures)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
