#pragma once

#include "CoreMinimal.h"
#include "Components/StaticMeshComponent.h"
#include "AmmoType.h"
#include "EnemyGunComponent.generated.h"

class AShipActor;
class ACannonProjectile;

/**
 * Autonomous turret - placed directly on an AEnemyShip (in the Blueprint's
 * Components panel, one instance per gun mount, individually positioned/
 * rotated - same workflow as UShipBreakComponent on AShipActor) rather than
 * as a separate actor. IS the gun's own visual mesh, same as
 * UShipBreakComponent IS its Niagara effect - there's no separate mesh
 * component. This component's own forward vector (whatever way it's rotated
 * in the Blueprint) is the direction it faces/scans/fires along - there's no
 * separate "facing direction" property to keep in sync with the mesh.
 *
 * Every tick, server-only (see TickComponent - nothing here has any purpose
 * on a non-authoritative machine, since firing spawns a replicated actor and
 * there's no separate cosmetic tied to scanning itself), checks whether
 * GetTargetShip() (the player's AShipActor, read from the owning AEnemyShip's
 * own already-resolved ControlledShip - see GetTargetShip) is within
 * FiringRange and FiringConeHalfAngleDegrees of this component. The cone
 * check is purely an engagement gate ("is the target roughly where this
 * turret already points, so it's worth engaging at all") - once engaged, the
 * actual shot (see FireAtTarget) is aimed directly at the target's current
 * position, not fired blindly along this component's fixed facing, or most
 * shots would miss anything not dead-centered in a (by default) 45-degree
 * cone. No leading (see AEnemyShip::ComputeLeadTarget for what that would
 * look like) - not requested, and would need this component to know the
 * target ship's velocity the same way AEnemyShip does.
 *
 * AmmoType (see FCannonAmmoType, shared with ACannon) drives fire rate,
 * damage, projectile mesh, and fire/impact sound/effect, same "data change,
 * not a code change" philosophy as ACannon::AmmoTypes - just a single value
 * here rather than a switchable list, since there's no player to switch
 * ammo. FireAtTarget mirrors ACannon::ServerFire_Implementation closely
 * (ammo-count gate/decrement, deferred spawn + ConfigureAmmo before
 * FinishSpawning) but marks the spawned projectile
 * ACannonProjectile::bDamagesPlayerShip = true (ACannon never does - see
 * that flag's comment for why) and has no camera shake to trigger (no
 * operator).
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class SHIPGAME_API UEnemyGunComponent : public UStaticMeshComponent
{
	GENERATED_BODY()

public:
	UEnemyGunComponent();

	// Ammo this gun fires - see FCannonAmmoType.
	UPROPERTY(EditDefaultsOnly, Category = "Enemy Gun")
	FCannonAmmoType AmmoType;

	// Projectile class this gun spawns - shared across whatever AmmoType is
	// configured, same relationship as ACannon::ProjectileClass to
	// ACannon::AmmoTypes.
	UPROPERTY(EditDefaultsOnly, Category = "Enemy Gun")
	TSubclassOf<ACannonProjectile> ProjectileClass;

	// Max distance (cm) to the player ship this gun will engage at.
	UPROPERTY(EditDefaultsOnly, Category = "Enemy Gun")
	float FiringRange = 2000.f;

	// Half-angle (degrees) of the engagement cone, centered on this
	// component's own forward vector - see the class comment.
	UPROPERTY(EditDefaultsOnly, Category = "Enemy Gun")
	float FiringConeHalfAngleDegrees = 45.f;

protected:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// The player ship to engage - GetOwner() cast to AEnemyShip, then that
	// ship's own ControlledShip (already resolved once in AEnemyShip::
	// BeginPlay - see its comment). Every gun on the same ship shares this
	// single lookup rather than each independently re-deriving it. Returns
	// nullptr if GetOwner() isn't an AEnemyShip, or that ship hasn't
	// resolved ControlledShip yet - this gun just doesn't fire until it has.
	AShipActor* GetTargetShip() const;

	// True if TargetShip is within FiringRange and FiringConeHalfAngleDegrees
	// of this component's current position/forward vector - see the class
	// comment.
	bool IsTargetInFiringArc(const AShipActor* TargetShip) const;

	// Fires one shot at TargetShip's current position if AmmoType's cooldown
	// and ammo count (see FCannonAmmoType) allow it - mirrors ACannon::
	// ServerFire_Implementation, see the class comment for the differences.
	void FireAtTarget(const AShipActor* TargetShip);

	// Server-only: GetWorld()->GetTimeSeconds() as of the last shot that
	// actually fired - same role as ACannon::LastFireTime.
	float LastFireTime = -MAX_FLT;
};
