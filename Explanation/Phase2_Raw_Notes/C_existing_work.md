> **Raw Phase 2 notes (C_existing_work), 2026-11-21.** Engine code is cited by file:line in the local UE 5.8.4 install. Third-party plugins were **read, not installed** (project brief). Their public source was read only to describe what it does; none of it is copied into this project.

# P02 SpawnPacer · Phase 2 notes, part C: existing work and what it leaves unsolved

---

## C1. Inside the engine: the Mass actor spawner (time-sliced spawn + pool)

Plugin `Engine/Plugins/Runtime/MassGameplay` (`"IsExperimentalVersion": true` in `MassGameplay.uplugin`).

| Fact | Evidence (`Plugins/Runtime/MassGameplay/Source/`) |
|---|---|
| `UMassActorSpawnerSubsystem::ProcessPendingSpawningRequest(const double MaxTimeSlicePerTick)` loops `while (FPlatformTime::Seconds() < TimeSliceEnd && IterationsCount++ < IterationsLimit)` | `MassActors/Private/MassActorSpawnerSubsystem.cpp:357–371` |
| So the time is checked **before** each spawn and a spawn, once started, runs to completion. Nothing predicts its cost **[inferred]** | same lines |
| Called at the start of Mass's PrePhysics phase with `DesiredActorSpawningTimeSlicePerTick` | `MassActorSpawnerSubsystem.cpp:571–583` |
| Defaults: spawning **0.0015 s** (1.5 ms), destruction **0.0005 s** per tick | `MassSimulation/Public/MassSimulationSettings.h:23, 27` |
| Destruction is time-sliced too (`ProcessPendingDestruction`) | `MassActorSpawnerSubsystem.cpp:500–520, 585–595` |
| Pool: `ReleaseActorToPool` requires `IMassActorPoolableInterface` and `CanBePooled`, calls `PrepareForPooling`, then **only** `SetActorHiddenInGame(true)` | `MassActorSpawnerSubsystem.cpp:119–146` |
| Retrieve: `SetActorHiddenInGame(false)`, `SetActorTransform(..., ResetPhysics)`, `PrepareForGame` | `MassActorSpawnerSubsystem.cpp:235–262` |
| Interface: three `BlueprintNativeEvent`s, `CanBePooled`, `PrepareForPooling`, `PrepareForGame` | `MassActors/Public/MassActorPoolableInterface.h:10–28` |
| Cvar `mass.spawning.actorpooling` (default on) | `MassActorSpawnerSubsystem.cpp:24–31` |

**What it leaves unsolved for this project:**
- It serves Mass representation only (requests come from Mass entities); a gameplay wave can't use it without Mass **[inferred]**.
- Check-before-start time slicing: one heavy actor can overrun.
- The pool's reset is whatever `PrepareForPooling`/`PrepareForGame` do; nothing checks it.

## C2. SpawnBudget (SimulatedFlow / Teufel Engineering), source-available, paid

**Note:** SpawnBudget is a **paid** plugin whose source is publicly readable on GitHub. It is cited here by file and line **for comparison only**. None of its code is copied into this project, and it was not installed.

Repository: https://github.com/SimulatedFlow/ue-plugin-SpawnBudget (default branch `autobackup`, created 2026-08-22, last push 2026-09-06; GitHub reports no licence file; the description says source-available under the Fab EULA). Read 2026-11-21 through GitHub's public API. The online manual (https://wiki.teufel-engineering.com/en/SpawnBudget/documentation) returned only a title, so the repository's `Docs/DOCUMENTATION.md` was read instead.

| What it states or does | Evidence |
|---|---|
| UE **5.8**, plugin 1.0.0 | `Docs/DOCUMENTATION.md` header and §10 |
| Per-frame limits `MaxSpawnsPerFrame`, `MaxDespawnsPerFrame`, `MaxMillisecondsPerFrame`; queue sorted by importance (distance / weight, visible boost), FIFO on ties | `DOCUMENTATION.md` §1, §4 "The sort key" |
| "`MaxMillisecondsPerFrame` is checked **between** items, so one slow actor can overshoot it by exactly one actor and never by more." | `DOCUMENTATION.md` §4 "The millisecond ceiling" |
| Spawns with plain `SpawnActor` (not deferred) | `Source/SpawnBudget/Private/SpawnBudgetSubsystem.cpp:900`; `SpawnBudgetPool.cpp:63` |
| The deadline test happens before each queue item | `SpawnBudgetSubsystem.cpp:791–794, 816, 835` |
| Pool release: hide, disable collision, switch off the actor tick **and every component tick that was on** (remembered and restored), optional park location | `DOCUMENTATION.md` §4 "What a despawn actually does"; `SpawnBudgetPool.cpp:124–162` |
| Gameplay reset is the user's job: `ISpawnBudgetPoolable` with `OnSpawnedFromPool` / `OnReturnedToPool`. Troubleshooting: "Enemies come back dead / with no ammo / stuck in their old state. They are pooled and not resetting themselves." | `DOCUMENTATION.md` §5, §11, §12 |
| No timer clearing in the pool (no `ClearAllTimers` in `SpawnBudgetPool.cpp`) | grep of the file **[inferred]** |
| Benchmarks: none published | README, DOCUMENTATION.md |

