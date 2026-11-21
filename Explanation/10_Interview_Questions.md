# 10 · Interview Questions: SpawnPacer

Answers are grounded in this project's code and measurements (09).

**1. Why does spawning 500 actors hitch, and what part of it is expensive?**
A plain `SpawnActor` does construction, native component registration (render and physics state), the construction script and BeginPlay in one call (01b #1, #2, #6). The baseline wave peaked at 84 ms. In the deferred baseline, `SpawnActor`, which includes registration, was 57% of the spawn time (09 S3, H1).

**2. What can a plugin do about it without engine changes?**
Spread the work over frames and cut a spawn only at the engine's public seams: after construction and registration (`SpawnActorDeferred` + `FinishSpawning`), and per native component (`bAutoRegister` cleared in `CustomPreSpawnInitialization`, then `RegisterComponent` one per job). `FinishSpawning` itself is indivisible (01b §0).

**3. Time-sliced spawners already exist. What is different here?**
They check the clock **between** items, so the item that starts under the limit can run far over it. SpawnPacer **predicts** each job's cost per class and stage (an RFC 6298 estimator, mean + 2 × deviation) and doesn't start a job that won't fit. At the same 2 ms budget, the time cap was over 1.1 × budget in 37% of its spawn frames; SpawnPacer in 0.3% (09).

**4. What happens with a job you have never measured?**
It is "cold": predicted as the whole budget, and run only as a frame's first job. A component never measured is predicted from its family (the same actor class and stage), so a 23-part boss isn't 23 cold frames (05 G1).

**5. What if one job is bigger than the budget?**
The frame's first job always runs (progress), and it is counted as **forced** when predicted over the budget. Overruns are reported, never hidden. At 1 ms, a whole boss gave 81 forced frames; split, 8–9 (09 S6).

**6. Was the per-component split worth it?**
Not for this boss. At 2 ms the three modes were equal within noise. At 1 ms the Deferred seam already removed the forced frames, and per-component added jobs without a further gain. It would matter for an actor whose registration alone exceeds the budget (09 S6).

**7. Why is the work done in `OnWorldPostActorTick` and not in a tickable subsystem?**
Tickable objects have no guaranteed order, so a request from another tickable could wait a frame by luck. The post-actor-tick broadcast comes after every tick group and every tickable, so a request is always served in the frame it is made (02 §4.1).

**8. What does pooling have to reset that the engine doesn't?**
Timers and latent actions (the engine clears neither), component ticks (`SetActorTickEnabled` touches only the actor), collision and visibility (restored to the class defaults), and the game's own state through `ISpawnPacerPoolable`. An actor parked in a frame isn't reused in that same frame, because latent removal is applied only at the next latent update (05 H2).

**9. How do you know a pooled actor was really reset?**
`FSpawnPacerResetVerifier` snapshots every reflected property and the structure of a fresh actor, and compares after each reuse. The 10,000-cycle soak with seeded defects (SC5) was **not run**; one automation test shows it catching a property left changed (09).

**10. What does pacing cost?**
Latency: a 500-actor wave takes 54 frames instead of B2's 42, because predictions leave headroom (utilisation 0.86 of the budget) and spreading spawns does about 25% more total work than one burst (09 S3, S6).
