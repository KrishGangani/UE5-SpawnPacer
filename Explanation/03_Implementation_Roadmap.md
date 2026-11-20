# 03 · Implementation Roadmap: SpawnPacer

**Written:** end of Phase 3, 2026-11-21 · **Engine:** UE 5.8.4

**Rules for every step:**
- Each step is small enough to read in one sitting: a few hundred lines at most.
- A step is finished only when its code **compiles with 0 warnings through the machine queue**, its check **passes**, and its `Explanation/` entries (`04`, `05`, `06`, and `07` where relevant) are **written in the same step**. If the design changes, `02` changes in the same commit.
- After each step that adds C++: run `simplify`, then `code-review`, and record what they found in `09`.
- **The baselines are captured before SpawnPacer exists.** Steps S1–S3 build only the workload, the harness and the four baselines. The plugin's system code starts at S4.
- Every Unreal job (build, package, editor, packaged run, benchmark) goes through `tools\ue_queue.py`. Timed runs are pinned to the performance cores (01 §7).
- Commit locally at the end of each step; never push.

---

**Progress (2026-11-21):** Phase 4 ✅ (F1–F4, plus the F-5 token fix). **S1 ✅** (code review + simplify applied, 09). **S2 ✅** (calibration: BeginPlay work 20000; finding 01b B-7). **S3 ✅** (baseline batch `2026-11-21_035912_baseline`; H1–H7 answered in 09). **S4 ✅** (8 policy tests pass; 09 S4). Next: **S5** (subsystem + staged spawn).

## Phase 4: Foundation (no system code yet) ✅

| Step | What gets built | How it is checked |
|---|---|---|
| **F1** ✅ | `SpawnPacer\SpawnPacer.uproject` (EngineAssociation 5.8), `Source\SpawnPacer.Target.cs`, `SpawnPacerEditor.Target.cs`, the game module (`SpawnPacer.Build.cs`, `SpawnPacerModule.cpp`) | `Tools\build.bat SpawnPacerEditor` and `Tools\build.bat SpawnPacer` through the queue: 0 errors, 0 warnings (09 F-1, F-2) |
| **F2** ✅ | The plugin skeleton: `Plugins\SpawnPacerCore\SpawnPacerCore.uplugin`, `SpawnPacerCore.Build.cs`, `SpawnPacerCoreModule.cpp` | Builds with both targets; mounted in the packaged game (09 F-3, F-4) |
| **F3** ✅ | `Config\DefaultEngine.ini` (engine template map, VSync off, no frame smoothing), `DefaultGame.ini`, `DefaultInput.ini`; `Tools\build.bat`, `package.bat`, `smoke_game.bat`, `perf_cores.py` | Packaged Development build: `BUILD SUCCESSFUL`, cook `0 error(s), 0 warning(s)` (09 F-3) |
| **F4** ✅ | One packaged smoke run on the real GPU | Exit 0; map loads; RTX adapter chosen; 0 warnings after the `DefaultInput.ini` fix (09 F-4) |

## Phase 5: Implementation

