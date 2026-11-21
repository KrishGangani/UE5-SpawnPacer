"""P02 SpawnPacer - statistics for Unreal CSV-profiler captures, cut into the analysis windows of 01 section 6.3.

Copied from UE5-UpdateBudget's Tools/csv_stats.py (percentile, summarize, load_capture, frame_events, column_values
are unchanged), then extended for this project's wave script. No tool shipped with UE 5.8 reports p95/p99.

Usage:
    python -I csv_stats.py CAPTURE.csv [CAPTURE2.csv ...] [--columns A,B,...] [--json OUT.json]
    python -I csv_stats.py --selftest

Rules (documented so every number can be reproduced):
  * A finalised Unreal capture ends with two non-data rows: the full header and a "[key],value,..." metadata row.
    Both are dropped, and the END header is used for column lookup: a stat first recorded mid-capture (for example
    Exclusive/GameThread/ActorSpawning on the first wave) exists only in the end header.
  * The EVENTS column holds "Category/Name##timestamp" entries separated by ";".
  * Percentiles use linear interpolation between closest ranks (numpy's default "linear" method):
      position = p/100 * (n - 1);  value = lower + (upper - lower) * fraction.
  * Windows (fixed step, so a window is a frame count; --fps gives frames per simulated second):
      W   wave window      from each SPBench/WaveRequested frame, --wave-window seconds (default 4.0 = 240 frames)
      D   despawn window   from each SPBench/DespawnRequested frame, --despawn-window seconds (default 2.0 = 120)
      GC  GC frames        each SPBench/GCForced frame and the --gc-frames - 1 frames after it (default 3 in all)
      PRE pre-wave         the --pre-window seconds before each WaveRequested frame (default 1.0 = 60)
    Each window type pools its frames over all waves. A window is clipped at the end of the capture, and the frames
    it actually holds are reported, so a short window can't pass unnoticed.
  * FrameTime is one row late. The engine computes it at the START of a frame, as the time since the previous frame's
    start (FCsvProfiler::UpdateFrameTime, Core/Private/ProfilingDebugging/CsvProfiler.cpp:4369-4390, "the phase
    shift"), so frame N's duration is in row N+1, while GameThreadTime and our own stats are in row N (observed in the
    S2 calibration: the wave row has SPBaseline/WorkMs 34 ms and FrameTime 3 ms, the next row FrameTime 39 ms). Every
    window therefore takes FrameTime from the rows one later than the other columns (FRAME_TIME_LAG).
  * Hitch counts: frames with FrameTime > 33.3 ms and > 16.7 ms (two 60 Hz frame intervals, one interval).
  * Wave latency: frames from each WaveRequested to the next WaveComplete (0 = the same frame).
  * Spawn-work frames (SC3): for each spawner's WorkMs column, the frames of a window that did real work
    (> ACTIVE_WORK_MS; the timer alone costs microseconds), their statistics, and how many exceeded 1.1 x the budget
    (the run's spbudgetms metadata, 2.0 ms when absent).
"""
from __future__ import annotations

import argparse
import csv
import json
import math
import os
import sys

DEFAULT_COLUMNS = (
    "FrameTime",
    "GameThreadTime",
    "RenderThreadTime",
    "GPUTime",
    "SPBaseline/WorkMs",
    "SPBaseline/ConstructMs",
    "SPBaseline/FinishMs",
    "SpawnPacer/WorkMs",
    "SpawnPacer/SpawnMs",
    "SpawnPacer/ConstructMs",
    "SpawnPacer/RegisterMs",
    "SpawnPacer/FinishMs",
    "SpawnPacer/DestroyMs",
    "Exclusive/GameThread/ActorSpawning",
    "Exclusive/GameThread/ActorDestroying",
)

FRAME_TIME_LAG = 1   # rows: FrameTime of frame N is written in row N + 1 (module docstring)
HITCH_MS = 33.3
MISSED_FRAME_MS = 16.7
BENCH = "SPBench/"
WORK_COLUMNS = ("SPBaseline/WorkMs", "SpawnPacer/WorkMs")
ACTIVE_WORK_MS = 0.01       # a frame with less spawner work than this did nothing but read the clock
BUDGET_TOLERANCE = 1.10     # SC3: work <= 1.1 x budget
FORCED_COLUMN = "SpawnPacer/ForcedJobs"   # SC3 lists frames whose first job was predicted over the budget separately

csv.field_size_limit(1 << 30)


