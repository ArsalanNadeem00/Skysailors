#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EnemyShip.generated.h"

class UBoxComponent;
class UStaticMeshComponent;
class UNiagaraSystem;
class AShipActor;

// Which target-seeking behavior UpdateFlightState currently runs - see the
// class comment for the overall approach/circle shape. A one-way ratchet:
// once Circling, never reverts to ApproachingFlank.
UENUM(BlueprintType)
enum class EEnemyShipState : uint8
{
	ApproachingFlank,
	Circling
};

/**
 * AI-flown hostile ship: approaches the player's AShipActor from the port or
 * starboard side, banks past it, then circles it - while steering around
 * obstacles and other AEnemyShip instances - and can be shot down by
 * ACannonProjectile.
 *
 * There's no NavMesh anywhere in this project (confirmed by search) and it
 * would be a poor fit for open 3D flight anyway - Recast NavMesh generates a
 * 2D-ish walkable surface, not a free 3D volume. The only other AI in this
 * codebase (Variant_Combat/Variant_SideScrolling's StateTree) is purely a
 * decision layer bolted onto hand-rolled CharacterMovementComponent walking,
 * never a movement/pathfinding primitive itself - and since obstacle/ship
 * avoidance here is naturally just extra steering forces blended in every
 * tick (see UpdateFlightState), not a discrete decision, StateTree wouldn't
 * add capability, only boilerplate. So, like AShipActor - the one existing
 * "autonomous ship" precedent in the actual game - this is a plain AActor
 * with steering math directly in Tick(), no AIController/Pawn/StateTree.
 *
 * Networking follows AShipActor's pattern exactly (see its class comment),
 * per this project's own repo-wide convention: replicate only the minimal
 * authoritative state (CurrentState, FlankSign, HullIntegrity), run the same
 * steering simulation locally on every machine each tick from that state,
 * and soft-correct drift via ServerLocation/ServerRotation rather than
 * relying on bReplicateMovement. Values that are also deterministically
 * re-derived every tick from that same small state (OrbitAngle, the per-tick
 * avoidance/separation vectors) are deliberately NOT replicated, exactly
 * like AShipActor::CurrentTiltPitch/CurrentYawInput aren't.
 */
UCLASS()
class SHIPGAME_API AEnemyShip : public AActor
{
	GENERATED_BODY()

public:
	AEnemyShip();

	// Root component, and the only thing swept for collision each tick (see
	// SteerTowards) - same role as AShipActor::HazardBox, including ignoring
	// the Pawn channel for the same reason (this doesn't need to block player
	// characters, only the world/other ships). Tagged "crash" in the
	// constructor so colliding into the player's AShipActor triggers its
	// existing hull-damage path (AShipActor::OnShipMeshHit) for free - no
	// changes needed there. Also what ACannonProjectile's BlockAllDynamic
	// collision profile already blocks, so no special setup is needed for
	// "can be hit by cannon fire" either.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy Ship")
	UBoxComponent* HazardBox;

	// Visual only, no collision - nobody stands on an enemy ship the way
	// players stand on AShipActor's deck, so unlike ShipMesh there this one
	// doesn't need to block anything itself.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy Ship")
	UStaticMeshComponent* ShipMesh;

	// The player ship this enemy attacks. Auto-found in BeginPlay (first
	// AShipActor in the level) if left unset, same as AMount/APuddle/
	// ASteeringWheel's ControlledShip.
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Enemy Ship")
	AShipActor* ControlledShip;

	// Constant forward flight speed in cm/s, same role as
	// AShipActor::ForwardSpeed.
	UPROPERTY(EditDefaultsOnly, Category = "Enemy Ship|Flight")
	float ForwardSpeed = 900.f;

	// Degrees/second SteerTowards can turn the ship's facing to chase the
	// current desired direction - the turn-rate limit is what keeps this a
	// smooth bank instead of an instant snap-to-face.
	UPROPERTY(EditDefaultsOnly, Category = "Enemy Ship|Flight")
	float TurnRateDegreesPerSec = 45.f;

	// How far abeam (cm, along the player ship's local right vector) the
	// approach target sits - see ComputeFlankTargetWorld.
	UPROPERTY(EditDefaultsOnly, Category = "Enemy Ship|Flank")
	float FlankLateralOffset = 800.f;

	// Forward/aft bias (cm, along the player ship's local forward vector) of
	// the approach target - 0 means dead abeam; positive biases the approach
	// toward the player ship's bow, negative toward its stern.
	UPROPERTY(EditDefaultsOnly, Category = "Enemy Ship|Flank")
	float FlankForwardOffset = 0.f;

