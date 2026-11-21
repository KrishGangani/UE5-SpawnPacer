# 12 · Future Scalability: SpawnPacer

What the current design would need next, in order of value (from 02 §12–13 and the measurements in 09).

1. **Finish SC5:** build the seeded-defect class and the 10,000-cycle soak (drafted in S8, never compiled), then run the clean and seeded runs.
2. **Settings, cvars, Blueprint surface (S9):** a `UDeveloperSettings` with per-class rules (split mode, pool cap, verifier ignores), `sp.*` cvars including a pass-through mode, and `BlueprintCallable` wrappers with a dynamic batch event.
3. **Benchmark SPP and the matrix:** the pool path was never measured; a balanced B0–B3 / SPS / SPP matrix, budget and K sweeps, an uncapped real-time run (SC1), and LLM pool memory (SC6).
4. **Seed the cost model:** save the per-key estimates at the end of a session (or from a cook-time pass) and load them, so the first wave isn't cold.
5. **Robustness to slow bursts:** one slow stretch inflates the deviation and makes jobs run alone for a while (09 S6, round 6). A capped or decaying deviation would bound that latency cost.
6. **Many worlds and streaming:** one subsystem per world already isolates PIE clients; level-streamed content could feed requests in at Background priority.
7. **A future engine API:** the policy (`FSpawnPacerScheduler`, `FSpawnCostModel`) is separate from the mechanism, so it could sit on top of an engine-level amortised spawn if one ships (01b #14).
