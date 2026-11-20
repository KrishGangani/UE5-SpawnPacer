// P02 SpawnPacer (demo) - the baseline spawners B0-B3 that SpawnPacer is compared against (01_Problem_Analysis.md 6.2).

#pragma once

#include "UObject/Object.h"
#include "SPBaselineSpawner.generated.h"

class AActor;
class ASPWaveActor;
class UWorld;

/** One actor of a wave: which class, and where. The director builds the list once per run from a fixed seed. */
USTRUCT()
struct FSPWaveEntry
{
	GENERATED_BODY()

	UPROPERTY()
	TSubclassOf<ASPWaveActor> Class;

	UPROPERTY()
	FTransform Transform;
};

/** B3: the parked actors of one class. A struct, because a UPROPERTY TMap can't hold an array directly. */
USTRUCT()
struct FSPPooledActors
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<TObjectPtr<ASPWaveActor>> Actors;
};

/** The four baselines (01 6.2). Each is the simple, competent version of a pattern real code uses. */
UENUM()
enum class ESPBaseline : uint8
{
	B0,   // plain SpawnActor for the whole wave in the request frame; Destroy for the whole wave in the despawn frame
	B1,   // the deferred-spawn pattern: a queue, SpawnActorDeferred + FinishSpawning, at most CountCap actors per frame
	B2,   // a time cap checked between items (the Mass / SpawnBudget model): spawn or destroy while elapsed < BudgetMs
	B3    // a simple pool, filled during warm-up; the whole wave acquired or released in one frame
};

/**
 * Spawns and removes a wave the way existing code does.
 *
 * A plain UObject owned by the director, not a subsystem of its own: the director calls RunFrame() itself, right after
 * it advanced the script, so a request and the work it triggers are always in the same frame (two tickable subsystems
 * have no guaranteed order: new tickables wait in a TMap, Public/Tickable.h:62, Private/Tickable.cpp:66-92).
 *
 * Every frame it publishes the game-thread time it spent (CSV SPBaseline/WorkMs), timed with the same clock as
 * SpawnPacer, so the measurement cost is equal in every configuration (02 section 9).
 */
UCLASS()
class USPBaselineSpawner : public UObject
{
	GENERATED_BODY()

public:
	/** Chooses the baseline and its one parameter (CountCap for B1, BudgetMs for B2). Call once, before any request. */
	void Configure(ESPBaseline InMode, int32 InCountCap, double InBudgetMs);

	/** B3 only: fills the pool with one parked actor per wave entry, all at once (the director calls it on the first
	 *  warm-up frame, outside the capture). */
	void FillPool(UWorld& World, TConstArrayView<FSPWaveEntry> Entries);

	/** Queues a whole wave. Its actors are created (or taken from the pool) on the following RunFrame() calls. */
	void RequestWave(TConstArrayView<FSPWaveEntry> Entries);

	/** Queues the removal (or parking) of every live actor of the wave, and cancels entries not yet brought in: a
	 *  capped baseline that hasn't finished by the despawn time leaves its wave incomplete, which the run reports. */
	void RequestDespawn();

	/** Does this frame's work and publishes the frame's CSV stats. Called once per frame by the director. */
	void RunFrame(UWorld& World);

	/** Actors of the current wave that are still live. */
	int32 GetNumLive() const { return Live.Num(); }

	/** True when no spawn or despawn work is waiting. */
	bool IsIdle() const { return NextPending >= Pending.Num() && !bDespawnRequested; }

private:
	/** Brings the next pending entry into the world: a spawn (B0-B2) or an acquire from the pool (B3, which spawns on
	 *  a pool miss, as a competent pool does). */
	void BringIn(UWorld& World, const FSPWaveEntry& Entry);

	/** B3: the next valid parked actor of a class, or null if the pool has none. */
	ASPWaveActor* TakeFromPool(UClass* Class);

	/** Takes the last live actor out of the world: Destroy (B0-B2) or park it in the pool (B3). Returns false if the
	 *  actor was already gone (destroyed by something else), so it isn't counted. */
	bool TakeOutLast();

	/** B3: hides the actor, removes its collision and tick, stops its timer, parks it below the arena. */
	static void Park(ASPWaveActor& Actor);

	/** True if the frame may start one more item: B0 and B3 always, B1 below its count, B2 below its time. */
	bool MayStartAnother(int32 ItemsThisFrame, uint64 FrameStartCycles) const;

	ESPBaseline Mode = ESPBaseline::B0;
	int32 CountCap = 25;
	double BudgetMs = 2.0;

	/** The same spawn parameters for every actor, built once in Configure (not inside the timed loop). */
	FActorSpawnParameters SpawnParameters;

	/** B1 this frame: ms inside SpawnActor (construction + native registration) and inside FinishSpawning (construction
	 *  script, initialisation, BeginPlay). The split behind hypothesis H1, measured before SpawnPacer exists. */
	double ConstructMsThisFrame = 0.0;
	double FinishMsThisFrame = 0.0;

	TArray<FSPWaveEntry> Pending;
	int32 NextPending = 0;
	bool bDespawnRequested = false;

	/** The wave's live actors. UPROPERTY so GC sees the references (they are also in the level's actor list). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<AActor>> Live;

	/** B3: parked actors per class, hidden, without collision or actor tick, far below the arena. */
	UPROPERTY(Transient)
	TMap<TObjectPtr<UClass>, FSPPooledActors> Pool;
};
