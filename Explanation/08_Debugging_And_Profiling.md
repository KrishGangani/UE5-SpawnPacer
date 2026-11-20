# 08 · Debugging And Profiling: SpawnPacer

**Status:** harness part written in step S2; the rest is filled in when the system exists (S5 onwards) and in Phase 8.

## Reading a capture (S2)

- **Run one analysis:** `python -I Tools\csv_stats.py Benchmarks\Raw\<run>.csv` prints, per window (W, D, GC, PRE), the frame count, frames over 33.3 ms and over 16.7 ms, and median / p95 / p99 / max of each column; `--json OUT.json` keeps everything.
- **`FrameTime` is one row late.** A frame's duration is written into the next row (01b B-7). Looking at a spike by hand: find the event row (`SPBench/WaveRequested`), read `SPBaseline/WorkMs` and `GameThreadTime` there, and `FrameTime` in the row after it.
- **Columns that first appear mid-capture** (the engine's `Exclusive/GameThread/ActorSpawning` on the first wave) are only in the end header; `csv_stats.py` reads that one.
- **Self-description:** the last row's metadata holds `spconfig`, `spwavesize`, `spwork`, `spwavehash` and the full command line, so a capture can be identified without its file name.

**Will contain:** how to see the system working, with exact commands for UE 5.8.4:
- CSV profiler captures started and stopped by the game's events, and the columns that matter (`FrameTime`, `SpawnPacer/*`, `Exclusive/GameThread/ActorSpawning`, `Exclusive/GameThread/ActorDestroying`; 01b §4);
- the engine's `spawnactortimer start` / `stop` command as a cross-check of per-class cost;
- an Unreal Insights capture of one wave, with the engine's per-phase scopes (`STAT_PostActorConstruction`, `STAT_ActorBeginPlay`, `STAT_RegisterComponent`, `STAT_ComponentCreateRenderState`, `STAT_ComponentCreatePhysicsState`);
- `-LLM` memory runs;
- the `sp.*` console variables and the `sp.Debug` overlay;
- how to read a reset-verifier report.
