// P02 SpawnPacer (demo) - USPWaveBrainComponent.

#include "SPWaveBrainComponent.h"

#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(SPWaveBrainComponent)

namespace SPBrain
{
	/** Seconds between thinks. Half a second is a typical enemy decision rate. */
	constexpr float ThinkInterval = 0.5f;

	/** Iterations of the set-up loop for a class with BeginPlayWorkScale 1. Calibrated in step S2 (09); the director sets
	 *  it from -SPWork= so every run records the value it used. ECVF_Default so the command line and config can set it. */
	TAutoConsoleVariable<int32> CVarBeginPlayWork(
		TEXT("sp.Demo.BeginPlayWork"),
		4000,
		TEXT("Iterations of the demo enemies' BeginPlay set-up work (scaled per class)."),
		ECVF_Default);
}

USPWaveBrainComponent::USPWaveBrainComponent()
{
	// The brain's work happens in BeginPlay and in its timer, never per frame.
	PrimaryComponentTick.bCanEverTick = false;
}

void USPWaveBrainComponent::BeginPlay()
{
	Super::BeginPlay();

	// Calibrated set-up work: a dependent chain of sines, so it can't be vectorised away or skipped.
	const int32 Iterations = FMath::RoundToInt32(SPBrain::CVarBeginPlayWork.GetValueOnGameThread() * BeginPlayWorkScale);
	float Sink = 0.0f;
	for (int32 Index = 0; Index < Iterations; ++Index)
	{
		Sink = FMath::Sin(Sink + 0.001f * Index);
	}
	WorkSink = Sink;

	StartThinking();
}

void USPWaveBrainComponent::StartThinking()
{
	if (bThinks)
	{
		GetWorld()->GetTimerManager().SetTimer(ThinkTimer, this, &USPWaveBrainComponent::Think, SPBrain::ThinkInterval, true);
	}
}

void USPWaveBrainComponent::StopThinking()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ThinkTimer);
	}
}

void USPWaveBrainComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// The engine doesn't clear timers on EndPlay (01b #11). A destroyed brain's timer would simply stop firing, but a
	// pooled one would not, so the brain clears its own.
	StopThinking();
	Super::EndPlay(EndPlayReason);
}

void USPWaveBrainComponent::Think()
{
	++ThinkCount;
	Ammo = FMath::Max(0, Ammo - 1);
	State = static_cast<ESPBrainState>((static_cast<uint8>(State) + 1) % 3);
}
