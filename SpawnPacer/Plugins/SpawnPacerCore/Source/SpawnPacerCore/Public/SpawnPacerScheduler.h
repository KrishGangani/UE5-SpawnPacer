// SpawnPacerCore - FSpawnPacerScheduler: the admission rule. Never start a job that is predicted not to fit.

#pragma once

#include "CoreMinimal.h"
#include "SpawnCostModel.h"

#define UE_API SPAWNPACERCORE_API

/** What one frame of the scheduler did. Published once per frame as the SpawnPacer CSV stats (S5). */
struct FSpawnPacerFrameStats
{
	double SpentMs = 0.0;        // measured time of the jobs that ran
	double PredictedMs = 0.0;    // what the model predicted for them, before they ran
	int32 Jobs = 0;
	int32 ForcedJobs = 0;        // the frame's first job ran although it was predicted above the budget
	int32 ColdJobs = 0;          // jobs whose key had never been measured
	bool bStoppedOnBudget = false;   // a job was waiting, but its prediction didn't fit what was left
};

/**
 * The policy of 02 section 2, separate from the mechanism so it is testable with fake jobs and fake costs:
 *
 *   while there is a head job:
 *       predicted = model.Predict(head)                       (a cold key is predicted as the whole budget)
 *       if a job already ran this frame and (head is cold, or spent + predicted > budget): stop
 *                                                             (never start what won't fit; a cold job runs only first)
 *       run it (the frame's first job always runs, so a job larger than the budget can't block the queue forever;
 *               it is counted as forced)
 *       model.Observe(head, measured); spent += measured
 *
 * The scheduler doesn't know what a job is: the caller passes PeekHead (the head job's key, or unset when the queue is
 * empty) and RunHead (does one stage of the head request and returns the measured milliseconds).
 */
class UE_API FSpawnPacerScheduler
{
public:
	static FSpawnPacerFrameStats RunFrame(double BudgetMs, FSpawnCostModel& Model,
		TFunctionRef<TOptional<FSpawnCostKey>()> PeekHead, TFunctionRef<double()> RunHead);
};

#undef UE_API
