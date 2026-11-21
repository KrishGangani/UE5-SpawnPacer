// SpawnPacerCore - subsystem tests in a real game world: stages, cancellation, re-entrant requests, world teardown.
// Run with: Tools\run_tests.py (through the machine queue), filter "SpawnPacer.Subsystem".
//
// The world comes from the engine's FTestWorldWrapper (Engine/Public/Tests/AutomationCommon.h), as UE5-UpdateBudget's
// lifetime tests do: a game world with a game mode, so actors really begin play. TickTestWorld runs UWorld::Tick
// (which broadcasts OnWorldPostActorTick, where SpawnPacer works) and advances GFrameCounter.

#include "Engine/LatentActionManager.h"
#include "LatentActions.h"
#include "SpawnPacerSubsystem.h"
#include "Tests/AutomationCommon.h"
#include "Tests/SpawnPacerTestFlags.h"
#include "Tests/SpawnPacerTestTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace SpawnPacerSubsystemTests
{
	constexpr float FrameSeconds = 1.f / 60.f;

	/** Enough frames for any of these tests to drain its queue; reaching it is a failure. */
	constexpr int32 MaxFrames = 1000;

	/**
	 * An engine test world with its SpawnPacer subsystem. Declare whatever its callbacks write to BEFORE it: the world
	 * (and every callback it holds) must be destroyed first.
	 */
	struct FTestPacerWorld
	{
		FTestWorldWrapper Wrapper;
		UWorld* World = nullptr;
		USpawnPacerSubsystem* Pacer = nullptr;

		FTestPacerWorld()
		{
			if (Wrapper.CreateTestWorld(EWorldType::Game) && Wrapper.BeginPlayInTestWorld())
			{
				World = Wrapper.GetTestWorld();
				Pacer = World->GetSubsystem<USpawnPacerSubsystem>();
			}
		}

		bool IsReady(FAutomationTestBase& Test) const
		{
			return Test.TestNotNull(TEXT("test world"), World) && Test.TestNotNull(TEXT("SpawnPacer subsystem"), Pacer);
		}

		void Tick(int32 Frames = 1)
		{
			for (int32 Frame = 0; Frame < Frames; ++Frame)
			{
				Wrapper.TickTestWorld(FrameSeconds);
			}
		}

		/** Ticks until nothing is pending. Returns false if that takes MaxFrames. No extra frame afterwards: a batch
		 *  completes in the same SpawnPacer frame as its last request, and the tests check exactly that. */
		bool TickUntilIdle()
		{
			for (int32 Frame = 0; Frame < MaxFrames && Pacer->GetNumPending() > 0; ++Frame)
			{
				Tick();
			}
			return Pacer->GetNumPending() == 0;
		}

		/** A request for a test actor at Location; AlwaysSpawn, so overlapping test actors are never refused. */
		static FSpawnPacerSpawnParams Request(int32 BatchId = INDEX_NONE, const FVector& Location = FVector::ZeroVector,
			TSubclassOf<ASpawnPacerTestActor> Class = ASpawnPacerTestActor::StaticClass())
		{
			FSpawnPacerSpawnParams Params;
			Params.Class = Class;
			Params.Transform = FTransform(Location);
			Params.BatchId = BatchId;
			Params.CollisionHandling = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			return Params;
		}
	};

	/** Counts the batch-complete broadcasts per batch id. */
	struct FBatchLog
	{
		TMap<int32, int32> Completions;

		void Listen(USpawnPacerSubsystem& Pacer)
		{
			Pacer.OnBatchComplete.AddLambda([this](int32 BatchId) { ++Completions.FindOrAdd(BatchId); });
		}

		int32 Count(int32 BatchId) const { return Completions.FindRef(BatchId); }
	};

	FString ModeName(ESpawnPacerSplitMode Mode)
	{
		return StaticEnum<ESpawnPacerSplitMode>()->GetNameStringByValue(static_cast<int64>(Mode));
	}

	/** Components of an actor that are registered now. */
	int32 CountRegistered(const AActor& Actor)
	{
		TInlineComponentArray<UActorComponent*> Components(&Actor);
		int32 Registered = 0;
		for (const UActorComponent* Component : Components)
		{
			Registered += Component->IsRegistered() ? 1 : 0;
		}
		return Registered;
	}
}

using namespace SpawnPacerSubsystemTests;

// Every split mode spawns every request once, at its transform, with Initialize before BeginPlay; the batch completes once.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpawnPacerSubsystemSpawnTest, "SpawnPacer.Subsystem.EveryRequestSpawnsOnceInEveryMode", SpawnPacerTests::Flags)
bool FSpawnPacerSubsystemSpawnTest::RunTest(const FString& Parameters)
{
	constexpr int32 Count = 20;
	for (const ESpawnPacerSplitMode Mode : { ESpawnPacerSplitMode::Whole, ESpawnPacerSplitMode::Deferred, ESpawnPacerSplitMode::PerComponent })
	{
		TArray<TWeakObjectPtr<ASpawnPacerTestActor>> Spawned;
		FBatchLog Batches;
		FTestPacerWorld Test;
		if (!Test.IsReady(*this))
		{
			return false;
		}
		const FString Name = ModeName(Mode);
		Test.Pacer->SetClassRule(ASpawnPacerTestPartsActor::StaticClass(), Mode);
		Batches.Listen(*Test.Pacer);
		const int32 Batch = Test.Pacer->CreateBatch();
		for (int32 Index = 0; Index < Count; ++Index)
		{
			// The parts actor, so PerComponent really runs Register jobs (S6 code review).
			FSpawnPacerSpawnParams Params = FTestPacerWorld::Request(Batch, FVector(100.0 * Index, 0.0, 0.0), ASpawnPacerTestPartsActor::StaticClass());
			Params.Initialize = [](AActor& Actor) { CastChecked<ASpawnPacerTestActor>(&Actor)->bInitialized = true; };
			Params.OnSpawned = [&Spawned](AActor& Actor) { Spawned.Add(CastChecked<ASpawnPacerTestActor>(&Actor)); };
			Test.Pacer->RequestSpawn(MoveTemp(Params));
		}
		TestTrue(Name + TEXT(": queue drained"), Test.TickUntilIdle());

		TestEqual(Name + TEXT(": spawned"), Spawned.Num(), Count);
		TestEqual(Name + TEXT(": completed"), Test.Pacer->GetTotals().Completed, Count);
		TestEqual(Name + TEXT(": dropped"), Test.Pacer->GetTotals().Dropped, 0);
		TestEqual(Name + TEXT(": batch complete once"), Batches.Count(Batch), 1);
		for (int32 Index = 0; Index < Spawned.Num(); ++Index)
		{
			const ASpawnPacerTestActor* Actor = Spawned[Index].Get();
			if (!TestNotNull(Name + TEXT(": actor alive"), Actor))
			{
				continue;
			}
			TestTrue(Name + TEXT(": begun play"), Actor->HasActorBegunPlay());
			TestEqual(Name + TEXT(": BeginPlay once"), Actor->BeginPlayCalls, 1);
			TestTrue(Name + TEXT(": Initialize ran before BeginPlay"), Actor->bInitializedAtBeginPlay);
			// Request order is kept (one FIFO level), so the Nth spawned is the Nth requested.
			TestEqual(Name + TEXT(": location"), Actor->GetActorLocation(), FVector(100.0 * Index, 0.0, 0.0));
		}
	}
	return true;
}

