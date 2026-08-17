#pragma once

#include "CoreMinimal.h"
#include "MountableItem.h"
#include "Shield.generated.h"

/**
 * Mountable ship defense - currently just an AMountableItem with no behavior
 * of its own beyond the mesh/attach/carry it inherits. Blocking logic
 * (coverage arc, damage mitigation) belongs here once designed.
 */
UCLASS()
class SHIPGAME_API AShield : public AMountableItem
{
	GENERATED_BODY()
};
