#pragma once

#include "CoreMinimal.h"
#include "Components/StaticMeshComponent.h"
#include "AmmoType.h"
#include "EnemyGunComponent.generated.h"

class AShipActor;
class AShipCharacter;
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
 * there's no separate cosmetic tied to scanning itself), picks a target and
 * engages it if one's found:
 *
 * - AmmoType.bTargetPlayer == false (the default): targets the player ship
 *   (GetTargetShip() - the owning AEnemyShip's own already-resolved
 *   ControlledShip), gated by IsLocationInFiringArc (FiringRange +
 *   FiringConeHalfAngleDegrees around this component's forward vector).
 * - AmmoType.bTargetPlayer == true: targets a player CHARACTER instead (see
 *   FindVisiblePlayerTarget) - the first character within the same firing
 *   arc AND with line of sight (HasLineOfSightTo) from this component, i.e.
 *   actually visible, not just "in the right direction," whose player is
 *   currently active (AShipCharacter::IsEffectivelyPlayerControlled - either
 *   directly walking around, or piloting a wheel/cannon that character
 *   engaged, in which case the character's own body is simply left standing
 *   wherever it was when they got in, and is what actually gets aimed at/
 *   damaged - see that function's comment).
 *
 * Either way, the cone check is purely an engagement gate ("is the target
 * roughly where this turret already points, so it's worth engaging at all")
 * - once engaged, the actual shot (see FireAtTarget) is aimed at
 * PredictTargetLocation(Target), not fired blindly along this component's
 * fixed facing, or most shots would miss anything not dead-centered in a (by
 * default) 45-degree cone. For a bTargetPlayer shot, that prediction leads
 * ahead by however far GetTargetShip() (the ship the targeted player is
 * standing on) will travel during the shot's flight time - see
 * PredictTargetLocation - the same kind of correction AEnemyShip's own
 * ComputeLeadTarget applies when pursuing the ship itself, just against a
 * constant cruise velocity here rather than a curved orbit. A
 * non-bTargetPlayer shot has nothing to lead (GetTargetShip() IS the
 * target), so it's aimed at that ship's plain current position.
 *
 * AmmoType (see FCannonAmmoType, shared with ACannon) drives fire rate,
 * damage, projectile mesh, fire/impact sound/effect, and bTargetPlayer, same
 * "data change, not a code change" philosophy as ACannon::AmmoTypes - just a
 * single value here rather than a switchable list, since there's no player
 * to switch ammo. FireAtTarget mirrors ACannon::ServerFire_Implementation
 * closely (ammo-count gate/decrement, deferred spawn + ConfigureAmmo before
 * FinishSpawning) but marks the spawned projectile
 * ACannonProjectile::bDamagesPlayerShip or bDamagesPlayerCharacter = true
 * (exactly one, depending on AmmoType.bTargetPlayer - ACannon never sets
 * either - see those flags' comments for why) and has no camera shake to
 * trigger (no operator).
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

	// Max distance (cm) to a target this gun will engage at.
	UPROPERTY(EditDefaultsOnly, Category = "Enemy Gun")
	float FiringRange = 2000.f;

	// Half-angle (degrees) of the engagement cone, centered on this
	// component's own forward vector - see the class comment.
	UPROPERTY(EditDefaultsOnly, Category = "Enemy Gun")
	float FiringConeHalfAngleDegrees = 45.f;

protected:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// The player ship to engage while AmmoType.bTargetPlayer is false -
	// GetOwner() cast to AEnemyShip, then that ship's own ControlledShip
	// (already resolved once in AEnemyShip::BeginPlay - see its comment).
	// Every gun on the same ship shares this single lookup rather than each
	// independently re-deriving it. Returns nullptr if GetOwner() isn't an
	// AEnemyShip, or that ship hasn't resolved ControlledShip yet - this gun
	// just doesn't fire until it has.
	AShipActor* GetTargetShip() const;

	// The first player character to engage while AmmoType.bTargetPlayer is
	// true - scans AShipCharacter::GetActiveShipCharacters() (a static
	// registry, not a per-tick UGameplayStatics::GetAllActorsOfClass scan -
	// same reasoning/pattern as AEnemyShip::ActiveEnemyShips) for the first
	// character whose player is currently active
	// (AShipCharacter::IsEffectivelyPlayerControlled) that's both within
	// this gun's firing arc (IsLocationInFiringArc) and visible
	// (HasLineOfSightTo). Returns nullptr if none qualify.
	AShipCharacter* FindVisiblePlayerTarget() const;

	// True if TargetLocation is within FiringRange and
	// FiringConeHalfAngleDegrees of this component's current position/
	// forward vector - see the class comment. Takes a location rather than
	// an actor so the same check works for both GetTargetShip's AShipActor
	// and FindVisiblePlayerTarget's AShipCharacter candidates.
	bool IsLocationInFiringArc(const FVector& TargetLocation) const;

	// True if nothing blocks a line trace from this component to Target -
	// used by FindVisiblePlayerTarget to only ever engage a player character
	// this gun can actually see (e.g. not through the ship's own deck/hull),
	// unlike ship-targeting, which has no such check (the ship is a large,
	// generally-unmissable target, so it wasn't worth the extra trace).
	bool HasLineOfSightTo(const AActor* Target) const;

	// Fires one shot at PredictTargetLocation(Target) if AmmoType's cooldown
	// and ammo count (see FCannonAmmoType) allow it - mirrors ACannon::
	// ServerFire_Implementation, see the class comment for the differences.
	void FireAtTarget(const AActor* Target);

	// Where to actually aim this shot - Target's current position, led ahead
	// by however far the ship it's standing on will travel during this
	// shot's flight time, if AmmoType.bTargetPlayer (a player-targeting shot
	// aimed at a player who's simply standing wherever they are on
	// GetTargetShip()'s deck - riding along with it - is otherwise
	// consistently aiming behind a moving ship, exactly as stale as
	// AEnemyShip's own pre-lead-pursuit shots at the ship itself used to be).
	// Ship-targeting shots (AmmoType.bTargetPlayer false) are left exactly as
	// Target->GetActorLocation() - GetTargetShip() IS the target there, so
	// there's nothing to lead relative to.
	//
	// Uses ProjectileClass's own CDO to read ProjectileMovement->InitialSpeed
	// ahead of actually spawning anything, purely to get FlightTime = Distance
	// / InitialSpeed - a single-iteration estimate, not a re-solved fixed
	// point (the target's predicted distance changes slightly once you lead
	// it), but AShipActor::GetVelocity() is constant enough tick-to-tick that
	// re-solving wouldn't meaningfully change the result, and this exactly
	// mirrors the reasoning already used for ACannonProjectile's explicit
	// post-spawn Velocity assignment in FireAtTarget.
	FVector PredictTargetLocation(const AActor* Target) const;

	// Server-only: GetWorld()->GetTimeSeconds() as of the last shot that
	// actually fired - same role as ACannon::LastFireTime.
	float LastFireTime = -MAX_FLT;
};
