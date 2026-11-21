// SpawnPacerCore - USpawnPacerSubsystem.

#include "SpawnPacerSubsystem.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformTime.h"
#include "ProfilingDebugging/CsvProfiler.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(SpawnPacerSubsystem)

DEFINE_LOG_CATEGORY_STATIC(LogSpawnPacer, Log, All);

// CSV category of the per-frame telemetry (02 section 4.1) and the BatchComplete events.
CSV_DEFINE_CATEGORY(SpawnPacer, true);

namespace SpawnPacer
{
	constexpr int32 NumPriorities = static_cast<int32>(ESpawnPacerPriority::High) + 1;

	/** A queue is compacted when at least this many ended requests sit before its head and they are most of it. */
	constexpr int32 CompactAfter = 64;
}

bool USpawnPacerSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void USpawnPacerSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Queues.SetNum(SpawnPacer::NumPriorities); // never resized again: HeadQueue points into it
	PostActorTickHandle = FWorldDelegates::OnWorldPostActorTick.AddUObject(this, &USpawnPacerSubsystem::HandlePostActorTick);
}

void USpawnPacerSubsystem::Deinitialize()
{
	FWorldDelegates::OnWorldPostActorTick.Remove(PostActorTickHandle);
	// Requests still waiting end silently with the world: no callbacks, no batch events. Half-spawned actors are in the
	// level's actor list, so the world destroys them with everything else.
	for (FSpawnPacerQueue& Queue : Queues)
	{
		Queue.Requests.Empty();
		Queue.Head = 0;
	}
	Incoming.Empty();
	OpenBatches.Empty();
	OnBatchComplete.Clear();
	HeadQueue = nullptr;
	Super::Deinitialize();
}

int32 USpawnPacerSubsystem::CreateBatch()
{
	bActive = true;
	const int32 BatchId = NextBatchId++;
	OpenBatches.Add(BatchId, 0);
	return BatchId;
}

void USpawnPacerSubsystem::AddToBatch(int32 BatchId)
{
	if (BatchId == INDEX_NONE)
	{
		return;
	}
	if (int32* Outstanding = OpenBatches.Find(BatchId))
	{
		++*Outstanding;
	}
	else
	{
		UE_LOG(LogSpawnPacer, Warning, TEXT("Batch %d is not open (never created, or already complete): the request is not counted in it."), BatchId);
	}
}

bool USpawnPacerSubsystem::RequestSpawn(FSpawnPacerSpawnParams&& Params)
{
	if (!Params.Class)
	{
		return false;
	}
	bActive = true;
	const ESpawnPacerSplitMode* Rule = ClassRules.Find(Params.Class.Get());
	const bool bDeferred = Rule && *Rule == ESpawnPacerSplitMode::Deferred;

	FSpawnPacerRequest& Request = Incoming.AddDefaulted_GetRef();
	Request.Kind = ESpawnPacerRequestKind::Spawn;
	Request.Class = Params.Class;
	Request.Transform = Params.Transform;
	Request.Initialize = MoveTemp(Params.Initialize);
	Request.OnSpawned = MoveTemp(Params.OnSpawned);
	Request.BatchId = Params.BatchId;
	Request.NextStage = bDeferred ? ESpawnPacerStage::Construct : ESpawnPacerStage::Spawn;
	Request.Priority = Params.Priority;
	Request.CollisionHandling = Params.CollisionHandling;
	AddToBatch(Params.BatchId);
	return true;
}

