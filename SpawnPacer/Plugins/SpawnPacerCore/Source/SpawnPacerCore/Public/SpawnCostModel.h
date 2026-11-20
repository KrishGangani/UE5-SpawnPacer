// SpawnPacerCore - FSpawnCostModel: predicts how long one indivisible spawn stage will take.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectKey.h"

#define UE_API SPAWNPACERCORE_API

/** The indivisible pieces of work a request is made of (02_System_Architecture.md section 2). */
enum class ESpawnPacerStage : uint8
{
	Spawn,      // whole spawn: plain SpawnActor
	Construct,  // SpawnActor with bDeferConstruction: construction and native component registration
	Register,   // RegisterComponent of one held-back native component
	Finish,     // FinishSpawning: construction script, initialisation, BeginPlay
	Acquire,    // take a parked actor out of the pool
	Release,    // park an actor in the pool
	Destroy     // DestroyActor
};

/**
 * What one estimate is kept for: a class, a stage and, for Register, the component's name. Default subobjects have the
 * same name in every instance of a class, so the name identifies "the same component" across spawns.
 *
 * The class is a weak key (object index + serial number), not a raw pointer: a Blueprint class recompiled or unloaded
 * between PIE sessions must not hand its learned cost to a new class that happens to reuse its address.
 */
struct FSpawnCostKey
{
	TObjectKey<UClass> Class;
	ESpawnPacerStage Stage = ESpawnPacerStage::Spawn;
	FName Component;

	bool operator==(const FSpawnCostKey& Other) const
	{
		return Class == Other.Class && Stage == Other.Stage && Component == Other.Component;
	}

	friend uint32 GetTypeHash(const FSpawnCostKey& Key)
	{
		return HashCombine(HashCombine(GetTypeHash(Key.Class), GetTypeHash(static_cast<uint8>(Key.Stage))), GetTypeHash(Key.Component));
	}
};

/**
 * The cost of each key, learned while the game runs: a smoothed mean and a smoothed mean deviation, updated the way
 * RFC 6298 estimates a round-trip time (alpha 1/8, beta 1/4). A prediction is mean + K x deviation (K = 2 by default,
 * 02 section 7). A key never measured is "cold": it is predicted as the caller's cold value (the scheduler passes the
 * whole budget, and runs a cold job only as the first job of a frame).
 *
 * Plain C++, no UObject: the policy is unit-tested without a world (step S4).
 */
class UE_API FSpawnCostModel
{
public:
	struct FEstimate
	{
		double MeanMs = 0.0;
		double DeviationMs = 0.0;
		int32 Samples = 0;
	};

	/** RFC 6298 section 2: "SHOULD be computed using alpha=1/8 and beta=1/4". */
	static constexpr double Alpha = 1.0 / 8.0;
	static constexpr double Beta = 1.0 / 4.0;

	explicit FSpawnCostModel(double InDeviationMultiplier = 2.0)
		: DeviationMultiplier(InDeviationMultiplier)
	{
	}

	/** Predicted milliseconds for the key: mean + K x deviation, or ColdPredictionMs if it was never measured. */
	double Predict(const FSpawnCostKey& Key, double ColdPredictionMs) const;

	/** True if the key has never been measured (or seeded). */
	bool IsCold(const FSpawnCostKey& Key) const { return !Estimates.Contains(Key); }

	/** Learns one measurement. The first sets mean = R and deviation = R / 2; later ones update the deviation with the
	 *  old mean first, then the mean (RFC 6298 rules 2.2 and 2.3). */
	void Observe(const FSpawnCostKey& Key, double MeasuredMs);

	/** Sets an estimate directly: for tests, and for seeding from a saved table later (02 section 12). */
	void Seed(const FSpawnCostKey& Key, double MeanMs, double DeviationMs) { Estimates.Add(Key, { MeanMs, DeviationMs, 1 }); }

	/** The estimate of a key, or null if it is cold. */
	const FEstimate* Find(const FSpawnCostKey& Key) const { return Estimates.Find(Key); }

	double GetDeviationMultiplier() const { return DeviationMultiplier; }
	void SetDeviationMultiplier(double InDeviationMultiplier) { DeviationMultiplier = InDeviationMultiplier; }

private:
	TMap<FSpawnCostKey, FEstimate> Estimates;
	double DeviationMultiplier;
};

#undef UE_API
