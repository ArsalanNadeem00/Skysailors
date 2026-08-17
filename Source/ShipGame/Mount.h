#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Mount.generated.h"

class UStaticMeshComponent;
class AShipActor;
class AMountableItem;

// Convenience typed view of AMount::AttachedItem for Blueprint/UI use -
// AttachedItem itself (nullptr/ACannon/AShield) is the actual source of
// truth, so this is derived (see AMount::GetMountState), never stored
// separately, to avoid the two ever drifting apart.
UENUM(BlueprintType)
enum class EMountState : uint8
{
	Empty,
	Cannon,
	Shield
};

/**
 * Stationary circular platform placed on the sides of the ship - holds at
 * most one AMountableItem (currently ACannon or AShield) at a time.
 * AShipCharacter's wrench tool-use action detaches whatever's here to carry
 * (see AShipCharacter::TryRemoveMountItem/PerformWrenchFix); the swing/
 * tool-use-action inputs, while carrying an item, attach it to the nearest
 * *empty* mount instead of their usual tool behavior (see
 * AShipCharacter::TryPlaceCarriedMountItem). Holds no other gameplay state -
 * AttachedItem is both "is this occupied" and "which kind" (see
 * GetMountState), same philosophy as ASteeringWheel deriving IsOccupied()
 * from GetController() rather than a separate bool.
 *
 * AttachedItem is replicated (unlike most cosmetic/one-shot state elsewhere
 * in this codebase, which uses NetMulticast RPCs - see UShipBreakComponent's
 * class comment for the same reasoning) since it's persistent state a
 * client joining mid-match still needs to see. The actual visual of the
 * attached item riding along, though, comes for free from AMountableItem's
 * own built-in actor-attachment replication, not from this property.
 *
 * Attaches itself to the ship's hull in BeginPlay (same as ASteeringWheel/
 * AClosetDoor/APuddle) so it rides the deck instead of being left behind as
 * the ship sails on.
 */
UCLASS()
class SHIPGAME_API AMount : public AActor
{
	GENERATED_BODY()

public:
	AMount();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mount")
	UStaticMeshComponent* MountMesh;

	// Where an attached AMountableItem's mesh snaps to - reposition/rotate in
	// the Blueprint so items sit correctly on top of MountMesh's platform.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mount")
	USceneComponent* ItemAttachPoint;

	// The ship this mount rides on. Auto-found in BeginPlay (first
	// AShipActor in the level) if left unset, same as ASteeringWheel/
	// AClosetDoor/APuddle's ControlledShip.
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Mount")
	AShipActor* ControlledShip;

	// Optional item already mounted here at level start (e.g. a ship that
	// spawns pre-equipped with a cannon) - place an ACannon/AShield instance
	// in the level and reference it here; BeginPlay attaches it automatically.
	// Leave unset for a mount that starts empty.
	UPROPERTY(EditInstanceOnly, Category = "Mount")
	AMountableItem* InitialItem;

	UFUNCTION(BlueprintPure, Category = "Mount")
	bool IsEmpty() const { return AttachedItem == nullptr; }

	UFUNCTION(BlueprintPure, Category = "Mount")
	AMountableItem* GetAttachedItem() const { return AttachedItem; }

	// Empty/Cannon/Shield, derived from AttachedItem's actual class - see the
	// EMountState comment above for why this isn't stored separately.
	UFUNCTION(BlueprintPure, Category = "Mount")
	EMountState GetMountState() const;

	// Attaches Item to ItemAttachPoint and marks this mount occupied.
	// Server-only, same as AShipActor::RepairHull - assumes its caller
	// (AShipCharacter::TryPlaceCarriedMountItem) is already authoritative
	// rather than checking HasAuthority() itself. No-ops (returns false) if
	// already occupied or Item is null.
	bool AttachItem(AMountableItem* Item);

	// Detaches and returns whatever's currently attached (nullptr if empty),
	// leaving this mount empty. Server-only, same caveat as AttachItem -
	// called by AShipCharacter::TryRemoveMountItem (the wrench's tool-use
	// action).
	AMountableItem* DetachItem();

protected:
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	AShipActor* GetOrFindControlledShip();

	// Whichever AMountableItem (ACannon/AShield) is currently attached, or
	// nullptr if empty - see the class comment for why this is replicated.
	// Plain Replicated, not ReplicatedUsing: there's no client-side visual
	// work to react to (the item's own attachment replication already
	// handles that), just a value GetMountState()/IsEmpty() read directly.
	UPROPERTY(Replicated)
	AMountableItem* AttachedItem;
};
