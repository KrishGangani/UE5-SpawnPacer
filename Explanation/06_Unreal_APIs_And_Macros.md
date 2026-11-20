# 06 · Unreal APIs And Macros: SpawnPacer

**Rule:**
- Only APIs this project **actually uses** appear here, each in the same step it enters the code.
- Each entry follows the 8-point format: what it is · why it exists · how it works · when to use it · when not to · common mistakes · a small example · how we use it.
- Verified against UE **5.8.4** source; file references are relative to `Engine/Source/` unless stated.

| # | API | First used in |
|---|---|---|
| 1 | `IMPLEMENT_PRIMARY_GAME_MODULE` / `IMPLEMENT_MODULE`, `FDefaultGameModuleImpl` / `FDefaultModuleImpl` | Phase 4 |
| 2 | `ModuleRules`: dependency lists and `PCHUsage` | Phase 4 |
| 3 | `TargetRules`: `Type`, `DefaultBuildSettings`, `IncludeOrderVersion` | Phase 4 |
| 4 | Descriptor `LoadingPhase` and plugin flags (`.uproject` / `.uplugin`) | Phase 4 |
| 5 | `BuildCookRun` (RunUAT) and the packaged layout | Phase 4 |

The spawn, registration, pool and profiling APIs that the design relies on (`SpawnActor` with `FActorSpawnParameters`, `FinishSpawning`, `bAutoRegister`, `RegisterComponent`, `UTickableWorldSubsystem`, `UINTERFACE`, `FProperty`, timers, latent actions, CSV profiler) are verified in [01b](01b_Research_Findings.md) and get their full 8-point entries here in the step where they enter the code (S1–S9).

---

## 1. `IMPLEMENT_PRIMARY_GAME_MODULE` / `IMPLEMENT_MODULE`

1. **What it is.** Macros that generate a module's entry point (`Runtime/Core/Public/Modules/ModuleManager.h:943–971` for `IMPLEMENT_MODULE`, `1097–1136` for the primary game module).
2. **Why it exists.** Unreal loads code as **modules**: DLLs in editor builds, parts of one executable in packaged builds (04 §3). The module manager needs one standard way to find and start each one, and to call its `StartupModule()` / `ShutdownModule()`.
3. **How it works.**
   - In a modular build it exports an `InitializeModule` function that the module manager calls after loading the DLL; it returns a new instance of the class you name.
   - In a monolithic build it registers a static initializer instead.
   - The *primary* variant also defines project-wide globals for packaged builds: `GInternalProjectName` (from `UE_PROJECT_NAME`, 1102) and `GIsGameAgnosticExe = false` (1104).
4. **When to use it.** Exactly one per module. At least one primary game module per project.
5. **When not to.** Not twice in one module (duplicate-symbol link errors). Not the primary variant in a plugin.
6. **Common mistakes.**
   - The module-name argument doesn't match `Build.cs` and the `.uproject`: "module not found" at startup.
   - Writing an empty custom `IModuleInterface` class when `FDefaultModuleImpl` would do.
   - Thinking the third argument still sets the game name. In 5.8 it is named `DEPRECATED_GameName` and ignored (1096–1100).
7. **Example.**
   ```cpp
   #include "Modules/ModuleManager.h"
   IMPLEMENT_MODULE(FDefaultModuleImpl, MyPluginModule);
   ```
8. **In this project.** `SpawnPacerModule.cpp` uses the primary variant with `FDefaultGameModuleImpl`; `SpawnPacerCoreModule.cpp` uses `IMPLEMENT_MODULE(FDefaultModuleImpl, SpawnPacerCore)`. Neither module needs startup logic: the plugin's classes register themselves through reflection.

## 2. `ModuleRules`: dependencies and `PCHUsage`

1. **What it is.** The C# class in `<Module>.Build.cs` that tells UnrealBuildTool how to compile one module (`Programs/UnrealBuildTool/Configuration/Rules/ModuleRules.cs`).
2. **Why it exists.** Unreal is hundreds of modules. Each one declares what it depends on, so UBT can work out include paths, link order and what to rebuild.
3. **How it works.**
   - `PublicDependencyModuleNames`: their include paths go to us *and* to every module that depends on us.
   - `PrivateDependencyModuleNames`: only to us.
   - Both link the dependency.
   - `PCHUsage = UseExplicitOrSharedPCHs`: "Shared PCHs may be used if an explicit private PCH is not set through PrivatePCHHeaderFile" (`ModuleRules.cs:217–220`).
