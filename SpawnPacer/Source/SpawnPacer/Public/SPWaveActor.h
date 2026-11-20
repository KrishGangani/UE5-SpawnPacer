// P02 SpawnPacer (demo) - the wave actors: one abstract base and four classes that differ in what they cost to spawn.

#pragma once

#include "GameFramework/Actor.h"
#include "SPWaveActor.generated.h"

class UStaticMesh;
class UStaticMeshComponent;
class USPWaveBrainComponent;

/**
 * Base of every actor in a benchmark wave. It has no logic of its own: a root mesh, PartCount - 1 child meshes, and a
 * USPWaveBrainComponent holding the gameplay state. The subclasses differ only in what their constructors build, so
 * each has its own class default object, its own component set and its own spawn cost (02_System_Architecture.md 4.2).
 */
UCLASS(Abstract)
class ASPWaveActor : public AActor
{
	GENERATED_BODY()

public:
	ASPWaveActor();

	//~ Begin AActor
	/** Runs the engine's BeginPlay (components first, then Blueprint), then WaveBeginPlay, then tells the director this
	 *  actor is ready. Subclasses override WaveBeginPlay, not this, so "ready" really comes last. */
	virtual void BeginPlay() override final;
	//~ End AActor

	USPWaveBrainComponent* GetBrain() const { return Brain; }

	/** Tells the director this actor is ready. Called at the end of BeginPlay, and by a pool when it reuses the actor
	 *  (a reused actor doesn't run BeginPlay again). Does nothing in worlds without the director. */
	void NotifyReady() const;

	/** A pool is about to reuse this actor at its new place: restart what a new life starts. */
	virtual void PrepareForReuse();

	/** A pool is parking this actor: stop what would otherwise keep running while it waits. */
	void PrepareForParking();

protected:
	/** A subclass's own BeginPlay work. Called by BeginPlay after Super::BeginPlay and before the ready notification. */
	virtual void WaveBeginPlay() {}

	/**
	 * Builds the root and PartCount - 1 child meshes. Call only from a subclass constructor. The meshes are passed in,
	 * because each subclass finds its own with a static ConstructorHelpers::FObjectFinder (one lookup per class).
	 * RootProfile is the root's collision profile; children collide only if bPartsCollide.
	 */
	void BuildBody(int32 PartCount, UStaticMesh* RootMesh, UStaticMesh* PartMesh, FName RootProfile, bool bPartsCollide);

	UPROPERTY(VisibleAnywhere, Category = "SpawnPacer Demo")
	TObjectPtr<USPWaveBrainComponent> Brain;
};

/** Light: one sphere with projectile collision, moving on a small circle every tick (a projectile or pickup). */
UCLASS()
class ASPDrone : public ASPWaveActor
{
	GENERATED_BODY()

public:
	ASPDrone();

	//~ Begin AActor
	virtual void Tick(float DeltaSeconds) override;
	//~ End AActor

	virtual void PrepareForReuse() override;

protected:
	virtual void WaveBeginPlay() override;

private:
	/** Where BeginPlay (or a reuse) found the drone; the circle is drawn around it. */
	FVector Origin = FVector::ZeroVector;
};

/** Medium: three colliding cubes, no tick (a prop). */
UCLASS()
class ASPCrate : public ASPWaveActor
{
	GENERATED_BODY()

public:
	ASPCrate();
};

/** Heavy: a colliding cylinder root plus five visual parts, BeginPlay set-up work and a think timer (an enemy). */
UCLASS()
class ASPGrunt : public ASPWaveActor
{
	GENERATED_BODY()

public:
	ASPGrunt();
};

/** One actor whose single spawn is larger than the budget: a grunt with 24 mesh components. */
UCLASS()
class ASPBoss : public ASPWaveActor
{
	GENERATED_BODY()

public:
	ASPBoss();
};
