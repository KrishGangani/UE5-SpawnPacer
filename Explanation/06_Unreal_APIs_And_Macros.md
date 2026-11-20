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
