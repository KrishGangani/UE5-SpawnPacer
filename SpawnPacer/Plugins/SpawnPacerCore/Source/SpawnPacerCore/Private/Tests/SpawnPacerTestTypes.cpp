// SpawnPacerCore - ASpawnPacerTestActor, ASpawnPacerTestPartsActor.

#include "Tests/SpawnPacerTestTypes.h"

#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(SpawnPacerTestTypes)

ASpawnPacerTestActor::ASpawnPacerTestActor()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root")); // so the spawn transform has something to place
}

void ASpawnPacerTestActor::BeginPlay()
{
	// Counted before Super: what the actor's BeginPlay (and its components') sees.
	TInlineComponentArray<UActorComponent*> Components(this);
	RegisteredAtBeginPlay = 0;
	for (const UActorComponent* Component : Components)
	{
		RegisteredAtBeginPlay += Component->IsRegistered() ? 1 : 0;
	}

	Super::BeginPlay();
	++BeginPlayCalls;
	bInitializedAtBeginPlay = bInitialized;
	if (OnBeginPlay)
	{
		OnBeginPlay(*this);
	}
}

const FVector ASpawnPacerTestPartsActor::PartAOffset(100.0, 0.0, 0.0);
const FVector ASpawnPacerTestPartsActor::PartBOffset(0.0, 50.0, 0.0);

ASpawnPacerTestPartsActor::ASpawnPacerTestPartsActor()
{
	PartA = CreateDefaultSubobject<USceneComponent>(TEXT("PartA"));
	PartA->SetupAttachment(RootComponent);
	PartA->SetRelativeLocation(PartAOffset);

	PartB = CreateDefaultSubobject<USceneComponent>(TEXT("PartB"));
	PartB->SetupAttachment(PartA);
	PartB->SetRelativeLocation(PartBOffset);

	Extra = CreateDefaultSubobject<USpawnPacerTestComponent>(TEXT("Extra"));

	Dormant = CreateDefaultSubobject<USceneComponent>(TEXT("Dormant"));
	Dormant->SetupAttachment(RootComponent);
	Dormant->bAutoRegister = false;
}

USpawnPacerTestTickComponent::USpawnPacerTestTickComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void USpawnPacerTestTickComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	++Ticks;
}

ASpawnPacerTestPoolActor::ASpawnPacerTestPoolActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	Ticker = CreateDefaultSubobject<USpawnPacerTestTickComponent>(TEXT("Ticker"));
}

void ASpawnPacerTestPoolActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	++Ticks;
}

void ASpawnPacerTestPoolActor::ArmTimers(float Seconds)
{
	FTimerHandle ActorTimer;
	FTimerHandle ComponentTimer;
	GetWorldTimerManager().SetTimer(ActorTimer, this, &ASpawnPacerTestPoolActor::OnTimer, Seconds, false);
	GetWorldTimerManager().SetTimer(ComponentTimer, FTimerDelegate::CreateUObject(Ticker.Get(), &USpawnPacerTestTickComponent::OnTimer), Seconds, false);
}