	// Vertical offset (cm, along the player ship's local up vector) of the
	// approach target - lets a Blueprint variant attack from above/below.
	UPROPERTY(EditDefaultsOnly, Category = "Enemy Ship|Flank")
	float FlankHeightOffset = 0.f;

	// How far (cm) behind the player ship's origin (measured along the
	// player ship's own forward axis - see TryTransitionToCircling) this
	// enemy has to trail before ApproachingFlank gives way to Circling.
	UPROPERTY(EditDefaultsOnly, Category = "Enemy Ship|Flank")
	float FlankPastDistanceThreshold = 1500.f;

	// Radius (cm) of the circle flown around the player ship once Circling.
	UPROPERTY(EditDefaultsOnly, Category = "Enemy Ship|Circling")
	float OrbitRadius = 1200.f;

	// Vertical offset (cm) of the orbit circle's center above/below the
	// player ship's current location.
	UPROPERTY(EditDefaultsOnly, Category = "Enemy Ship|Circling")
	float OrbitHeightOffset = 0.f;

	// Degrees/second the orbit angle advances - see AdvanceOrbitAngle.
	// Multiplied by FlankSign there, so this is always a positive magnitude;
	// the actual direction flown depends on which side this ship approached
	// from.
	UPROPERTY(EditDefaultsOnly, Category = "Enemy Ship|Circling")
	float OrbitAngularSpeed = 30.f;

	// How far ahead (cm) ComputeAvoidanceDirection sweeps looking for
	// obstacles on the "ShipBlock" trace channel.
	UPROPERTY(EditDefaultsOnly, Category = "Enemy Ship|Avoidance")
	float AvoidanceTraceDistance = 2000.f;

	// Radius (cm) of the sphere swept for obstacle avoidance.
	UPROPERTY(EditDefaultsOnly, Category = "Enemy Ship|Avoidance")
	float AvoidanceTraceRadius = 350.f;

	// Steering weight applied to the avoidance direction at point-blank range
	// (see ComputeAvoidanceDirection - scales linearly to 0 at
	// AvoidanceTraceDistance). Deliberately large relative to
	// SeekWeight/SeparationWeight so avoidance dominates the blended steering
	// direction when something is close, without needing a hard priority
	// branch - see UpdateFlightState.
	UPROPERTY(EditDefaultsOnly, Category = "Enemy Ship|Avoidance")
	float BaseAvoidanceWeight = 4.f;

	// How close (cm) another AEnemyShip has to be before this one starts
	// steering away from it - see ComputeSeparationDirection.
	UPROPERTY(EditDefaultsOnly, Category = "Enemy Ship|Avoidance")
	float SeparationRadius = 1000.f;

	// Steering weight applied to the seek direction (toward the current
	// flank/orbit target) in the blended sum - see UpdateFlightState.
	UPROPERTY(EditDefaultsOnly, Category = "Enemy Ship|Avoidance")
	float SeekWeight = 1.f;

	// Steering weight applied to the separation direction in the blended sum.
	UPROPERTY(EditDefaultsOnly, Category = "Enemy Ship|Avoidance")
	float SeparationWeight = 1.f;

	// Caps how far ahead ComputeLeadTarget will project a moving target's
	// position, in seconds. This ship only ever thrusts forward at a fixed
	// ForwardSpeed with no way to slow down (see SteerTowards), so pursuing
	// a target that's also moving needs to aim ahead of it rather than at
	// its stale current position - see ComputeLeadTarget. Without a cap,
	// the naive Distance/ForwardSpeed time estimate that drives the lead
	// amount would blow up for a target still far away (e.g. right after
	// this ship spawns), aiming absurdly far ahead of where the target will
	// actually be by the time it's reached. Only used for the
	// ApproachingFlank lead (see UpdateFlightState) - the flank target
	// moves in a straight line (the player ship's own velocity, no
	// rotational component), so a distance-scaled linear lead stays
	// accurate no matter how large it gets. Circling uses OrbitLeadTime
	// instead - see its comment for why a distance-scaled lead is actively
	// dangerous for a target moving in a circle.
	UPROPERTY(EditDefaultsOnly, Category = "Enemy Ship|Pursuit")
	float MaxLeadTime = 4.f;

