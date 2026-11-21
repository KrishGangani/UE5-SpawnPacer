// SpawnPacerCore - USpawnPacerSubsystem: brings actors into a world, and takes them out, within a per-frame time budget.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/EngineTypes.h"
#include "SpawnCostModel.h"
#include "SpawnPacerResetVerifier.h"
#include "SpawnPacerScheduler.h"
#include "Subsystems/WorldSubsystem.h"
#include "Templates/SubclassOf.h"
#include "SpawnPacerSubsystem.generated.h"

#define UE_API SPAWNPACERCORE_API

/** Three FIFO levels (02 section 7): a higher level is always served first; inside a level, request order is kept. */
UENUM(BlueprintType)
enum class ESpawnPacerPriority : uint8
{
	Background,  // pre-warming (S7): never delays a gameplay request
	Normal,
	High
};

/** How a class's spawn is cut into jobs (02 section 2). Set per class with SetClassRule; the default is Whole. */
UENUM(BlueprintType)
enum class ESpawnPacerSplitMode : uint8
{
	Whole,       // one Spawn job: SpawnActor, everything in one go
	Deferred,    // a Construct job (SpawnActor deferred: construction + native registration), then a Finish job
	             // (FinishSpawning: construction script, initialisation, BeginPlay), possibly in a later frame
	PerComponent // like Deferred, but the native non-root scene components are held back from the Construct job and
	             // registered one per Register job (parent first), all before the Finish job (01b #3, #4). Limits:
	             // native components only (a Blueprint's construction-script components are made inside Finish,
	             // 01b #5); and AActor::PostRegisterAllComponents runs in the Construct job, before the held-back
	             // components register, so a class that reads its parts there must stay Whole or Deferred
};

/** One spawn request. Moved into the subsystem by RequestSpawn. */
struct FSpawnPacerSpawnParams
{
	TSubclassOf<AActor> Class;
	FTransform Transform;
	ESpawnPacerPriority Priority = ESpawnPacerPriority::Normal;

	/** The batch this request belongs to (CreateBatch), or INDEX_NONE. */
	int32 BatchId = INDEX_NONE;

	/** Undefined = the class's own choice, as SpawnActor does. */
	ESpawnActorCollisionHandlingMethod CollisionHandling = ESpawnActorCollisionHandlingMethod::Undefined;

	/** Optional: after construction and native component registration, before the construction script and BeginPlay
	 *  (the moment deferred spawning exists for). In every split mode; in PerComponent, the held-back components are
	 *  not registered yet when it runs. */
	TFunction<void(AActor&)> Initialize;

	/** Optional: once the actor has finished spawning (and begun play, if the world has). Not called for a request that
	 *  was dropped or cancelled. */
	TFunction<void(AActor&)> OnSpawned;
};

/** Requests that ended, in total since the world began (for tests and the debug display). */
struct FSpawnPacerTotals
{
	int32 Completed = 0;   // spawned, or released
	int32 Dropped = 0;     // the actor became invalid (destroyed behind our back), or the spawn failed
	int32 Cancelled = 0;   // CancelBatch: never started, or half-spawned and destroyed
};

enum class ESpawnPacerRequestKind : uint8
{
	Spawn,    // handed to the caller when spawned (or acquired from the pool)
	Release,  // parked (pooled class with room) or destroyed
	Prewarm   // spawned for the pool: parked as soon as it has spawned (S7)
};

/** What one request is waiting for. Internal; USTRUCT so the class pointer is visible to GC while the request waits. */
USTRUCT()
struct FSpawnPacerRequest
{
	GENERATED_BODY()

	/** Spawn: the class to spawn. Release: the actor's class (the cost model's key). Kept alive while queued. */
	UPROPERTY()
	TSubclassOf<AActor> Class;

	/** Weak: anything may destroy the actor between two of its stages. */
	TWeakObjectPtr<AActor> Actor;

	/** PerComponent: the components held back from registration, parent first; NextHeldBack is the next to register. */
	TArray<TWeakObjectPtr<UActorComponent>> HeldBack;
	int32 NextHeldBack = 0;

	FTransform Transform;
	TFunction<void(AActor&)> Initialize;
	TFunction<void(AActor&)> OnSpawned;
	int32 BatchId = INDEX_NONE;
	ESpawnPacerRequestKind Kind = ESpawnPacerRequestKind::Spawn;
	ESpawnPacerStage NextStage = ESpawnPacerStage::Spawn;

