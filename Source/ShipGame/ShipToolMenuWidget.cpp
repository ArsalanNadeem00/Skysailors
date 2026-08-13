#include "ShipToolMenuWidget.h"
#include "ShipCharacter.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"

FReply UShipToolMenuWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (IsCloseKey(InKeyEvent.GetKey()))
	{
		if (OwningCharacter)
		{
			OwningCharacter->CloseToolMenu();
		}
		return FReply::Handled();
	}

	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

bool UShipToolMenuWidget::IsCloseKey(const FKey& Key) const
{
	if (Key == EKeys::Escape)
	{
		return true;
	}

	APlayerController* PC = OwningCharacter ? OwningCharacter->GetController<APlayerController>() : nullptr;
	ULocalPlayer* LocalPlayer = PC ? PC->GetLocalPlayer() : nullptr;
	UEnhancedInputLocalPlayerSubsystem* Subsystem = LocalPlayer ? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LocalPlayer) : nullptr;
	if (!Subsystem)
	{
		return false;
	}

	for (const FKey& BoundKey : Subsystem->QueryKeysMappedToAction(OwningCharacter->GetInteractAction()))
	{
		if (BoundKey == Key)
		{
			return true;
		}
	}

	return false;
}
