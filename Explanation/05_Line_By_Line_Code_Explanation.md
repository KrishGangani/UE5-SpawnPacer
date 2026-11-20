# 05 · Line By Line Code Explanation: SpawnPacer

**Rule:** every file that exists in `SpawnPacer\` and `Tools\` is explained here, line by line, in the same step it is written. Sections follow the build order in [03_Implementation_Roadmap.md](03_Implementation_Roadmap.md).

| Part | Step | Files |
|---|---|---|
| A | Phase 4, F1–F4 (foundation) | `.uproject`, two `Target.cs`, two `Build.cs`, two module `.cpp`, `.uplugin`, three config `.ini`, `Tools\*.bat`, `Tools\perf_cores.py` |
| B… | S1 onwards | added with each step |

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
