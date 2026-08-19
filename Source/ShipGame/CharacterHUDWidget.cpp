#include "CharacterHUDWidget.h"
#include "ShipCharacter.h"

void UCharacterHUDWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (Character)
	{
		SetHealth(Character->GetHealth());
		SetHunger(Character->GetHunger());
	}
}
