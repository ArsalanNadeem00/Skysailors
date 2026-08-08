#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ShipActor.generated.h"

class UStaticMeshComponent;
class UPrimitiveComponent;
class UBoxComponent;
class UCameraShakeBase;
class APlayerController;

/**
 * The ship the players spawn on/around. Moves forward at a constant speed.
 *
 * Movement is simulated identically on every machine (server AND clients),
 * driven by a small set of Replicated helm-input fields below, rather than by
 * relying on AActor's built-in replicated-transform to convey motion. That
 * built-in replication has no interpolation for a plain non-physics Actor
 * like this one - a simulated proxy just teleports straight to each new
 * snapshot (see AActor::PostNetReceiveLocationAndRotation) - which reads as
 * jitter, and is especially noticeable for rotation since a yaw snap swings
 * the whole hull (and anyone standing on/steering it) in one frame. Having
 * every client integrate the same constant-velocity/yaw-rate math locally
 * each tick keeps motion smooth regardless of network update rate.
 *
 * bReplicateMovement is deliberately left OFF (see the constructor) - with it
 * on, every client would also periodically get hard-snapped to the server's
 * (by-then-stale, latency-delayed) Transform snapshot, fighting the smooth
 * local simulation.
 *
 * That local simulation still needs to be kept from silently drifting away
 * from the server's over time, though - not for the ship's own sake, but
 * because standing characters use UCharacterMovementComponent's "moving
 * base" support, which reads their standing component's live *local*
 * transform on whichever machine is predicting a move. If a client's copy
 * has drifted from the server's (different frame times/float rounding,
 * accumulated over an entire match with nothing to correct it), the
 * server's authoritative re-simulation of that character's move disagrees
 * with what the client predicted, and the resulting correction is what
 * shows up as characters rubberbanding on deck. See ServerLocation/
 * ServerRotation and CorrectDriftFromServer below - the server replicates
 * its authoritative transform at NetUpdateFrequency, and clients
 * continuously nudge (never snap) toward it each tick, bounding drift to a
 * small, invisible, self-correcting error instead of letting it grow
 * unbounded.
 *
 * IMPORTANT: The root component (HazardBox) must be a
 * UPrimitiveComponent so MoveShip has something to sweep for collision each
 * tick. Everything else - ShipMesh, whatever other visual pieces make up the
 * hull, SpawnPoints - is attached beneath it, so it all rides along for free
 * whenever the root moves; none of those children are swept individually.
 * That's also what lets players standing anywhere on the ship (via
 * UCharacterMovementComponent's "moving base" support on whichever mesh
 * component their feet land on) automatically move with the ship, with no
 * manual attachment code - attachment propagates any component's move down
 * to its children automatically, root or not.
 */
UCLASS()
class SHIPGAME_API AShipActor : public AActor
{
	GENERATED_BODY()

public:
	AShipActor();

protected:
	virtual void BeginPlay() override;

public:
	virtual void Tick(float DeltaTime) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// Ship hull mesh. Attached to HazardBox (the RootComponent) below -
	// also a movement base players stand on, same as any other walkable
	// mesh attached to the ship.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship")
	UStaticMeshComponent* ShipMesh;

	// 4 deck spawn points, created as child scene components.
	// Reposition each one in the editor (select the component, move it in the
	// viewport) or in a Blueprint child class to the exact deck locations you want.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship")
	TArray<USceneComponent*> SpawnPoints;

	// Constant forward speed in cm/s (Unreal units). The ship always cruises
	// forward at this speed on autopilot, whether or not a player is at the
	// helm - steering only ever affects yaw, never throttle. Replicated so
	// clients know the correct speed if you ever change it at runtime (e.g.
	// via a server-side event) rather than only at BeginPlay.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Replicated, Category = "Ship|Movement")
	float ForwardSpeed = 200.f;

	// Yaw turn rate in degrees/sec at full A/D helm input.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship|Movement")
	float HelmYawRate = 12;

	// Ascend/descend speed in cm/s at full W/S helm input. Kept low - vertical
	// movement is meant to be slow and gradual, not a fast elevator.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship|Movement")
	float HelmVerticalSpeed = 40.f;

	// Pitch (degrees) the ship holds while ascending/descending (W/S held).
	// Eases back to level once released - see TiltRecoverySpeed.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship|Movement")
	float AscendDescendTiltAngle = 4.0f;

	// How long (seconds) the tilt takes to ease fully from level to its held
	// angle, or back from its held angle to level - a constant-rate ramp
	// (see MoveShip), not an exponential ease, so this is the actual
	// wall-clock time either transition takes rather than just an
	// approximation. Used both when easing into the held tilt and back to
	// level afterward, always starting from whatever the current tilt
	// already is - so pressing W/S again shortly after releasing continues
	// smoothly instead of snapping back through level first.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship|Movement")
	float TiltRecoveryDuration = 1.0f;

	// How quickly the effective yaw/vertical input eases toward the
	// replicated HelmYawInput/HelmVerticalInput target (higher = faster).
	// HelmYawInput/HelmVerticalInput only change value when a new network
	// update arrives, so without this the turn/ascend rate would snap
	// instantly from 0 to full the instant that update lands instead of
	// ramping - see CurrentYawInput/CurrentVerticalInput below.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship|Movement")
	float HelmInputSmoothingSpeed = 4.f;

