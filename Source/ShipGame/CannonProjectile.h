#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CannonProjectile.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UProjectileMovementComponent;
class UStaticMesh;
class USoundBase;
class UNiagaraSystem;

/**
 * Fired by ACannon::ServerFire - spawned server-only (see ACannon::
 * ServerFire_Implementation), then replicates to every client the same way
 * any other replicated AActor with movement does (bReplicates plus the
 * engine's default SetReplicateMovement(true)): the server's transform
 * updates are what clients ultimately see, while ProjectileMovementComponent
 * simulates the same deterministic straight-line/gravity motion locally on
 * every machine (server included) so it looks smooth between those updates
 * rather than snapping. This is the standard Unreal projectile pattern and
 * deliberately different from AShipActor's locally-simulated-from-replicated-
 * input approach - that pattern exists to avoid jitter for a large actor
 * characters stand on, which doesn't apply to a small, short-lived,
 * fire-and-forget projectile like this one.
 *
 * CollisionComponent (a USphereComponent) is the root so there's something to
 * generate a hit event, same reasoning as AShipActor's HazardBox - the mesh
 * itself carries no collision (purely visual, same as ItemMesh/BarrelMesh/
 * MopMesh elsewhere in this codebase). OnHit always applies Damage to an
 * AEnemyShip it hits (see EnemyShip.h); additionally applies Damage to an
 * AShipActor it hits if bDamagesPlayerShip is true, or to an AShipCharacter
 * it hits if bDamagesPlayerCharacter is true - see those flags' comments for
 * why this is conditional (ACannon's own projectiles never set either, so a
 * player can never damage their own ship or another player; UEnemyGunComponent
 * sets exactly one of the two per shot, depending on the fired ammo type's
 * FCannonAmmoType::bTargetPlayer).
 *
 * Damage/ProjectileMesh's static mesh/FireSound/ImpactEffect/ImpactSound are
 * all set by ACannon::ServerFire_Implementation right after spawning (see
 * ConfigureAmmo), from whichever FCannonAmmoType is currently equipped - this
 * class carries sensible class defaults for all five (so it still behaves
 * reasonably if ever spawned/placed without going through ACannon), but a
 * fired shot always overrides them. ProjectileMeshAsset/FireSound/
 * ImpactEffect/ImpactSound are Replicated (unlike Damage, which only the
 * server's own OnHit logic ever reads) since every machine needs to render
 * the right mesh and independently play the right sound/effect - see
 * ConfigureAmmo/OnRep_ProjectileMeshAsset.
 */
UCLASS()
class SHIPGAME_API ACannonProjectile : public AActor
{
	GENERATED_BODY()

public:
	ACannonProjectile();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Projectile")
	USphereComponent* CollisionComponent;

	// Visual only - no collision, see the class comment. Its static mesh
	// asset is normally set by ConfigureAmmo (see ProjectileMeshAsset)
	// rather than authored here, but this can still carry a fallback mesh
	// in the Blueprint for when it isn't.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Projectile")
	UStaticMeshComponent* ProjectileMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Projectile")
	UProjectileMovementComponent* ProjectileMovement;

	// How long this projectile lives before self-destructing if it never hits
	// anything - set via SetLifeSpan in BeginPlay rather than relying on
	// something else to clean it up.
	UPROPERTY(EditDefaultsOnly, Category = "Projectile")
	float LifeSpanSeconds = 5.f;

	// Damage applied to an AEnemyShip this projectile hits, before
	// destroying itself - see OnHit. Not replicated: only the server's own
	// OnHit call (guarded by HasAuthority()) ever reads this.
	UPROPERTY(EditDefaultsOnly, Category = "Projectile")
	float Damage = 25.f;

