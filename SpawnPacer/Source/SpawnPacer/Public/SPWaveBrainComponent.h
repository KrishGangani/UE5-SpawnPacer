// P02 SpawnPacer (demo) - the gameplay part of a wave actor: set-up work at BeginPlay, a think timer, gameplay state.

#pragma once

#include "Components/ActorComponent.h"
#include "Engine/TimerHandle.h"
#include "SPWaveBrainComponent.generated.h"

/** What an enemy is doing. Changed by the think timer, so it is state a pool must reset (02 section 11). */
UENUM()
enum class ESPBrainState : uint8
{
	Idle,
	Engaging,
	Retreating
};

/**
 * Stands in for an enemy's gameplay code. At BeginPlay it does a calibrated amount of set-up work (data look-ups, AI
 * set-up in a real game): sp.Demo.BeginPlayWork iterations times BeginPlayWorkScale. Enemies then run a 0.5 s think
 * timer that changes their state. (The owning ASPWaveActor reports "ready" to the director after its whole BeginPlay,
 * this component's included.)
 *
 * The gameplay state is UPROPERTY on purpose: the reset verifier (step S8) compares reflected state.
 */
UCLASS(ClassGroup = (SpawnPacerDemo))
class USPWaveBrainComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USPWaveBrainComponent();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End UActorComponent

	/** Multiplies sp.Demo.BeginPlayWork for this class: 0 = no set-up work (drone, crate), 1 = an enemy's set-up. */
	UPROPERTY(EditDefaultsOnly, Category = "SpawnPacer Demo")
	float BeginPlayWorkScale = 0.0f;

	/** Whether this brain runs the think timer (enemies only). */
	UPROPERTY(EditDefaultsOnly, Category = "SpawnPacer Demo")
	bool bThinks = false;

	float GetHealth() const { return Health; }
	int32 GetAmmo() const { return Ammo; }
	ESPBrainState GetState() const { return State; }
	int32 GetThinkCount() const { return ThinkCount; }

private:
	/** One think: spends ammo and cycles the state. Runs every ThinkInterval seconds while the actor is alive. */
	void Think();

	UPROPERTY(VisibleInstanceOnly, Category = "SpawnPacer Demo")
	float Health = 100.0f;

	UPROPERTY(VisibleInstanceOnly, Category = "SpawnPacer Demo")
	int32 Ammo = 30;

	UPROPERTY(VisibleInstanceOnly, Category = "SpawnPacer Demo")
	ESPBrainState State = ESPBrainState::Idle;

	UPROPERTY(VisibleInstanceOnly, Category = "SpawnPacer Demo")
	int32 ThinkCount = 0;

	/** The set-up loop's result, stored so the optimiser can't delete the loop. */
	float WorkSink = 0.0f;

	FTimerHandle ThinkTimer;
};
