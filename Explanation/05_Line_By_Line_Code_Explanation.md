# 05 · Line By Line Code Explanation: SpawnPacer

**Rule:** every file that exists in `SpawnPacer\` and `Tools\` is explained here, line by line, in the same step it is written. Sections follow the build order in [03_Implementation_Roadmap.md](03_Implementation_Roadmap.md).

| Part | Step | Files |
|---|---|---|
| A | Phase 4, F1–F4 (foundation) | `.uproject`, two `Target.cs`, two `Build.cs`, two module `.cpp`, `.uplugin`, three config `.ini`, `Tools\*.bat`, `Tools\perf_cores.py` |
| B | S1 (workload, B0 only) | `SPWaveActor`, `SPWaveBrainComponent`, `SPBaselineSpawner`, `SPBenchmarkSubsystem`, `DefaultGame.ini` |
| C | S2 (measurement tools) | `Tools\csv_stats.py`, `Tools\run_bench.py` |
| D | S3 (baselines B1–B3) | `SPBaselineSpawner` (rewritten), S3 additions to `SPWaveActor`, `SPWaveBrainComponent`, `SPBenchmarkSubsystem` |
| E | S4 (pure policy + unit tests) | `SpawnCostModel`, `SpawnPacerScheduler`, `Tests\SpawnPacerPolicyTests.cpp`, `Tools\run_tests.py`, `run_and_reap` in `Tools\run_bench.py` |
| F… | S5 onwards | added with each step |

---

## Part A · Foundation (Phase 4)

**State after Phase 4 (2026-11-21):** the project builds as an editor target and as a packaged game, loads the engine's template map and quits cleanly. There is no system code yet: the module and plugin contain only their entry points. The empty `Public\` folders and `Content\` exist on disk but are not in git (git doesn't store empty folders); they fill in from S1.

### A1. `SpawnPacer.uproject`: the project descriptor

```json
{
	"FileVersion": 3,
	"EngineAssociation": "5.8",
	"Category": "",
	"Description": "P02 SpawnPacer: hitch-free content entry (UE 5.8.4 portfolio project).",
	"Modules": [ { "Name": "SpawnPacer", "Type": "Runtime", "LoadingPhase": "Default" } ],
	"Plugins": [
		{ "Name": "SpawnPacerCore", "Enabled": true },
		{ "Name": "AndroidFileServer", "Enabled": false }
	]
}
```

| Line | What it does, and why |
|---|---|
| `"FileVersion": 3` | The descriptor schema version. 3 is what Epic's 5.8 `TP_Blank` template uses (`Templates/TP_Blank/TP_Blank.uproject`). A wrong value makes the editor refuse or upgrade the file |
| `"EngineAssociation": "5.8"` | Which installed engine opens the project. The descriptor reader stores this string (`Runtime/Projects/Private/ProjectDescriptor.cpp:125`). A launcher engine is identified by its version. The template leaves it empty, which makes the launcher ask on first open |
| `"Category"`, `"Description"` | Shown in the project browser. Cosmetic |
| `"Modules"` | The C++ modules the **project** owns. UBT builds these, plus the modules of enabled plugins |
| `"Name": "SpawnPacer"` | Must match the folder `Source\SpawnPacer\` and the class in `SpawnPacer.Build.cs`. Module names are unique across the engine and project, which is why the plugin is called `SpawnPacerCore` and not `SpawnPacer` |
| `"Type": "Runtime"` | Built into both game and editor targets. *Alternative:* `Editor`, which would leave the code out of the packaged game we benchmark |
| `"LoadingPhase": "Default"` | "Loaded at the default loading point during startup (during engine init, after game modules are loaded.)" (`Runtime/Projects/Public/ModuleDescriptor.h:46–47`). That is before the first map loads, so the module's classes (world subsystems from S1) exist when the world is created |
| `"Plugins"` / `"SpawnPacerCore"` | Enables the project's own plugin explicitly. The plugin is `EnabledByDefault`, so it would load anyway; listing it keeps the dependency visible in one place and safe if that default changes |
| `"AndroidFileServer"`, `"Enabled": false` | Switches off an engine plugin that is on by default (`Engine/Plugins/Runtime/AndroidFileServer/AndroidFileServer.uplugin:15`). Its editor module's settings object, on first load, replaces the placeholder `[AUTO]` with a new random GUID `SecurityToken` and writes it into the project's `DefaultEngine.ini` (`TryUpdateDefaultConfigFile`, `Engine/Plugins/Runtime/AndroidFileServer/Source/AndroidFileServerEditor/Private/AndroidFileServerRuntimeSettings.cpp:27–37`). That happened during the first Phase 4 cook. The token only matters for Epic's Android device file server, which this Windows-only project never uses, but a random credential-like value doesn't belong in a public repository, and a file the engine rewrites by itself would show up as noise in every diff. Disabling the plugin removes the writer, and the block was deleted (A7). The check that the cook no longer writes it is 09 F-5 |
| *Not listed:* `ModelingToolsEditorMode` | The template enables it for editor modelling tools. This project builds everything from code, so the line is left out |

### A2. `Source\SpawnPacer.Target.cs` and `SpawnPacerEditor.Target.cs`: build targets

```csharp
// P02 SpawnPacer - game target (standalone/packaged builds).

using UnrealBuildTool;

public class SpawnPacerTarget : TargetRules
{
	public SpawnPacerTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("SpawnPacer");
	}
}
```

| Line | What it does, and why |
|---|---|
| `// P02 SpawnPacer - game target …` | Says which project and executable this file describes |
| `using UnrealBuildTool;` | Target files are **C#**, compiled by UBT itself (04 §5). This brings `TargetRules`, `TargetType` and the version enums into scope |
| `public class SpawnPacerTarget : TargetRules` | UBT finds targets by class name: `<Name>Target` defines the target `<Name>`. So `SpawnPacerTarget` is the target `SpawnPacer`, and `build.bat SpawnPacer` works |
| `(TargetInfo Target) : base(Target)` | UBT passes in the platform, configuration and architecture; the base constructor stores them |
| `Type = TargetType.Game` | A standalone game executable with no editor code, the target that gets packaged and benchmarked. The editor file sets `TargetType.Editor`, which builds DLLs loaded by `UnrealEditor.exe` |
| `DefaultBuildSettings = BuildSettingsVersion.V7` | Opts into this engine's newest default build settings. In 5.8.4, `V7` is also `Latest` (`Programs/UnrealBuildTool/Configuration/Rules/TargetRules.cs:170, 177`), and the `TP_Blank` template uses it. Naming the version, rather than `Latest`, means a future engine upgrade doesn't silently change our build settings |
| `IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8` | Uses 5.8's engine header include order (`TargetRules.cs:235, 242`). Epic trims indirect includes each version; pinning the order means an upgrade can't silently change what our code sees |
| `ExtraModuleNames.Add("SpawnPacer")` | The project modules this target builds. Plugin modules come in through the plugin system |

**Why two target files?** The editor and the packaged game are different executables with different module sets, so UBT needs one rule per executable. The editor file is identical except for the class name (`SpawnPacerEditorTarget`), the comment and `TargetType.Editor`.

### A3. `Source\SpawnPacer\SpawnPacer.Build.cs`: the game module's build rules

```csharp
public class SpawnPacer : ModuleRules
{
	public SpawnPacer(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });

		// SpawnPacerCore is private for now: no public header of this module includes a plugin header yet.
		// It becomes public in the step where a public header first does (rule in 05_Line_By_Line_Code_Explanation.md A3).
		PrivateDependencyModuleNames.AddRange(new string[] { "SpawnPacerCore" });
	}
}
```

| Line | What it does, and why |
|---|---|
| Header comments (two lines) | State the module's job (demo and benchmark only) and the dependency rule (game → plugin, never the reverse, 02 §3) |
| `class SpawnPacer : ModuleRules` | The class name **must equal the module name**; UBT maps each `*.Build.cs` to its module |
| `ReadOnlyTargetRules Target` | A module may read the target's settings but not change them, so module rules can't fight each other |
| `PCHUsage = UseExplicitOrSharedPCHs` | Use a **shared precompiled header** built for a common dependency set, unless the module names its own (`ModuleRules.cs:217–220`). The build log shows `SharedPCH.UnrealEd.Project…` (editor) and `SharedPCH.Engine.Project…` (game). *Rejected:* `NoPCHs` (every file compiles all its headers from scratch) or a private PCH (not worth it for a small module) |
| `PublicDependencyModuleNames …` | Modules whose headers **our public headers** will include, so anyone depending on us gets their include paths too: `Core` (containers, strings, logging), `CoreUObject` (UObject, reflection, GC), `Engine` (actors, components, worlds). From S1 our public headers declare actor and subsystem classes, so these three are public |
| `PrivateDependencyModuleNames … "SpawnPacerCore"` | **The rule: private unless a public header needs it.** Today only `.cpp` files will use the plugin. When S5 adds a public director header that names a plugin type, this line moves to the public list in the same step. Making everything public "just in case" spreads include paths and widens rebuilds |
| *Not included:* `InputCore`, `EnhancedInput` | The template adds them, but this project reads no input in code. An honest dependency list is part of modular architecture |

