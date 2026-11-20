# 01 · Problem Analysis: SpawnPacer

**Phase:** 1 (Problem Analysis), written 2026-11-21 together with Phase 2, whose evidence is in [01b_Research_Findings.md](01b_Research_Findings.md). Where Phase 2 changed the Phase 1 view, this file says so.
**Engine:** UE **5.8.4** (CL 58902052), launcher build.
**Status of every claim:** **[verified]** (read in engine source or a cited source), **[hypothesis]** (to be confirmed by measurement), or **[target]** (a pass/fail threshold, not a result).

---

## 1. The problem in one paragraph

Games create actors in bursts: an ambush of enemies, a volley of projectiles, a room full of props when a door opens. In Unreal, every `SpawnActor` call does all of its work immediately, on the game thread, inside the frame that asked for it:
- it constructs the object and its components;
- it registers every component, which creates render state and physics state;
- it runs the construction script, initialises the components, and calls `BeginPlay`.

When 500 actors arrive in one frame, that frame pays for all 500. At 60 fps a frame has 16.7 ms. A frame that takes 40 or 100 ms is a visible **hitch**: the game freezes for a moment, and input feels dead.

The obvious fixes all exist in some form:
- **Spread the spawns over several frames.** But existing pacers count spawns, or check the clock *after* each spawn, so one heavy actor still overruns.
- **Reuse actors from a pool.** But a reused actor carries its previous life with it (health, timers, attached objects) unless something resets it, and nothing checks that the reset was complete.

## 2. Where this happens in real games

| Evidence | What it shows | Source |
|---|---|---|
| Epic, "The Great Hitch Hunt" (Unreal Fest 2025) | Spawning is one of the named hitch categories; fixes named are fewer actors per frame and pooling; studios build their own time-sliced spawners on incremental component registration | [research/02](Research/02_technical_challenges.md) [T1-D] |
| Epic, shader-stutter blog (2025) | "Too much spawning or stream-in" is a source of traversal stutter; root causes are hard to find "without instrumentation" | [research/02](Research/02_technical_challenges.md) [T1-A] |
| Epic forum, World Partition streaming (2025) | The streaming time limit is checked only *between* work items, so one heavy item overruns it. The same flaw applies to check-between spawn pacers | [research/02](Research/02_technical_challenges.md) [T4-A] |
| Epic staff, 2025 | Spawning must stay on the game thread; generic pooling and amortised batched spawning were "being investigated" | [01b](01b_Research_Findings.md) §5.4 |
| Engine code | Mass ships its own time-sliced spawner and actor pool for crowds: Epic needed this for its own systems | [01b](01b_Research_Findings.md) §5 |
| Job postings, 2026 | CDPR's Expert Gameplay Programmer (AI) posting names "spawning and systemic open world" and "monitoring performance and memory"; Riot's Engine Performance role asks for profiling with Unreal Insights or PIX | [research/01](Research/01_job_market.md) (C4, R1) |

## 3. What already exists, and why it doesn't fully solve the problem