	// Fixed (not distance-scaled) lookahead time, in seconds, used to lead
	// the orbit target while Circling - see UpdateFlightState. Deliberately
	// NOT ComputeLeadTarget's distance/ForwardSpeed approach: the orbit
	// target's true path curves (it's going around a ring), so a
	// straight-line velocity extrapolation only stays close to that true
	// path for a small time window. A distance-scaled lead time gets WORSE
	// exactly when this ship is lagging behind (larger distance -> larger
	// lead time -> extrapolation diverges further from the true curve),
	// which can point this ship across the inside of the ring - close to or
	// through the player ship's own hull - while trying to catch up. A
	// small fixed value here avoids that: this ship instead aims at the
	// ring's exact future position (see EvaluateOrbitRing), which stays
	// exactly OrbitRadius from the player ship no matter how far ahead it's
	// evaluated, so raising this only changes how far around the ring this
	// ship "cuts the corner" toward - it can never aim through the ship's
	// interior.
	UPROPERTY(EditDefaultsOnly, Category = "Enemy Ship|Pursuit")
	float OrbitLeadTime = 1.5f;

	// Extra lead time (seconds), on top of OrbitLeadTime, added while this
	// ship is heading in roughly the same direction as the player ship -
	// see UpdateFlightState. Scaled by how aligned the two headings
	// currently are (the dot product of forward vectors, clamped to
	// [0, 1]) so it's at full strength while directly tailing the ship
	// (the exact geometry where a fixed lead falls shortest, since both
	// ship and pursuer are moving the same way) and fades to zero once
	// this ship is heading elsewhere around the ring - never subtracts
	// lead when heading away from or across the player ship's direction of
	// travel. Safe to push higher than OrbitLeadTime itself: since
	// EvaluateOrbitRing evaluates the ring's exact future position rather
	// than linearly extrapolating a velocity, a larger total lead only
	// cuts the corner further around the ring - it can't aim through the
	// player ship's interior the way OrbitLeadTime's comment describes the
	// old (fixed) approach could.
	UPROPERTY(EditDefaultsOnly, Category = "Enemy Ship|Pursuit")
	float ChaseLeadTimeBoost = 2.5f;

	// How fast a client closes the gap to ServerLocation/ServerRotation, same
	// role/style as AShipActor::DriftCorrectionSpeed.
	UPROPERTY(EditDefaultsOnly, Category = "Enemy Ship|Movement")
	float DriftCorrectionSpeed = 2.f;

	// Ship's health, same role as AShipActor::HullIntegrity. Starts at
	// whatever's set here (100 by default) - editable both on the class
	// defaults and per placed instance, so a Blueprint variant or an
	// individual level-placed ship can be tougher/weaker than the rest -
	// drops via ApplyDamage (called by ACannonProjectile::OnHit), destroys
	// the actor at 0.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category = "Enemy Ship|Hull")
	float HullIntegrity = 100.f;

	// Spawned at this ship's location the moment HullIntegrity reaches 0,
	// just before it's actually destroyed - see ApplyDamage/
	// MulticastPlayDeathEffect. Left unset, nothing plays.
	UPROPERTY(EditDefaultsOnly, Category = "Enemy Ship|Hull")
	UNiagaraSystem* DeathEffect;

