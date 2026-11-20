// P02 SpawnPacer (demo) - USPBenchmarkSubsystem.

#include "SPBenchmarkSubsystem.h"

#include "Camera/CameraActor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Math/RandomStream.h"
#include "Misc/CommandLine.h"
#include "Misc/Crc.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "SPWaveActor.h"
#include "Stats/Stats.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(SPBenchmarkSubsystem)

DEFINE_LOG_CATEGORY_STATIC(LogSPBench, Log, All);

// CSV category for the script's events: BenchStart, WaveRequested, WaveComplete, DespawnRequested, DespawnComplete,
// GCForced, BenchEnd. The analysis windows of 01 6.3 are cut at these events.
CSV_DEFINE_CATEGORY(SPBench, true);

namespace SPBench
{
	/** Share of each class in a wave (01 6.1): 30% crates, 19% grunts, 1% bosses (at least one), drones the rest
	 *  (250 / 150 / 95 / 5 in 500). Rounding goes to the cheapest class, so the heavy classes are never over-weighted. */
	constexpr int32 CratePercent = 30;
	constexpr int32 GruntPercent = 19;
	constexpr int32 BossPercent = 1;

	/** Upper bound on -SPWaveSize: far above the benchmark's 500, low enough to catch a typo. */
	constexpr int32 MaxWaveSize = 20000;

	/** Exit code of a run that can't start (invalid settings), so the batch script sees the failure at once. */
	constexpr uint8 InvalidRunExitCode = 2;

	/** The wave stands on a square grid in front of the camera: 2 m cells with up to 0.5 m jitter, 1 m above the floor. */
	constexpr double GridSpacing = 200.0;
	constexpr double GridJitter = 50.0;
	constexpr double WaveHeight = 100.0;
	const FVector WaveCenter(3000.0, 0.0, 0.0);

	/** A fixed camera 15 m behind the grid's centre and 25 m up, looking down at it. */
	const FVector CameraLocation(-1500.0, 0.0, 2500.0);
	const FRotator CameraRotation(-30.0, 0.0, 0.0);

	/** Frames to wait after BenchEnd before quitting, so the CSV profiler finishes writing its file. */
	constexpr int32 ExitDelayFrames = 3;
}

bool USPBenchmarkSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

USPBenchmarkSubsystem::FRunSettings USPBenchmarkSubsystem::ReadRunSettings()
{
	const TCHAR* CommandLine = FCommandLine::Get();
	FRunSettings Read;
	FParse::Value(CommandLine, TEXT("SPConfig="), Read.Config);
	FParse::Value(CommandLine, TEXT("SPCountCap="), Read.CountCap);
	FParse::Value(CommandLine, TEXT("SPBudgetMs="), Read.BudgetMs);
	FParse::Value(CommandLine, TEXT("SPWaveSize="), Read.WaveSize);
	FParse::Value(CommandLine, TEXT("SPWaves="), Read.Waves);
	FParse::Value(CommandLine, TEXT("SPSeed="), Read.Seed);
	FParse::Value(CommandLine, TEXT("SPWork="), Read.Work);
	FParse::Value(CommandLine, TEXT("SPWarmup="), Read.WarmupSeconds);
	FParse::Value(CommandLine, TEXT("SPCycle="), Read.CycleSeconds);
	FParse::Value(CommandLine, TEXT("SPLife="), Read.LifeSeconds);
	FParse::Value(CommandLine, TEXT("SPGCAfter="), Read.GCAfterSeconds);
	FParse::Value(CommandLine, TEXT("SPRunTag="), Read.RunTag);
	Read.bExitWhenDone = FParse::Param(CommandLine, TEXT("SPExit"));
	Read.bSpawnTimer = FParse::Param(CommandLine, TEXT("SPSpawnTimer"));
	// The tag names a file: keep it a plain file name, whatever the command line held.
	Read.RunTag = FPaths::MakeValidFileName(Read.RunTag, TEXT('_'));
	return Read;
}

