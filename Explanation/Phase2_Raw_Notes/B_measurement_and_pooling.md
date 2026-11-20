> **Raw Phase 2 notes (B_measurement_and_pooling), UE 5.8.4, 2026-11-21.** Every claim cites a file:line in the local engine source or a URL. Claims the design depends on are restated in [../01b_Research_Findings.md](../01b_Research_Findings.md).

# P02 SpawnPacer · Phase 2 notes, part B: measuring a hitch, and what a pool must reset

Paths are relative to `Engine/Source/Runtime/` unless stated otherwise.

---

## B1. Frame time is wall-clock time, also in fixed-step runs

| Fact | Evidence |
|---|---|
| CSV `FrameTime` = difference of two `FPlatformTime::Seconds()` stamps, one per frame | `Core/Private/ProfilingDebugging/CsvProfiler.cpp:4383–4390` |
| `GameThreadTime`, `RenderThreadTime`, `GPUTime` come from `GGameThreadTime`, `GRenderThreadTime`, `RHIGetGPUFrameCycles()` | `Launch/Private/LaunchEngineLoop.cpp:1515–1525` |
| `-deterministic` = fixed time step + fixed seed | `Launch/Private/LaunchEngineLoop.cpp:2457–2460` |
| In fixed-step mode the engine sets the delta to the fixed value and doesn't wait ("this causes it to lie about how fast it is going") | `Engine/Private/UnrealEngine.cpp:3014–3022` |

**Consequence:** with `-deterministic -FPS=60` every configuration simulates exactly the same frames (same wave start frame, same lifetimes), while `FrameTime` still shows the real cost of each frame. A 100 ms spawn frame shows as 100 ms even though game time advanced 16.67 ms. This is what makes "frames over 33.3 ms" a fair count across configurations. The same choice was made and checked in UE5-UpdateBudget (its 01b §4).

**Caveat:** in fixed-step mode a slow frame does not make game time jump. In a real game, a 100 ms hitch also causes a 100 ms simulation step. Our wave-completion latency is therefore reported in **frames and in simulated seconds**, and an uncapped real-time confirmation run is part of the plan (01 §7).

## B2. CSV capture control

| Fact | Evidence |
|---|---|
| `-csvStartOnEvent=`, `-csvStopOnEvent=`, `-csvCaptureOnEventFrameCount=` | `CsvProfiler.cpp:5078, 5081, 5090` |
| `-ExitAfterCsvProfiling` requests exit when a capture ends | `CsvProfiler.cpp:4309–4312` |
| The `Exclusive` category is on by default | `CsvProfiler.cpp:73` |
| A stat first recorded mid-capture is missing from the first header row and present in the **end** header row | Observed in UE5-UpdateBudget batch `2026-11-20_235024_s2wave` (its wave run): `Exclusive/GameThread/ActorSpawning` appears only in the final header. That project's `csv_stats.py` already reads the end header; our copy must keep that rule |
| Observed scale in that same capture (500 cube actors with one gameplay component, spawned by deferred spawn in one frame) | The wave frame took 15.6 ms against a 3.7 ms steady state. **Not our measurement and not our workload**: it only says a 500-actor wave of *trivial* actors is far from 33.3 ms, so our workload must use heavier, realistic classes (01 §6.1) |

## B3. Memory measurement

| Fact | Evidence |
|---|---|
| LLM is compiled into Development (`!UE_BUILD_SHIPPING && (!UE_BUILD_TEST || ...)`) | `Core/Public/HAL/LowLevelMemTrackerDefines.h:25–29` |
| It is off unless `-LLM` is on the command line; `-LLMCSV` writes its own CSV | `Core/Private/HAL/LLM/LLM.cpp:2318, 2324` |
| `FPlatformMemoryStats::UsedPhysical` = "the amount of physical memory used by the process" | `Core/Public/GenericPlatform/GenericPlatformMemory.h:141` |
| The CSV already has `PhysicalUsedMB` and `MemoryFreeMB` columns | UE5-UpdateBudget capture header (observed) |

**Plan:** pool memory is measured in **separate** runs with `-LLM` (LLM adds its own overhead, so it never runs in timing runs). Reported: LLM total and per-tag deltas, plus `PhysicalUsedMB`, between "pool empty" and "pool holding 500 parked actors", and the same for "500 live actors" (01 SC6).

## B4. Garbage collection after a destroy wave

