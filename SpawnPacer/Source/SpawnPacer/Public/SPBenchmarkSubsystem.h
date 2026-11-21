// P02 SpawnPacer (demo) - benchmark director: builds the seeded wave and runs one timed, repeatable wave script.

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "SPBaselineSpawner.h"
#include "SPBenchmarkSubsystem.generated.h"

class ACameraActor;
class ASPWaveActor;
class USpawnPacerSubsystem;

/**
 * Runs the benchmark for one game world (01_Problem_Analysis.md 6.1). A world subsystem rather than a placed actor,
 * because the project has no hand-authored map (nothing would place a director), and a subsystem has exactly the
 * world's lifetime. Tickable, because it advances the script and drives the spawner every frame.
 *
 * Timeline (world time; fixed step, so every configuration simulates the same frames):
 *   [0, Warmup)                  warm-up: nothing measured
 *   Warmup                       CSV_EVENT BenchStart (capture starts with -csvStartOnEvent=BenchStart)
 *   per cycle c (Cycle s each):  t = 0        WaveRequested: one batch of WaveSize actors
 *                                as soon as   WaveComplete: every actor of the wave has finished BeginPlay
 *                                t = Life     DespawnRequested
 *                                as soon as   DespawnComplete: no actor of the wave is left
 *                                t = Life+GC  GCForced: one full garbage collection, the same in every configuration
 *   after Waves cycles           CSV_EVENT BenchEnd (capture stops, -csvStopOnEvent=BenchEnd)
 *   next frame                   the run report is written; with -SPExit the game quits a few frames later
 *
 * Command line (all optional):
 *   -SPConfig=B0      the configuration: B0, B1, B2, B3 (01 6.2), or SPS (SpawnPacer, spawn path; 02 9)
 *   -SPCountCap=K     B1: actors per frame (default 25; the batch script passes the calibrated value)
 *   -SPBudgetMs=MS    B2's time cap and SpawnPacer's budget: the same value for both (default 2.0)
 *   -SPBossSplit=M    SpawnPacer: the boss's split mode, Whole, Deferred or PerComponent (default PerComponent; the
 *                     split ablation of S6 compares them)
 *   -SPWaveSize=N     actors per wave (default 0: no workload, a normal empty map)
 *   -SPWaves=N        wave cycles in the capture (default 3)
 *   -SPSeed=S         seed for the wave's class order and positions (default 1)
 *   -SPWork=K         sp.Demo.BeginPlayWork, the enemies' set-up iterations (default: the cvar's value)
 *   -SPWarmup=SEC     warm-up seconds (default 5)
 *   -SPCycle=SEC      seconds per wave cycle (default 12)
 *   -SPLife=SEC       seconds from a wave request to its despawn (default 6)
 *   -SPGCAfter=SEC    seconds from the despawn request to the forced GC (default 2)
 *   -SPRunTag=NAME    run report file name and CSV metadata tag (default "run"; made a valid file name)
 *   -SPSpawnTimer     run the engine's spawnactortimer over the capture: per-class average spawn time in the log
 *   -SPExit           quit when the run is finished (Game worlds only: in PIE it would close the editor);
 *                     a run whose settings are invalid quits at once with exit code 2
 */
UCLASS()
class USPBenchmarkSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin UWorldSubsystem
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	//~ End UWorldSubsystem

	//~ Begin FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	//~ End FTickableGameObject

	/** Called by every wave actor at the very end of its BeginPlay (ASPWaveActor::BeginPlay), and by a pool that reuses
	 *  one. Completes the wave in the frame its last actor became ready, whoever spawned it and whenever in the frame. */
	void NotifyActorReady();

protected:
	//~ Begin UWorldSubsystem
	/** Game and PIE worlds only (the default would also create it in editor worlds). */
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	//~ End UWorldSubsystem