bool USpawnPacerSubsystem::RequestRelease(AActor* Actor, ESpawnPacerPriority Priority, int32 BatchId)
{
	if (!IsValid(Actor))
	{
		return false;
	}
	bActive = true;

	// 02 section 8: an actor that hasn't finished spawning is never finished once released; its own request becomes the
	// release. A half-spawned request is always at the head of its queue (it stays there from its Construct job to its
	// Finish job), so only the heads are checked. Not the request whose stage is running right now (a BeginPlay that
	// releases its own actor): that one completes, and the release queued below follows it.
	for (FSpawnPacerQueue& Queue : Queues)
	{
		if (Queue.Num() == 0)
		{
			continue;
		}
		FSpawnPacerRequest& Head = Queue.Requests[Queue.Head];
		if (&Head != Running && Head.Kind == ESpawnPacerRequestKind::Spawn && Head.NextStage == ESpawnPacerStage::Finish && Head.Actor.Get() == Actor)
		{
			End(Head, EOutcome::Cancelled); // the spawn ends here, and its batch is told
			Head.Kind = ESpawnPacerRequestKind::Release;
			Head.NextStage = ESpawnPacerStage::Destroy;
			Head.BatchId = BatchId;
			Head.bCancelled = false;
			Head.Initialize.Reset();
			Head.OnSpawned.Reset();
			AddToBatch(BatchId);
			return true;
		}
	}

	FSpawnPacerRequest& Request = Incoming.AddDefaulted_GetRef();
	Request.Kind = ESpawnPacerRequestKind::Release;
	Request.Class = Actor->GetClass();
	Request.Actor = Actor;
	Request.BatchId = BatchId;
	Request.NextStage = ESpawnPacerStage::Destroy;
	Request.Priority = Priority;
	AddToBatch(BatchId);
	return true;
}

void USpawnPacerSubsystem::ForEachOpenRequest(TFunctionRef<void(FSpawnPacerRequest&)> Visitor)
{
	for (FSpawnPacerQueue& Queue : Queues)
	{
		for (int32 Index = Queue.Head; Index < Queue.Requests.Num(); ++Index)
		{
			Visitor(Queue.Requests[Index]);
		}
	}
	for (FSpawnPacerRequest& Request : Incoming)
	{
		Visitor(Request);
	}
}

void USpawnPacerSubsystem::CancelBatch(int32 BatchId)
{
	if (BatchId == INDEX_NONE)
	{
		return;
	}
	// Only a flag changes, never the arrays: safe even from a callback inside a stage. What a cancelled request does is
	// decided when it reaches the head (FindHead), from its state then, so a cancel made at any moment is handled.
	ForEachOpenRequest([BatchId](FSpawnPacerRequest& Request)
	{
		if (Request.BatchId == BatchId)
		{
			Request.bCancelled = true;
		}
	});
}

void USpawnPacerSubsystem::SetClassRule(TSubclassOf<AActor> Class, ESpawnPacerSplitMode Mode)
{
	if (Class)
	{
		ClassRules.Add(Class.Get(), Mode);
	}
}

int32 USpawnPacerSubsystem::GetNumPending() const
{
	int32 Pending = Incoming.Num();
	for (const FSpawnPacerQueue& Queue : Queues)
	{
		Pending += Queue.Num();
	}
	return Pending;
}

void USpawnPacerSubsystem::HandlePostActorTick(UWorld* InWorld, ELevelTick TickType, float DeltaSeconds)
{
	// The delegate is global: every world's tick broadcasts it. Until the first request or batch, nothing runs and
	// nothing is published, so a world that never uses SpawnPacer (a baseline run) pays nothing and has no columns.
	if (bActive && InWorld == GetWorld())
	{
		RunFrame();
	}
}

FSpawnPacerRequest* USpawnPacerSubsystem::FindHead()
{
	for (int32 Level = SpawnPacer::NumPriorities - 1; Level >= 0; --Level)
	{
		FSpawnPacerQueue& Queue = Queues[Level];
		while (Queue.Head < Queue.Requests.Num())
		{
			FSpawnPacerRequest& Request = Queue.Requests[Queue.Head];
			if (Request.bCancelled)
			{
				const bool bSpawn = Request.Kind == ESpawnPacerRequestKind::Spawn;
				if (bSpawn && Request.NextStage == ESpawnPacerStage::Finish)
				{
					Request.NextStage = ESpawnPacerStage::Destroy; // half-spawned: cleaned up by a budgeted Destroy job
				}
				else if (!(bSpawn && Request.NextStage == ESpawnPacerStage::Destroy))
				{
					End(Request, EOutcome::Cancelled); // nothing to clean up (not started, or a release): no job, no cost
					++Queue.Head;
					continue;
				}
			}
			HeadQueue = &Queue;
			return &Request;
		}
	}
	HeadQueue = nullptr;
	return nullptr;
}

