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

---

## Added in step S1

## 7. Classes, inheritance, `virtual` and `override`

```cpp
struct Animal       { virtual void Speak() {} virtual ~Animal() = default; };
struct Dog : Animal { void Speak() override { /* bark */ } };
```
- `class Dog : public Animal` means a dog *is an* animal: it has everything the base has and may replace `virtual` functions.
- A `virtual` call is decided at run time through a hidden table of function pointers (the vtable). `override` asks the compiler to check that a base function really is being replaced, so a typo becomes an error.
- **In our code:** `ASPDrone : public ASPWaveActor : public AActor`; `ASPDrone` overrides `BeginPlay` and `Tick`, and calls `Super::BeginPlay()` first so the engine's own work still runs (`Super` is a typedef UHT adds for the base class).

## 8. Namespaces, `constexpr` and named constants; `enum class`

```cpp
namespace Grid { constexpr double Spacing = 200.0; }   // compile-time, typed, named, scoped
enum class EState : uint8 { Idle, Engaging };           // scoped: EState::Idle; no silent int conversion; 1 byte
```
- **In our code:** `SPWave::PartRingRadius`, `SPBench::GridSpacing`, `SPBrain::ThinkInterval`: no magic numbers, each with its reason next to it. `ESPBrainState`, `ERunPhase`, `ECycleStep` are scoped enums.

## 9. `static_cast` and integer division

```cpp
const int32 Drones = Count * 50 / 100;   // integer maths: 500 * 50 / 100 = 250 exactly; 7 * 50 / 100 = 3 (truncates)
const uint8 Next = (static_cast<uint8>(State) + 1) % 3;   // explicit, visible conversion of an enum to its number
```
- **In our code:** the wave's class counts (the remainder goes to bosses, so the total is always the wave size); the brain's state cycle.

## 10. References vs pointers

```cpp
void A(UWorld* World) { if (World) { … } }   // may be null: check it
void B(UWorld& World) { … }                   // can't be null: no check needed
```
- **In our code:** `USPBaselineSpawner::RunFrame(UWorld& World)` takes a reference, because the director always has a world. Functions that may get "nothing" take pointers and check them (`if (USPBenchmarkSubsystem* Bench = World->GetSubsystem<…>())`).

## 11. A "sink" keeps measured work alive

```cpp
float Sink = 0; for (...) Sink = f(Sink);   // if Sink were never used, the optimiser could delete the loop
Member = Sink;                              // storing it makes the result observable, so the loop must run
```
- **In our code:** `USPWaveBrainComponent::WorkSink`. Each iteration also depends on the previous one, so the loop can't be vectorised: its cost stays proportional to the iteration count, which is what calibration needs.

## 12. `static` locals

```cpp
Widget& Get() { static Widget Instance; return Instance; }   // built once, on the first call
```
- **In our code:** `static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(...)` in each wave class's constructor: the asset lookup runs once per class (on its class default object), not once per spawned actor.

## 13. Lambdas and what they capture

```cpp
auto Add = [this](TSubclassOf<ASPWaveActor> Class, int32 Number) { … Wave.AddDefaulted_GetRef() … };
```
- The capture list `[this]` lets the lambda use the object's members; it copies only a pointer, so the object must outlive the lambda (here the lambda is used and discarded inside one function).
- **In our code:** `BuildWave` uses one small lambda to add N entries of a class, instead of four copies of the same loop.

## 14. `const` member functions

```cpp
int32 GetAmmo() const { return Ammo; }   // promises not to change the object; callable on const objects
```
- **In our code:** every getter (`GetBrain`, `GetHealth`, `GetNumLive`, `IsIdle`) and `WriteRunReport() const`.

## Added in step S4

## 15. A type as a map key: `operator==` and a hash function

