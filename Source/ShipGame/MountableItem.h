#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "MountableItem.generated.h"

class UStaticMeshComponent;

/**
 * Shared base for anything that can sit on an AMount (see Mount.h) and be
 * carried by an AShipCharacter between mounts - currently ACannon and
 * AShield. Purely a mesh + identity; AMount and AShipCharacter own all the
 * attach/carry logic (AMount::AttachItem/DetachItem, AShipCharacter::
 * TryRemoveMountItem/TryPlaceCarriedMountItem) since this class has no state
 * of its own to track which mount (if any) it's on or who's carrying it -
 * that's entirely expressed via normal actor attachment.
 *
 * APawn (not AActor) specifically so a subclass can be operated by a
 * character the same way ASteeringWheel is - see ACannon, which is the only
 * subclass that actually gets possessed today; AShield simply never is,
 * same as any other placed-but-never-controlled Pawn.
 *
 * bReplicates is set true (even though there are no other replicated
 * properties) so the server re-parenting this actor between an AMount's
 * ItemAttachPoint and an AShipCharacter's hand socket actually replicates to
 * clients too - level-placed actors default to bReplicates = false, same
 * reasoning as APuddle/UShipBreakComponent elsewhere in this codebase. Unlike
 * AShipActor, this class deliberately leaves the engine's default
 * SetReplicateMovement(true) alone: that default's built-in attachment
 * replication (FRepAttachment/OnRep_AttachmentReplication) is exactly what's
 * needed here, since an item only ever teleports/re-attaches occasionally
 * rather than moving continuously - there's no jitter concern like
 * AShipActor's to design around.
 */
UCLASS(abstract)
class SHIPGAME_API AMountableItem : public APawn
{
	GENERATED_BODY()

public:
	AMountableItem();

	// No collision - purely a visual prop whether sitting on a mount or
	// carried in a character's hand, same as AShipCharacter's MopMesh/
	// WrenchMesh. The actual mesh is only ever real in the Blueprint - see
	// the class comment in ShipActor.h for why that split exists project-wide.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mountable Item")
	UStaticMeshComponent* ItemMesh;
};
