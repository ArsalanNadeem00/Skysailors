#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Logging/LogMacros.h"
#include "ShipCharacter.generated.h"

class UCameraComponent;
class UInputAction;
class UStaticMeshComponent;
class UAnimMontage;
class AClosetDoor;
class APuddle;
struct FInputActionValue;

DECLARE_LOG_CATEGORY_EXTERN(LogShipCharacter, Log, All);

// Identifies which tool (if any) the character currently has equipped. Add a
// new entry here plus a matching entry in AShipCharacter::ToolDefinitions to
// bring in a new tool (e.g. a future wrench) - no other C++ changes needed
// unless the tool needs behavior beyond what FToolDefinition can express.
UENUM(BlueprintType)
enum class EEquippedTool : uint8
{
	None,
	Mop
};

// Per-tool capabilities/behavior, keyed off EEquippedTool by
// AShipCharacter::ToolDefinitions. Deliberately data-only (filled in the
// Blueprint) so adding a new swingable tool is a data change, not a code
// change. Second-action fields (e.g. the mop's future floor-cleaning ability)
// belong here too once that's implemented - same extensibility point.
USTRUCT(BlueprintType)
struct FToolDefinition
{
	GENERATED_BODY()

	// Whether the main-action input (default: left mouse click) swings this tool.
	UPROPERTY(EditDefaultsOnly, Category = "Tool")
	bool bCanSwing = false;

	// Montage played on every machine (see MulticastPlaySwing) when swinging
	// this tool. Optional - swinging still works (and still enforces
	// SwingDuration as a cooldown) with no montage assigned, so this can be
	// wired up before swing art/animation exists.
	UPROPERTY(EditDefaultsOnly, Category = "Tool", meta = (EditCondition = "bCanSwing"))
	UAnimMontage* SwingMontage = nullptr;

	// How long (seconds) a swing blocks re-triggering the main action for.
	// Should roughly match SwingMontage's length once one is assigned.
	UPROPERTY(EditDefaultsOnly, Category = "Tool", meta = (EditCondition = "bCanSwing"))
	float SwingDuration = 0.6f;

	// Whether the secondary-action input (default: right mouse click) does
	// anything for this tool. Unlike swinging, each tool's secondary-action
	// *effect* is tool-specific code (the mop cleans, a future wrench would
	// repair, etc - see AShipCharacter::ResolveSecondaryActionEffect), so this
	// struct only carries the generic lock/montage timing shared by all of
	// them, not the effect itself.
	UPROPERTY(EditDefaultsOnly, Category = "Tool")
	bool bCanUseSecondaryAction = false;

	// Montage played on every machine while the secondary action's lock is in
	// effect. Optional, same as SwingMontage.
	UPROPERTY(EditDefaultsOnly, Category = "Tool", meta = (EditCondition = "bCanUseSecondaryAction"))
	UAnimMontage* SecondaryActionMontage = nullptr;

	// How long (seconds) the secondary action locks the character's movement
	// for before its effect resolves and control returns. Should roughly
	// match SecondaryActionMontage's length once one is assigned.
	UPROPERTY(EditDefaultsOnly, Category = "Tool", meta = (EditCondition = "bCanUseSecondaryAction"))
	float SecondaryActionDuration = 1.5f;
};

/**
 * Player pawn using Enhanced Input, matching the project's existing
 * AShipGamePlayerController, which applies the InputMappingContext(s) these
 * actions are bound through. CharacterMovementComponent handles movement
 * replication and moving-platform support automatically, so no custom
 * networking code is needed here.
 */
UCLASS()
class SHIPGAME_API AShipCharacter : public ACharacter
{
	GENERATED_BODY()

	/** First person camera, fixed relative to the capsule (not the animated mesh) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	UCameraComponent* FirstPersonCamera;

protected:
	/** Jump Input Action */
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* JumpAction;

	/** Move Input Action */
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* MoveAction;

	/** Look Input Action */
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* LookAction;

	/** Interact Input Action - used to take the helm of a nearby ASteeringWheel, or to pick up/put away a mop at a nearby AClosetDoor */
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* InteractAction;

	/** Main-action Input Action (default: left mouse click) - swings the equipped tool, if it supports swinging. See FToolDefinition::bCanSwing. */
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* MainActionAction;

