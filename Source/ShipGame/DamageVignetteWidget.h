#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DamageVignetteWidget.generated.h"

/**
 * Screen-edge damage flash, shown to a player regardless of whether they
 * currently possess their own AShipCharacter, a ASteeringWheel, or an
 * ACannon. Unlike UCharacterHUDWidget/UHelmHUDWidget/UCannonWidget (which
 * are created/destroyed as a player moves between those three possessed
 * pawns - see AShipGamePlayerController::HandlePossessedPawnChanged), this
 * widget is created exactly once, in AShipGamePlayerController::BeginPlay,
 * and lives for that PlayerController's entire lifetime - it has to survive
 * every HUD swap, since AShipCharacter::ApplyDamage (and the resulting
 * ClientPlayHitFlash/PlayCharacterHitFlash call) can land while the damaged
 * player is piloting a wheel/cannon (see AShipCharacter::
 * IsEffectivelyPlayerControlled), i.e. exactly when UCharacterHUDWidget
 * would otherwise not exist to show it.
 *
 * Purely a C++ contract - the actual vignette visual (e.g. an Image widget
 * with a radial-gradient red texture, opacity animated up then back down)
 * belongs in a Blueprint subclass implementing PlayHitFlash, same pattern as
 * UCharacterHUDWidget/UHelmHUDWidget.
 */
UCLASS(abstract)
class SHIPGAME_API UDamageVignetteWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// Briefly flashes a faint red vignette at the screen edges - implement in
	// the Blueprint subclass. Called once per actual hit by
	// AShipGamePlayerController::PlayCharacterHitFlash - see the class
	// comment for why this widget (rather than UCharacterHUDWidget) is what
	// that forwards to.
	UFUNCTION(BlueprintImplementableEvent, Category = "Damage Vignette")
	void PlayHitFlash();
};