**Unsolved:** cost is checked after the fact (one-actor overshoot, stated by the author); no phase splitting; no reset verification; no published measurements.

## C3. PoolManager (JanSeliv), MIT

Repository: https://github.com/JanSeliv/PoolManager (MIT; description "Open-source plugin that helps reuse objects in UE5.7"; 188 stars; last push 2026-07-21). Read 2026-11-21.

| What it does | Evidence |
|---|---|
| `"EngineVersion": "5.7.0"` | `PoolManager.uplugin` |
| Spreads creation by **count**: `SpawnObjectsPerFrame=5` | `Config/BasePoolManager.ini:2`; `Private/Factories/PoolFactory_UObject.cpp:163–187` (`SetTimerForNextTick`, `FMath::Min(ObjectsPerFrame, SpawnQueue.Num())`) |
| Actors: `bDeferConstruction = true`, then `FinishSpawning` right after (same frame) | `Private/Factories/PoolFactory_Actor.cpp:36–52` |
| Activation toggles `SetActorHiddenInGame`, `SetActorEnableCollision`, `SetActorTickEnabled` (the actor tick only, see B §B5) | `PoolFactory_Actor.cpp:96–106` |
| Return parks the actor at a far location | `PoolFactory_Actor.cpp:86–93` |

**Unsolved:** count-based, not time-based; no reset verification; component ticks are not switched off by the factory shown **[inferred from the lines read]**; no published measurements.

## C4. Epic statements (forums)

**"Is it possible to safely spawn actors off the game thread?"**, https://forums.unrealengine.com/t/is-it-possible-to-safely-spawn-actors-off-the-game-thread/2592995 (opened 2026-11-21):
- john.yatco (Epic, 2025-05-21): "spawning actors in unreal must be done on the game thread"; suggests "deferred spawning which can queue up spawn requests and be spawned on the game thread later".
- john.yatco (2025-05-23): "you get to control when to call FinishSpawningActor allowing you to spread spawning over multiple frames"; "FinishSpawningActor must also be called on the game thread".
- Andy Davidson (Epic, 2025-06-06): Epic is investigating "a generic solution to both actor and component pooling/recycling" and "providing user controlled amortized batched spawning"; "I'm hopeful you'll see the beginnings of these efforts appearing around the 5.7 and 5.8 releases"; no guarantee.

**Checked against 5.8.4:** no general actor or component pool and no amortised spawn API in `Source/Runtime` or `Plugins/Runtime` headers (grep for `SpawnActorAsync`, `AsyncSpawnActor`, `TimeSlicedSpawn`, `ActorPool`, `ComponentPool`, `Poolable`, `Recycl` finds only the Mass pool and unrelated camera-shake recycling). FX component pools exist (Niagara `ENCPoolMethod`, `Plugins/FX/Niagara/Source/Niagara/Public/NiagaraComponentPoolMethodEnum.h:8`). **Risk:** a future engine version may ship this; the design keeps the scheduler policy separate so it could sit on top of an engine API.

**"The Great Hitch Hunt"** (Ari Arnbjörnsson, Epic, Unreal Fest 2025), forum thread https://forums.unrealengine.com/t/tutorial-the-great-hitch-hunt-tracking-down-every-frame-drop/2541268, summarised in [research/02](../Research/02_technical_challenges.md) [T1-D]: spawning is a top hitch category; fixes named are fewer actors per frame and pooling ("not a built-in feature yet" per press coverage); studios build time-sliced spawners on incremental component registration.

**"Spread spawning across multiple frames?"** (snippet only, research/03 Addendum A): "To pool 300 actors you still need to spawn 300 actors." This is why pre-warming the pool must itself be budgeted.

## C5. Fab and smaller GitHub pools (not opened beyond search snippets)

From [research/03](../Research/03_existing_solutions_gap_analysis.md) Addendum A and one 2026-11-21 search:
- Flex Pool, UltraPool, Actor Pool Manager, Object Pool Creater (Fab): background pre-loading, growth policies, interface-based reuse. **Snippet only.**
- PoolKit (Fab): replaces "Spawn Actor from Class" with "Spawn Actor From Pool", lists 5.4–5.8. https://www.fab.com/listings/97bc42c6-bd16-4b1b-bfd1-67f19e8ec4c7 **Search snippet only.**
- bennystarfighter/UnrealEngine-ObjectPool: a world-subsystem pool whose README warns pre-spawning causes "a big initial performance hit". **Snippet only.**

None of these was found to publish controlled measurements or to verify reset. Because they were not read in full, the claim is limited to "not found in what was read".

## C6. Summary table

| Solution | Unit of pacing | Cost known before starting? | Splits one spawn? | Pool reset | Reset verified? | Published measurements |
|---|---|---|---|---|---|---|
| Mass actor spawner (Experimental) | time, checked before each spawn | No | No | interface + hide | No | No |
| SpawnBudget (5.8) | count + time, checked between items | No (one-actor overshoot stated) | No | hide, collision, actor + component ticks, interface | No | No |
| PoolManager (5.7) | count (5/frame) | No | No (deferred + finish in one frame) | hide, collision, actor tick | No | No |
| Hand-written deferred pattern (forum) | count per tick | No | Possible at the deferred seam | n/a | n/a | n/a |
| Engine `spawnactortimer` | (measurement only) | Measures per class, offline | n/a | n/a | n/a | n/a |