	/** The stage a fresh spawn starts with (Spawn or Construct), to go back to if the pool it was going to use is empty. */
	ESpawnPacerStage FirstStage = ESpawnPacerStage::Spawn;
	ESpawnPacerPriority Priority = ESpawnPacerPriority::Normal;
	ESpawnActorCollisionHandlingMethod CollisionHandling = ESpawnActorCollisionHandlingMethod::Undefined;

	/** PerComponent: hold the native components back in the Construct job. */
	bool bHoldBackComponents = false;

	/** CancelBatch was called. Resolved when the request reaches the head (USpawnPacerSubsystem::FindHead). */
	bool bCancelled = false;
};

/** One priority level's FIFO: requests before Head are done; compacted at the end of a frame. */
USTRUCT()
struct FSpawnPacerQueue
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FSpawnPacerRequest> Requests;

	int32 Head = 0;

	int32 Num() const { return Requests.Num() - Head; }
};

/** One parked actor and the ticks Park switched off, so Unpark can switch exactly those back on (02 section 11). */
USTRUCT()
struct FSpawnPacerParkedActor
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<AActor> Actor;

	/** The actor's key, kept so its ParkedActors entry can be removed even after GC has cleared the pointer. */
	TObjectKey<AActor> Key;

	/** Components whose tick was on when the actor was parked. */
	TArray<TWeakObjectPtr<UActorComponent>> TickingComponents;

	bool bActorTickWasEnabled = false;

	/** GFrameCounter when parked: not acquired in that frame (its latent actions are removed only at the next latent
	 *  update, LatentActionManager.cpp:52-75, and a new Delay with the same id would find the stale one). */
	uint64 ParkedFrame = 0;
};

/** One class's parked actors. In a UPROPERTY map on the subsystem, so GC sees them (02 section 4.1). */
USTRUCT()
struct FSpawnPacerPool
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FSpawnPacerParkedActor> Parked;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FSpawnPacerBatchCompleteDelegate, int32 /*BatchId*/);

/** After each verified acquire (S8): the actor and what the verifier found (empty: clean). */
DECLARE_MULTICAST_DELEGATE_TwoParams(FSpawnPacerResetVerifiedDelegate, AActor& /*Actor*/, const TArray<FSpawnPacerLeak>& /*Leaks*/);

/**
 * The mechanism and public API (02 section 4.1). Requests are cut into indivisible jobs; once per frame the scheduler
 * (S4) runs as many as are predicted to fit the budget, and the cost model learns from each one.
 *
 * When it runs: on FWorldDelegates::OnWorldPostActorTick for its own world, after every tick group and every tickable
 * object of the frame (Private/LevelTick.cpp:1821, 1877-1906). So a request made anywhere during a frame (an actor's
 * tick, a tickable, a timer) is served in that same frame, whatever order the engine ticks tickable objects in; and
 * the actors it spawns take part in the next frame's ticks and physics. Not when the world is paused.
 *
 * Requests made while it runs (a BeginPlay that spawns more) go to an incoming list that is merged at the start of the
 * next frame: the queue being worked on is never resized mid-frame (02 section 8).
 *
 * Game and PIE worlds only.
 */
