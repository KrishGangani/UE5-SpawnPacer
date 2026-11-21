// SpawnPacerCore - a minimal actor for the subsystem tests (SpawnPacerSubsystemTests.cpp). UHT only reads headers, so a
// reflected test type lives in one. Like the engine's own test classes, it exists in every configuration and is hidden
// from editor pickers.

#pragma once

#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
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