	// Sets Damage/ProjectileMeshAsset/FireSound/ImpactEffect/ImpactSound from
	// the fired ammo type's data - called by ACannon::ServerFire_Implementation
	// between SpawnActorDeferred and FinishSpawning, so these are all in
	// place before this projectile's own BeginPlay runs (see the class
	// comment on ACannon for why that ordering matters). Server-only in
	// practice (only ever called from ServerFire_Implementation), but does
	// nothing authority-specific itself.
	void ConfigureAmmo(float InDamage, UStaticMesh* InProjectileMesh, USoundBase* InFireSound, UNiagaraSystem* InImpactEffect, USoundBase* InImpactSound);

	// Whether this projectile can damage the player's AShipActor on hit -
	// see OnHit. Defaults false (ACannon's own projectiles never set this,
	// preserving the existing "no self-damage against your own ship" rule).
	// UEnemyGunComponent::FireAtTarget sets exactly one of this or
	// bDamagesPlayerCharacter true right after spawning its own projectiles
	// (depending on the fired ammo's FCannonAmmoType::bTargetPlayer), since
	// those are hostile shots. Set directly by the firer (not part of
	// ConfigureAmmo - this is about who fired the shot and at what kind of
	// target, not what ammo type it is), and never needs to be replicated:
	// only the server's own OnHit branch (guarded by HasAuthority()) ever
	// reads it.
	bool bDamagesPlayerShip = false;

	// Whether this projectile can damage a player's AShipCharacter (their
	// Health) on hit - see OnHit. Same defaults-false/set-by-the-firer/
	// never-replicated reasoning as bDamagesPlayerShip - see its comment.
	// Set true instead of bDamagesPlayerShip specifically for
	// UEnemyGunComponent shots fired at a bTargetPlayer ammo type's visible
	// player target, so those shots damage the targeted player instead of
	// the ship even if they happen to hit the hull.
	bool bDamagesPlayerCharacter = false;

protected:
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// Damages an AEnemyShip hit (see AEnemyShip::ApplyDamage), an AShipActor
	// hit if bDamagesPlayerShip is true (see AShipActor::ApplyDamage), or an
	// AShipCharacter hit if bDamagesPlayerCharacter is true (see
	// AShipCharacter::ApplyDamage) - and spawns/plays ImpactEffect/
	// ImpactSound at the impact point/normal, then destroys the projectile
	// on impact regardless of what it hit. The ImpactEffect/ImpactSound and
	// collision-disable happen unconditionally
	// (every machine plays its own local cosmetic effect/sound and stops
	// this instance generating further hits as soon as it locally detects
	// one), but Destroy()/ApplyDamage stay Server-only (guarded with
	// HasAuthority()) since only the server's Destroy() actually replicates
	// to every client, and ApplyDamage is itself server-authoritative-
	// assuming - clients also run this component's hit event locally (their
	// own ProjectileMovementComponent simulates the same motion, see the
	// class comment) but must not act on the gameplay side of it themselves.
	UFUNCTION()
	void OnHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

	// Applies ProjectileMeshAsset to ProjectileMesh whenever it changes -
	// shared by ConfigureAmmo (direct call, since OnRep never fires locally
	// on the machine that made the change - i.e. the server) and this
	// callback (fires on clients once the replicated value arrives).
	UFUNCTION()
	void OnRep_ProjectileMeshAsset();

	// Mesh asset this projectile should display - see ConfigureAmmo/
	// OnRep_ProjectileMeshAsset. Kept separate from ProjectileMesh (the
	// component) since a UStaticMeshComponent's assigned mesh isn't itself
	// something the engine replicates.
	UPROPERTY(ReplicatedUsing = OnRep_ProjectileMeshAsset)
	UStaticMesh* ProjectileMeshAsset = nullptr;

	// Played once in BeginPlay - see ConfigureAmmo.
	UPROPERTY(Replicated)
	USoundBase* FireSound = nullptr;

	// Spawned in OnHit - see ConfigureAmmo.
	UPROPERTY(Replicated)
	UNiagaraSystem* ImpactEffect = nullptr;

	// Played in OnHit - see ConfigureAmmo.
	UPROPERTY(Replicated)
	USoundBase* ImpactSound = nullptr;
};
