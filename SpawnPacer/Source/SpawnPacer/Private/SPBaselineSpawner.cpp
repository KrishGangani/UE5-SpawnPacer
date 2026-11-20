// P02 SpawnPacer (demo) - USPBaselineSpawner: baselines B0-B3.

#include "SPBaselineSpawner.h"

#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "SPWaveActor.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(SPBaselineSpawner)

// Per-frame cost of the baseline spawners. SpawnPacer publishes the same numbers in its own category (S5).
CSV_DEFINE_CATEGORY(SPBaseline, true);

namespace SPBaselines
{
	/** B3 parks actors far below the arena, out of every view and every trace that matters. */
	const FVector ParkLocation(0.0, 0.0, -100000.0);
}

void USPBaselineSpawner::Configure(ESPBaseline InMode, int32 InCountCap, double InBudgetMs)
{
	Mode = InMode;
	CountCap = FMath::Max(1, InCountCap);
	BudgetMs = FMath::Max(0.0, InBudgetMs);

	// The wave's positions are fixed by the seed; a collision test must never move or reject an actor.
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	// B1 is the deferred pattern: construction and native registration in SpawnActor, the rest in FinishSpawning.
	SpawnParameters.bDeferConstruction = (Mode == ESPBaseline::B1);
}

void USPBaselineSpawner::Park(ASPWaveActor& Actor)
{
	// A simple pool parks with the three engine switches PoolManager uses (01b 5): hidden, no collision, no actor tick,
	// plus the one callback its user would write (stop the think timer). It doesn't switch off component ticks or reset
	// gameplay state: that unchecked reset is what SpawnPacer's pool (S7, S8) addresses.
	Actor.PrepareForParking();
	Actor.SetActorHiddenInGame(true);
	Actor.SetActorEnableCollision(false);
	Actor.SetActorTickEnabled(false);
	Actor.SetActorLocation(SPBaselines::ParkLocation);
}

void USPBaselineSpawner::FillPool(UWorld& World, TConstArrayView<FSPWaveEntry> Entries)
{
	for (const FSPWaveEntry& Entry : Entries)
	{
		if (ASPWaveActor* Actor = World.SpawnActor<ASPWaveActor>(Entry.Class, FTransform(SPBaselines::ParkLocation), SpawnParameters))
		{
			Park(*Actor);
			Pool.FindOrAdd(Entry.Class).Actors.Add(Actor);
		}
	}
}

void USPBaselineSpawner::RequestWave(TConstArrayView<FSPWaveEntry> Entries)
{
	Pending.Append(Entries.GetData(), Entries.Num());
}

void USPBaselineSpawner::RequestDespawn()
{
	bDespawnRequested = true;
	Pending.Reset();
	NextPending = 0;
}

bool USPBaselineSpawner::MayStartAnother(int32 ItemsThisFrame, uint64 FrameStartCycles) const
{
	switch (Mode)
	{
	case ESPBaseline::B1:
		return ItemsThisFrame < CountCap;
	case ESPBaseline::B2:
		// Checked between items: an item that starts just before the limit runs to completion (the overrun under test, H4).
		return FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - FrameStartCycles) < BudgetMs;
	default:
		return true; // B0 and B3: the whole wave in one frame
	}
}

