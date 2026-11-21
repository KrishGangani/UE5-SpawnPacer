// SpawnPacerCore - a minimal actor for the subsystem tests (SpawnPacerSubsystemTests.cpp). UHT only reads headers, so a
// reflected test type lives in one. Like the engine's own test classes, it exists in every configuration and is hidden
// from editor pickers.

#pragma once

#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "SpawnPacerPoolable.h"
#include "Templates/Function.h"
#include "SpawnPacerTestTypes.generated.h"

/**
 * An actor that records what happened to it. A test hands it a BeginPlay hook through the request's Initialize
 * callback, which runs before BeginPlay in every split mode (FSpawnPacerSpawnParams::Initialize).
 */
UCLASS(NotBlueprintable, NotPlaceable, HideDropdown)
class ASpawnPacerTestActor : public AActor
{
	GENERATED_BODY()

public:
	ASpawnPacerTestActor();

	/** Runs at the end of BeginPlay if set. */
	TFunction<void(ASpawnPacerTestActor&)> OnBeginPlay;

	/** Set by a test's Initialize callback. */
	bool bInitialized = false;

	/** Whether Initialize had run when BeginPlay started. */
	bool bInitializedAtBeginPlay = false;

	int32 BeginPlayCalls = 0;

	/** Components registered when BeginPlay started (S6: a split spawn must show BeginPlay the same as a whole one). */
	int32 RegisteredAtBeginPlay = 0;

protected:
	virtual void BeginPlay() override;
};

/** A non-scene component that does nothing (UActorComponent itself is abstract). */
UCLASS(NotBlueprintable, HideDropdown)
class USpawnPacerTestComponent : public UActorComponent
{
	GENERATED_BODY()
};

/**
 * S6: an actor with a small component tree for the per-component split: two scene parts (PartB attached to PartA,
 * PartA to the root), a non-scene component, and a dormant part that doesn't register itself (bAutoRegister = false)
 * and must stay that way.
 */
UCLASS(NotBlueprintable, NotPlaceable, HideDropdown)
class ASpawnPacerTestPartsActor : public ASpawnPacerTestActor
{
	GENERATED_BODY()

public:
	ASpawnPacerTestPartsActor();

	UPROPERTY()
	TObjectPtr<USceneComponent> PartA;

	UPROPERTY()
	TObjectPtr<USceneComponent> PartB;

	UPROPERTY()
	TObjectPtr<USpawnPacerTestComponent> Extra;

	UPROPERTY()
	TObjectPtr<USceneComponent> Dormant;

	/** PartA's offset from the root, and PartB's from PartA. */
	static const FVector PartAOffset;
	static const FVector PartBOffset;
};

/** S7: a component that counts its ticks and its timer's firings. */
UCLASS(NotBlueprintable, HideDropdown)
class USpawnPacerTestTickComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USpawnPacerTestTickComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	void OnTimer() { ++TimerFired; }

	int32 Ticks = 0;
	int32 TimerFired = 0;
};

/** S7: a pooled actor that ticks, has a ticking component, counts its pool callbacks, and can arm timers during use. */
UCLASS(NotBlueprintable, NotPlaceable, HideDropdown)
class ASpawnPacerTestPoolActor : public ASpawnPacerTestActor, public ISpawnPacerPoolable
{
	GENERATED_BODY()

public:
	ASpawnPacerTestPoolActor();

	virtual void Tick(float DeltaSeconds) override;

	//~ Begin ISpawnPacerPoolable
	virtual void OnPoolReleased_Implementation() override
	{
		++Released;
		Uses = bResetUses ? 0 : Uses;
	}
	virtual void OnPoolAcquired_Implementation() override { ++Acquired; }
	//~ End ISpawnPacerPoolable

	/** One timer on the actor and one on its component, both firing once after Seconds. */
	void ArmTimers(float Seconds);

	void OnTimer() { ++TimerFired; }

	UPROPERTY()
	TObjectPtr<USpawnPacerTestTickComponent> Ticker;

	int32 Ticks = 0;
	int32 Released = 0;
	int32 Acquired = 0;
	int32 TimerFired = 0;

	/** S8: a reflected per-life value the verifier can see; the test decides whether OnPoolReleased resets it. */
	UPROPERTY()
	int32 Uses = 0;

	bool bResetUses = true;
};
