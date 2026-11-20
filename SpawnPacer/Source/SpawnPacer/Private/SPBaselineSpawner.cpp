// P02 SpawnPacer (demo) - USPBaselineSpawner.

#include "SPBaselineSpawner.h"

#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "SPWaveActor.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(SPBaselineSpawner)

// Per-frame cost of the baseline spawners. SpawnPacer publishes the same numbers in its own category (S5).
CSV_DEFINE_CATEGORY(SPBaseline, true);

void USPBaselineSpawner::RequestWave(TConstArrayView<FSPWaveEntry> Entries)
{
	Pending.Append(Entries.GetData(), Entries.Num());
}

void USPBaselineSpawner::RequestDespawn()
{
	bDespawnRequested = true;
}

void USPBaselineSpawner::RunFrame(UWorld& World)
{
	int32 Spawned = 0;
	int32 Destroyed = 0;

	// One clock for every configuration: Cycles64 around exactly the engine calls (02 section 9).
	const uint64 Start = FPlatformTime::Cycles64();
	if (!Pending.IsEmpty())
	{
		Spawned = SpawnAllPending(World);
	}
	if (bDespawnRequested)
	{
		Destroyed = DestroyAllLive();
	}
	const double WorkMs = FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - Start);

	// Once per frame, never per actor (a CSV stat costs about 50 ns, measured by UE5-UpdateBudget).
	CSV_CUSTOM_STAT(SPBaseline, WorkMs, WorkMs, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(SPBaseline, Spawned, Spawned, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(SPBaseline, Destroyed, Destroyed, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(SPBaseline, Live, Live.Num(), ECsvCustomStatOp::Set);
}

int32 USPBaselineSpawner::SpawnAllPending(UWorld& World)
{
	FActorSpawnParameters Parameters;
	// The wave's positions are fixed by the seed; a collision test must never move or reject one.
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	const int32 LiveBefore = Live.Num();
	Live.Reserve(LiveBefore + Pending.Num());
	for (const FSPWaveEntry& Entry : Pending)
	{
		// B0: plain SpawnActor. Construction, registration, construction script and BeginPlay all happen here, now.
		if (ASPWaveActor* Actor = World.SpawnActor<ASPWaveActor>(Entry.Class, Entry.Transform, Parameters))
		{
			Live.Add(Actor);
		}
	}
	Pending.Reset();
	return Live.Num() - LiveBefore;
}

int32 USPBaselineSpawner::DestroyAllLive()
{
	int32 Destroyed = 0;
	for (AActor* Actor : Live)
	{
		// IsValid: something else may already have destroyed it (level teardown, gameplay).
		if (IsValid(Actor) && Actor->Destroy())
		{
			++Destroyed;
		}
	}
	Live.Reset();
	bDespawnRequested = false;
	return Destroyed;
}
