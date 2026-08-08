#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Puddle.generated.h"

class UStaticMeshComponent;
class USphereComponent;
class AShipActor;
class AShipCharacter;

/**
 * Stationary mess placed on the ship's deck - destroyed by AShipCharacter's
 * mop secondary action (see AShipCharacter::PerformMopClean) when a character
 * mops it up. Holds no gameplay state of its own; being destroyed *is* the
 * "cleaned" state, so there's nothing to replicate beyond the actor's own
 * destruction.
 *
 * Also makes any AShipCharacter that walks into SlipTriggerSphere slip (see
 * AShipCharacter::TrySlip) - a separate, overlap-driven hazard from the
 * mop's cleaning interaction above.
 *
 * bReplicates is set true (even though there are no replicated properties)
 * specifically so the server calling Destroy() actually removes this actor
 * from clients too - level-placed actors default to bReplicates = false,
 * which would leave Destroy() a server-only change clients never learn about.
 *
 * Attaches itself to the ship's hull in BeginPlay (same as ASteeringWheel/
 * AClosetDoor) so it rides the deck instead of being left behind as the ship
 * sails on.
 */
UCLASS()
class SHIPGAME_API APuddle : public AActor
{
	GENERATED_BODY()

public:
	APuddle();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Puddle")
	UStaticMeshComponent* PuddleMesh;

	// Overlap-only trigger that makes a AShipCharacter walking through it
	// slip (see AShipCharacter::TrySlip) - resize/reposition in the
	// Blueprint to roughly match the puddle mesh's visible extent.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Puddle")
	USphereComponent* SlipTriggerSphere;

	// The ship this puddle rides on. Auto-found in BeginPlay (first AShipActor
	// in the level) if left unset, same as ASteeringWheel/AClosetDoor's
	// ControlledShip.
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Puddle")
	AShipActor* ControlledShip;

protected:
	virtual void BeginPlay() override;

	AShipActor* GetOrFindControlledShip();

	// Bound to SlipTriggerSphere's overlap. Only acts when HasAuthority() -
	// both server and clients run this locally (collision is simulated
	// everywhere), but only the server's call is allowed to actually start a
	// slip, since AShipCharacter::TrySlip enforces the immunity window
	// authoritatively and there's no client input to route through a Server
	// RPC here (this is purely environmental).
	UFUNCTION()
	void OnSlipTriggerBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);
};