// 02 section 8: an actor destroyed between its stages (before Finish, or between two Register jobs) is dropped and counted,
// never finished.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpawnPacerSubsystemDestroyedBetweenStagesTest, "SpawnPacer.Subsystem.ActorDestroyedBetweenStagesIsDropped", SpawnPacerTests::Flags)
bool FSpawnPacerSubsystemDestroyedBetweenStagesTest::RunTest(const FString& Parameters)
{
	for (const ESpawnPacerSplitMode Mode : { ESpawnPacerSplitMode::Deferred, ESpawnPacerSplitMode::PerComponent })
	{
		TWeakObjectPtr<AActor> Constructed;
		int32 SpawnedCalls = 0;
		FBatchLog Batches;
		FTestPacerWorld Test;
		if (!Test.IsReady(*this))
		{
			return false;
		}
		const FString Name = ModeName(Mode);
		Test.Pacer->SetClassRule(ASpawnPacerTestPartsActor::StaticClass(), Mode);
		Batches.Listen(*Test.Pacer);
		const int32 Batch = Test.Pacer->CreateBatch();
		FSpawnPacerSpawnParams Params = FTestPacerWorld::Request(Batch, FVector::ZeroVector, ASpawnPacerTestPartsActor::StaticClass());
		Params.Initialize = [&Constructed](AActor& Actor) { Constructed = &Actor; };
		Params.OnSpawned = [&SpawnedCalls](AActor&) { ++SpawnedCalls; };
		Test.Pacer->RequestSpawn(MoveTemp(Params));

		// Frame 1: the Construct job runs (cold, but the frame's first job); its next job (Finish, or the first Register)
		// is cold too, so it waits.
		Test.Tick();
		if (!TestTrue(Name + TEXT(": constructed after one frame"), Constructed.IsValid()))
		{
			return false;
		}
		TestFalse(Name + TEXT(": not begun play yet"), Constructed->HasActorBegunPlay());
		TestEqual(Name + TEXT(": still pending"), Test.Pacer->GetNumPending(), 1);

		Constructed->Destroy(); // behind SpawnPacer's back
		TestTrue(Name + TEXT(": queue drained"), Test.TickUntilIdle());
		TestEqual(Name + TEXT(": dropped"), Test.Pacer->GetTotals().Dropped, 1);
		TestEqual(Name + TEXT(": completed"), Test.Pacer->GetTotals().Completed, 0);
		TestEqual(Name + TEXT(": OnSpawned not called"), SpawnedCalls, 0);
		TestEqual(Name + TEXT(": batch complete once"), Batches.Count(Batch), 1);
	}
	return true;
}

// S6: a per-component spawn shows BeginPlay exactly what a whole spawn shows: every component that registers itself is
// registered, the dormant one isn't, the parts stand where they should, and the held-back flags are restored.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpawnPacerSubsystemPerComponentEquivalenceTest, "SpawnPacer.Subsystem.PerComponentBeginPlaySeesEveryComponent", SpawnPacerTests::Flags)
bool FSpawnPacerSubsystemPerComponentEquivalenceTest::RunTest(const FString& Parameters)
{
	const FVector Location(300.0, 200.0, 100.0);
	TMap<ESpawnPacerSplitMode, int32> RegisteredAtBeginPlay;
	for (const ESpawnPacerSplitMode Mode : { ESpawnPacerSplitMode::Whole, ESpawnPacerSplitMode::PerComponent })
	{
		TWeakObjectPtr<ASpawnPacerTestPartsActor> Spawned;
		FTestPacerWorld Test;
		if (!Test.IsReady(*this))
		{
			return false;
		}
		const FString Name = ModeName(Mode);
		Test.Pacer->SetClassRule(ASpawnPacerTestPartsActor::StaticClass(), Mode);
		FSpawnPacerSpawnParams Params = FTestPacerWorld::Request(INDEX_NONE, Location, ASpawnPacerTestPartsActor::StaticClass());
		Params.OnSpawned = [&Spawned](AActor& Actor) { Spawned = CastChecked<ASpawnPacerTestPartsActor>(&Actor); };
		Test.Pacer->RequestSpawn(MoveTemp(Params));
		TestTrue(Name + TEXT(": queue drained"), Test.TickUntilIdle());

		const ASpawnPacerTestPartsActor* Actor = Spawned.Get();
		if (!TestNotNull(Name + TEXT(": spawned"), Actor))
		{
			return false;
		}
		RegisteredAtBeginPlay.Add(Mode, Actor->RegisteredAtBeginPlay);
		TestEqual(Name + TEXT(": BeginPlay saw root, PartA, PartB and Extra registered"), Actor->RegisteredAtBeginPlay, 4);
		TestFalse(Name + TEXT(": the dormant part stays unregistered"), Actor->Dormant->IsRegistered());
		TestFalse(Name + TEXT(": and keeps its own flag"), static_cast<bool>(Actor->Dormant->bAutoRegister));
		TestTrue(Name + TEXT(": PartA's flag restored"), static_cast<bool>(Actor->PartA->bAutoRegister));
		TestTrue(Name + TEXT(": PartB's flag restored"), static_cast<bool>(Actor->PartB->bAutoRegister));
		TestTrue(Name + TEXT(": Extra's flag restored"), static_cast<bool>(Actor->Extra->bAutoRegister));
		TestEqual(Name + TEXT(": PartB placed through its parent"), Actor->PartB->GetComponentLocation(),
			Location + ASpawnPacerTestPartsActor::PartAOffset + ASpawnPacerTestPartsActor::PartBOffset);
	}
	TestEqual(TEXT("the same in both modes"), RegisteredAtBeginPlay.FindRef(ESpawnPacerSplitMode::PerComponent),
		RegisteredAtBeginPlay.FindRef(ESpawnPacerSplitMode::Whole));
	return true;
}

