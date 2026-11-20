> **Raw Phase 2 notes (A_spawn_pipeline), UE 5.8.4, 2026-11-21.** Every claim cites a file:line in the local engine source. The claims the design depends on are restated in [../01b_Research_Findings.md](../01b_Research_Findings.md). Treat anything here that is not restated there as a working note.

# P02 SpawnPacer · Phase 2 notes, part A: what one spawn does

Engine: `C:\Program Files\Epic Games\UE_5.8\Engine`, `Build/Build.version` = 5.8.4, CL 58902052. Paths below are relative to `Engine/Source/Runtime/Engine/` unless they start with `Engine/` or `Plugins/`.

The question for this part (from the project brief): **what can a plugin do about spawn hitches without engine changes?** To answer it, the whole spawn path is read in order, and each step is tagged with whether a plugin can move it to another frame.

---

## A1. `UWorld::SpawnActor`: the entry point (`Private/LevelActor.cpp`)

The real work is in the `FTransform const*` overload; the others forward to it.

| Line | What happens | Notes |
|---|---|---|
| 456 | `AActor* UWorld::SpawnActor(UClass* Class, FTransform const* UserTransformPtr, const FActorSpawnParameters& SpawnParameters)` | The only body that spawns. `SpawnActorDeferred` (World.h:3851) calls it with `bDeferConstruction = true` (World.h:3869) |
| 458 | `SCOPE_CYCLE_COUNTER(STAT_SpawnActorTime);` | A stats-system cycle counter around the whole call |
| 459 | `CSV_SCOPED_TIMING_STAT_EXCLUSIVE(ActorSpawning);` | **The CSV profiler already times spawning.** Exclusive stats go to category `Exclusive` (enabled by default: `Core/Private/ProfilingDebugging/CsvProfiler.cpp:73`) |
| 475 | `SCOPE_TIME_GUARD_NAMED_MS(TEXT("SpawnActor Of Type"), Class->GetFName(), 2);` | A time guard that reports spawns slower than 2 ms when time guards are enabled **[unverified: time-guard enable switch not read]** |
| 477–479 | `#if ENABLE_SPAWNACTORTIMER FScopedSpawnActorTimer SpawnTimer(...)` | A per-class spawn timer, see A9 |
| 481–537 | Class checks: deprecated, abstract, not an actor, template mismatch, `bIsRunningConstructionScript`, tearing down, NaN transform | Cheap rejections |
| 539–601 | Level choice, template (CDO by default), name generation (`FActorSpawnUtils::MakeUniqueActorName`) | Name generation is per spawn |
| 623–667 | Collision handling method; for `DontSpawnIfColliding` an `EncroachingBlockingGeometry` test **before** construction | Only for that method |
| 672 | `AActor* const Actor = NewObject<AActor>(LevelToSpawnIn, Class, NewActorName, ActorFlags, Template, ...);` | **Object construction**: allocation, property initialisation from the template, C++ constructor, and instancing of every default subobject (the native components). One call: it cannot be split |
| 729–733 | `if (SpawnParameters.CustomPreSpawnInitialization) { SpawnParameters.CustomPreSpawnInitialization(Actor); }` | **A caller hook that runs after construction and before any component is registered.** Declared at `Classes/Engine/World.h:518`: "Custom function allowing the caller to specific a function to execute post actor construction but before other systems see this actor spawn." |
| 739 | `LevelToSpawnIn->TryAddActorToList( Actor, false);` | The actor is now in the level's actor list (actor iterators can find it) |
| 753 | `OnActorPreSpawnInitialization.Broadcast(Actor);` | A world delegate, same moment as the hook |
| 755 | `Actor->PostSpawnInitialize(UserTransform, ..., SpawnParameters.bDeferConstruction, ...);` | See A2 |
| 769–773, 791–797 | `if (!UE::Gameplay::CVars::bDelayOnActorSpawnedUntilFinishedSpawning) { OnActorSpawned.Broadcast(Actor); ... AddNetworkActor(Actor); }` | With the default (true, A8) the broadcast moves to `FinishSpawning` |