TOptional<ESPBaseline> USPBenchmarkSubsystem::ParseBaseline(const FString& Config)
{
	if (Config == TEXT("B0")) { return ESPBaseline::B0; }
	if (Config == TEXT("B1")) { return ESPBaseline::B1; }
	if (Config == TEXT("B2")) { return ESPBaseline::B2; }
	if (Config == TEXT("B3")) { return ESPBaseline::B3; }
	return {};
}

bool USPBenchmarkSubsystem::ValidateRunSettings() const
{
	bool bValid = true;
	if (!ParseBaseline(Settings.Config).IsSet())
	{
		UE_LOG(LogSPBench, Error, TEXT("-SPConfig=%s is not a known configuration (B0, B1, B2, B3)."), *Settings.Config);
		bValid = false;
	}
	if (Settings.CountCap < 1 || Settings.BudgetMs <= 0.0)
	{
		UE_LOG(LogSPBench, Error, TEXT("Invalid limits: count cap %d (>= 1), budget %.3f ms (> 0)."), Settings.CountCap, Settings.BudgetMs);
		bValid = false;
	}
	if (Settings.WaveSize > SPBench::MaxWaveSize || Settings.Waves < 1 || Settings.WarmupSeconds < 0.0)
	{
		UE_LOG(LogSPBench, Error, TEXT("Invalid run: wave size %d (max %d), %d waves, warm-up %.3f s."),
			Settings.WaveSize, SPBench::MaxWaveSize, Settings.Waves, Settings.WarmupSeconds);
		bValid = false;
	}
	// Each step must come after the previous one, and the despawn and the GC must fall inside their own cycle, or the
	// windows of 01 6.3 would overlap.
	if (!(Settings.LifeSeconds > 0.0 && Settings.GCAfterSeconds >= 0.0 && Settings.LifeSeconds + Settings.GCAfterSeconds < Settings.CycleSeconds))
	{
		UE_LOG(LogSPBench, Error, TEXT("Invalid cycle: life %.3f s + GC after %.3f s must be inside the %.3f s cycle."),
			Settings.LifeSeconds, Settings.GCAfterSeconds, Settings.CycleSeconds);
		bValid = false;
	}
	return bValid;
}

void USPBenchmarkSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	Settings = ReadRunSettings();
	if (Settings.WaveSize <= 0)
	{
		return; // No workload requested: the world behaves like a normal empty map.
	}
	if (!ValidateRunSettings())
	{
		QuitRun(SPBench::InvalidRunExitCode); // a batch must not wait for a timeout to find out
		return;
	}

	// The enemies' set-up work is a console variable, so one value applies to every brain, whoever spawns it.
	IConsoleVariable* WorkVariable = IConsoleManager::Get().FindConsoleVariable(TEXT("sp.Demo.BeginPlayWork"));
	if (WorkVariable && Settings.Work >= 0)
	{
		WorkVariable->Set(Settings.Work, ECVF_SetByCommandline);
	}
	const int32 EffectiveWork = WorkVariable ? WorkVariable->GetInt() : -1;

	WaveHash = BuildWave();
	Baseline = NewObject<USPBaselineSpawner>(this);
	if (const TOptional<ESPBaseline> Mode = ParseBaseline(Settings.Config))
	{
		Baseline->Configure(*Mode, Settings.CountCap, Settings.BudgetMs);
		bPoolToFill = (*Mode == ESPBaseline::B3);
	}
	Camera = InWorld.SpawnActor<ACameraActor>(SPBench::CameraLocation, SPBench::CameraRotation);
	BeginPlayTime = InWorld.GetTimeSeconds();
	Phase = ERunPhase::WarmUp;

	UE_LOG(LogSPBench, Display, TEXT("Config %s: %d waves of %d actors (seed %d, work %d), wave hash 0x%08X"),
		*Settings.Config, Settings.Waves, Wave.Num(), Settings.Seed, EffectiveWork, WaveHash);

	// The run's description goes into the CSV itself, so every capture is self-describing.
	CSV_METADATA(TEXT("SPRunTag"), *Settings.RunTag);
	CSV_METADATA(TEXT("SPConfig"), *Settings.Config);
	CSV_METADATA(TEXT("SPCountCap"), *FString::FromInt(Settings.CountCap));
	CSV_METADATA(TEXT("SPBudgetMs"), *FString::SanitizeFloat(Settings.BudgetMs));
	CSV_METADATA(TEXT("SPWaveSize"), *FString::FromInt(Wave.Num()));
	CSV_METADATA(TEXT("SPWaves"), *FString::FromInt(Settings.Waves));
	CSV_METADATA(TEXT("SPSeed"), *FString::FromInt(Settings.Seed));
	CSV_METADATA(TEXT("SPWork"), *FString::FromInt(EffectiveWork));
	CSV_METADATA(TEXT("SPWaveHash"), *FString::Printf(TEXT("0x%08X"), WaveHash));
	CSV_METADATA(TEXT("SPCycle"), *FString::SanitizeFloat(Settings.CycleSeconds));
	CSV_METADATA(TEXT("SPLife"), *FString::SanitizeFloat(Settings.LifeSeconds));
	CSV_METADATA(TEXT("SPGCAfter"), *FString::SanitizeFloat(Settings.GCAfterSeconds));
}