	// Roll (degrees) the ship holds while turning (A/D held), banking into the
	// turn. Much more subtle than AscendDescendTiltAngle. Eases in/out the
	// same way, at TiltRecoverySpeed.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship|Movement")
	float TurnTiltAngle = 3.f;

	UFUNCTION(BlueprintPure, Category = "Ship")
	USceneComponent* GetSpawnPoint(int32 Index) const;

	// Invisible box, and the RootComponent (see the class comment above) -
	// the thing MoveShip actually sweeps for collision each tick. The ship
	// is really several separate static meshes (hull, deck, mast, etc.), no
	// one of which is a good collision proxy for the whole thing, so this
	// stands in for all of them for hazard purposes instead. Ignores Pawn
	// (see the constructor) so it never blocks or generates hits against
	// player characters - only ShipMesh/etc.'s own collision does that, same
	// as before. Default extent is a placeholder; resize/reposition per ship
	// variant in its Blueprint to roughly wrap that ship's silhouette.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship|Hull")
	UBoxComponent* HazardBox;

	// Ship's health. Starts at 100, drops by HullDamagePerCrash each time
	// HazardBox hits something tagged "crash" (see OnShipMeshHit).
	// Replicated read-only - only the server (in ApplyCrashDamage) ever
	// changes it.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category = "Ship|Hull")
	float HullIntegrity = 100.f;

	// Flat damage per crash - not scaled by impact speed/angle/anything else
	// yet, just a static number for now.
	UPROPERTY(EditDefaultsOnly, Category = "Ship|Hull")
	float HullDamagePerCrash = 5.f;

	// Minimum time (seconds) between damage applications - i.e. how long the
	// ship stays immune to further crash events (no damage, no camera shake,
	// no knockback) after one lands. HazardBox still collides/blocks
	// normally during this window (see MoveShip) - only the damage/shake/
	// knockback side effects are suppressed, via this same cooldown check in
	// ApplyCrashDamage. Without it, staying wedged against a crash-tagged
	// obstacle would otherwise re-trigger all of that every single tick
	// instead of once per actual crash.
	UPROPERTY(EditDefaultsOnly, Category = "Ship|Hull")
	float HullDamageCooldown = 4.f;

	// Multiplier applied to the ship's own length (HazardBox's box extent
	// along its local forward/X axis) to get how far it's knocked back along
	// its -forward vector when it takes crash damage - e.g. the default of 2
	// knocks the ship back by twice its own length. Scaling off the ship's
	// actual size (rather than a fixed distance) means bigger ship variants
	// automatically get a proportionally bigger knockback. See
	// StartCrashKnockback.
	UPROPERTY(EditDefaultsOnly, Category = "Ship|Hull")
	float CrashKnockbackLengthMultiplier = 0.5f;

	// How long (seconds) the knockback takes to play out - eased over time
	// in MoveShip as a temporary backward velocity added on top of the
	// normal forward cruise, rather than applied as one instant jump. See
	// StartCrashKnockback/KnockbackTimeRemaining.
	UPROPERTY(EditDefaultsOnly, Category = "Ship|Hull")
	float CrashKnockbackDuration = 1.f;

	// Played on every player's active camera (via their PlayerCameraManager)
	// when the ship takes crash damage, regardless of whether that player is
	// currently steering the ship or not. Assign a camera shake Blueprint.
	UPROPERTY(EditDefaultsOnly, Category = "Ship|Hull")
	TSubclassOf<UCameraShakeBase> CrashCameraShake;

	// How long the screen fade to black takes once HullIntegrity reaches 0,
	// before the game closes. Placeholder "ship destroyed" behavior - see
	// MulticastOnHullDestroyed.
	UPROPERTY(EditDefaultsOnly, Category = "Ship|Hull")
	float DestroyedFadeOutDuration = 2.f;

	// Called by ASteeringWheel when a player takes/releases the helm. While
	// controlled, MoveShip also applies HelmYawInput/HelmVerticalInput on
	// top of the unchanged forward autopilot.
	void SetHelmControlled(bool bControlled);

	// Called by ASteeringWheel each time it receives Move input. Clamped to
	// [-1,1] (A/D). There is no player throttle control - the ship's forward
	// speed is always the constant autopilot ForwardSpeed.
	void SetHelmYawInput(float YawInput);

	// Called by ASteeringWheel each time it receives Move input. Clamped to
	// [-1,1] (W = ascend/+1, S = descend/-1). Triggers the momentary tilt kick
	// whenever vertical movement starts or reverses direction.
	void SetHelmVerticalInput(float VerticalInput);

protected:
	// Advances the ship's position/rotation each tick. Runs on every machine
	// (see the class comment above) - only the server's call actually sweeps
	// for collision; clients move the same Delta purely for smooth visuals.
	void MoveShip(float DeltaTime);