4. **When to use which.** Public **only** if one of *your public headers* includes that module's headers. Everything else private.
5. **When not to.** Don't add modules "just in case"; each one costs build time and couples modules.
6. **Common mistakes.**
   - Making everything public: wider include paths, wider rebuilds.
   - A missing dependency: `cannot open include file` or `unresolved external symbol`.
   - A private dependency where a public header needs it: works until another module includes that header.
   - A circular dependency between two modules (UBT reports it).
7. **Example.**
   ```csharp
   PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine" });
   PrivateDependencyModuleNames.Add("SpawnPacerCore");
   ```
8. **In this project.** The game module depends privately on `SpawnPacerCore` (only `.cpp` files will use it until S5). The plugin depends publicly on `Core`, `CoreUObject` and `Engine`, because its public headers will expose engine types. `DeveloperSettings` is added in S9.

## 3. `TargetRules`: `Type`, `DefaultBuildSettings`, `IncludeOrderVersion`

1. **What it is.** The C# class in `<Name>.Target.cs` that describes one executable: game, editor, client, server or program (`Configuration/Rules/TargetRules.cs`).
2. **Why it exists.** The same modules build into different executables with different settings and module sets.
3. **How it works.** UBT finds the class `<Name>Target` and builds its `ExtraModuleNames` plus the engine and enabled plugins. `DefaultBuildSettings` selects a versioned set of default options (`V7` = `Latest` in 5.8.4, `TargetRules.cs:170, 177`). `IncludeOrderVersion` selects which engine version's header include order applies (`Unreal5_8` = `Latest`, 235, 242).
4. **When to use it.** One file per executable type. Here: `SpawnPacer` (game) and `SpawnPacerEditor` (editor).
5. **When not to.** No `Client` or `Server` targets: this project isn't networked.
6. **Common mistakes.**
   - The class name doesn't follow `<Name>Target`.
   - Leaving an old `IncludeOrderVersion` after an engine upgrade, which hides missing-include bugs until later.
   - Expecting `Test` to build on a launcher engine: "Targets cannot be built in the Test configuration with this engine distribution" (found by UE5-UpdateBudget in its Phase 4).
7. **Example.** `Source\SpawnPacer.Target.cs` (05 A2).
8. **In this project.** `V7` and `Unreal5_8`, the values Epic's 5.8 `TP_Blank` template uses. Headline builds are packaged **Development**.

## 4. Descriptor `LoadingPhase` and plugin flags

1. **What it is.** Fields in the `.uproject` and `.uplugin` JSON that tell the engine when to load each module and how to treat each plugin.
2. **Why it exists.** Some modules must load before the engine finishes starting (rendering plugins); most can load later. Plugins need on/off and maturity flags for the editor's Plugins window.
3. **How it works.** `LoadingPhase` names a point in startup. `Default` is "Loaded at the default loading point during startup (during engine init, after game modules are loaded.)" (`Runtime/Projects/Public/ModuleDescriptor.h:46–47`); earlier phases such as `PostConfigInit` exist for low-level hooks (31–32). `EnabledByDefault` decides whether a plugin loads without being listed in the project; `IsBetaVersion` / `IsExperimentalVersion` drive the badge in the Plugins window ([research/03](Research/03_existing_solutions_gap_analysis.md) Appendix A lists these flags for the engine plugins this portfolio touches).
4. **When to use it.** `Default` unless a module must exist earlier (e.g. it provides a renderer or a config class read at startup).
5. **When not to.** Don't pick an earlier phase "to be safe": earlier modules can't rely on engine systems that aren't up yet.
6. **Common mistakes.** Loading a module later than the code that needs its classes (for example `PostEngineInit` for classes the startup map references) **[inferred, not tested]**; changing `LoadingPhase` to fix an unrelated startup error; marking a plugin Experimental, or not, without a reason.
7. **Example.** `"Modules": [ { "Name": "SpawnPacerCore", "Type": "Runtime", "LoadingPhase": "Default" } ]`.
8. **In this project.** Both modules use `Default`; the plugin is `EnabledByDefault: true`, not Beta, not Experimental (05 A5). Its world subsystems (from S5) are registered before the map loads, which is all they need.

## 5. `BuildCookRun` (RunUAT) and the packaged layout

