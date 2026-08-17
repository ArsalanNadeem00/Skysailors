#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CannonWidget.generated.h"

class ACannon;

/**
 * On-screen aiming/ammo widget shown only to the player currently operating
 * an ACannon (see AShipGamePlayerController::HandlePossessedPawnChanged,
 * which creates one of these and sets Cannon the moment that player
 * possesses an ACannon, and destroys it the moment they don't) - same
 * lifecycle pattern as UHelmHUDWidget. Renamed from UCannonReticleWidget now
 * that it's grown beyond a static aiming reticle to also show the cannon's
 * ammo list (see RefreshAmmoDisplay) - if you're looking for the old class,
 * see the ActiveClassRedirects entry in DefaultEngine.ini, which keeps any
 * existing Blueprint parented to the old name pointed at this one.
 *
 * The reticle itself is still a static Blueprint element with nothing to
 * drive from C++ (aiming only ever moves via W/A/S/D, not the mouse - see
 * ACannon's class comment). The ammo list is the part that's actually
 * data-driven: read Cannon->AmmoTypes (each entry's Name/Icon/AmmoCount/
 * MaxAmmoCount/bInfiniteAmmo) and Cannon->CurrentAmmoTypeIndex (which one to
 * highlight) in the Blueprint subclass's implementation of
 * RefreshAmmoDisplay to build/update a horizontal list of ammo icons (plus
 * whatever ammo-count text/bar you want per entry). This class handles
 * binding to Cannon's OnAmmoStateChanged delegate and calling
 * RefreshAmmoDisplay whenever it fires (plus once up front, in
 * NativeConstruct) - pushed via that delegate (which covers both switching
 * ammo type and the equipped type's AmmoCount decrementing on fire) rather
 * than polled every tick like UHelmHUDWidget's HullIntegrity, since
 * CurrentAmmoTypeIndex/AmmoTypes both have a proper OnRep to hook (see
 * ACannon's class comment for why HullIntegrity doesn't).
 */
UCLASS(abstract)
class SHIPGAME_API UCannonWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// Cannon this widget is showing ammo for. Set once by whoever creates
	// the widget (see AShipGamePlayerController::HandlePossessedPawnChanged),
	// before this widget is added to the viewport - NativeConstruct relies
	// on this already being set by the time it runs.
	UPROPERTY(BlueprintReadOnly, Category = "Cannon Widget")
	TObjectPtr<ACannon> Cannon;

	// Rebuild/refresh the ammo list UI - read Cannon->AmmoTypes and
	// Cannon->CurrentAmmoTypeIndex to know what to show and which entry to
	// highlight. Called once up front (NativeConstruct) and again every time
	// Cannon->OnAmmoStateChanged fires. Implement in the Blueprint subclass.
	UFUNCTION(BlueprintImplementableEvent, Category = "Cannon Widget")
	void RefreshAmmoDisplay();

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UFUNCTION()
	void HandleAmmoStateChanged();
};