## A2. `AActor::PostSpawnInitialize` (`Private/Actor.cpp:4276–4366`)

The function's own comment (4278–4286) lists the intended order: basics → `PreInitializeComponents` → construction → `OnComponentCreated` → `InitializeComponent` → `PostInitializeComponents`, "the same sequence for deferred or nondeferred spawning".

| Line | What happens |
|---|---|
| 4292 | `CreationTime = World->GetTimeSeconds()` |
| 4295–4302 | Net roles, owner, instigator |
| 4305–4325 | `FixupNativeActorComponents(this)` finds the native root; the root gets its world transform (`SetWorldTransform(..., ETeleportType::ResetPhysics)`) |
| 4328 | `DispatchOnComponentsCreated(this);` → `OnComponentCreated` on every native component (4140–4157) |
| 4333 | `bHasDeferredComponentRegistration = (SceneRootComponent == nullptr && Cast<UBlueprintGeneratedClass>(GetClass()) != nullptr);` |
| 4334–4337 | `if (!bHasDeferredComponentRegistration && GetWorld()) { RegisterAllComponents(); }` → **native components are registered here, inside `SpawnActor`, even for a deferred spawn** |
| 4354 | `PostActorCreated();` |
| 4358–4361 | `if (!bDeferConstruction) { FinishSpawning(UserSpawnTransform, true); }` → a plain `SpawnActor` finishes in the same call |
| 4362–4366 | Deferred: the spawn transform is cached in `GSpawnActorDeferredTransformCache` for `FinishSpawning` |

**Consequence [inferred]:** `SpawnActorDeferred` already pays construction **and** native component registration. The deferred seam is *after* registration, not before it.

## A3. Component registration (`Private/Actor.cpp`, `Private/Components/ActorComponent.cpp`)

| Fact | Evidence |
|---|---|
| `RegisterAllComponents()` = `PreRegisterAllComponents()` then `IncrementalRegisterComponents(0)`; "0 - means register all components" | `Actor.cpp:5996–6006` |
| `IncrementalRegisterComponents(int32 NumComponentsToRegister, FRegisterComponentContext* Context)` is public | `Classes/GameFramework/Actor.h` (also research/04 §4b); body `Actor.cpp:6186` |
| Root registered first | `Actor.cpp:6208–6221` |
| The loop registers a component only `if (!Component->IsRegistered() && Component->bAutoRegister && IsValidChecked(Component))` | `Actor.cpp:6235` |
| An unregistered parent is registered before its child | `Actor.cpp:6237–6252` (`GetUnregisteredParent`) |
| With no context it stops after `NumComponentsToRegister` (a **count**); with a context it stops only when the context's timeout has expired (a **time** check, made after a component finished) | `Actor.cpp:6230, 6266–6280` |
| When every component has been visited: `bHasRegisteredAllComponents = true; PostRegisterAllComponents();` (navigation `OnActorRegistered`) | `Actor.cpp:6284–6299`, `4134–4140` |
| `bAutoRegister` is a **public** bitfield (not a UPROPERTY), default `true` | `Classes/Components/ActorComponent.h:284–286`; `ActorComponent.cpp:562` |
| `RegisterComponentWithWorld` → `ExecuteRegisterEvents(Context)` | `ActorComponent.cpp:1967, 2033` |
| `ExecuteRegisterEvents`: `OnRegister()` → `CreateRenderState_Concurrent(Context)` (if the app can render) → `CreatePhysicsState(true)` | `ActorComponent.cpp:2510–2531` (2515, 2526, 2530) |
| `CreatePhysicsState` can **defer** physics creation only if `p.EnableDeferredPhysicsCreation` (default 0), the world allows deferral, the primitive has no overlap events and its body setup has no cooked meshes yet | `ActorComponent.cpp:122–127, 2398–2440` |
| `UActorComponent::RegisterComponent()` (public) registers one component with its owner's world | `ActorComponent.cpp:2079–2089` |

