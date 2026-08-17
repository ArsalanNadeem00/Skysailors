#include "MountableItem.h"
#include "Components/StaticMeshComponent.h"

AMountableItem::AMountableItem()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	ItemMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ItemMesh"));
	RootComponent = ItemMesh;
	ItemMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// APawn defaults AutoPossessAI to PlacedInWorld, which would otherwise
	// spawn an AIController and possess every placed/mounted item the moment
	// it exists - same gotcha (and same fix) as ASteeringWheel's constructor.
	// Only ACannon::TryEngage should ever possess one of these.
	AutoPossessAI = EAutoPossessAI::Disabled;
}
