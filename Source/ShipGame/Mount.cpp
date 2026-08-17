#include "Mount.h"
#include "Components/StaticMeshComponent.h"
#include "Components/ArrowComponent.h"
#include "Net/UnrealNetwork.h"
#include "ShipActor.h"
#include "MountableItem.h"
#include "Cannon.h"
#include "Shield.h"
#include "Kismet/GameplayStatics.h"

AMount::AMount()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	MountMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MountMesh"));
	RootComponent = MountMesh;
	MountMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	MountMesh->SetCollisionResponseToAllChannels(ECR_Block);

	ItemAttachPoint = CreateDefaultSubobject<USceneComponent>(TEXT("ItemAttachPoint"));
	ItemAttachPoint->SetupAttachment(RootComponent);
}

void AMount::BeginPlay()
{
	Super::BeginPlay();

	// Attaches to the ship's hull so the mount rides the deck as the ship
	// moves, keeping wherever it was placed in the level as its relative
	// offset - same as ASteeringWheel/AClosetDoor/APuddle::BeginPlay, and for
	// the same reason: both server and clients resolve the same level-placed
	// AShipActor here, so no attachment replication is needed.
	if (AShipActor* Ship = GetOrFindControlledShip())
	{
		AttachToComponent(Ship->ShipMesh, FAttachmentTransformRules::KeepWorldTransform);
	}

	// Only the server ever writes AttachedItem (see AttachItem/DetachItem) -
	// clients pick up whatever InitialItem attached to via replication.
	if (HasAuthority() && InitialItem)
	{
		AttachItem(InitialItem);
	}
}

void AMount::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMount, AttachedItem);
}

AShipActor* AMount::GetOrFindControlledShip()
{
	if (IsValid(ControlledShip))
	{
		return ControlledShip;
	}

	// Assumes exactly one ship is placed in the level, matching
	// ASteeringWheel/AClosetDoor/APuddle's own lookup for the same reason.
	TArray<AActor*> FoundShips;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AShipActor::StaticClass(), FoundShips);
	if (FoundShips.Num() > 0)
	{
		ControlledShip = Cast<AShipActor>(FoundShips[0]);
	}

	return ControlledShip;
}

EMountState AMount::GetMountState() const
{
	if (!AttachedItem)
	{
		return EMountState::Empty;
	}
	if (AttachedItem->IsA<ACannon>())
	{
		return EMountState::Cannon;
	}
	if (AttachedItem->IsA<AShield>())
	{
		return EMountState::Shield;
	}

	return EMountState::Empty;
}

bool AMount::AttachItem(AMountableItem* Item)
{
	if (!Item || AttachedItem)
	{
		return false;
	}

	Item->AttachToComponent(ItemAttachPoint, FAttachmentTransformRules::SnapToTargetIncludingScale);

	// Rotate Item so its own facing arrow points the same world direction as
	// this mount's facing arrow, rather than relying on ItemAttachPoint's
	// rotation (which is shared across every Mount instance regardless of
	// which side of the ship it's on). Both arrows are hand-placed per
	// Blueprint instance - the mount's arrow points outward off the hull,
	// the item's arrow points wherever "forward" means for that item (e.g.
	// out of the cannon's barrel) - so aligning them is what actually makes
	// items face outward once mounted. No-ops if either side is missing its
	// arrow (e.g. not yet added to that item's Blueprint), leaving
	// ItemAttachPoint's rotation as a fallback.
	UArrowComponent* MountArrow = FindComponentByClass<UArrowComponent>();
	UArrowComponent* ItemArrow = Item->FindComponentByClass<UArrowComponent>();
	if (MountArrow && ItemArrow)
	{
		const FQuat DeltaRotation = FQuat::FindBetweenNormals(ItemArrow->GetForwardVector(), MountArrow->GetForwardVector());
		Item->SetActorRotation(DeltaRotation * Item->GetActorQuat());
	}

	AttachedItem = Item;
	return true;
}

AMountableItem* AMount::DetachItem()
{
	if (!AttachedItem)
	{
		return nullptr;
	}

	AMountableItem* Item = AttachedItem;
	Item->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	AttachedItem = nullptr;
	return Item;
}