uint32 USPBenchmarkSubsystem::BuildWave()
{
	const int32 Count = Settings.WaveSize;
	const int32 Crates = Count * SPBench::CratePercent / 100;
	const int32 Grunts = Count * SPBench::GruntPercent / 100;
	const int32 Bosses = FMath::Min(Count - Crates - Grunts, FMath::Max(1, Count * SPBench::BossPercent / 100));
	const int32 Drones = Count - Crates - Grunts - Bosses;

	Wave.Reset(Count);
	auto AddEntries = [this](TSubclassOf<ASPWaveActor> Class, int32 Number)
	{
		for (int32 Index = 0; Index < Number; ++Index)
		{
			Wave.AddDefaulted_GetRef().Class = Class; // the transform is set below, after the shuffle
		}
	};
	AddEntries(ASPDrone::StaticClass(), Drones);
	AddEntries(ASPCrate::StaticClass(), Crates);
	AddEntries(ASPGrunt::StaticClass(), Grunts);
	AddEntries(ASPBoss::StaticClass(), Bosses);

	// Fisher-Yates shuffle with the seeded stream: the same seed always gives the same class order.
	FRandomStream Random(Settings.Seed);
	for (int32 Index = Wave.Num() - 1; Index > 0; --Index)
	{
		Wave.Swap(Index, Random.RandRange(0, Index));
	}

	// Columns along Y, rows along X; the last row may be partly filled, so rows and columns are centred separately.
	const int32 Columns = FMath::CeilToInt32(FMath::Sqrt(static_cast<double>(Count)));
	const int32 Rows = (Count + Columns - 1) / Columns;
	const double HalfX = 0.5 * SPBench::GridSpacing * (Rows - 1);
	const double HalfY = 0.5 * SPBench::GridSpacing * (Columns - 1);
	uint32 Hash = 0;
	for (int32 Index = 0; Index < Wave.Num(); ++Index)
	{
		const FVector Location(
			SPBench::WaveCenter.X + (Index / Columns) * SPBench::GridSpacing - HalfX + Random.FRandRange(-SPBench::GridJitter, SPBench::GridJitter),
			SPBench::WaveCenter.Y + (Index % Columns) * SPBench::GridSpacing - HalfY + Random.FRandRange(-SPBench::GridJitter, SPBench::GridJitter),
			SPBench::WaveHeight);
		const FRotator Rotation(0.0, Random.FRandRange(0.0, 360.0), 0.0);
		Wave[Index].Transform = FTransform(Rotation, Location);

		// Running CRC over class and placement: chaining equals one CRC over the whole list, without a buffer.
		Hash = FCrc::StrCrc32(*Wave[Index].Class->GetName(), Hash);
		Hash = FCrc::TypeCrc32(FVector3f(Location), Hash);
		Hash = FCrc::TypeCrc32(static_cast<float>(Rotation.Yaw), Hash);
	}
	return Hash;
}

