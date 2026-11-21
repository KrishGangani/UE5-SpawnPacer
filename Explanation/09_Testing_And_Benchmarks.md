# 09 · Testing And Benchmarks: SpawnPacer

**Rule:** every check is recorded with its date, its exact command and its result, **including failures**. The full functional and benchmark sections are filled in during Phases 5–8. Results from earlier phases are recorded as they happen.

**Every Unreal job below ran through the machine-wide queue** (`tools\ue_queue.py run --project P02 …`), never directly. The queue's job id is given so the full log can be found on the development machine.

---

## Phase 4 · Foundation checks (2026-11-21)

**Job `20261121_005146_3cea9c`** ("P02 Phase 4: build editor + game (Development), package Development, packaged smoke run"), one chained command, exit 0, 271.4 s:
`Tools\build.bat SpawnPacerEditor Development && Tools\build.bat SpawnPacer Development && Tools\package.bat Development && Tools\smoke_game.bat packaged`

| # | Check | Command (from `P02_SpawnPacer\`) | Result |
|---|---|---|---|
| F-1 | Editor target builds | `Tools\build.bat` (`SpawnPacerEditor Win64 Development`) | ✅ `Result: Succeeded` in 50.3 s; 12 actions (shared PCH, both module `.cpp`, two DLLs); **0 errors, 0 warnings**. Toolchain: MSVC 14.44.35229, Windows SDK 10.0.22621.0 |
| F-2 | Game target builds | `Tools\build.bat SpawnPacer Development` | ✅ `Result: Succeeded` in 38.3 s; 24 actions, linking one `SpawnPacer.exe`; **0 errors, 0 warnings** |
| F-3 | Package a **Development** build | `Tools\package.bat` | ✅ `BUILD SUCCESSFUL`, `BuildCookRun time: 144.70 s`; cook: 516 packages cooked (7 skipped by platform), **`Success - 0 error(s), 0 warning(s)`**; three `UnrealPak` runs exit 0. Output `Build\Windows\` (≈ 983 MB, git-ignored) |
| F-4a | Packaged game on the real GPU (first run) | `Tools\smoke_game.bat packaged` (then the bootstrap `Build\Windows\SpawnPacer.exe -windowed -ResX=1280 -ResY=720 -nosound -unattended -traceautostart=0 -ExecCmds="quit" -log -abslog=…`) | ✅ exit 0. `SpawnPacerCore` mounted; D3D12, SM6; **chosen adapter 0 = NVIDIA GeForce RTX 5070 Laptop GPU** (driver 610.47), not the integrated Intel GPU; `Template_Default` loaded in 0.144 s; clean `RequestExit(0)`. **0 errors, ⚠ 1 warning:** `LogEnhancedInput: Warning: UEnhancedInputLocalPlayerSubsystem … does not have a valid PlayerInput object. Failed to load user settings.` |
| F-4b | The same after the fix: `DefaultInput.ini` with the template's two input-class keys (05 A7), and the smoke script switched to the real game exe (05 A8) | Job `20261121_011548_19bb05`: `Tools\package.bat Development && Tools\smoke_game.bat packaged` (`Build\Windows\SpawnPacer\Binaries\Win64\SpawnPacer.exe -windowed -ResX=1280 -ResY=720 -nosound -unattended -traceautostart=0 -ExecCmds="quit" -log -abslog=…`) | ✅ job exit 0 in 70.1 s. Package: `BUILD SUCCESSFUL`, `BuildCookRun time: 64.45 s`, 516 packages cooked, cook **`Success - 0 error(s), 0 warning(s)`**. Game: exit 0; base directory `Build/Windows/SpawnPacer/Binaries/Win64/` (the real exe); `SpawnPacerCore` mounted; adapter 0 = RTX 5070 Laptop; `Template_Default` loaded in 0.087 s; clean `RequestExit(0)`. **0 errors, 0 warnings** (`: Warning:` and `: Error:` counts in the game log are both 0) |
| F-5 | Remove the engine-written `SecurityToken`: the `AndroidFileServer` plugin disabled in the `.uproject` and its block deleted from `DefaultEngine.ini` (05 A1, A7) | Job `20261121_014927_d6d7e7`: `Tools\build.bat SpawnPacerEditor Development && Tools\build.bat SpawnPacer Development && Tools\package.bat Development && Tools\smoke_game.bat packaged` | ✅ job exit 0 in 104.4 s. Editor build `Result: Succeeded` (4 actions, 10.9 s), game build `Result: Succeeded` (20.4 s), **0 errors, 0 warnings**. Package: `BUILD SUCCESSFUL` (66.3 s), 516 packages cooked, cook **`Success - 0 error(s), 0 warning(s)`**. **After the cook (which runs the editor), `DefaultEngine.ini` is unchanged: 0 `AndroidFileServer` or `SecurityToken` lines**, so the block is no longer written. Smoke run: exit 0, adapter 0, map in 0.092 s, **0 warnings, 0 errors** |

**What F-4a taught us:**
1. **A warning in a run is a defect, even when the build is clean.** The cause was read in the plugin source (`EnhancedInputSubsystems.cpp:115–119`): Enhanced Input is enabled by default, but without a `DefaultPlayerInputClass` setting the engine creates the classic player input. The fix is the two lines Epic's template uses, not a copy of the template's whole input file.
2. **The real exe, not the bootstrap.** The first run used `Build\Windows\SpawnPacer.exe`, a small launcher that starts the real game. For a smoke run that is harmless, but the benchmark launcher must see the game's own exit code, so both now use `Build\Windows\SpawnPacer\Binaries\Win64\SpawnPacer.exe`.

**Observations recorded for later steps (not defects):**
- **PSO creation on the first frame:** `LogPSOHitching: Encountered 50 PSO creation hitches so far (4 graphics, 46 compute). 0 of them were precached.` and `Waited for PSO creation for 100.000000ms`, both on frame 0 at startup, before any gameplay. F-4b, the second run of the same build, logged one PSO precache miss instead. This cold/warm difference is the evidence behind H6 (cold first spawn) and why every batch has a discarded priming run and a warm-up. The benchmark CSV records the `PSO/*` columns (seen in UE5-UpdateBudget captures) so first-use PSO cost during a wave is visible.
- **Slow shutdown, first run only:** in F-4a, 27 s passed between `Slate User Destroyed` and `LogExit: Preparing to exit`; in F-4b the same gap was 5 ms. The cause was not investigated **[unverified]**. F-4a was the first run of a fresh build on this machine, and it also had 50 PSO creation hitches where F-4b had one PSO precache miss (`Encountered a new graphics PSO … (PSOPrecacheState: Missed)`). So first-run work that a warm driver cache avoids is the likely explanation **[inferred]**. It doesn't affect measurements (captures end before exit), but the benchmark launcher's per-run timeout must allow for a slow first run, and the priming run absorbs it.
- **Engine-written config:** the first cook's editor run appended an `AndroidFileServerRuntimeSettings` block, with a random `SecurityToken`, to `DefaultEngine.ini`. It was first left as written, then removed in F-5 by disabling the plugin (05 A1, A7).

**CPU-pinning helper:** `python -I Tools\perf_cores.py` prints `0xFFFF` (P-cores = logical CPUs 0–15) on this machine, 2026-11-21. The copied `KERNEL32` setup, `performance_core_mask`, `pin_self` and `applied_mask` are identical to UE5-UpdateBudget's current `run_bench.py` (`diff` empty). A one-off check in a throwaway Python process: `applied_mask()` read `0xFFFFFF (all logical processors)`, `pin_self(0xFFFF)` returned `True`, and `applied_mask()` then read `0xFFFF (performance cores only; hybrid CPU)`. Builds and smoke runs are not pinned; timed runs will be (01 §7).


---

## Step S1 · Workload and baseline B0 (2026-11-21)

All runs: packaged Development build, real GPU (RTX 5070 Laptop, adapter 0), 1280×720 windowed, fixed step `-deterministic -FPS=60`, P-core pinned (`applied_mask` 0xFFFF), AC power, power plan "Turbo". These are **checks, not results**.

| # | Check | Result |
|---|---|---|
| S1-a | **Build** (jobs `20261121_024900_6928b5` and, after the exit-code fix, `20261121_025813_920cda`) | ✅ Editor and game Development targets `Result: Succeeded`, the four new `.cpp` files compiled, **0 errors, 0 warnings**; package `BUILD SUCCESSFUL`, cook `Success - 0 error(s), 0 warning(s)` (519 packages, 3 more than in Phase 4, after `/Engine/BasicShapes` was added to the cook) |
| S1-b | **Determinism:** the same seed gives the same wave | ✅ All 9 runs of batch `2026-11-21_031105_calib` (seed 1) logged `wave hash 0x29FD2F1C` |
| S1-c | **A run with invalid settings quits at once with a failure code** (`-SPWaveSize=10 -SPConfig=B1 -SPExit`, B1 not implemented in S1) | ❌ then ✅. First run: the error was logged and the game quit after 2.5 s, but with **exit code 0**. Cause: a clean `RequestExitWithStatus` on Windows only posts a quit message and the process returns the engine's own error level (05 B4). Fixed (`QuitRun` forces the exit for a non-zero code); re-run: **exit code 2 after 1.5 s** |
| S1-d | **B0 behaves as designed** | ✅ In every run: 3 waves; the run report shows `ready` = 500 and latency 0 frames for every wave (the whole wave in its request frame); every wave and despawn event present; `DespawnComplete` on the despawn frame; 0 log errors and 0 log warnings; 2 PSO events per run |
| S1-e | **The B0 wave hitches** | ✅ Every wave frame is over 33.3 ms (3 of 3 per run). How much over depends on the BeginPlay work: the S2 calibration below |

The S1 checks above ran inside the S2 calibration job, using the S2 harness (`run_bench.py`) before it was committed: one job instead of two in a busy machine queue.

**Audit trail for S1:**
- `code-review` (medium): 8 findings, 1 important + 7 nits, **all fixed**:
  - (important) timing validation accepted a negative `-SPGCAfter`;
  - "ready" was sent from the brain's BeginPlay, before the actor's own BeginPlay finished;
  - a refused run with `-SPExit` idled until its timeout;
  - an incomplete wave reported `ready=0`;
  - bosses took the rounding remainder (10% of a 10-actor wave);
  - the grid was off-centre when the last row is partly filled;
  - `AActor` / `UWorld` were not forward-declared in `SPBaselineSpawner.h`;
  - the run tag was not sanitised, and `-SPExit` would have closed the editor in PIE.
- `simplify`: done as a structured self-review on the four angles (reuse, simplification, efficiency, altitude) instead of four review agents, because the project brief allows no sub-agents. Applied:
  - (efficiency) part names built once instead of `FString::Printf` in every spawned actor's constructor, which was measured cost a real class doesn't have;
  - (simplification) two completion booleans replaced by the wave record's own fields, and the spawn counter derived from `Live`;
  - (altitude) one `QuitRun` decides whether to quit.
  - Reuse: nothing to reuse yet; the engine's `Algo::RandomShuffle` uses the global random stream, so the seeded Fisher–Yates stays.
- Found by the first run: the exit-code behaviour (S1-c).


---

## Step S2 · Harness and calibration (2026-11-21)

Batch **`2026-11-21_031105_calib`** (job `20261121_025813_920cda`): B0 only, BeginPlay work 0 / 2000 / 4000 / 8000 iterations, 2 runs each + 1 priming run, wave size 500, 3 waves per run. Same build and environment as S1 (the manifest records `cpu_affinity: 0xFFFF (performance cores only; hybrid CPU)`, AC power, power plan Turbo). A run takes about 12 s of wall time: fixed step runs as fast as the frames allow, so 36 simulated seconds of capture at about 4 ms per frame pass quickly.

| # | Check | Result |
|---|---|---|
| S2-a | **Tool self-test** (`python -I csv_stats.py --selftest`) | ✅ percentiles 5.5 / 9.55 / 9.91 on 1…10; event parsing; windows; the FrameTime lag; hitch counts; latency |
| S2-b | **Where a frame's time is written** | ⚠ finding: on every B0 wave row, `SPBaseline/WorkMs` ≈ 34–55 ms and `GameThreadTime` ≈ 40–59 ms, but `FrameTime` ≈ 2–11 ms; the **next** row's `FrameTime` holds the spike. The engine computes `FrameTime` at the start of the next frame (01b B-7). `csv_stats.py` now reads `FrameTime` one row later (`FRAME_TIME_LAG`); the summary below was recomputed from the raw CSVs with that rule (the manifest is unchanged) |
| S2-c | **Calibration of the BeginPlay work** | Table below |
| S2-d | **Despawn and GC** (H5) | B0's destroy of 500 actors costs about 6–9 ms of spawner work (D-window max 11–14 ms frame time), and the forced full GC frame about 7–10 ms. **No frame over 33.3 ms in any D or GC window.** At this workload, H5 ("destroy and GC hitch like spawning") is not supported for B0; the S3 baseline batch measures it properly |
| S2-e | **Run-to-run spread of a single spike frame** | Large: the same setting gives wave-frame maxima from 41 to 61 ms (work 2000). One frame's cost depends on cache and scheduler state, so hitch metrics need several waves and runs, which the protocol already has (3 waves × 6 runs in the main batch) |

**S2-c: B0 wave frame against BeginPlay work** (median across 2 runs [min–max]; 100 enemies per wave do the work):

| Work (iterations) | Wave window FrameTime max (ms) | Spawner work in the wave frame, max (ms) | Pre-wave median (ms) | Frames > 33.3 ms per run |
|---|---|---|---|---|
| 0 | 43.11 [40.50–45.72] | 38.90 [36.44–41.36] | 2.91 | 3 (one per wave) |
| 2000 | 51.03 [41.30–60.76] | 46.25 [37.74–54.76] | 2.92 | 3 |
| 4000 | 49.17 [43.82–54.53] | 44.93 [40.39–49.48] | 2.87 | 3 |
| 8000 | 56.40 [53.73–59.07] | 52.27 [49.77–54.77] | 2.92 | 3 |

**Decision: 20000 iterations.**
- Across the 24 wave frames, the spawner's work grows about **1.7 ms per 1,000 iterations** for the wave's 100 enemies: about 17 ns per iteration per enemy (mean spawner work over the six wave frames of each setting: 36.5 ms at 0, 49.8 ms at 8000).
- The 01 §6.1 target is a plain-`SpawnActor` wave frame of at least 66.7 ms. Extrapolating linearly, 20000 iterations give about 36.5 + 1.66 × 20 ≈ 70 ms of spawn work, so the frame lands above the target with some margin. Each enemy's BeginPlay then does about 0.33 ms of set-up work. That is heavy, but plausible for an enemy that builds its AI and loads its data, and it is synthetic and stated as such (02 §13).
- The steady state stays far below the 12 ms ceiling: pre-wave median 2.9 ms, wave-window median about 3.7 ms with 500 live actors. So the 2 ms budget fits easily.
- `run_bench.py` now passes `-SPWork=20000` by default. **The extrapolation is checked, not assumed:** the S3 baseline batch reports B0's wave-frame maximum at 20000, and if it is below 66.7 ms the value is raised before any SpawnPacer code exists.

**Audit trail for S2:**
- The tools are copies of UE5-UpdateBudget's, adapted. S2-b was found by reading the calibration CSVs row by row before trusting the summary. The summary's spawn-work column was changed from p99 to max for the same reason (a p99 over 240 frames hides the one frame that does a whole B0 wave).
- `code-review` (medium) on both tools: 9 findings, 5 important + 4 nits, **all fixed** (05 C1–C2):
  - windows now clipped to the batch's own script timings;
  - failed runs excluded from the summary and listed;
  - wait after `taskkill` and catch a locked file;
  - AC power from `PowerOnline`;
  - a wave that never completes is a failure;
  - FrameTime rows counted separately;
  - at least 2 runs per combination;
  - `_W<work>` in the naming rule;
  - latency as median across runs.
- `simplify` (self-review, four angles): events parsed once per capture (efficiency). Nothing to reuse beyond the copied functions; no altitude issue.
- The calibration summary was recomputed with the reviewed tools; every number in S2-c is unchanged.


---

## Step S3 · Baselines B1–B3 and the baseline batch (2026-11-21)

Job `20261121_032954_d2b651`: build, package, then batch **`2026-11-21_035912_baseline`** (B0, B1, B2, B3 × 4 interleaved runs + 1 priming run; wave size 500, 3 waves, BeginPlay work 20000, B1 cap K = 14, B2 budget 2.0 ms) and batch **`2026-11-21_040413_spawntimer`** (one B0 run with the engine's `spawnactortimer`). Same environment as S1/S2 (`applied_mask` 0xFFFF, AC power, power plan Turbo). **This is the bar SpawnPacer must clear, captured before any SpawnPacer code exists.**

| # | Check | Result |
|---|---|---|
| S3-a | **Build** | ✅ Editor and game Development targets `Result: Succeeded`, **0 errors, 0 warnings**; package `BUILD SUCCESSFUL`, cook 0 errors, 0 warnings |
| S3-b | **Every run valid** | ✅ 17 runs + 1: all exit 0, 3 waves each, every wave completed, 0 log errors, 0 log warnings; no run excluded from the summary |
| S3-c | **Calibration confirmed** (S2-c) | ✅ B0's wave frame at work 20000: max **84.2 ms** [75.6–97.6], above the 66.7 ms target in every run |

**The baseline matrix** (median across 4 runs [min–max]; ms; summary [2026-11-21_035912_baseline](../Benchmarks/Summaries/2026-11-21_035912_baseline_summary.md)):

| Config | W max FrameTime | Frames > 33.3 ms (W, sum of 4 runs) | Spawn frames / run | Spawn work p99 | Spawn work max | Spawn frames > 1.1 × 2 ms | Wave latency (frames) | D max FrameTime | GC max FrameTime |
|---|---|---|---|---|---|---|---|---|---|
| B0 plain `SpawnActor` | 84.17 [75.59–97.56] | **12** (every wave) | 3 | 79.52 | 79.61 | 12 / 12 | 0 | 12.91 | 9.01 |
| B1 deferred, 14 per frame | 8.81 [7.95–11.33] | 0 | 108 | 4.40 [3.83–4.85] | 4.63 [4.35–5.71] | **320 / 432 (74%)** | 35 | 5.48 | 8.95 |
| B2 time cap 2 ms, checked between items | 7.57 [6.40–8.07] | 0 | 132 | 2.60 [2.51–2.69] | 2.75 [2.64–2.86] | **197 / 528 (37%)** | 42 [41–43] | 8.07 | 8.93 |
| B3 simple pool | 17.37 [16.53–17.84] | 0 | 3 | 10.87 | 10.92 [9.85–11.43] | 12 / 12 | 0 | 13.12 | 8.91 |

The pre-wave median is 3.1 ms in every configuration and the wave-window median 4.0–4.2 ms, so the scene is light and the spawn work dominates the spikes.

**Hypotheses after the baseline batch:**

| # | Hypothesis | Status |
|---|---|---|
| H1 | Registration dominates spawn cost | **Supported, partly.** In B1, `SpawnActor` with `bDeferConstruction` (construction **and** native registration) is **57%** of the spawn time in every run (145–177 ms against 110–129 ms for `FinishSpawning`, over 3 waves). `FinishSpawning` includes the enemies' calibrated BeginPlay work (about 33 ms per wave), so without it construction + registration would dominate further. Separating construction from registration needs the per-stage timing of S5 |
| H2 | Cost grows with component count | **Supported.** Engine `spawnactortimer`, per class: drone (1 part) 0.046 ms, crate (3 colliding parts) 0.110 ms, grunt (6 parts, + ~0.33 ms BeginPlay work) 0.446 ms, boss (24 parts, + the same work) 0.648 ms. Without the BeginPlay work, about 0.12 ms for 6 parts and 0.32 ms for 24: close to linear, about 13–16 µs per visual part; colliding parts cost about twice as much |
| H3 | A count cap fails with mixed classes | **Confirmed.** B1's spawn frames swing with the classes at the head of the queue: 74% of them are over 1.1 × the 2 ms budget it was tuned for, max 4.6 ms |
| H4 | A time cap checked between items overruns by up to one item | **Confirmed.** B2 is over 1.1 × budget in 37% of its spawn frames; max 2.75 ms = the budget plus about one grunt or boss |
| H5 | Destroy and GC hitch like spawning; pooling removes them | **Not supported at this scale.** Destroying 500 actors costs at most 8.9 ms (B0), and the forced GC frame peaks at about 9 ms in **every** configuration, pooled or not. No D or GC frame is over 33.3 ms |
| H6 | First spawns cost more | **Weak at this scale.** The priming run's B0 wave frame (85.2 ms) is within the measured runs' range (75.6–97.6 ms); PSO events: 2 per run, at start-up |
| H7 | Re-activating a whole pool in one frame is not free | **Confirmed.** B3's acquire frame does 10.9 ms of work (500 actors: about 22 µs each) and parking 7.4 ms. Still about **7×** cheaper than spawning (9.3 ms per wave against B0's 76.5 ms of spawn work) |
| new | **Spreading spawns costs more in total** | B1 and B2 do 98 and 95 ms of spawn work per wave against B0's 76.5 ms in one burst: about **25% more**, presumably because a burst runs with warm caches [inferred]. A cost of pacing that SpawnPacer will pay too, and is reported |

**What this changes (stated plainly):**
- **At this workload, any pacing removes the hitch.** B1, B2 and even the simple pool B3 have **0 frames over 33.3 ms**. So SC1 still has to pass, but it **cannot distinguish** SpawnPacer from the existing patterns here.
- The measurable difference that remains is **budget adherence (SC3)**. B2, the Mass / SpawnBudget model, exceeds 1.1 × its own budget in 37% of its spawn frames, and B1 in 74%. Then the **tail** (SC2: B2's max 7.57 ms is the bar), the **latency** paid (SC4: B2 takes 42 frames), and the pool (SC5, SC6).
- **No single actor exceeds the 2 ms budget** (the boss is 0.65 ms), so the per-component split (S6) has nothing to fix at 2 ms. It can only matter at the 1 ms budget of the sweep or with heavier actors, and the ablation will show whether it does, not assume it.
- The targets of 01 §6.4 stay as they were fixed; nothing is reframed. The README and 01 say this in one line each.

**Audit trail for S3:**
- `code-review` (medium) on the diff: 9 findings, 5 important + 4 nits, **all fixed** (05 D1–D3):
  - a reused drone flew back to the park location;
  - a despawn left unserved spawns that leaked into the next cycle;
  - a pool miss dropped the request;
  - the pool fill could land in the capture with a zero warm-up;
  - parked enemies kept thinking;
  - per-item setup inside the timed loop;
  - "Destroyed" overcounted;
  - `TOptional::GetValue` without a check;
  - docs not yet updated.
- While fixing them, one of my own fixes (filling the pool in `OnWorldBeginPlay`) was found wrong before it was built: the world hadn't begun play, so BeginPlay would have run after parking. It was reverted to the first warm-up frame (05 D3).
- `simplify` (self-review, four angles): no further change.
- Tools: `csv_stats.py` gained per-window statistics of the frames that did spawn work, with the count over 1.1 × budget (SC3), and `run_bench.py --resummarize` rebuilds a summary from raw data. The S2 calibration and S3 baseline summaries were rebuilt with it; their manifests are unchanged.
- Shared-scratchpad rule (brief §2 rule 11): drafts now live in the git-ignored `_wip\`. My files in the shared scratchpad were checked by content and moved to `scratchpad\p02_spawnpacer\`. Every tracked and pending file was searched for other projects' identifiers: none.


---

## Step S4 · Pure policy and its unit tests (2026-11-21)

Job `20261121_041329_4292c1`: build the editor and game Development targets, then `Tools\run_tests.py --filter SpawnPacer --expect 8` (the editor, headless, runs every test whose path starts with `SpawnPacer`; 05 E4). Nothing timed: S4 has no benchmark.

| # | Check | Result |
|---|---|---|
| S4-a | **Build** | ✅ Editor and game Development targets `Result: Succeeded`, **0 errors, 0 warnings** |
| S4-b | **8 policy tests, all passed** | ✅ `run_tests: 8 tests (expected at least 8), 0 not passed, editor exit code 0` |

| Test (`SpawnPacer.Policy.…`) | What it proves (roadmap S4 check) |
|---|---|
| `EstimatorFollowsRfc6298` | The estimator matches a hand calculation, including the deviation updated with the **old** mean |
| `ColdKeyIsPredictedAsTheBudget` | Cold key = the cold value (the budget); one sample warms it; another component is another key |
| `NeverStartsAJobThatWontFit` | Never starts a job predicted not to fit (3 of 5 × 0.6 ms in 2 ms) |
| `FirstJobAlwaysRunsAndIsCountedForced` | The first job always runs and is counted forced when predicted over the budget |
| `ColdJobRunsOnlyFirst` | A cold job runs only as a frame's first job, **also after a first job that measured 0 ms** (the code-review case) |
| `EmptyQueueDoesNothing` | An empty queue is fine |
| `ManyTinyJobsStopAtTheBudget` | 1,000 tiny jobs stop at the budget, never above it |
| `OverrunIsReportedAndLearned` | An overrun is reported as spent, never hidden, and the model learns from it |

**Audit trail for S4:**
- `code-review` (medium) on the diff: 6 findings, 1 important + 5 nits, **all fixed**:
  - **important:** a cold job could run second after a first job that measured about 0 ms (`0 + budget > budget` is false). The rule is now an explicit check, with a test for that case (05 E2, E3; 02 §2, §6.3, §7 updated: the design said "cold = predicted as the budget" and relied on it);
  - the docs were not yet written (they are now: 04 §15–18, 05 Part E, 06 §19–21, 07, this section);
  - `--expect` had a default of 1, so a stale build could pass: it is now required, and tests are counted once each by path;
  - the class in the cost key was a raw `UClass*`, which a Blueprint class recompiled at the same address could inherit: now `TObjectKey<UClass>` (06 §20);
  - a log still locked by a running editor crashed the runner with a traceback: it now fails with a clear line;
  - two tests read `Find()` without a null check: now `TestNotNull` first.
- While checking the runner I found its draft docstring wrong: the engine's automation runner **does** exit with −1 when a test reports an error (`AutomationCommandline.cpp:493–504`). The script now checks the exit code too; it still parses the result lines, because exit code 0 can't say which tests ran.
- `simplify`: run as a structured self-review on the four angles (the brief allows no sub-agents, so the skill's parallel reviewers were not started):
  - **reuse:** `run_tests.py` repeated `run_bench.py`'s launch–wait–kill, without the wait timeout after a kill: both now call one `run_and_reap` (05 E5), checked on a normal exit (code 3) and a timeout (killed after 1.2 s);
  - **simplification:** the fake queue's two parallel arrays became one array of jobs;
  - **efficiency:** the scheduler looks a key up twice per job (`IsCold`, `Predict`): tens of nanoseconds against jobs of microseconds, and folding them would duplicate the prediction formula. **Skipped**;
  - **altitude:** the explicit cold check *is* the design's rule, not a patch on it. No change.
- Tests flags: `ProductFilter` (a project's tests) with Editor and Client contexts, as UE5-UpdateBudget's tests use.
- Audits before the commit: `check_links.py` 0 problems; no banned words, personal paths or emails; no stray control characters; `git status` shows only this project's files.


---

## Step S5 · Subsystem, staged spawn, and the first SPS run (2026-11-21)

Job `20261121_051002_175d01`: build both Development targets; `run_tests.py --filter SpawnPacer --expect 17`; package; then batch **`2026-11-21_061433_sps_first`** (SPS × 1 run + 1 priming run; wave 500, work 20000, budget 2.0 ms; grunts and bosses `Deferred`, drones and crates `Whole`). Environment as in S1–S3 (`applied_mask` 0xFFFF, AC power, Turbo).

| # | Check | Result |
|---|---|---|
| S5-a | **Build** | ✅ Editor and game `Result: Succeeded`, **0 errors, 0 warnings**; package `BUILD SUCCESSFUL`, cook 0 errors, 0 warnings |
| S5-b | **17 tests, all passed** | ✅ 8 policy (S4) + 9 subsystem tests (05 F4), `editor exit code 0` |
| S5-c | **The run is valid and the same workload** | ✅ exit 0, 3 waves, every wave complete, 0 log errors or warnings; wave hash `0x29FD2F1C`, the same as every baseline run; `SpawnPacer totals: 3000 completed, 0 dropped, 0 cancelled` (1,500 spawns + 1,500 releases) |

The roadmap's three named tests are `ActorDestroyedBetweenStagesIsDropped`, `SpawnFromInsideBeginPlay` and `WorldEndsWithRequestsPending`. The other six check both split modes, cancellation (including from inside a request's own callback), release (including of a half-spawned actor) and priorities.

**The first SPS run against the S3 baselines** (one run, so no spread yet; ms; summary [2026-11-21_061433_sps_first](../Benchmarks/Summaries/2026-11-21_061433_sps_first_summary.md)):

| | SPS (1 run) | B2, the time cap (4 runs, 09 S3) | B1, the count cap |
|---|---|---|---|
| W max FrameTime | **6.59** | 7.57 [6.40–8.07] | 8.81 [7.95–11.33] |
| W p99 FrameTime | 5.88 | 6.42 [5.98–6.73] | — |
| Frames > 33.3 ms (W and D) | **0** | 0 | 0 |
| Spawn frames per run (W) | 152 | 132 | 108 |
| Spawn work p99 / max | **2.18 / 2.35** | 2.60 / 2.75 | 4.40 / 4.63 |
| Spawn frames > 1.1 × budget | **2 of 152 (1.3%)**, neither forced | 197 of 528 (37%) | 320 of 432 (74%) |
| Wave latency, frames (per wave) | 47, 55, 47 | 42 [41–43] | 35 |
| Spawn work per wave | 81 / 100 / 88 ms | 95 ms | 98 ms |
| Mean work in a spawn frame (utilisation of 2 ms) | 1.69–1.83 ms (**0.85–0.92**) | about 2.16 ms (1.08) | — |

**Against the targets of 01 §6.4, on this one run (the matrix decides):**
- **SC1 ✅:** 0 frames over 33.3 ms in W and D.
- **SC2, not yet:**
  - the max (6.59 ms) is far below B0, B1 and B3 but inside B2's run range, so it isn't separated;
  - the p99 (5.88 ms) is 0.07 ms over its bound (pre-wave median 2.81 + budget 2.0 + 1.0 = 5.81 ms).
- **SC3, narrowly missed:** 98.7% of spawn frames within 1.1 × budget, against the ≥ 99% target (B2: 63%, B1: 26%).
  - Both overruns are in wave 3, not in the cold start, and neither was forced.
  - In one, 3 jobs predicted at 0.75 ms together took 2.35 ms: an outlier no history predicts.
  - The other ran 14 jobs to 2.27 ms.
- **SC4 ✅ on utilisation (0.85–0.92 ≥ 0.75), and latency is the price, as stated:** 47–55 frames against B2's 42.
  - The summed predictions were about 1.6 × the measured work (K = 2): headroom that buys adherence and costs frames.
  - That trade-off is what the K sweep of Phase 8 measures. Nothing is tuned to this run.
- The deferred split's own cost is small: per wave, `ConstructMs` + `FinishMs` = 46–55 ms for the 100 deferred enemies, and `SpawnMs` = 35–45 ms for the 400 whole ones.

**Audit trail for S5:**
- `code-review` (medium) on the diff: 5 findings, 3 important + 2 nits, **all fixed**, with 2 new tests:
  - **important:** a cancel from inside a request's own Construct job left the actor half-spawned. Cancellation is now a flag, resolved from the request's state when it reaches the head (05 F2; test `CancelFromInsideInitialize`);
  - **important:** releasing a half-spawned actor queued a second request that raced its Finish job. The spawn request itself now becomes the release, as 02 §8 requires (test `ReleaseOfAHalfSpawnedActor`);
  - **important:** the plugin's subsystem ran and published in every Game world, so an idle 0.01 ms frame could be read as a baseline's spawn work. It now does nothing until first used, and the summary reads each configuration's own column (05 F2, F5);
  - a release whose actor was already gone counted as completed: now `Dropped`;
  - the director kept one pair of batch ids, so a late despawn could be written to the next cycle's record: now per record (05 F3).
- `simplify` (self-review, four angles): one candidate (sharing a struct between the per-frame and total outcome counters) **skipped**, because it made `End()` harder to read for three fields. No other change.
- **Two design changes, recorded in 02 §15:**
  - SpawnPacer works on `OnWorldPostActorTick` instead of as a tickable, so requests are always served in the frame they are made. This resolves the one-frame risk 02 §15 recorded in S1.
  - "Whole" with an `Initialize` callback is a deferred spawn finished in the same job (the same engine work, `Actor.cpp:4357–4360`).
- The director's wave completion moved into `NotifyActorReady()`. For the baselines this is the same frame as before, so their results stand.
- The S3 baseline summary was rebuilt with `--resummarize` for the new SC3 column. Every earlier cell is unchanged and the new one is `n/a`.
- Audits before the commit: `check_links.py` 0 problems; no banned words, personal paths or emails; no stray control characters; `git status` shows only this project's files.


---

## Step S6 · Per-component split and the split ablation (2026-11-21)

Jobs:
- `20261121_062926_ec426f`: build both targets; `run_tests.py --expect 20`; package; a first ablation batch (`2026-11-21_081704_split`, 4 runs each).
- `20261121_082445_89a529`: the **balanced** ablation, `2026-11-21_090236_split_balanced`, with 6 configurations × 6 runs + 1 priming run.
- `20261121_082530_a65b28`: build both targets and the 20 tests again, after the simplify change.

| # | Check | Result |
|---|---|---|
| S6-a | **Build** | ✅ both targets `Result: Succeeded`, **0 errors, 0 warnings**, in both build jobs; package `BUILD SUCCESSFUL`, cook 0 errors, 0 warnings |
| S6-b | **20 tests, all passed** | ✅ 9 policy (new: `UnseenComponentIsPredictedFromItsFamily`) + 11 subsystem (new: `PerComponentBeginPlaySeesEveryComponent`, `PerComponentRegistersOneComponentPerJob`; three extended to PerComponent), `editor exit code 0`, in both test runs |
| S6-c | **Every ablation run valid** | ✅ 37 of 37 runs exit 0, 3 waves complete, 0 log errors or warnings |
| S6-d | **Rotation balanced** | ⚠ → ✅ The first batch passed `--runs 4` for 6 configurations, and the script warned "the rotation is unbalanced". The brief requires balanced runs, so it was **re-run with 6 runs**. The first batch's files are kept and labelled superseded; nothing below uses it |

The roadmap's test is `PerComponentBeginPlaySeesEveryComponent`: a split spawn shows BeginPlay the same 4 registered components as a whole spawn, and the parts stand where they should.

**The split ablation, balanced** (6 runs each, median [min–max]; W window; summary [2026-11-21_090236_split_balanced](../Benchmarks/Summaries/2026-11-21_090236_split_balanced_summary.md)). `SPS` has the boss PerComponent (the S6 default); grunts are Deferred in every row.

| Budget | Boss | Frames > 33.3 ms | W max FrameTime | Spawn frames over 1.1 × budget, of about N (the median spawn frames per run × 6 runs) | Forced frames | Spawn work max | Wave latency, frames |
|---|---|---|---|---|---|---|---|
| 2 ms | **PerComponent** | 0 | 6.47 [6.25–7.60] | **3 of ~996 (0.3%)** | 0 | 2.22 [2.08–2.33] | 54 [50–63] |
| 2 ms | Deferred | 0 | 6.56 [6.31–8.73] | 1 of ~996 (0.1%) | 4 | 2.04 [1.99–2.60] | 54.5 [51–101] |
| 2 ms | Whole | 0 | 6.55 [6.34–11.63] | 9 of ~1002 (0.9%) | 7 | 2.37 [2.29–2.78] | 54 [49–128] |
| 1 ms | PerComponent | 0 | 5.83 [5.43–6.85] | 25 of ~2748 (0.9%) | 8 | 1.41 [1.17–1.47] | 153 [144–162] |
| 1 ms | Deferred | 0 | 5.94 [5.38–10.77] | 16 of ~2754 (0.6%) | 9 | 1.32 [1.16–1.45] | 152 [142–157] |
| 1 ms | Whole | 0 | 5.61 [5.45–8.09] | 24 of ~2676 (0.9%) | **81** | 1.43 [1.23–1.78] | 144 [139–217] |

**What the ablation shows (stated plainly):**
- **Splitting the boss matters only where one boss no longer fits the budget.**
  - At 2 ms the three modes are the same within noise: no hitches, the same latency, and 0.1–0.9% of spawn frames over 1.1 × budget.
  - At 1 ms the whole boss is predicted over the budget, so it runs alone as a **forced** job: 81 forced frames, against 8–9 when it is split.
- **PerComponent buys nothing over Deferred for this boss.**
  - Its 23 cones have no collision, and registering all of them costs less than the 1 ms budget.
  - The Deferred seam (registration in one job, BeginPlay in the next) already cuts the boss below the budget.
  - PerComponent adds 23 jobs and has slightly more overruns at 1 ms (25 against 16), which is within noise across 6 runs.
  - The per-component seam would matter for actors whose registration alone exceeds the budget (many colliding parts, 01 H2). This demo has none, and the result is reported as it is.
- **A whole boss completes slightly sooner at 1 ms** (144 against 152–153 frames), because a forced job finishes in one frame. That speed is bought with the 81 frames over budget.
- **SC3 at the project's 2 ms budget:** 99.7% of SPS spawn frames are within 1.1 × budget (≥ 99% target met in this batch). The S5 single run had 98.7%.
- **One environmental disturbance, reported and not excluded.**
  - In rotation round 6, three consecutive runs took 13–19 s instead of 11–12 s, with wave latency 101–128 frames (2 ms) and 217 (1 ms).
  - That points to a machine-wide slowdown at that time, not to a configuration. The rotation spread it over three configurations, and the medians are unaffected.
  - It also shows a weakness worth knowing: a burst of slow measurements inflates the estimator's deviation, so jobs run alone for a while. That costs latency, never budget.

**Audit trail for S6:**
- **Design check before any code:** per-component keys would have made each of the boss's 23 parts a cold job, and a split request holds its queue's head. The first boss would have stalled its queue for about 25 frames. Fixed with the **family prior** (05 G1; 02 §7, §15).
- `code-review` (medium): 9 findings, all nits, **all fixed**:
  - a comment stated engine behaviour wrongly (`GetUnregisteredParent` registers a parent first only if its own flag is set); corrected after reading the function;
  - the brain was held back too: now scene components only, which also keeps the family's samples comparable;
  - gone or already-registered parts made near-0 ms "jobs": now skipped;
  - `-SPBossSplit` accepted the generated `_MAX`: now bounded;
  - a three-way ablation needs `SPSBossDeferred`: added;
  - the every-mode test never reached a Register job: now uses the parts actor;
  - the per-job test assumed budget 0 means one job per frame: now counts jobs;
  - `PostRegisterAllComponents` runs before the held-back parts: documented as a limit (02 §13);
  - the split-mode metadata appeared in baseline runs: now `n/a` there.
- `simplify` (self-review, four angles): one change. The "skip held-back parts, then Finish" logic existed twice, and is now one helper, `SkipToNextHeldBack`. That change was built and tested again in job `20261121_082530_a65b28`.
- `SPS` changed meaning in S6 (boss PerComponent; the S5 batch had it Deferred, now `SPSBossDeferred`), as stated in `run_bench.py` and 05 G4.
- Audits before the commit: `check_links.py` 0 problems; no banned words, personal paths or emails; no stray control characters; `git status` shows only this project's files.

---

## Phases 5–8 (to be written)

- S1–S9 checks (functional tests, determinism hash, stage-equivalence test, soak), with their commands and results.
- The calibration, baseline, main, sweep, ablation, memory, soak and overhead batches, each with its batch name and summary (protocol: [Benchmarks/README.md](../Benchmarks/README.md)).
