#pragma once

#include "CoreMinimal.h"
#include "MountableItem.h"
#include "AmmoType.h"
#include "Cannon.generated.h"

class UCameraComponent;
class UStaticMeshComponent;
class USceneComponent;
class UInputAction;
class UCameraShakeBase;
class ACannonProjectile;
struct FInputActionValue;

// Fired whenever anything about the ammo state UCannonWidget displays
// changes - either which AmmoTypes entry is equipped (CurrentAmmoTypeIndex)
// or the equipped entry's own data (e.g. AmmoCount decrementing on fire) -
// see ACannon::OnRep_CurrentAmmoTypeIndex/OnRep_AmmoTypes/ServerFire/
// ServerCycleAmmo. No parameters: listeners (e.g. UCannonWidget) are
// expected to just re-read AmmoTypes/CurrentAmmoTypeIndex themselves rather
// than have the new state pushed through the delegate call.
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnAmmoStateChanged);

/**
 * Mountable ship weapon - operable in place, same TryEngage/possession
 * pattern as ASteeringWheel: a character interacts with a mounted (AMount-
 * attached) cannon to hand control of their PlayerController over to this
 * Pawn. CannonCamera becomes the active view automatically once possessed
 * (APawn::CalcCamera finds it, same as ASteeringWheel::HelmCamera), and
 * interacting again hands control back to the character that engaged it.
 * Only one controller can possess this Pawn at a time (enforced by
 * TryEngage), which is what guarantees only one player can operate a given
 * cannon at once.
 *
 * Aiming (W/S/A/D only - no mouse) reuses the same "replicate the raw
 * input, simulate identically on every machine" pattern as AShipActor's
 * helm (see its class comment) rather than replicating the resulting
 * rotation: ServerSetAimInput authoritatively writes AimYawInput/
 * AimPitchInput, and Tick integrates those into AimYaw/AimPitch (clamped to
 * +/-MaxAimAngleDegrees).
 *
 * Aiming only ever rotates CannonCamera, never this actor - ItemMesh
 * (inherited from AMountableItem, this actor's RootComponent) is the
 * cannon's fixed "body" and stays exactly at whatever rotation
 * AMount::AttachItem's facing-arrow alignment set it to (see Mount.cpp);
 * only CannonCamera (and BarrelMesh, rigidly attached to it) swivels to
 * aim. This is also why Tick recomputes CannonCamera's relative rotation
 * from scratch each time (RestCameraRotation + the clamped AimPitch/AimYaw
 * offset) instead of nudging it with an incremental AddLocalRotation: this
 * class used to rotate the whole actor that way, but repeatedly composing
 * small local pitch+yaw rotations is not path-independent (rotation
 * composition doesn't commute), so it visibly rolled the cannon onto its
 * side over time. Recomputing an absolute rotation with a fixed zero roll
 * every tick can't drift, no matter how long or how erratically the player
 * aims.
 *
 * Firing (left mouse click / FireAction) follows the same "client requests,
 * server is authoritative" shape as everything else here: Fire() forwards to
 * ServerFire, which enforces the current ammo type's FireCooldown (via
 * LastFireTime) and, if not still on cooldown, spawns ProjectileClass at
 * MuzzleLocation, configured with that ammo type's damage/mesh/sound/impact
 * effect (see ConfigureAmmo/AmmoTypes below). Unlike the swing/tool-use-action
 * cosmetics elsewhere in this codebase, firing doesn't need a NetMulticast:
 * the projectile itself is a normal replicated actor (see ACannonProjectile),
 * so spawning it once on the server is sufficient for every client to see it -
 * there's no separate "everyone plays this cosmetic" step required. The one
 * exception is each ammo type's FireCameraShake: unlike AShipActor's crash
 * shake (which every player feels, since it's the whole ship jolting), a
 * fired shot should only shake the operator's own camera - so
 * ServerFire_Implementation targets ClientPlayFireCameraShake (now taking the
 * shake class as a parameter, since it varies per ammo type) at just this
 * cannon's possessing controller (a Client RPC, not a NetMulticast) rather
 * than looping every PlayerController the way AShipActor::MulticastOnCrash
 * does.
 *
 * AmmoTypes (see FCannonAmmoType) is a data-driven list of "kinds of shot"
 * this cannon can fire, same "Blueprint fills in the data, C++ just reads it"
 * philosophy as AShipCharacter::ToolDefinitions - each entry carries its own
 * fire rate, damage, projectile mesh, muzzle camera shake, fire sound/impact
 * effect/sound, display name/icon, and ammo count (AmmoCount/MaxAmmoCount/
 * bInfiniteAmmo - see FCannonAmmoType). ServerFire_Implementation refuses to
 * fire an ammo type that's out of ammo (AmmoCount <= 0 and not
 * bInfiniteAmmo), and decrements AmmoCount by 1 on every shot that isn't
 * bInfiniteAmmo - reloading isn't implemented yet, so this is currently a
 * one-way depletion.
 *
 * CurrentAmmoTypeIndex picks which AmmoTypes entry is currently equipped -
 * unlike AimYawInput/AimPitchInput (which integrate every tick, so
 * replicating the raw input is enough), switching ammo is a one-shot
 * discrete change, so this follows the more standard "client requests,
 * server RPC writes the Replicated value directly" shape instead: ScrollUp/
 * DownAction (mouse wheel) call CycleAmmoNext/Previous, which forward to
 * ServerCycleAmmo, which wraps CurrentAmmoTypeIndex by +/-1 through
 * AmmoTypes. Both CurrentAmmoTypeIndex and AmmoTypes itself are
 * ReplicatedUsing OnRep_CurrentAmmoTypeIndex/OnRep_AmmoTypes respectively
 * (the latter replicates as a whole array - fine at this list's expected
 * size, no need for per-element delta replication) - both OnReps broadcast
 * the same OnAmmoStateChanged delegate, which the operator's UCannonWidget
 * binds to so it knows when to refresh its ammo list UI, rather than polling
 * every tick (see UCannonWidget's class comment). ServerFire_Implementation/
 * ServerCycleAmmo_Implementation also broadcast OnAmmoStateChanged directly
 * after writing CurrentAmmoTypeIndex/AmmoTypes, since an OnRep never fires
 * locally on whichever machine made the change - only relevant for a
 * listen-server host operating their own cannon, since every other client
 * gets it via the OnRep.
 */