**One component's registration is indivisible [inferred]:** `OnRegister`, render-state creation and physics-state creation run in one call (`ExecuteRegisterEvents`). The engine's own incremental registration checks its timeout only *between* components (6266–6280).

## A4. `AActor::FinishSpawning` and `PostActorConstruction` (`Private/Actor.cpp`)

| Line | What happens |
|---|---|
| 4374 | `void AActor::FinishSpawning(const FTransform& UserTransform, bool bIsDefaultTransform, ...)` |
| 4376–4379 | `FScopedSpawnActorTimer` of type `FinishSpawning` (A9) |
| 4381–4383 | `if (ensure(!bHasFinishedSpawning)) { bHasFinishedSpawning = true; ...` → calling it twice is a bug the engine reports |
| 4386–4409 | Reconciles a transform that differs from the one cached at spawn |
| 4415 | `ExecuteConstruction(...)`: Blueprint construction scripts and `OnConstruction` (A5) |
| 4419–4420 | `SCOPE_CYCLE_COUNTER(STAT_PostActorConstruction); PostActorConstruction();` |
| 4425 | `World->OnActorFinishedSpawning(this);` → `OnActorSpawned` broadcast (A8) |
| 4440 | `PreInitializeComponents();` |
| 4449 | `InitializeComponents();` → for each **registered** component: `Activate` if `bAutoActivate`, `InitializeComponent` if `bWantsInitializeComponent` (`Actor.cpp:6388–6410`) |
| 4452–4495 | Spawn collision handling (`FindTeleportSpot`, `EncroachingBlockingGeometry`, possibly `Destroy()`) |
| 4501 | `PostInitializeComponents();` |
| 4509 | `bool bRunBeginPlay = !bDeferBeginPlayAndUpdateOverlaps && (BeginPlayCallDepth > 0 || World->HasBegunPlay());` |
| 4528–4529 | `SCOPE_CYCLE_COUNTER(STAT_ActorBeginPlay); DispatchBeginPlay();` |

**Consequence [inferred]:** once `FinishSpawning` starts, construction scripts, component initialisation, `PostInitializeComponents` and `BeginPlay` all run before it returns. A plugin cannot put a frame boundary inside it. `bDeferBeginPlayAndUpdateOverlaps` (4443) applies only to replicated actors on clients.

## A5. Blueprint components (`Private/ActorConstruction.cpp:818–1010`)

- `ExecuteConstruction` runs every Blueprint class's `SimpleConstructionScript` (`SCS->ExecuteScriptOnActor`, 893–905), then registers any component it created that is not yet registered (`USimpleConstructionScript::RegisterInstancedComponent`, 913–924), then `ProcessUserConstructionScript()` (939).
- **So components added in a Blueprint are created and registered inside `FinishSpawning`.** The `bAutoRegister` technique in A7 cannot reach them; only native (C++ `CreateDefaultSubobject`) components exist early enough. **[inferred]**
- 4333 (A2): a Blueprint class with no native scene root defers even native registration to `ExecuteConstruction` (`RegisterAllComponents` at 907–911).

## A6. BeginPlay, ticks and overlaps (`Private/Actor.cpp`)

