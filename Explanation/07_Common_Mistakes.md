# 07 · Common Mistakes: SpawnPacer

**Rule:** for every module, the mistakes that are easy to make here, **how to spot them, how to prevent them and how to debug them**. Mistakes this project **actually made or caught** are marked ⚠ caught; they are the real interview stories.

**State (Phase 4):** only the build and run pipeline exists, so the first table is about that. The second table lists the spawn and pool mistakes the Phase 2 research showed are easy to make. Each one becomes a test or a design rule in the step named, and moves to a module section once that code exists.

---

## Build and run pipeline (Phase 4)

| Mistake | How to spot it | How to prevent it | How to debug it |
|---|---|---|---|
| ⚠ caught: **no `DefaultInput.ini` while Enhanced Input is enabled** | One startup warning in the packaged log: "UEnhancedInputLocalPlayerSubsystem … does not have a valid PlayerInput object" (09 F-4a) | Set the template's two class keys (05 A7) | Grep the packaged log for `Warning`; find the message in the plugin source to see the condition |
| ⚠ caught (in research): **passing flags the engine doesn't parse** (`-ExitAfterSeconds`) | Nothing happens, so nothing tells you | Grep `Engine/Source` for a flag before using it | `grep -rn "FlagName" Engine/Source --include=*.cpp` |
| ⚠ caught: **an engine plugin writes a random token into the project config** (Android File Server, on by default) | A `SecurityToken=` line appears in `DefaultEngine.ini` after the first editor run or cook | Disable plugins the project doesn't use in the `.uproject` (05 A1) | `git diff` after every cook; grep the config for values you didn't write |
| **Batch files with LF line endings** | `goto` reports a missing label, or lines run in the wrong place | `.gitattributes` forces CRLF for `*.bat`; check with `file *.bat` | Open the file in a hex view or run `file` |
| **Running the bootstrap exe when the exit code matters** | A failed run reports success | Run `Build\Windows\<Name>\Binaries\Win64\<Name>.exe` (05 A8, A9) | Compare the bootstrap's exit code with the real exe's on a run that fails on purpose |
| **`cmd /c start /wait` for pinned runs** | Every run "succeeds" (exit code 0 always, found by UE5-TestTrust) | Pin the batch script once with `pin_self`, then start each game with `subprocess.Popen` (05 A9) | Launch a command that exits 3 and check what comes back |
| **Starting a UE build or run outside the machine queue** | Another project's benchmark has an outlier | Every UE job through `tools\ue_queue.py` (stated in every script header) | `ue_queue.py status`; the queue's job history |
| **Expecting the Test configuration on a launcher engine** | "Targets cannot be built in the Test configuration with this engine distribution" | Headline builds are packaged Development (05 A8) | Read the UAT log |
| **An asset referenced only from C++ isn't cooked** (from S1) | Invisible actors in the packaged game; "Failed to load" in its log | `DirectoriesToAlwaysCook` for `/Engine/BasicShapes` | Grep the packaged log for the asset path |

## Spawn, split and pool mistakes the research predicts (become tests in Phase 5)

