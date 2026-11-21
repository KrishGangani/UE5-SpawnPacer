// SpawnPacerCore - ISpawnPacerPoolable: the game's half of the pool's reset contract (02 section 11).

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "SpawnPacerPoolable.generated.h"

UINTERFACE(MinimalAPI, Blueprintable)
class USpawnPacerPoolable : public UInterface
{
	GENERATED_BODY()
};

/**
 * Implemented by an actor class that SpawnPacer pools (USpawnPacerSubsystem::SetClassPoolCap). SpawnPacer resets what
 * the engine owns: timers and latent actions (cleared), actor and component ticks, collision, visibility, transform.
 * The actor resets what it means: health, ammo, AI state. A Blueprint actor can implement it too.
 *
 * For a pooled class, BeginPlay is once-per-object setup; per-use setup belongs in OnPoolAcquired (02 section 8).
 * The same shape as the Mass and SpawnBudget pool interfaces, on purpose: users already know it (02 section 4.1).
 * Exported, because actors in other modules (the demo) inherit it.
 */
class SPAWNPACERCORE_API ISpawnPacerPoolable
{
	GENERATED_BODY()

public:
	/** The actor is being parked: put gameplay state back to what a fresh actor has. Called first, before SpawnPacer's
	 *  own reset, so the actor can still read its components. */
	UFUNCTION(BlueprintNativeEvent, Category = "SpawnPacer")
	void OnPoolReleased();

	/** The actor is being reused, already at its new transform and visible: start what a new life starts. */
	UFUNCTION(BlueprintNativeEvent, Category = "SpawnPacer")
	void OnPoolAcquired();
};
