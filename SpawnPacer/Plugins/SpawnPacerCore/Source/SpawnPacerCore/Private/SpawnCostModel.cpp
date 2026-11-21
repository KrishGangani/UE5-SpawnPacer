// SpawnPacerCore - FSpawnCostModel.

#include "SpawnCostModel.h"

const FSpawnCostModel::FEstimate* FSpawnCostModel::Lookup(const FSpawnCostKey& Key) const
{
	if (const FEstimate* Estimate = Estimates.Find(Key))
	{
		return Estimate;
	}
	return Key.Component.IsNone() ? nullptr : Estimates.Find(FamilyOf(Key));
}

double FSpawnCostModel::Predict(const FSpawnCostKey& Key, double ColdPredictionMs) const
{
	const FEstimate* Estimate = Lookup(Key);
	return Estimate ? Estimate->MeanMs + DeviationMultiplier * Estimate->DeviationMs : ColdPredictionMs;
}

void FSpawnCostModel::Observe(const FSpawnCostKey& Key, double MeasuredMs)
{
	ObserveOne(Key, MeasuredMs);
	if (!Key.Component.IsNone())
	{
		ObserveOne(FamilyOf(Key), MeasuredMs); // the family learns from every one of its components (S6)
	}
}

void FSpawnCostModel::ObserveOne(const FSpawnCostKey& Key, double MeasuredMs)
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
