#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RainZone.generated.h"

class USphereComponent;
class UNiagaraSystem;
class UNiagaraComponent;
class AShipActor;
class APuddle;

/**
 * Stationary weather trigger placed in the level - when AShipActor's
 * HazardBox sweeps into ZoneVolume, starts playing RainEffect (attached to
 * the ship) and begins rolling, every PuddleSpawnInterval seconds, a
 * PuddleSpawnChance shot at spawning a PuddleClass instance somewhere on the
 * ship's deck (see AShipActor::GetRandomDeckLocation). Leaving the zone stops
 * the effect and the spawn roll - puddles already spawned are left in place.
 *
 * AShipActor's HazardBox only ever sweeps (and so can only ever generate
 * overlaps) on the server - see MoveShip's HasAuthority() branch in
 * ShipActor.cpp - so OnZoneBeginOverlap/OnZoneEndOverlap only ever
 * meaningfully fire there, same as AShipActor::OnShipMeshHit. That makes the
 * "is the ship in this zone" state and the puddle-spawn timer naturally
 * server-only; MulticastSetRaining exists purely to broadcast the cosmetic
 * rain effect to every client, same "server decides, everyone plays it"
 * pattern used throughout AShipCharacter/AShipActor.
 */
UCLASS()
class SHIPGAME_API ARainZone : public AActor
{
	GENERATED_BODY()

public:
	ARainZone();

	// Overlap-only volume defining the zone's extent - resize/reposition in
	// the Blueprint to cover the desired area. Collision response to
	// WorldStatic (HazardBox's object type) is explicitly Overlap, never
	// Block - MoveShip sweeps HazardBox every tick, so a Block response here
	// would act as an invisible wall stopping the ship.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rain Zone")
	USphereComponent* ZoneVolume;

	// Niagara system played (attached to the ship) while it's in this zone -
	// assign in the Blueprint. Optional, same as the various *Montage fields
	// on AShipCharacter - the zone still tracks entry/exit and spawns
	// puddles with no effect assigned.
	UPROPERTY(EditDefaultsOnly, Category = "Rain Zone")
	UNiagaraSystem* RainEffect;

	// How often (seconds), while the ship is inside this zone, to roll for a
	// puddle spawn.
	UPROPERTY(EditDefaultsOnly, Category = "Rain Zone")
	float PuddleSpawnInterval = 7.f;

	// Chance [0,1] each PuddleSpawnInterval tick of actually spawning a
	// puddle.
	UPROPERTY(EditDefaultsOnly, Category = "Rain Zone", meta = (ClampMin = "0", ClampMax = "1"))
	float PuddleSpawnChance = 0.25f;

	// Puddle class to spawn - set to BP_Puddle in the Blueprint.
	UPROPERTY(EditDefaultsOnly, Category = "Rain Zone")
	TSubclassOf<APuddle> PuddleClass;

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnZoneBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnZoneEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	// Server-only, bound to PuddleSpawnTimerHandle at PuddleSpawnInterval
	// while OverlappingShip is set. Rolls PuddleSpawnChance and, on success,
	// spawns PuddleClass at a random point on OverlappingShip's deck.
	void TrySpawnPuddle();

	// Spawns (bRaining=true) or stops (bRaining=false) the local Niagara
	// effect attached to Ship, on every machine including the server - same
	// cosmetic-replication pattern as AShipCharacter's Multicast* functions.
	// Ship is passed explicitly (rather than reading OverlappingShip) because
	// OverlappingShip is never set on clients in the first place - see the
	// class comment.
	UFUNCTION(NetMulticast, Reliable)
	void MulticastSetRaining(AShipActor* Ship, bool bRaining);

	// The ship currently inside this zone, if any. Server-only - see the
	// class comment for why clients never populate this themselves.
	AShipActor* OverlappingShip = nullptr;

	// Server-only - drives TrySpawnPuddle while OverlappingShip is set.
	FTimerHandle PuddleSpawnTimerHandle;

	// Local-only handle (never replicated - cosmetic, matches the
	// bIsSwinging-style per-machine state elsewhere) to the Niagara component
	// this zone spawned on the ship, so MulticastSetRaining(_, false) can
	// stop the right instance. A zone can only ever have one ship inside it
	// at a time (see OverlappingShip/OnZoneBeginOverlap), so one handle is
	// enough.
	UPROPERTY()
	UNiagaraComponent* ActiveRainEffectComponent = nullptr;
};