1. **What it is.** The AutomationTool command that turns a project into a standalone game: build the game target, cook content for a platform, stage files, pack them into `.pak`/IoStore containers, and archive the result.
2. **Why it exists.** A packaged game doesn't read editor assets: content must be **cooked** (converted to platform format, editor-only data stripped).
3. **How it works.** `Engine\Build\BatchFiles\RunUAT.bat BuildCookRun …` runs UBT for the target, then `UnrealEditor-Cmd -run=Cook`, then UnrealPak, then copies the staged build to `-archivedirectory`. Flags are parsed in `Programs/AutomationTool/AutomationUtils/ProjectParams.cs` (`archivedirectory` 940, `unattended` 1053/1482, `clientconfig` 1140).
4. **When to use it.** Every headline benchmark build (never PIE or the editor).
5. **When not to.** For quick code checks: `build.bat` is enough and much faster.
6. **Common mistakes.**
   - Running the bootstrap `Build\Windows\<Name>.exe` from a script that needs the game's exit code: the real executable is `Build\Windows\<Name>\Binaries\Win64\<Name>.exe`.
   - Assets referenced only from C++ aren't cooked unless listed (`DirectoriesToAlwaysCook`).
   - Packaging a configuration the engine distribution can't build (Test on a launcher engine).
7. **Example.** `Tools\package.bat` (05 A8).
8. **In this project.** Phase 4: 516 packages cooked with 0 errors and 0 warnings; the archive is `Build\Windows\` (09 F-3).

---

## APIs added in step S1 (workload and baseline B0)

| # | API | First used in |
|---|---|---|
| 6 | `UCLASS` / `USTRUCT` / `UENUM` / `UPROPERTY` / `GENERATED_BODY` | S1 |
| 7 | `CreateDefaultSubobject`, `SetupAttachment`, `ConstructorHelpers::FObjectFinder` | S1 |
| 8 | `UWorld::SpawnActor` (plain), `FActorSpawnParameters`, collision profiles, `AActor::Destroy` | S1 |
| 9 | `FTimerManager::SetTimer` / `ClearTimer` | S1 |
| 10 | CSV profiler: `CSV_DEFINE_CATEGORY`, `CSV_CUSTOM_STAT`, `CSV_EVENT`, `CSV_METADATA` | S1 |
| 11 | `TObjectPtr`, `TSubclassOf`, `TArray`, `TConstArrayView` | S1 |
| 12 | `FPlatformTime::Cycles64` / `ToMilliseconds64` | S1 |
| 13 | `FParse`, `TAutoConsoleVariable` / `IConsoleManager`, `FRandomStream`, `FCrc` | S1 |
| 14 | `UTickableWorldSubsystem`, `ACameraActor` + `SetViewTarget`, `GFrameCounter`, `ForceGarbageCollection`, `RequestExit` | S1 |

## 6. `UCLASS` / `USTRUCT` / `UENUM` / `UPROPERTY` / `GENERATED_BODY`

1. **What it is.** Markers that Unreal Header Tool (UHT) reads before the C++ compiler runs; UHT writes the `.generated.h` code that registers the type with the reflection system.
2. **Why it exists.** C++ has no run-time type information rich enough for Unreal's needs: garbage collection (which pointers does an object hold?), the editor's details panel, Blueprint, saving, networking.
3. **How it works.** `UCLASS()` before a `UObject` subclass, `USTRUCT()` before a struct, `UENUM()` before an enum; `GENERATED_BODY()` inside pastes the generated members. `UPROPERTY(...)` marks a member reflected; specifiers decide who may see or edit it (`VisibleAnywhere`, `EditDefaultsOnly`, `VisibleInstanceOnly`, `Transient`).
4. **When to use it.** Any `UObject` pointer member (so GC sees it), anything the editor, Blueprint or the reset verifier (S8) must see.
5. **When not to.** Plain bookkeeping no other system needs (our `FRunSettings`, `WorkSink`, the drone's `Origin`): reflection costs memory and compile time.
6. **Common mistakes.** The `.generated.h` include not last; a raw `UObject*` member without `UPROPERTY` (GC can't see it and may free the object); `EditAnywhere` on default-subobject pointers (lets a designer break the actor).
7. **Example.** `UPROPERTY(VisibleInstanceOnly, Category = "SpawnPacer Demo") float Health = 100.0f;`
8. **In this project.** `ASPWaveActor` is `UCLASS(Abstract)`; the brain's gameplay state is `UPROPERTY` on purpose (the S8 verifier compares reflected state); `FSPWaveEntry` is a `USTRUCT` so the wave list can be a `UPROPERTY` array.

## 7. `CreateDefaultSubobject`, `SetupAttachment`, `ConstructorHelpers::FObjectFinder`

1. **What it is.** Constructor-time helpers: create a component as part of the class default object; record its attachment parent; find an asset.
2. **Why it exists.** An actor's components are defined once, on the class default object (CDO), and copied into each spawned instance by `NewObject` (01b §2.1).
3. **How it works.** `CreateDefaultSubobject<T>(Name)` creates a subobject with a fixed name (unique per owner). `SetupAttachment(Parent)` stores the parent; the real attachment is made at registration. `ConstructorHelpers::FObjectFinder<T>` loads an asset by path, and is only allowed inside a constructor.
4. **When to use it.** Components every instance of a class has. Assets referenced from C++ defaults.
5. **When not to.** Components added at run time (`NewObject` + `RegisterComponent`); `AttachToComponent` belongs to run time, not constructors.
6. **Common mistakes.** Two subobjects with the same name; a **non-static** `FObjectFinder` (an asset lookup in every instance's constructor); one static finder in a helper shared by several classes (the first class's asset wins); forgetting to cook assets referenced only from C++.
7. **Example.** `static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube")); Mesh->SetStaticMesh(Cube.Object);`
8. **In this project.** Each wave class keeps its own static finders and passes the meshes to `BuildBody` (05 B1); parts are named `Part00`, `Part01`, ….