| Step | What gets built | How it is checked | Explanation files touched |
|---|---|---|---|
| **S1. Workload, B0 only ✅** | `ASPWaveActor` + `ASPDrone`/`ASPCrate`/`ASPGrunt`/`ASPBoss` (1/3/6/24 meshes), `USPWaveBrainComponent` (gameplay state, BeginPlay work, timer, simulated use); `USPBenchmarkSubsystem` (command line, camera, seeded wave list, the 3-cycle script, scripted GC, CSV events and metadata, run report, exit); `USPBaselineSpawner` (a `UObject` the director drives, 02 §4.2) with **B0 only**; `DirectoriesToAlwaysCook` for the engine meshes | Same seed → same wave hash, twice. One packaged B0 run shows a spike on each wave frame and on each despawn frame; CSV has the events and `SPBaseline/WorkMs` | 04, 05, 06, 07 |
| **S2. Harness + calibration ✅** | `Tools\csv_stats.py` (copied from UE5-UpdateBudget, extended: event-delimited windows W/D/GC/pre-wave, frames > 33.3 / 16.7 ms, max), `Tools\run_bench.py` (copied and adapted: `pin_self` once at start, real exe with `Popen`, rotation, priming run, environment record with `applied_mask()`, timeout + tree kill in `finally`) | `csv_stats.py --selftest` matches a hand calculation. **Calibration batch** (B0): choose the Grunt BeginPlay work and confirm the 01 §6.1 target (wave frame ≥ 66.7 ms, steady state ≤ 12 ms). Values recorded in 09 | 05, 08, 09 |
| **S3. Baselines B1–B3, captured ✅** | B1 (deferred, count cap K), B2 (time cap checked between items), B3 (simple pool) in `USPBaselineSpawner` | **The baseline batch:** B0–B3 × 4 interleaved runs + priming, before any SpawnPacer code. H1–H7 answered with data (H1/H2 with an Insights capture of one wave for the engine's per-phase scopes) | 05, 06, 09 |
| **S4. Pure policy + unit tests ✅** | `FSpawnCostModel` (RFC 6298 estimator, cold rule), `FSpawnPacerScheduler` (admission, progress rule, frame stats) | Automation tests with fake jobs: never starts a job predicted not to fit; the first job always runs and is flagged forced if over budget; cold key = budget; estimator matches a hand calculation; empty queue is fine; 1,000 tiny jobs stop at the budget | 04, 05, 06, 09 |
| **S5. Subsystem + staged spawn (Whole, Deferred)** | `USpawnPacerSubsystem` (Game/PIE), request state machine, priorities, incoming list, `Whole`/`Deferred` stages, budgeted Destroy, CSV telemetry, `OnBatchComplete`; SPS in the director. Until S9 adds the settings class, the director sets each demo class's split mode through a small subsystem call (`SetClassRule`), which the settings later feed | Tests: destroy an actor between its stages; spawn from inside BeginPlay; end PIE with requests pending. First SPS run: SC1 and SC3 on one run before the matrix | 05, 06, 07 |
| **S6. Per-component split** | `PerComponent` mode: `CustomPreSpawnInitialization` holds back native non-root components (`bAutoRegister = false`), Register jobs (parent first), `Finish` only when none is held back | Test that BeginPlay sees every component registered (same result as a whole spawn). **Split ablation batch** (Boss whole vs per component) | 05, 06, 07, 09 |
| **S7. Pool + reset contract** | `FSpawnPacerPool` (park/unpark, remembered ticks, timers and latent actions cleared), `ISpawnPacerPoolable`, Acquire/Release jobs, Background pre-warm; SPP in the director | Functional: a parked actor costs no tick; a timer set during use never fires after reuse | 05, 06, 07 |
| **S8. Reset verifier + soak** | `FSpawnPacerResetVerifier`; `ASPLeakyGrunt` with `-SPSeedLeak=<kind>`; a soak mode in the director (10,000 cycles) | **SC5:** clean classes 0 leaks (false-positive test); each seeded kind detected in 100% of cycles; exclusions found necessary are documented with reasons | 05, 06, 07, 09 |
| **S9. Settings, cvars, Blueprint surface** | `USpawnPacerSettings` + `FSpawnPacerClassRule` (adds `DeveloperSettings` to the plugin's dependencies), cvars `sp.*`, `BlueprintCallable` API, `sp.Debug` overlay | Settings round-trip through `DefaultGame.ini`; cvars change behaviour live; a Blueprint call path works in PIE | 05, 06, 07 |

## Phases 6–10 (after the code exists)

| Phase | Work |
|---|---|
| 6 Explanation | Consolidate 04–07 into one consistent line-by-line pass; re-check every API against 5.8.4 |
| 7 Testing | Edge cases (0 and 1 requests, 5,000-actor wave, budget 0.1 ms, all-cold classes, release during spawn, world teardown mid-wave, pool exhausted), stability (a 30-minute soak), scalability (wave size sweep) |
| 8 Profiling | **Main batch:** 6 configs × 6 interleaved runs + priming, fixed step; budget sweep; overhead batch; `-LLM` memory runs (SC6); one uncapped confirmation run per config; Insights drill-downs. Optimisations only with data (`11_Optimization_Decisions.md`) |
| 9 Code review | `code-review` and `simplify`, plus a written self-review: weaknesses, not praise |
| 10 Portfolio | Charts (`dataviz`), an architecture page, the B0-vs-SPS video, README results, interview Q&A |

**Rough cost of the main batch:** 37 runs × (≈ 15 s load + 5 s warm-up + 36 s capture + exit) ≈ 40 minutes, scripted and unattended, queued as one job.
