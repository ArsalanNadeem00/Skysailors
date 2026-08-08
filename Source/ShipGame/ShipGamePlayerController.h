// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "ShipGamePlayerController.generated.h"

class UInputMappingContext;
class UUserWidget;
class UHelmHUDWidget;

/**
 *  Basic PlayerController class for a third person game
 *  Manages input mappings
 */
UCLASS(abstract)
class SHIPGAME_API AShipGamePlayerController : public APlayerController
{
	GENERATED_BODY()
	
protected:

	/** Input Mapping Contexts */
	UPROPERTY(EditAnywhere, Category ="Input|Input Mappings")
	TArray<UInputMappingContext*> DefaultMappingContexts;

	/** Input Mapping Contexts */
	UPROPERTY(EditAnywhere, Category="Input|Input Mappings")
	TArray<UInputMappingContext*> MobileExcludedMappingContexts;

	/** Mobile controls widget to spawn */
	UPROPERTY(EditAnywhere, Category="Input|Touch Controls")
	TSubclassOf<UUserWidget> MobileControlsWidgetClass;

	/** Pointer to the mobile controls widget */
	UPROPERTY()
	TObjectPtr<UUserWidget> MobileControlsWidget;

	/** If true, the player will use UMG touch controls even if not playing on mobile platforms */
	UPROPERTY(EditAnywhere, Config, Category = "Input|Touch Controls")
	bool bForceTouchControls = false;

	// Class of the HullIntegrity health bar shown while this player is at the
	// helm (see HandlePossessedPawnChanged). Left unset disables the HUD.
	UPROPERTY(EditAnywhere, Category = "UI|Helm HUD")
	TSubclassOf<UHelmHUDWidget> HelmHUDWidgetClass;

	// Only non-null while this player currently possesses an ASteeringWheel -
	// created and destroyed by HandlePossessedPawnChanged, never persists
	// across a helm session.
	UPROPERTY()
	TObjectPtr<UHelmHUDWidget> HelmHUDWidget;

	/** Gameplay initialization */
	virtual void BeginPlay() override;

	/** Input mapping context setup */
	virtual void SetupInputComponent() override;

	/** Returns true if the player should use UMG touch controls */
	bool ShouldUseTouchControls() const;

	// Bound to OnPossessedPawnChanged (fires on the server and, separately,
	// on the owning client via Pawn's OnRep - never on other clients, since
	// PlayerController only replicates to its own owning connection) - this
	// is what keeps the helm HUD visible only to the player actually at the
	// wheel. Shows/creates the widget when NewPawn is an ASteeringWheel,
	// hides/destroys it otherwise.
	UFUNCTION()
	void HandlePossessedPawnChanged(APawn* PreviouslyPossessedPawn, APawn* NewPawn);

};
