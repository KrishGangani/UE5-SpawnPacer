# P02 · SpawnPacer: hitch-free content entry

**Domain:** Frame-time consistency · actor lifecycle · **Difficulty:** ★★★½☆ · **Engine:** UE 5.8.4

> **Status: planned; Phases 1–3 (problem analysis, research, architecture) are starting.** The ten projects are developed in parallel, and every Unreal Engine build and benchmark runs one at a time on the same machine, so measurements stay clean. Everything below is the plan. The success criteria are **targets fixed before any code exists, not results**. Real numbers will only come from controlled before/after runs, each traceable to a raw benchmark batch.

## The problem

Spawning waves of enemies, projectiles or props causes frame spikes. Epic's "Great Hitch Hunt" lists spawning among the top hitch causes, and the engine has no time-sliced spawn or general pool

## Proposed solution

A spawn scheduler that budgets by **predicted per-class cost**, so it never starts a spawn that can't fit. A per-phase cost breakdown (construct, register, physics, BeginPlay). A pool with an explicit reset contract and **automated state-leak checks**

## Main Unreal Engine technologies

`SpawnActorDeferred`/`FinishSpawning`, actor lifecycle, `IncrementalRegisterComponents`, async physics-state creation (Experimental), UINTERFACE, Insights

## Success criteria (targets, defined before implementation)

Frames > 33.3 ms during a scripted 500-actor wave: baseline count vs **0** target; p99 and max frame time during waves; wave-completion latency reported as the trade-off; **0** state leaks in 10k pooled reuse cycles; pool memory cost in MB

## Existing work it must be compared against

SpawnBudget (SimulatedFlow), PoolManager (JanSeliv), Fab pool plugins, `SpawnActorDeferred` pattern

**The gap it targets:** Predictive per-class cost budgeting, a per-phase spawn breakdown, verified pool reset (gap strength: Medium).

**Main risk:** Splitting one actor's spawn across frames may need engine changes. Phase 2 decides what's possible at plugin level

## Why studios care

Epic hitch talk; Epic stutter blog; CDPR Expert AI "spawning and systemic open world"; profiling roles (Riot R1)

## Method

Every project in the portfolio follows the same ten phases: problem analysis → research → architecture → foundation → implementation → explanation → testing → profiling → review → portfolio. Each phase is documented in `Explanation/` (files 00–12, including a line-by-line code walkthrough), and every claimed improvement must be measured against the existing solutions above on the same workload. Engine: Unreal Engine 5.8.4, C++.

## Folder layout

```text
P02_SpawnPacer\
├── README.md        ← this page (becomes the project's portfolio page)
├── Explanation\     ← 00–12, created when the project starts
├── Benchmarks\      ← Raw\ + Summaries\
├── Media\           ← diagrams, screenshots, demo video
├── Tools\           ← analysis and run scripts
└── SpawnPacer\    ← the Unreal Engine project (created in its Phase 4)
```