UCLASS(MinimalAPI)
class USpawnPacerSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	UE_API virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	UE_API virtual void Deinitialize() override;
	//~ End USubsystem

	/**
	 * A new batch id. A batch completes (OnBatchComplete) at the end of the first SpawnPacer frame in which none of its
	 * requests is outstanding, so add its requests in the same frame it is created; an empty batch completes at the end
	 * of the next SpawnPacer frame.
	 */
	UE_API int32 CreateBatch();

	/** Queues a spawn. Returns false (and queues nothing) without a class. */
	UE_API bool RequestSpawn(FSpawnPacerSpawnParams&& Params);

	/**
	 * Queues the removal of an actor: a budgeted Destroy job (from S7, a pooled class is parked instead). An actor that
	 * SpawnPacer has constructed but not finished is never finished: its own request becomes the release (it keeps its
	 * place in its queue; Priority is not used then).
	 */
	UE_API bool RequestRelease(AActor* Actor, ESpawnPacerPriority Priority = ESpawnPacerPriority::Normal, int32 BatchId = INDEX_NONE);

	/**
	 * Cancels the batch's requests that haven't ended. Takes effect at a request's next stage: one that hasn't started
	 * ends without a job; a half-spawned actor (constructed, not finished) gets a Destroy job instead of its Finish job;
	 * a request whose last stage is already running (its BeginPlay cancels the batch) completes. The batch still
	 * completes as usual.
	 */
	UE_API void CancelBatch(int32 BatchId);

	/** How this class's spawns are cut into jobs, from its next request on (the settings of S9 feed this). */
	UE_API void SetClassRule(TSubclassOf<AActor> Class, ESpawnPacerSplitMode Mode);

	/**
	 * Pools this exact class with up to Cap parked actors (0 = not pooled, the default). Releases of a pooled class are
	 * parked while there is room and destroyed beyond it; its spawn requests are served from the pool when it has an
	 * actor (an Acquire job), else spawned. An actor class that implements ISpawnPacerPoolable resets its own state.
	 * An acquired actor is placed at the request's transform as a teleport: the request's collision handling applies to
	 * fresh spawns only.
	 */
	UE_API void SetClassPoolCap(TSubclassOf<AActor> Class, int32 Cap);

	/**
	 * Queues up to Count spawns of a pooled class that are parked as soon as they have spawned, never handed out
	 * (Background priority by default, so they never delay a gameplay request). Clamped to the room left in the pool
	 * after the parked actors and the pre-warms already queued. Returns how many were queued (0 or more).
	 */
	UE_API int32 Prewarm(TSubclassOf<AActor> Class, int32 Count, ESpawnPacerPriority Priority = ESpawnPacerPriority::Background, int32 BatchId = INDEX_NONE);

	/** Parked actors of this exact class. */
	UE_API int32 GetNumParked(TSubclassOf<AActor> Class) const;

	/**
	 * S8, the reset verifier (02 section 11): when on, every actor of a pooled class is snapshot once, right after it
	 * first finished spawning, and compared with that snapshot after every acquire (before Initialize). Off by default:
	 * it exports every property as text, fine for a soak, too slow for timing runs (sp.Pool.VerifyReset in S9).
	 */
	void SetVerifyReset(bool bVerify) { bVerifyReset = bVerify; }
	bool IsVerifyingReset() const { return bVerifyReset; }

	/** Property names of this class the verifier ignores: its per-spawn parameters (02 section 11). */
	UE_API void SetClassVerifierIgnore(TSubclassOf<AActor> Class, const TSet<FName>& PropertyNames);

	/** Broadcast after every verified acquire. */
	FSpawnPacerResetVerifiedDelegate OnResetVerified;

	/** Leaks found since the world began, by kind (ESpawnPacerLeakKind). */
	int32 GetResetLeaks(ESpawnPacerLeakKind Kind) const { return ResetLeaks[static_cast<int32>(Kind)]; }

	/** The per-frame budget in ms (default 2.0, 02 section 7). 0 means one job per frame. */
	void SetBudgetMs(double InBudgetMs) { BudgetMs = FMath::Max(InBudgetMs, 0.0); }
	double GetBudgetMs() const { return BudgetMs; }

	/** Requests not ended yet, queued or incoming. */
	UE_API int32 GetNumPending() const;

	const FSpawnCostModel& GetCostModel() const { return CostModel; }
	FSpawnCostModel& GetCostModel() { return CostModel; }
	const FSpawnPacerFrameStats& GetLastFrameStats() const { return LastFrameStats; }
	const FSpawnPacerTotals& GetTotals() const { return Totals; }

	/** Broadcast once per completed batch, at the end of the SpawnPacer frame it completed in. */
	FSpawnPacerBatchCompleteDelegate OnBatchComplete;

protected:
	//~ Begin UWorldSubsystem
	/** Game and PIE worlds only (the default would also create it in editor worlds, WorldSubsystem.cpp:29-32). */
	UE_API virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	//~ End UWorldSubsystem

