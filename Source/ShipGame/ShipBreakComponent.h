#pragma once

#include "CoreMinimal.h"
#include "NiagaraComponent.h"
#include "ShipBreakComponent.generated.h"

/**
 * Placed directly on AShipActor (in the Blueprint's Components panel, one
 * instance per hazard spot along the hull - position each individually,
 * same workflow as any other component) rather than as a separate actor.
 * IS the smoke/electricity Niagara effect for its spot, same as the old
 * AShipBreak's BreakEffect was its whole visible representation - there's no
 * separate mesh.
 *
 * Starts inactive (bAutoActivate = false, see the constructor): invisible
 * and not interactable. AShipActor::ActivateRandomShipBreak flips one
 * inactive instance on (SetBroken(true)) each time cumulative crash damage
 * crosses DamagePerShipBreakActivation. AShipCharacter's wrench tool-use
 * action fixes it - see AShipCharacter::PerformWrenchFix - by restoring hull
 * (AShipActor::RepairHull) and then calling SetBroken(false), which turns
 * the effect back off instead of destroying anything, so the same component
 * can break and be fixed again later in the match.
 *
 * bIsBroken is replicated (unlike most cosmetic state in this codebase,
 * which instead uses "server decides, everyone plays it" NetMulticast RPCs -
 * see e.g. AShipCharacter::MulticastPlaySwing's comment) because it's
 * persistent state, not a momentary effect: a client joining mid-match still
 * needs to see whichever breaks are currently active, not just ones
 * activated after they connected.
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class SHIPGAME_API UShipBreakComponent : public UNiagaraComponent
{
	GENERATED_BODY()

public:
	UShipBreakComponent();

	// HullIntegrity restored to the owning AShipActor (via AShipActor::
	// RepairHull) when a wrench fixes this break - see
	// AShipCharacter::PerformWrenchFix.
	UPROPERTY(EditDefaultsOnly, Category = "ShipBreak")
	float HullRepairAmount = 10.f;

	UFUNCTION(BlueprintPure, Category = "ShipBreak")
	bool IsBroken() const { return bIsBroken; }

	// Turns this break on/off - shows/hides+activates/deactivates the
	// Niagara effect (see ApplyBrokenVisualState) and, while true, makes
	// this the kind of break AShipCharacter::FindNearestActiveShipBreak will
	// find. Called server-side only (from AShipActor::ActivateRandomShipBreak
	// to turn one on, and AShipCharacter::PerformWrenchFix to turn one back
	// off) - bIsBroken then replicates the resulting state to every client.
	void SetBroken(bool bNewBroken);

protected:
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(ReplicatedUsing = OnRep_IsBroken)
	bool bIsBroken = false;

	UFUNCTION()
	void OnRep_IsBroken();

	// Shared by SetBroken (immediate, on whichever machine calls it - OnRep
	// never fires on the machine that made the change) and OnRep_IsBroken
	// (every other machine, once bIsBroken's new value replicates) - keeps
	// the Niagara effect's active/visible state in sync with bIsBroken.
	void ApplyBrokenVisualState();
};
