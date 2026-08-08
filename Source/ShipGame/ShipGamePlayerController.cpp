// Copyright Epic Games, Inc. All Rights Reserved.


#include "ShipGamePlayerController.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "InputMappingContext.h"
#include "Blueprint/UserWidget.h"
#include "ShipGame.h"
#include "Widgets/Input/SVirtualJoystick.h"
#include "HelmHUDWidget.h"
#include "SteeringWheel.h"
#include "ShipActor.h"

void AShipGamePlayerController::BeginPlay()
{
	Super::BeginPlay();

	// only spawn touch controls on local player controllers
	if (ShouldUseTouchControls() && IsLocalPlayerController())
	{
		// spawn the mobile controls widget
		MobileControlsWidget = CreateWidget<UUserWidget>(this, MobileControlsWidgetClass);

		if (MobileControlsWidget)
		{
			// add the controls to the player screen
			MobileControlsWidget->AddToPlayerScreen(0);

		} else {

			UE_LOG(LogShipGame, Error, TEXT("Could not spawn mobile controls widget."));

		}

	}

	// Only the local player needs to know about their own possessed pawn -
	// this is what keeps the helm HUD from ever being created for anyone but
	// the player actually steering (see HandlePossessedPawnChanged).
	if (IsLocalPlayerController())
	{
		OnPossessedPawnChanged.AddDynamic(this, &AShipGamePlayerController::HandlePossessedPawnChanged);
	}
}

void AShipGamePlayerController::HandlePossessedPawnChanged(APawn* PreviouslyPossessedPawn, APawn* NewPawn)
{
	ASteeringWheel* Wheel = Cast<ASteeringWheel>(NewPawn);
	if (!Wheel)
	{
		if (HelmHUDWidget)
		{
			HelmHUDWidget->RemoveFromParent();
			HelmHUDWidget = nullptr;
		}
		return;
	}

	AShipActor* Ship = Wheel->GetOrFindControlledShip();
	if (!HelmHUDWidgetClass || !Ship)
	{
		return;
	}

	HelmHUDWidget = CreateWidget<UHelmHUDWidget>(this, HelmHUDWidgetClass);
	if (HelmHUDWidget)
	{
		HelmHUDWidget->Ship = Ship;
		HelmHUDWidget->AddToPlayerScreen(0);
	}
}

void AShipGamePlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	// only add IMCs for local player controllers
	if (IsLocalPlayerController())
	{
		// Add Input Mapping Contexts
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
		{
			for (UInputMappingContext* CurrentContext : DefaultMappingContexts)
			{
				Subsystem->AddMappingContext(CurrentContext, 0);
			}

			// only add these IMCs if we're not using mobile touch input
			if (!ShouldUseTouchControls())
			{
				for (UInputMappingContext* CurrentContext : MobileExcludedMappingContexts)
				{
					Subsystem->AddMappingContext(CurrentContext, 0);
				}
			}
		}
	}
}

bool AShipGamePlayerController::ShouldUseTouchControls() const
{
	// are we on a mobile platform? Should we force touch?
	return SVirtualJoystick::ShouldDisplayTouchInterface() || bForceTouchControls;
}
