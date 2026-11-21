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

/** Number of stages, for arrays indexed by stage. */
inline constexpr int32 NumSpawnPacerStages = static_cast<int32>(ESpawnPacerStage::Destroy) + 1;

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
 * Families (S6): a key with a component name also teaches its family, (class, stage, no name), which every component
 * of that actor class and stage feeds. A component never measured itself is predicted from its family and is not cold
 * once the family has a sample. Otherwise a boss's 23 held-back parts would be 23 cold jobs, each alone in a frame,
 * holding the head of its queue for 23 frames; the family's deviation carries the spread between its components. Only
 * scene components are held back (SpawnPacerSubsystem.cpp), so a family holds comparable jobs.
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

	/** The family of a key: the same class and stage, without a component name. */
	static FSpawnCostKey FamilyOf(const FSpawnCostKey& Key) { return { Key.Class, Key.Stage, NAME_None }; }

	/** Predicted milliseconds for the key: mean + K x deviation of its own estimate, else of its family's (a key with a
	 *  component name), else ColdPredictionMs. */
	double Predict(const FSpawnCostKey& Key, double ColdPredictionMs) const;

	/** True if neither the key nor (for a key with a component name) its family has been measured or seeded. */
	bool IsCold(const FSpawnCostKey& Key) const { return Lookup(Key) == nullptr; }

	/** Learns one measurement, for the key and, if it has a component name, for its family. The first sets
	 *  mean = R and deviation = R / 2; later ones update the deviation with the old mean first, then the mean
	 *  (RFC 6298 rules 2.2 and 2.3). */
	void Observe(const FSpawnCostKey& Key, double MeasuredMs);

	/** Sets an estimate directly: for tests, and for seeding from a saved table later (02 section 12). */
	void Seed(const FSpawnCostKey& Key, double MeanMs, double DeviationMs) { Estimates.Add(Key, { MeanMs, DeviationMs, 1 }); }

	/** The key's own estimate (not its family's), or null if it has none. */
	const FEstimate* Find(const FSpawnCostKey& Key) const { return Estimates.Find(Key); }

	double GetDeviationMultiplier() const { return DeviationMultiplier; }
	void SetDeviationMultiplier(double InDeviationMultiplier) { DeviationMultiplier = InDeviationMultiplier; }

private:
	/** The key's own estimate, else its family's (keys with a component name only), else null. */
	const FEstimate* Lookup(const FSpawnCostKey& Key) const;

	void ObserveOne(const FSpawnCostKey& Key, double MeasuredMs);

	TMap<FSpawnCostKey, FEstimate> Estimates;
	double DeviationMultiplier;
};

#undef UE_API
