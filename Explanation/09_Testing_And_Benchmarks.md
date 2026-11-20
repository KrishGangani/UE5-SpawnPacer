# 09 · Testing And Benchmarks: SpawnPacer

**Rule:** every check is recorded with its date, its exact command and its result, **including failures**. The full functional and benchmark sections are filled in during Phases 5–8. Results from earlier phases are recorded as they happen.

**Every Unreal job below ran through the machine-wide queue** (`tools\ue_queue.py run --project P02 …`), never directly. The queue's job id is given so the full log can be found on the development machine.

---

## Phase 4 · Foundation checks (2026-11-21)

**Job `20261121_005146_3cea9c`** ("P02 Phase 4: build editor + game (Development), package Development, packaged smoke run"), one chained command, exit 0, 271.4 s:
`Tools\build.bat SpawnPacerEditor Development && Tools\build.bat SpawnPacer Development && Tools\package.bat Development && Tools\smoke_game.bat packaged`

| # | Check | Command (from `P02_SpawnPacer\`) | Result |
|---|---|---|---|
| F-1 | Editor target builds | `Tools\build.bat` (`SpawnPacerEditor Win64 Development`) | ✅ `Result: Succeeded` in 50.3 s; 12 actions (shared PCH, both module `.cpp`, two DLLs); **0 errors, 0 warnings**. Toolchain: MSVC 14.44.35229, Windows SDK 10.0.22621.0 |
| F-2 | Game target builds | `Tools\build.bat SpawnPacer Development` | ✅ `Result: Succeeded` in 38.3 s; 24 actions, linking one `SpawnPacer.exe`; **0 errors, 0 warnings** |
| F-3 | Package a **Development** build | `Tools\package.bat` | ✅ `BUILD SUCCESSFUL`, `BuildCookRun time: 144.70 s`; cook: 516 packages cooked (7 skipped by platform), **`Success - 0 error(s), 0 warning(s)`**; three `UnrealPak` runs exit 0. Output `Build\Windows\` (≈ 983 MB, git-ignored) |
| F-4a | Packaged game on the real GPU (first run) | `Tools\smoke_game.bat packaged` (then the bootstrap `Build\Windows\SpawnPacer.exe -windowed -ResX=1280 -ResY=720 -nosound -unattended -traceautostart=0 -ExecCmds="quit" -log -abslog=…`) | ✅ exit 0. `SpawnPacerCore` mounted; D3D12, SM6; **chosen adapter 0 = NVIDIA GeForce RTX 5070 Laptop GPU** (driver 610.47), not the integrated Intel GPU; `Template_Default` loaded in 0.144 s; clean `RequestExit(0)`. **0 errors, ⚠ 1 warning:** `LogEnhancedInput: Warning: UEnhancedInputLocalPlayerSubsystem … does not have a valid PlayerInput object. Failed to load user settings.` |
| F-4b | The same after the fix: `DefaultInput.ini` with the template's two input-class keys (05 A7), and the smoke script switched to the real game exe (05 A8) | Job `20261121_011548_19bb05`: `Tools\package.bat Development && Tools\smoke_game.bat packaged` (`Build\Windows\SpawnPacer\Binaries\Win64\SpawnPacer.exe -windowed -ResX=1280 -ResY=720 -nosound -unattended -traceautostart=0 -ExecCmds="quit" -log -abslog=…`) | ✅ job exit 0 in 70.1 s. Package: `BUILD SUCCESSFUL`, `BuildCookRun time: 64.45 s`, 516 packages cooked, cook **`Success - 0 error(s), 0 warning(s)`**. Game: exit 0; base directory `Build/Windows/SpawnPacer/Binaries/Win64/` (the real exe); `SpawnPacerCore` mounted; adapter 0 = RTX 5070 Laptop; `Template_Default` loaded in 0.087 s; clean `RequestExit(0)`. **0 errors, 0 warnings** (`: Warning:` and `: Error:` counts in the game log are both 0) |
| F-5 | Remove the engine-written `SecurityToken`: the `AndroidFileServer` plugin disabled in the `.uproject` and its block deleted from `DefaultEngine.ini` (05 A1, A7) | Job `20261121_014927_d6d7e7`: `Tools\build.bat SpawnPacerEditor Development && Tools\build.bat SpawnPacer Development && Tools\package.bat Development && Tools\smoke_game.bat packaged` | ✅ job exit 0 in 104.4 s. Editor build `Result: Succeeded` (4 actions, 10.9 s), game build `Result: Succeeded` (20.4 s), **0 errors, 0 warnings**. Package: `BUILD SUCCESSFUL` (66.3 s), 516 packages cooked, cook **`Success - 0 error(s), 0 warning(s)`**. **After the cook (which runs the editor), `DefaultEngine.ini` is unchanged: 0 `AndroidFileServer` or `SecurityToken` lines**, so the block is no longer written. Smoke run: exit 0, adapter 0, map in 0.092 s, **0 warnings, 0 errors** |

**What F-4a taught us:**
1. **A warning in a run is a defect, even when the build is clean.** The cause was read in the plugin source (`EnhancedInputSubsystems.cpp:115–119`): Enhanced Input is enabled by default, but without a `DefaultPlayerInputClass` setting the engine creates the classic player input. The fix is the two lines Epic's template uses, not a copy of the template's whole input file.
2. **The real exe, not the bootstrap.** The first run used `Build\Windows\SpawnPacer.exe`, a small launcher that starts the real game. For a smoke run that is harmless, but the benchmark launcher must see the game's own exit code, so both now use `Build\Windows\SpawnPacer\Binaries\Win64\SpawnPacer.exe`.

**Observations recorded for later steps (not defects):**
- **PSO creation on the first frame:** `LogPSOHitching: Encountered 50 PSO creation hitches so far (4 graphics, 46 compute). 0 of them were precached.` and `Waited for PSO creation for 100.000000ms`, both on frame 0 at startup, before any gameplay. F-4b, the second run of the same build, logged one PSO precache miss instead. This cold/warm difference is the evidence behind H6 (cold first spawn) and why every batch has a discarded priming run and a warm-up. The benchmark CSV records the `PSO/*` columns (seen in UE5-UpdateBudget captures) so first-use PSO cost during a wave is visible.
- **Slow shutdown, first run only:** in F-4a, 27 s passed between `Slate User Destroyed` and `LogExit: Preparing to exit`; in F-4b the same gap was 5 ms. The cause was not investigated **[unverified]**. F-4a was the first run of a fresh build on this machine, and it also had 50 PSO creation hitches where F-4b had one PSO precache miss (`Encountered a new graphics PSO … (PSOPrecacheState: Missed)`). So first-run work that a warm driver cache avoids is the likely explanation **[inferred]**. It doesn't affect measurements (captures end before exit), but the benchmark launcher's per-run timeout must allow for a slow first run, and the priming run absorbs it.
- **Engine-written config:** the first cook's editor run appended an `AndroidFileServerRuntimeSettings` block, with a random `SecurityToken`, to `DefaultEngine.ini`. It was first left as written, then removed in F-5 by disabling the plugin (05 A1, A7).

**CPU-pinning helper:** `python -I Tools\perf_cores.py` prints `0xFFFF` (P-cores = logical CPUs 0–15) on this machine, 2026-11-21. The copied `KERNEL32` setup, `performance_core_mask`, `pin_self` and `applied_mask` are identical to UE5-UpdateBudget's current `run_bench.py` (`diff` empty). A one-off check in a throwaway Python process: `applied_mask()` read `0xFFFFFF (all logical processors)`, `pin_self(0xFFFF)` returned `True`, and `applied_mask()` then read `0xFFFF (performance cores only; hybrid CPU)`. Builds and smoke runs are not pinned; timed runs will be (01 §7).

---

## Phases 5–8 (to be written)

- S1–S9 checks (functional tests, determinism hash, stage-equivalence test, soak), with their commands and results.
- The calibration, baseline, main, sweep, ablation, memory, soak and overhead batches, each with its batch name and summary (protocol: [Benchmarks/README.md](../Benchmarks/README.md)).
