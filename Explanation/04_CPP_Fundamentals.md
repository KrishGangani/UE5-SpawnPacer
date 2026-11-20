# 04 · C++ Fundamentals: SpawnPacer

**Rule:** only the language ideas this project's code actually relies on, each with a tiny example first and then where it appears in our code. This file grows with every implementation step.

**State (Phase 4):** the project has two tiny C++ files (the module entry points) and four C# build files. So this file covers what *those* need: how a C++ program is assembled from files, what a macro is, and the bit of C# that Unreal's build rules are written in. The C++ used by the system itself (classes, `virtual`, references, lambdas, `TFunction`, weak pointers) is added in steps S1–S9.

---

## 1. Translation units, `#include` and the preprocessor

```cpp
// Widget.h                        // Main.cpp
#pragma once                       #include "Widget.h"      // the preprocessor pastes Widget.h's text here
int CountWidgets();                int main() { return CountWidgets(); }
```

- The compiler compiles each `.cpp` file **on its own**. Together with every header it includes, that is one **translation unit**.
- `#include "File.h"` is plain text substitution, done by the **preprocessor** before compilation. The compiler never sees the `#include` line, only the pasted text.
- The **linker** then joins the compiled translation units (and libraries) into one program or DLL, matching each use of a function with its one definition.
- `#pragma once` tells the preprocessor to paste a header only once per translation unit, even if it is included twice.
- **Include what you use (IWYU):** include the header that declares what you need, not a giant "everything" header. Each `.cpp` compiles faster and its dependencies are visible.
- **In our code:** `SpawnPacerModule.cpp` and `SpawnPacerCoreModule.cpp` each include exactly one engine header, `"Modules/ModuleManager.h"`, because that header declares the macros and classes they use (05 A4, A6).

## 2. Macros

```cpp
#define SQUARE(X) ((X) * (X))      // text replacement with parameters
int Nine = SQUARE(3);              // the compiler sees: int Nine = ((3) * (3));
```

- A **macro** is a preprocessor rule that replaces a name (and its arguments) with text before compilation.
- Unreal uses macros for **boilerplate that must look the same in every module**: entry points, reflection (`UCLASS`, `GENERATED_BODY`), logging. One line in our file expands into a page of standard code.
- **Costs:** a macro has no type checking, and an error inside its expansion points at the line that *used* it, which can confuse. Read the macro's definition when an error is unclear (05 A4 cites where each is defined).
- **In our code:** `IMPLEMENT_PRIMARY_GAME_MODULE(...)` and `IMPLEMENT_MODULE(...)`, defined in `Core/Public/Modules/ModuleManager.h:946–1136` (5.8.4).

## 3. Modular vs monolithic builds

| | Editor build (modular) | Packaged game (monolithic) |
|---|---|---|
| Each module becomes | its own DLL, e.g. `UnrealEditor-SpawnPacerCore.dll` | part of one executable, `SpawnPacer.exe` |
| A module is found by | the module manager loading its DLL and calling its exported entry function | a static registration object created when the program starts |

- The same source code builds both ways. That is why the module macros have more than one definition, chosen by the preprocessor: `#if IS_MONOLITHIC || UE_MERGED_MODULES` at `ModuleManager.h:943` picks one of the two `IMPLEMENT_MODULE` definitions (946 or 955), and `#if IS_MONOLITHIC` at 1097 picks among the primary-game-module definitions (1100 and 1114 for monolithic desktop and other platforms, 1130 for modular builds).
- **In our build logs (09 F-1, F-2):** the editor build links `UnrealEditor-SpawnPacer.dll` and `UnrealEditor-SpawnPacerCore.dll`; the game build links one `SpawnPacer.exe`.

## 4. Comments

```cpp
// A line comment: from // to the end of the line.
/* A block comment: everything between the markers. */
```

- Comments are removed by the preprocessor and cost nothing at run time.
- **In our code:** every file starts with a one-line comment saying which project and module it belongs to, and each non-obvious line says *why*, not *what*.

## 5. C#: classes, inheritance and constructors (for the build rules)

Unreal's build rules (`*.Target.cs`, `*.Build.cs`) are **C#**, compiled and run by UnrealBuildTool (UBT) before any C++ is compiled.

```csharp
using UnrealBuildTool;                               // bring a namespace's names into scope

public class MyGame : ModuleRules                    // MyGame derives from (inherits) ModuleRules
{
	public MyGame(ReadOnlyTargetRules Target)        // the constructor: runs when UBT creates the object
		: base(Target)                               // first run the base class's constructor with Target
	{
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "Engine" });   // a list of strings
	}
}
```

- `class X : Y` means X **inherits** Y: it has all of Y's fields and methods and can set them. UBT reads those fields after the constructor runs.
- `: base(Target)` chains to the base class's constructor. Without it the base fields would not know which target is being built.
- `new string[] { "A", "B" }` creates an array; `AddRange` appends every element to a list.
- **Assignment in the constructor is configuration:** `Type = TargetType.Game;` doesn't *do* anything when it runs; it records a choice that UBT acts on later.
- **In our code:** two target classes (`SpawnPacerTarget : TargetRules`, `SpawnPacerEditorTarget : TargetRules`) and two module classes (`SpawnPacer : ModuleRules`, `SpawnPacerCore : ModuleRules`), 05 A2–A3, A6.

## 6. Bit masks (used by the CPU-pinning tool)

```text
CPU:       23 22 ... 16 | 15 14 ... 1 0
mask 0xFFFF  =   0 0 ... 0  |  1  1 ... 1 1     → "run only on logical CPUs 0–15"
combined |= core_mask       (bitwise OR: switch on the bits of this core, keep the others)
```

- A **bit mask** stores a set of small numbers as the bits of one integer: bit *n* set means "member *n* is in the set".
- `|` (OR) adds members; `&` (AND) keeps only members in both.
- Hex is used because each hex digit is exactly 4 bits: `0xFFFF` is 16 ones.
- **In our code:** `Tools\perf_cores.py` ORs together the masks of the cores with the highest efficiency class (the P-cores), giving `0xFFFF` on this laptop (05 A9).
