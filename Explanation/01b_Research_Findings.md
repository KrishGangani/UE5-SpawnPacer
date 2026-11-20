# 01b · Research Findings (Phase 2): SpawnPacer

**Phase:** 2 (Research), completed 2026-11-21 · **Engine:** UE 5.8.4 (CL 58902052), read from `C:\Program Files\Epic Games\UE_5.8\Engine`

**Method:**
- The whole spawn path was read in order, from `UWorld::SpawnActor` to `BeginPlay`, plus component registration, destroy, level-streaming registration and asynchronous physics-state creation.
- Existing solutions were read at source level where the source is public (the engine's Mass spawner, SpawnBudget, PoolManager). Nothing was installed.
- Every engine claim cites a path relative to `Engine/Source/Runtime/Engine/` (or a longer path where stated) plus a line number in 5.8.4. Claims that drive a design decision were opened and re-read before being written here, and are marked ✔.
- **[inferred]** = drawn from following code flow, not a direct statement. **[to measure]** = a hypothesis that needs data.

**The audit trail** with every quote is in [Phase2_Raw_Notes/](Phase2_Raw_Notes/README.md). This file is authoritative.

---

## 0. The core question, answered

**What can a plugin do about spawn hitches without engine changes?**

1. **Pace whole spawns across frames.** Fully possible with public API. This is what the engine's Mass spawner, SpawnBudget and PoolManager already do (§5), so it is **not** this project's contribution.
2. **Decide *before* starting a spawn whether it fits.** Possible. All three existing pacers decide by count, or by time checked between items, so one heavy actor overruns. The engine can already *measure* per-class spawn cost (§4), but nothing uses that to *decide*. **This is a real gap.**
3. **Split one actor's spawn across frames.** Possible, **but only at two seams the engine already has**, and only for native components:
   - **Seam S-b (deferred):** `SpawnActorDeferred` on one frame (construction + native component registration), `FinishSpawning` on a later one (construction script + initialisation + BeginPlay).
   - **Seam S-c (per component):** native non-root components can be held back from registration with the public `bAutoRegister` flag (set from the spawn's `CustomPreSpawnInitialization` hook), then registered one per job on later frames, **all before** `FinishSpawning`, so BeginPlay still sees every component.
   - **Indivisible at plugin level:** object construction (`NewObject` with all default subobjects), one component's registration (`OnRegister` + render state + physics state), and `FinishSpawning` as a whole (construction script, `InitializeComponents`, `PostInitializeComponents`, `BeginPlay`, overlaps). Blueprint-added components are created and registered inside `FinishSpawning`, so they can't be split either.
4. **Move work off the game thread.** Not possible: spawning is game-thread work (§2.7). Asynchronous physics-state creation exists but is reachable only from level-streaming pre-registration behind two default-off switches (§2.6), so v1 doesn't use it.
5. **Avoid the spawn entirely by pooling.** Possible, and done by others. Nobody checks that a reused actor was really reset. **This is the second real gap.**

**Honest gap assessment (changed from the roadmap):** the roadmap said "the engine has no time-sliced spawn or general pool". That is true for *general* use, but the engine's own Mass spawner does have a time-sliced spawn, a time-sliced destroy and an actor pool (§5.1). Time-sliced spawning is therefore **not new anywhere**. The remaining gap is narrower and is now stated as three parts:
- **predicted-cost admission**, so a job that can't fit is never started (medium);
- **staged spawning at the engine's two seams**, measured to show when it matters (medium–weak: the seams are public, and the benefit only appears for actors whose single spawn exceeds the frame's headroom);
- **verified pool reset**, with a seeded-defect test that proves the check is not vacuous (medium–strong: no reviewed solution does it).

Overall gap strength stays **Medium**, with the emphasis moved from "time slicing" to "prediction + verification".

---

## 1. What Phase 2 changed

| # | Finding | Consequence for SpawnPacer |
|---|---|---|
| 1 | **Native components are registered inside `SpawnActor`, even for a deferred spawn:** `PostSpawnInitialize` calls `RegisterAllComponents()` (`Private/Actor.cpp:4333–4337`) ✔ before returning to the caller, and `FinishSpawning` is only reached later for deferred spawns (4358–4361) ✔ | The deferred seam is *after* registration, not before. "Deferred spawn" alone doesn't move the heaviest part (H1) to another frame |
| 2 | **`FinishSpawning` is one synchronous block:** `ExecuteConstruction` (4415), `PostActorConstruction` (4419–4420) = `PreInitializeComponents` (4440), `InitializeComponents` (4449), collision handling, `PostInitializeComponents` (4501), `DispatchBeginPlay` (4528–4529) ✔ | It is one scheduler job with one predicted cost. A plugin can't place a frame boundary inside it |
| 3 | **`CustomPreSpawnInitialization` runs after `NewObject` and before any registration** (`Private/LevelActor.cpp:672, 729–733, 755`) ✔. **`bAutoRegister` is public** (`Classes/Components/ActorComponent.h:284–286`) ✔, and `IncrementalRegisterComponents` skips components with it false (`Actor.cpp:6235`) ✔ | Enables seam S-c: hold back chosen native components, register each on a later frame with `RegisterComponent()` (`ActorComponent.cpp:2079`), then `FinishSpawning`. Opt-in per class; to be proven equivalent by test in Phase 5 |
| 4 | **`InitializeComponents` and `BeginPlay` skip unregistered components** (`Actor.cpp:6397`, `4822`) ✔; a component registered after its owner began play gets `BeginPlay` at registration (`Actor.cpp:6443–6451`) ✔ | All held-back components must be registered **before** `FinishSpawning`, or BeginPlay code would see missing components. That ordering is a hard rule of the request state machine (02 §8) |
| 5 | **Blueprint construction-script components are created and registered inside `FinishSpawning`** (`Private/ActorConstruction.cpp:897–924`) ✔ | Seam S-c applies to **native** components only. The demo classes are C++ (a stated limit, 02 §13) |
| 6 | **One component's registration is indivisible:** `ExecuteRegisterEvents` = `OnRegister` → `CreateRenderState_Concurrent` → `CreatePhysicsState` (`ActorComponent.cpp:2510–2531`) ✔. The engine's own incremental loop checks its timeout only between components (`Actor.cpp:6266–6280`) | The finest plugin-level job is "register one component". Its cost is predicted per class and component |
| 7 | **A half-spawned actor (between the stages) does not tick:** in game worlds actor and component ticks are registered at BeginPlay (`Actor.cpp:6198–6205`, `4814, 4824`; `ActorComponent.cpp:2035–2052`) ✔. **`OnActorSpawned` fires only after `FinishSpawning`** by default (`s.DelayOnActorSpawnedUntilFinishedSpawning`, `LevelActor.cpp:50–57`; `World.cpp:6691–6701`) ✔. But it is in the level's actor list (`LevelActor.cpp:739`) and its registered components render and collide **[inferred]** | Splitting is safe for engine bookkeeping but visible to the player and to actor iterators. Opt-in per class, default `Whole` (02 §7) |
| 8 | **BeginPlay order = the order `FinishSpawning` is called** (each actor's BeginPlay runs inside its own `FinishSpawning`, `Actor.cpp:4528–4529`) **[inferred]** | Requests in one priority level complete in request order (FIFO, head of line). A "batch complete" event tells gameplay when a whole wave has begun play |
| 9 | **The engine already measures spawn cost:** CSV `Exclusive/GameThread/ActorSpawning` (`LevelActor.cpp:459`) ✔ and `ActorDestroying` (842) ✔; the `spawnactortimer start/stop` command logs average cost per class (`Private/GameFramework/SpawnActorTimer.cpp:59–119, 179–183`, non-Shipping) ✔; Insights scopes for each phase (`STAT_PostActorConstruction`, `STAT_ActorBeginPlay`, `STAT_RegisterComponent`, `STAT_ComponentCreateRenderState`, `STAT_ComponentCreatePhysicsState`) | **Per-class and per-phase measurement is not new, and we don't claim it.** Our per-stage timing is the input to a *prediction used for admission*. The engine columns are used to cross-check our numbers |
| 10 | **Async physics-state creation never applies to spawned actors:** it starts from pre-registration (`ActorComponent.cpp:1535–1560`), which the engine calls only from level-streaming incremental updates (`Level.cpp:1880–1892`), behind `p.Chaos.EnableAsyncInitBody` (default false, read-only, Experimental, `Source/Runtime/Experimental/Chaos/Private/Chaos/AsyncInitBodyHelper.cpp:10–16`) ✔ and `LevelStreaming.AllowIncrementalPreRegisterComponents` (default false, `Level.cpp:132–136`) ✔ | **Not used in v1.** Driving it by hand on spawned actors would mean steering an Experimental path from outside the code that owns it. Recorded as a possible later experiment (02 §14) |
| 11 | **The engine clears no timers on EndPlay or destroy** (no non-test caller of `ClearAllTimersForObject` in `Engine/Private`) **[inferred]**; **there is no public per-object timer query** (`Public/TimerManager.h:291` clear only; the index `ObjectToTimers` at 550 is private) ✔. Latent actions can be counted (`Classes/Engine/LatentActionManager.h:149`) ✔ | The reset contract **clears** timers unconditionally on release, and verifies them **by effect** (a leaked timer changes a property). Latent actions are cleared and counted |
| 12 | **`SetActorTickEnabled` switches only the actor's own tick** (`Actor.cpp:1754–1760`) ✔ | Release switches off component ticks too and remembers which were on (SpawnBudget does the same, §5.2) |
| 13 | **Reflection can compare and print any `UPROPERTY`** (`FProperty::Identical_InContainer`, `ExportTextItem_InContainer`, `CoreUObject/Public/UObject/UnrealType.h:528, 592`) ✔ | The reset verifier compares a reused actor (and its components) field by field with a snapshot of the same actor taken when it was fresh. Non-`UPROPERTY` members are invisible to it: a stated limit |
| 14 | **No general pool or amortised spawn API in 5.8.4** (grep of `Source/Runtime` and `Plugins/Runtime` headers) ✔, although Epic staff said in June 2025 such work was being investigated for "around the 5.7 and 5.8 releases" (§5.4) | The project is still valid on 5.8.4. The policy (`FSpawnPacerScheduler`) is kept separate from the spawn mechanism so it could sit on top of a future engine API (02 §12) |

**Benchmark-plan changes:**

| # | Finding | Consequence |
|---|---|---|
| B-1 | **`FrameTime` is wall-clock time between frames** (`Core/Private/ProfilingDebugging/CsvProfiler.cpp:4383–4390`) ✔, and fixed-step mode sets the simulated delta without waiting (`Private/UnrealEngine.cpp:3014–3022`) ✔ | Fixed-step runs (`-deterministic -FPS=60`) give every configuration identical simulated frames while `FrameTime` still shows each spawn frame's real cost. Wave latency is reported in **frames** and in simulated seconds; one uncapped real-time run per configuration confirms SC1 |
| B-2 | **A CSV column that first appears mid-capture exists only in the end header** (observed: `Exclusive/GameThread/ActorSpawning` in a UE5-UpdateBudget wave capture) | The copied `csv_stats.py` must keep UE5-UpdateBudget's "use the end header" rule. Our own CSV stats are written every frame from the first captured frame, so they appear in both headers |
| B-3 | **The Mass spawner's defaults are 1.5 ms spawn / 0.5 ms destroy per tick** (`Plugins/Runtime/MassGameplay/Source/MassSimulation/Public/MassSimulationSettings.h:23, 27`) ✔ | A check-between-items baseline (B2) is part of the comparison, at the **same** budget as SpawnPacer. That is the fairest "best existing" pacer |
| B-4 | **GC runs every 61.1 s by default** (`Engine/Config/BaseEngine.ini:1749`) ✔; `ForceGarbageCollection(bool)` exists (`Classes/Engine/Engine.h:2828`) ✔ | A full GC is forced at the same scripted time after every despawn, in every configuration, so destroy-wave garbage is measured instead of left to luck |
| B-5 | **LLM is in Development builds and off unless `-LLM`** (`Core/Public/HAL/LowLevelMemTrackerDefines.h:25–29`; `Core/Private/HAL/LLM/LLM.cpp:2318`) ✔ | Pool memory (SC6) is measured in separate `-LLM` runs, never in timing runs |
| B-6 | **Hybrid CPU:** UE5-UpdateBudget saw runs about 1.6× apart when Windows put the game thread on an E-core | The benchmark script pins itself once (`pin_self` with the mask from `performance_core_mask`, copied into `Tools\perf_cores.py`; 0xFFFF on this machine, checked 2026-11-21), so every game it starts inherits the mask from its first instruction; `applied_mask()` is recorded. Games are started as the real exe with `subprocess.Popen`. `cmd /c start /wait` is not used: it always returns 0 (found by UE5-TestTrust) |

---

## 2. Spawn path (evidence)

### 2.1 `UWorld::SpawnActor` (`Private/LevelActor.cpp`)

| Fact | Evidence (5.8.4) |
|---|---|
| The one spawning body; `SpawnActorDeferred` calls it with `bDeferConstruction = true` | `LevelActor.cpp:456`; `Classes/Engine/World.h:3851–3871` ✔ |
| CSV exclusive stat `ActorSpawning`, cycle stat `STAT_SpawnActorTime`, 2 ms time guard, per-class spawn timer | `:458, 459, 475, 477–479` ✔ |
| Construction: `NewObject<AActor>(Level, Class, Name, Flags, Template, ...)` | `:672` ✔ |
| `CustomPreSpawnInitialization(Actor)` | `:729–733` ✔; declared `World.h:518` ✔ |
| Added to the level's actor list | `:739` ✔ |
| `OnActorPreSpawnInitialization` broadcast, then `PostSpawnInitialize` | `:753, 755` ✔ |

### 2.2 `PostSpawnInitialize` and registration (`Private/Actor.cpp`)

| Fact | Evidence |
|---|---|
| Root transform set, `OnComponentCreated` dispatched | `:4305–4328` |
| Native components registered unless a Blueprint class has no native root | `:4333–4337` ✔ |
| Non-deferred spawns call `FinishSpawning` here | `:4358–4361` ✔ |
| `RegisterAllComponents` = `PreRegisterAllComponents` + `IncrementalRegisterComponents(0)` (all) | `:5996–6006` ✔ |
| Root first; parents before children; skips `bAutoRegister == false` | `:6208–6221, 6235–6252` |
| Count limit without a context, time limit (checked after a component) with one | `:6230, 6266–6280` |
| `PostRegisterAllComponents` (navigation `OnActorRegistered`) once all are visited | `:6284–6299, 4134–4140` |
| `ExecuteRegisterEvents`: `OnRegister`, render state, physics state | `Private/Components/ActorComponent.cpp:2510–2531` ✔ |
| `p.EnableDeferredPhysicsCreation` (default 0) can defer physics creation only for primitives without overlap events whose physics meshes aren't built | `ActorComponent.cpp:122–127, 2398–2440` |

### 2.3 `FinishSpawning` → BeginPlay (`Private/Actor.cpp`)

| Fact | Evidence |
|---|---|
| `ensure(!bHasFinishedSpawning)` | `:4381` |
| `ExecuteConstruction`, `PostActorConstruction`, `OnActorFinishedSpawning` | `:4415, 4419–4420, 4425` ✔ |
| `PreInitializeComponents`, `InitializeComponents`, `PostInitializeComponents` | `:4440, 4449, 4501` ✔ |
| BeginPlay runs only once the world has begun play (`World->HasBegunPlay()`) | `:4509` ✔ |
| `DispatchBeginPlay` → `BeginPlay` → `UpdateInitialOverlaps` | `:4528–4529, 4738–4806` |
| `BeginPlay` registers actor ticks, then for each registered component: ticks + `BeginPlay` | `:4808–4845` ✔ |
| `InitializeComponents` touches only registered components | `:6388–6410` ✔ |

### 2.4 Blueprints (`Private/ActorConstruction.cpp`)

| Fact | Evidence |
|---|---|
| SCS components created and registered during `ExecuteConstruction` | `:897–924` ✔ |
| User construction script after that | `:939` |

### 2.5 Who sees a half-spawned actor

| Fact | Evidence |
|---|---|
| Ticks are registered only at BeginPlay in game worlds | `Actor.cpp:6198–6205` ✔; `ActorComponent.cpp:2035–2052` ✔ |
| `OnActorSpawned` and `AddNetworkActor` delayed to `FinishSpawning` by default (since 5.6) | `LevelActor.cpp:50–57, 769–797`; `World.cpp:6691–6701` ✔ |

### 2.6 Asynchronous physics-state creation (Experimental)

| Fact | Evidence |
|---|---|
| `p.Chaos.EnableAsyncInitBody` default false, `ECVF_ReadOnly`, "[Experimental]" | `Source/Runtime/Experimental/Chaos/Private/Chaos/AsyncInitBodyHelper.cpp:10–16` ✔ |
| `SupportsAsyncPhysicsStateCreation()` returns it | `Private/PhysicsEngine/Experimental/PhysScene_Chaos.cpp:1927–1930` ✔ |
| Components opt in (`AllowsAsyncPhysicsStateCreation`, base `false`; static mesh allows it when not welding) | `Classes/Components/ActorComponent.h:898–901`; `Private/Components/StaticMeshComponent.cpp:2324–2328` |
| The job is pushed from `OnPreRegister` | `ActorComponent.cpp:1535–1560` ✔ |
| Pre-registration is driven only by level streaming, behind `LevelStreaming.AllowIncrementalPreRegisterComponents` (default false) | `Private/Level.cpp:132–136, 1880–1892` ✔ |
| Task budget `p.Chaos.AsyncPhysicsStateTask.TimeBudgetMS` (0 = unlimited) | `Private/PhysicsEngine/Experimental/PhysScene_AsyncPhysicsStateJobQueue.cpp:12–17` |

### 2.7 Threads

| Fact | Evidence |
|---|---|
| No `check(IsInGameThread())` inside `SpawnActor`, but it mutates the level's actor list, and the physics job queue asserts the game thread | `LevelActor.cpp:739`; `PhysScene_Chaos.cpp:1939, 1952` |
| Epic staff: spawning and `FinishSpawningActor` "must be done on the game thread" | Forum thread, §5.4 |

### 2.8 Level streaming (the engine's own precedent)

| Fact | Evidence |
|---|---|
| `ULevel::IncrementalRegisterComponents` registers loaded actors' components across frames, before their BeginPlay | `Private/Level.cpp:1990–2050` |
| Defaults: 10 components per step, 5.0 ms time limit | `Private/CoreSettings.cpp:17, 20, 72–74` |
| Progress uses fixed work units (actor count + 0.5 × actor count), not measured cost | `Level.cpp:123–129, 3805–3835` |

### 2.9 Destroy and GC

| Fact | Evidence |
|---|---|
| `DestroyActor`: `ActorDestroying` CSV stat, `Destroyed()` → `EndPlay`, `RemoveActor`, `UnregisterAllComponents`, `MarkAsGarbage` | `LevelActor.cpp:839–1060` (842, 1034, 1052, 1055) |
| Memory is freed by GC; default interval 61.1 s | `Private/UnrealEngine.cpp:1777–1779`; `Engine/Config/BaseEngine.ini:1749` |

---

## 3. Measurement and tooling (evidence)

| Fact | Evidence (5.8.4) |
|---|---|
| `FrameTime` from `FPlatformTime::Seconds()` | `Core/Private/ProfilingDebugging/CsvProfiler.cpp:4383–4390` ✔ |
| `-deterministic` = fixed step + fixed seed | `Launch/Private/LaunchEngineLoop.cpp:2457–2460` ✔ |
| Fixed step: delta = fixed value, no wait | `Private/UnrealEngine.cpp:3014–3022` ✔ |
| `-csvStartOnEvent=`, `-csvStopOnEvent=`, `-ExitAfterCsvProfiling` | `CsvProfiler.cpp:5081, 5090, 4309` ✔ |
| `-ExecCmds=` is parsed by the engine at startup | `Private/UnrealEngine.cpp:2552` ✔ |
| **`-ExitAfterSeconds` is not parsed anywhere** in 5.8.4 `.cpp`/`.h` (grep of `Engine/Source`) | so our smoke script doesn't pass it |
| LLM: Development yes, `-LLM`, `-LLMCSV` | `LowLevelMemTrackerDefines.h:25–29`; `LLM.cpp:2318, 2324` ✔ |
| `FPlatformMemoryStats::UsedPhysical` | `Core/Public/GenericPlatform/GenericPlatformMemory.h:141` |

## 4. Engine tools that already measure spawn cost

| Tool | Gives | Doesn't |
|---|---|---|
| CSV `Exclusive/GameThread/ActorSpawning`, `ActorDestroying` | ms per frame inside `SpawnActor` / `DestroyActor` | Per class; `FinishSpawning` time of deferred spawns outside `SpawnActor` **[inferred: the CSV scope is in `SpawnActor` only]** |
| `spawnactortimer start` / `stop` | Per-class average, total and count, logged at stop | Anything at run time; Shipping |
| Insights scopes (`STAT_PostActorConstruction`, `STAT_ActorBeginPlay`, `STAT_RegisterComponent`, `STAT_ComponentCreateRenderState`, `STAT_ComponentCreatePhysicsState`) | Per-phase breakdown in a trace | Feed any decision |

## 5. Existing solutions, updated

| Solution | Paces by | Knows cost before starting? | Splits one spawn? | Pool reset | Verified? | Numbers | Evidence |
|---|---|---|---|---|---|---|---|
| **Mass actor spawner** (engine, Experimental) | Time, checked **before** each spawn (1.5 ms default); destroy 0.5 ms | No | No | `IMassActorPoolableInterface` + hide only | No | — | `MassActorSpawnerSubsystem.cpp:119–146, 235–262, 357–371, 571–595`; `MassSimulationSettings.h:23, 27` ✔ |
| **SpawnBudget** (UE 5.8, a paid plugin whose source is publicly readable on GitHub; cited by file:line for comparison only, none of its code is copied) | Count + time checked **between** items: "one slow actor can overshoot it by exactly one actor" | No | No (plain `SpawnActor`) | Hide, collision, actor + component ticks, `ISpawnBudgetPoolable` | No | None published | `Docs/DOCUMENTATION.md` §4, §11–12; `SpawnBudgetSubsystem.cpp:791–835, 900` |
| **PoolManager** (UE 5.7, MIT) | Count: 5 per frame | No | No (deferred + finish in one frame) | Hide, collision, actor tick | No | None published | `Config/BasePoolManager.ini:2`; `PoolFactory_Actor.cpp:36–52, 96–106` |
| Fab pools (Flex Pool, UltraPool, PoolKit, …) | Not stated | Not stated | Not stated | Interfaces | Not stated | — | Search snippets only (raw notes C §C5) |
| Hand-written deferred pattern (Epic forum advice) | Count per tick | No | Possible at seam S-b | n/a | n/a | — | Forum, §5.4 |

### 5.4 Epic statements

- **Off the game thread:** "spawning actors in unreal must be done on the game thread"; "FinishSpawningActor must also be called on the game thread" (john.yatco, Epic, 2025-05-21 and 2025-05-23).
- **Future engine work:** "a generic solution to both actor and component pooling/recycling" and "user controlled amortized batched spawning", "hopeful you'll see the beginnings of these efforts appearing around the 5.7 and 5.8 releases" (Andy Davidson, Epic, 2025-06-06). Source: https://forums.unrealengine.com/t/is-it-possible-to-safely-spawn-actors-off-the-game-thread/2592995. **Not found in 5.8.4** (finding 14).
- **The Great Hitch Hunt** (Ari Arnbjörnsson, Epic, Unreal Fest 2025): spawning is a top hitch category; studios build time-sliced spawners on incremental component registration. Details and links in [research/02](Research/02_technical_challenges.md) [T1-D].

**The gap, stated conservatively:**
- Time-sliced spawning and actor pooling exist, in the engine (Mass, Experimental) and in community plugins. We cite them and benchmark the same pattern as baseline B2.
- Among the solutions reviewed, none decides with a **predicted per-class, per-stage cost** whether the next job fits, so each can overrun by one item (SpawnBudget says so itself).
- None splits one actor's spawn at the engine's seams and measures when that helps.
- None **verifies** that a reused actor was reset; all leave it to an interface the user implements.
- None publishes controlled measurements.
- Studio-internal systems aren't public, and a search can't prove absence. This is a statement about what was reviewed, never "nobody has done this".

---

## 6. Root-cause hypotheses after Phase 2

| # | Hypothesis | Status |
|---|---|---|
| H1 | Component registration (render + physics state) dominates spawn cost, not construction | **Supported by code** (registration does three kinds of state creation per component, §2.2) **[to measure]** with our per-stage timing and the engine's Insights scopes |
| H2 | Spawn cost grows about linearly with the number of primitive components | Unchanged **[to measure]** (the 1/3/6/24-component demo classes) |
| H3 | A per-frame **count** cap fails with mixed classes: the frame's cost depends on which classes happen to be at the head of the queue | Unchanged **[to measure]** with B1 |
| H4 | A **time** cap checked between items overruns by up to one item, so the heaviest class sets the worst frame | **Stated by SpawnBudget's own documentation** and visible in the Mass loop (§5) **[to measure]** with B2 |
| H5 | Destroying a wave, and the GC that follows, hitch like the spawn does; pooling removes most of both | **Supported by code** (`DestroyActor` unregisters every component, §2.9) **[to measure]** |
| H6 | The first spawn of a class costs much more than later ones (first-use allocations, PSO creation) | **Supported by observation:** the first Phase 4 smoke run logged 50 PSO creation hitches on its first frame, the second run of the same build one PSO miss (09 F-4a, F-4b) **[to measure]** per wave |
| H7 | Re-activating a whole pool in one frame is not free (render-state and physics-state changes) | Unchanged **[to measure]** with B3 |

## 7. Carried into Phase 3

- A request is a small **state machine of stage jobs** (`Construct`, `Register` × k, `Finish`, or `Acquire`, `Release`, `Destroy`), each with its own predicted cost.
- **Admission:** a job starts only if `spent + predicted ≤ budget`, except that the frame's first job always runs (progress), and that run is counted.
- **Prediction:** per (class, stage) mean and mean deviation, updated like RFC 6298 (α = 1/8, β = 1/4), predicted = mean + K × deviation. An unknown (class, stage) is predicted as the whole budget, so its first sample runs alone in a frame.
- Seam S-c only for native, non-root components, registered parent-first, all before `FinishSpawning`.
- Pool release clears timers and latent actions, switches off actor and component ticks (remembering them), hides, disables collision, parks. A verifier compares the reused actor with its own fresh snapshot.
- Everything on the game thread. No async physics in v1.
- Benchmarks: packaged Development, fixed step, P-core pinning, forced GC at a scripted time, separate `-LLM` memory runs, end-header CSV parsing.

**Raw research notes**, with every quote, are in [Phase2_Raw_Notes/](Phase2_Raw_Notes/README.md).
