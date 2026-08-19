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
#include "CannonWidget.h"
#include "Cannon.h"
#include "CharacterHUDWidget.h"
#include "DamageVignetteWidget.h"
#include "ShipCharacter.h"

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
	// this is what keeps the character/helm/cannon HUD from ever being
	// created for anyone but the player it actually belongs to (see
	// HandlePossessedPawnChanged).
	if (IsLocalPlayerController())
	{
		// Created once, up front, rather than by HandlePossessedPawnChanged
		// like CharacterHUDWidget/HelmHUDWidget/CannonWidget - see
		// UDamageVignetteWidget's class comment for why this one has to
		// persist across every possessed-pawn change instead of being
		// swapped alongside them. Added at a higher Z-order than those three
		// (all added at 0) so the flash always renders on top of whichever
		// of them is currently showing.
		if (DamageVignetteWidgetClass)
		{
			DamageVignetteWidget = CreateWidget<UDamageVignetteWidget>(this, DamageVignetteWidgetClass);
			if (DamageVignetteWidget)
			{
				DamageVignetteWidget->AddToPlayerScreen(10);
			}
		}

		OnPossessedPawnChanged.AddDynamic(this, &AShipGamePlayerController::HandlePossessedPawnChanged);

		// Covers the initial spawn case, where OnPossessedPawnChanged may
		// never actually fire after this binding: AShipGameMode possesses
		// the new AShipCharacter as part of spawning it, and depending on
		// timing that Possess() call can land before this BeginPlay runs -
		// reliably true for a listen-server host's own PlayerController
		// (server-side possession happens as part of the same spawn flow
		// this BeginPlay is also part of), and possible for a remote client
		// too if its Pawn replicates down around the same time as its own
		// PlayerController's BeginPlay. Without this, a player who never
		// takes the helm/a cannon (and so never generates a later
		// OnPossessedPawnChanged to piggyback on) would simply never see
		// CharacterHUDWidget at all. Synthesizing the same call here with
		// whatever's already possessed (if anything) makes the HUD created
		// unconditionally, matching the game mode's own current possession
		// state.
		if (GetPawn())
		{
			HandlePossessedPawnChanged(nullptr, GetPawn());
		}
	}
}

void AShipGamePlayerController::HandlePossessedPawnChanged(APawn* PreviouslyPossessedPawn, APawn* NewPawn)
{
	if (AShipCharacter* PossessedCharacter = Cast<AShipCharacter>(NewPawn))
	{
		if (CharacterHUDWidgetClass)
		{
			CharacterHUDWidget = CreateWidget<UCharacterHUDWidget>(this, CharacterHUDWidgetClass);
			if (CharacterHUDWidget)
			{
				CharacterHUDWidget->Character = PossessedCharacter;
				CharacterHUDWidget->AddToPlayerScreen(0);
			}
		}
	}
	else if (CharacterHUDWidget)
	{
		CharacterHUDWidget->RemoveFromParent();
		CharacterHUDWidget = nullptr;
	}

	if (ASteeringWheel* Wheel = Cast<ASteeringWheel>(NewPawn))
	{
		AShipActor* Ship = Wheel->GetOrFindControlledShip();
		if (HelmHUDWidgetClass && Ship)
		{
			HelmHUDWidget = CreateWidget<UHelmHUDWidget>(this, HelmHUDWidgetClass);
			if (HelmHUDWidget)
			{
				HelmHUDWidget->Ship = Ship;
				HelmHUDWidget->AddToPlayerScreen(0);
			}
		}
	}
	else if (HelmHUDWidget)
	{
		HelmHUDWidget->RemoveFromParent();
		HelmHUDWidget = nullptr;
	}

	if (ACannon* OperatedCannon = Cast<ACannon>(NewPawn))
	{
		if (CannonWidgetClass)
		{
			CannonWidget = CreateWidget<UCannonWidget>(this, CannonWidgetClass);
			if (CannonWidget)
			{
				CannonWidget->Cannon = OperatedCannon;
				CannonWidget->AddToPlayerScreen(0);
			}
		}
	}
	else if (CannonWidget)
	{
		CannonWidget->RemoveFromParent();
		CannonWidget = nullptr;
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

void AShipGamePlayerController::PlayCharacterHitFlash()
{
	if (DamageVignetteWidget)
	{
		DamageVignetteWidget->PlayHitFlash();
	}
}
