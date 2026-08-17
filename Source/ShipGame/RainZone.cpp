#include "RainZone.h"
#include "Components/SphereComponent.h"
#include "ShipActor.h"
#include "Puddle.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "TimerManager.h"

ARainZone::ARainZone()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true; // Required for MulticastSetRaining to actually reach clients.

	ZoneVolume = CreateDefaultSubobject<USphereComponent>(TEXT("ZoneVolume"));
	RootComponent = ZoneVolume;
	ZoneVolume->SetSphereRadius(1000.f); // Placeholder - resize/reposition per placement.
	ZoneVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	ZoneVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
	ZoneVolume->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Overlap);
	ZoneVolume->SetGenerateOverlapEvents(true);
}

void ARainZone::BeginPlay()
{
	Super::BeginPlay();

	ZoneVolume->OnComponentBeginOverlap.AddDynamic(this, &ARainZone::OnZoneBeginOverlap);
	ZoneVolume->OnComponentEndOverlap.AddDynamic(this, &ARainZone::OnZoneEndOverlap);
}

void ARainZone::OnZoneBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (!HasAuthority())
	{
		return;
	}

	AShipActor* Ship = Cast<AShipActor>(OtherActor);
	if (!Ship || OverlappingShip)
	{
		return;
	}

	OverlappingShip = Ship;
	MulticastSetRaining(Ship, true);

	GetWorldTimerManager().SetTimer(PuddleSpawnTimerHandle, this, &ARainZone::TrySpawnPuddle, PuddleSpawnInterval, /*bLoop=*/true);
}

void ARainZone::OnZoneEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	if (!HasAuthority())
	{
		return;
	}

	if (!OverlappingShip || OtherActor != OverlappingShip)
	{
		return;
	}

	GetWorldTimerManager().ClearTimer(PuddleSpawnTimerHandle);
	MulticastSetRaining(OverlappingShip, false);
	OverlappingShip = nullptr;
}

void ARainZone::TrySpawnPuddle()
{
	if (!OverlappingShip || !PuddleClass)
	{
		return;
	}

	if (FMath::FRand() > PuddleSpawnChance)
	{
		return;
	}

	FVector SpawnLocation;
	if (!OverlappingShip->GetRandomDeckLocation(SpawnLocation))
	{
		return;
	}

	GetWorld()->SpawnActor<APuddle>(PuddleClass, SpawnLocation, FRotator::ZeroRotator);
}

void ARainZone::MulticastSetRaining_Implementation(AShipActor* Ship, bool bRaining)
{
	if (!bRaining)
	{
		if (ActiveRainEffectComponent)
		{
			ActiveRainEffectComponent->Deactivate();
			ActiveRainEffectComponent = nullptr;
		}
		return;
	}

	if (!Ship || !RainEffect)
	{
		return;
	}

	ActiveRainEffectComponent = UNiagaraFunctionLibrary::SpawnSystemAttached(
		RainEffect, Ship->ShipMesh, NAME_None, FVector::ZeroVector, FRotator::ZeroRotator,
		EAttachLocation::SnapToTarget, /*bAutoDestroy=*/true);
}