| Fact | Evidence |
|---|---|
| In a game world, actor tick functions are registered **right before BeginPlay**, not at registration, "so as to not actually tick until BeginPlay() executes" | `Actor.cpp:6198–6205` (`if (bAllowTickBeforeBeginPlay || !World->IsGameWorld())`) |
| Components likewise: "If not in a game world register ticks now, otherwise defer until BeginPlay" | `ActorComponent.cpp:2035–2052` |
| `AActor::BeginPlay`: `RegisterAllActorTickFunctions(true, false)`, then for every **registered** component that has not begun play: `RegisterAllComponentTickFunctions(true)` and `Component->BeginPlay()`; then `ReceiveBeginPlay()` | `Actor.cpp:4808–4845` (4814, 4822–4826) |
| The engine expects unregistered components at BeginPlay only when `bAutoRegister` is false (commented-out `check(!Component->bAutoRegister)`) | `Actor.cpp:4829–4833` |
| `DispatchBeginPlay` calls `BeginPlay()`, then `UpdateInitialOverlaps` | `Actor.cpp:4738–4806` (4801) |
| A component registered **after** its owner began play gets its tick functions and `BeginPlay` at registration (`HandleRegisterComponentWithWorld`) | `Actor.cpp:6429–6452` (6443–6451) |
| `UWorld::BeginPlay`: world subsystems' `OnWorldBeginPlay` (6167) before `GameMode->StartPlay()` (6174), which begins play on existing actors | `Private/World.cpp:6153–6181` |

**Consequences [inferred]:**
- An actor between `SpawnActorDeferred` and `FinishSpawning` is in the level and its native components are registered (it renders and collides), but **it does not tick** and has not begun play.
- Each spawned actor's BeginPlay runs inside its own `FinishSpawning`. So with time-slicing, **the BeginPlay order of a wave equals the order in which `FinishSpawning` is called**, and BeginPlays spread over frames. Gameplay that expects "the whole wave began play in the same frame" needs a "batch complete" notification.

## A7. The plugin-level seams, assembled from A1–A6

| Seam | How a plugin uses it | Public API? | What it cannot do |
|---|---|---|---|
| **S-a. Between whole spawns** | Run N complete spawns per frame | Yes (`SpawnActor`) | A single expensive actor still lands in one frame |
| **S-b. Deferred seam** | Frame k: `SpawnActor` with `bDeferConstruction = true` (construction + native registration). Frame k+j: `FinishSpawning` (construction script + init + BeginPlay) | Yes (World.h:3851, `FinishSpawning` is public) | The split point is fixed by the engine |
| **S-c. Per-component registration** | In `CustomPreSpawnInitialization` (runs after `NewObject`, before registration, LevelActor.cpp:729–733) set `bAutoRegister = false` on chosen native non-root components. `RegisterAllComponents` skips them (Actor.cpp:6235). On later frames, register them one at a time with `RegisterComponent()` (parent before child). Then `FinishSpawning` | Yes (`bAutoRegister` public; `RegisterComponent` public) | Not for Blueprint-added components (A5). Not for the root. **[inferred]** BeginPlay semantics are kept only if every deferred component is registered *before* `FinishSpawning`: `InitializeComponents` and `BeginPlay` skip unregistered components (Actor.cpp:6397, 4822) |
| **Indivisible units** | — | — | `NewObject` (construction + subobject instancing); one component's `ExecuteRegisterEvents` (render + physics state); `FinishSpawning` as a whole |
| **Off the game thread** | — | — | Not possible: see A10 |

**Why S-c should preserve semantics (to be tested in Phase 5, not assumed):** a deferred component registered before `FinishSpawning` follows the same path as one registered by `RegisterAllComponents`: `RegisterComponentWithWorld` → `ExecuteRegisterEvents` → `HandleRegisterComponentWithWorld`, which does nothing yet because the owner is neither initialised nor begun play (Actor.cpp:6429–6452). Then `FinishSpawning` initialises it (6397–6407) and begins its play (4822–4826) like any other. **[inferred]**

**Known side effects of S-c to test [inferred]:**
- `PostRegisterAllComponents` (navigation `OnActorRegistered`, Actor.cpp:4134–4140) runs when the *first* pass finishes, before the deferred components exist in the scene. Navigation relevance of the late components is **[unverified]**. The demo map has no navmesh, so the benchmark is not affected; it is stated as a limit.
- Between stages the actor is visible with only some components. The demo classes are spawned out of view or accept this (it is a frame or two).

