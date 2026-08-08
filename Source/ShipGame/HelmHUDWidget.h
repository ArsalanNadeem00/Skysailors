#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HelmHUDWidget.generated.h"

class AShipActor;

/**
 * Screen-space health bar for the ship's HullIntegrity, shown only to the
 * player currently at the helm (see AShipGamePlayerController::
 * HandlePossessedPawnChanged, which creates one of these and sets Ship the
 * moment that player possesses an ASteeringWheel, and destroys it the moment
 * they don't). Purely a C++ contract - actual bar visuals belong in a
 * Blueprint subclass implementing SetHullIntegrity, same pattern as
 * UCombatLifeBar/USideScrollingUI.
 */
UCLASS(abstract)
class SHIPGAME_API UHelmHUDWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// Ship whose HullIntegrity this widget displays. Set once by whoever
	// creates the widget.
	UPROPERTY(BlueprintReadOnly, Category = "Helm HUD")
	TObjectPtr<AShipActor> Ship;

	// Called every tick with Ship's current HullIntegrity (0-100). Implemented
	// in the Blueprint subclass to drive the actual progress bar. Polled here
	// rather than pushed via a delegate since HullIntegrity is a plain
	// replicated float with no OnRep to hook.
	UFUNCTION(BlueprintImplementableEvent, Category = "Helm HUD")
	void SetHullIntegrity(float Value);

protected:
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
};