def percentile(sorted_values: list[float], p: float) -> float:
    """Linear-interpolation percentile of an already sorted, non-empty list. p in [0, 100]."""
    if not sorted_values:
        raise ValueError("percentile of an empty list")
    if len(sorted_values) == 1:
        return sorted_values[0]
    position = (p / 100.0) * (len(sorted_values) - 1)
    lower = math.floor(position)
    upper = min(lower + 1, len(sorted_values) - 1)
    fraction = position - lower
    return sorted_values[lower] + (sorted_values[upper] - sorted_values[lower]) * fraction


def summarize(values: list[float]) -> dict:
    """Count, mean, min, median, p95, p99 and max of a list of numbers."""
    ordered = sorted(values)
    return {
        "n": len(ordered),
        "mean": sum(ordered) / len(ordered),
        "min": ordered[0],
        "median": percentile(ordered, 50.0),
        "p95": percentile(ordered, 95.0),
        "p99": percentile(ordered, 99.0),
        "max": ordered[-1],
    }


def load_capture(path: str) -> tuple[list[str], list[list[str]], dict, bool]:
    """Return (header, data rows, metadata dict, finalised) of an Unreal CSV capture."""
    with open(path, newline="", encoding="utf-8", errors="replace") as handle:
        rows = list(csv.reader(handle))
    if len(rows) < 2:
        raise ValueError(f"{path}: too few rows to be an Unreal capture")
    # With HasHeaderRowAtEnd, the END header is authoritative: columns can be added during the capture, so the
    # first header is only a prefix of it.
    first_header = rows[0]
    finalised = (len(rows) >= 3 and bool(rows[-1]) and rows[-1][0].strip().startswith("[")
                 and rows[-2][:len(first_header)] == first_header)
    if not finalised:
        return first_header, rows[1:], {}, False

    header, data, meta_row = rows[-2], rows[1:-2], rows[-1]
    # Metadata pairs: [key],value,[key],value,...  Stored without brackets, lower-case.
    metadata = {}
    for index in range(0, len(meta_row) - 1, 2):
        key = meta_row[index].strip().strip("[]").lower()
        if key:
            metadata[key] = meta_row[index + 1].strip()
    return header, data, metadata, True


def frame_events(header: list[str], row: list[str]) -> list[str]:
    """Event names ("Category/Name", timestamps removed) recorded on one frame."""
    lowered = [h.lower() for h in header]
    if "events" not in lowered:
        return []
    index = lowered.index("events")
    if index >= len(row) or not row[index].strip():
        return []
    return [entry.split("##")[0].strip() for entry in row[index].split(";") if entry.strip()]


def column_values(header: list[str], rows: list[list[str]], column: str) -> list[float]:
    """Numeric values of one column (empty cells skipped). Raises ValueError if the column is absent."""
    index = header.index(column)
    return [float(row[index]) for row in rows if index < len(row) and row[index].strip() != ""]


def cell_value(row: list[str], index: int | None) -> float:
    """The number in one cell, 0 for a missing column, a short row or an empty cell."""
    if index is None or index >= len(row) or not row[index].strip():
        return 0.0
    return float(row[index])


def events_per_frame(header: list[str], data: list[list[str]]) -> list[list[str]]:
    """The events of every frame, parsed once per capture (the analysis looks them up several times)."""
    return [frame_events(header, row) for row in data]


def event_frames(events: list[list[str]], name: str) -> list[int]:
    """Indices of the frames that carry the event SPBench/<name>, in order."""
    target = BENCH + name
    return [i for i, frame in enumerate(events) if target in frame]


def window_rows(data: list[list[str]], starts: list[int], length: int, before: bool = False,
                offset: int = 0) -> list[list[str]]:
    """The frames of every window, pooled. A window starts at each start (or ends just before it if before=True);
    offset shifts every window by that many rows (FRAME_TIME_LAG for FrameTime)."""
    rows: list[list[str]] = []
    for start in starts:
        first, last = (start - length, start) if before else (start, start + length)
        rows.extend(data[max(0, first + offset):min(len(data), last + offset)])
    return rows