| Mistake | Why it happens | Prevention (design rule) | Test (step) |
|---|---|---|---|
| **Believing `SpawnActorDeferred` defers the expensive part** | Native components are registered inside it (01b #1) | Time the stages separately; per-component holding (S6) | Per-stage timing shows registration inside Construct (S3, S5) |
| **Registering held-back components after `FinishSpawning`** | It "works", but BeginPlay ran without them and their own BeginPlay runs at registration (01b #4) | The state machine runs `Finish` only when nothing is held back (02 §8) | BeginPlay sees every component registered (S6) |
| **Holding back the root, or registering a child before its parent** | Children attach to the root; the engine registers parents first (01b §2.2) | Never hold back the root; register in attachment order | Boss with nested components (S6) |
| **Trying to split Blueprint components** | They are created inside `FinishSpawning` (01b #5) | Per-component mode is native-only; stated in the setting's tooltip (S9) | — |
| **A half-spawned actor destroyed behind the scheduler's back** | Level unload or gameplay destroys it between stages | Weak pointer + `IsValid` before every stage; drop and count | Destroy between stages (S5) |
| **Changing the queue while iterating it** | BeginPlay requests more spawns | Requests made during the loop go to an incoming list (02 §8) | Spawn from inside BeginPlay (S5) |
| **Checking the clock after the job** | Overruns by one job (H4) | Predict before admitting (02 §2) | SC3 vs B2 (S5, Phase 8) |
| **Cold start treated as cheap** | An unknown class predicted as 0 ms is admitted into a full frame | Cold key = whole budget (02 §7) | Unit test (S4) |
| **Parked actors that keep ticking** | `SetActorTickEnabled` doesn't touch components (01b #12) | Park switches off and remembers component ticks | A parked actor costs no tick (S7) |
| **Timers surviving into the next life** | The engine never clears them on EndPlay or destroy, and a pooled actor isn't destroyed (01b #11) | Park clears timers and latent actions for actor and components | A timer set during use never fires after reuse (S7) |
| **Gameplay setup in BeginPlay for a pooled class** | BeginPlay runs once per object, not once per use | Per-use setup goes in `OnPoolAcquired` (02 §8) | Leaky class, seeded kinds (S8) |
| **Trusting a reset nobody checked** | Reset code covers what its author remembered | The verifier compares with the fresh snapshot (02 §11) | SC5 soak (S8) |
| **A verifier that reports nothing because it checks nothing** | An empty comparison passes everything | Seeded defects must be caught (SC5) | S8 |
| **A pool cap below the live population** | Release silently destroys | Count `Destroyed instead of parked` in telemetry | S7 |

## Game module: workload, director and baseline B0 (step S1)

| Mistake | How to spot it | How to prevent it | How to debug it |
|---|---|---|---|
| ⚠ caught (design): **two tickable subsystems assumed to tick in a fixed order** | A request is served this frame in one run and next frame in another; latency off by one | Drive one object from the other: the director calls `USPBaselineSpawner::RunFrame()` itself (02 §4.2) | Log `GFrameCounter` at the request and at the spawn |
| ⚠ caught (code review): **"ready" sent from a component's BeginPlay** | The actor's own BeginPlay work runs after the wave counts as complete | Notify from the actor's `BeginPlay`, after `Super::BeginPlay()` and a hook, and mark it `final` (05 B1) | Read `AActor::BeginPlay`: component BeginPlays run inside it (`Actor.cpp:4808–4845`) |
| ⚠ caught (code review): **a validation that accepts nonsense timing** (a negative `-SPGCAfter` passed) | A capture whose windows overlap, which still looks valid | Check every step's order, not only the sum | Feed the validator edge values on purpose |
| ⚠ caught (simplify): **string formatting in a spawned actor's constructor** | Spawn cost includes work a real class doesn't do | Build subobject names once (`PartNames()`, 05 B1) | Insights: `FString::Printf` under `NewObject` |
| ⚠ caught (first S1 run): **trusting a clean exit to carry an exit code** | A failed run reports 0 | On Windows, return a failure with a forced exit (`QuitRun`, 05 B4); a clean `RequestExitWithStatus` only posts a quit message | A deliberate invalid run, checking the code it returns (09 S1-c) |
| ⚠ caught (code review): **a refused benchmark run that idles until its timeout** | A batch stalls for minutes per bad run | Quit at once with a non-zero exit code (`QuitRun(2)`) | Run with an invalid `-SPConfig` and time it |
| **One shared static `FObjectFinder` in a helper used by several classes** | Every class shows the first class's mesh | Each class keeps its own static finder and passes the mesh in (05 B1) | Look at the packaged game: are the drones spheres? |
| **A non-static `FObjectFinder`** | Spawn cost includes an asset lookup per actor | `static` local: once per class (04 §12) | Insights: `LoadObject` inside `SpawnActor` |
| **Measured work the optimiser deletes** | Cost doesn't change with `-SPWork=` | Store the result (`WorkSink`) and make each step depend on the last (04 §11) | Sweep `-SPWork=` in the calibration batch (S2) and check the cost scales |
| **A collision test that moves or rejects spawns** | Different configurations spawn different waves | `AlwaysSpawn` for the seeded wave | Wave hash and actor count per wave in the run report |
| **`FMath::Rand` for the wave** | The wave hash differs run to run | A seeded `FRandomStream` | The S1 check: same seed, same hash |
| **Despawn or GC outside its cycle** | Analysis windows overlap; a GC lands in the next wave's window | `ValidateRunSettings` refuses such a run with an `Error:` | The run's log |
| **Wave completion counted differently per configuration** | Latency isn't comparable | Every configuration completes through `NotifyActorReady()` at the end of BeginPlay (02 §9) | Compare `ready` in the run report with the wave size |
| **Expecting `ForceGarbageCollection` to collect now** | The GC frame is the next frame or later | Treat it as "armed"; the GC window is 3 frames (01 §6.3) | CSV: the frame after `GCForced` |

## Baselines B1–B3 (step S3): mistakes the S3 code review and its fixes caught

| Mistake | How to spot it | How to prevent it | How to debug it |
|---|---|---|---|
| ⚠ caught: **a reused actor still holds state captured at its first BeginPlay** (the drone's circle centre) | Reused drones fly back to the park location | A per-use callback (`PrepareForReuse`) for everything BeginPlay captured | Watch one pooled drone over two waves |
| ⚠ caught: **parked actors keep running timers** | Frame time between waves differs from the other baselines | `PrepareForParking` stops the think timer; SpawnPacer's pool will clear all timers (02 §11) | `ListTimers` on the timer manager; count `Think` calls while parked |
| ⚠ caught: **filling a pool before the world has begun play** | Parked actors still think: their BeginPlay ran *after* they were parked | Fill on the first warm-up frame, once the world has begun play (05 D3) | Log BeginPlay and park order for one actor |
| ⚠ caught: **a despawn that leaves unserved spawns pending** | Actors from one wave appear in the next; `DespawnComplete` never fires | `RequestDespawn` cancels what wasn't brought in yet; the wave is reported incomplete | Compare `Added` per wave with the wave size |
| ⚠ caught: **a pool that silently drops a request on a miss** | One actor short per wave; `WaveComplete` never fires | Pop until valid, then spawn on a miss (05 D1) | Destroy a parked actor on purpose |
| ⚠ caught: **a "Destroyed" counter that also counts parked or already-gone actors** | Stats disagree with what happened | One meaning per stat: `Added` / `Removed`, and only what the spawner really did | Destroy actors behind the spawner's back and read the stats |
| **Building per-item setup inside a timed loop** (spawn parameters, array growth) | Baseline work includes bookkeeping the pattern doesn't need | Build parameters once in `Configure`; `Reserve` before the loop | Compare `WorkMs` with the engine's `ActorSpawning` column |
| **A count cap tuned for one class mix** | B1's frames swing with the classes at the head of the queue | That is the measured weakness (H3), not a bug to fix in the baseline | `SPBaseline/WorkMs` distribution in B1 |

## Policy and unit tests (step S4): mistakes the S4 code review and its fixes caught

| Mistake | How to spot it | How to prevent it | How to debug it |
|---|---|---|---|
| ⚠ caught: **a rule implemented as a side effect of a number** ("cold = predicted as the whole budget, so it runs only first") | A cold job runs second after a first job that measured about 0 ms (0 + budget is not > budget) | State the rule as its own check (`bCold` in the admission test) and test the edge case (05 E3, `ColdJobRunsOnlyFirst`) | A fake first job of 0 ms, then a cold job |
| ⚠ caught: **a raw `UClass*` as a long-lived map key** | After a Blueprint recompile in the editor, a new class predicts warm with an old class's cost | `TObjectKey<UClass>` (06 §20) | Recompile a Blueprint between PIE sessions and print the model's entries |
| ⚠ caught: **believing a test runner's exit code 0 means the intended tests passed** | A new test is missing from a stale build and nobody notices | Parse each test's result line and require `--expect` distinct tests (05 E4) | Run with a filter that matches nothing: the script must fail |
| ⚠ caught: **dereferencing a lookup result in a test without checking it** | One regression crashes the whole test run, with no failing test named | `if (!TestNotNull(...)) return false;` | The run log ends without `Test Completed` lines |
| **Comparing floating-point sums at a boundary exactly** | A test that passes on one compiler and fails on another | A tolerance, or accept both sides of the boundary (04 §17) | Print the sum with 17 digits |
| **Storing a `TFunctionRef`** | A crash or garbage after the call returned | Only as a parameter; `TFunction` or a delegate to keep one (06 §21) | Address sanitiser; check where the callable lived |

## Subsystem and staged spawn (step S5)

| Mistake | How to spot it | How to prevent it | How to debug it |
|---|---|---|---|
| **Serving requests from a tickable that may tick before the requester** | Latency differs by a frame between runs, or between two machines | Do the work on `OnWorldPostActorTick`, after every tickable (06 §22) | Log `GFrameCounter` at the request and at the spawn |
| **Detecting completion in your own tick when the work happens later in the frame** | The completion event is one frame late for one configuration only | Complete where the last piece of work reports (the ready notification, the batch callback) (05 F3) | Compare the completion frame with the frame of the last spawn |
| **Modifying the queue being iterated** (a BeginPlay that spawns more, a callback that cancels) | Crashes or skipped requests, only with re-entrant gameplay | Requests made during a stage go to `Incoming`; `CancelBatch` changes flags only (02 §8) | The test `SpawnFromInsideBeginPlay`; an ensure on the queue size around `RunStage` |
| **Keeping a raw pointer to a half-spawned actor across frames** | A crash on `FinishSpawning` after a level unload or gameplay destroy | `TWeakObjectPtr` and a validity check in every stage (06 §25) | The test `ActorDestroyedBetweenStagesIsDropped` |
| **Callbacks firing during world teardown** | Code runs against a world that is being destroyed | `Deinitialize` ends waiting requests silently and clears the delegate | The test `WorldEndsWithRequestsPending` |
| **Forgetting to remove a global delegate handler** | A crash in a later world after the subsystem is gone | `Remove(Handle)` in `Deinitialize` (06 §23) | Create a second world after destroying the first (the same test) |
| ⚠ caught: **deciding what a cancel means at the moment it is made** | A cancel from inside a request's own callback leaves a half-spawned actor in the level | Set a flag; decide at the request's next stage, from its state then (05 F2) | The test `CancelFromInsideInitialize` |
| ⚠ caught: **a release that races the spawn of the same actor** | Whether a released half-spawned actor runs BeginPlay depends on two requests' priorities | The spawn request itself becomes the release (02 §8) | The test `ReleaseOfAHalfSpawnedActor` |
| ⚠ caught: **an idle system that still publishes** | A baseline run's summary shows another spawner's numbers | Publish nothing until first use; analyse each configuration's own column | Open a baseline capture and look for `SpawnPacer/*` columns |
| ⚠ caught: **"the last record" as the owner of an asynchronous result** | A slow completion is written to the next cycle's record | Keep the id on the record it belongs to and look it up | Shorten the cycle until the despawn overlaps the next wave |
| **Different spawn parameters for different spawners** | One configuration's actors are moved or refused on collision | The director gives SpawnPacer exactly the baselines' parameters (`AlwaysSpawn`) | Compare actor counts and positions between configurations |

## Per-component split (step S6)

| Mistake | How to spot it | How to prevent it | How to debug it |
|---|---|---|---|
| ⚠ caught before any run: **one cold key per component** | The first split actor of a session stalls its queue for as many frames as it has parts | A family prior: an unseen part is predicted from its class's other parts (05 G1) | `ColdJobs` per frame in the first wave |
| ⚠ caught: **a comment that states engine behaviour from memory** | The code is right for the wrong reason, and the next edit breaks it | Read the function the comment cites (`GetUnregisteredParent`, 06 §28) | Grep the engine for the function and read its condition |
| ⚠ caught: **a cheap job as a family's first sample** | The heavy parts that follow are predicted far too low and overrun | Hold back only components with real registration work (scene components) | Compare a family's first sample with its later mean |
| ⚠ caught: **a test that "covers" a mode without reaching its code** | A PerComponent bug passes the spawn test | Use an actor that has something to hold back | Count `Register` jobs in the test |
| ⚠ caught: **assuming budget 0 means one job per frame** | An intermittent test failure | Count the jobs from the frame stats | Run the test many times |
| ⚠ caught: **accepting the `_MAX` entry of a reflected enum** | A bogus mode runs as another and is recorded as itself | Bound the parsed value to the real enumerators (06 §29) | Pass `-SPBossSplit=ESpawnPacerSplitMode_MAX` |
| **Registering a held-back component after BeginPlay** | The actor's BeginPlay doesn't see the part | `Finish` only when nothing is held back (02 §8) | The test `PerComponentBeginPlaySeesEveryComponent` |
| **Reading parts in `PostRegisterAllComponents` with PerComponent** | Cached state from parts that weren't registered yet | Keep such a class Whole or Deferred (02 §13) | Log the registered count there |