	// Public entry point for anything that damages this ship (currently only
	// ACannonProjectile::OnHit). Server-authoritative-assuming, same
	// convention as AMount::AttachItem/AShipActor::RepairHull - trusts its
	// caller is already authoritative rather than checking HasAuthority()
	// itself.
	void ApplyDamage(float Amount);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaTime) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	AShipActor* GetOrFindControlledShip();

	// Runs the whole steering pipeline for this tick: picks the current
	// target (flank or orbit point depending on CurrentState), blends in
	// obstacle avoidance and ship-to-ship separation, and calls
	// SteerTowards with the result. Runs identically on every machine.
	void UpdateFlightState(float DeltaTime);

	// Point (world space) this ship flies toward while ApproachingFlank -
	// FlankSign * FlankLateralOffset/FlankForwardOffset/FlankHeightOffset
	// transformed from the player ship's local space into world space fresh
	// every call, so it rides along as the player ship moves. Only actually
	// used by UpdateFlightState before bReachedFlank flips true - see its
	// comment for why this ship stops re-seeking this point once it draws
	// level with the player ship. Its velocity (for lead pursuit - see
	// ComputeLeadTarget) is just GetShipVelocityEstimate(), since this
	// point is rigidly attached to the player ship with no orbital motion
	// of its own - no separate "flank target velocity" function needed.
	FVector ComputeFlankTargetWorld() const;

	// Signed distance (cm) of this ship's position projected onto the
	// player ship's own forward axis, relative to the player ship's origin -
	// positive means ahead of the player ship, negative means behind it.
	// Shared by TryTransitionToCircling (compared against
	// -FlankPastDistanceThreshold) and UpdateFlightState (compared against
	// 0, to detect "drawn level with the player ship").
	float GetRelativeForwardDistanceFromShip() const;

	// Seeds (if not yet done for this Circling phase) and advances
	// OrbitAngle by this tick's share of OrbitAngularSpeed (scaled by
	// FlankSign - keeps the orbit direction consistent with the side this
	// ship approached from, rather than an arbitrary flip at the moment of
	// transition). Seeded once, on whichever machine/tick first observes
	// CurrentState == Circling, from this ship's own current angular
	// position around the player ship expressed in the player ship's LOCAL
	// frame (see the .cpp) - has to match the space EvaluateOrbitRing
	// works in, or the very first orbit target would jump relative to
	// wherever this ship actually is at the moment of transition.
	void AdvanceOrbitAngle(float DeltaTime);

	// Evaluates the orbit ring's position formula at an arbitrary Angle and
	// ship transform - a circle of OrbitRadius in the SHIP's own local
	// frame (rotated and translated by ShipRotation/ShipLocation, same as
	// ComputeFlankTargetWorld) rather than a fixed world-space offset. This
	// is deliberate, not just consistency for its own sake: the player
	// ship translates continuously, so a fixed-world-axis ring only
	// re-centered each tick has every point on it being chased down from a
	// stale offset - collapsing to a tight turn near the bow (the moving
	// center races toward that point) and stretching near the stern (it
	// races away) - which a ring rigidly attached to the ship's own frame
	// doesn't, since every point on it already moves at the ship's own
	// velocity. This does mean the ring turns with the ship if a player
	// actively steers it - a desired consequence, not a bug, matching how
	// the flank approach already tracks the ship's current heading.
	//
	// Taking Angle/ShipLocation/ShipRotation as parameters (rather than
	// always reading OrbitAngle and the player ship's CURRENT transform)
	// is what lets UpdateFlightState evaluate this ring at a point
	// slightly in the future for lead pursuit (see OrbitLeadTime) by
	// exact closed-form formula instead of linearly extrapolating a
	// velocity - see OrbitLeadTime's comment for why that distinction
	// matters here specifically.
	FVector EvaluateOrbitRing(float Angle, const FVector& ShipLocation, const FQuat& ShipRotation) const;

	// Estimate of the player ship's current world-space velocity - its
	// ForwardSpeed (a public, Replicated property on AShipActor) along its
	// current forward vector. Doesn't account for AShipActor's smaller
	// transient motions (ascend/descend, crash knockback) - good enough for
	// leading a target that's cruising in roughly one direction (see
	// ComputeLeadTarget and UpdateFlightState's orbit lead), and much
	// simpler/more robust than trying to derive true velocity from
	// frame-to-frame position deltas.
	FVector GetShipVelocityEstimate() const;

	// First-order lead-pursuit aim point: TargetWorld projected forward by
	// TargetVelocity for an estimated time-to-intercept (the time it would
	// take this ship to close today's distance to TargetWorld at its own
	// ForwardSpeed, clamped to MaxLeadTime). Used instead of aiming
	// directly at a moving target's current (already-stale-by-the-time-it-
	// gets-there) position - this ship always thrusts forward at a fixed
	// speed with no way to slow down or stop (see SteerTowards), so plain
	// pursuit of a target that's a meaningful fraction of its own speed
	// falls further and further behind over time. Naturally collapses to
	// plain pursuit (returns TargetWorld unchanged) when TargetVelocity is
	// zero. Not a precise intercept solve (that needs an iterative or
	// quadratic solution accounting for the target's own motion changing
	// the intercept time) - this single-shot estimate is recomputed fresh
	// every tick, so it keeps correcting itself as distance closes, which
	// is enough for steering rather than an exact firing solution.
	FVector ComputeLeadTarget(const FVector& TargetWorld, const FVector& TargetVelocity) const;

	// Sphere-sweeps forward against the "ShipBlock" custom trace channel
	// (Config/DefaultEngine.ini's unused ECC_GameTraceChannel1 - obstacles
	// like BP_FloatingRock opt in by setting their response to Block on it
	// in their Blueprint; everything else ignores it by default, so this
	// never reacts to players/projectiles/other ships). Returns a "slide
	// around it" direction (the hit normal projected onto the plane
	// perpendicular to travel) and writes OutWeight (0 at trace range, up to
	// BaseAvoidanceWeight at point-blank) - both zero/ZeroVector if nothing
	// was hit.
	FVector ComputeAvoidanceDirection(float& OutWeight) const;

	// Inverse-distance repulsion (classic boids-style separation) away from
	// every other currently-active AEnemyShip within SeparationRadius - see
	// ActiveEnemyShips.
	FVector ComputeSeparationDirection() const;

	// Turns (turn-rate-limited, see TurnRateDegreesPerSec) toward
	// DesiredDirection, then advances forward at ForwardSpeed. Same
	// server-sweeps/client-doesn't split as AShipActor::MoveShip: only the
	// server is authoritative over collision.
	void SteerTowards(const FVector& DesiredDirection, float DeltaTime);

	// Server-only (called from Tick under HasAuthority()) - flips
	// CurrentState from ApproachingFlank to Circling once this ship has
	// trailed more than FlankPastDistanceThreshold behind the player ship's
	// origin, measured along the player ship's own forward axis. A one-way
	// ratchet; never transitions back.
	void TryTransitionToCircling();

	// Spawns DeathEffect at this ship's location - called from ApplyDamage
	// once HullIntegrity reaches 0, just before Destroy(). A NetMulticast
	// (not a direct local spawn), same "server decides, everyone plays this
	// cosmetic" pattern as AShipActor::MulticastOnCrash - ApplyDamage only
	// ever runs on the server, but the effect needs to be seen by every
	// machine, and Destroy() removing this actor doesn't stop an RPC issued
	// just before it from still reaching every client.
	UFUNCTION(NetMulticast, Reliable)
	void MulticastPlayDeathEffect();

	UFUNCTION()
	void OnRep_ServerTransform();

	// Nudges (never snaps) a client's locally-simulated transform toward the
	// latest server snapshot - verbatim copy of
	// AShipActor::CorrectDriftFromServer, retargeted at this actor.
	void CorrectDriftFromServer(float DeltaTime);

	// Which target-seeking behavior is currently active - see
	// EEnemyShipState. Written only by the server (TryTransitionToCircling),
	// replicated so every machine's UpdateFlightState branches identically
	// instead of each independently re-deriving a ratchet that could drift
	// over a long session.
	UPROPERTY(Replicated)
	EEnemyShipState CurrentState = EEnemyShipState::ApproachingFlank;

	// +1 (starboard) or -1 (port), derived once in BeginPlay from which side
	// of the player ship this enemy was placed on - see the constructor
	// comment on HazardBox and BeginPlay. Replicated even though it's
	// derivable identically from level-placed transforms on every machine
	// already (belt-and-suspenders - costs one float, forecloses any future
	// divergence risk if enemy placement ever becomes runtime/randomized).
	UPROPERTY(Replicated)
	float FlankSign = 1.f;

	// Accumulated angle (radians) around the player ship while Circling -
	// see AdvanceOrbitAngle. Local-only, not replicated - every machine
	// derives it identically from FlankSign/DeltaTime once seeded, same
	// category as AShipActor::CurrentTiltPitch.
	float OrbitAngle = 0.f;

	// True once OrbitAngle has been seeded for the current Circling phase -
	// see AdvanceOrbitAngle.
	bool bOrbitInitialized = false;

	// True once this ship has drawn level with (or past) the player ship's
	// beam during ApproachingFlank - see UpdateFlightState for why it then
	// stops re-seeking ComputeFlankTargetWorld's fixed point. Local-only,
	// not replicated - every machine derives it identically each tick from
	// GetRelativeForwardDistanceFromShip(), same category as
	// bOrbitInitialized/OrbitAngle above.
	bool bReachedFlank = false;

	// Server-only guard against ApplyDamage calling Destroy() more than once
	// between HullIntegrity reaching 0 and the actor actually being removed -
	// same role as AShipActor::bHullDestroyed.
	bool bDestroyed = false;

	// The server's authoritative root transform, replicated purely for
	// CorrectDriftFromServer - verbatim copy of AShipActor's
	// ServerLocation/ServerRotation/bHasServerTransform trio.
	UPROPERTY(ReplicatedUsing = OnRep_ServerTransform)
	FVector ServerLocation = FVector::ZeroVector;

	UPROPERTY(Replicated)
	FRotator ServerRotation = FRotator::ZeroRotator;

	bool bHasServerTransform = false;

	// Every AEnemyShip currently in play, added/removed in BeginPlay/EndPlay -
	// backs ComputeSeparationDirection. A static registry rather than a
	// per-tick UGameplayStatics::GetAllActorsOfClass scan: that pattern
	// elsewhere in this codebase (AMount/APuddle/ASteeringWheel's
	// GetOrFindControlledShip) is a one-shot cache-and-forget lookup for a
	// single actor, not meant to be repeated every tick for every ship to
	// reflect the full current roster. Also simpler than an overlap
	// component, which would need its own BeginOverlap/EndOverlap
	// bookkeeping for what's fundamentally a small, fixed-size list.
	static TArray<TWeakObjectPtr<AEnemyShip>> ActiveEnemyShips;
};
