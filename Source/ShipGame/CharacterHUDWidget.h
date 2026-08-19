#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CharacterHUDWidget.generated.h"

class AShipCharacter;

/**
 * Screen-space health/hunger bars for the player's own AShipCharacter,
 * shown only while a player is possessing their character directly - not
 * while steering the ship or operating a cannon (see
 * AShipGamePlayerController::HandlePossessedPawnChanged, which creates one
 * of these and sets Character the moment that player possesses an
 * AShipCharacter, and destroys it the moment they possess anything else -
 * same lifecycle pattern as UHelmHUDWidget/UCannonWidget, effectively the
 * "default" HUD of the three, replaced by whichever of the other two is
 * appropriate once the player takes the helm or a cannon). Purely a C++
 * contract - actual bar visuals belong in a Blueprint subclass implementing
 * SetHealth/SetHunger, same pattern as UHelmHUDWidget/UCombatLifeBar.
 *
 * The damage hit-flash (AShipCharacter::ClientPlayHitFlash/
 * AShipGamePlayerController::PlayCharacterHitFlash) deliberately does NOT
 * live here - see UDamageVignetteWidget instead. That damage can land while
 * the player is piloting a wheel/cannon (see AShipCharacter::
 * IsEffectivelyPlayerControlled), i.e. exactly when this widget doesn't
 * exist, so the flash needs a widget that survives every HUD swap this one
 * doesn't.
 */
UCLASS(abstract)
class SHIPGAME_API UCharacterHUDWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// Character whose Health/Hunger this widget displays. Set once by
	// whoever creates the widget.
	UPROPERTY(BlueprintReadOnly, Category = "Character HUD")
	TObjectPtr<AShipCharacter> Character;

	// Called every tick with Character's current Health (0-100). Implemented
	// in the Blueprint subclass to drive the actual bar. Polled here rather
	// than pushed via a delegate, same reasoning as
	// UHelmHUDWidget::SetHullIntegrity - both Health and Hunger are plain
	// replicated floats with no OnRep to hook.
	UFUNCTION(BlueprintImplementableEvent, Category = "Character HUD")
	void SetHealth(float Value);

	// Called every tick with Character's current Hunger (0-100). Same
	// reasoning as SetHealth.
	UFUNCTION(BlueprintImplementableEvent, Category = "Character HUD")
	void SetHunger(float Value);

protected:
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
};