	/** Secondary-action Input Action (default: right mouse click) - activates the equipped tool's secondary effect, if it has one (e.g. the mop cleans a nearby puddle). See FToolDefinition::bCanUseSecondaryAction. */
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* SecondaryActionAction;

	// Max distance (cm) to a ASteeringWheel/AClosetDoor for Interact to engage it.
	UPROPERTY(EditDefaultsOnly, Category = "Interaction")
	float InteractionRange = 150.f;

	// Max distance (cm) in front of the character to look for an APuddle to
	// clean up when the mop's secondary action resolves. Kept separate from
	// InteractionRange since the mop's reach has no reason to match
	// wheel/closet interaction distance.
	UPROPERTY(EditDefaultsOnly, Category = "Mop")
	float CleaningRange = 150.f;

	// How directly the character must be facing a wheel/door to interact with
	// it, as the min dot product between the character's forward vector and
	// the direction to the target. 1 = dead-on only, 0 = full 180-degree arc
	// either side. 0.5 allows roughly a 60-degree cone either side of forward.
	UPROPERTY(EditDefaultsOnly, Category = "Interaction")
	float InteractionFacingDotThreshold = 0.5f;

	// "Mop" prop shown/hidden in the character's hand when carrying one (see
	// ToggleMop) - always present as a component and just toggled visible/
	// invisible, rather than spawned/destroyed as a separate actor, so
	// there's no attach latency and only one replicated bool (bHasMop) is
	// needed to keep it in sync instead of replicating a whole extra actor.
	// The actual mesh/socket are only ever real in the Blueprint - see the
	// class comment in ShipActor.h for why that split exists project-wide.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mop", meta = (AllowPrivateAccess = "true"))
	UStaticMeshComponent* MopMesh;

	// Socket on GetMesh() (the first person arms mesh) MopMesh is attached
	// to - set to match the character's actual skeleton in the Blueprint.
	UPROPERTY(EditDefaultsOnly, Category = "Mop")
	FName MopAttachSocketName = TEXT("hand_rSocket");

	// Montage played on every machine while a slip (see TrySlip) is locking
	// movement. Optional, same as SwingMontage/SecondaryActionMontage.
	UPROPERTY(EditDefaultsOnly, Category = "Slip")
	UAnimMontage* SlipMontage = nullptr;

	// How long (seconds) a slip locks the character's movement for. Should
	// roughly match SlipMontage's length once one is assigned.
	UPROPERTY(EditDefaultsOnly, Category = "Slip")
	float SlipDuration = 1.5f;

	// How long (seconds) a character is immune to slipping again after
	// TrySlip actually triggers a slip - stops the same puddle re-triggering
	// every tick the character overlaps it.
	UPROPERTY(EditDefaultsOnly, Category = "Slip")
	float SlipImmunityDuration = 5.f;

	// Tool currently carried, if any - toggled by ToggleMop (server-only) and
	// replicated so MopMesh's visibility stays in sync on every machine, not
	// just the carrying player's.
	UPROPERTY(ReplicatedUsing = OnRep_EquippedTool)
	EEquippedTool EquippedTool = EEquippedTool::None;

	// Capabilities (currently just swing) for each EEquippedTool value, filled
	// in the Blueprint (e.g. BP_ShipCharacter). See FToolDefinition/EEquippedTool
	// comments for how to add a new tool.
	UPROPERTY(EditDefaultsOnly, Category = "Tools")
	TMap<EEquippedTool, FToolDefinition> ToolDefinitions;

	// True on this machine while a swing's cooldown is still running - blocks
	// re-triggering MainAction until FinishSwing clears it. Deliberately not
	// replicated: the server enforces its own copy authoritatively in
	// ServerMainAction, this copy just stops each client from spamming that
	// RPC, so there's no need to keep the two in sync.
	bool bIsSwinging = false;

	FTimerHandle SwingTimerHandle;

	// True on this machine while the equipped tool's secondary action is
	// locking movement/blocking re-triggering - set by MulticastPlaySecondaryAction
	// and cleared by FinishSecondaryAction, same pattern (and same reasoning
	// for not being replicated) as bIsSwinging above. Checked in DoMove/
	// DoJumpStart to actually enforce the "locked in place" part.
	bool bIsPerformingSecondaryAction = false;

	// Per-machine cosmetic unlock timer, mirrors SwingTimerHandle.
	FTimerHandle SecondaryActionTimerHandle;

