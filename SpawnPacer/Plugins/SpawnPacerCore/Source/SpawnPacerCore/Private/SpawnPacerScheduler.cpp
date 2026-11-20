// SpawnPacerCore - FSpawnPacerScheduler.

#include "SpawnPacerScheduler.h"

FSpawnPacerFrameStats FSpawnPacerScheduler::RunFrame(double BudgetMs, FSpawnCostModel& Model,
	TFunctionRef<TOptional<FSpawnCostKey>()> PeekHead, TFunctionRef<double()> RunHead)
{
	FSpawnPacerFrameStats Stats;
	// The key is copied out of the queue (TOptional by value): RunHead may advance or reallocate the queue.
	while (const TOptional<FSpawnCostKey> Head = PeekHead())
	{
		const bool bCold = Model.IsCold(*Head);
		const double Predicted = Model.Predict(*Head, BudgetMs);
		// A cold job is refused explicitly, not only through its prediction: after a first job that measured ~0 ms,
		// spent + budget > budget would be false and an unknown job would run second.
		if (Stats.Jobs > 0 && (bCold || Stats.SpentMs + Predicted > BudgetMs))
		{
			Stats.bStoppedOnBudget = true; // the job stays at the head for the next frame
			break;
		}
		Stats.ForcedJobs += (Stats.Jobs == 0 && Predicted > BudgetMs) ? 1 : 0;
		Stats.ColdJobs += bCold ? 1 : 0;

		const double Measured = RunHead();
		Model.Observe(*Head, Measured);

		Stats.SpentMs += Measured;
		Stats.PredictedMs += Predicted;
		++Stats.Jobs;
	}
	return Stats;
}
