# 00 · Project Overview: SpawnPacer

> *"This was the problem. This was my investigation. This was my solution. These were my technical decisions. This was how I measured the results."*

**Status (2026-11-21):** Phases 1–4 are done: problem analysis (`01`), verified research (`01b`), final architecture v1.0 (`02`), build steps (`03`), and the UE project foundation (plugin and module skeletons, built and smoke-run through the machine queue; `09` Phase 4). **No system code and no results exist yet.**

---

## A. Identity

| | |
|---|---|
| **Name** | SpawnPacer: hitch-free content entry |
| **Technical category** | Frame-time consistency: actor lifecycle, spawn and despawn cost, pooling |
| **Main objective** | Let a 500-actor wave enter (and leave) the world with **no frame over 33.3 ms**, by running each spawn stage only when its **predicted** cost fits a per-frame millisecond budget, and let pooled actors be reused with a reset that is **checked**, not assumed |
| **Difficulty** | ★★★½☆ Advanced |
| **Core UE technologies** | `UWorld::SpawnActor` with `FActorSpawnParameters` (`bDeferConstruction`, `CustomPreSpawnInitialization`), `AActor::FinishSpawning`, component registration (`bAutoRegister`, `RegisterComponent`), the actor lifecycle (`PostSpawnInitialize` → `BeginPlay` → `EndPlay`), `UTickableWorldSubsystem`, `UINTERFACE`, `UDeveloperSettings`, reflection (`FProperty`), timers and latent actions, the CSV profiler, Unreal Insights, LLM |
| **Relation to existing work** | Time-sliced spawning and actor pooling already exist: in the engine's own Mass spawner (Experimental), in SpawnBudget and in PoolManager. They pace by **count** or by **time checked between items**, and leave pool reset to an interface nobody checks. SpawnPacer adds predicted-cost admission, staged spawns at the engine's two public seams, and a reset verifier with a seeded-defect test. Details: `01` §3, `01b` §5 |

## B–D. Problem, solution, architecture

- Problem, root-cause hypotheses, success criteria and the measurement plan: **[01_Problem_Analysis.md](01_Problem_Analysis.md)**
- What the engine actually does, and what that changed: **[01b_Research_Findings.md](01b_Research_Findings.md)**
- Solution and architecture (final v1.0): **[02_System_Architecture.md](02_System_Architecture.md)**
- Build steps, baselines first: **[03_Implementation_Roadmap.md](03_Implementation_Roadmap.md)**

## E. Technical demonstration

**What the viewer sees**
- A fixed camera over an arena. Every 12 seconds a wave of 500 actors appears: drones, crates, grunts and five bosses.
- An on-screen frame-time graph with two lines drawn on it: 16.7 ms (60 fps) and 33.3 ms (a visible hitch).
- A text overlay with the active configuration and the wave's completion time.

*The before/after pictures below are what the hypotheses (01 §4) predict. The recorded demo will show what Phase 8 actually measures, including anything that turns out differently.*

**Before** (B0 plain `SpawnActor`, then B1–B3)
- B0: the whole wave appears in one frame, and the graph shows a single tall spike far above 33.3 ms. The despawn and the garbage collection after it spike again.
- B1 (N per frame) and B2 (time cap checked between spawns): the wave arrives over several frames, but frames where heavy grunts or a boss land poke above the budget, and with B1 sometimes above 33.3 ms.
- B3 (simple pool): no construction cost, but re-activating 500 actors at once is still one heavy frame.

**After** (SPS and SPP)
- The wave fills in over tens of frames. The goal is a graph that stays below 33.3 ms, with spawn work per frame under the budget line (SC1, SC3: targets, not results).
- A boss appears in pieces over a few frames (its components are registered one per job) before it starts moving.
- SPP: the pool is filled during warm-up, so no actor is constructed or destroyed during any wave, and the garbage-collection frame should get much smaller (H5, to be measured).
- A soak run prints the reset verifier's result: 10,000 reuse cycles with 0 leaks on the clean classes, and every seeded leak caught.

**Recorded material:**
- a side-by-side video of B0 and SPS on the same wave;
- frame-time charts of every configuration over the three waves;
- a per-stage cost chart per class (construct, register, finish);
- the latency-versus-hitch trade-off chart;
- the soak report.

## G. Portfolio presentation (produced in Phase 10)

1. Problem statement, one paragraph (from `01` §1).
2. The investigation: the spawn path read line by line, what a plugin can and cannot split (`01b` §0), and what the baseline captures showed for H1–H7.
3. The architecture diagram (`02` §5) and the per-frame flow (`02` §6).
4. Key decisions and rejected alternatives: prediction instead of check-after, head-of-line order, staged spawning opt-in per class, reflection snapshot instead of hand-written reset checks, no async physics.
5. Before/after benchmarks against **all four** baselines, with the full environment and method.
6. What didn't work or didn't improve, stated plainly.
7. Video, source repository and the complete `Explanation/` folder.

## H. Industry relevance

**Skills demonstrated:**
- engine-level understanding of the actor lifecycle: what `SpawnActor`, `SpawnActorDeferred`, `FinishSpawning` and component registration actually do, read from the 5.8.4 source;
- frame-time thinking: budgets, tail latency (p99, max), hitch counts, and the latency trade-off;
- online cost estimation (mean plus deviation, as TCP estimates its round-trip time);
- object lifetime safety: half-spawned actors, actors destroyed behind the pool's back, re-entrant spawns from BeginPlay;
- testing a test: a reset verifier is only trusted after it catches seeded defects;
- honest benchmarking against the engine's and the community's existing solutions.

**Studios and roles that ask for this:**
- CDPR's Expert Gameplay Programmer (AI): "spawning and systemic open world", "monitoring performance and memory";
- Riot's Engine Performance role: profiling with Unreal Insights;
- Epic's own hitch-hunting talk lists spawning among the top causes, and Epic staff said studios build their own time-sliced spawners.

Sources: [research/01](Research/01_job_market.md) (C4, R1), [research/02](Research/02_technical_challenges.md) [T1-A, T1-D].

**Interview questions it prepares you for:**
- "What actually happens, step by step, when you call `SpawnActor`? Which part is expensive?"
- "What does `SpawnActorDeferred` defer, and what doesn't it?"
- "You need 500 enemies in an ambush without a hitch. What do you do, and what does it cost you?"
- "Why is 'stop when the time is up' not a budget? How do you never start work that won't fit?"
- "How do you predict the cost of something you've only measured a few times?"
- "Your pooled enemies sometimes come back with zero health. How do you find every bug of that kind, not just this one?"
- "What can't be reset by reflection, and how do you handle it?"
- "Why not spawn on another thread?"
- "How do you prove your spawner removed hitches, rather than moved them somewhere you didn't measure?"
