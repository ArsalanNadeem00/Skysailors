#pragma once

#include "CoreMinimal.h"
#include "AmmoType.generated.h"

class UStaticMesh;
class USoundBase;
class UNiagaraSystem;
class UTexture2D;
class UCameraShakeBase;

// Everything that varies per kind of shot - shared by ACannon (see
// ACannon::AmmoTypes) and UEnemyGunComponent (see UEnemyGunComponent::AmmoType).
// Deliberately data-only (filled in the Blueprint), same "adding a new X is a
// data change, not a code change" philosophy as AShipCharacter::
// FToolDefinition. FireCameraShake is only ever meaningful for ACannon (a
// player-operated weapon with a camera to shake) - UEnemyGunComponent simply
// never reads it, same as it being left unset costs nothing for ACannon.
// Name/Icon are purely cosmetic - read by UCannonWidget's Blueprint subclass
// to build its ammo list UI - and have no effect on firing behavior.
USTRUCT(BlueprintType)
struct FCannonAmmoType
{
	GENERATED_BODY()

	// Display name shown in the ammo-type UI (see UCannonWidget). FText
	// (not FString/FName) since it's user-facing.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ammo")
	FText Name;

	// Icon shown in the ammo-type UI (see UCannonWidget).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ammo")
	UTexture2D* Icon = nullptr;

	// If true, this ammo type never runs out - AmmoCount is ignored
	// entirely (never checked, never decremented) and MaxAmmoCount is
	// effectively meaningless. See ACannon::ServerFire_Implementation/
	// UEnemyGunComponent::FireAtTarget.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ammo")
	bool bInfiniteAmmo = false;

	// How many shots of this ammo type are currently loaded. Decremented by
	// 1 each time a shot of this ammo type actually fires (floored at 0),
	// ignored entirely while bInfiniteAmmo is true. Reaching 0 blocks
	// further firing of this ammo type until it's reloaded - reloading
	// isn't implemented yet (still belongs here once designed), so for now
	// this is a one-way depletion. Independently editable from
	// MaxAmmoCount (not auto-initialized from it) so a Blueprint/level
	// instance can deliberately start an ammo type already partially
	// depleted - set it equal to MaxAmmoCount for the common "starts full"
	// case.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ammo")
	int32 AmmoCount = 10;

	// Ammo capacity for this type - not enforced as a hard cap on AmmoCount
	// anywhere yet (nothing currently increases AmmoCount, so there's
	// nothing to clamp), purely a display/future-reload-target value until
	// reloading exists.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ammo")
	int32 MaxAmmoCount = 10;

	// Minimum seconds between shots while this ammo type is equipped.
	UPROPERTY(EditDefaultsOnly, Category = "Ammo")
	float FireCooldown = 1.f;

	// Damage dealt to whatever this ammo type's projectile hits - see
	// ACannonProjectile::OnHit.
	UPROPERTY(EditDefaultsOnly, Category = "Ammo")
	float Damage = 25.f;

	// Overrides the spawned projectile's visual mesh - see
	// ACannonProjectile::ConfigureAmmo. Left unset, the projectile keeps
	// whatever mesh its own Blueprint (ProjectileClass) already has assigned.
	UPROPERTY(EditDefaultsOnly, Category = "Ammo")
	UStaticMesh* ProjectileMesh = nullptr;

	// Played on just the firing player's camera each time this ammo type
	// fires a shot - see ACannon::ClientPlayFireCameraShake. Only ever read
	// by ACannon (a player-operated weapon) - see the class comment.
	UPROPERTY(EditDefaultsOnly, Category = "Ammo")
	TSubclassOf<UCameraShakeBase> FireCameraShake;

	// Played once by the spawned projectile itself, in its BeginPlay - see
	// ACannonProjectile::BeginPlay. Heard by everyone (not just the
	// firer/operator), since the projectile is a normal replicated actor
	// everyone sees.
	UPROPERTY(EditDefaultsOnly, Category = "Ammo")
	USoundBase* FireSound = nullptr;

	// Spawned by the projectile at the impact point/normal when it hits
	// something - see ACannonProjectile::OnHit.
	UPROPERTY(EditDefaultsOnly, Category = "Ammo")
	UNiagaraSystem* ImpactEffect = nullptr;

	// Played by the projectile at the impact point when it hits something -
	// see ACannonProjectile::OnHit. Same idea as ImpactEffect, just audio
	// instead of visual.
	UPROPERTY(EditDefaultsOnly, Category = "Ammo")
	USoundBase* ImpactSound = nullptr;
};
