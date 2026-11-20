# Phase 2 raw notes

These are the working notes behind [01b_Research_Findings.md](../01b_Research_Findings.md). They are kept so every statement in the findings can be traced back to the exact engine line or web page it came from.

| File | Scope |
|---|---|
| [A_spawn_pipeline.md](A_spawn_pipeline.md) | `UWorld::SpawnActor`, `SpawnActorDeferred` / `FinishSpawning`, component registration, BeginPlay ordering, ticks, destroy, async physics-state creation, level-streaming registration |
| [B_measurement_and_pooling.md](B_measurement_and_pooling.md) | Engine timers and CSV stats around spawning, fixed-step timing, memory measurement, GC, and the engine facts a pool's reset contract depends on (timers, latent actions, reflection) |
| [C_existing_work.md](C_existing_work.md) | Mass actor spawner and pool, SpawnBudget, PoolManager, Fab pools, Epic forum statements, and what each one leaves unsolved |

**Conventions:**
- Engine paths are relative to `C:\Program Files\Epic Games\UE_5.8\Engine\`, UE **5.8.4** (`Build/Build.version`: 5.8.4, CL 58902052). Line numbers are from that install.
- **[inferred]** marks a conclusion drawn by following code flow rather than a direct statement. **[unverified]** marks something not checked.
- Read-only research. No engine file was modified. No third-party plugin was installed.

**The findings file is authoritative.** These notes are the audit trail.