### A4. `Source\SpawnPacer\Private\SpawnPacerModule.cpp`

```cpp
// P02 SpawnPacer - primary game module registration.

#include "Modules/ModuleManager.h"

// The game module needs no startup logic, so the engine's default implementation is enough.
IMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl, SpawnPacer, "SpawnPacer");
```

| Line | What it does, and why |
|---|---|
| `#include "Modules/ModuleManager.h"` | Declares `IMPLEMENT_PRIMARY_GAME_MODULE` and `FDefaultGameModuleImpl`. Only this header, nothing larger (04 §1) |
| `IMPLEMENT_PRIMARY_GAME_MODULE(…)` | Generates the module's entry point (04 §2, §3). **In the monolithic packaged build** (`Core/Public/Modules/ModuleManager.h:1097–1126`) it also defines the globals `GInternalProjectName`, set from `UE_PROJECT_NAME` (which UBT passes in from the `.uproject`, 1102), and `GIsGameAgnosticExe = false` (1104). In the modular editor build (1130) it registers the module and the project name comes from the command line. *Primary* means "the project's main game module"; a project needs exactly one. *Without it:* the module never registers, and the packaged game has no project-name global to start from |
| `FDefaultGameModuleImpl` | An engine class with empty startup and shutdown whose `IsGameModule()` returns true (`ModuleManager.h:892–902`). We have no startup logic, so writing our own module class would be a class with no reason |
| `SpawnPacer` | The module name; must match the `.uproject` and `Build.cs` |
| `"SpawnPacer"` | The old "game name" argument. In 5.8 it is ignored: the monolithic definitions name it `DEPRECATED_GameName` (1100, 1114), and Epic's comment says specifying it "is no longer necessary" (1096). The macro still takes three arguments, so we pass the project name, matching the template |

**Why there is no `SpawnPacer.h`:** the template ships an empty module header that nothing includes. An empty file adds nothing, so it is left out (UE5-UpdateBudget made the same choice).

### A5. `Plugins\SpawnPacerCore\SpawnPacerCore.uplugin`: the plugin descriptor

| Field | Value | Why |
|---|---|---|
| `FileVersion`, `Version`, `VersionName` | 3, 1, "1.0" | The same schema as Epic's plugins in 5.8 (e.g. `Plugins/Runtime/SignificanceManager/SignificanceManager.uplugin:2`) |
| `FriendlyName`, `Description`, `Category` | "Spawn Pacer Core", one sentence on predicted-cost pacing and a checked pool, "Performance" | Shown in the editor's Plugins window. The description states the two claims the project makes, not more |
| `CreatedBy` … `SupportURL` | empty | No author or links inside the plugin; the repository README carries those |
| `EnabledByDefault` | `true` | Any project that copies the plugin gets it working without editing its `.uproject` (later projects reuse it this way, 02 §12) |
| `CanContainContent` | `false` | Code only. Saying so avoids an empty content mount point |
| `IsBetaVersion`, `IsExperimentalVersion` | `false`, `false` | No warning badge. That describes the code's stability; it is not a claim of production readiness |
| `Installed` | `false` | Part of the project, not installed into the engine |
| `Modules[0]` | `SpawnPacerCore`, `Runtime`, `Default` | One runtime module, loaded at the default phase. The game module depends on it, so it is loaded first anyway |

### A6. `SpawnPacerCore.Build.cs` and `Private\SpawnPacerCoreModule.cpp`

- **`Build.cs`:** the same pattern as A3, with `Core`, `CoreUObject` and `Engine` as **public** dependencies. The plugin's public headers will expose engine types (`UTickableWorldSubsystem`, `AActor`, `FActorSpawnParameters`), so any module that includes them needs those include paths. `DeveloperSettings` is added in S9, when the settings class exists, not before. Its header comment lists the module's job and the rule that it must never depend on a game module.
- **`IMPLEMENT_MODULE(FDefaultModuleImpl, SpawnPacerCore);`** is the non-primary version of A4: an ordinary module entry point (`ModuleManager.h:943–971`). `FDefaultModuleImpl` is the empty default implementation (884–886). A plugin module must not use the primary-game variant: a project has only one primary module.

The build-log line `[Adaptive Build] Excluded from SpawnPacerCore unity file: SpawnPacerCoreModule.cpp` is normal. UBT compiles files that git reports as changed on their own instead of inside the "unity" batch file, so editing one file doesn't recompile its neighbours.

### A7. `Config\DefaultEngine.ini`, `DefaultGame.ini`, `DefaultInput.ini`

**`DefaultEngine.ini`**

| Section / key | Why |
|---|---|
| `GameMapsSettings: GameDefaultMap / EditorStartupMap = /Engine/Maps/Templates/Template_Default` | A shipped engine map with a floor and lights, so no map has to be authored in the editor and the project stays buildable from text files. The template's `OpenWorld` map was rejected: it is World Partition, and streaming would add exactly the kind of noise this project measures |
| `[/Script/Engine.Engine] bSmoothFrameRate=False, bUseFixedFrameRate=False` | Both are engine defaults. **Restated** so nobody can switch on smoothing or a frame cap later without it showing in a diff. Either one would make a spawn hitch look shorter than it was |
| `[SystemSettings] r.VSync=0` | Also a default, restated: VSync makes Present wait for the monitor, which would quantise frame times |
| `RendererSettings`, `WindowsTargetSettings` | Copied from Epic's 5.8 template: DX12, Shader Model 6, virtual shadow maps, no static lighting. Standard modern defaults, not benchmark choices |
| *Removed:* an engine-written `[/Script/AndroidFileServerEditor.AndroidFileServerRuntimeSettings]` block | The Android File Server editor plugin appended it, with a random `SecurityToken`, the first time the editor ran on this project (the first Phase 4 cook). The plugin is now disabled in the `.uproject` (A1), so nothing writes it again; the block was deleted. The later cooks in 09 F-5 confirm it is not re-written |

**`DefaultGame.ini`:** the project name and description. From S1 it will also list `/Engine/BasicShapes` under `DirectoriesToAlwaysCook`, because the demo meshes are referenced only from C++ and the cooker would otherwise leave them out.

**`DefaultInput.ini`** (added after the first smoke run):

```ini
[/Script/Engine.InputSettings]
DefaultPlayerInputClass=/Script/EnhancedInput.EnhancedPlayerInput
DefaultInputComponentClass=/Script/EnhancedInput.EnhancedInputComponent
```

| Line | Why |
|---|---|
| The section | `UInputSettings`'s config section; `DefaultPlayerInputClass` is its property (`Runtime/Engine/Classes/GameFramework/InputSettings.h:219`) |
| `DefaultPlayerInputClass=…EnhancedPlayerInput` | The first packaged run logged one warning: "UEnhancedInputLocalPlayerSubsystem … does not have a valid PlayerInput object. Failed to load user settings." It comes from `Plugins/EnhancedInput/Source/EnhancedInput/Private/EnhancedInputSubsystems.cpp:115–119`, which needs an Enhanced Input player-input object. The Enhanced Input plugin is enabled by default (`EnhancedInput.uplugin:13`), but without this key the engine creates the classic `UPlayerInput`. The 5.8 template sets exactly these two keys (`Templates/TP_Blank/Config/DefaultInput.ini:79–80`) |
| `DefaultInputComponentClass=…` | The matching input component class, as the template sets it |
| *Not copied:* the rest of the template's file | Axis configs and touch settings for input this project never reads. *Alternative rejected:* disabling the Enhanced Input plugin in the `.uproject`. It is a default engine plugin that other plugins may expect, and the template's two keys are the smaller change |

### A8. `Tools\build.bat`, `package.bat`, `smoke_game.bat`

All three: `@echo off`, a header comment with usage, `setlocal` (variables don't leak into the caller), `%~dp0` (the script's own folder, so they work from any working directory), quoted paths (the folder name contains a space), and `exit /b %ERRORLEVEL%` (the caller, here the machine queue, sees the real exit code). All three are run **only through `tools\ue_queue.py`**, never directly, because one machine serves ten projects (stated in each header). The files are stored with CRLF line endings (`.gitattributes`): `cmd.exe` can fail to find `goto` labels in LF-only batch files.