// S6: one Register job per held-back scene component, parent before child, and Finish only after the last one. The
// jobs are counted from the frame stats rather than assumed one per frame (S6 code review: a job that measures 0 ms
// lets a second one in even at budget 0).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpawnPacerSubsystemPerComponentOrderTest, "SpawnPacer.Subsystem.PerComponentRegistersOneComponentPerJob", SpawnPacerTests::Flags)
bool FSpawnPacerSubsystemPerComponentOrderTest::RunTest(const FString& Parameters)
{
	TWeakObjectPtr<ASpawnPacerTestPartsActor> Constructed;
	FTestPacerWorld Test;
	if (!Test.IsReady(*this))
	{
		return false;
	}
	Test.Pacer->SetBudgetMs(0.0);
	Test.Pacer->SetClassRule(ASpawnPacerTestPartsActor::StaticClass(), ESpawnPacerSplitMode::PerComponent);
	FSpawnPacerSpawnParams Params = FTestPacerWorld::Request(INDEX_NONE, FVector::ZeroVector, ASpawnPacerTestPartsActor::StaticClass());
	Params.Initialize = [&Constructed](AActor& Actor) { Constructed = CastChecked<ASpawnPacerTestPartsActor>(&Actor); };
	Test.Pacer->RequestSpawn(MoveTemp(Params));

	Test.Tick(); // the Construct job is cold, so it runs alone
	const ASpawnPacerTestPartsActor* Actor = Constructed.Get();
	if (!TestNotNull(TEXT("constructed"), Actor))
	{
		return false;
	}
	// The root and the non-scene Extra register in the Construct job; PartA and PartB are held back.
	TestEqual(TEXT("after Construct: root and Extra"), CountRegistered(*Actor), 2);
	TestFalse(TEXT("PartA held back"), Actor->PartA->IsRegistered());
	TestFalse(TEXT("PartB held back"), Actor->PartB->IsRegistered());
	int32 Jobs = Test.Pacer->GetLastFrameStats().Jobs;
	bool bParentFirst = true;
	bool bNoBeginPlayWhileHeldBack = true;
	for (int32 Frame = 0; Frame < MaxFrames && Test.Pacer->GetNumPending() > 0; ++Frame)
	{
		Test.Tick();
		Jobs += Test.Pacer->GetLastFrameStats().Jobs;
		bParentFirst &= !Actor->PartB->IsRegistered() || Actor->PartA->IsRegistered();
		bNoBeginPlayWhileHeldBack &= !Actor->HasActorBegunPlay() || (Actor->PartA->IsRegistered() && Actor->PartB->IsRegistered());
	}
	TestEqual(TEXT("jobs: Construct + one Register per held-back part + Finish"), Jobs, 4);
	TestTrue(TEXT("parent before child"), bParentFirst);
	TestTrue(TEXT("no BeginPlay while a part is held back"), bNoBeginPlayWhileHeldBack);
	TestTrue(TEXT("begun play"), Actor->HasActorBegunPlay());
	TestEqual(TEXT("BeginPlay saw every component"), Actor->RegisteredAtBeginPlay, 4);
	return true;
}

// 02 section 8: a request made inside a stage (here, from BeginPlay) waits for the next frame and is served then.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpawnPacerSubsystemSpawnFromBeginPlayTest, "SpawnPacer.Subsystem.SpawnFromInsideBeginPlay", SpawnPacerTests::Flags)
bool FSpawnPacerSubsystemSpawnFromBeginPlayTest::RunTest(const FString& Parameters)
{
	for (const ESpawnPacerSplitMode Mode : { ESpawnPacerSplitMode::Whole, ESpawnPacerSplitMode::Deferred })
	{
		uint64 ParentFrame = 0;
		uint64 ChildFrame = 0;
		FTestPacerWorld Test;
		if (!Test.IsReady(*this))
		{
			return false;
		}
		const FString Name = ModeName(Mode);
		USpawnPacerSubsystem* Pacer = Test.Pacer;
		Pacer->SetClassRule(ASpawnPacerTestActor::StaticClass(), Mode);

		FSpawnPacerSpawnParams Parent = FTestPacerWorld::Request();
		Parent.Initialize = [Pacer, &ChildFrame](AActor& Actor)
		{
			CastChecked<ASpawnPacerTestActor>(&Actor)->OnBeginPlay = [Pacer, &ChildFrame](ASpawnPacerTestActor&)
			{
				FSpawnPacerSpawnParams Child = FTestPacerWorld::Request(INDEX_NONE, FVector(0.0, 200.0, 0.0));
				Child.OnSpawned = [&ChildFrame](AActor&) { ChildFrame = GFrameCounter; };
				Pacer->RequestSpawn(MoveTemp(Child));
			};
		};
		Parent.OnSpawned = [&ParentFrame](AActor&) { ParentFrame = GFrameCounter; };
		Pacer->RequestSpawn(MoveTemp(Parent));

		TestTrue(Name + TEXT(": queue drained"), Test.TickUntilIdle());
		TestEqual(Name + TEXT(": both spawned"), Pacer->GetTotals().Completed, 2);
		TestTrue(Name + TEXT(": parent spawned"), ParentFrame != 0);
		TestTrue(Name + TEXT(": child in a later frame than its parent"), ChildFrame > ParentFrame);
	}
	return true;
}