def summarize_window(header: list[str], data: list[list[str]], starts: list[int], length: int,
                     columns: tuple[str, ...], before: bool = False, budget_ms: float = 2.0) -> dict:
    """Statistics of each column over a window type's frames, plus the hitch counts on FrameTime (read one row later)."""
    rows = window_rows(data, starts, length, before)
    frame_rows = window_rows(data, starts, length, before, FRAME_TIME_LAG)
    # "frames" counts the event-aligned rows; "frame_time_frames" the shifted FrameTime rows, which can be one shorter
    # where a window touches the end of the capture (the last frame's duration lands in a row that was never written).
    result: dict = {"frames": len(rows), "frame_time_frames": len(frame_rows), "columns": {}}
    for column in columns:
        if column in header:
            values = column_values(header, frame_rows if column == "FrameTime" else rows, column)
            if values:
                result["columns"][column] = summarize(values)
    limit = BUDGET_TOLERANCE * budget_ms
    for column in WORK_COLUMNS:
        if column in header:
            # Row-aligned (work, forced) pairs: SpawnPacer publishes ForcedJobs in the same frame as WorkMs.
            work_index = header.index(column)
            forced_index = header.index(FORCED_COLUMN) if column == "SpawnPacer/WorkMs" and FORCED_COLUMN in header else None
            pairs = [(cell_value(row, work_index), cell_value(row, forced_index) > 0) for row in rows]
            active = [(work, forced) for work, forced in pairs if work > ACTIVE_WORK_MS]
            if active:
                stats = summarize([work for work, _ in active])
                stats["over_budget"] = sum(1 for work, _ in active if work > limit)
                if forced_index is not None:
                    stats["forced"] = sum(1 for _, forced in active if forced)
                    stats["over_budget_unforced"] = sum(1 for work, forced in active if work > limit and not forced)
                result.setdefault("active_work", {})[column] = stats
    if "FrameTime" in header:
        frame_times = column_values(header, frame_rows, "FrameTime")
        result["over_33_3ms"] = sum(1 for v in frame_times if v > HITCH_MS)
        result["over_16_7ms"] = sum(1 for v in frame_times if v > MISSED_FRAME_MS)
    return result


def wave_latencies(events: list[list[str]]) -> list[int | None]:
    """Frames from each WaveRequested to the first WaveComplete at or after it (None if the wave never completed)."""
    requested = event_frames(events, "WaveRequested")
    complete = event_frames(events, "WaveComplete")
    latencies: list[int | None] = []
    for index, start in enumerate(requested):
        limit = requested[index + 1] if index + 1 < len(requested) else len(events)
        done = next((c for c in complete if start <= c < limit), None)
        latencies.append(None if done is None else done - start)
    return latencies


def analyse(path: str, columns: tuple[str, ...], fps: float = 60.0, wave_window: float = 4.0,
            despawn_window: float = 2.0, gc_frames: int = 3, pre_window: float = 1.0) -> dict:
    header, data, metadata, finalised = load_capture(path)
    events = events_per_frame(header, data)
    event_counts: dict[str, int] = {}
    for frame in events:
        for event in frame:
            key = event.split("/")[0] if event.startswith("PSO/") else event
            event_counts[key] = event_counts.get(key, 0) + 1

    waves = event_frames(events, "WaveRequested")
    despawns = event_frames(events, "DespawnRequested")
    gcs = event_frames(events, "GCForced")
    budget_ms = float(metadata.get("spbudgetms", 2.0) or 2.0)
    windows = {
        "W": summarize_window(header, data, waves, round(wave_window * fps), columns, budget_ms=budget_ms),
        "D": summarize_window(header, data, despawns, round(despawn_window * fps), columns, budget_ms=budget_ms),
        "GC": summarize_window(header, data, gcs, gc_frames, columns),
        "PRE": summarize_window(header, data, waves, round(pre_window * fps), columns, before=True),
    }
    return {"file": os.path.basename(path), "frames": len(data), "finalised": finalised, "metadata": metadata,
            "events": event_counts, "waves": len(waves), "wave_latency_frames": wave_latencies(events),
            "windows": windows}