| Script | Key lines | Why |
|---|---|---|
| `build.bat [Target] [Config]` | `call "%UE_ROOT%\Engine\Build\BatchFiles\Build.bat" %TARGET% Win64 %CONFIG% -Project="%PROJECT%" -WaitMutex -NoHotReloadFromIDE` | A reproducible command-line build. `-WaitMutex`: "Wait for another instance to finish and then start, rather than aborting immediately" (`Programs/UnrealBuildTool/GlobalOptions.cs:75–76`). `-NoHotReloadFromIDE` turns off IDE hot reload for this build (`Configuration/BuildConfiguration.cs:160`). Defaults: `SpawnPacerEditor`, `Development`. The comment notes Test can't be built by the launcher engine |
| `package.bat [Config]` | `call "…\RunUAT.bat" BuildCookRun -project=… -noP4 -utf8output -unattended -platform=Win64 -clientconfig=%CONFIG% -build -cook -stage -pak -archive -archivedirectory=…` | Build, cook, stage, pak and archive a standalone game into `..\Build\Windows\` (git-ignored). `-unattended` "assumes no operator is present" and `-archivedirectory`/`-clientconfig` are parsed by `Programs/AutomationTool/AutomationUtils/ProjectParams.cs:1482, 940, 1140`. The default is **Development**, because the launcher engine can't build Test and Shipping has no CSV profiler |
| `smoke_game.bat` (uncooked) | `UnrealEditor-Cmd.exe <uproject> -game -nullrhi -nosound -unattended -nosplash -traceautostart=0 -ExecCmds="quit" -log -abslog=…` | The uncooked game with no window or GPU: loads the default map and quits. `-ExecCmds` is parsed at startup (`Runtime/Engine/Private/UnrealEngine.cpp:2552`). `-traceautostart=0` turns off trace auto-start, "whether to start tracing automatically at start or wait to initiate via Console Command" (`Runtime/Core/Private/ProfilingDebugging/TraceAuxiliary.cpp:265–268`, parsed at 2378), so a running Unreal Insights doesn't start recording the run. `%*` passes extra arguments through |
| `smoke_game.bat packaged` | `…\Build\Windows\SpawnPacer\Binaries\Win64\SpawnPacer.exe -windowed -ResX=1280 -ResY=720 -nosound -unattended -traceautostart=0 -ExecCmds="quit" -log -abslog=…` | The packaged game on the **real GPU** at the benchmark resolution. It starts the **real** game executable, not the bootstrap `Build\Windows\SpawnPacer.exe`, so the exit code is the game's own (the benchmark launcher does the same, A9) |
| *Not passed:* `-ExitAfterSeconds=0` | UE5-UpdateBudget's smoke script passes it, but **no `.cpp` or `.h` file under `Engine/Source` parses it** in 5.8.4 (grep, 01b §3), so it does nothing. We don't pass flags we can't find in the source |

### A9. `Tools\perf_cores.py`

The `KERNEL32` setup and the three functions are copied unchanged from UE5-UpdateBudget's `Tools\run_bench.py` (a `diff` of each body is empty), as the portfolio protocol requires: each repository stands alone, so code is copied, never imported across project folders. `run_bench.py` (S2) will import it from the same folder.

| Lines | What they do, and why |
|---|---|
| Module docstring | Why pinning is needed (a hybrid CPU; UE5-UpdateBudget's runs 1.6× apart when the game thread landed on an E-core), and **how** a batch uses it: pin the batch script itself once, record the read-back mask, start each real game exe with `subprocess.Popen`, wait with a timeout, kill in a `finally`. It also records why `cmd /c start /wait /affinity` is not used: it always returns exit code 0, so failed runs would look successful (found by UE5-TestTrust) |
| `from __future__ import annotations` | Lets the annotations `int \| None` work on any Python 3.7+ |
| `KERNEL32 = ctypes.WinDLL("kernel32", use_last_error=True)` and the `restype`/`argtypes` lines | Loads the Windows kernel library once, only on Windows (`None` elsewhere, so the functions report "not Windows" or `None` instead of crashing). Declaring argument and return types stops `ctypes` from truncating 64-bit handles and masks to 32 bits |
| `performance_core_mask()` | Asks Windows for one record per physical core (`GetLogicalProcessorInformationEx` with relationship 0, `RelationProcessorCore`). Each record carries an `EfficiencyClass` (byte 9) and the core's affinity mask (offset 32). The P-cores have the higher class; their masks are ORed together (04 §6). Each efficiency class's core masks are collected in a dictionary; the highest class wins. Returns `None` on a non-hybrid CPU (only one class), where pinning is unnecessary. On this laptop it returns **0xFFFF** (logical CPUs 0–15), checked 2026-11-21 |
| The first call with `None` | The standard Windows two-call pattern: the first call only reports the buffer size needed |
| `pin_self(mask)` | Calls `SetProcessAffinityMask` on **the current process** (the batch script). Windows gives a child process its parent's affinity, so every game the script starts afterwards runs on the P-cores from its first instruction, engine start-up included. *Rejected:* pinning each game after `Popen` returns (a short unpinned window at start-up, and one more handle to open per run). Returns whether Windows accepted the mask |
| `applied_mask()` | Reads the mask back with `GetProcessAffinityMask` and says whether it is all processors or the P-cores only. The environment record stores what Windows *applied*, not what was requested, so a failed pin can't go unnoticed |
| `main()` | Prints the mask, for the environment record and a quick manual check |

**Phase 4 results** are recorded in [09_Testing_And_Benchmarks.md](09_Testing_And_Benchmarks.md) (F-1 to F-4).

---

## Part B · Step S1: the workload, B0 only

**State after S1:** the game module builds a seeded 500-actor wave of four classes, runs the three-cycle wave script, and spawns and removes the wave with the plain-`SpawnActor` baseline B0. There is no SpawnPacer system code yet, on purpose: the baselines are captured first (03).

### B1. `Public\SPWaveActor.h` / `Private\SPWaveActor.cpp`: the wave actors

```cpp
UCLASS(Abstract)
class ASPWaveActor : public AActor
{
	GENERATED_BODY()
public:
	ASPWaveActor();
	virtual void BeginPlay() override final;
	USPWaveBrainComponent* GetBrain() const { return Brain; }
protected:
	virtual void WaveBeginPlay() {}
	void BuildBody(int32 PartCount, UStaticMesh* RootMesh, UStaticMesh* PartMesh, FName RootProfile, bool bPartsCollide);
	UPROPERTY(VisibleAnywhere, Category = "SpawnPacer Demo")
	TObjectPtr<USPWaveBrainComponent> Brain;
};
```

| Line | What it does, and why |
|---|---|
| `#include "GameFramework/Actor.h"` | `AActor` is the base class. A header includes what its declarations need; everything else is forward-declared (`class UStaticMesh;` …) so files that include this header don't pay for those headers (04 §1) |
| `#include "SPWaveActor.generated.h"` | The code Unreal Header Tool (UHT) writes for the `UCLASS`es in this file. It must be the **last** include, or UHT stops with an error |
| `UCLASS(Abstract)` | Registers the class with the reflection system (06 §6). `Abstract`: nobody may spawn the base itself (`SpawnActor` refuses abstract classes, `LevelActor.cpp:486–490`); only the four subclasses are spawned. It still has a class default object, which the engine needs |
| `class ASPWaveActor : public AActor` | `A` prefix: an Actor subclass (UE naming). Public inheritance: a wave actor *is an* actor (04 §7) |
| `GENERATED_BODY()` | Pastes the UHT-generated members (type information, constructors UHT needs) into the class |
| `ASPWaveActor();` | The default constructor. Unreal constructs every object, including the class default object, with it |
| `virtual void BeginPlay() override final;` | Runs the engine's `BeginPlay` (`Super::BeginPlay()`: actor ticks, every component's `BeginPlay`, then Blueprint's `ReceiveBeginPlay`, `Actor.cpp:4808–4845`), then `WaveBeginPlay()`, then reports "ready" to the director. `final`: no subclass can add work *after* the notification. **Changed by the S1 code review:** the notification used to be sent from the brain component's `BeginPlay`, which runs *inside* the actor's `Super::BeginPlay()`, so the drone's own BeginPlay work happened after "ready" |
| `virtual void WaveBeginPlay() {}` | The hook a subclass overrides for its own BeginPlay work. Empty by default |
| `GetBrain() const` | Read-only access for the director and later steps. `const` because it doesn't change the actor |
| `BuildBody(...)` (protected) | Only subclasses call it, from their constructors. **Why the meshes are parameters:** a `static ConstructorHelpers::FObjectFinder` local is initialised once; if one shared helper looked up the path itself, the first class's mesh would be used for every class. Each subclass keeps its own static finder and passes the result in |
| `UPROPERTY(VisibleAnywhere, Category = …)` on `Brain` | `UPROPERTY` makes GC see the pointer and the editor show it (06 §6). `VisibleAnywhere`: shown but not replaceable in the editor (replacing a default subobject pointer would break the actor). `TObjectPtr` is the UE5 form of an object pointer for `UPROPERTY` members (06 §11) |
| Four subclasses, each `UCLASS()` | One class per spawn-cost profile. They have no state of their own except the drone's `Origin` (a plain member: it is set at BeginPlay and nothing needs to reflect it) |
| `ASPDrone::WaveBeginPlay` / `Tick` overrides | `virtual … override` (04 §7): only the drone moves, so only it overrides `Tick` and the hook |