private:
	/** Values read once from the command line. */
	struct FRunSettings
	{
		FString Config = TEXT("B0");
		int32 CountCap = 25;
		double BudgetMs = 2.0;
		FString BossSplit = TEXT("PerComponent");
		int32 WaveSize = 0;
		int32 Waves = 3;
		int32 Seed = 1;
		int32 Work = -1;
		double WarmupSeconds = 5.0;
		double CycleSeconds = 12.0;
		double LifeSeconds = 6.0;
		double GCAfterSeconds = 2.0;
		FString RunTag = TEXT("run");
		bool bExitWhenDone = false;
		bool bSpawnTimer = false;
	};

	enum class ERunPhase : uint8
	{
		Idle,      // no workload requested
		WarmUp,
		Capture,
		Done
	};

	/** The next scripted step inside the current wave cycle. */
	enum class ECycleStep : uint8
	{
		RequestWave,
		Despawn,
		ForceGC,
		CycleDone
	};

	/** What happened to one wave, for the run report. Frame numbers are GFrameCounter values. */
	struct FWaveRecord
	{
		uint64 RequestFrame = 0;
		uint64 CompleteFrame = 0;
		double RequestTime = 0.0;
		double CompleteTime = -1.0;
		uint64 DespawnRequestFrame = 0;
		uint64 DespawnCompleteFrame = 0;
		uint64 GCFrame = 0;
		int32 Ready = 0;

		/** SPS: this cycle's SpawnPacer batches, INDEX_NONE once complete (or never made). */
		int32 PacerWaveBatch = INDEX_NONE;
		int32 PacerDespawnBatch = INDEX_NONE;
	};

	static FRunSettings ReadRunSettings();

	/** Maps -SPConfig to a baseline; unset for an unknown name and for the SpawnPacer configurations. */
	static TOptional<ESPBaseline> ParseBaseline(const FString& Config);

	/** True for the SpawnPacer configurations (SPS; SPP from S7). */
	static bool IsPacerConfig(const FString& Config);

	/** SPS: the wave as one SpawnPacer batch, with the same spawn parameters as the baselines. */
	void RequestPacedWave(FWaveRecord& Record);

	/** SPS: cancels what the wave batch hasn't spawned yet and releases every live wave actor, as one batch. */
	void RequestPacedDespawn(FWaveRecord& Record);

	/** SPS: a cycle's despawn is complete when both of its batches have completed (in either order). */
	void HandleBatchComplete(int32 BatchId);

	/** Logs settings that would silently produce a misleading run. Returns false if the run can't go ahead. */
	bool ValidateRunSettings() const;

	/** Builds the wave list from the seed (class counts fixed by 01 6.1, order shuffled, grid positions jittered).
	 *  Returns a hash of the list, so two runs can prove they used the same wave. */
	uint32 BuildWave();

	/** Advances the wave script for this frame (called from Tick during the capture). */
	void StepScript(double SecondsInCapture);

	/** Places the fixed camera and keeps it as the view target. */
	void UpdateCamera();

	/** Writes one line per wave to Saved/SPRuns/<RunTag>.csv. */
	void WriteRunReport() const;

	/** Quits the game with ExitCode if -SPExit was given, in Game worlds only (in PIE it would close the editor).
	 *  0 exits cleanly; any other code forces the exit so the code reaches the process (Windows drops it otherwise). */
	void QuitRun(uint8 ExitCode);

	FRunSettings Settings;
	ERunPhase Phase = ERunPhase::Idle;
	double BeginPlayTime = 0.0;
	double BenchStartTime = 0.0;
	uint32 WaveHash = 0;

	int32 CurrentCycle = 0;
	ECycleStep NextStep = ECycleStep::RequestWave;
	int32 ReadyThisWave = 0;
	TArray<FWaveRecord> WaveRecords;
	int32 FramesSinceEnd = 0;
	bool bPoolToFill = false;   // B3: fill the pool on the first warm-up frame

	/** The seeded wave: the same list is requested in every cycle and every configuration. */
	UPROPERTY(Transient)
	TArray<FSPWaveEntry> Wave;

	/** B0-B3 only; null in the SpawnPacer configurations. */
	UPROPERTY(Transient)
	TObjectPtr<USPBaselineSpawner> Baseline;

	/** SPS only: the world's SpawnPacer subsystem. */
	UPROPERTY(Transient)
	TObjectPtr<USpawnPacerSubsystem> Pacer;

	/** SPS: the wave actors SpawnPacer has spawned in this cycle (weak: the despawn may find some gone). */
	TArray<TWeakObjectPtr<ASPWaveActor>> PacedLive;

	UPROPERTY(Transient)
	TObjectPtr<ACameraActor> Camera;
};