def selftest() -> None:
    """Checks the statistics, parsing and windows against values worked out by hand."""
    values = [float(v) for v in range(1, 11)]                   # 1..10
    assert percentile(values, 50) == 5.5                          # position 4.5 -> 5 + 0.5*(6-5)
    assert abs(percentile(values, 95) - 9.55) < 1e-12             # position 8.55 -> 9 + 0.55*1
    assert abs(percentile(values, 99) - 9.91) < 1e-12             # position 8.91 -> 9 + 0.91*1
    assert percentile([7.0], 99) == 7.0
    s = summarize([4.0, 1.0, 3.0, 2.0])
    assert s["n"] == 4 and s["min"] == 1.0 and s["max"] == 4.0 and s["mean"] == 2.5 and s["median"] == 2.5
    header = ["EVENTS", "FrameTime"]
    assert frame_events(header, ["SPBench/WaveRequested##1.5;PSO/Encountered new graphics PSO##1.6", "3"]) == \
        ["SPBench/WaveRequested", "PSO/Encountered new graphics PSO"]
    assert frame_events(header, ["", "3"]) == []

    # Ten frames: wave requested on frame 2 (completes on frame 4), despawn on frame 6, GC on frame 8.
    events = {2: "SPBench/WaveRequested##0", 4: "SPBench/WaveComplete##0", 6: "SPBench/DespawnRequested##0",
              8: "SPBench/GCForced##0"}
    times = [5, 5, 4, 40, 20, 10, 5, 35, 5, 50]   # FrameTime one row late: frame 2's 40 ms is in row 3
    data = [[events.get(i, ""), str(t)] for i, t in enumerate(times)]
    events = events_per_frame(header, data)
    assert event_frames(events, "WaveRequested") == [2]
    assert wave_latencies(events) == [2]
    wave = summarize_window(header, data, [2], 3, ("FrameTime",))                  # FrameTime rows 3, 4, 5: 40, 20, 10
    assert wave["frames"] == 3 and wave["over_33_3ms"] == 1 and wave["over_16_7ms"] == 2
    assert wave["columns"]["FrameTime"]["max"] == 40.0 and wave["columns"]["FrameTime"]["median"] == 20.0
    pre = window_rows(data, [2], 2, before=True)                                   # frames 0, 1
    assert [row[1] for row in pre] == ["5", "5"]
    assert [row[1] for row in window_rows(data, [2], 2, before=True, offset=FRAME_TIME_LAG)] == ["5", "4"]
    clipped = window_rows(data, [8], 5)                                            # frames 8, 9 only
    assert len(clipped) == 2
    work_header = ["EVENTS", "FrameTime", "SPBaseline/WorkMs"]
    work = [[data[i][0], str(t), str(w)] for i, (t, w) in enumerate(zip(times, [0, 0, 1.0, 2.5, 0.005, 0, 0, 0, 0, 0]))]
    active = summarize_window(work_header, work, [2], 3, ("FrameTime",))["active_work"]["SPBaseline/WorkMs"]
    assert active["n"] == 2 and active["max"] == 2.5 and active["over_budget"] == 1   # 0.005 is idle; 2.5 > 2.2
    paced_header = ["EVENTS", "FrameTime", "SpawnPacer/WorkMs", "SpawnPacer/ForcedJobs"]
    paced = [[data[i][0], str(t), str(w), str(f)] for i, (t, w, f) in
             enumerate(zip(times, [0, 0, 3.0, 2.5, 1.0, 0, 0, 0, 0, 0], [0, 0, 1, 0, 0, 0, 0, 0, 0, 0]))]
    active = summarize_window(paced_header, paced, [2], 3, ("FrameTime",))["active_work"]["SpawnPacer/WorkMs"]
    assert active["over_budget"] == 2 and active["forced"] == 1 and active["over_budget_unforced"] == 1   # 3.0 forced
    print("selftest: OK (percentiles, summary, event parsing, windows, FrameTime lag, hitch counts, active work, forced frames and latency match hand-worked values)")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("captures", nargs="*")
    parser.add_argument("--columns", default=",".join(DEFAULT_COLUMNS))
    parser.add_argument("--fps", type=float, default=60.0)
    parser.add_argument("--wave-window", type=float, default=4.0)
    parser.add_argument("--despawn-window", type=float, default=2.0)
    parser.add_argument("--gc-frames", type=int, default=3)
    parser.add_argument("--pre-window", type=float, default=1.0)
    parser.add_argument("--json")
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args(argv)

    if args.selftest:
        selftest()
        return 0
    if not args.captures:
        parser.error("give at least one capture, or --selftest")

    columns = tuple(c.strip() for c in args.columns.split(",") if c.strip())
    results = [analyse(path, columns, args.fps, args.wave_window, args.despawn_window, args.gc_frames, args.pre_window)
               for path in args.captures]
    for result in results:
        flag = "" if result["finalised"] else "  [NOT FINALISED: partial capture, treat with care]"
        print(f"{result['file']}  ({result['frames']} frames, config={result['metadata'].get('spconfig', '?')}, "
              f"waves={result['waves']}, latency frames={result['wave_latency_frames']}){flag}")
        for name, window in result["windows"].items():
            print(f"  [{name}] {window['frames']} frames, >33.3 ms: {window.get('over_33_3ms', '?')}, "
                  f">16.7 ms: {window.get('over_16_7ms', '?')}")
            for column, s in window["columns"].items():
                print(f"    {column:<38} median {s['median']:9.3f}  p95 {s['p95']:9.3f}  p99 {s['p99']:9.3f}  max {s['max']:9.3f}")
    if args.json:
        with open(args.json, "w", encoding="utf-8") as handle:
            json.dump(results, handle, indent=2)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