AActor* USpawnPacerSubsystem::Spawn(const FSpawnPacerRequest& Request, bool bDeferConstruction) const
{
	FActorSpawnParameters Parameters;
	Parameters.SpawnCollisionHandlingOverride = Request.CollisionHandling;
	Parameters.bDeferConstruction = bDeferConstruction;
	return GetWorld()->SpawnActor(Request.Class, &Request.Transform, Parameters);
}

bool USpawnPacerSubsystem::RunStage(FSpawnPacerRequest& Request)
{
	auto Finished = [this, &Request](AActor* Actor)
	{
		// Valid here = it survived its own BeginPlay; OnSpawned is the caller's first sight of it.
		if (IsValid(Actor))
		{
			if (Request.OnSpawned)
			{
				Request.OnSpawned(*Actor);
			}
			End(Request, EOutcome::Completed);
		}
		else
		{
			End(Request, EOutcome::Dropped);
		}
		return true;
	};

	switch (Request.NextStage)
	{
	case ESpawnPacerStage::Spawn:
	{
		// Whole. Without an Initialize callback this is exactly a plain SpawnActor. With one, the spawn is deferred and
		// finished in the same job: the same work, because a plain SpawnActor is PostSpawnInitialize + FinishSpawning
		// (Private/Actor.cpp:4357-4360), with the callback in between.
		AActor* Actor = Spawn(Request, static_cast<bool>(Request.Initialize));
		if (Actor && Request.Initialize)
		{
			Request.Initialize(*Actor);
			if (Request.bCancelled && IsValid(Actor))
			{
				Actor->Destroy(); // its batch was cancelled from inside Initialize: never finished
				End(Request, EOutcome::Cancelled);
				return true;
			}
			if (IsValid(Actor))
			{
				Actor->FinishSpawning(Request.Transform);
			}
		}
		return Finished(Actor); // a cancel during BeginPlay comes too late: the actor exists, so it completes
	}

	case ESpawnPacerStage::Construct:
	{
		AActor* Actor = Spawn(Request, true);
		if (!IsValid(Actor))
		{
			End(Request, EOutcome::Dropped);
			return true;
		}
		Request.Actor = Actor;
		if (Request.Initialize)
		{
			Request.Initialize(*Actor);
		}
		Request.NextStage = ESpawnPacerStage::Finish;
		return false; // stays at the head: its Finish job comes next (or, if cancelled meanwhile, a Destroy job)
	}

	case ESpawnPacerStage::Finish:
	{
		AActor* Actor = Request.Actor.Get();
		if (!IsValid(Actor))
		{
			End(Request, EOutcome::Dropped); // destroyed between its stages (02 section 8)
			return true;
		}
		Actor->FinishSpawning(Request.Transform);
		return Finished(Actor);
	}

	case ESpawnPacerStage::Destroy:
	{
		AActor* Actor = Request.Actor.Get();
		const bool bValid = IsValid(Actor);
		if (bValid)
		{
			Actor->Destroy();
		}
		// A spawn request only reaches Destroy by being cancelled half-spawned. A release whose actor was already
		// destroyed behind our back is dropped, like any stage that finds its actor gone (02 section 8).
		const bool bSpawn = Request.Kind == ESpawnPacerRequestKind::Spawn;
		End(Request, bSpawn ? EOutcome::Cancelled : (bValid ? EOutcome::Completed : EOutcome::Dropped));
		return true;
	}

	default:
		checkNoEntry(); // Register, Acquire and Release come with S6 and S7
		End(Request, EOutcome::Dropped);
		return true;
	}
}

void USpawnPacerSubsystem::End(const FSpawnPacerRequest& Request, EOutcome Outcome)
{
	switch (Outcome)
	{
	case EOutcome::Completed: ++Frame.Completed; ++Totals.Completed; break;
	case EOutcome::Dropped:   ++Frame.Dropped;   ++Totals.Dropped;   break;
	case EOutcome::Cancelled: ++Frame.Cancelled; ++Totals.Cancelled; break;
	}
	if (int32* Outstanding = OpenBatches.Find(Request.BatchId))
	{
		--*Outstanding;
	}
}