void USPBenchmarkSubsystem::UpdateCamera()
{
	// Re-applied every frame: a pawn possessed later would otherwise take the view back.
	APlayerController* PlayerController = GetWorld()->GetFirstPlayerController();
	if (Camera && PlayerController && PlayerController->GetViewTarget() != Camera)
	{
		PlayerController->SetViewTarget(Camera);
	}
}

void USPBenchmarkSubsystem::StepScript(double SecondsInCapture)
{
	const double InCycle = SecondsInCapture - CurrentCycle * Settings.CycleSeconds;
	const uint64 Frame = GFrameCounter;
	const double Now = GetWorld()->GetTimeSeconds();

	switch (NextStep)
	{
	case ECycleStep::RequestWave:
		if (InCycle >= 0.0)
		{
			FWaveRecord& Record = WaveRecords.AddDefaulted_GetRef();
			Record.RequestFrame = Frame;
			Record.RequestTime = Now;
			ReadyThisWave = 0;
			Baseline->RequestWave(Wave);
			CSV_EVENT(SPBench, TEXT("WaveRequested"));
			NextStep = ECycleStep::Despawn;
		}
		break;

	case ECycleStep::Despawn:
		if (InCycle >= Settings.LifeSeconds)
		{
			WaveRecords.Last().DespawnRequestFrame = Frame;
			Baseline->RequestDespawn();
			CSV_EVENT(SPBench, TEXT("DespawnRequested"));
			NextStep = ECycleStep::ForceGC;
		}
		break;

	case ECycleStep::ForceGC:
		if (InCycle >= Settings.LifeSeconds + Settings.GCAfterSeconds)
		{
			// Arms a full purge; the engine runs it at its next garbage-collection check (end of this world tick).
			WaveRecords.Last().GCFrame = Frame;
			GEngine->ForceGarbageCollection(true);
			CSV_EVENT(SPBench, TEXT("GCForced"));
			NextStep = ECycleStep::CycleDone;
		}
		break;

	case ECycleStep::CycleDone:
		if (InCycle >= Settings.CycleSeconds)
		{
			++CurrentCycle;
			NextStep = ECycleStep::RequestWave;
		}
		break;
	}
}

void USPBenchmarkSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (Phase == ERunPhase::Idle)
	{
		return;
	}

	UWorld* World = GetWorld();
	const double Now = World->GetTimeSeconds();
	UpdateCamera();

	switch (Phase)
	{
	case ERunPhase::WarmUp:
		if (bPoolToFill)
		{
			// B3's pool is filled on the first warm-up frame, when the world has begun play: each actor's BeginPlay runs
			// inside its SpawnActor, so parking really comes after it (filled earlier, in OnWorldBeginPlay, BeginPlay would
			// run later and restart the parked enemies' think timers). The capture can start on the next frame at the
			// earliest, so the fill's hitch never lands in it, whatever the warm-up length.
			bPoolToFill = false;
			Baseline->FillPool(*World, Wave);
			break;
		}
		if (Now - BeginPlayTime >= Settings.WarmupSeconds)
		{
			BenchStartTime = Now;
			Phase = ERunPhase::Capture;
			CSV_EVENT(SPBench, TEXT("BenchStart"));
			if (Settings.bSpawnTimer)
			{
				// The engine's own per-class spawn timer (non-Shipping, 01b 4): a cross-check of our per-class costs.
				GEngine->Exec(World, TEXT("spawnactortimer start"));
			}
			UE_LOG(LogSPBench, Display, TEXT("BenchStart at %.3f s"), Now);
		}
		break;

	case ERunPhase::Capture:
		if (CurrentCycle >= Settings.Waves)
		{
			// The stop request is processed at the end of this frame; the report is written next frame, outside the capture.
			CSV_EVENT(SPBench, TEXT("BenchEnd"));
			if (Settings.bSpawnTimer)
			{
				GEngine->Exec(World, TEXT("spawnactortimer stop")); // logs "<class>,<average s>,<total s>,<count>" lines
			}
			Phase = ERunPhase::Done;
			FramesSinceEnd = 0;
			UE_LOG(LogSPBench, Display, TEXT("BenchEnd after %d waves"), Settings.Waves);
			break;
		}
		StepScript(Now - BenchStartTime);
		break;

	case ERunPhase::Done:
		++FramesSinceEnd;
		if (FramesSinceEnd == 1)
		{
			WriteRunReport();
		}
		if (FramesSinceEnd == SPBench::ExitDelayFrames)
		{
			QuitRun(0);
		}
		break;

	default:
		break;
	}

	// The spawner works in the same frame as the request that triggered it (SPBaselineSpawner.h).
	Baseline->RunFrame(*World);

	// Completion is checked after the spawner ran, so a wave finished this frame is reported this frame.
	if (!WaveRecords.IsEmpty())
	{
		FWaveRecord& Record = WaveRecords.Last();
		Record.Ready = ReadyThisWave; // kept current, so an incomplete wave still reports how many actors became ready
		if (Record.CompleteTime < 0.0 && ReadyThisWave >= Wave.Num())
		{
			Record.CompleteFrame = GFrameCounter;
			Record.CompleteTime = Now;
			CSV_EVENT(SPBench, TEXT("WaveComplete"));
		}
		if (Record.DespawnRequestFrame != 0 && Record.DespawnCompleteFrame == 0 && Baseline->IsIdle() && Baseline->GetNumLive() == 0)
		{
			Record.DespawnCompleteFrame = GFrameCounter;
			CSV_EVENT(SPBench, TEXT("DespawnComplete"));
		}
	}
}

