#include "EnemyGunComponent.h"
#include "EnemyShip.h"
#include "ShipActor.h"
#include "CannonProjectile.h"
#include "Engine/World.h"

UEnemyGunComponent::UEnemyGunComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	// Visual only - no collision, same convention as every other mesh in
	// this codebase (ItemMesh/BarrelMesh/ProjectileMesh/etc.) - the owning
	// AEnemyShip's own HazardBox is what actually handles this ship's
	// collision.
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void UEnemyGunComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Nothing here has any purpose on a non-authoritative machine - firing
	// spawns a replicated actor, which must only ever happen once, on the
	// server, same as ACannon::ServerFire.
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	AShipActor* TargetShip = GetTargetShip();
	if (!TargetShip)
	{
		return;
	}

	if (IsTargetInFiringArc(TargetShip))
	{
		FireAtTarget(TargetShip);
	}
}

AShipActor* UEnemyGunComponent::GetTargetShip() const
{
	AEnemyShip* OwningShip = Cast<AEnemyShip>(GetOwner());
	return OwningShip ? OwningShip->ControlledShip : nullptr;
}

bool UEnemyGunComponent::IsTargetInFiringArc(const AShipActor* TargetShip) const
{
	const FVector ToTarget = TargetShip->GetActorLocation() - GetComponentLocation();
	const float Distance = ToTarget.Size();
	if (Distance <= 0.f || Distance > FiringRange)
	{
		return false;
	}

	const float DotToTarget = FVector::DotProduct(GetForwardVector(), ToTarget / Distance);
	const float CosHalfAngle = FMath::Cos(FMath::DegreesToRadians(FiringConeHalfAngleDegrees));
	return DotToTarget >= CosHalfAngle;
}

void UEnemyGunComponent::FireAtTarget(const AShipActor* TargetShip)
{
	if (!ProjectileClass)
	{
		return;
	}

	if (!AmmoType.bInfiniteAmmo && AmmoType.AmmoCount <= 0)
	{
		return;
	}

	const float CurrentTime = GetWorld()->GetTimeSeconds();
	if (CurrentTime - LastFireTime < AmmoType.FireCooldown)
	{
		return;
	}
	LastFireTime = CurrentTime;

	if (!AmmoType.bInfiniteAmmo)
	{
		AmmoType.AmmoCount = FMath::Max(AmmoType.AmmoCount - 1, 0);
	}

	// Aimed directly at the target's current position, not fired along this
	// component's fixed facing - see the class comment for why.
	const FRotator AimRotation = (TargetShip->GetActorLocation() - GetComponentLocation()).Rotation();
	const FTransform SpawnTransform(AimRotation, GetComponentLocation());

	// Deferred, not a plain SpawnActor - ConfigureAmmo needs to set this
	// ammo's damage/mesh/sound/impact effect on the projectile before its
	// own BeginPlay runs, same reasoning as ACannon::ServerFire_Implementation.
	ACannonProjectile* Projectile = GetWorld()->SpawnActorDeferred<ACannonProjectile>(ProjectileClass, SpawnTransform, GetOwner(), nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (Projectile)
	{
		Projectile->ConfigureAmmo(AmmoType.Damage, AmmoType.ProjectileMesh, AmmoType.FireSound, AmmoType.ImpactEffect, AmmoType.ImpactSound);

		// Unlike ACannon's projectiles (which never damage the player's own
		// AShipActor - see this flag's comment on ACannonProjectile), this
		// is a hostile shot and should be able to hurt the player ship.
		Projectile->bDamagesPlayerShip = true;

		Projectile->FinishSpawning(SpawnTransform);
	}
}