## A8. Who can see a half-spawned actor

| Fact | Evidence |
|---|---|
| `s.DelayOnActorSpawnedUntilFinishedSpawning` defaults to **true**: "Introduced in UE5.6 that UWorld::OnActorSpawned will run only after the Actor has FinishedSpawning (is fully initialized)" | `Private/LevelActor.cpp:50–57` |
| The broadcast and `AddNetworkActor` then happen in `UWorld::OnActorFinishedSpawning` | `Private/World.cpp:6691–6701` |
| Actor iterators see the actor from `TryAddActorToList` onward | `LevelActor.cpp:739` **[inferred]** |

## A9. Spawn measurement already in the engine

| Tool | What it gives | Evidence |
|---|---|---|
| CSV `Exclusive/GameThread/ActorSpawning` | Per-frame exclusive game-thread ms inside `SpawnActor` | `LevelActor.cpp:459`; column observed in the **end** header of a UE5-UpdateBudget wave capture (it first appeared mid-capture) |
| CSV `Exclusive/GameThread/ActorDestroying` | The same for `DestroyActor` | `LevelActor.cpp:842` |
| `spawnactortimer start` / `stop` console command | Logs, per class: average and total spawn time and count (`SpawnActor` + `FinishSpawning` summed for deferred spawns), and spawn ms per frame | `Private/GameFramework/SpawnActorTimer.cpp:59–119, 127–177, 179–183`; compiled out of Shipping (`SpawnActorTimer.h:11–15`) |
| Stats / Insights scopes | `STAT_SpawnActorTime`, `STAT_PostActorConstruction`, `STAT_ActorBeginPlay`, `STAT_RegisterComponent`, `STAT_ComponentCreateRenderState`, `STAT_ComponentCreatePhysicsState`, trace scope `UActorComponent::CreatePhysicsState` | `LevelActor.cpp:458`; `Actor.cpp:4419, 4528`; `ActorComponent.cpp:1969, 2521, 2405–2406` |

**So per-class and per-phase spawn cost can already be *observed*** (offline, in a log or a trace). What the engine does not do is use it to *decide* what to spawn this frame. **[inferred from the absence of any caller that reads these values]**

## A10. Off the game thread?

- There is no `check(IsInGameThread())` inside `UWorld::SpawnActor` itself (grep of `LevelActor.cpp` finds one only at 1863, in another function).
- But the path mutates unsynchronised world state (`TryAddActorToList`, 739), and the physics job queue asserts the game thread (`Private/PhysicsEngine/Experimental/PhysScene_Chaos.cpp:1939, 1952`).
- Epic staff, 2025: "spawning actors in unreal must be done on the game thread" and "FinishSpawningActor must also be called on the game thread" (C_existing_work §C4).
- **Decision:** the plugin runs entirely on the game thread. **[inferred + Epic statement]**

## A11. Asynchronous physics-state creation (Experimental)

