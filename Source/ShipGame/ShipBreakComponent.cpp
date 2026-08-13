#include "ShipBreakComponent.h"
#include "Net/UnrealNetwork.h"

UShipBreakComponent::UShipBreakComponent()
{
	// Starts inactive - invisible, not playing, not interactable - until
	// AShipActor::ActivateRandomShipBreak turns it on (see the class
	// comment). ApplyBrokenVisualState (called from BeginPlay) enforces this
	// regardless of whatever a Blueprint instance's Niagara defaults say.
	bAutoActivate = false;

	SetIsReplicatedByDefault(true);
}

void UShipBreakComponent::BeginPlay()
{
	Super::BeginPlay();

	ApplyBrokenVisualState();
}

void UShipBreakComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UShipBreakComponent, bIsBroken);
}

void UShipBreakComponent::SetBroken(bool bNewBroken)
{
	if (bIsBroken == bNewBroken)
	{
		return;
	}

	bIsBroken = bNewBroken;
	ApplyBrokenVisualState();
}

void UShipBreakComponent::OnRep_IsBroken()
{
	ApplyBrokenVisualState();
}

void UShipBreakComponent::ApplyBrokenVisualState()
{
	if (bIsBroken)
	{
		Activate(true);
		SetVisibility(true, true);
	}
	else
	{
		Deactivate();
		SetVisibility(false, true);
	}
}
