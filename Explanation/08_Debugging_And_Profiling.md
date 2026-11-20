# 08 · Debugging And Profiling: SpawnPacer

**Status:** not written yet. Filled in **Phases 5–8**: harness parts in step S2, the rest when the system exists. No system code exists yet.

**Will contain:** how to see the system working, with exact commands for UE 5.8.4:
- CSV profiler captures started and stopped by the game's events, and the columns that matter (`FrameTime`, `SpawnPacer/*`, `Exclusive/GameThread/ActorSpawning`, `Exclusive/GameThread/ActorDestroying`; 01b §4);
- the engine's `spawnactortimer start` / `stop` command as a cross-check of per-class cost;
- an Unreal Insights capture of one wave, with the engine's per-phase scopes (`STAT_PostActorConstruction`, `STAT_ActorBeginPlay`, `STAT_RegisterComponent`, `STAT_ComponentCreateRenderState`, `STAT_ComponentCreatePhysicsState`);
- `-LLM` memory runs;
- the `sp.*` console variables and the `sp.Debug` overlay;
- how to read a reset-verifier report.