**`SPWaveActor.cpp`:**

| Lines | What they do, and why |
|---|---|
| `namespace SPWave { … }` | Named constants with their reasons next to them (04 §8): mesh paths, part layout, drone circle. No magic numbers in the code |
| Mesh paths `"/Engine/BasicShapes/Sphere.Sphere"` … | Engine content, so nothing has to be authored. Referenced only from C++, so `DefaultGame.ini` lists the folder for cooking (B5) |
| `PrimaryActorTick.bCanEverTick = false;` (base constructor) | Most wave actors never tick. A tick function that exists costs registration and dispatch even when empty (UE5-UpdateBudget measured this), so it is off unless the class needs it |
| `SPWave::MaxParts` and `PartNames()` | The subobject names `Part00` … `Part22`, built **once** on first use (a `static` local initialised by a lambda, 04 §12–13). **Changed by the S1 simplify pass:** the names used to be formatted with `FString::Printf` inside every spawned actor's constructor: 23 strings per boss, a cost a real class (which uses literal names) doesn't have, inside the very thing being measured |
| `ASPWaveActor::BeginPlay()` → `NotifyReady()` → `GetWorld()->GetSubsystem<USPBenchmarkSubsystem>()` → `NotifyActorReady()` | The "ready" call (`NotifyReady()` since S3, so a pool can call it too, 05 D2). `GetSubsystem` returns null in worlds without the director (editor, other maps), so it is guarded. Every configuration completes a wave through this one call (02 §9) |
| `Brain = CreateDefaultSubobject<USPWaveBrainComponent>(TEXT("Brain"));` | Creates the component as a **default subobject**: part of the class default object, and copied into every spawned instance (06 §7). The name must be unique within the actor |
| `CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Root"))` … `SetRootComponent(Root)` | The root mesh: every other part attaches to it, and the engine registers it first (01b §2.2) |
| `SetCollisionProfileName(RootProfile)` | Collision profiles are named presets (06 §8). Drone: `DefaultProjectile`; crate: `BlockAllDynamic`; grunt and boss: `Pawn` (`Classes/Engine/CollisionProfile.h:189–196`). So each class creates a different amount of physics state at registration (H1) |
| `SetCastShadow(false)` | Keeps the GPU cost of 500 actors low, so frames stay game-thread bound and a game-thread change shows in frame time (the calibration checks this, 01 §6.1) |
| `check(NumParts <= SPWave::MaxParts);` then `CreateDefaultSubobject<UStaticMeshComponent>(SPWave::PartNames()[Index])` | One child mesh per part, each with a unique, prebuilt name. The `check` stops a future class with more parts than names at its first construction |
| `Part->SetupAttachment(Root)` | The constructor-time way to attach (06 §7): it records the parent; the actual attachment happens at registration. *Not* `AttachToComponent`, which is for registered components at run time |
| `SetRelativeLocation(...)` ring, three layers | A deterministic shape: same parts in the same places in every run |
| `SetCollisionProfileName(bPartsCollide ? BlockAllDynamic : NoCollision)` | Crate parts collide (more physics state); grunt and boss parts are visual only, like most enemy meshes |
| `ASPDrone::ASPDrone()` | `bCanEverTick = true` (it moves), a 1-part body, scale 0.3 |
| `static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(...)` | Loads the mesh once per class: `static` means initialised on the first construction (the class default object) and reused for every later instance. A non-static finder would load the asset in every spawned actor's constructor and distort the spawn cost being measured |
| `ASPDrone::WaveBeginPlay` → `Origin = GetActorLocation()` | The spawn position becomes the circle's centre |
| `ASPDrone::Tick` | Position from **world time**, not accumulated `DeltaSeconds`: in fixed-step runs every configuration puts every drone at exactly the same place on the same frame |
| `ASPCrate` / `ASPGrunt` / `ASPBoss` constructors | 3 / 6 / 24 parts (01 §6.1). The grunt and boss set `Brain->BeginPlayWorkScale = 1.0f` and `Brain->bThinks = true`: they are the enemies |
| `GetRootComponent()->SetRelativeScale3D(...)` | Size only. Scale on the root scales its children too |

### B2. `Public\SPWaveBrainComponent.h` / `.cpp`: the gameplay stand-in

