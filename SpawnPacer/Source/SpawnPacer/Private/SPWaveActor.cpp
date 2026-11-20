// P02 SpawnPacer (demo) - ASPWaveActor and its four classes.

#include "SPWaveActor.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "SPBenchmarkSubsystem.h"
#include "SPWaveBrainComponent.h"
#include "UObject/ConstructorHelpers.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(SPWaveActor)

namespace SPWave
{
	// Engine basic shapes; cooked through DirectoriesToAlwaysCook in DefaultGame.ini (they are referenced only from C++).
	const TCHAR* const SphereMesh = TEXT("/Engine/BasicShapes/Sphere.Sphere");
	const TCHAR* const CubeMesh = TEXT("/Engine/BasicShapes/Cube.Cube");
	const TCHAR* const CylinderMesh = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
	const TCHAR* const ConeMesh = TEXT("/Engine/BasicShapes/Cone.Cone");

	/** Child parts sit on a ring around the root (cm, before the root's scale), stacked in three layers. */
	constexpr double PartRingRadius = 60.0;
	constexpr double PartLayerHeight = 30.0;
	constexpr double PartScale = 0.25;

	/** The most parts any class has (the boss's 23 children), and their subobject names, built once on first use, so a
	 *  spawned actor's constructor doesn't format strings (that would be cost a real class doesn't have). */
	constexpr int32 MaxParts = 23;
	const TArray<FName>& PartNames()
	{
		static const TArray<FName> Names = []
		{
			TArray<FName> Built;
			for (int32 Index = 0; Index < MaxParts; ++Index)
			{
				Built.Add(FName(*FString::Printf(TEXT("Part%02d"), Index)));
			}
			return Built;
		}();
		return Names;
	}

	/** A drone circles its spawn point: 1 m radius, one lap every 2 s. */
	constexpr double DroneCircleRadius = 100.0;
	constexpr double DroneLapsPerSecond = 0.5;
}

ASPWaveActor::ASPWaveActor()
{
	// Only classes that move tick (the drone switches this on).
	PrimaryActorTick.bCanEverTick = false;

	Brain = CreateDefaultSubobject<USPWaveBrainComponent>(TEXT("Brain"));
}

void ASPWaveActor::BeginPlay()
{
	// The engine's BeginPlay: actor ticks, every component's BeginPlay (the brain's set-up work), then Blueprint.
	Super::BeginPlay();
	WaveBeginPlay();

	// Last, so "ready" means this actor's BeginPlay has completely finished.
	NotifyReady();
}

void ASPWaveActor::NotifyReady() const
{
	// Every configuration completes a wave through this one call (02 section 9). Null in worlds without the director.
	if (USPBenchmarkSubsystem* Bench = GetWorld()->GetSubsystem<USPBenchmarkSubsystem>())
	{
		Bench->NotifyActorReady();
	}
}

void ASPWaveActor::PrepareForReuse()
{
	Brain->StartThinking();
}

void ASPWaveActor::PrepareForParking()
{
	Brain->StopThinking();
}

void ASPWaveActor::BuildBody(int32 PartCount, UStaticMesh* RootMesh, UStaticMesh* PartMesh, FName RootProfile, bool bPartsCollide)
{
	UStaticMeshComponent* Root = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Root"));
	Root->SetStaticMesh(RootMesh);
	Root->SetCollisionProfileName(RootProfile);
	Root->SetCastShadow(false);
	SetRootComponent(Root);

	const int32 NumParts = PartCount - 1;
	check(NumParts <= SPWave::MaxParts);
	for (int32 Index = 0; Index < NumParts; ++Index)
	{
		UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(SPWave::PartNames()[Index]);
		Part->SetupAttachment(Root);
		Part->SetStaticMesh(PartMesh);
		const double Angle = 2.0 * UE_DOUBLE_PI * Index / NumParts;
		Part->SetRelativeLocation(FVector(SPWave::PartRingRadius * FMath::Cos(Angle), SPWave::PartRingRadius * FMath::Sin(Angle),
			SPWave::PartLayerHeight * (Index % 3)));
		Part->SetRelativeScale3D(FVector(SPWave::PartScale));
		Part->SetCollisionProfileName(bPartsCollide ? UCollisionProfile::BlockAllDynamic_ProfileName : UCollisionProfile::NoCollision_ProfileName);
		Part->SetCastShadow(false);
	}
}

ASPDrone::ASPDrone()
{
	PrimaryActorTick.bCanEverTick = true;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(SPWave::SphereMesh);
	BuildBody(1, Sphere.Object, nullptr, UCollisionProfile::DefaultProjectile_ProfileName, false);
	GetRootComponent()->SetRelativeScale3D(FVector(0.3));
}

void ASPDrone::WaveBeginPlay()
{
	Origin = GetActorLocation();
}

void ASPDrone::PrepareForReuse()
{
	Super::PrepareForReuse();
	Origin = GetActorLocation(); // the pool has moved it; without this it would fly back to where it was first spawned
}

void ASPDrone::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Position from world time, not accumulated deltas: every configuration draws the same circle on the same frame.
	const double Angle = 2.0 * UE_DOUBLE_PI * SPWave::DroneLapsPerSecond * GetWorld()->GetTimeSeconds();
	SetActorLocation(Origin + FVector(SPWave::DroneCircleRadius * FMath::Cos(Angle), SPWave::DroneCircleRadius * FMath::Sin(Angle), 0.0));
}

ASPCrate::ASPCrate()
{
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(SPWave::CubeMesh);
	BuildBody(3, Cube.Object, Cube.Object, UCollisionProfile::BlockAllDynamic_ProfileName, true);
	GetRootComponent()->SetRelativeScale3D(FVector(0.5));
}

ASPGrunt::ASPGrunt()
{
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(SPWave::CylinderMesh);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(SPWave::CubeMesh);
	BuildBody(6, Cylinder.Object, Cube.Object, UCollisionProfile::Pawn_ProfileName, false);
	GetRootComponent()->SetRelativeScale3D(FVector(0.5, 0.5, 1.0));

	Brain->BeginPlayWorkScale = 1.0f;
	Brain->bThinks = true;
}

ASPBoss::ASPBoss()
{
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(SPWave::CylinderMesh);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cone(SPWave::ConeMesh);
	BuildBody(24, Cylinder.Object, Cone.Object, UCollisionProfile::Pawn_ProfileName, false);
	GetRootComponent()->SetRelativeScale3D(FVector(1.0, 1.0, 2.0));

	Brain->BeginPlayWorkScale = 1.0f;
	Brain->bThinks = true;
}