void USPBenchmarkSubsystem::WriteRunReport() const
{
	FString Report = FString::Printf(TEXT("# run=%s config=%s wavesize=%d waves=%d seed=%d wavehash=0x%08X cycle=%.3f life=%.3f gcafter=%.3f\n"),
		*Settings.RunTag, *Settings.Config, Wave.Num(), Settings.Waves, Settings.Seed, WaveHash,
		Settings.CycleSeconds, Settings.LifeSeconds, Settings.GCAfterSeconds);
	Report += TEXT("wave,request_frame,complete_frame,latency_frames,latency_ms,ready,despawn_request_frame,despawn_complete_frame,gc_frame\n");

	for (int32 Index = 0; Index < WaveRecords.Num(); ++Index)
	{
		const FWaveRecord& Record = WaveRecords[Index];
		const bool bComplete = Record.CompleteTime >= 0.0;
		Report += FString::Printf(TEXT("%d,%llu,%llu,%lld,%.3f,%d,%llu,%llu,%llu\n"), Index,
			Record.RequestFrame, Record.CompleteFrame,
			bComplete ? static_cast<int64>(Record.CompleteFrame - Record.RequestFrame) : -1LL,
			bComplete ? (Record.CompleteTime - Record.RequestTime) * 1000.0 : -1.0,
			Record.Ready, Record.DespawnRequestFrame, Record.DespawnCompleteFrame, Record.GCFrame);
	}

	const FString Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SPRuns"), Settings.RunTag + TEXT(".csv"));
	if (FFileHelper::SaveStringToFile(Report, *Path))
	{
		UE_LOG(LogSPBench, Display, TEXT("Run report written: %s"), *FPaths::ConvertRelativePathToFull(Path));
	}
	else
	{
		UE_LOG(LogSPBench, Error, TEXT("Could not write the run report to %s"), *Path);
	}
}

void USPBenchmarkSubsystem::QuitRun(uint8 ExitCode)
{
	if (Settings.bExitWhenDone && GetWorld()->WorldType == EWorldType::Game)
	{
		Settings.bExitWhenDone = false; // request once
		// A clean exit (Force = false) only posts a quit message on Windows, and the process then returns the engine's
		// own error level, so a failure code would be lost (09 S1). A failure therefore forces the exit: the engine
		// flushes the log first and terminates with ExitCode. A finished run exits cleanly with 0.
		const bool bForce = ExitCode != 0;
		FPlatformMisc::RequestExitWithStatus(bForce, ExitCode, TEXT("USPBenchmarkSubsystem"));
	}
}

TStatId USPBenchmarkSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USPBenchmarkSubsystem, STATGROUP_Tickables);
}
