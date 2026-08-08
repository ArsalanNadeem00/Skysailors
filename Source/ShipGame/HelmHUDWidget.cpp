#include "HelmHUDWidget.h"
#include "ShipActor.h"

void UHelmHUDWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (Ship)
	{
		SetHullIntegrity(Ship->HullIntegrity);
	}
}