// Ending the world with requests waiting (one half-spawned) calls nothing and leaves nothing behind: a second world works.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpawnPacerSubsystemWorldEndsTest, "SpawnPacer.Subsystem.WorldEndsWithRequestsPending", SpawnPacerTests::Flags)
bool FSpawnPacerSubsystemWorldEndsTest::RunTest(const FString& Parameters)
{
	int32 SpawnedCalls = 0;
	int32 BatchCalls = 0;
	{
		FTestPacerWorld Test;
		if (!Test.IsReady(*this))
		{
			return false;
		}
		Test.Pacer->SetClassRule(ASpawnPacerTestActor::StaticClass(), ESpawnPacerSplitMode::Deferred);
		Test.Pacer->OnBatchComplete.AddLambda([&BatchCalls](int32) { ++BatchCalls; });
		const int32 Batch = Test.Pacer->CreateBatch();
		for (int32 Index = 0; Index < 50; ++Index)
		{
			FSpawnPacerSpawnParams Params = FTestPacerWorld::Request(Batch);
			Params.OnSpawned = [&SpawnedCalls](AActor&) { ++SpawnedCalls; };
			Test.Pacer->RequestSpawn(MoveTemp(Params));
		}
		Test.Tick(); // one Construct job: one actor half-spawned, 49 queued
		TestEqual(TEXT("pending when the world ends"), Test.Pacer->GetNumPending(), 50);
	}   // the world is destroyed here, with everything still waiting
	TestEqual(TEXT("no OnSpawned during teardown"), SpawnedCalls, 0);
	TestEqual(TEXT("no batch event during teardown"), BatchCalls, 0);

	// The global post-actor-tick handler of the first world is gone: a second world spawns normally.
	int32 SecondWorldSpawns = 0;
	FTestPacerWorld Second;
	if (!Second.IsReady(*this))
	{
		return false;
	}
	FSpawnPacerSpawnParams Params = FTestPacerWorld::Request();
	Params.OnSpawned = [&SecondWorldSpawns](AActor&) { ++SecondWorldSpawns; };
	Second.Pacer->RequestSpawn(MoveTemp(Params));
	TestTrue(TEXT("second world drained"), Second.TickUntilIdle());
	TestEqual(TEXT("second world spawned"), SecondWorldSpawns, 1);
	return true;
}

// CancelBatch: requests not started end without a job; a half-spawned actor is destroyed; the batch still completes.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpawnPacerSubsystemCancelTest, "SpawnPacer.Subsystem.CancelBatch", SpawnPacerTests::Flags)
bool FSpawnPacerSubsystemCancelTest::RunTest(const FString& Parameters)
{
	constexpr int32 Count = 10;
	TArray<TWeakObjectPtr<AActor>> Constructed;
	int32 SpawnedCalls = 0;
	FBatchLog Batches;
	FTestPacerWorld Test;
	if (!Test.IsReady(*this))
	{
		return false;
	}
	Test.Pacer->SetClassRule(ASpawnPacerTestActor::StaticClass(), ESpawnPacerSplitMode::Deferred);
	Batches.Listen(*Test.Pacer);
	const int32 Batch = Test.Pacer->CreateBatch();
	for (int32 Index = 0; Index < Count; ++Index)
	{
		FSpawnPacerSpawnParams Params = FTestPacerWorld::Request(Batch);
		Params.Initialize = [&Constructed](AActor& Actor) { Constructed.Add(&Actor); };
		Params.OnSpawned = [&SpawnedCalls](AActor&) { ++SpawnedCalls; };
		Test.Pacer->RequestSpawn(MoveTemp(Params));
	}
	Test.Tick(); // the first Construct job (cold) runs alone
	if (!TestEqual(TEXT("one half-spawned actor"), Constructed.Num(), 1))
	{
		return false;
	}

	Test.Pacer->CancelBatch(Batch);
	TestTrue(TEXT("queue drained"), Test.TickUntilIdle());
	TestEqual(TEXT("cancelled"), Test.Pacer->GetTotals().Cancelled, Count);
	TestEqual(TEXT("completed"), Test.Pacer->GetTotals().Completed, 0);
	TestEqual(TEXT("OnSpawned not called"), SpawnedCalls, 0);
	TestEqual(TEXT("no other actor was constructed"), Constructed.Num(), 1);
	TestFalse(TEXT("the half-spawned actor was destroyed"), Constructed[0].IsValid());
	TestEqual(TEXT("batch complete once"), Batches.Count(Batch), 1);
	return true;
}

// A cancel from inside the request's own Initialize: in both modes the actor is destroyed, never finished (code review).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpawnPacerSubsystemCancelFromInitializeTest, "SpawnPacer.Subsystem.CancelFromInsideInitialize", SpawnPacerTests::Flags)
bool FSpawnPacerSubsystemCancelFromInitializeTest::RunTest(const FString& Parameters)
{
	for (const ESpawnPacerSplitMode Mode : { ESpawnPacerSplitMode::Whole, ESpawnPacerSplitMode::Deferred })
	{
		TWeakObjectPtr<AActor> Constructed;
		bool bInitializeRan = false;
		int32 SpawnedCalls = 0;
		FBatchLog Batches;
		FTestPacerWorld Test;
		if (!Test.IsReady(*this))
		{
			return false;
		}
		const FString Name = ModeName(Mode);
		USpawnPacerSubsystem* Pacer = Test.Pacer;
		Pacer->SetClassRule(ASpawnPacerTestActor::StaticClass(), Mode);
		Batches.Listen(*Pacer);
		const int32 Batch = Pacer->CreateBatch();
		FSpawnPacerSpawnParams Params = FTestPacerWorld::Request(Batch);
		Params.Initialize = [Pacer, Batch, &Constructed, &bInitializeRan](AActor& Actor)
		{
			bInitializeRan = true;
			Constructed = &Actor;
			Pacer->CancelBatch(Batch);
		};
		Params.OnSpawned = [&SpawnedCalls](AActor&) { ++SpawnedCalls; };
		Pacer->RequestSpawn(MoveTemp(Params));

		TestTrue(Name + TEXT(": queue drained"), Test.TickUntilIdle());
		TestTrue(Name + TEXT(": Initialize ran"), bInitializeRan);
		TestFalse(Name + TEXT(": the actor is gone"), Constructed.IsValid());
		TestEqual(Name + TEXT(": cancelled"), Pacer->GetTotals().Cancelled, 1);
		TestEqual(Name + TEXT(": completed"), Pacer->GetTotals().Completed, 0);
		TestEqual(Name + TEXT(": OnSpawned not called"), SpawnedCalls, 0);
		TestEqual(Name + TEXT(": batch complete once"), Batches.Count(Batch), 1);
	}
	return true;
}

