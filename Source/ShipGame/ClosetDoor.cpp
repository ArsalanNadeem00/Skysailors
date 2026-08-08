#include "ClosetDoor.h"
#include "Components/StaticMeshComponent.h"
#include "ShipActor.h"
#include "Kismet/GameplayStatics.h"

AClosetDoor::AClosetDoor()
{
	PrimaryActorTick.bCanEverTick = false;

	DoorMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DoorMesh"));
	RootComponent = DoorMesh;
	DoorMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	DoorMesh->SetCollisionResponseToAllChannels(ECR_Block);
}

void AClosetDoor::BeginPlay()
{
	Super::BeginPlay();

	// Attaches to the ship's hull so the closet rides the deck as the ship
	// moves, keeping wherever it was placed in the level as its relative
	// offset - same as ASteeringWheel::BeginPlay, and for the same reason:
	// both server and clients resolve the same level-placed AShipActor here,
	// so no attachment replication is needed.
	if (AShipActor* Ship = GetOrFindControlledShip())
	{
		AttachToComponent(Ship->ShipMesh, FAttachmentTransformRules::KeepWorldTransform);
	}
}

AShipActor* AClosetDoor::GetOrFindControlledShip()
{
	if (IsValid(ControlledShip))
	{
		return ControlledShip;
	}

	// Assumes exactly one ship is placed in the level, matching
	// ASteeringWheel's own lookup for the same reason.
	TArray<AActor*> FoundShips;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AShipActor::StaticClass(), FoundShips);
	if (FoundShips.Num() > 0)
	{
		ControlledShip = Cast<AShipActor>(FoundShips[0]);
	}

	return ControlledShip;
}
