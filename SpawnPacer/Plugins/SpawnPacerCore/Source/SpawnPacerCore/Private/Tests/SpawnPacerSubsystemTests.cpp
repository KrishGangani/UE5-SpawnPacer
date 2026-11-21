// SpawnPacerCore - subsystem tests in a real game world: stages, cancellation, re-entrant requests, world teardown.
// Run with: Tools\run_tests.py (through the machine queue), filter "SpawnPacer.Subsystem".
//
// The world comes from the engine's FTestWorldWrapper (Engine/Public/Tests/AutomationCommon.h), as UE5-UpdateBudget's
// lifetime tests do: a game world with a game mode, so actors really begin play. TickTestWorld runs UWorld::Tick
// (which broadcasts OnWorldPostActorTick, where SpawnPacer works) and advances GFrameCounter.

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

		/** A request for the test actor at Location; AlwaysSpawn, so overlapping test actors are never refused. */
		static FSpawnPacerSpawnParams Request(int32 BatchId = INDEX_NONE, const FVector& Location = FVector::ZeroVector)
		{
			FSpawnPacerSpawnParams Params;
			Params.Class = ASpawnPacerTestActor::StaticClass();
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

	const TCHAR* ModeName(ESpawnPacerSplitMode Mode)
	{
		return Mode == ESpawnPacerSplitMode::Whole ? TEXT("Whole") : TEXT("Deferred");
	}
}

using namespace SpawnPacerSubsystemTests;

// Both split modes spawn every request once, at its transform, with Initialize before BeginPlay; the batch completes once.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpawnPacerSubsystemSpawnTest, "SpawnPacer.Subsystem.EveryRequestSpawnsOnceInBothModes", SpawnPacerTests::Flags)
bool FSpawnPacerSubsystemSpawnTest::RunTest(const FString& Parameters)
{
	constexpr int32 Count = 20;
	for (const ESpawnPacerSplitMode Mode : { ESpawnPacerSplitMode::Whole, ESpawnPacerSplitMode::Deferred })
	{
		TArray<TWeakObjectPtr<ASpawnPacerTestActor>> Spawned;
		FBatchLog Batches;
		FTestPacerWorld Test;
		if (!Test.IsReady(*this))
		{
			return false;
		}
		const FString Name = ModeName(Mode);
		Test.Pacer->SetClassRule(ASpawnPacerTestActor::StaticClass(), Mode);
		Batches.Listen(*Test.Pacer);
		const int32 Batch = Test.Pacer->CreateBatch();
		for (int32 Index = 0; Index < Count; ++Index)
		{
			FSpawnPacerSpawnParams Params = FTestPacerWorld::Request(Batch, FVector(100.0 * Index, 0.0, 0.0));
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

// 02 section 8: an actor destroyed between Construct and Finish is dropped and counted, never finished.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpawnPacerSubsystemDestroyedBetweenStagesTest, "SpawnPacer.Subsystem.ActorDestroyedBetweenStagesIsDropped", SpawnPacerTests::Flags)
bool FSpawnPacerSubsystemDestroyedBetweenStagesTest::RunTest(const FString& Parameters)
{
	TWeakObjectPtr<AActor> Constructed;
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
	FSpawnPacerSpawnParams Params = FTestPacerWorld::Request(Batch);
	Params.Initialize = [&Constructed](AActor& Actor) { Constructed = &Actor; };
	Params.OnSpawned = [&SpawnedCalls](AActor&) { ++SpawnedCalls; };
	Test.Pacer->RequestSpawn(MoveTemp(Params));

	// Frame 1: the Construct job runs (cold, but the frame's first job); the Finish job is cold too, so it waits.
	Test.Tick();
	if (!TestTrue(TEXT("constructed after one frame"), Constructed.IsValid()))
	{
		return false;
	}
	TestFalse(TEXT("not begun play yet"), Constructed->HasActorBegunPlay());
	TestEqual(TEXT("still pending"), Test.Pacer->GetNumPending(), 1);

	Constructed->Destroy(); // behind SpawnPacer's back
	TestTrue(TEXT("queue drained"), Test.TickUntilIdle());
	TestEqual(TEXT("dropped"), Test.Pacer->GetTotals().Dropped, 1);
	TestEqual(TEXT("completed"), Test.Pacer->GetTotals().Completed, 0);
	TestEqual(TEXT("OnSpawned not called"), SpawnedCalls, 0);
	TestEqual(TEXT("batch complete once"), Batches.Count(Batch), 1);
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
	TWeakObjectPtr<AActor> Constructed;
	int32 SpawnedCalls = 0;
	FBatchLog Batches;
	FTestPacerWorld Test;
	if (!Test.IsReady(*this))
	{
		return false;
	}
	Test.Pacer->SetClassRule(ASpawnPacerTestActor::StaticClass(), ESpawnPacerSplitMode::Deferred);
	Batches.Listen(*Test.Pacer);
	const int32 SpawnBatch = Test.Pacer->CreateBatch();
	FSpawnPacerSpawnParams Params = FTestPacerWorld::Request(SpawnBatch);
	Params.Initialize = [&Constructed](AActor& Actor) { Constructed = &Actor; };
	Params.OnSpawned = [&SpawnedCalls](AActor&) { ++SpawnedCalls; };
	Test.Pacer->RequestSpawn(MoveTemp(Params));
	Test.Tick(); // Construct only (its Finish job is cold)
	if (!TestTrue(TEXT("half-spawned"), Constructed.IsValid() && !Constructed->HasActorBegunPlay()))
	{
		return false;
	}

	const int32 ReleaseBatch = Test.Pacer->CreateBatch();
	TestTrue(TEXT("release accepted"), Test.Pacer->RequestRelease(Constructed.Get(), ESpawnPacerPriority::Normal, ReleaseBatch));
	TestEqual(TEXT("still one request: the spawn became the release"), Test.Pacer->GetNumPending(), 1);
	TestTrue(TEXT("queue drained"), Test.TickUntilIdle());
	TestFalse(TEXT("destroyed"), Constructed.IsValid());
	TestEqual(TEXT("never finished: OnSpawned not called"), SpawnedCalls, 0);
	TestEqual(TEXT("the spawn is cancelled"), Test.Pacer->GetTotals().Cancelled, 1);
	TestEqual(TEXT("the release is completed"), Test.Pacer->GetTotals().Completed, 1);
	TestEqual(TEXT("spawn batch complete once"), Batches.Count(SpawnBatch), 1);
	TestEqual(TEXT("release batch complete once"), Batches.Count(ReleaseBatch), 1);
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
