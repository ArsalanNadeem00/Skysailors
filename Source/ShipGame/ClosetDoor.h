#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ClosetDoor.generated.h"

class UStaticMeshComponent;
class AShipActor;

/**
 * Stationary interactable placed on the ship - not a Pawn like ASteeringWheel
 * (nothing to possess here, and no occupancy to enforce - any number of
 * characters can interact with the same closet). AShipCharacter finds the
 * nearest one in range and, on interact, opens a tool menu letting the player
 * pick which tool (if any) to equip (see AShipCharacter::ClientOpenToolMenu/
 * RequestEquipTool); the door itself holds no state and never opens/animates
 * - "opening" the closet is represented purely by the menu appearing on the
 * interacting player's screen, and the chosen tool appearing/disappearing in
 * their hand.
 *
 * Attaches itself to the ship's hull in BeginPlay (same as ASteeringWheel) so
 * it rides the deck instead of being left behind as the ship sails on.
 */
UCLASS()
class SHIPGAME_API AClosetDoor : public AActor
{
	GENERATED_BODY()

public:
	AClosetDoor();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Closet Door")
	UStaticMeshComponent* DoorMesh;

	// The ship this closet rides on. Auto-found in BeginPlay (first
	// AShipActor in the level) if left unset, same as ASteeringWheel's
	// ControlledShip.
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Closet Door")
	AShipActor* ControlledShip;

protected:
	virtual void BeginPlay() override;

	AShipActor* GetOrFindControlledShip();
};
