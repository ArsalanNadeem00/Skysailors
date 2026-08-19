// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "ShipGamePlayerController.generated.h"

class UInputMappingContext;
class UUserWidget;
class UHelmHUDWidget;
class UCannonWidget;
class UCharacterHUDWidget;
class UDamageVignetteWidget;
class ACannon;
class AShipCharacter;

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

public:
	// Forwards to DamageVignetteWidget->PlayHitFlash() - called by
	// AShipCharacter::ClientPlayHitFlash (itself a Client RPC, so this always
	// runs on the damaged player's own local machine). DamageVignetteWidget
	// is created once in BeginPlay and persists across every HUD swap (see
	// its class comment), unlike CharacterHUDWidget, specifically so this
	// still shows a flash while the damaged player is piloting a wheel/cannon
	// (see AShipCharacter::IsEffectivelyPlayerControlled) - not just while
	// they're directly possessing their own character.
	UFUNCTION(BlueprintCallable, Category = "UI")
	void PlayCharacterHitFlash();

protected:
	// Class of the health/hunger HUD shown while this player is possessing
	// their own AShipCharacter directly - i.e. whenever they're not at the
	// helm or a cannon (see HandlePossessedPawnChanged). Left unset disables
	// the HUD.
	UPROPERTY(EditAnywhere, Category = "UI|Character HUD")
	TSubclassOf<UCharacterHUDWidget> CharacterHUDWidgetClass;

	// Only non-null while this player currently possesses an AShipCharacter -
	// created and destroyed by HandlePossessedPawnChanged, same pattern as
	// HelmHUDWidget/CannonWidget.
	UPROPERTY()
	TObjectPtr<UCharacterHUDWidget> CharacterHUDWidget;

	// Class of the HullIntegrity health bar shown while this player is at the
	// helm (see HandlePossessedPawnChanged). Left unset disables the HUD.
	UPROPERTY(EditAnywhere, Category = "UI|Helm HUD")
	TSubclassOf<UHelmHUDWidget> HelmHUDWidgetClass;

	// Only non-null while this player currently possesses an ASteeringWheel -
	// created and destroyed by HandlePossessedPawnChanged, never persists
	// across a helm session.
	UPROPERTY()
	TObjectPtr<UHelmHUDWidget> HelmHUDWidget;

	// Class of the on-screen aiming/ammo widget shown while this player is
	// operating an ACannon (see HandlePossessedPawnChanged). Left unset
	// disables it.
	UPROPERTY(EditAnywhere, Category = "UI|Cannon")
	TSubclassOf<UCannonWidget> CannonWidgetClass;

	// Only non-null while this player currently possesses an ACannon -
	// created and destroyed by HandlePossessedPawnChanged, same pattern as
	// HelmHUDWidget.
	UPROPERTY()
	TObjectPtr<UCannonWidget> CannonWidget;

	// Class of the persistent screen-edge damage flash - see
	// UDamageVignetteWidget's class comment for why this is created once in
	// BeginPlay rather than by HandlePossessedPawnChanged like the other
	// three HUD widgets. Left unset disables the flash.
	UPROPERTY(EditAnywhere, Category = "UI|Damage Vignette")
	TSubclassOf<UDamageVignetteWidget> DamageVignetteWidgetClass;

	// Created once in BeginPlay and never destroyed/recreated - unlike
	// CharacterHUDWidget/HelmHUDWidget/CannonWidget, this has to keep
	// existing across every possessed-pawn change, since PlayCharacterHitFlash
	// must be able to reach it no matter what this player currently
	// possesses.
	UPROPERTY()
	TObjectPtr<UDamageVignetteWidget> DamageVignetteWidget;

	/** Gameplay initialization */
	virtual void BeginPlay() override;

	/** Input mapping context setup */
	virtual void SetupInputComponent() override;

	/** Returns true if the player should use UMG touch controls */
	bool ShouldUseTouchControls() const;

	// Bound to OnPossessedPawnChanged (fires on the server and, separately,
	// on the owning client via Pawn's OnRep - never on other clients, since
	// PlayerController only replicates to its own owning connection) - this
	// is what keeps exactly one of CharacterHUDWidget/HelmHUDWidget/
	// CannonWidget visible at a time, matching whichever of AShipCharacter/
	// ASteeringWheel/ACannon this player currently possesses, and
	// hides/destroys whichever of the other two was previously showing.
	UFUNCTION()
	void HandlePossessedPawnChanged(APawn* PreviouslyPossessedPawn, APawn* NewPawn);

};