| Fact | Evidence |
|---|---|
| Master switch `p.Chaos.EnableAsyncInitBody`, default **false**, `ECVF_ReadOnly`: "[Experimental] ... Read-only - must be set in DefaultEngine.ini before world init" | `Source/Runtime/Experimental/Chaos/Private/Chaos/AsyncInitBodyHelper.cpp:10–16` |
| `FPhysScene_Chaos::SupportsAsyncPhysicsStateCreation()` returns that switch | `PhysScene_Chaos.cpp:1927–1930` |
| Components opt in: `AllowsAsyncPhysicsStateCreation()` returns `false` in `UActorComponent` | `Classes/Components/ActorComponent.h:898–901` |
| `UStaticMeshComponent` allows it for static collision or when not welding | `Private/Components/StaticMeshComponent.cpp:2324–2328` |
| The async job is pushed from **pre-registration**: `OnPreRegister` → `PushAsyncCreatePhysicsStateJob` | `ActorComponent.cpp:1535–1560` |
| Pre-registration "should be called for this component when its Level is being added to the world" | `ActorComponent.h:891–892` |
| The only engine caller of pre-registration is level streaming's incremental update, and only if `LevelStreaming.AllowIncrementalPreRegisterComponents` (default **false**) and async creation is supported, not on a dedicated server, not in a blocking load | `Private/Level.cpp:132–136, 1880–1892`; `Actor.cpp:6101–6185` |
| `AActor::IncrementalPreRegisterComponents` is declared under `private:` | `Actor.h:1525–1539` |
| Budget cvars `p.Chaos.AsyncPhysicsStateTask.TimeBudgetMS` (0 = unlimited) and `.PriorityExtraTimeBudgetMS` | `PhysicsEngine/Experimental/PhysScene_AsyncPhysicsStateJobQueue.cpp:12–24` |

**Consequence [inferred]:** a spawned actor never takes the async path. `SpawnActor` → `RegisterAllComponents` → `RegisterComponentWithWorld` → `ExecuteRegisterEvents` → `CreatePhysicsState` runs on the game thread (A3). A plugin *could* drive `UActorComponent::PreRegisterComponentWithWorld` (public, `ActorComponent.h:1039`) by hand on components it set to `bAutoRegister = false`, but that means driving an Experimental, read-only-switch path from outside the engine code that owns it. **Rejected for v1** (02 §14); recorded as a possible later experiment.

## A12. Level streaming as the engine's own precedent

- `ULevel::IncrementalRegisterComponents` walks the level's actors and calls `Actor->IncrementalRegisterComponents(NumComponentsToUpdate, &Context)` per actor, returning to the caller after each actor so it can stop for the frame (`Private/Level.cpp:1990–2050`).
- Defaults: `s.LevelStreamingComponentsRegistrationGranularity` = 10 components, `s.LevelStreamingActorsUpdateTimeLimit` = 5.0 ms (`Private/CoreSettings.cpp:17, 20, 72–74`).
- Progress is estimated with fixed **work units**: actor count plus 0.5 × actor count for actor initialisation (`s.RouteActorInitializationWorkUnitWeighting`, comment: "RouteActorInit for a single actor generally takes about half the time to complete compared to component init", `Level.cpp:123–129, 3805–3835`). That is a fixed weight per item, not a measured per-class cost.
- So the engine already registers loaded actors' components across frames and **before** their BeginPlay. That is the pattern seam S-c copies for spawned actors, and the "incremental component registration" that Epic staff said studios build their own spawners on (research/02 [T1-D]).

## A13. Destroy (`Private/LevelActor.cpp:839–1060`)

- `CSV_SCOPED_TIMING_STAT_EXCLUSIVE(ActorDestroying)` (842).
- Detach children and self, clear owner (≈ 940–990), `Destroyed()` → `RouteEndPlay(Destroyed)` → `EndPlay`, `UninitializeComponents` (`Actor.cpp:3221–3260, 3311–3316`), `RemoveActor` (1034), `UnregisterAllComponents()` (1052), `MarkAsGarbage()` (1055), `MarkComponentsAsGarbage()` (1057).
- Memory is freed later by garbage collection. `gc.TimeBetweenPurgingPendingKillObjects` defaults to 60 s in code and 61.1 s in `Engine/Config/BaseEngine.ini:1749` (`Private/UnrealEngine.cpp:1777–1779`).
- **No engine code clears an actor's timers on destroy or EndPlay**: the only callers of `FTimerManager::ClearAllTimersForObject` in `Source/Runtime/Engine/Private` are AutoRTFM tests. **[inferred from grep]** A destroyed actor's timers die with the object; a **pooled** actor's timers survive into its next life (B §B6).