void USPBaselineSpawner::RunFrame(UWorld& World)
{
	const int32 LiveBefore = Live.Num();
	Live.Reserve(LiveBefore + Pending.Num() - NextPending); // one allocation, outside the timed loop's growth
	ConstructMsThisFrame = 0.0;
	FinishMsThisFrame = 0.0;
	int32 Items = 0;

	// One clock for every configuration: Cycles64 around exactly the engine calls (02 section 9).
	const uint64 Start = FPlatformTime::Cycles64();
	while (NextPending < Pending.Num() && MayStartAnother(Items, Start))
	{
		BringIn(World, Pending[NextPending++]);
		++Items;
	}
	const int32 BroughtIn = Live.Num() - LiveBefore;
	if (NextPending >= Pending.Num())
	{
		Pending.Reset();
		NextPending = 0;
	}

	// The despawn shares the frame's limit with any spawning still going on.
	int32 TakenOut = 0;
	if (bDespawnRequested)
	{
		while (!Live.IsEmpty() && MayStartAnother(Items, Start))
		{
			TakenOut += TakeOutLast() ? 1 : 0;
			++Items;
		}
		bDespawnRequested = !Live.IsEmpty();
	}
	const double WorkMs = FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - Start);

	// Once per frame, never per actor (a CSV stat costs about 50 ns, measured by UE5-UpdateBudget).
	CSV_CUSTOM_STAT(SPBaseline, WorkMs, WorkMs, ECsvCustomStatOp::Set);
	// Added / Removed: spawned or taken from the pool; destroyed or parked (one meaning for every baseline).
	CSV_CUSTOM_STAT(SPBaseline, Added, BroughtIn, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(SPBaseline, Removed, TakenOut, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(SPBaseline, Live, Live.Num(), ECsvCustomStatOp::Set);
	if (Mode == ESPBaseline::B1)
	{
		CSV_CUSTOM_STAT(SPBaseline, ConstructMs, ConstructMsThisFrame, ECsvCustomStatOp::Set);
		CSV_CUSTOM_STAT(SPBaseline, FinishMs, FinishMsThisFrame, ECsvCustomStatOp::Set);
	}
}

void USPBaselineSpawner::BringIn(UWorld& World, const FSPWaveEntry& Entry)
{
	if (Mode == ESPBaseline::B3)
	{
		if (ASPWaveActor* Actor = TakeFromPool(Entry.Class))
		{
			Actor->SetActorTransform(Entry.Transform, false, nullptr, ETeleportType::ResetPhysics);
			Actor->SetActorHiddenInGame(false);
			Actor->SetActorEnableCollision(true);
			Actor->SetActorTickEnabled(true); // does nothing for classes that can't tick
			Actor->PrepareForReuse();
			Live.Add(Actor);
			Actor->NotifyReady(); // a reused actor doesn't run BeginPlay again
			return;
		}
		// Pool miss: a competent pool spawns a new actor (below).
	}

	// B0, B2 and a B3 miss: plain SpawnActor (construction, registration, construction script and BeginPlay, all now).
	// B1: SpawnActorDeferred stops before the construction script; FinishSpawning runs it and BeginPlay, in this same
	// frame (PoolManager does the same, 01b 5).
	const uint64 SpawnStart = FPlatformTime::Cycles64();
	ASPWaveActor* Actor = World.SpawnActor<ASPWaveActor>(Entry.Class, Entry.Transform, SpawnParameters);
	if (Actor && SpawnParameters.bDeferConstruction)
	{
		const uint64 FinishStart = FPlatformTime::Cycles64();
		Actor->FinishSpawning(Entry.Transform);
		ConstructMsThisFrame += FPlatformTime::ToMilliseconds64(FinishStart - SpawnStart);
		FinishMsThisFrame += FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - FinishStart);
	}
	if (Actor)
	{
		Live.Add(Actor);
	}
}

ASPWaveActor* USPBaselineSpawner::TakeFromPool(UClass* Class)
{
	FSPPooledActors* Parked = Pool.Find(Class);
	while (Parked && !Parked->Actors.IsEmpty())
	{
		// Pop until a valid actor turns up: something else may have destroyed a parked one (Mass does the same).
		ASPWaveActor* Actor = Parked->Actors.Pop(EAllowShrinking::No).Get();
		if (IsValid(Actor))
		{
			return Actor;
		}
	}
	return nullptr;
}

bool USPBaselineSpawner::TakeOutLast()
{
	AActor* Actor = Live.Pop(EAllowShrinking::No);
	if (!IsValid(Actor)) // something else may already have destroyed it (level teardown, gameplay)
	{
		return false;
	}
	if (Mode == ESPBaseline::B3)
	{
		ASPWaveActor& WaveActor = *CastChecked<ASPWaveActor>(Actor);
		Park(WaveActor);
		Pool.FindOrAdd(Actor->GetClass()).Actors.Add(&WaveActor);
		return true;
	}
	return Actor->Destroy();
}
