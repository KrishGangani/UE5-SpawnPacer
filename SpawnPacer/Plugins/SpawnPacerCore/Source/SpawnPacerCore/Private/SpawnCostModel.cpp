// SpawnPacerCore - FSpawnCostModel.

#include "SpawnCostModel.h"

double FSpawnCostModel::Predict(const FSpawnCostKey& Key, double ColdPredictionMs) const
{
	const FEstimate* Estimate = Estimates.Find(Key);
	return Estimate ? Estimate->MeanMs + DeviationMultiplier * Estimate->DeviationMs : ColdPredictionMs;
}

void FSpawnCostModel::Observe(const FSpawnCostKey& Key, double MeasuredMs)
{
	FEstimate* Estimate = Estimates.Find(Key);
	if (!Estimate)
	{
		// RFC 6298 rule 2.2, first measurement R: SRTT <- R, RTTVAR <- R/2.
		Estimates.Add(Key, { MeasuredMs, 0.5 * MeasuredMs, 1 });
		return;
	}
	// Rule 2.3: the deviation is updated with the OLD mean, then the mean.
	Estimate->DeviationMs = (1.0 - Beta) * Estimate->DeviationMs + Beta * FMath::Abs(Estimate->MeanMs - MeasuredMs);
	Estimate->MeanMs = (1.0 - Alpha) * Estimate->MeanMs + Alpha * MeasuredMs;
	++Estimate->Samples;
}