UCLASS()
class SHIPGAME_API ACannon : public AMountableItem
{
	GENERATED_BODY()

public:
	ACannon();

	// The fixed "body" (ItemMesh, inherited) never rotates for aiming - only
	// this camera does, and BarrelMesh (rigidly attached below) rides along
	// with it, so the visible barrel swivels while the body stays put.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cannon")
	UCameraComponent* CannonCamera;

	// Visual barrel/turret-head piece that aims along with CannonCamera -
	// child of the camera, not of ItemMesh, specifically so it moves with
	// aiming instead of staying fixed to the body. No collision, same as
	// ItemMesh/MopMesh/WrenchMesh elsewhere in this codebase - purely a
	// visual prop. The actual mesh is only ever real in the Blueprint.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cannon")
	UStaticMeshComponent* BarrelMesh;

	// Where fired projectiles spawn from and aim along - child of BarrelMesh
	// (not ItemMesh) specifically so it swivels with aiming the same way
	// BarrelMesh itself does. Position it at the barrel's muzzle tip in the
	// Blueprint.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cannon")
	USceneComponent* MuzzleLocation;

	// Reuses the same 2D move action characters use: X (A/D) drives aim
	// yaw, Y (W/S) drives aim pitch - same reuse-the-shared-action idiom as
	// ASteeringWheel::MoveAction, just interpreted as aiming instead of
	// steering.
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* MoveAction;

	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* InteractAction;

	// Fire-action Input Action (default: left mouse click) - fires a shot
	// from the cannon. Bound like MoveAction/InteractAction, only active
	// while this Pawn is possessed.
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* FireAction;

	// Scroll-up/down Input Actions (default: mouse wheel up/down) - cycle to
	// the next/previous entry in AmmoTypes. Bound like FireAction, only
	// active while this Pawn is possessed.
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* ScrollUpAction;

	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* ScrollDownAction;

	// Projectile spawned by Fire - assign a Blueprint subclass of
	// ACannonProjectile. Shared across every ammo type (see AmmoTypes) -
	// only the projectile's mesh/damage/sound/effect vary per ammo type, not
	// its class.
	UPROPERTY(EditDefaultsOnly, Category = "Cannon")
	TSubclassOf<ACannonProjectile> ProjectileClass;

	// The kinds of shot this cannon can fire - see FCannonAmmoType. Fill in
	// at least one entry in the Blueprint, or ServerFire_Implementation has
	// nothing to fire. BlueprintReadOnly so UCannonWidget's Blueprint
	// subclass can read it to build the ammo list UI. Replicated as a whole
	// array (see the class comment) since AmmoCount changes at runtime.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, ReplicatedUsing = OnRep_AmmoTypes, Category = "Cannon")
	TArray<FCannonAmmoType> AmmoTypes;

	// Which AmmoTypes entry is currently equipped - see the class comment
	// for the switching flow (CycleAmmoNext/Previous -> ServerCycleAmmo) and
	// replication (OnRep_CurrentAmmoTypeIndex/OnAmmoStateChanged).
	// EditAnywhere still sets the starting value. BlueprintReadOnly for the
	// same reason as AmmoTypes.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_CurrentAmmoTypeIndex, Category = "Cannon")
	int32 CurrentAmmoTypeIndex = 0;

	// Fired whenever CurrentAmmoTypeIndex or AmmoTypes (e.g. AmmoCount)
	// changes - see the class comment. UCannonWidget binds to this to know
	// when to refresh its ammo list UI.
	UPROPERTY(BlueprintAssignable, Category = "Cannon")
	FOnAmmoStateChanged OnAmmoStateChanged;