| Fact | Evidence |
|---|---|
| `gc.TimeBetweenPurgingPendingKillObjects`: 60 s in code, **61.1 s** in `BaseEngine.ini` | `Engine/Private/UnrealEngine.cpp:1777–1779`; `Engine/Config/BaseEngine.ini:1749` |
| `UEngine::ForceGarbageCollection(bool bFullPurge = false)` | `Engine/Classes/Engine/Engine.h:2828` |

**Plan:** a 60 s GC interval means whether a GC lands inside a capture is luck. The benchmark therefore **forces one full GC at a fixed scripted time** after each despawn, identically in every configuration (01 §6.1). The GC frame then measures how much garbage the configuration created: destroy-based configurations create 500 actors' worth per wave, pooled ones almost none.

## B5. What a parked (pooled) actor still is

| Fact | Evidence |
|---|---|
| `AActor::SetActorHiddenInGame`, `SetActorEnableCollision`, `SetActorTickEnabled` | `Engine/Classes/GameFramework/Actor.h:1922, 1926, 2898` |
| An actor's own tick does not switch off its components' ticks: `SetActorTickEnabled` only calls `PrimaryActorTick.SetTickFunctionEnable(bEnabled)` | `Engine/Private/Actor.cpp:1754–1760`. SpawnBudget documents and works around exactly this (C §C2) |
| Mass's pool does only `SetActorHiddenInGame(true)` itself and leaves the rest to `IMassActorPoolableInterface::PrepareForPooling` | `Plugins/Runtime/MassGameplay/Source/MassActors/Private/MassActorSpawnerSubsystem.cpp:119–146` |

## B6. What survives into the next life of a pooled actor

| Fact | Evidence |
|---|---|
| The engine never clears an actor's timers on `EndPlay` or destroy: the only callers of `ClearAllTimersForObject` under `Engine/Private` are AutoRTFM tests | grep of `Source/Runtime/Engine/Private` **[inferred]**; A §A13 |
| `FTimerManager::ClearAllTimersForObject(void const* Object)` is public | `Engine/Public/TimerManager.h:291–297` |
| **No public way to ask how many timers an object has.** The per-object index `ObjectToTimers` is private | `TimerManager.h:550` (private member); public queries take a handle (`IsTimerActive`, `TimerExists`, 331, 398); `ListTimers()` only logs (482) |
| Latent actions *can* be counted and removed: `FLatentActionManager::GetNumActionsForObject`, `RemoveActionsForObject` | `Engine/Classes/Engine/LatentActionManager.h:138, 149` |
| Reflection can compare and print any `UPROPERTY`: `FProperty::Identical_InContainer`, `ExportTextItem_InContainer` | `CoreUObject/Public/UObject/UnrealType.h:528, 592` |

**Consequences for the reset contract [inferred]:**
- **Timers:** clear them unconditionally on release (actor and every component). They can't be verified directly. They are verified **by effect**: a seeded "leaky" class whose timer changes a `UPROPERTY` when it fires is caught by the property comparison (01 SC5).
- **Latent actions:** cleared on release, and their count verified to be 0 after acquire.
- **Gameplay state:** whatever the class stores in `UPROPERTY` fields can be compared field by field against a snapshot of the same actor taken right after its first, fresh spawn. Plain C++ members (no `UPROPERTY`) are invisible to this check: a stated limit.
- **Structure:** components added at run time and actors attached at run time are counted (`GetComponents`, `GetAttachedActors`) and compared with the fresh snapshot.

## B7. Prediction maths for the cost model

RFC 6298, "Computing TCP's Retransmission Timer", https://www.rfc-editor.org/rfc/rfc6298 (opened 2026-11-21), section 2:
- first sample R: `SRTT <- R`, `RTTVAR <- R/2`;
- later samples R': `RTTVAR <- (1 - beta) * RTTVAR + beta * |SRTT - R'|`, then `SRTT <- (1 - alpha) * SRTT + alpha * R'`, with `alpha = 1/8`, `beta = 1/4` ("SHOULD");
- the timeout `RTO <- SRTT + max(G, K*RTTVAR)`, `K = 4`.

**Why it fits:** the problem is the same shape. TCP must predict a noisy duration and pick a value that is rarely exceeded, without keeping a history. Mean plus K × mean deviation is cheap (two numbers per estimate) and adapts within a few samples. SpawnPacer uses the same two updates per class and stage, with its own K (02 §7), because the cost of an overestimate (wasted budget) and of an underestimate (an overrun frame) differ from TCP's.