	// Written only by the server (via SetHelmControlled/SetHelmYawInput/
	// SetHelmVerticalInput, called from ASteeringWheel, itself only ever
	// running server-side logic). Replicated so every client can run the same
	// MoveShip() math locally each tick instead of relying on snapshot-and-
	// teleport transform replication for motion.
	UPROPERTY(Replicated)
	bool bHelmControlled = false;

	UPROPERTY(Replicated)
	float HelmYawInput = 0.f;

	UPROPERTY(Replicated)
	float HelmVerticalInput = 0.f;

	// Current ascend/descend tilt (pitch, degrees) and turn tilt (roll,
	// degrees), each easing toward its target every tick in MoveShip. Not
	// replicated - every machine derives the same values locally from the
	// replicated helm inputs above.
	float CurrentTiltPitch = 0.f;
	float CurrentTurnTiltRoll = 0.f;

	// Smoothed versions of HelmYawInput/HelmVerticalInput, eased toward the
	// raw replicated value (zero while not helm-controlled) every tick in
	// MoveShip - see HelmInputSmoothingSpeed. Not replicated - every machine
	// derives the same values locally, same pattern as the tilt values above.
	float CurrentYawInput = 0.f;
	float CurrentVerticalInput = 0.f;

	// Direction/speed/time-left of an in-progress crash knockback, set by
	// StartCrashKnockback and consumed a slice at a time in MoveShip (added
	// on top of the normal forward-cruise Delta as a temporary backward
	// velocity) until KnockbackTimeRemaining reaches zero. Not replicated -
	// StartCrashKnockback runs on every machine via MulticastOnCrash, same
	// pattern as the tilt/input values above.
	FVector KnockbackDirection = FVector::ZeroVector;
	float KnockbackSpeed = 0.f;
	float KnockbackTimeRemaining = 0.f;

	// The server's authoritative root (HazardBox) transform, replicated
	// at NetUpdateFrequency purely so clients can correct drift - NOT used
	// to drive primary motion (that stays local; see the class comment
	// above).
	UPROPERTY(ReplicatedUsing = OnRep_ServerTransform)
	FVector ServerLocation;

	UPROPERTY(Replicated)
	FRotator ServerRotation;

	// How fast clients close the gap to ServerLocation/ServerRotation, as an
	// FInterpTo speed (same style as HelmInputSmoothingSpeed/
	// TiltRecoverySpeed above) - deliberately gentle so the correction reads
	// as a continuous nudge, never a snap.
	UPROPERTY(EditDefaultsOnly, Category = "Ship|Movement")
	float DriftCorrectionSpeed = 2.f;

	// True once at least one ServerLocation/ServerRotation update has
	// actually arrived. Guards against correcting toward the (0,0,0)
	// pre-replication default the instant a client joins, which would yank
	// the ship toward the origin for a frame.
	bool bHasServerTransform = false;

	UFUNCTION()
	void OnRep_ServerTransform();

	// Nudges (never snaps) a client's locally-simulated root transform
	// toward the latest server snapshot. Client-only - the server's own
	// root transform is already the authority. See the class comment above
	// for why this is necessary at all.
	void CorrectDriftFromServer(float DeltaTime);

	// Bound to HazardBox's OnComponentHit. Only the server sweeps for
	// collision (see MoveShip), so this only ever meaningfully fires there -
	// applies damage if the other actor/component is tagged "crash".
	UFUNCTION()
	void OnShipMeshHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

	// Server-only. Applies HullDamagePerCrash (subject to HullDamageCooldown),
	// tells every client to shake their camera, and - if that brings
	// HullIntegrity to 0 - tells every client to fade out and quit.
	void ApplyCrashDamage();

	// Plays CrashCameraShake on every player's local PlayerCameraManager
	// (regardless of what pawn each player currently controls) and starts
	// the knockback (see StartCrashKnockback). Runs on every machine, same
	// as MoveShip/CorrectDriftFromServer - the server sweeps (it's
	// authoritative over collision), clients just move to match since they
	// aren't.
	UFUNCTION(NetMulticast, Reliable)
	void MulticastOnCrash();

	// Sets KnockbackDirection/KnockbackSpeed/KnockbackTimeRemaining so
	// MoveShip starts easing the ship backward by
	// CrashKnockbackLengthMultiplier * the ship's own length, over
	// CrashKnockbackDuration seconds. Called from MulticastOnCrash, so every
	// machine starts its own local knockback at the same moment.
	void StartCrashKnockback();

	// Fades every player's screen to black, then quits their game.
	// Placeholder "ship destroyed" behavior - see DestroyedFadeOutDuration.
	UFUNCTION(NetMulticast, Reliable)
	void MulticastOnHullDestroyed();

	// Timer callback for MulticastOnHullDestroyed - quits the game for the
	// specific local PlayerController the fade-out timer was started for.
	void QuitGameForPlayer(APlayerController* PC);

	// World time (seconds) HullDamageCooldown is measured from - see
	// ApplyCrashDamage. Server-only.
	double LastCrashDamageTime = TNumericLimits<double>::Lowest();

	// True once HullIntegrity has reached 0, so further collisions afterward
	// don't re-trigger the destroyed sequence. Server-only.
	bool bHullDestroyed = false;
};
