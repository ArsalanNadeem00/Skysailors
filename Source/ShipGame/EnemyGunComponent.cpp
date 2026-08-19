#include "EnemyGunComponent.h"
#include "EnemyShip.h"
#include "ShipActor.h"
#include "ShipCharacter.h"
#include "CannonProjectile.h"
#include "GameFramework/ProjectileMovementComponent.h"
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

	if (AmmoType.bTargetPlayer)
	{
		if (AShipCharacter* PlayerTarget = FindVisiblePlayerTarget())
		{
			FireAtTarget(PlayerTarget);
		}
	}
	else if (AShipActor* TargetShip = GetTargetShip())
	{
		if (IsLocationInFiringArc(TargetShip->GetActorLocation()))
		{
			FireAtTarget(TargetShip);
		}
	}
}

AShipActor* UEnemyGunComponent::GetTargetShip() const
{
	AEnemyShip* OwningShip = Cast<AEnemyShip>(GetOwner());
	return OwningShip ? OwningShip->ControlledShip : nullptr;
}

AShipCharacter* UEnemyGunComponent::FindVisiblePlayerTarget() const
{
	for (const TWeakObjectPtr<AShipCharacter>& CandidatePtr : AShipCharacter::GetActiveShipCharacters())
	{
		AShipCharacter* Candidate = CandidatePtr.Get();

		// Covers both directly-controlled characters and ones currently
		// piloting a wheel/cannon (their body just stands wherever they got
		// in) - see IsEffectivelyPlayerControlled's comment.
		if (!Candidate || !Candidate->IsEffectivelyPlayerControlled())
		{
			continue;
		}

		if (IsLocationInFiringArc(Candidate->GetActorLocation()) && HasLineOfSightTo(Candidate))
		{
			return Candidate;
		}
	}

	return nullptr;
}

bool UEnemyGunComponent::IsLocationInFiringArc(const FVector& TargetLocation) const
{
	const FVector ToTarget = TargetLocation - GetComponentLocation();
	const float Distance = ToTarget.Size();
	if (Distance <= 0.f || Distance > FiringRange)
	{
		return false;
	}

	const float DotToTarget = FVector::DotProduct(GetForwardVector(), ToTarget / Distance);
	const float CosHalfAngle = FMath::Cos(FMath::DegreesToRadians(FiringConeHalfAngleDegrees));
	return DotToTarget >= CosHalfAngle;
}

bool UEnemyGunComponent::HasLineOfSightTo(const AActor* Target) const
{
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(GetOwner());
	QueryParams.AddIgnoredActor(Target);

	FHitResult Hit;
	const bool bBlocked = GetWorld()->LineTraceSingleByChannel(Hit, GetComponentLocation(), Target->GetActorLocation(), ECC_Visibility, QueryParams);
	return !bBlocked;
}

FVector UEnemyGunComponent::PredictTargetLocation(const AActor* Target) const
{
	const FVector TargetLocation = Target->GetActorLocation();

	// Ship-targeting shots have nothing to lead relative to - GetTargetShip()
	// IS Target here, so its own velocity would just be lagging behind
	// itself.
	if (!AmmoType.bTargetPlayer)
	{
		return TargetLocation;
	}

	AShipActor* PlayerShip = GetTargetShip();
	if (!PlayerShip)
	{
		return TargetLocation;
	}

	const FVector ShipVelocity = PlayerShip->GetVelocity();
	if (ShipVelocity.IsNearlyZero())
	{
		return TargetLocation;
	}

	// Reads InitialSpeed off ProjectileClass's own CDO rather than an
	// already-spawned instance - this runs before FireAtTarget spawns
	// anything, so the aim direction/velocity assignment there both end up
	// based on the same predicted point. ProjectileClass is guaranteed valid
	// here - FireAtTarget already returned early otherwise, before ever
	// calling this.
	const ACannonProjectile* ProjectileDefaults = ProjectileClass->GetDefaultObject<ACannonProjectile>();
	const float ProjectileSpeed = ProjectileDefaults && ProjectileDefaults->ProjectileMovement
		? ProjectileDefaults->ProjectileMovement->InitialSpeed
		: 0.f;
	if (ProjectileSpeed <= 0.f)
	{
		return TargetLocation;
	}

	const float Distance = FVector::Dist(GetComponentLocation(), TargetLocation);
	const float FlightTime = Distance / ProjectileSpeed;

	return TargetLocation + ShipVelocity * FlightTime;
}

void UEnemyGunComponent::FireAtTarget(const AActor* Target)
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

	// Aimed at PredictTargetLocation(Target), not fired along this
	// component's fixed facing, and not just at Target's current position
	// either - see the class comment/PredictTargetLocation's comment.
	const FVector AimPoint = PredictTargetLocation(Target);
	const FRotator AimRotation = (AimPoint - GetComponentLocation()).Rotation();
	const FTransform SpawnTransform(AimRotation, GetComponentLocation());

	// Deferred, not a plain SpawnActor - ConfigureAmmo needs to set this
	// ammo's damage/mesh/sound/impact effect on the projectile before its
	// own BeginPlay runs, same reasoning as ACannon::ServerFire_Implementation.
	ACannonProjectile* Projectile = GetWorld()->SpawnActorDeferred<ACannonProjectile>(ProjectileClass, SpawnTransform, GetOwner(), nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (Projectile)
	{
		Projectile->ConfigureAmmo(AmmoType.Damage, AmmoType.ProjectileMesh, AmmoType.FireSound, AmmoType.ImpactEffect, AmmoType.ImpactSound);

		// Unlike ACannon's projectiles (which never damage the player's own
		// AShipActor/AShipCharacter - see those flags' comments), this is a
		// hostile shot and should be able to hurt whichever kind of target
		// AmmoType.bTargetPlayer says it was aimed at.
		if (AmmoType.bTargetPlayer)
		{
			Projectile->bDamagesPlayerCharacter = true;
		}
		else
		{
			Projectile->bDamagesPlayerShip = true;
		}

		Projectile->FinishSpawning(SpawnTransform);

		// Explicitly (re)set the projectile's velocity to fly exactly along
		// AimRotation, rather than relying solely on
		// UProjectileMovementComponent's own implicit "derive Velocity from
		// InitialSpeed + spawn rotation" behavior (which runs automatically
		// during component registration, as part of FinishSpawning above).
		// That implicit derivation is what ACannon's own shots also rely on,
		// but a human aiming ACannon self-corrects by eye, so any small
		// imprecision there is invisible - this gun's aim has no such
		// feedback loop, so the same imprecision would show up directly as
		// shots consistently landing near, but not exactly on, a target.
		// Setting Velocity here, after the projectile is fully spawned,
		// guarantees this shot flies precisely at the target's location
		// this tick sampled, with nothing left to implicit timing.
		if (Projectile->ProjectileMovement)
		{
			Projectile->ProjectileMovement->Velocity = AimRotation.Vector() * Projectile->ProjectileMovement->InitialSpeed;
		}
	}
}
