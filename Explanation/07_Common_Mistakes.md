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