// 02 section 8: releasing an actor that is constructed but not finished: it is never finished; its own request becomes
// the release (code review).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpawnPacerSubsystemReleaseHalfSpawnedTest, "SpawnPacer.Subsystem.ReleaseOfAHalfSpawnedActor", SpawnPacerTests::Flags)
bool FSpawnPacerSubsystemReleaseHalfSpawnedTest::RunTest(const FString& Parameters)
{
	for (const ESpawnPacerSplitMode Mode : { ESpawnPacerSplitMode::Deferred, ESpawnPacerSplitMode::PerComponent })
	{
		TWeakObjectPtr<AActor> Constructed;
		int32 SpawnedCalls = 0;
		FBatchLog Batches;
		FTestPacerWorld Test;
		if (!Test.IsReady(*this))
		{
			return false;
		}
		const FString Name = ModeName(Mode);
		Test.Pacer->SetClassRule(ASpawnPacerTestPartsActor::StaticClass(), Mode);
		Batches.Listen(*Test.Pacer);
		const int32 SpawnBatch = Test.Pacer->CreateBatch();
		FSpawnPacerSpawnParams Params = FTestPacerWorld::Request(SpawnBatch, FVector::ZeroVector, ASpawnPacerTestPartsActor::StaticClass());
		Params.Initialize = [&Constructed](AActor& Actor) { Constructed = &Actor; };
		Params.OnSpawned = [&SpawnedCalls](AActor&) { ++SpawnedCalls; };
		Test.Pacer->RequestSpawn(MoveTemp(Params));
		Test.Tick(); // Construct only (its next job is cold)
		if (!TestTrue(Name + TEXT(": half-spawned"), Constructed.IsValid() && !Constructed->HasActorBegunPlay()))
		{
			return false;
		}

		const int32 ReleaseBatch = Test.Pacer->CreateBatch();
		TestTrue(Name + TEXT(": release accepted"), Test.Pacer->RequestRelease(Constructed.Get(), ESpawnPacerPriority::Normal, ReleaseBatch));
		TestEqual(Name + TEXT(": still one request: the spawn became the release"), Test.Pacer->GetNumPending(), 1);
		TestTrue(Name + TEXT(": queue drained"), Test.TickUntilIdle());
		TestFalse(Name + TEXT(": destroyed"), Constructed.IsValid());
		TestEqual(Name + TEXT(": never finished: OnSpawned not called"), SpawnedCalls, 0);
		TestEqual(Name + TEXT(": the spawn is cancelled"), Test.Pacer->GetTotals().Cancelled, 1);
		TestEqual(Name + TEXT(": the release is completed"), Test.Pacer->GetTotals().Completed, 1);
		TestEqual(Name + TEXT(": spawn batch complete once"), Batches.Count(SpawnBatch), 1);
		TestEqual(Name + TEXT(": release batch complete once"), Batches.Count(ReleaseBatch), 1);
	}
	return true;
}

// RequestRelease: budgeted Destroy jobs; the release batch completes when the last actor is gone.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpawnPacerSubsystemReleaseTest, "SpawnPacer.Subsystem.ReleaseDestroysTheActors", SpawnPacerTests::Flags)
bool FSpawnPacerSubsystemReleaseTest::RunTest(const FString& Parameters)
{
	constexpr int32 Count = 5;
	TArray<TWeakObjectPtr<AActor>> Spawned;
	FBatchLog Batches;
	FTestPacerWorld Test;
	if (!Test.IsReady(*this))
	{
		return false;
	}
	Batches.Listen(*Test.Pacer);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		FSpawnPacerSpawnParams Params = FTestPacerWorld::Request();
		Params.OnSpawned = [&Spawned](AActor& Actor) { Spawned.Add(&Actor); };
		Test.Pacer->RequestSpawn(MoveTemp(Params));
	}
	TestTrue(TEXT("spawns drained"), Test.TickUntilIdle());
	if (!TestEqual(TEXT("spawned"), Spawned.Num(), Count))
	{
		return false;
	}

	const int32 Release = Test.Pacer->CreateBatch();
	for (const TWeakObjectPtr<AActor>& Actor : Spawned)
	{
		TestTrue(TEXT("release accepted"), Test.Pacer->RequestRelease(Actor.Get(), ESpawnPacerPriority::Normal, Release));
	}
	TestFalse(TEXT("an invalid actor is refused"), Test.Pacer->RequestRelease(nullptr));
	TestTrue(TEXT("releases drained"), Test.TickUntilIdle());
	for (const TWeakObjectPtr<AActor>& Actor : Spawned)
	{
		TestFalse(TEXT("destroyed"), Actor.IsValid());
	}
	TestEqual(TEXT("completed: spawns and releases"), Test.Pacer->GetTotals().Completed, 2 * Count);
	TestEqual(TEXT("release batch complete once"), Batches.Count(Release), 1);
	return true;
}

