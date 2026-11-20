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

/**
 * Spawns and removes a wave the way existing code does. Step S1 implements B0 only: plain SpawnActor for the whole
 * wave in the request frame, Destroy for the whole wave in the despawn frame. B1-B3 are added in S3.
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
	/** Queues a whole wave. Its actors are spawned on the next RunFrame(). */
	void RequestWave(TConstArrayView<FSPWaveEntry> Entries);

	/** Queues the removal of every live actor of the wave. */
	void RequestDespawn();

	/** Does this frame's work and publishes the frame's CSV stats. Called once per frame by the director. */
	void RunFrame(UWorld& World);

	/** Actors of the current wave that are still alive. */
	int32 GetNumLive() const { return Live.Num(); }

	/** True when no spawn or despawn work is waiting. */
	bool IsIdle() const { return Pending.IsEmpty() && !bDespawnRequested; }

private:
	/** Spawns every pending entry (B0). Returns how many actors it created. */
	int32 SpawnAllPending(UWorld& World);

	/** Destroys every live actor (B0). Returns how many it destroyed. */
	int32 DestroyAllLive();

	TArray<FSPWaveEntry> Pending;
	bool bDespawnRequested = false;

	/** The wave's live actors. UPROPERTY so GC sees the references (they are also in the level's actor list). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<AActor>> Live;
};