void USpawnPacerSubsystem::RunFrame()
{
	const uint64 FrameStart = FPlatformTime::Cycles64();

	// 1. Requests made since the last frame join their level, in request order.
	for (FSpawnPacerRequest& Request : Incoming)
	{
		Queues[static_cast<int32>(Request.Priority)].Requests.Add(MoveTemp(Request));
	}
	Incoming.Reset();

	// 2. The policy (S4): run the head job while it is predicted to fit. Each RunHead times exactly one stage.
	LastFrameStats = FSpawnPacerScheduler::RunFrame(BudgetMs, CostModel,
		[this]() -> TOptional<FSpawnCostKey>
		{
			const FSpawnPacerRequest* Head = FindHead();
			return Head ? TOptional<FSpawnCostKey>(FSpawnCostKey{ Head->Class.Get(), Head->NextStage, NAME_None }) : TOptional<FSpawnCostKey>();
		},
		[this]() -> double
		{
			// The head is still the request FindHead returned: nothing between the peek and this call moves the queues.
			FSpawnPacerQueue& Queue = *HeadQueue;
			FSpawnPacerRequest& Request = Queue.Requests[Queue.Head];
			const ESpawnPacerStage Stage = Request.NextStage;
			const uint64 Start = FPlatformTime::Cycles64();
			Running = &Request;
			const bool bEnded = RunStage(Request); // may call user code; new requests go to Incoming, never to a queue
			Running = nullptr;
			const double Ms = FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - Start);
			Frame.StageMs[static_cast<int32>(Stage)] += Ms;
			if (bEnded)
			{
				++Queue.Head;
			}
			return Ms;
		});

	// 3. Compact: drop ended requests at the front once they are most of a queue (amortised; no per-request shifting).
	for (FSpawnPacerQueue& Queue : Queues)
	{
		if (Queue.Head == Queue.Requests.Num())
		{
			Queue.Requests.Reset();
			Queue.Head = 0;
		}
		else if (Queue.Head >= SpawnPacer::CompactAfter && Queue.Head * 2 >= Queue.Requests.Num())
		{
			Queue.Requests.RemoveAt(0, Queue.Head, EAllowShrinking::No);
			Queue.Head = 0;
		}
	}
	HeadQueue = nullptr;

	// 4. Batches with nothing outstanding are complete. Collected first: a handler may create batches or requests.
	TArray<int32, TInlineAllocator<4>> Completed;
	for (const TPair<int32, int32>& Batch : OpenBatches)
	{
		if (Batch.Value <= 0)
		{
			Completed.Add(Batch.Key);
		}
	}
	for (const int32 BatchId : Completed)
	{
		OpenBatches.Remove(BatchId);
	}
	const double WorkMs = FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - FrameStart);

	// 5. Telemetry, once per frame (a CSV stat costs about 50 ns, measured by UE5-UpdateBudget), outside WorkMs.
	CSV_CUSTOM_STAT(SpawnPacer, WorkMs, WorkMs, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(SpawnPacer, PredictedMs, LastFrameStats.PredictedMs, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(SpawnPacer, Jobs, LastFrameStats.Jobs, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(SpawnPacer, ForcedJobs, LastFrameStats.ForcedJobs, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(SpawnPacer, ColdJobs, LastFrameStats.ColdJobs, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(SpawnPacer, Pending, GetNumPending(), ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(SpawnPacer, Completed, Frame.Completed, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(SpawnPacer, Dropped, Frame.Dropped, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(SpawnPacer, Cancelled, Frame.Cancelled, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(SpawnPacer, SpawnMs, Frame.StageMs[static_cast<int32>(ESpawnPacerStage::Spawn)], ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(SpawnPacer, ConstructMs, Frame.StageMs[static_cast<int32>(ESpawnPacerStage::Construct)], ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(SpawnPacer, FinishMs, Frame.StageMs[static_cast<int32>(ESpawnPacerStage::Finish)], ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(SpawnPacer, DestroyMs, Frame.StageMs[static_cast<int32>(ESpawnPacerStage::Destroy)], ECsvCustomStatOp::Set);

	// Reset after publishing, so what ends between two frames (a release taking over a half-spawned request) is counted
	// in the next frame's stats.
	Frame = FFrameCounters();

	for (const int32 BatchId : Completed)
	{
		CSV_EVENT(SpawnPacer, TEXT("BatchComplete %d"), BatchId);
		OnBatchComplete.Broadcast(BatchId);
	}
}
