#include "CannonWidget.h"
#include "Cannon.h"

void UCannonWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (Cannon)
	{
		Cannon->OnAmmoStateChanged.AddDynamic(this, &UCannonWidget::HandleAmmoStateChanged);
	}

	RefreshAmmoDisplay();
}

void UCannonWidget::NativeDestruct()
{
	if (Cannon)
	{
		Cannon->OnAmmoStateChanged.RemoveDynamic(this, &UCannonWidget::HandleAmmoStateChanged);
	}

	Super::NativeDestruct();
}

void UCannonWidget::HandleAmmoStateChanged()
{
	RefreshAmmoDisplay();
}