```cpp
struct FKey { const void* A; uint8 B; bool operator==(const FKey& O) const { return A == O.A && B == O.B; }
              friend uint32 GetTypeHash(const FKey& K) { return HashCombine(GetTypeHash(K.A), GetTypeHash(K.B)); } };
TMap<FKey, double> Costs;   // needs both: the hash picks the bucket, == finds the entry in it
```
- A hash map puts each key in a bucket chosen by its hash, then compares keys inside the bucket with `==`. Two keys that are `==` **must** have the same hash, or lookups miss.
- `friend` inside the struct defines a free function the compiler finds by the argument's type (argument-dependent lookup), which is how `TMap` finds `GetTypeHash`.
- **In our code:** `FSpawnCostKey` (class, stage, component name): every field that `==` compares also goes into the hash.

## 16. Passing a function: templates, owning wrappers and borrowed references

```cpp
void RunAll(TFunctionRef<double()> Job);    // borrows a callable for the duration of the call: no allocation
RunAll([this]() { return Next++; });        // the lambda lives until the end of this statement: long enough
```
- A function can take "something callable" three ways: a template parameter (fastest, but the code lives in the header), an **owning** wrapper that copies the callable and may allocate (`TFunction`, like `std::function`), or a **borrowing** reference (`TFunctionRef`, like C++26 `std::function_ref`) that stores only a pointer to it.
- A borrowed reference must not outlive what it points to: never store a `TFunctionRef` in a member.
- **In our code:** `FSpawnPacerScheduler::RunFrame` borrows `PeekHead` and `RunHead` for one call, so the policy knows nothing about what a job is and the tests can pass fake jobs (06 §21).

## 17. Comparing floating-point numbers

```cpp
0.1 + 0.2 == 0.3                       // false: neither side is exact in binary
FMath::Abs(Actual - Expected) <= 1e-9  // "equal within a tolerance"
```
- Most decimal fractions have no exact binary form, so sums drift in the last bits. Tests compare with a tolerance; code that adds up many small values must expect the total at a boundary to land on either side of it.
- **In our code:** the policy tests pass `Tolerance = 1e-9` to `TestEqual`; `ManyTinyJobsStopAtTheBudget` accepts 249 or 250 jobs of 0.002 ms in 0.5 ms, because whether 250 × 0.002 adds up to exactly 0.5 depends on rounding.

## 18. `explicit` constructors and `static constexpr` members

```cpp
class FModel { public: static constexpr double Alpha = 1.0 / 8.0;   // one compile-time constant per class
               explicit FModel(double K = 2.0); };                  // no silent conversion from double
FModel M = 3.0;   // error with explicit; without it, a double would quietly become a model
```
- `explicit` stops a one-argument constructor from acting as an implicit conversion.
- **In our code:** `FSpawnCostModel(double InDeviationMultiplier = 2.0)` is `explicit`; `Alpha` and `Beta` are `static constexpr`, with RFC 6298's wording next to them.

## Added in step S6

## 19. `static` functions in a `.cpp`, and file-local helpers

```cpp
static TOptional<EMode> ParseMode(const FString& Name) { … }   // visible in this .cpp only (internal linkage)
namespace SpawnPacer { void HoldBackComponents(AActor&, …); }   // a named namespace: a scope, still linkable
```
- `static` on a free function gives it **internal linkage**: the linker never sees it outside this file, so another file may use the same name. A header only declares what other files need.
- **In our code:** the director's `ParseSplitMode` is file-local (the header would otherwise need the plugin's enum); the subsystem's `HoldBackComponents` lives in the `.cpp`'s `SpawnPacer` namespace next to the constants it uses.

## 20. Sorting with a comparison: `StableSort` and pairs

```cpp
TArray<TPair<int32, USceneComponent*>> ByDepth;   // (attachment depth, component)
ByDepth.StableSort([](const auto& A, const auto& B) { return A.Key < B.Key; });
```
- A **stable** sort keeps equal elements in their original order. Two parts at the same depth keep the order the engine lists them in, so the order is deterministic.
- `TPair<K, V>` holds two values, `.Key` and `.Value`, like `std::pair`'s `first` and `second`.
- **In our code:** held-back components sorted parent first (05 G2).
