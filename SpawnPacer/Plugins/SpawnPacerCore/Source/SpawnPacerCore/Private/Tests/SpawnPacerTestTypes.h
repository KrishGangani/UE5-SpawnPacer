// SpawnPacerCore - a minimal actor for the subsystem tests (SpawnPacerSubsystemTests.cpp). UHT only reads headers, so a
// reflected test type lives in one. Like the engine's own test classes, it exists in every configuration and is hidden
// from editor pickers.

#pragma once

#include "GameFramework/Actor.h"
#include "Templates/Function.h"
#include "SpawnPacerTestTypes.generated.h"

/**
 * An actor that records what happened to it. A test hands it a BeginPlay hook through the request's Initialize
 * callback, which runs before BeginPlay in both split modes (FSpawnPacerSpawnParams::Initialize).
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

protected:
	virtual void BeginPlay() override;
};