	// AmmoTypes[CurrentAmmoTypeIndex], or nullptr if that index isn't valid
	// (e.g. AmmoTypes is empty) - ServerFire_Implementation no-ops rather
	// than firing with made-up defaults if this returns null. Non-const
	// (returns a mutable pointer) since ServerFire_Implementation needs to
	// decrement the returned entry's AmmoCount. Plain C++ (not a UFUNCTION)
	// - a raw pointer to a USTRUCT isn't a type Blueprint can use, and this
	// is only ever called from ServerFire_Implementation.
	FCannonAmmoType* GetCurrentAmmoType() { return AmmoTypes.IsValidIndex(CurrentAmmoTypeIndex) ? &AmmoTypes[CurrentAmmoTypeIndex] : nullptr; }

	// Max degrees off CannonCamera's rest orientation AimYaw/AimPitch can
	// reach in either direction.
	UPROPERTY(EditDefaultsOnly, Category = "Cannon")
	float MaxAimAngleDegrees = 35.f;

	// Degrees/second AimYaw/AimPitch move while a direction is held.
	UPROPERTY(EditDefaultsOnly, Category = "Cannon")
	float AimRotationSpeed = 60.f;

	UFUNCTION(BlueprintPure, Category = "Cannon")
	bool IsOccupied() const { return GetController() != nullptr; }

	// Called by a character trying to operate this cannon. Returns false if
	// someone's already operating it, or it isn't currently attached to an
	// AMount (see AMount::AttachItem) - operating only makes sense for a
	// mounted cannon, not one sitting in a character's hand. Server-only -
	// the caller is expected to have already routed this through a Server
	// RPC.
	bool TryEngage(AController* InstigatingController, APawn* InstigatingPawn);

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void UnPossessed() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	void Aim(const FInputActionValue& Value);
	void StopAim();
	void Interact();
	void Fire();

	// Scroll-up/down input handlers - forward to ServerCycleAmmo with +1/-1
	// respectively. Client-side entry points, same role as Fire()/Interact().
	void CycleAmmoNext();
	void CycleAmmoPrevious();

	UFUNCTION(Server, Reliable)
	void ServerSetAimInput(float YawInput, float PitchInput);

	UFUNCTION(Server, Reliable)
	void ServerReleaseCannon();

	// Authoritative fire handler - enforces FireCooldown via LastFireTime and
	// the current ammo type's ammo count (see FCannonAmmoType), then spawns
	// ProjectileClass at MuzzleLocation's current world transform. No
	// cosmetic NetMulticast needed - see the class comment for why the
	// projectile's own replication is sufficient.
	UFUNCTION(Server, Reliable)
	void ServerFire();

	// Authoritative ammo-switch handler - wraps CurrentAmmoTypeIndex by
	// Direction (expected to always be +1 or -1) through AmmoTypes. No-ops
	// if AmmoTypes is empty. Called from CycleAmmoNext/Previous.
	UFUNCTION(Server, Reliable)
	void ServerCycleAmmo(int32 Direction);

	// Broadcasts OnAmmoStateChanged - see the class comment.
	UFUNCTION()
	void OnRep_CurrentAmmoTypeIndex();

	// Broadcasts OnAmmoStateChanged - see the class comment.
	UFUNCTION()
	void OnRep_AmmoTypes();

	// Plays ShakeClass (the fired ammo type's FireCameraShake) on just this
	// cannon's possessing controller's camera - called from
	// ServerFire_Implementation once a shot actually fires (never for a shot
	// rejected by the cooldown). A Client RPC, not a NetMulticast, since only
	// the operator should feel this shake - see the class comment. Takes the
	// shake class as a parameter (rather than reading a fixed member) since
	// it now varies per ammo type.
	UFUNCTION(Client, Reliable)
	void ClientPlayFireCameraShake(TSubclassOf<UCameraShakeBase> ShakeClass);

	// Raw held-input values (-1..1), authoritative on the server and
	// replicated so Tick can integrate identically on every machine - see
	// the class comment for why this mirrors AShipActor's helm input
	// pattern.
	UPROPERTY(Replicated)
	float AimYawInput = 0.f;

	UPROPERTY(Replicated)
	float AimPitchInput = 0.f;

	// Accumulated degrees off CannonCamera's rest orientation, clamped to
	// +/-MaxAimAngleDegrees - tracked locally on every machine (not
	// replicated), applied on top of RestCameraRotation each Tick.
	float AimYaw = 0.f;
	float AimPitch = 0.f;

	// CannonCamera's relative rotation as authored in the Blueprint,
	// captured once in BeginPlay (after any construction-script edits have
	// applied) so Tick/UnPossessed can add/restore the aim offset without
	// clobbering whatever look angle the Blueprint set up.
	FRotator RestCameraRotation = FRotator::ZeroRotator;

	// The character (or other pawn) to hand control back to when this
	// cannon is released - same role as ASteeringWheel::PreviousPawn.
	UPROPERTY()
	APawn* PreviousPawn;

	// Server-only: GetWorld()->GetTimeSeconds() as of the last shot that
	// actually fired, used by ServerFire_Implementation to enforce
	// FireCooldown. Never replicated - clients have no need to know it, a
	// spammed fire input is just silently rejected server-side.
	float LastFireTime = -MAX_FLT;
};
