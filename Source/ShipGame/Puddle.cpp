#include "Puddle.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SphereComponent.h"
#include "ShipActor.h"
#include "ShipCharacter.h"
#include "Kismet/GameplayStatics.h"

APuddle::APuddle()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	PuddleMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PuddleMesh"));
	RootComponent = PuddleMesh;
	// No collision - a puddle is a flat, walkable floor mesh, not an obstacle.
	PuddleMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Overlap-only against pawns - this is what makes characters slip, not
	// PuddleMesh (which deliberately has no collision at all).
	SlipTriggerSphere = CreateDefaultSubobject<USphereComponent>(TEXT("SlipTriggerSphere"));
	SlipTriggerSphere->SetupAttachment(PuddleMesh);
	SlipTriggerSphere->SetSphereRadius(60.f);
	SlipTriggerSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	SlipTriggerSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	SlipTriggerSphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	SlipTriggerSphere->SetGenerateOverlapEvents(true);
}

void APuddle::BeginPlay()
{
	Super::BeginPlay();

	SlipTriggerSphere->OnComponentBeginOverlap.AddDynamic(this, &APuddle::OnSlipTriggerBeginOverlap);

	// Attaches to the ship's hull so the puddle rides the deck as the ship
	// moves, keeping wherever it was placed in the level as its relative
	// offset - same as ASteeringWheel/AClosetDoor::BeginPlay, and for the same
	// reason: both server and clients resolve the same level-placed
	// AShipActor here, so no attachment replication is needed.
	if (AShipActor* Ship = GetOrFindControlledShip())
	{
		AttachToComponent(Ship->ShipMesh, FAttachmentTransformRules::KeepWorldTransform);
	}
}

void APuddle::OnSlipTriggerBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (!HasAuthority())
	{
		return;
	}

	if (AShipCharacter* Character = Cast<AShipCharacter>(OtherActor))
	{
		Character->TrySlip();
	}
}

AShipActor* APuddle::GetOrFindControlledShip()
{
	if (IsValid(ControlledShip))
	{
		return ControlledShip;
	}

	// Assumes exactly one ship is placed in the level, matching
	// ASteeringWheel/AClosetDoor's own lookup for the same reason.
	TArray<AActor*> FoundShips;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AShipActor::StaticClass(), FoundShips);
	if (FoundShips.Num() > 0)
	{
		ControlledShip = Cast<AShipActor>(FoundShips[0]);
	}

	return ControlledShip;
}
