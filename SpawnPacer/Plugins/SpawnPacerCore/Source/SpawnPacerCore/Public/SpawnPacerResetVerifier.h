// SpawnPacerCore - FSpawnPacerResetVerifier: proves a reused actor is back to its fresh state (02 section 11, SC5).

#pragma once

#include "CoreMinimal.h"

#define UE_API SPAWNPACERCORE_API

class AActor;

/** The six seeded defect kinds of SC5, plus latent actions; a property that is a delegate counts as Delegate. */
enum class ESpawnPacerLeakKind : uint8
{
	ActorProperty,      // an actor UPROPERTY differs from its fresh value
	ComponentProperty,  // a component UPROPERTY differs
	ComponentSet,       // a component was added (or removed) during use and left
	Attachment,         // an actor was left attached
	Delegate,           // a dynamic multicast delegate was left bound (its exported invocation list differs)
	Tick,               // a component's tick was left in a different state
	LatentAction,       // a latent action survived (the pool clears them, so this should never happen)
	Num
};

/** One difference between a reused actor and its fresh snapshot. */
struct FSpawnPacerLeak
{
	ESpawnPacerLeakKind Kind = ESpawnPacerLeakKind::ActorProperty;
	FString Where;   // "Actor.Property", "Component.Property", a component name, ...
	FString Fresh;
	FString Now;
};

/** What the verifier compares: an actor and its components, as text, plus structure (02 section 11). */
struct FSpawnPacerSnapshot
{
	/** "<object name>.<property name>" -> the property's exported text, for the actor (as "Actor") and each component. */
	TMap<FString, FString> Properties;

	/** The names of the properties that are delegates, so their differences are classed as Delegate. */
	TSet<FString> DelegateProperties;

	/** Component name -> class name. */
	TMap<FName, FName> Components;

	/** Component name -> tick enabled. */
	TMap<FName, bool> TickEnabled;

	int32 AttachedActors = 0;
	int32 LatentActions = 0;
};

/**
 * Snapshots an actor the moment it is fresh (right after its first FinishSpawning) and compares it after every reuse.
 * Plain C++: it reads reflection (FProperty::ExportTextItem_InContainer, 01b #13), so it sees every UPROPERTY of the
 * actor and its components, including dynamic delegates' bindings, but no plain C++ member (02 section 13).
 *
 * Ignored on purpose: the transform properties every reuse changes (RelativeLocation, RelativeRotation,
 * ComponentVelocity), the names a class rule lists as per-spawn parameters, and the engine bookkeeping listed in
 * IgnoredByDefault() with a reason each (found by the clean-class soak, SC5's false-positive test).
 */
class UE_API FSpawnPacerResetVerifier
{
public:
	/** Property names never compared, each with its reason in the .cpp. */
	static const TSet<FName>& IgnoredByDefault();

	/** Captures Actor now. ExtraIgnored: the class's per-spawn parameters. */
	static FSpawnPacerSnapshot Capture(const AActor& Actor, const TSet<FName>& ExtraIgnored);

	/** Appends every difference between Fresh and Now to OutLeaks; returns how many it found. */
	static int32 Compare(const FSpawnPacerSnapshot& Fresh, const FSpawnPacerSnapshot& Now, TArray<FSpawnPacerLeak>& OutLeaks);

	static const TCHAR* KindName(ESpawnPacerLeakKind Kind);
};

#undef UE_API