| Line | What it does, and why |
|---|---|
| `#include "Engine/TimerHandle.h"` | The header declares an `FTimerHandle` **member**, which needs the full type (a forward declaration is enough only for pointers and references). Found while reviewing the code before its first build |
| `UENUM() enum class ESPBrainState : uint8 { Idle, Engaging, Retreating };` | A scoped enum, one byte, reflected so a `UPROPERTY` can hold it (06 §6). It is state the think timer changes, so a pool would have to reset it |
| `UCLASS(ClassGroup = (SpawnPacerDemo))` | `ClassGroup` only files the component under a heading in the editor's "Add Component" menu |
| `PrimaryComponentTick.bCanEverTick = false;` | The brain never works per frame: in BeginPlay and in its timer only |
| `UPROPERTY(EditDefaultsOnly) float BeginPlayWorkScale = 0.0f;` | Editable on class defaults only (per class, not per placed instance). 0 for drones and crates, 1 for enemies (set by their constructors). `float` because it multiplies the iteration count |
| `UPROPERTY(EditDefaultsOnly) bool bThinks = false;` | `b` prefix for booleans (UE naming). Enemies only |
| `UPROPERTY(VisibleInstanceOnly) float Health … int32 Ammo … ESPBrainState State … int32 ThinkCount` | Gameplay state, **private** with `const` getters (no outside code may change it). `UPROPERTY` on purpose: the reset verifier of step S8 compares reflected properties. `VisibleInstanceOnly`: visible on a live instance in the editor's details panel, never editable |
| `float WorkSink` (not a `UPROPERTY`) | The set-up loop's result, stored in a member so the optimiser can't delete the loop (04 §11) |
| `FTimerHandle ThinkTimer` | The handle the timer manager fills in; needed to clear the timer (06 §9) |
| `TAutoConsoleVariable<int32> CVarBeginPlayWork(TEXT("sp.Demo.BeginPlayWork"), 4000, …, ECVF_Default)` | One value for every brain, whoever spawned it. `ECVF_Default` so it can be set from the command line and config (cheat flags can't be, UE5-UpdateBudget 01b #11). The code default 4000 was the starting value; the S2 calibration chose **20000**, which `run_bench.py` passes to every run as `-SPWork=20000` (09 S2-c) |
| `const int32 Iterations = FMath::RoundToInt32(CVar.GetValueOnGameThread() * BeginPlayWorkScale);` | `GetValueOnGameThread()` is the thread-correct read for game-thread code. Rounded, because a fractional scale is allowed |
| `for (...) { Sink = FMath::Sin(Sink + 0.001f * Index); }` | A **dependent chain**: each step needs the previous result, so the CPU can't run iterations in parallel and the compiler can't vectorise it. The cost is therefore proportional to the count, which makes it a calibratable dial |
| `World->GetTimerManager().SetTimer(ThinkTimer, this, &USPWaveBrainComponent::Think, SPBrain::ThinkInterval, true);` | A repeating 0.5 s timer bound to a member function (06 §9). `true` = loop |
| `EndPlay` → `ClearTimer(ThinkTimer)` → `Super::EndPlay` | The engine doesn't clear timers on EndPlay (01b #11). A destroyed brain's timer would just stop firing; a pooled one (S7) would keep firing, so the brain clears its own. `Super` last, so the base class still sees a valid component |
| `Think()` | `++ThinkCount`; `Ammo = FMath::Max(0, Ammo - 1)`; the state cycles through the three values with `static_cast` (04 §9) |

### B3. `Public\SPBaselineSpawner.h` / `.cpp`: baseline B0 (S1 version)

Rewritten in S3 to hold all four baselines; the current file is explained line by line in **D1**.

### B4. `Public\SPBenchmarkSubsystem.h` / `.cpp`: the director

| Line | What it does, and why |
|---|---|
| `UCLASS() class USPBenchmarkSubsystem : public UTickableWorldSubsystem` | A world subsystem has exactly the world's lifetime, and the project has no authored map to place a director in (UE5-UpdateBudget made the same choice). Tickable, because it advances the script every frame (06 §14). **Changed in S1** from the planned plain world subsystem |
| The header comment (timeline, command line) | The run's contract in one place: the events cut the analysis windows of 01 §6.3 |
| `NotifyActorReady() { ++ReadyThisWave; }` | Inline: it is called once per actor per wave |
| `DoesSupportWorldType` → Game or PIE | The default also creates world subsystems in editor worlds (`Private/Subsystems/WorldSubsystem.cpp:29–32`) |
| `struct FRunSettings`, `enum class ERunPhase`, `enum class ECycleStep`, `struct FWaveRecord` | Private, plain types: nothing outside the class needs them, so they aren't reflected (04 §8) |
| `UPROPERTY(Transient) TArray<FSPWaveEntry> Wave;` | The seeded wave, requested unchanged in every cycle |
| `UPROPERTY(Transient) TObjectPtr<USPBaselineSpawner> Baseline;` / `Camera` | Owned objects: `UPROPERTY` keeps them alive and GC-visible |
| `CSV_DEFINE_CATEGORY(SPBench, true);` | The CSV category of the script's events; `true` = enabled by default (06 §10) |
| `ReadRunSettings()` with `FParse::Value(CommandLine, TEXT("SPWaveSize="), Read.WaveSize)` … | Reads `-Name=value` pairs (06 §13). Missing values keep their defaults. `FParse::Param(…, TEXT("SPExit"))` reads a bare flag. `FPaths::MakeValidFileName(Read.RunTag, TEXT('_'))` (`Core/Public/Misc/Paths.h:696`) keeps the tag a plain file name, so it can't write outside `Saved\SPRuns` (code review) |
| `ValidateRunSettings()` | Refuses: a configuration not implemented yet; a wave above `MaxWaveSize` (20,000, a typo guard); no waves; a negative warm-up; and any timing where a step doesn't follow the previous one or the despawn and GC fall outside their own cycle (`Life > 0`, `GCAfter ≥ 0`, `Life + GCAfter < Cycle`; the code review found that a negative `GCAfter` used to pass). A refused run logs an `Error:` |
| `QuitRun(SPBench::InvalidRunExitCode)` on a refused run | With `-SPExit`, a refused run quits at once with **exit code 2** (`FPlatformMisc::RequestExitWithStatus`, `Core/Public/GenericPlatform/GenericPlatformMisc.h:1096`), so a batch doesn't wait for its timeout (code review) |
| `OnWorldBeginPlay` | Runs once the world starts play, before actors begin play (`Private/World.cpp:6167, 6174`). No `-SPWaveSize` means no workload: a normal empty map |
| `FindConsoleVariable(TEXT("sp.Demo.BeginPlayWork"))` → `Set(Settings.Work, ECVF_SetByCommandline)` | Applies `-SPWork=` to the brain's cvar, and reads back the value actually in effect for the metadata (06 §13) |
| `NewObject<USPBaselineSpawner>(this)` | Creates the spawner with the director as its outer |
| `InWorld.SpawnActor<ACameraActor>(Location, Rotation)` | The fixed camera (06 §14) |
| `CSV_METADATA(TEXT("SPConfig"), …)` … | The run's settings and the wave hash go into the CSV file itself, so every capture is self-describing |
| `BuildWave()`: `Crates = Count * 30 / 100`, `Grunts = Count * 19 / 100`, `Bosses = Min(Count − Crates − Grunts, Max(1, Count * 1 / 100))`, `Drones` = the rest | Integer arithmetic: for 500, exactly 250 / 150 / 95 / 5. Rounding goes to the cheapest class (drones), so the heavy classes are never over-weighted, and a small test wave still has one boss (code review: the remainder used to go to bosses, 10% of a 10-actor wave) |
| `Wave.AddDefaulted_GetRef().Class = Class;` | Appends a default entry and sets its class. (A brace initialiser was avoided: whether a `USTRUCT` with `GENERATED_BODY()` is an aggregate isn't something to rely on) |
| `FRandomStream Random(Settings.Seed);` + Fisher–Yates loop with `Random.RandRange(0, Index)` | A seeded, private random stream (06 §13): same seed, same order and positions. Fisher–Yates gives every permutation the same probability |
| Grid: `Columns = CeilToInt32(Sqrt(Count))`, `Rows = (Count + Columns − 1) / Columns`, each centred separately; 2 m cells, ±0.5 m jitter, random yaw | A block centred in front of the camera. Rows are counted separately because the last row may be partly filled (500 actors: 23 columns, 22 rows; the code review found the block used to be off-centre) |
| `Hash = FCrc::StrCrc32(*Class->GetName(), Hash); Hash = FCrc::TypeCrc32(FVector3f(Location), Hash); …` | A running CRC over the class name, position and yaw of every entry: chaining equals one CRC over the whole list, without a buffer. Two runs with the same seed must print the same hash (the S1 check) |
| `UpdateCamera()` → `SetViewTarget(Camera)` every frame | A pawn possessed later would take the view back; re-applying is cheap |
| `StepScript(SecondsInCapture)`: `InCycle = SecondsInCapture - CurrentCycle * CycleSeconds` | Time inside the current 12 s cycle. A small state machine (`NextStep`) fires each step once, when its time has come |
| `RequestWave` step | Opens a `FWaveRecord` (frame from `GFrameCounter`, time), resets the ready count, queues the wave, and fires `CSV_EVENT(SPBench, TEXT("WaveRequested"))` |
| `Despawn` step | At `LifeSeconds`: queues the removal, fires `DespawnRequested` |
| `ForceGC` step | At `LifeSeconds + GCAfterSeconds`: `GEngine->ForceGarbageCollection(true)`. This only arms a full purge, `Private/UnrealEngine.cpp:2136–2140`; the engine runs it at its next garbage-collection check, which is why the GC window spans three frames (01 §6.3) |
| `CycleDone` step | At the end of the cycle: next cycle |
| `Tick`: phases `WarmUp` → `Capture` → `Done` | `BenchStart` after the warm-up; `BenchEnd` after the last cycle; the report one frame later (outside the capture); `QuitRun(0)` three frames later, so the CSV profiler has finished writing (as in UE5-UpdateBudget) |
| `QuitRun(uint8 ExitCode)` | The one place that decides whether to quit: only with `-SPExit`, only once, and only in Game worlds (in PIE it would close the editor; code review). `RequestExitWithStatus(bForce, ExitCode, …)` with `bForce = ExitCode != 0`. **Found by the first S1 run (09 S1-c):** a clean exit on Windows only posts a quit message (`Core/Private/Windows/WindowsPlatformMisc.cpp:1516–1519`) and the process returns the engine's own error level (`Launch/Private/Launch.cpp:146–203`), so code 2 arrived as 0. A forced exit flushes the log and calls `TerminateProcess` with the code (`WindowsPlatformMisc.cpp:1501–1514`). Nothing needs saving in a refused run, so forcing is safe there; a finished run still exits cleanly with 0 |
| `Baseline->RunFrame(*World);` after the script step | The spawner works in the same frame as the request |
| Completion checks after `RunFrame` | `Record.Ready` is copied every frame, so an incomplete wave still reports how many actors became ready (code review). `WaveComplete` fires once, when every actor has reported ready and the record has no completion time yet; `DespawnComplete` once, when nothing is pending or live and the record has no completion frame yet. The record itself is the "already sent" flag (simplify pass: two separate booleans removed). Checked after the spawner ran, so a wave that finished this frame is reported this frame (B0: latency 0) |
| `WriteRunReport()` → `Saved/SPRuns/<RunTag>.csv` | One line per wave: request and completion frames, latency in frames and in simulated ms, despawn frames, GC frame. The batch script moves it next to the capture |
| `GetStatId()` → `RETURN_QUICK_DECLARE_CYCLE_STAT(USPBenchmarkSubsystem, STATGROUP_Tickables)` | Required by `FTickableGameObject`: names the tickable's cost in the stats system |

### B5. `Config\DefaultGame.ini` (S1 addition)

`[/Script/UnrealEd.ProjectPackagingSettings] +DirectoriesToAlwaysCook=(Path="/Engine/BasicShapes")`: the demo meshes are referenced only from C++ (`ConstructorHelpers`). No map references them, so without this line the cooker would leave them out and packaged wave actors would be invisible (UE5-UpdateBudget hit the same).

---

## Part C · Step S2: the measurement tools (`Tools\`)

### C1. `csv_stats.py`

`percentile`, `summarize`, `load_capture`, `frame_events` and `column_values` are copied unchanged from UE5-UpdateBudget's `csv_stats.py` (each repository stands alone). The additions implement 01 §6.3.

| Lines | What they do, and why |
|---|---|
| Module docstring | The rules, so every number can be reproduced: end header, events, linear-interpolation percentiles, the four windows, hitch thresholds, latency |
| `DEFAULT_COLUMNS` | Frame, game-thread, render-thread and GPU time; both spawners' `WorkMs`; the engine's own `ActorSpawning` / `ActorDestroying` columns (01b §4), for cross-checking our timing |
| `FRAME_TIME_LAG = 1` | **Found in the S2 calibration (09 S2-b):** the engine writes frame N's `FrameTime` into row N+1. It computes it at the *start* of a frame, as the time since the previous start (`FCsvProfiler::UpdateFrameTime`, `Core/Private/ProfilingDebugging/CsvProfiler.cpp:4369–4390`), while `GameThreadTime` and our own stats belong to their own row. So every window reads `FrameTime` one row later than the other columns. *Rejected:* `csv.UseLegacyFrameTime 1` (`CsvProfiler.cpp:150–153`), which restores end-of-frame timing but changes an engine default for every run |
| `HITCH_MS = 33.3`, `MISSED_FRAME_MS = 16.7` | Two and one 60 Hz refresh intervals (01 §6.3) |
| `csv.field_size_limit(1 << 30)` | The metadata row can exceed Python's default field limit |
| `percentile` | `position = p/100 × (n − 1)`, then linear interpolation between the two neighbours (numpy's default). The self-test checks 5.5 / 9.55 / 9.91 on 1…10 by hand |
| `load_capture` | Reads the whole file; a finalised capture ends with the full header and the `[key],value` metadata row. **The end header is used for column lookup**, because a stat first written mid-capture exists only there (01b B-2). A capture without them is flagged, never silently trusted |
| `frame_events` | Splits the `EVENTS` cell (`Category/Name##timestamp;…`) into names |
| `events_per_frame(header, data)` | Every frame's events, parsed **once** per capture (simplify pass: the analysis looks them up five times, and each lookup used to re-parse every row) |
| `event_frames(events, name)` | Indices of the frames that carry `SPBench/<name>` |
| `window_rows(data, starts, length, before, offset)` | Pools the frames of every window of one type over all waves; `offset` shifts every window (by `FRAME_TIME_LAG` for `FrameTime`); clips at the capture's ends, and the caller reports how many frames each window actually held |
| `summarize_window(header, data, starts, length, columns, before)` | Per window type: statistics of each column (`FrameTime` from the shifted rows, the rest from the event-aligned rows), plus `over_33_3ms` and `over_16_7ms` on `FrameTime`. `frames` counts the event-aligned rows and `frame_time_frames` the shifted ones, which can be one shorter at the end of a capture (code review: the shortfall used to be hidden) |
| `WORK_COLUMNS`, `ACTIVE_WORK_MS = 0.01`, `BUDGET_TOLERANCE = 1.10` and the `active_work` part of `summarize_window` (S3) | For each spawner's `WorkMs`, the frames of a window that did real work (above 0.01 ms: reading the clock alone costs microseconds), their statistics, and how many exceed 1.1 × the run's budget (its `spbudgetms` metadata, 2.0 ms when absent). This is SC3's statistic; a p99 over all 240 frames would hide it |
| `wave_latencies` | For each `WaveRequested`, the first `WaveComplete` at or after it and before the next request; `None` if the wave never completed (that is a failure the summary shows) |
| `analyse(...)` | One capture → events, wave count, latencies and the W / D / GC / PRE windows. Window lengths come from `--fps` and seconds, so a different fixed step only needs a flag |
| `selftest()` | Hand-worked cases: percentiles, summary, event parsing, a ten-frame capture with a wave on frame 2 (completes on 4) whose 40 ms frame time sits in row 3, a 3-frame window (40, 20, 10 ms → one frame over 33.3, two over 16.7, median 20, max 40), a pre-window with and without the lag, and a clipped window |
| `main()` | Prints per capture and window; `--json` writes everything for later processing |

### C2. `run_bench.py`

Copied from UE5-UpdateBudget's `run_bench.py` and adapted. What changed, and why:

| Part | What it does, and why |
|---|---|
| `from perf_cores import KERNEL32, applied_mask, performance_core_mask, pin_self` | The pinning code lives in one copied file (05 A9), imported from the same folder |
| `EXE = …\Build\Windows\SpawnPacer\Binaries\Win64\SpawnPacer.exe` | The **real** game executable, not the bootstrap, so the exit code is the game's own (09 F-4) |
| `STARTUP_MARGIN_SECONDS = 240.0` | Per-run timeout = warm-up + waves × cycle + this margin. Generous, because a first run once took 27 s just to exit (09 F-4a) |
| `COMMON_ARGS` | Fixed step `-deterministic -FPS=60 -novsync`, 1280×720 windowed, no sound, `-traceautostart=0`, `-SPExit`, capture on `BenchStart`/`BenchEnd` (01 §7) |
| `CONFIGS` | Command-line switch per configuration; each one is added in the step that implements it (S1: `B0`) |
| `environment_record()` | CPU, GPUs and drivers, RAM, OS build, power plan, engine version, and **`cpu_affinity: applied_mask()`**, the mask read back from Windows. AC power from `root/wmi BatteryStatus.PowerOnline`, which is true on mains whether the battery is charging or full (code review: `Win32_Battery.BatteryStatus == 2` means only \"AC, not charging\", so a charging laptop was reported as on battery) |
| `analysis_windows(args)` | The window lengths of 01 §6.3 (W 4 s, D 2 s, GC 3 frames, PRE 1 s), **clipped to this batch's script timings**, so a shorter `--life` or `--gc-after` can't make one window swallow the next event (code review). They go into `csv_stats.analyse` and into the manifest |
| `launch()` | Builds one command line with the `-SP…` settings and `--extra`; deletes `GameUserSettings.ini` (it can re-enable VSync or a frame cap); runs it with `run_and_reap` (05 E5) |
| `collect()` / `collect_files()` | A file that still can't be moved is recorded as `collect_error` instead of stopping the whole batch (code review). Moves the newest CSV, the run report (`Saved\SPRuns\<name>.csv` → `<name>_report.csv`) and a copy of the log into `Benchmarks\Raw`, refusing to overwrite; counts `Error:` and `Warning:` in the log; runs `csv_stats.analyse` |
| `is_failure()` | A run fails on a non-zero exit, a timeout, a missing or unfinalised capture, a missing log, log errors, a collect error, **a wave count different from `--waves`**, or **a wave that never completed** (code review). Failed runs are **excluded from the summary** and listed under it; their files stay in `Raw` |
| `write_summary()` | Per configuration × work × window: frames per run, FrameTime median / p99 / max as median across runs [min–max], frames over 33.3 ms (sum and worst run), then the spawn frames (count per run, p99, max, count over 1.1 × budget; S3), with spawn-work **max** (a p99 over 240 frames hides the one frame that does a whole B0 wave); then the wave latency as the median across runs of each run's median (code review: it used to pool every wave of every run, unlike every other cell) |
| `--work` default `20000` | The calibrated BeginPlay work (09 S2-c), passed as `-SPWork=` to every run, so all configurations use the same value whatever the cvar's code default is |
| `--resummarize BATCH` → `resummarize()` (S3) | Rebuilds a batch's summary from its manifest and raw CSVs with the current analysis code, without running anything; the manifest (raw data) is never changed. Used after the S3 tool change to rebuild the S2 and S3 summaries |
| `main()` | `SetThreadExecutionState` keeps the display awake while the batch runs (changes no power settings); **`pin_self(performance_core_mask())` once, before the environment is recorded or anything is started**, refusing to measure if it fails; default `--runs` = the number of configurations and **at least 2** (a median and a range need two runs; code review); one priming run (`_r0_prime`, excluded from summaries); then each round rotates the configuration order |

---

## Part D · Step S3: the baselines B1–B3

### D1. `Public\SPBaselineSpawner.h` / `.cpp`: B0–B3 (rewritten in S3)

| Line | What it does, and why |
|---|---|
| `USTRUCT() struct FSPWaveEntry { UPROPERTY() TSubclassOf<ASPWaveActor> Class; UPROPERTY() FTransform Transform; };` | One actor of a wave. `USTRUCT` + `UPROPERTY` so arrays of it can be `UPROPERTY` too. `TSubclassOf<ASPWaveActor>` is a `UClass*` that only accepts subclasses of `ASPWaveActor` (06 §11) |
| `USTRUCT() struct FSPPooledActors { UPROPERTY() TArray<TObjectPtr<ASPWaveActor>> Actors; };` | B3's parked actors of one class. A struct because a reflected `TMap` can't hold an array as its value directly (06 §18) |
| `UENUM() enum class ESPBaseline : uint8 { B0, B1, B2, B3 };` | The four baselines of 01 §6.2, one comment each |
| `class AActor; class ASPWaveActor; class UWorld;` | Forward declarations: the header only names these types (04 §1) |
| `UCLASS() class USPBaselineSpawner : public UObject` | A plain `UObject` the director owns and drives each frame (02 §4.2): two tickable subsystems have no guaranteed order (`Public/Tickable.h:62`, `Private/Tickable.cpp:66–92`) |
| `Configure(Mode, CountCap, BudgetMs)` | Called once by the director. It also builds `SpawnParameters` once, outside every timed loop (code review): `AlwaysSpawn` so a collision test never moves or rejects a seeded position, and `bDeferConstruction` only for B1 |
| `FillPool(World, Entries)` | B3: one parked actor per wave entry, spawned at the park location and parked at once. The director calls it on the first warm-up frame, when the world has begun play, so each actor's BeginPlay has already run (inside `SpawnActor`) when it is parked |
| `RequestWave(TConstArrayView<FSPWaveEntry>)` | Appends the wave to `Pending` (06 §11) |
| `RequestDespawn()` | Sets the despawn flag **and cancels the entries not brought in yet** (`Pending.Reset()`). A capped baseline that hasn't finished by the despawn time leaves its wave incomplete, which the run reports, instead of spawning the rest afterwards and leaking them into the next cycle (code review) |
| `MayStartAnother(ItemsThisFrame, FrameStartCycles)` | The **only** difference between the paced baselines. B1: `ItemsThisFrame < CountCap`. B2: elapsed `< BudgetMs`, checked **before** each item, so the item that starts just under the limit runs to completion: the overrun under test (H4), exactly like Mass and SpawnBudget (01b §5). B0 and B3: always (the whole wave in one frame) |
| `RunFrame(World)` | `Live.Reserve(...)` once; `Start = Cycles64()`; bring in pending entries while `MayStartAnother`; then, if a despawn is requested, take out live actors under the **same** frame limit; `WorkMs` from the same clock as SpawnPacer (06 §12) |
| CSV stats, once per frame | `SPBaseline/WorkMs`; `Added` and `Removed` (spawned or taken from the pool; destroyed or parked: one meaning in every baseline, so B3 doesn't report parked actors as "destroyed", code review); `Live`; for B1 also `ConstructMs` and `FinishMs` |
| `BringIn` for B3 | `TakeFromPool(Class)`; then `SetActorTransform(..., ETeleportType::ResetPhysics)` (06 §16), un-hide, re-enable collision and the actor tick, `PrepareForReuse()` (the user callback, D2), then `NotifyReady()`: a reused actor doesn't run BeginPlay again, so the pool reports it ready itself. **On a pool miss it spawns**, as a competent pool does (code review) |
| `BringIn` for B0 / B2 / a B3 miss | Plain `SpawnActor`: construction, registration, construction script and BeginPlay all happen inside it |
| `BringIn` for B1 | `SpawnActor` with `bDeferConstruction` = `SpawnActorDeferred` (`Classes/Engine/World.h:3851–3871`), then `FinishSpawning(Entry.Transform)` **in the same frame** (06 §15). Two `Cycles64` reads split the time into `ConstructMs` (construction + native component registration, `Actor.cpp:4333–4337`) and `FinishMs` (construction script, `InitializeComponents`, `PostInitializeComponents`, BeginPlay, `Actor.cpp:4415–4529`). That split is H1's first measurement, made before SpawnPacer exists |
| `TakeFromPool(Class)` | Pops until a valid actor turns up: something else may have destroyed a parked one (Mass does the same, `MassActorSpawnerSubsystem.cpp:244–249`) |
| `TakeOutLast()` → `bool` | Pops the last live actor. Already gone: `false`, not counted. B3: `Park` and back into the pool. Otherwise `Destroy()` |
| `static void Park(ASPWaveActor&)` | B3's reset: `PrepareForParking()` (stops the think timer), `SetActorHiddenInGame(true)`, `SetActorEnableCollision(false)`, `SetActorTickEnabled(false)` (06 §16), move to `ParkLocation` (1 km below the arena). PoolManager's three switches plus the callback its user would write. It does **not** switch off component ticks or reset gameplay state: that unchecked reset is what SpawnPacer's pool addresses (S7, S8) |
| `UPROPERTY(Transient) TMap<TObjectPtr<UClass>, FSPPooledActors> Pool;` | GC-visible parked actors, by class (06 §18) |

### D2. `SPWaveActor` and `SPWaveBrainComponent` (S3 additions)

| Line | What it does, and why |
|---|---|
| `void NotifyReady() const;` | The one "ready" call: from the end of `BeginPlay`, and from a pool that reuses the actor. `GetSubsystem<USPBenchmarkSubsystem>()` is null without the director, so it is guarded |
| `virtual void PrepareForReuse();` | A pool is about to reuse the actor at its new place: restart what a new life starts. Base: `Brain->StartThinking()`. `ASPDrone` overrides it to also take its new location as the circle's centre (code review: a reused drone otherwise flew back to where it was first spawned, the park location) |
| `void PrepareForParking();` | A pool is parking the actor: `Brain->StopThinking()`, so parked enemies don't keep thinking (code review) |
| `USPWaveBrainComponent::StartThinking()` / `StopThinking()` | The think timer as two public calls: BeginPlay and a reuse start it; EndPlay and parking stop it (`ClearTimer`, 06 §9) |

### D3. `SPBenchmarkSubsystem` (S3 additions)

| Line | What it does, and why |
|---|---|
| `-SPCountCap=K`, `-SPBudgetMs=MS` | B1's cap and B2's budget, read in `ReadRunSettings`, validated (≥ 1, > 0), and written into the CSV metadata. `run_bench.py` passes K = 14 and 2.0 ms (09 S3) |
| `static TOptional<ESPBaseline> ParseBaseline(const FString&)` | Maps `-SPConfig` to a baseline; empty for an unknown name (06 §17). Validation refuses an unknown name |
| `if (const TOptional<ESPBaseline> Mode = ParseBaseline(...)) { Baseline->Configure(*Mode, ...); }` | Configures the spawner only when there is a baseline: from S5, the SpawnPacer configurations have none, and `GetValue()` on an empty `TOptional` would assert (code review) |
| `bPoolToFill` and the first warm-up frame | B3's pool is filled on the first warm-up frame, then that frame `break`s, so the capture can start on the next frame at the earliest: the fill's hitch never lands in it, whatever the warm-up length (code review). Filling earlier, in `OnWorldBeginPlay`, was tried and rejected: the world hasn't begun play yet, so the actors' BeginPlay would run *after* they were parked and restart their think timers |
| `-SPSpawnTimer` | Brackets the capture with the engine's `spawnactortimer start` / `stop` (`GEngine->Exec`, which passes console commands on to `IConsoleManager::ProcessUserConsoleInput`, `Private/UnrealEngine.cpp:5519, 5722`). The stop logs `<class>,<average s>,<total s>,<count>` per class (`Private/GameFramework/SpawnActorTimer.cpp:110–117`): the engine's own per-class cost, a cross-check for H2 and for our per-stage timing from S5 |

---

## Part E · Step S4: the pure policy and its unit tests

All in the plugin, `Plugins\SpawnPacerCore\Source\SpawnPacerCore\`. Plain C++: no `UObject`, no world, so the policy is tested with fake jobs (02 §4).

### E1. `Public\SpawnCostModel.h` / `Private\SpawnCostModel.cpp`: what a stage will cost

| Line | What it does, and why |
|---|---|
| `#include "UObject/ObjectKey.h"` | For `TObjectKey` (06 §20). `CoreMinimal.h` brings `TMap`, `FName`, `TOptional` and `TFunctionRef` |
| `#define UE_API SPAWNPACERCORE_API` … `#undef UE_API` | The export macro under a short local name, the engine's own pattern; `#undef` at the end so it doesn't leak into files that include this one |
| `enum class ESpawnPacerStage : uint8 { Spawn, Construct, Register, Finish, Acquire, Release, Destroy };` | The indivisible pieces of work of 02 §2, one comment each. `Spawn` is a whole spawn; `Construct` is `SpawnActor` with `bDeferConstruction`, which already registers the native components (06 §15); `Register` is one held-back component; `Finish` is `FinishSpawning` |
| `struct FSpawnCostKey { TObjectKey<UClass> Class; ESpawnPacerStage Stage; FName Component; };` | What one estimate is kept for. The component's `FName` identifies "the same component" across spawns, because a default subobject has the same name in every instance. The class is a **weak** key: a raw `UClass*` could be reused by a new class at the same address after a Blueprint recompile, and inherit the old class's learned cost (S4 code review) |
| `operator==` and `friend uint32 GetTypeHash(...)` | What `TMap` needs from a key (04 §15): equality over all three fields, and a hash combining all three with `HashCombine` |
| `class UE_API FSpawnCostModel` | Exported, so the game module and the tests can use it |
| `struct FEstimate { double MeanMs; double DeviationMs; int32 Samples; };` | RFC 6298's two numbers per key (SRTT and RTTVAR), plus a sample count, kept for the debug display and for seeding (02 §12) |
| `static constexpr double Alpha = 1.0 / 8.0; Beta = 1.0 / 4.0;` | RFC 6298 §2: "SHOULD be computed using alpha=1/8 and beta=1/4" |
| `explicit FSpawnCostModel(double InDeviationMultiplier = 2.0)` | K, 2 by default (02 §7). `explicit` so a `double` never silently becomes a model (04 §18) |
| `double Predict(const FSpawnCostKey&, double ColdPredictionMs) const` | `Mean + K × Deviation`; for a cold key, the caller's cold value. The model doesn't know the budget, so the scheduler passes it |
| `bool IsCold(const FSpawnCostKey&) const` | True if the key has never been observed or seeded |
| `void Observe(const FSpawnCostKey&, double MeasuredMs)` | **First sample R:** `Mean = R`, `Deviation = R / 2` (RFC 6298 rule 2.2). **Later:** `Deviation = (1 − β)·Deviation + β·|Mean − R|` with the **old** mean, then `Mean = (1 − α)·Mean + α·R` (rule 2.3: the order matters, the test checks it) |
| `void Seed(Key, MeanMs, DeviationMs)` | Sets an estimate directly: for tests now, for a saved table later (02 §12) |
| `const FEstimate* Find(Key) const` | The estimate, or null when cold; for tests and the debug display |
| `Get/SetDeviationMultiplier` | K, changeable at run time by the settings and cvars of S9 |

### E2. `Public\SpawnPacerScheduler.h` / `Private\SpawnPacerScheduler.cpp`: the admission rule

| Line | What it does, and why |
|---|---|
| `struct FSpawnPacerFrameStats` | One frame of the scheduler: `SpentMs` (measured), `PredictedMs` (what was predicted for the jobs that ran, before they ran), `Jobs`, `ForcedJobs`, `ColdJobs`, `bStoppedOnBudget`. Published once per frame as the `SpawnPacer` CSV stats from S5 (02 §4.1, telemetry) |
| `static FSpawnPacerFrameStats RunFrame(double BudgetMs, FSpawnCostModel& Model, TFunctionRef<TOptional<FSpawnCostKey>()> PeekHead, TFunctionRef<double()> RunHead)` | The policy, with the mechanism passed in: `PeekHead` gives the head job's key (empty when the queue is empty), `RunHead` does one stage of the head request and returns the milliseconds it measured. Borrowed for the one call (`TFunctionRef`, 04 §16, 06 §21), so nothing is allocated per frame |
| `while (const TOptional<FSpawnCostKey> Head = PeekHead())` | Tests and binds in one line (06 §17). The key is **copied** out of the queue, because `RunHead` may advance or reallocate it |
| `bCold = Model.IsCold(*Head); Predicted = Model.Predict(*Head, BudgetMs);` | A cold key is predicted as the whole budget (02 §7) |
| `if (Stats.Jobs > 0 && (bCold \|\| Stats.SpentMs + Predicted > BudgetMs)) { bStoppedOnBudget = true; break; }` | **Never start a job that is predicted not to fit**, and run a cold job only as the frame's first job. The cold check is explicit (S4 code review): with the prediction alone, after a first job that measured about 0 ms, `0 + budget > budget` is false and an unknown job would run second. The job stays at the head for the next frame |
| `ForcedJobs += (Jobs == 0 && Predicted > BudgetMs)` | Progress: the first job always runs, so a job bigger than the budget can't block the queue forever. When it is predicted over the budget, that is counted: an overrun the policy knew about (SC3 counts forced frames separately) |
| `Measured = RunHead(); Model.Observe(*Head, Measured);` | Run, then learn from what it really cost |
| `SpentMs += Measured; PredictedMs += Predicted; ++Jobs;` | What the frame really spent. An overrun shows up in `SpentMs`; it is never clipped to the budget |

### E3. `Private\Tests\SpawnPacerPolicyTests.cpp`: 8 automation tests with fake jobs

| Line | What it does, and why |
|---|---|
| `#if WITH_DEV_AUTOMATION_TESTS` | Compiled only into builds that carry developer tests (not Shipping, 06 §19) |
| `Flags = EditorContext \| ClientContext \| ProductFilter` | Runs in the editor and in a game; one filter, as the macro requires (`AutomationTest.h:4127–4133`); `ProductFilter` because these are a project's own tests, not engine tests |
| `FSpawnCostKey Key(Stage, Component)` | A key with `UObject::StaticClass()`: the policy doesn't care which class |
| `struct FFakeQueue { struct FJob { Key; CostMs; }; TArray<FJob> Jobs; int32 Next; }` | Fake jobs: each has a key and the time it "measures" when it runs. `RunFrame` passes two lambdas capturing `this` to the scheduler: peek = the job at `Next`, run = return its cost and advance. One array of jobs, not two parallel arrays (simplify) |
| `EstimatorFollowsRfc6298` | Hand-worked: 4 ms gives mean 4, deviation 2, prediction 8; then 2 ms gives deviation 2 (computed with the **old** mean 4), mean 3.75, prediction 7.75; 2 samples. `TestNotNull` before reading the estimate, so a regression fails the test instead of crashing the run (code review) |
| `ColdKeyIsPredictedAsTheBudget` | Cold → the cold value; warm after one sample; a different component name is a different key |
| `NeverStartsAJobThatWontFit` | Five 0.6 ms jobs, 2 ms budget: three run (1.8 ms), the fourth would make 2.4 and stays at the head; nothing forced |
| `FirstJobAlwaysRunsAndIsCountedForced` | A 5 ms job with a 2 ms budget runs alone and is counted forced; the second waits |
| `ColdJobRunsOnlyFirst` | Warm then cold: only the warm one runs. Cold then warm: both run, the cold one isn't "forced" (its prediction equals the budget) and is now learned. **A warm job that measures 0 ms, then a cold one: the cold one waits** (the explicit rule; this case fails without it) |
| `EmptyQueueDoesNothing` | No jobs, nothing spent, not "stopped on budget" |
| `ManyTinyJobsStopAtTheBudget` | 1,000 jobs of 0.002 ms in 0.5 ms: 249 or 250 run (floating-point rounding at the boundary, 04 §17), never more than the budget, and the frame stops on the budget |
| `OverrunIsReportedAndLearned` | Predicted 0.5, measures 1.5: `SpentMs` is 1.5 (an overrun is visible), `PredictedMs` 0.5, and the model learns (mean 0.625, prediction 1.125), so the next job (1.5 + 1.125 > 2) waits |

### E4. `Tools\run_tests.py`: run the tests and believe only what ran

| Line | What it does, and why |
|---|---|
| Docstring | Why a script: the runner **does** exit with −1 when a test reported an error (`AutomationCommandline.cpp:493–504`), but exit code 0 can't say *which* tests ran. A stale build without a new test, or a too-narrow filter, still exits 0 |
| `--expect N` (required) | The fewest **distinct** tests that must report; required, so every caller states how many it means to run (code review) |
| `os.remove(LOG)` in `try` | Never read a previous run's results; a log still locked by a running editor gives a clear `FAILED` line instead of a traceback (code review) |
| `-ExecCmds="Automation RunTests <filter>;Quit" -unattended -nullrhi -nosplash -nosound -nopause -log -abslog=…` | The editor, headless, runs the tests whose path starts with the filter, then `Quit` (a forced exit, `AutomationCommandline.cpp:39, 726–734`) once they are done |
| `run_and_reap(command, args.timeout)` | The shared launch–wait–kill (E5) |
| `RESULT` regex over the log | One line per test, `Test Completed. Result={…} Name={…} Path={…}` (`AutomationControllerManager.cpp:43, 57`) |
| `distinct = len({path ...})` | A test is counted once, even if its line appears twice |
| `exit_code != 0 or failed or distinct < args.expect` → exit 1 | Fails on a timeout (`None`), a non-zero exit, any result that isn't `Success`, or too few tests. The queue sees the exit code |

### E5. `Tools\run_bench.py`: `run_and_reap` (simplify)

| Line | What it does, and why |
|---|---|
| `def run_and_reap(command, timeout) -> int \| None` | `subprocess.Popen(command)` (one string, so `-Key="…"` reaches Unreal as written); `wait(timeout)`; `None` on a timeout; **`finally`:** if it is still running, `taskkill /F /T` its tree and wait up to 30 s, so Windows releases its files and nothing runs into the next job. Used by `launch` and by `run_tests.py`, so both tools kill and wait the same way (it replaced two copies; the test runner's copy waited without a timeout) |