private:
	enum class EOutcome : uint8 { Completed, Dropped, Cancelled };

	void HandlePostActorTick(UWorld* InWorld, ELevelTick TickType, float DeltaSeconds);

	/** One SpawnPacer frame: merge incoming, run the scheduler, compact, complete batches, publish telemetry. */
	void RunFrame();

	/** The request at the head of the highest non-empty level. Cancelled requests there are resolved on the way: ended
	 *  without a job, or (half-spawned) turned into a Destroy job. Null if none. */
	FSpawnPacerRequest* FindHead();

	/** Does the head request's next stage. Returns true when the request has ended. */
	bool RunStage(FSpawnPacerRequest& Request);

	/** SpawnActor with the request's parameters; for a PerComponent request, holds its components back (HeldBack). */
	AActor* Spawn(FSpawnPacerRequest& Request, bool bDeferConstruction) const;

	/** A spawn constructed but not finished: its actor exists and is waiting for Register or Finish jobs. */
	static bool IsHalfSpawned(const FSpawnPacerRequest& Request);

	/** Moves NextHeldBack past components that are gone or already registered; with none left, the next job is Finish. */
	static void SkipToNextHeldBack(FSpawnPacerRequest& Request);

	/** A cancelled request at the head: true if it ended without a job, false if it still has a job to do. */
	bool ResolveCancel(FSpawnPacerRequest& Request);

	/** The pool decisions for a head request (S7): Acquire instead of a fresh spawn, Destroy instead of a park into a
	 *  full pool. Returns true if the request ended without a job (a second release of an actor already parked). */
	bool ResolvePool(FSpawnPacerRequest& Request);

	/** Drops the pool's entries whose actor is gone (destroyed behind the pool's back), with their ParkedActors keys. */
	void SweepPool(UClass* Class);

	/** The index of the most recently parked actor of the class that may be acquired now (valid, not parked this frame),
	 *  or INDEX_NONE. */
	int32 FindAcquirable(UClass* Class);

	bool PoolHasRoom(UClass* Class);

	/** The engine half of the reset contract, steps 1-4 on release and on acquire (02 section 11). */
	void Park(AActor& Actor);
	void Unpark(FSpawnPacerParkedActor& Parked, const FTransform& Transform);

	/** Ends a request: counts its outcome and its batch. */
	void End(const FSpawnPacerRequest& Request, EOutcome Outcome);

	/** Adds one outstanding request to a batch (an unknown id is treated as no batch). */
	void AddToBatch(int32 BatchId);

	/** Calls Visitor on every request that hasn't ended (queued and incoming). */
	void ForEachOpenRequest(TFunctionRef<void(FSpawnPacerRequest&)> Visitor);

	UPROPERTY(Transient)
	TArray<FSpawnPacerQueue> Queues;   // indexed by ESpawnPacerPriority

	/** Requests made since this frame's merge (02 section 8). */
	UPROPERTY(Transient)
	TArray<FSpawnPacerRequest> Incoming;

	/** Outstanding requests per open batch. */
	TMap<int32, int32> OpenBatches;
	int32 NextBatchId = 0;

	TMap<TObjectKey<UClass>, ESpawnPacerSplitMode> ClassRules;

	/** Parked actors per class; emptied in Deinitialize (the world destroys the actors). */
	UPROPERTY(Transient)
	TMap<TObjectPtr<UClass>, FSpawnPacerPool> Pools;

	/** Pool caps per class (absent = not pooled). */
	TMap<TObjectKey<UClass>, int32> PoolCaps;

	/** Every parked actor, so a second release of one is refused at once. */
	TSet<TObjectKey<AActor>> ParkedActors;

	/** S8: each pooled actor's fresh snapshot, and the classes' ignored property names. */
	TMap<TObjectKey<AActor>, FSpawnPacerSnapshot> FreshSnapshots;
	TMap<TObjectKey<UClass>, TSet<FName>> VerifierIgnore;
	int32 ResetLeaks[static_cast<int32>(ESpawnPacerLeakKind::Num)] = {};
	int32 FrameResetLeaks = 0;
	bool bVerifyReset = false;

	FSpawnCostModel CostModel;
	double BudgetMs = 2.0;

	/** The queue whose head FindHead returned last: RunHead works on it. */
	FSpawnPacerQueue* HeadQueue = nullptr;

	/** The request whose stage is running, during RunStage only (RequestRelease must not take it over). */
	const FSpawnPacerRequest* Running = nullptr;

	/** Set by the first request or batch: until then the subsystem does nothing per frame and publishes nothing. */
	bool bActive = false;

	/** Per-frame counters for the telemetry, reset after each frame is published. */
	struct FFrameCounters
	{
		double StageMs[NumSpawnPacerStages] = {};   // indexed by ESpawnPacerStage
		int32 Completed = 0;
		int32 Dropped = 0;
		int32 Cancelled = 0;
	};
	FFrameCounters Frame;

	FSpawnPacerFrameStats LastFrameStats;
	FSpawnPacerTotals Totals;
	FDelegateHandle PostActorTickHandle;
};

#undef UE_API
