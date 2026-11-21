// SpawnPacerCore - ASpawnPacerTestActor.

#include "Tests/SpawnPacerTestTypes.h"

#include "Components/SceneComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(SpawnPacerTestTypes)

ASpawnPacerTestActor::ASpawnPacerTestActor()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root")); // so the spawn transform has something to place
}

void ASpawnPacerTestActor::BeginPlay()
{
	Super::BeginPlay();
	++BeginPlayCalls;
	bInitializedAtBeginPlay = bInitialized;
	if (OnBeginPlay)
	{
		OnBeginPlay(*this);
	}
}