	// Server-only timer that resolves the secondary action's actual gameplay
	// effect (e.g. destroying a puddle) once FToolDefinition::SecondaryActionDuration
	// has elapsed - kept separate from SecondaryActionTimerHandle so the
	// effect only ever runs once, authoritatively, regardless of how many
	// machines are running the cosmetic lock/unlock above.
	FTimerHandle SecondaryActionEffectTimerHandle;

	// True on this machine while a slip (see TrySlip) is locking movement -
	// set by MulticastPlaySlip and cleared by FinishSlip, same per-machine-
	// cosmetic-only pattern as bIsSwinging/bIsPerformingSecondaryAction.
	// Checked alongside those two in DoMove/DoJumpStart.
	bool bIsSlipping = false;

	// Per-machine cosmetic unlock timer, mirrors SwingTimerHandle/
	// SecondaryActionTimerHandle.
	FTimerHandle SlipTimerHandle;

	// True on the server only while a slip's immunity window is running -
	// unlike bIsSwinging/bIsPerformingSecondaryAction this has no per-client
	// mirror, because nothing ever originates client-side for a slip (it's
	// purely environmental, driven by APuddle's overlap on the server - see
	// APuddle::OnSlipTriggerBeginOverlap), so there's no client copy to keep
	// from spamming an RPC in the first place.
	bool bIsImmuneToSlipping = false;

	// Server-only timer that clears bIsImmuneToSlipping after SlipImmunityDuration.
	FTimerHandle SlipImmunityTimerHandle;

public:
	AShipCharacter();

protected:
	virtual void BeginPlay() override;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Bind Enhanced Input actions - the mapping context itself is added by
	 *  the PlayerController (AShipGamePlayerController), not here. */
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	/** Called for movement input */
	void Move(const FInputActionValue& Value);
	/** Called for looking input */
	void Look(const FInputActionValue& Value);
	/** Called for interact input - tries to take the helm of the nearest
	 *  ASteeringWheel in range, else tries to toggle the mop at the nearest
	 *  AClosetDoor in range. */
	void Interact();

	/** Called for main-action input - swings the equipped tool if it supports
	 *  swinging (see FToolDefinition::bCanSwing). */
	void MainAction();

	/** Called for secondary-action input - activates the equipped tool's
	 *  secondary effect if it has one (see FToolDefinition::bCanUseSecondaryAction). */
	void SecondaryAction();

	// Looks up the currently equipped tool's FToolDefinition, if any.
	const FToolDefinition* GetEquippedToolDefinition() const;

	// True if the equipped tool supports swinging, and no swing/secondary
	// action/slip is already in progress on this machine.
	bool CanSwingEquippedTool() const;

	// True if the equipped tool supports a secondary action, and no
	// swing/secondary action/slip is already in progress on this machine.
	bool CanUseSecondaryActionOnEquippedTool() const;

	// Clears bIsSwinging - bound as a one-shot timer by MulticastPlaySwing so
	// the swing "cooldown" expires without depending on animation ticking
	// (which dedicated servers may skip for an unseen mesh).
	void FinishSwing();

	// Clears bIsPerformingSecondaryAction - bound as a one-shot timer by
	// MulticastPlaySecondaryAction, mirroring FinishSwing/SwingTimerHandle.
	void FinishSecondaryAction();

	// Finds the nearest APuddle within CleaningRange, roughly in front of the
	// character (same facing check as FindNearestSteeringWheel/
	// FindNearestClosetDoor). Server-only: called from PerformMopClean so the
	// range/facing check is authoritative.
	APuddle* FindNearestPuddle() const;

	// Resolves the equipped tool's secondary-action gameplay effect - server-
	// only, called once FToolDefinition::SecondaryActionDuration has elapsed
	// since ServerSecondaryAction. Dispatches on EquippedTool; add a new case
	// (plus a PerformXSecondaryEffect-style helper) here when a new tool's
	// secondary action is implemented - unlike swinging, the effect itself
	// isn't data-driven since every tool's secondary action does something
	// different.
	void ResolveSecondaryActionEffect();

	// The mop's secondary-action effect: destroys the nearest APuddle in
	// front of the character within CleaningRange, if any. Server-only,
	// called from ResolveSecondaryActionEffect.
	void PerformMopClean();

	// Clears bIsSlipping - bound as a one-shot timer by MulticastPlaySlip,
	// mirroring FinishSwing/FinishSecondaryAction.
	void FinishSlip();

