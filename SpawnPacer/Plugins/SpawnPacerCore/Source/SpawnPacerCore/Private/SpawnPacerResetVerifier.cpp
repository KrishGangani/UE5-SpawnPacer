// SpawnPacerCore - FSpawnPacerResetVerifier.

#include "SpawnPacerResetVerifier.h"

#include "Components/ActorComponent.h"
#include "Engine/LatentActionManager.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "UObject/UnrealType.h"

const TSet<FName>& FSpawnPacerResetVerifier::IgnoredByDefault()
{
	static const TSet<FName> Ignored = {
		// Every reuse moves the actor: its transform is a per-spawn parameter, not state (02 section 11).
		TEXT("RelativeLocation"),
		TEXT("RelativeRotation"),
		TEXT("ComponentVelocity"),
	};
	return Ignored;
}

namespace SpawnPacerVerifier
{
	void AddObject(FSpawnPacerSnapshot& Snapshot, const UObject& Object, const FString& Prefix, const TSet<FName>& ExtraIgnored)
	{
		const TSet<FName>& Ignored = FSpawnPacerResetVerifier::IgnoredByDefault();
		for (TFieldIterator<FProperty> It(Object.GetClass()); It; ++It)
		{
			const FProperty* Property = *It;
			const FName Name = Property->GetFName();
			if (Ignored.Contains(Name) || ExtraIgnored.Contains(Name))
			{
				continue;
			}
			FString Text;
			Property->ExportTextItem_InContainer(Text, &Object, nullptr, nullptr, PPF_None);
			FString Key = Prefix + TEXT(".") + Property->GetName();
			if (Property->IsA<FMulticastDelegateProperty>())
			{
				Snapshot.DelegateProperties.Add(Key);
			}
			Snapshot.Properties.Add(MoveTemp(Key), MoveTemp(Text));
		}
	}
}

FSpawnPacerSnapshot FSpawnPacerResetVerifier::Capture(const AActor& Actor, const TSet<FName>& ExtraIgnored)
{
	FSpawnPacerSnapshot Snapshot;
	SpawnPacerVerifier::AddObject(Snapshot, Actor, TEXT("Actor"), ExtraIgnored);

	TInlineComponentArray<UActorComponent*> Components(&Actor);
	for (const UActorComponent* Component : Components)
	{
		const FName Name = Component->GetFName();
		Snapshot.Components.Add(Name, Component->GetClass()->GetFName());
		Snapshot.TickEnabled.Add(Name, Component->IsComponentTickEnabled());
		SpawnPacerVerifier::AddObject(Snapshot, *Component, Name.ToString(), ExtraIgnored);
	}

	TArray<AActor*> Attached;
	Actor.GetAttachedActors(Attached);
	Snapshot.AttachedActors = Attached.Num();

	if (UWorld* World = Actor.GetWorld())
	{
		FLatentActionManager& Latent = World->GetLatentActionManager();
		Snapshot.LatentActions = Latent.GetNumActionsForObject(const_cast<AActor*>(&Actor));
		for (const UActorComponent* Component : Components)
		{
			Snapshot.LatentActions += Latent.GetNumActionsForObject(const_cast<UActorComponent*>(Component));
		}
	}
	return Snapshot;
}

int32 FSpawnPacerResetVerifier::Compare(const FSpawnPacerSnapshot& Fresh, const FSpawnPacerSnapshot& Now, TArray<FSpawnPacerLeak>& OutLeaks)
{
	const int32 Before = OutLeaks.Num();
	auto Leak = [&OutLeaks](ESpawnPacerLeakKind Kind, FString Where, FString FreshValue, FString NowValue)
	{
		OutLeaks.Add({ Kind, MoveTemp(Where), MoveTemp(FreshValue), MoveTemp(NowValue) });
	};

	// Structure first: a component added or removed is one leak, and its properties aren't compared one by one.
	for (const TPair<FName, FName>& Component : Now.Components)
	{
		if (!Fresh.Components.Contains(Component.Key))
		{
			Leak(ESpawnPacerLeakKind::ComponentSet, Component.Key.ToString(), TEXT("(none)"), Component.Value.ToString());
		}
	}
	for (const TPair<FName, FName>& Component : Fresh.Components)
	{
		if (!Now.Components.Contains(Component.Key))
		{
			Leak(ESpawnPacerLeakKind::ComponentSet, Component.Key.ToString(), Component.Value.ToString(), TEXT("(none)"));
		}
	}
	if (Now.AttachedActors != Fresh.AttachedActors)
	{
		Leak(ESpawnPacerLeakKind::Attachment, TEXT("attached actors"), FString::FromInt(Fresh.AttachedActors), FString::FromInt(Now.AttachedActors));
	}
	if (Now.LatentActions != Fresh.LatentActions)
	{
		Leak(ESpawnPacerLeakKind::LatentAction, TEXT("latent actions"), FString::FromInt(Fresh.LatentActions), FString::FromInt(Now.LatentActions));
	}
	for (const TPair<FName, bool>& Tick : Fresh.TickEnabled)
	{
		const bool* NowTick = Now.TickEnabled.Find(Tick.Key);
		if (NowTick && *NowTick != Tick.Value)
		{
			Leak(ESpawnPacerLeakKind::Tick, Tick.Key.ToString(), Tick.Value ? TEXT("on") : TEXT("off"), *NowTick ? TEXT("on") : TEXT("off"));
		}
	}

	// Then every property both snapshots have (a removed or added component's properties are covered above).
	for (const TPair<FString, FString>& Property : Fresh.Properties)
	{
		const FString* NowValue = Now.Properties.Find(Property.Key);
		if (!NowValue || *NowValue == Property.Value)
		{
			continue;
		}
		const bool bActor = Property.Key.StartsWith(TEXT("Actor."));
		const ESpawnPacerLeakKind Kind = Fresh.DelegateProperties.Contains(Property.Key) ? ESpawnPacerLeakKind::Delegate
			: (bActor ? ESpawnPacerLeakKind::ActorProperty : ESpawnPacerLeakKind::ComponentProperty);
		Leak(Kind, Property.Key, Property.Value, *NowValue);
	}
	return OutLeaks.Num() - Before;
}

const TCHAR* FSpawnPacerResetVerifier::KindName(ESpawnPacerLeakKind Kind)
{
	switch (Kind)
	{
	case ESpawnPacerLeakKind::ActorProperty:     return TEXT("ActorProperty");
	case ESpawnPacerLeakKind::ComponentProperty: return TEXT("ComponentProperty");
	case ESpawnPacerLeakKind::ComponentSet:      return TEXT("ComponentSet");
	case ESpawnPacerLeakKind::Attachment:        return TEXT("Attachment");
	case ESpawnPacerLeakKind::Delegate:          return TEXT("Delegate");
	case ESpawnPacerLeakKind::Tick:              return TEXT("Tick");
	case ESpawnPacerLeakKind::LatentAction:      return TEXT("LatentAction");
	default:                                     return TEXT("?");
	}
}
