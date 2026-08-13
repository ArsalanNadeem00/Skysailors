#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ShipToolMenuWidget.generated.h"

class AShipCharacter;

/**
 * Tool-selection menu shown to a player interacting with an AClosetDoor (see
 * AShipCharacter::ClientOpenToolMenu) - lets them pick which tool (Mop,
 * Wrench, or None) to equip. Purely a C++ contract, same pattern as
 * UHelmHUDWidget: actual buttons/layout belong in a Blueprint subclass.
 * Each button should call OwningCharacter->RequestEquipTool(NewTool) - that
 * single call both equips the chosen tool and closes this menu (see
 * RequestEquipTool's comment), so there's no separate close/cancel button to
 * wire up.
 */
UCLASS(abstract)
class SHIPGAME_API UShipToolMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// Character that opened this menu - set once by AShipCharacter::
	// ClientOpenToolMenu right after creating the widget.
	UPROPERTY(BlueprintReadOnly, Category = "Tool Menu")
	TObjectPtr<AShipCharacter> OwningCharacter;

protected:
	// Closes the menu (via OwningCharacter->CloseToolMenu) on Escape, or on
	// whichever key is currently bound to OwningCharacter's InteractAction -
	// needed because ClientOpenToolMenu puts input in UI-only mode while this
	// menu is open, so OwningCharacter's own Enhanced Input bindings (which
	// would otherwise handle Interact) never fire until the menu closes.
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

private:
	// True if Key should close this menu - see NativeOnKeyDown.
	bool IsCloseKey(const FKey& Key) const;
};