	// Clears bIsImmuneToSlipping - bound as a one-shot timer by TrySlip.
	void ClearSlipImmunity();

	// Finds the nearest unoccupied ASteeringWheel within InteractionRange, if any.
	// Server-only: called from ServerInteract so the range check is authoritative.
	class ASteeringWheel* FindNearestSteeringWheel() const;

	// Finds the nearest AClosetDoor within InteractionRange, if any. Server-only:
	// called from ServerInteract so the range check is authoritative.
	AClosetDoor* FindNearestClosetDoor() const;

	// Flips bHasMop and applies the resulting MopMesh visibility locally -
	// server-only (see ServerInteract), replication carries bHasMop (and thus
	// the visual) to every other machine via OnRep_HasMop.
	void ToggleMop();

	UFUNCTION()
	void OnRep_EquippedTool();

	UFUNCTION(Server, Reliable)
	void ServerInteract();

	// Authoritative main-action handler - re-validates CanSwingEquippedTool
	// (never trust the client's local check) before broadcasting the swing.
	UFUNCTION(Server, Reliable)
	void ServerMainAction();

	// Plays the swing (and starts the local bIsSwinging cooldown) on every
	// machine, including whichever one called ServerMainAction - simple
	// "server decides, everyone plays it" cosmetic replication, matching this
	// codebase's preference for minimal replicated state over a persistent
	// replicated "is swinging" flag. Setting bIsSwinging only here (rather
	// than also optimistically in MainAction) matters for a listen-server
	// host: MainAction and ServerMainAction_Implementation run as the same
	// synchronous call on that machine, so setting the flag any earlier would
	// make ServerMainAction immediately reject its own swing.
	UFUNCTION(NetMulticast, Reliable)
	void MulticastPlaySwing(UAnimMontage* MontageToPlay, float Duration);

	// Authoritative secondary-action handler - re-validates
	// CanUseSecondaryActionOnEquippedTool (never trust the client's local
	// check) before broadcasting the cosmetic lock/montage and scheduling
	// ResolveSecondaryActionEffect.
	UFUNCTION(Server, Reliable)
	void ServerSecondaryAction();

	// Plays the secondary-action montage (and starts the local
	// bIsPerformingSecondaryAction lock) on every machine, including
	// whichever one called ServerSecondaryAction - same "server decides,
	// everyone plays it" cosmetic replication as MulticastPlaySwing, and for
	// the same listen-server-host ordering reason (see MulticastPlaySwing's
	// comment).
	UFUNCTION(NetMulticast, Reliable)
	void MulticastPlaySecondaryAction(UAnimMontage* MontageToPlay, float Duration);

	// Plays the slip montage (and starts the local bIsSlipping lock, plus
	// hard-stops current momentum via StopMovementImmediately so residual
	// velocity from before the slip doesn't keep carrying the character) on
	// every machine - same "server decides, everyone plays it" cosmetic
	// replication as MulticastPlaySwing/MulticastPlaySecondaryAction. Called
	// directly from TrySlip rather than from a Server RPC, since a slip is
	// never client-initiated in the first place (see TrySlip's comment).
	UFUNCTION(NetMulticast, Reliable)
	void MulticastPlaySlip(UAnimMontage* MontageToPlay, float Duration);

public:
	// Called by APuddle::OnSlipTriggerBeginOverlap when this character walks
	// into a puddle's SlipTriggerSphere. Server-only (the overlap that calls
	// this is itself gated to HasAuthority() in APuddle, so there's no
	// client-originated call to validate against - unlike MainAction/
	// SecondaryAction there's no matching client-side prediction/RPC pair
	// here). No-ops if already swinging, mid secondary-action, mid-slip, or
	// still within the post-slip immunity window; otherwise starts
	// SlipImmunityDuration's immunity timer and broadcasts the cosmetic
	// lock/montage via MulticastPlaySlip.
	void TrySlip();

	/** Handles move inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category = "Input")
	virtual void DoMove(float Right, float Forward);
	/** Handles look inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category = "Input")
	virtual void DoLook(float Yaw, float Pitch);
	/** Handles jump pressed inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category = "Input")
	virtual void DoJumpStart();
	/** Handles jump released inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category = "Input")
	virtual void DoJumpEnd();

	/** Returns FirstPersonCamera subobject */
	FORCEINLINE class UCameraComponent* GetFirstPersonCamera() const { return FirstPersonCamera; }
};