## 8. `UWorld::SpawnActor` (plain), `FActorSpawnParameters`, collision profiles, `AActor::Destroy`

1. **What it is.** The engine's one way to create an actor in a world (`Private/LevelActor.cpp:456`), its options struct (`Classes/Engine/World.h:420–519`), named collision presets (`Classes/Engine/CollisionProfile.h:189–196`), and the actor-side destroy call.
2. **Why it exists.** An actor must be constructed, added to a level, registered, constructed by its scripts and begun play in a fixed order (01b §2).
3. **How it works.** A plain `SpawnActor` does all of that before it returns (01b #1–2). `SpawnCollisionHandlingOverride = AlwaysSpawn` skips the encroachment test. `Destroy()` calls `UWorld::DestroyActor`: EndPlay, unregister every component, mark as garbage (`LevelActor.cpp:839–1060`); memory is freed by a later GC.
4. **When to use it.** A few actors per frame, or when the whole cost must land now.
5. **When not to.** Hundreds in one frame: that is the hitch this project measures (B0).
6. **Common mistakes.** Ignoring a null return; using a collision method that silently moves or rejects actors in a benchmark; treating `Destroy()` as freeing memory.
7. **Example.** `World.SpawnActor<ASPWaveActor>(Entry.Class, Entry.Transform, Parameters);`
8. **In this project.** Baseline B0 in `USPBaselineSpawner` (05 B3); the drone uses `DefaultProjectile`, the crate `BlockAllDynamic`, enemies `Pawn`.

## 9. `FTimerManager::SetTimer` / `ClearTimer`

1. **What it is.** The world's timer service (`Public/TimerManager.h`).
2. **Why it exists.** Gameplay often needs "do this every N seconds" without a per-frame tick.
3. **How it works.** `SetTimer(Handle, Object, &Class::Function, Rate, bLoop)` stores a delegate bound to the object; the manager ticks once per frame and fires due timers. `ClearTimer(Handle)` removes one; `ClearAllTimersForObject(Object)` removes all of an object's (291).
4. **When to use it.** Low-frequency, time-based work.
5. **When not to.** Per-frame work (use a tick), or work that must happen at an exact frame.
6. **Common mistakes.** Assuming EndPlay or destroy clears timers: the engine doesn't (01b #11). A pooled actor's timer keeps firing in its next life.
7. **Example.** `World->GetTimerManager().SetTimer(ThinkTimer, this, &USPWaveBrainComponent::Think, 0.5f, true);`
8. **In this project.** The enemies' 0.5 s think timer, cleared by the brain in its own `EndPlay` (05 B2); the pool will clear all timers unconditionally (S7).

## 10. CSV profiler: `CSV_DEFINE_CATEGORY`, `CSV_CUSTOM_STAT`, `CSV_EVENT`, `CSV_METADATA`

1. **What it is.** Macros that write one row per frame to a CSV capture (`Core/Public/ProfilingDebugging/CsvProfiler.h:50, 56, 85, 155`).
2. **Why it exists.** Frame-by-frame numbers for whole runs, cheap enough for packaged Development builds, readable by scripts.
3. **How it works.** `CSV_DEFINE_CATEGORY(Name, bOnByDefault)` once per `.cpp`; `CSV_CUSTOM_STAT(Category, Stat, Value, ECsvCustomStatOp::Set)` sets this frame's value of a column `Category/Stat`; `CSV_EVENT(Category, Text)` adds `Category/Text` to the frame's `EVENTS` column; `CSV_METADATA(Key, Value)` adds a key to the file's last row. The capture starts and stops on events (`-csvStartOnEvent=`, `-csvStopOnEvent=`, 01b §3).
4. **When to use it.** Per-frame totals and run markers.
5. **When not to.** Per item inside a hot loop (about 50 ns per call, UE5-UpdateBudget's measurement); fine-grained timing (use Insights).
6. **Common mistakes.** Publishing per actor; reading the first header row instead of the end header (columns first written mid-capture exist only there, 01b B-2).
7. **Example.** `CSV_CUSTOM_STAT(SPBaseline, WorkMs, WorkMs, ECsvCustomStatOp::Set);`
8. **In this project.** `SPBaseline/*` per frame, `SPBench/*` events cut the analysis windows (01 §6.3), metadata makes every capture self-describing.

## 11. `TObjectPtr`, `TSubclassOf`, `TArray`, `TConstArrayView`

1. **What it is.** Engine containers and pointer types.
2. **Why it exists.** `TObjectPtr<T>` is UE5's pointer type for `UPROPERTY` object members (access tracking in editor builds, a plain pointer in packaged builds); `TSubclassOf<T>` is a `UClass*` restricted to subclasses of `T`; `TArray` is the dynamic array; `TConstArrayView` is a read-only pointer-plus-count view.
3. **How it works.** `TSubclassOf` checks the class on assignment; `TConstArrayView` converts implicitly from a `TArray`; `TArray::Reserve` allocates once; `Reset` empties but keeps the allocation; `AddDefaulted_GetRef` appends and returns a reference.
4. **When to use it.** `TObjectPtr` for every reflected object member; views for passing arrays without copying.
5. **When not to.** A view must not outlive the array it points into.
6. **Common mistakes.** Storing a `TConstArrayView` as a member; keeping references into a `TArray` while adding to it (it may reallocate).
7. **Example.** `void RequestWave(TConstArrayView<FSPWaveEntry> Entries) { Pending.Append(Entries.GetData(), Entries.Num()); }`
8. **In this project.** `Live`, `Brain`, `Camera`, `Baseline` are `TObjectPtr`; `FSPWaveEntry::Class` is a `TSubclassOf<ASPWaveActor>`.

## 12. `FPlatformTime::Cycles64` / `ToMilliseconds64`

1. **What it is.** The CPU's high-resolution counter and its conversion (`Core/Public/GenericPlatform/GenericPlatformTime.h:202`).
2. **Why it exists.** Timing short pieces of work with sub-microsecond resolution.
3. **How it works.** Two reads of a monotonic counter; the difference times the counter period.
4. **When to use it.** Timing a section of game-thread work by our own code.
5. **When not to.** Simulated time (use world time); overlapping threads (each thread's time is separate).
6. **Common mistakes.** Mixing it with `FPlatformTime::Seconds()` differences in one statistic; converting through `float` and losing precision.
7. **Example.** `const uint64 Start = FPlatformTime::Cycles64(); … ToMilliseconds64(FPlatformTime::Cycles64() - Start);`
8. **In this project.** Every spawner (baselines now, SpawnPacer from S5) times its own work with it, so the measurement cost is equal (02 §9).

## 13. `FParse`, `TAutoConsoleVariable` / `IConsoleManager`, `FRandomStream`, `FCrc`

1. **What it is.** Command-line parsing, console variables, a seeded random stream, CRC checksums.
2. **Why it exists.** Repeatable, scriptable runs: settings from the command line, one tunable value for all brains, the same random wave for the same seed, a hash to prove it.
3. **How it works.** `FParse::Value(CommandLine, TEXT("Key="), Out)` reads `-Key=value`; `FParse::Param` a bare flag. `TAutoConsoleVariable<int32>` registers a cvar at start-up; `IConsoleManager::Get().FindConsoleVariable(Name)->Set(Value, ECVF_SetByCommandline)` sets it from code. `FRandomStream(Seed)` produces the same sequence for the same seed, independent of `FMath::Rand`. `FCrc::StrCrc32` / `TypeCrc32(Value, Crc)` chain a checksum.
4. **When to use it.** Anything a benchmark must vary or reproduce.
5. **When not to.** `ECVF_Cheat` for benchmark switches (can't be set from config or `-dpcvars`, UE5-UpdateBudget 01b #11).
6. **Common mistakes.** `FMath::Rand` in a benchmark (global, shared state); a hash over values that differ by floating-point noise.
7. **Example.** `FRandomStream Random(Settings.Seed); Wave.Swap(Index, Random.RandRange(0, Index));`
8. **In this project.** `-SP…=` settings, `sp.Demo.BeginPlayWork`, the seeded wave and its hash (05 B4).

## 14. `UTickableWorldSubsystem`, `ACameraActor` + `SetViewTarget`, `GFrameCounter`, `ForceGarbageCollection`, `RequestExit`

1. **What it is.** A world subsystem that also ticks (`Engine/Public/Subsystems/WorldSubsystem.h:80`); a camera actor and the player controller's view target; the global frame counter; the GC trigger (`Classes/Engine/Engine.h:2828`); the exit request (`Core/Public/GenericPlatform/GenericPlatformMisc.h:1086`).
2. **Why it exists.** A per-world service with per-frame work; a fixed viewpoint; frame numbering for the report; a scripted GC; leaving when a run is done.
3. **How it works.** World subsystems are created with the world; `DoesSupportWorldType` filters world types (default Game, Editor and PIE, `Private/Subsystems/WorldSubsystem.cpp:29–32`); `OnWorldBeginPlay` runs before actors begin play (`Private/World.cpp:6167, 6174`); `Tick` runs once per frame after `TG_PostPhysics` (`Private/LevelTick.cpp:1778–1821`) and needs `GetStatId`. `ForceGarbageCollection(true)` only arms a full purge (`Private/UnrealEngine.cpp:2136–2140`). `RequestExit(false, CallSite)` asks the engine to quit at the end of the frame.
4. **When to use it.** Per-world managers with no map placement.
5. **When not to.** Two tickable subsystems that must run in a fixed order: there is no guaranteed order (new tickables wait in a `TMap`, `Public/Tickable.h:62`) — drive one from the other.
6. **Common mistakes.** Forgetting `DoesSupportWorldType` (the subsystem also runs in editor worlds); expecting `ForceGarbageCollection` to collect immediately.
7. **Example.** `RETURN_QUICK_DECLARE_CYCLE_STAT(USPBenchmarkSubsystem, STATGROUP_Tickables);`
8. **In this project.** The director (05 B4).

---

## APIs added in step S3 (baselines B1–B3)

| # | API | First used in |
|---|---|---|
| 15 | `SpawnActorDeferred` (`bDeferConstruction`) + `AActor::FinishSpawning` | S3 (B1) |
| 16 | `SetActorHiddenInGame`, `SetActorEnableCollision`, `SetActorTickEnabled`, `SetActorTransform` + `ETeleportType` | S3 (B3) |
| 17 | `TOptional` | S3 |
| 18 | A `UPROPERTY` `TMap` with a struct value | S3 (B3) |

## 15. `SpawnActorDeferred` (`bDeferConstruction`) + `AActor::FinishSpawning`

1. **What it is.** A spawn that stops before the construction script (`Classes/Engine/World.h:3851–3871`, `bDeferConstruction` at 476), finished later by `FinishSpawning` (`Private/Actor.cpp:4374`).
2. **Why it exists.** So the caller can set properties after construction and before the construction script and BeginPlay see them.
3. **How it works.** `SpawnActor` with `bDeferConstruction = true` still does construction **and native component registration** (`Actor.cpp:4333–4337`); it skips only `FinishSpawning`. `FinishSpawning` then runs the construction script, initialisation and BeginPlay in one call (`Actor.cpp:4415–4529`). It may be called on a later frame; it must be called exactly once (`ensure(!bHasFinishedSpawning)`, 4381).
4. **When to use it.** Setting spawn parameters before BeginPlay; splitting a spawn at the engine's deferred seam (01b §0).
5. **When not to.** Believing it defers the expensive part: registration has already happened (01b #1).
6. **Common mistakes.** Never calling `FinishSpawning` (the actor never begins play); calling it twice; a different transform at `FinishSpawning` without knowing the engine reconciles it with the spawn transform (`Actor.cpp:4386–4409`).
7. **Example.** `Parameters.bDeferConstruction = true; AActor* A = World.SpawnActor<AActor>(Class, T, Parameters); /* set things */ A->FinishSpawning(T);`
8. **In this project.** B1 (05 D1), timed as two halves (`ConstructMs`, `FinishMs`); SpawnPacer's staged spawn from S5.

## 16. `SetActorHiddenInGame`, `SetActorEnableCollision`, `SetActorTickEnabled`, `SetActorTransform` + `ETeleportType`

1. **What it is.** The actor-level switches a pool uses (`Classes/GameFramework/Actor.h:1922, 1926, 2898, 1800`).
2. **Why it exists.** Turning an actor's visibility, collision and tick on and off without destroying it.
3. **How it works.** Hidden and collision apply to the actor's components; `SetActorTickEnabled` touches **only** the actor's own tick (`Private/Actor.cpp:1754–1760`); `SetActorTransform(T, bSweep, OutHit, ETeleportType::ResetPhysics)` moves the actor and resets any physics velocity instead of sweeping.
4. **When to use it.** Parking and reusing pooled actors.
5. **When not to.** Expecting them to stop everything: component ticks, timers and gameplay state keep going (01b #11–12).
6. **Common mistakes.** Forgetting component ticks; teleporting without `ResetPhysics`; reusing an actor whose code captured its old position at BeginPlay (the drone, 05 D2).
7. **Example.** `Actor->SetActorHiddenInGame(true); Actor->SetActorEnableCollision(false); Actor->SetActorTickEnabled(false);`
8. **In this project.** B3's `Park` and reuse (05 D1); SpawnPacer's pool adds component ticks, timers, latent actions and verification (02 §11).

## 17. `TOptional`

1. **What it is.** A value that may be absent (`Core/Public/Misc/Optional.h`).
2. **Why it exists.** "No value" without a magic number or a separate flag.
3. **How it works.** `IsSet()`, `GetValue()` (asserts if empty), `*Opt`, and `if (const TOptional<T> X = F())` tests and binds in one line; `return {};` returns an empty one.
4. **When to use it.** Parsing that can fail; optional results.
5. **When not to.** Where an empty value is a programming error that should assert at once.
6. **Common mistakes.** `GetValue()` on an empty optional (an assertion).
7. **Example.** `if (const TOptional<ESPBaseline> Mode = ParseBaseline(Config)) { Configure(*Mode, ...); }`
8. **In this project.** `ParseBaseline` (05 D3); the scheduler's `while (const TOptional<FSpawnCostKey> Head = PeekHead())` (05 E2).

## 18. A `UPROPERTY` `TMap` with a struct value

1. **What it is.** A reflected map, here `TMap<TObjectPtr<UClass>, FSPPooledActors>`.
2. **Why it exists.** GC must see the actors in the map; only reflected containers are visible to it.
3. **How it works.** Keys and values must be reflectable types; a value can't be a `TArray` directly, so the array is wrapped in a `USTRUCT` with a `UPROPERTY` array. `FindOrAdd(Key)` returns a reference to a new or existing value; `Find(Key)` returns a pointer or null.
4. **When to use it.** Per-key collections of objects that must stay alive.
5. **When not to.** Hot per-frame lookups over many keys (a `TMap` lookup hashes the key).
6. **Common mistakes.** A raw, non-`UPROPERTY` map of object pointers (GC can't see them); `TArray` as the value type (UHT refuses it).
7. **Example.** `Pool.FindOrAdd(Class).Actors.Add(Actor);`
8. **In this project.** B3's pool (05 D1); SpawnPacer's per-class pools from S7.

## APIs added in step S4 (pure policy + unit tests)

| # | API | First used in |
|---|---|---|
| 19 | Automation tests: `IMPLEMENT_SIMPLE_AUTOMATION_TEST`, `EAutomationTestFlags`, `WITH_DEV_AUTOMATION_TESTS`, `Automation RunTests` | S4 |
| 20 | `TObjectKey` / `FObjectKey` | S4 |
| 21 | `TFunctionRef` | S4 |

## 19. Automation tests: `IMPLEMENT_SIMPLE_AUTOMATION_TEST`, `EAutomationTestFlags`, `WITH_DEV_AUTOMATION_TESTS`, `Automation RunTests`

1. **What it is.** The engine's unit-test framework (`Core/Public/Misc/AutomationTest.h`). `IMPLEMENT_SIMPLE_AUTOMATION_TEST(FClass, "Path.Of.Test", Flags)` (4297) declares a test class and registers it; you write `bool FClass::RunTest(const FString& Parameters)`.
2. **Why it exists.** Tests that run inside the engine, in the editor or a game, from the Session Frontend or the command line.
3. **How it works.** Registration happens through a static object, so a test exists as soon as its module is loaded. `TestEqual(What, Actual, Expected, Tolerance)` (1989 for `double`), `TestTrue`, `TestNotNull` (2449) record an error and return false on failure; the test fails if any error was recorded. The flags need at least one context (`EditorContext`, `ClientContext`, …, `AutomationTest.h:88–95`) and **exactly one** filter (`ProductFilter`, `EngineFilter`, …, 131–133), checked by `static_assert` (4127–4133). `WITH_DEV_AUTOMATION_TESTS` is set by UBT per configuration (`UEBuildTarget.cs:6331`): on in Development, off in Shipping. From the command line, `-ExecCmds="Automation RunTests <prefix>;Quit"` runs every test whose path starts with the prefix; `Quit` waits for them, then exits with −1 if any report had errors, else 0 (`Developer/AutomationController/Private/AutomationCommandline.cpp:493–504`). Each result is logged as `Test Completed. Result={…} Name={…} Path={…}` (`AutomationControllerManager.cpp:43, 57`).
4. **When to use it.** Pure logic (this step) and, with a world, functional checks (S5 onwards).
5. **When not to.** Timing claims: a test proves behaviour, a benchmark proves cost.
6. **Common mistakes.** Two filters in the flags (a compile error); test code outside `#if WITH_DEV_AUTOMATION_TESTS`; trusting exit code 0 without checking which tests ran; dereferencing a pointer the test should have checked with `TestNotNull` (a crash ends the whole run).
7. **Example.** `IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMyTest, "SpawnPacer.Policy.Example", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter) bool FMyTest::RunTest(const FString&) { TestEqual(TEXT("sum"), 1 + 1, 2); return true; }`
8. **In this project.** `SpawnPacerPolicyTests.cpp` (05 E3), run by `Tools\run_tests.py` (05 E4).

## 20. `TObjectKey` / `FObjectKey`

1. **What it is.** A key that identifies a `UObject` by its slot in the global object array and that slot's serial number (`CoreUObject/Public/UObject/ObjectKey.h:18, 221`), not by its address.
2. **Why it exists.** A raw pointer can't tell an object from a new one that later got the same address; a `TObjectKey` can, because a reused slot gets a new serial number.
3. **How it works.** Built implicitly from a pointer (it goes through `FWeakObjectPtr`); `==` and `GetTypeHash` compare index and serial; `ResolveObjectPtr()` returns the object or null if it is gone. It doesn't keep the object alive and is invisible to GC.
4. **When to use it.** Map keys about objects you don't own: caches, statistics per class or per actor.
5. **When not to.** When you need to keep the object alive (use a `UPROPERTY` `TObjectPtr`) or access it often (resolving costs a lookup).
6. **Common mistakes.** Using a raw `UObject*` or `UClass*` as a long-lived map key: after the object is destroyed, a new one at the same address is taken for it.
7. **Example.** `TMap<TObjectKey<UClass>, double> CostByClass; CostByClass.Add(Actor->GetClass(), 0.4);`
8. **In this project.** `FSpawnCostKey::Class` (05 E1): a Blueprint class recompiled between PIE sessions mustn't inherit the old one's cost.

## 21. `TFunctionRef`

1. **What it is.** A non-owning reference to any callable with a given signature (`Core/Public/Templates/Function.h:556`), like C++26 `std::function_ref`.
2. **Why it exists.** To accept "a function" without making the callee a template and without the allocation an owning `TFunction` may need.
3. **How it works.** It stores a pointer to the callable and a pointer to a small function that calls it. It is valid only as long as the callable it was made from.
4. **When to use it.** A parameter that is called during the call and not kept.
5. **When not to.** Anything stored for later: delegates, members, async work (use `TFunction` or a delegate).
6. **Common mistakes.** Storing it in a member or a container; making one from a temporary lambda and using it after the statement ends.
7. **Example.** `static FSpawnPacerFrameStats RunFrame(double BudgetMs, FSpawnCostModel& Model, TFunctionRef<TOptional<FSpawnCostKey>()> PeekHead, TFunctionRef<double()> RunHead);`
8. **In this project.** `FSpawnPacerScheduler::RunFrame` (05 E2): the policy calls the mechanism through two borrowed functions, so the tests can pass fake jobs.