| Existing tool | What it does | What it doesn't do | Evidence |
|---|---|---|---|
| `SpawnActor` | Spawns completely, now | Any pacing | [verified] `LevelActor.cpp:456` |
| `SpawnActorDeferred` + `FinishSpawning` | Lets you set properties before construction scripts and BeginPlay, and so lets you put a frame between the two | Moves registration: **native components are already registered inside `SpawnActorDeferred`** | [verified] `Actor.cpp:4333–4337` (01b #1) |
| Mass actor spawner (engine, Experimental) | Time-sliced spawn (1.5 ms) and destroy (0.5 ms), actor pool | Checks time **before** each spawn without knowing its cost; Mass-only; pool reset unchecked | [verified] 01b §5 |
| SpawnBudget (UE 5.8, paid) | Count + ms cap, distance rings, per-class pool that also switches off component ticks | Its author: the ms cap "is checked between items, so one slow actor can overshoot it by exactly one actor"; reset left to an interface; no measurements | [verified] 01b §5 |
| PoolManager (UE 5.7, MIT) | Pool; creates 5 objects per frame | Count-based; no reset check; no measurements | [verified] 01b §5 |
| Engine `spawnactortimer`, CSV `ActorSpawning`, Insights scopes | Measure spawn cost per class and per phase | Use it to decide anything | [verified] 01b §4 |
| Async physics-state creation (Experimental) | Moves physics-body creation to worker threads | Applies only to level-streaming pre-registration, behind two default-off switches | [verified] 01b #10 |

**The gap, after Phase 2 (medium; narrower than the roadmap said):** time-sliced spawning and pooling are **not new**. They exist in the engine itself (Mass) and in community plugins, and this project benchmarks that pattern as a baseline. What none of the reviewed solutions does:
1. **decide before starting a job whether it fits**, using a predicted per-class, per-stage cost;
2. **split one actor's spawn at the engine's own seams** and measure when that matters;
3. **verify that a pooled actor was really reset**, and prove the check works by seeding defects;
4. **publish controlled measurements** of any of this.

## 4. Root-cause hypotheses

Each hypothesis gets a measurement that can confirm or reject it. The baseline runs come before any SpawnPacer code.

| # | Hypothesis | Why we suspect it | How we test it |
|---|---|---|---|
| H1 | **Component registration dominates spawn cost**, not object construction | Registration creates render state and physics state for every component (01b §2.2) | Per-stage timing in our harness (construct / register / finish), cross-checked with the engine's Insights scopes `STAT_RegisterComponent`, `STAT_ComponentCreateRenderState`, `STAT_ComponentCreatePhysicsState` |
| H2 | **Cost grows about linearly with primitive-component count** | Each component is registered on its own | The four demo classes have 1, 3, 6 and 24 mesh components; fit cost vs count |
| H3 | **A count cap fails with mixed classes**: the frame's cost depends on which classes are at the head of the queue | A count says nothing about cost | Baseline B1: distribution of per-frame spawn work at a fixed count |
| H4 | **A time cap checked between items overruns by up to one item** | Stated by SpawnBudget; visible in Mass's loop (01b §5) | Baseline B2: per-frame spawn work vs its budget; the worst frames should coincide with heavy classes |
| H5 | **Destroying a wave, and the GC after it, hitch like spawning**; pooling removes most of both | `DestroyActor` unregisters every component; garbage is collected later in one go (01b §2.9) | Despawn-window and GC-frame times, destroy-based vs pooled configurations |
| H6 | **A class's first spawn costs much more than later ones** (first-use allocations, PSO creation) | The first Phase 4 smoke run logged 50 PSO creation hitches on its first frame; the second run of the same build logged one PSO miss (09 F-4a, F-4b) | Per-wave statistics: wave 1 vs waves 2–3; cold vs warm cost-model samples |
| H7 | **Re-activating a whole pool in one frame is not free** | Un-hiding and re-enabling collision changes render and physics state for every component | Baseline B3's wave frame |

**Not a cause we accept without checking:** "the GPU is the bottleneck". Every capture records `GPUTime` and `RenderThreadTime`. If a spike frame is GPU- or render-bound, the game-thread fix won't show, and the workload is changed.

## 5. The cost of leaving it unsolved

A frame over 33.3 ms means a 60 fps game showed the same image for at least two extra refreshes. Players see that as a stutter. Epic gave a whole Unreal Fest talk to hunting these down ([T1-D]).

*Arithmetic illustration only, not a measurement:* if a typical enemy costs 150 µs to spawn, 500 of them cost 75 ms in one frame. Spread at 2 ms per frame, they take about 38 frames (0.6 s at 60 fps) and no single frame gets heavier than 2 ms. That trade (a little latency for no hitch) is what the project measures.

Without a solution, teams typically:
- spawn fewer things and cut design ideas;
- hand-tune "N per frame" constants that break when content changes;
- pool everything and then chase bugs where reused enemies come back with the wrong state.

## 6. What "solved" means: success criteria, defined now

### 6.1 The workload (identical in every run)

- **Map:** `/Engine/Maps/Templates/Template_Default` (floor and lights; no authored map). A fixed camera looks at the arena.
- **Four C++ demo classes**, engine basic-shape meshes only (so nothing has to be authored in the editor):

  | Class | Mesh components | Extra | Share of a wave |
  |---|---|---|---|
  | `Drone` (light: projectile, pickup) | 1 | moves every tick | 250 |
  | `Crate` (medium: prop) | 3 | collision, no tick | 150 |
  | `Grunt` (heavy: enemy) | 6 | gameplay component with a calibrated amount of BeginPlay work, a repeating timer, gameplay state (health, ammo, state) | 95 |
  | `Boss` (one actor whose single spawn is larger than the budget) | 24 | as Grunt | 5 |

- **Wave script** (fixed step 60 fps, so every configuration simulates identical frames):
  - warm-up: 5 s (pool configurations fill their pool here);
  - then **3 wave cycles of 12 s** inside the capture. At t = 0 s, 500 actors are requested as one batch (positions and class order from a fixed seed). At t = 6 s the batch is despawned. At t = 8 s one full GC is forced (the same in every configuration, 01b B-4). The next cycle starts at t = 12 s.
- **Calibration target (step S2):** the plain-`SpawnActor` wave frame must be at least **66.7 ms** (twice the hitch line), and the steady state with 500 live actors at most **12 ms** median frame time, so a 2 ms budget fits inside 16.7 ms. The Grunt's BeginPlay work and the component counts are the dials. The chosen values are recorded in 09, as UE5-UpdateBudget did for its compute step.

### 6.2 Compared configurations (all in the same packaged build, switched by command line)

| ID | Configuration | Why it is included |
|---|---|---|
| B0 | **Plain `SpawnActor`** for all 500 in the request frame; `Destroy` all at despawn | What most code does |
| B1 | **Deferred-spawn pattern, count cap:** a queue, `SpawnActorDeferred` + `FinishSpawning`, at most K actors per frame (spawn and destroy). K = budget ÷ mean per-actor cost from calibration, fixed per batch and recorded | The pattern Epic staff and the forums recommend; PoolManager's model |
| B2 | **Time cap checked between items:** spawn (and destroy) while elapsed < budget, at the **same budget** as SpawnPacer | Mass's and SpawnBudget's model: the strongest existing pacer |
| B3 | **Simple pool:** filled during warm-up, all 500 acquired in the request frame, released at despawn. Reset = hide, collision, actor tick (PoolManager's set) | The naive pool |
| **SPS** | **SpawnPacer, spawn path:** predicted-cost admission per stage; Grunt split at the deferred seam, Boss per component; budgeted destroy | Our system without a pool |
| **SPP** | **SpawnPacer, pool path:** budgeted pre-warm, budgeted acquire/release, full reset contract | Our system with its pool |

Defaults: budget **2.0 ms** (02 §7). A budget sweep (1, 2, 4 ms) and a split ablation (Boss whole vs per component) are separate, smaller batches.

### 6.3 Analysis windows and metrics

Because the step is fixed, every window covers the same frames in every configuration.

| Window | Frames | Why |
|---|---|---|
| **Wave window W** | From the request frame for 4.0 s (240 frames) × 3 waves | Contains the spawn work in every configuration (B0's spike or SpawnPacer's spread) and the first seconds of 500 live actors |
| **Despawn window D** | From the despawn request for 2.0 s (120 frames) × 3 | Destroy or release cost |
| **GC frames** | The forced-GC frame and the next 2 frames × 3 | Garbage left by the wave (H5) |
| **Pre-wave** | The 1.0 s before each wave | The reference steady state |

| Metric | Unit | Tool |
|---|---|---|
| Frames over 33.3 ms and over 16.7 ms, per window | count | CSV `FrameTime` + `Tools\csv_stats.py` (copied from UE5-UpdateBudget, extended for windows) |
| Frame time median, p95, p99, max, per window | ms | same |
| Game-thread, render-thread and GPU time | ms | CSV `GameThreadTime`, `RenderThreadTime`, `GPUTime` |
| Spawn work per frame (scheduler or baseline driver, timed by our code) | ms | our CSV stats; cross-checked with `Exclusive/GameThread/ActorSpawning` |
| Per-stage cost per class (construct, register, finish, acquire, release, destroy) | µs | our CSV stats; Insights spot checks |
| Wave-completion latency: request → last actor has begun play (or been acquired) | frames and ms of simulated time | CSV events `WaveRequested`, `WaveComplete` |
| Budget utilisation during a wave | ratio | spent ms ÷ (frames with pending work × budget) |
| Reset leaks per cycle, by kind | count | the reset verifier (soak runs) |
| Pool memory | MB, KB per actor | `-LLM` runs: LLM total and tags; `PhysicalUsedMB` |

### 6.4 Pass/fail criteria [target]

| # | Criterion | Threshold |
|---|---|---|
| SC1 | **No hitch frames during waves** | Frames over 33.3 ms inside W **and** D: B0–B3 counts reported; SPS and SPP: **0 in every run**, fixed-step and in the uncapped real-time confirmation run. GC frames are reported separately (H5), not hidden |
| SC2 | **Bounded tail** | In W, SPS and SPP **max** frame time < 33.3 ms and lower than every baseline's max, with non-overlapping run ranges. **p99** ≤ pre-wave median + budget + 1.0 ms. The p99 of every configuration is reported, including where a baseline's is lower (B0's three spike frames can hide above p99 of 720 frames; that is why max and hitch count lead) |
| SC3 | **The budget is respected because cost is predicted** | Spawn work ≤ 1.10 × budget in ≥ 99% of SpawnPacer frames that ran a job. Frames where the frame's first job was predicted above the budget (forced) are counted and listed separately. The same statistic is reported for B1 and B2 at the same budget, as the comparison |
| SC4 | **Latency is the price, and it is reported** | Wave-completion latency in frames and ms for all six configurations. SpawnPacer's budget utilisation during waves **≥ 0.75** (prediction must not waste most of the budget). If not met, reported and analysed, not reframed |
| SC5 | **Pool reuse is verified clean** | 10,000 release/acquire cycles over the demo classes, with simulated use between cycles: **0 leaks** reported on the correctly-reset classes (this is also the false-positive test). On a seeded-defect class, each of 6 defect kinds (actor property not reset, component property not reset, component added at run time and left, actor left attached, delegate left bound, component tick left in the wrong state) is detected in **100%** of the cycles where it is injected. Timers and latent actions are cleared by the pool on every release rather than inspected (01b #11); a functional check confirms that a timer set during use fires **0** times after reuse across the 10,000 cycles |
| SC6 | **Pool memory is known** | MB for 500 parked actors and for the same 500 live, from `-LLM` runs (LLM total) and `PhysicalUsedMB`; reported in MB and KB per actor. Target: parked ≤ live + 5% |
| SC7 | **Low overhead** | With an effectively unlimited budget (1,000 ms), SPS's total spawn work per wave ≤ 1.05 × B0's. Scheduler bookkeeping per job (excluding the engine's own work) median ≤ 2 µs |

## 7. Measurement plan (summary; the full protocol goes in `09_Testing_And_Benchmarks.md`)

- **Build:** packaged **Development**, standalone, never PIE or the editor. The launcher engine can't build Test (UE5-UpdateBudget found this in its Phase 4, and this project's build script documents it); Shipping drops the CSV profiler.
- **Timing mode:** fixed step `-deterministic -FPS=60 -novsync` for every headline run: identical simulated frames, real `FrameTime` (01b B-1). One **uncapped real-time** run per configuration confirms SC1.
- **Capture:** started and stopped by the game itself with CSV events (`-csvStartOnEvent=BenchStart -csvStopOnEvent=BenchEnd`), `-traceautostart=0`, `GameUserSettings.ini` reset before every run. Window boundaries come from CSV events written by the director (`WaveRequested`, `WaveComplete`, `DespawnRequested`, `GCForced`).
- **Real GPU**, windowed 1280×720, default scalability, no VSync, no sound.
- **CPU pinning:** the benchmark script pins **itself** to the performance cores once, before it records anything or starts a game (`pin_self` with the mask from `performance_core_mask`, both in `Tools\perf_cores.py`, copied from UE5-UpdateBudget; 0xFFFF on this i7-14650HX). Every game it starts inherits the mask from its first instruction, engine start-up included. The mask read back from Windows (`applied_mask()`) goes into the environment record. Each run starts the **real** game executable (`Build\Windows\SpawnPacer\Binaries\Win64\SpawnPacer.exe`, not the bootstrap) with `subprocess.Popen`, waits with a timeout, and kills its process tree in a `finally` block. Why: UE5-UpdateBudget saw runs 1.6× apart when Windows put the game thread on an E-core. `cmd /c start /wait /affinity` is not used, because it always returns exit code 0 and would hide failed runs (found by UE5-TestTrust).
- **Order and repetition:** one discarded priming run per batch, then interleaved runs with the configuration order rotated each round. **Runs per combination = number of configurations** (6 for the main batch), so every configuration sits in every order position exactly once. Combinations no criterion needs are skipped.
- **Batches:**

  | Batch | Configs | Runs | For |
  |---|---|---|---|
  | calibration | B0 | 2 per setting | §6.1 calibration target |
  | **baseline** (before SpawnPacer exists) | B0–B3 | 4 each | H1–H7, the bar SpawnPacer must clear |
  | **main** | B0–B3, SPS, SPP | 6 each | SC1–SC4 |
  | budget sweep | B2, SPS, SPP at 1 / 2 / 4 ms | 3 each per budget | SC3, SC4 sensitivity |
  | split ablation | SPS with Boss whole vs per component | 2 each | Does splitting one spawn matter? |
  | memory (`-LLM`) | pool-parked vs live | 2 each | SC6 |
  | soak | SPP verifier on; clean and seeded-defect classes | 1 each (correctness, not timing) | SC5 |
  | overhead | SPS at 1,000 ms vs B0 | 2 each | SC7 |

- **Statistics:** median, p95, p99 and max per run and per window (linear-interpolation percentiles, as in UE5-UpdateBudget's `csv_stats.py`), then the median across runs with the [min–max] range. Counts (frames over 33.3 ms) are reported per run and summed. An improvement is claimed only when run ranges don't overlap.
- **Environment recorded with every batch:** CPU i7-14650HX (8P + 8E), affinity mask, GPU RTX 5070 Laptop (driver 610.47), 32 GB RAM, Windows 11 build 26200, project on D: (Kingston NVMe), engine on C:, UE 5.8.4, build configuration, resolution, power mode, AC power.
- **Raw data:** untouched captures in `Benchmarks\Raw\`, summaries in `Benchmarks\Summaries\`, naming rules in [Benchmarks/README.md](../Benchmarks/README.md).

## 8. Limits we accept up front

- **Pacing adds latency.** A wave that B0 shows in one frame appears over tens of frames. That is the trade, and SC4 reports it. Gameplay that needs "the whole wave at once" must wait for the batch-complete event.
- **A single indivisible stage can still exceed the budget.** If one component's registration, or one actor's `FinishSpawning`, costs more than the budget, that frame overruns. It is counted (SC3), never hidden.
- **Splitting is visible.** Between stages a half-spawned actor renders and collides but doesn't tick. It is opt-in per class.
- **Native components only.** Blueprint construction-script components register inside `FinishSpawning` and can't be split (01b #5).
- **The reset verifier sees `UPROPERTY` state only.** Plain C++ members are invisible to it, and pending timers are cleared rather than inspected (01b #11).
- **Single-player, no replication.** Networked spawning (where clients spawn replicated actors) is out of scope (12_Future_Scalability.md).
- **Fixed-step timing hides the knock-on effect of a hitch on simulation time.** The uncapped confirmation run covers SC1 under real deltas.