// Priorities: a High request made after a Normal one is served first. Budget 0 = one job per frame, so order is visible.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpawnPacerSubsystemPriorityTest, "SpawnPacer.Subsystem.HigherPriorityIsServedFirst", SpawnPacerTests::Flags)
bool FSpawnPacerSubsystemPriorityTest::RunTest(const FString& Parameters)
{
	TArray<FString> Order;
	FTestPacerWorld Test;
	if (!Test.IsReady(*this))
	{
		return false;
	}
	Test.Pacer->SetBudgetMs(0.0);
	for (const ESpawnPacerPriority Priority : { ESpawnPacerPriority::Background, ESpawnPacerPriority::Normal, ESpawnPacerPriority::High })
	{
		FSpawnPacerSpawnParams Params = FTestPacerWorld::Request();
		Params.Priority = Priority;
		Params.OnSpawned = [&Order, Priority](AActor&) { Order.Add(UEnum::GetValueAsString(Priority)); };
		Test.Pacer->RequestSpawn(MoveTemp(Params));
	}
	Test.Tick();
	TestEqual(TEXT("budget 0: one job in the first frame"), Order.Num(), 1);
	TestTrue(TEXT("queue drained"), Test.TickUntilIdle());
	if (TestEqual(TEXT("all three"), Order.Num(), 3))
	{
		TestEqual(TEXT("first"), Order[0], UEnum::GetValueAsString(ESpawnPacerPriority::High));
		TestEqual(TEXT("second"), Order[1], UEnum::GetValueAsString(ESpawnPacerPriority::Normal));
		TestEqual(TEXT("third"), Order[2], UEnum::GetValueAsString(ESpawnPacerPriority::Background));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

// ---------------------------------------------------------------------------------------------------------------- S7

#if WITH_DEV_AUTOMATION_TESTS

namespace SpawnPacerPoolTests
{
	using namespace SpawnPacerSubsystemTests;

	/** Spawns Count pool test actors and drains the queue; returns them (empty on failure). */
	TArray<TWeakObjectPtr<ASpawnPacerTestPoolActor>> SpawnPooled(FTestPacerWorld& Test, int32 Count, const FVector& Origin = FVector::ZeroVector)
	{
		TArray<TWeakObjectPtr<ASpawnPacerTestPoolActor>> Spawned;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			FSpawnPacerSpawnParams Params = FTestPacerWorld::Request(INDEX_NONE, Origin + FVector(100.0 * Index, 0.0, 0.0), ASpawnPacerTestPoolActor::StaticClass());
			Params.OnSpawned = [&Spawned](AActor& Actor) { Spawned.Add(CastChecked<ASpawnPacerTestPoolActor>(&Actor)); };
			Test.Pacer->RequestSpawn(MoveTemp(Params));
		}
		Test.TickUntilIdle();
		return Spawned;
	}

	void ReleaseAll(FTestPacerWorld& Test, const TArray<TWeakObjectPtr<ASpawnPacerTestPoolActor>>& Actors)
	{
		for (const TWeakObjectPtr<ASpawnPacerTestPoolActor>& Actor : Actors)
		{
			Test.Pacer->RequestRelease(Actor.Get());
		}
		Test.TickUntilIdle();
	}

	/** A latent action that never finishes and counts its updates. */
	class FCountingLatentAction : public FPendingLatentAction
	{
	public:
		explicit FCountingLatentAction(int32& InUpdates) : Updates(InUpdates) {}
		virtual void UpdateOperation(FLatentResponse& Response) override { ++Updates; }

	private:
		int32& Updates;
	};
}

using namespace SpawnPacerPoolTests;

// Release parks a pooled actor (reset contract, engine half), and a later spawn request reuses it (Acquire job).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpawnPacerPoolReuseTest, "SpawnPacer.Pool.ReleaseParksAndAcquireReuses", SpawnPacerTests::Flags)
bool FSpawnPacerPoolReuseTest::RunTest(const FString& Parameters)
{
	FTestPacerWorld Test;
	if (!Test.IsReady(*this))
	{
		return false;
	}
	Test.Pacer->SetClassPoolCap(ASpawnPacerTestPoolActor::StaticClass(), 3);
	const TArray<TWeakObjectPtr<ASpawnPacerTestPoolActor>> First = SpawnPooled(Test, 3);
	if (!TestEqual(TEXT("spawned"), First.Num(), 3))
	{
		return false;
	}
	ReleaseAll(Test, First);
	TestEqual(TEXT("parked"), Test.Pacer->GetNumParked(ASpawnPacerTestPoolActor::StaticClass()), 3);
	for (const TWeakObjectPtr<ASpawnPacerTestPoolActor>& Actor : First)
	{
		if (!TestTrue(TEXT("parked, not destroyed"), Actor.IsValid()))
		{
			return false;
		}
		TestEqual(TEXT("OnPoolReleased once"), Actor->Released, 1);
		TestTrue(TEXT("hidden"), Actor->IsHidden());
		TestFalse(TEXT("no collision"), Actor->GetActorEnableCollision());
		TestFalse(TEXT("actor tick off"), Actor->IsActorTickEnabled());
		TestFalse(TEXT("component tick off"), Actor->Ticker->IsComponentTickEnabled());
	}
	TestFalse(TEXT("a parked actor can't be released again"), Test.Pacer->RequestRelease(First[0].Get()));

	const FVector Elsewhere(0.0, 1000.0, 0.0);
	const TArray<TWeakObjectPtr<ASpawnPacerTestPoolActor>> Second = SpawnPooled(Test, 3, Elsewhere);
	TestEqual(TEXT("served"), Second.Num(), 3);
	TestEqual(TEXT("pool emptied"), Test.Pacer->GetNumParked(ASpawnPacerTestPoolActor::StaticClass()), 0);
	for (int32 Index = 0; Index < Second.Num(); ++Index)
	{
		const ASpawnPacerTestPoolActor* Actor = Second[Index].Get();
		if (!TestNotNull(TEXT("acquired"), Actor))
		{
			return false;
		}
		TestTrue(TEXT("one of the parked actors"), First.Contains(Second[Index]));
		TestEqual(TEXT("OnPoolAcquired once"), Actor->Acquired, 1);
		TestEqual(TEXT("no second BeginPlay"), Actor->BeginPlayCalls, 1);
		TestFalse(TEXT("visible"), Actor->IsHidden());
		TestTrue(TEXT("collision back"), Actor->GetActorEnableCollision());
		TestTrue(TEXT("actor tick back"), Actor->IsActorTickEnabled());
		TestTrue(TEXT("component tick back"), Actor->Ticker->IsComponentTickEnabled());
		TestEqual(TEXT("at the new transform"), Actor->GetActorLocation(), Elsewhere + FVector(100.0 * Index, 0.0, 0.0));
	}
	return true;
}

// 03 S7: a parked actor costs no tick, neither its own nor its components'.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpawnPacerPoolNoTickTest, "SpawnPacer.Pool.ParkedActorCostsNoTick", SpawnPacerTests::Flags)
bool FSpawnPacerPoolNoTickTest::RunTest(const FString& Parameters)
{
	FTestPacerWorld Test;
	if (!Test.IsReady(*this))
	{
		return false;
	}
	Test.Pacer->SetClassPoolCap(ASpawnPacerTestPoolActor::StaticClass(), 1);
	const TArray<TWeakObjectPtr<ASpawnPacerTestPoolActor>> Spawned = SpawnPooled(Test, 1);
	if (!TestEqual(TEXT("spawned"), Spawned.Num(), 1))
	{
		return false;
	}
	ASpawnPacerTestPoolActor* Actor = Spawned[0].Get();
	Test.Tick(5);
	TestTrue(TEXT("ticks while live"), Actor->Ticks > 0 && Actor->Ticker->Ticks > 0);

	ReleaseAll(Test, Spawned);
	const int32 ActorTicks = Actor->Ticks;
	const int32 ComponentTicks = Actor->Ticker->Ticks;
	Test.Tick(10);
	TestEqual(TEXT("no actor tick while parked"), Actor->Ticks, ActorTicks);
	TestEqual(TEXT("no component tick while parked"), Actor->Ticker->Ticks, ComponentTicks);

	SpawnPooled(Test, 1);
	Test.Tick(5);
	TestTrue(TEXT("actor ticks again after reuse"), Actor->Ticks > ActorTicks);
	TestTrue(TEXT("component ticks again after reuse"), Actor->Ticker->Ticks > ComponentTicks);
	return true;
}

// 03 S7: a timer or latent action set during one use never fires after the actor is reused.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpawnPacerPoolTimerTest, "SpawnPacer.Pool.TimerFromUseNeverFiresAfterReuse", SpawnPacerTests::Flags)
bool FSpawnPacerPoolTimerTest::RunTest(const FString& Parameters)
{
	int32 LatentUpdates = 0; // before the world: the latent action keeps a reference to it
	FTestPacerWorld Test;
	if (!Test.IsReady(*this))
	{
		return false;
	}
	Test.Pacer->SetClassPoolCap(ASpawnPacerTestPoolActor::StaticClass(), 1);
	const TArray<TWeakObjectPtr<ASpawnPacerTestPoolActor>> Spawned = SpawnPooled(Test, 1);
	if (!TestEqual(TEXT("spawned"), Spawned.Num(), 1))
	{
		return false;
	}
	ASpawnPacerTestPoolActor* Actor = Spawned[0].Get();
	Actor->ArmTimers(0.2f); // 12 frames at 60 fps
	Test.World->GetLatentActionManager().AddNewAction(Actor, 1, new FCountingLatentAction(LatentUpdates));
	Test.Tick();
	TestTrue(TEXT("the latent action runs while in use"), LatentUpdates > 0);

	ReleaseAll(Test, Spawned); // parked well before the timers are due
	SpawnPooled(Test, 1);      // and reused
	const int32 UpdatesAtReuse = LatentUpdates;
	Test.Tick(30);             // half a second: long past the timers' time
	TestEqual(TEXT("reused"), Actor->Acquired, 1);
	TestEqual(TEXT("the actor's timer never fired"), Actor->TimerFired, 0);
	TestEqual(TEXT("the component's timer never fired"), Actor->Ticker->TimerFired, 0);
	TestEqual(TEXT("the latent action is gone"), Test.World->GetLatentActionManager().GetNumActionsForObject(Actor), 0);
	TestEqual(TEXT("and never ran again"), LatentUpdates, UpdatesAtReuse);
	return true;
}

// The S7 code-review cases: a double release parks once; an actor hidden and without collision at release comes back as a
// fresh actor of its class; an actor parked in a frame is not acquired in that same frame (its latent actions are only
// removed at the next latent update).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpawnPacerPoolEdgeTest, "SpawnPacer.Pool.EdgeCasesFromCodeReview", SpawnPacerTests::Flags)
bool FSpawnPacerPoolEdgeTest::RunTest(const FString& Parameters)
{
	TWeakObjectPtr<AActor> SameFrame;
	FTestPacerWorld Test;
	if (!Test.IsReady(*this))
	{
		return false;
	}
	UClass* Class = ASpawnPacerTestPoolActor::StaticClass();
	Test.Pacer->SetClassPoolCap(Class, 2);
	const TArray<TWeakObjectPtr<ASpawnPacerTestPoolActor>> Spawned = SpawnPooled(Test, 1);
	if (!TestEqual(TEXT("spawned"), Spawned.Num(), 1))
	{
		return false;
	}
	ASpawnPacerTestPoolActor* Actor = Spawned[0].Get();

	Actor->SetActorHiddenInGame(true);       // a dying enemy hides itself...
	Actor->SetActorEnableCollision(false);   // ...and stops colliding, then is released twice
	TestTrue(TEXT("first release"), Test.Pacer->RequestRelease(Actor));
	TestTrue(TEXT("second release, queued before the first ran"), Test.Pacer->RequestRelease(Actor));
	Test.TickUntilIdle();
	TestEqual(TEXT("parked once"), Test.Pacer->GetNumParked(Class), 1);
	TestEqual(TEXT("OnPoolReleased once"), Actor->Released, 1);

	SpawnPooled(Test, 1);
	TestEqual(TEXT("reused"), Actor->Acquired, 1);
	TestFalse(TEXT("visible, as a fresh actor"), Actor->IsHidden());
	TestTrue(TEXT("colliding, as a fresh actor"), Actor->GetActorEnableCollision());

	// Release and request in one frame; every key is warm and the budget is large, so both jobs run in that frame.
	Test.Pacer->SetBudgetMs(100.0);
	Test.Pacer->RequestRelease(Actor);
	FSpawnPacerSpawnParams Params = FTestPacerWorld::Request(INDEX_NONE, FVector::ZeroVector, Class);
	Params.OnSpawned = [&SameFrame](AActor& Spawn) { SameFrame = &Spawn; };
	Test.Pacer->RequestSpawn(MoveTemp(Params));
	Test.Tick();
	TestTrue(TEXT("both jobs ran in one frame"), SameFrame.IsValid() && Test.Pacer->GetNumPending() == 0);
	TestTrue(TEXT("not the actor parked in that frame"), SameFrame.Get() != Actor);
	TestEqual(TEXT("which stays parked"), Test.Pacer->GetNumParked(Class), 1);
	return true;
}

// A full pool destroys the rest of the releases.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpawnPacerPoolCapTest, "SpawnPacer.Pool.FullPoolDestroysTheRest", SpawnPacerTests::Flags)
bool FSpawnPacerPoolCapTest::RunTest(const FString& Parameters)
{
	FTestPacerWorld Test;
	if (!Test.IsReady(*this))
	{
		return false;
	}
	Test.Pacer->SetClassPoolCap(ASpawnPacerTestPoolActor::StaticClass(), 2);
	const TArray<TWeakObjectPtr<ASpawnPacerTestPoolActor>> Spawned = SpawnPooled(Test, 3);
	ReleaseAll(Test, Spawned);
	int32 Alive = 0;
	for (const TWeakObjectPtr<ASpawnPacerTestPoolActor>& Actor : Spawned)
	{
		Alive += Actor.IsValid() ? 1 : 0;
	}
	TestEqual(TEXT("parked up to the cap"), Test.Pacer->GetNumParked(ASpawnPacerTestPoolActor::StaticClass()), 2);
	TestEqual(TEXT("the third destroyed"), Alive, 2);
	return true;
}

// Prewarm fills the pool up to its cap (BeginPlay runs once per object), and the next spawns are served by Acquire jobs.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpawnPacerPoolPrewarmTest, "SpawnPacer.Pool.PrewarmFillsThePool", SpawnPacerTests::Flags)
bool FSpawnPacerPoolPrewarmTest::RunTest(const FString& Parameters)
{
	FTestPacerWorld Test;
	if (!Test.IsReady(*this))
	{
		return false;
	}
	Test.Pacer->SetClassPoolCap(ASpawnPacerTestPoolActor::StaticClass(), 4);
	TestEqual(TEXT("clamped to the room in the pool"), Test.Pacer->Prewarm(ASpawnPacerTestPoolActor::StaticClass(), 10), 4);
	TestTrue(TEXT("drained"), Test.TickUntilIdle());
	TestEqual(TEXT("parked"), Test.Pacer->GetNumParked(ASpawnPacerTestPoolActor::StaticClass()), 4);
	TestEqual(TEXT("pre-warm requests completed"), Test.Pacer->GetTotals().Completed, 4);

	const TArray<TWeakObjectPtr<ASpawnPacerTestPoolActor>> Served = SpawnPooled(Test, 4);
	TestEqual(TEXT("served"), Served.Num(), 4);
	TestEqual(TEXT("from the pool"), Test.Pacer->GetNumParked(ASpawnPacerTestPoolActor::StaticClass()), 0);
	for (const TWeakObjectPtr<ASpawnPacerTestPoolActor>& Actor : Served)
	{
		if (TestTrue(TEXT("alive"), Actor.IsValid()))
		{
			TestEqual(TEXT("BeginPlay once, at pre-warm"), Actor->BeginPlayCalls, 1);
			TestEqual(TEXT("parked once"), Actor->Released, 1);
			TestEqual(TEXT("acquired once"), Actor->Acquired, 1);
		}
	}
	return true;
}

// S8: the reset verifier finds a reflected property a release left changed, and finds nothing after a correct reset.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpawnPacerVerifierTest, "SpawnPacer.Pool.VerifierFindsAPropertyLeftChanged", SpawnPacerTests::Flags)
bool FSpawnPacerVerifierTest::RunTest(const FString& Parameters)
{
	TArray<int32> LeaksPerAcquire;
	TArray<FString> Kinds;
	FTestPacerWorld Test;
	if (!Test.IsReady(*this))
	{
		return false;
	}
	Test.Pacer->SetVerifyReset(true);
	Test.Pacer->SetClassPoolCap(ASpawnPacerTestPoolActor::StaticClass(), 1);
	Test.Pacer->OnResetVerified.AddLambda([&LeaksPerAcquire, &Kinds](AActor&, const TArray<FSpawnPacerLeak>& Leaks)
	{
		LeaksPerAcquire.Add(Leaks.Num());
		for (const FSpawnPacerLeak& Leak : Leaks)
		{
			Kinds.Add(FString(FSpawnPacerResetVerifier::KindName(Leak.Kind)) + TEXT(" ") + Leak.Where);
		}
	});
	TArray<TWeakObjectPtr<ASpawnPacerTestPoolActor>> Spawned = SpawnPooled(Test, 1);
	if (!TestEqual(TEXT("spawned"), Spawned.Num(), 1))
	{
		return false;
	}
	ASpawnPacerTestPoolActor* Actor = Spawned[0].Get();

	Actor->Uses = 1;            // used, and reset correctly
	ReleaseAll(Test, Spawned);
	SpawnPooled(Test, 1);
	Actor->Uses = 3;            // used, and the reset is skipped
	Actor->bResetUses = false;
	ReleaseAll(Test, Spawned);
	SpawnPooled(Test, 1);

	for (const FString& Kind : Kinds)
	{
		AddInfo(Kind);
	}
	if (TestEqual(TEXT("two verified acquires"), LeaksPerAcquire.Num(), 2))
	{
		TestEqual(TEXT("a correct reset: no leak"), LeaksPerAcquire[0], 0);
		TestEqual(TEXT("a skipped reset: exactly that property"), LeaksPerAcquire[1], 1);
	}
	TestEqual(TEXT("counted as an actor-property leak"), Test.Pacer->GetResetLeaks(ESpawnPacerLeakKind::ActorProperty), 1);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
