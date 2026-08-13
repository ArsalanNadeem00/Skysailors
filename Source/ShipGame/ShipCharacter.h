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
class UShipBreakComponent;
class UShipToolMenuWidget;
struct FInputActionValue;

DECLARE_LOG_CATEGORY_EXTERN(LogShipCharacter, Log, All);

// Identifies which tool (if any) the character currently has equipped. Add a
// new entry here plus a matching entry in AShipCharacter::ToolDefinitions to
// bring in a new tool - no other C++ changes needed unless the tool needs
// behavior beyond what FToolDefinition can express (as Wrench does for its
// tool-use effect - see AShipCharacter::PerformWrenchFix).
UENUM(BlueprintType)
enum class EEquippedTool : uint8
{
	None,
	Mop,
	Wrench
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

	// Whether the swing-action input (default: left mouse click) swings this tool.
	UPROPERTY(EditDefaultsOnly, Category = "Tool")
	bool bCanSwing = false;

	// Montage played on every machine (see MulticastPlaySwing) when swinging
	// this tool. Optional - swinging still works (and still enforces
	// SwingDuration as a cooldown) with no montage assigned, so this can be
	// wired up before swing art/animation exists.
	UPROPERTY(EditDefaultsOnly, Category = "Tool", meta = (EditCondition = "bCanSwing"))
	UAnimMontage* SwingMontage = nullptr;

	// How long (seconds) a swing blocks re-triggering the swing action for.
	// Should roughly match SwingMontage's length once one is assigned.
	UPROPERTY(EditDefaultsOnly, Category = "Tool", meta = (EditCondition = "bCanSwing"))
	float SwingDuration = 0.6f;

	// Whether the tool-use-action input (default: right mouse click) does
	// anything for this tool. Unlike swinging, each tool's tool-use-action
	// *effect* is tool-specific code (the mop cleans, the wrench repairs a
	// UShipBreakComponent, etc - see AShipCharacter::ResolveToolUseActionEffect), so
	// this struct only carries the generic lock/montage timing shared by all
	// of them, not the effect itself.
	UPROPERTY(EditDefaultsOnly, Category = "Tool")
	bool bCanUseToolUseAction = false;

	// Montage played on every machine while the tool-use action's lock is in
	// effect. Optional, same as SwingMontage.
	UPROPERTY(EditDefaultsOnly, Category = "Tool", meta = (EditCondition = "bCanUseToolUseAction"))
	UAnimMontage* ToolUseActionMontage = nullptr;

	// How long (seconds) the tool-use action locks the character's movement
	// for before its effect resolves and control returns. Should roughly
	// match ToolUseActionMontage's length once one is assigned.
	UPROPERTY(EditDefaultsOnly, Category = "Tool", meta = (EditCondition = "bCanUseToolUseAction"))
	float ToolUseActionDuration = 1.5f;

	// Whether pressing the tool-use-action input again while this tool's
	// tool-use action is already in progress cancels it early instead of
	// being ignored - see AShipCharacter::ToolUseAction/
	// ServerCancelToolUseAction. Canceling always skips the effect (e.g. the
	// wrench's hull repair); it just returns movement control early. The
	// mop's near-instant clean has no reason to be interruptible, but the
	// wrench's much longer fixing action does.
	UPROPERTY(EditDefaultsOnly, Category = "Tool", meta = (EditCondition = "bCanUseToolUseAction"))
	bool bToolUseActionInterruptible = false;
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

	/** Interact Input Action - used to take the helm of a nearby ASteeringWheel, or to open the tool menu at a nearby AClosetDoor */
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* InteractAction;

	/** Swing-action Input Action (default: left mouse click) - swings the equipped tool, if it supports swinging. See FToolDefinition::bCanSwing. */
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* SwingActionAction;

	/** Tool-use-action Input Action (default: right mouse click) - activates the equipped tool's secondary effect, if it has one (e.g. the mop cleans a nearby puddle). See FToolDefinition::bCanUseToolUseAction. */
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* ToolUseActionAction;

	// Max distance (cm) to a ASteeringWheel/AClosetDoor for Interact to engage it.
	UPROPERTY(EditDefaultsOnly, Category = "Interaction")
	float InteractionRange = 150.f;

	// Max distance (cm) in front of the character to look for an APuddle to
	// clean up when the mop's tool-use action resolves. Kept separate from
	// InteractionRange since the mop's reach has no reason to match
	// wheel/closet interaction distance.
	UPROPERTY(EditDefaultsOnly, Category = "Mop")
	float CleaningRange = 150.f;

	// Max distance (cm) in front of the character to look for an active
	// UShipBreakComponent to fix when the wrench's tool-use action resolves.
	// Kept separate from CleaningRange/InteractionRange for the same reason
	// those two are kept separate from each other - no reason a wrench's
	// reach should match a mop's or a wheel's.
	UPROPERTY(EditDefaultsOnly, Category = "Wrench")
	float WrenchRange = 150.f;

	// How directly the character must be facing a wheel/door to interact with
	// it, as the min dot product between the character's forward vector and
	// the direction to the target. 1 = dead-on only, 0 = full 180-degree arc
	// either side. 0.5 allows roughly a 60-degree cone either side of forward.
	UPROPERTY(EditDefaultsOnly, Category = "Interaction")
	float InteractionFacingDotThreshold = 0.5f;

	// "Mop" prop shown/hidden in the character's hand while EquippedTool is
	// Mop (see OnRep_EquippedTool) - always present as a component and just
	// toggled visible/invisible, rather than spawned/destroyed as a separate
	// actor, so there's no attach latency and the existing replicated
	// EquippedTool is all that's needed to keep it in sync, with no extra
	// replicated state of its own. The actual mesh/socket are only ever real
	// in the Blueprint - see the class comment in ShipActor.h for why that
	// split exists project-wide.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mop", meta = (AllowPrivateAccess = "true"))
	UStaticMeshComponent* MopMesh;

	// Socket on GetMesh() (the first person arms mesh) MopMesh is attached
	// to - set to match the character's actual skeleton in the Blueprint.
	UPROPERTY(EditDefaultsOnly, Category = "Mop")
	FName MopAttachSocketName = TEXT("hand_rSocket");

	// "Wrench" prop shown/hidden in the character's hand while EquippedTool
	// is Wrench (see OnRep_EquippedTool) - same pattern as MopMesh, and for
	// the same reasons.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wrench", meta = (AllowPrivateAccess = "true"))
	UStaticMeshComponent* WrenchMesh;

	// Socket on GetMesh() WrenchMesh is attached to - set to match the
	// character's actual skeleton in the Blueprint. Kept separate from
	// MopAttachSocketName in case the wrench needs a different grip socket
	// than the mop.
	UPROPERTY(EditDefaultsOnly, Category = "Wrench")
	FName WrenchAttachSocketName = TEXT("hand_rSocket");

	// Montage played on every machine while a slip (see TrySlip) is locking
	// movement. Optional, same as SwingMontage/ToolUseActionMontage.
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

	// Tool currently carried, if any - set by ServerSetEquippedTool
	// (server-only, called from RequestEquipTool when the player picks an
	// option in the tool menu - see ClientOpenToolMenu/UShipToolMenuWidget) and
	// replicated so MopMesh/WrenchMesh's visibility stays in sync on every
	// machine, not just the carrying player's. Setting this to a new value
	// implicitly unequips whatever was previously equipped, since only one
	// value can ever be held at a time.
	UPROPERTY(ReplicatedUsing = OnRep_EquippedTool)
	EEquippedTool EquippedTool = EEquippedTool::None;

	// Capabilities (currently just swing) for each EEquippedTool value, filled
	// in the Blueprint (e.g. BP_ShipCharacter). See FToolDefinition/EEquippedTool
	// comments for how to add a new tool.
	UPROPERTY(EditDefaultsOnly, Category = "Tools")
	TMap<EEquippedTool, FToolDefinition> ToolDefinitions;

	// Widget class shown by ClientOpenToolMenu when interacting with an
	// AClosetDoor - assign a Blueprint subclass of UShipToolMenuWidget with
	// buttons for Mop/Wrench/None. Left unset, interacting with a closet
	// does nothing.
	UPROPERTY(EditDefaultsOnly, Category = "Tools")
	TSubclassOf<UShipToolMenuWidget> ToolMenuWidgetClass;

	// The tool menu currently open for this (local) player, if any - created
	// by ClientOpenToolMenu and cleared by RequestEquipTool once a selection
	// is made. Client-only: never populated on machines other than the
	// interacting player's own.
	UPROPERTY()
	TObjectPtr<UShipToolMenuWidget> ToolMenuWidget;

	// True on this machine while a swing's cooldown is still running - blocks
	// re-triggering SwingAction until FinishSwing clears it. Deliberately not
	// replicated: the server enforces its own copy authoritatively in
	// ServerSwingAction, this copy just stops each client from spamming that
	// RPC, so there's no need to keep the two in sync.
	bool bIsSwinging = false;

	FTimerHandle SwingTimerHandle;

	// True on this machine while the equipped tool's tool-use action is
	// locking movement/blocking re-triggering - set by MulticastPlayToolUseAction
	// and cleared by FinishToolUseAction, same pattern (and same reasoning
	// for not being replicated) as bIsSwinging above. Checked in DoMove/
	// DoJumpStart to actually enforce the "locked in place" part.
	bool bIsPerformingToolUseAction = false;

	// Per-machine cosmetic unlock timer, mirrors SwingTimerHandle.
	FTimerHandle ToolUseActionTimerHandle;

	// Server-only timer that resolves the tool-use action's actual gameplay
	// effect (e.g. destroying a puddle) once FToolDefinition::ToolUseActionDuration
	// has elapsed - kept separate from ToolUseActionTimerHandle so the
	// effect only ever runs once, authoritatively, regardless of how many
	// machines are running the cosmetic lock/unlock above.
	FTimerHandle ToolUseActionEffectTimerHandle;

	// True on this machine while a slip (see TrySlip) is locking movement -
	// set by MulticastPlaySlip and cleared by FinishSlip, same per-machine-
	// cosmetic-only pattern as bIsSwinging/bIsPerformingToolUseAction.
	// Checked alongside those two in DoMove/DoJumpStart.
	bool bIsSlipping = false;

	// Per-machine cosmetic unlock timer, mirrors SwingTimerHandle/
	// ToolUseActionTimerHandle.
	FTimerHandle SlipTimerHandle;

	// True on the server only while a slip's immunity window is running -
	// unlike bIsSwinging/bIsPerformingToolUseAction this has no per-client
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
	 *  ASteeringWheel in range, else tries to open the tool menu (see
	 *  ClientOpenToolMenu) at the nearest AClosetDoor in range. */
	void Interact();

	/** Called for swing-action input - swings the equipped tool if it supports
	 *  swinging (see FToolDefinition::bCanSwing). */
	void SwingAction();

	/** Called for tool-use-action input - activates the equipped tool's
	 *  secondary effect if it has one (see FToolDefinition::bCanUseToolUseAction). */
	void ToolUseAction();

	// Looks up the currently equipped tool's FToolDefinition, if any.
	const FToolDefinition* GetEquippedToolDefinition() const;

	// True if the equipped tool supports swinging, and no swing/tool-use
	// action/slip is already in progress on this machine.
	bool CanSwingEquippedTool() const;

	// True if the equipped tool supports a tool-use action, and no
	// swing/tool-use action/slip is already in progress on this machine.
	bool CanUseToolUseActionOnEquippedTool() const;

	// Clears bIsSwinging - bound as a one-shot timer by MulticastPlaySwing so
	// the swing "cooldown" expires without depending on animation ticking
	// (which dedicated servers may skip for an unseen mesh).
	void FinishSwing();

	// Clears bIsPerformingToolUseAction - bound as a one-shot timer by
	// MulticastPlayToolUseAction, mirroring FinishSwing/SwingTimerHandle.
	void FinishToolUseAction();

	// Finds the nearest APuddle within CleaningRange, roughly in front of the
	// character (same facing check as FindNearestSteeringWheel/
	// FindNearestClosetDoor). Server-only: called from PerformMopClean so the
	// range/facing check is authoritative.
	APuddle* FindNearestPuddle() const;

	// Finds the nearest active (IsBroken()) UShipBreakComponent within
	// WrenchRange, roughly in front of the character (same facing check as
	// FindNearestPuddle). Server-only: called from PerformWrenchFix so the
	// range/facing check is authoritative.
	UShipBreakComponent* FindNearestActiveShipBreak() const;

	// Resolves the equipped tool's tool-use-action gameplay effect - server-
	// only, called once FToolDefinition::ToolUseActionDuration has elapsed
	// since ServerToolUseAction (and skipped entirely if ServerCancelToolUseAction
	// canceled first - see its comment). Dispatches on EquippedTool; add a
	// new case (plus a PerformXToolUseEffect-style helper) here when a new
	// tool's tool-use action is implemented - unlike swinging, the effect
	// itself isn't data-driven since every tool's tool-use action does
	// something different.
	void ResolveToolUseActionEffect();

	// The mop's tool-use-action effect: destroys the nearest APuddle in
	// front of the character within CleaningRange, if any. Server-only,
	// called from ResolveToolUseActionEffect.
	void PerformMopClean();

	// The wrench's tool-use-action effect: restores the nearest active
	// UShipBreakComponent's HullRepairAmount to the ship and turns that break
	// back off (UShipBreakComponent::SetBroken(false)) - not destroyed, so it
	// can break again later - if one is within WrenchRange in front of the
	// character. Server-only, called from ResolveToolUseActionEffect - never
	// reached if the fixing action was canceled first (see
	// ServerCancelToolUseAction), so an interrupted wrench never repairs the
	// hull.
	void PerformWrenchFix();

	// Clears bIsSlipping - bound as a one-shot timer by MulticastPlaySlip,
	// mirroring FinishSwing/FinishToolUseAction.
	void FinishSlip();

	// Clears bIsImmuneToSlipping - bound as a one-shot timer by TrySlip.
	void ClearSlipImmunity();

	// Direction used by all the FindNearest* facing checks below. The capsule's
	// own forward vector (GetActorForwardVector()) tracks movement direction,
	// not view direction (see bOrientRotationToMovement/bUseControllerRotationYaw
	// in the constructor), so it doesn't reflect where a stationary or
	// strafing player is actually looking. The controller's control rotation
	// does track view direction and - for a player-controlled character - is
	// kept in sync on the server as part of normal character movement
	// replication, so it's safe to read here even though these Find* helpers
	// are server-only. Falls back to the actor's forward vector if there's no
	// controller (e.g. called with none possessing this character).
	FVector GetInteractionFacingDirection() const;

	// Finds the nearest unoccupied ASteeringWheel within InteractionRange, if any.
	// Server-only: called from ServerInteract so the range check is authoritative.
	class ASteeringWheel* FindNearestSteeringWheel() const;

	// Finds the nearest AClosetDoor within InteractionRange, if any. Server-only:
	// called from ServerInteract so the range check is authoritative.
	AClosetDoor* FindNearestClosetDoor() const;

	UFUNCTION()
	void OnRep_EquippedTool();

	UFUNCTION(Server, Reliable)
	void ServerInteract();

	// Opens ToolMenuWidgetClass for this player - called by ServerInteract_
	// Implementation (a Client RPC, not a direct call, since only the one
	// player who interacted should see the menu) once it's confirmed a
	// AClosetDoor is actually in range. No-ops if ToolMenuWidgetClass is
	// unset or a menu is already open (see ToolMenuWidget). Switches the
	// owning player's input mode to UI-only (and shows the cursor) for as
	// long as the menu is open, since it's a modal selection UI - see
	// RestoreGameInputModeAndCloseToolMenu for the matching cleanup.
	UFUNCTION(Client, Reliable)
	void ClientOpenToolMenu();

	// Removes ToolMenuWidget from the viewport (if open) and restores normal
	// game input mode (hides the cursor, returns input to the character's
	// Enhanced Input bindings) - shared by RequestEquipTool (equip-and-close)
	// and CloseToolMenu (cancel-without-equipping). No-op if ToolMenuWidget
	// isn't currently open. Client-only, like ToolMenuWidget itself.
	void RestoreGameInputModeAndCloseToolMenu();

	// Authoritative tool-equip handler - sets EquippedTool (implicitly
	// unequipping whatever was equipped before, see EquippedTool's comment)
	// and applies it locally via OnRep_EquippedTool, same as every other
	// Server RPC in this class not trusting client state. Called only from
	// RequestEquipTool.
	UFUNCTION(Server, Reliable)
	void ServerSetEquippedTool(EEquippedTool NewTool);

	// Authoritative swing-action handler - re-validates CanSwingEquippedTool
	// (never trust the client's local check) before broadcasting the swing.
	UFUNCTION(Server, Reliable)
	void ServerSwingAction();

	// Plays the swing (and starts the local bIsSwinging cooldown) on every
	// machine, including whichever one called ServerSwingAction - simple
	// "server decides, everyone plays it" cosmetic replication, matching this
	// codebase's preference for minimal replicated state over a persistent
	// replicated "is swinging" flag. Setting bIsSwinging only here (rather
	// than also optimistically in SwingAction) matters for a listen-server
	// host: SwingAction and ServerSwingAction_Implementation run as the same
	// synchronous call on that machine, so setting the flag any earlier would
	// make ServerSwingAction immediately reject its own swing.
	UFUNCTION(NetMulticast, Reliable)
	void MulticastPlaySwing(UAnimMontage* MontageToPlay, float Duration);

	// Authoritative tool-use-action handler - re-validates
	// CanUseToolUseActionOnEquippedTool (never trust the client's local
	// check) before broadcasting the cosmetic lock/montage and scheduling
	// ResolveToolUseActionEffect.
	UFUNCTION(Server, Reliable)
	void ServerToolUseAction();

	// Plays the tool-use-action montage (and starts the local
	// bIsPerformingToolUseAction lock) on every machine, including
	// whichever one called ServerToolUseAction - same "server decides,
	// everyone plays it" cosmetic replication as MulticastPlaySwing, and for
	// the same listen-server-host ordering reason (see MulticastPlaySwing's
	// comment).
	UFUNCTION(NetMulticast, Reliable)
	void MulticastPlayToolUseAction(UAnimMontage* MontageToPlay, float Duration);

	// Authoritative early-cancel handler for an interruptible tool-use action
	// (FToolDefinition::bToolUseActionInterruptible) - called by ToolUseAction
	// when the button is pressed again while bIsPerformingToolUseAction is
	// already true. Re-validates both the lock and the interruptible flag
	// server-side (never trust the client's local bIsPerformingToolUseAction
	// copy), then clears ToolUseActionEffectTimerHandle so
	// ResolveToolUseActionEffect never fires for this activation - canceling
	// always skips the effect, it only ever returns control early.
	UFUNCTION(Server, Reliable)
	void ServerCancelToolUseAction();

	// Clears the local bIsPerformingToolUseAction lock and stops the
	// tool-use-action montage early, on every machine - same "server
	// decides, everyone plays it" cosmetic replication as
	// MulticastPlayToolUseAction, called only from
	// ServerCancelToolUseAction once it's confirmed the cancel is valid.
	UFUNCTION(NetMulticast, Reliable)
	void MulticastCancelToolUseAction();

	// Plays the slip montage (and starts the local bIsSlipping lock, plus
	// hard-stops current momentum via StopMovementImmediately so residual
	// velocity from before the slip doesn't keep carrying the character) on
	// every machine - same "server decides, everyone plays it" cosmetic
	// replication as MulticastPlaySwing/MulticastPlayToolUseAction. Called
	// directly from TrySlip rather than from a Server RPC, since a slip is
	// never client-initiated in the first place (see TrySlip's comment).
	UFUNCTION(NetMulticast, Reliable)
	void MulticastPlaySlip(UAnimMontage* MontageToPlay, float Duration);

public:
	// Called by UShipToolMenuWidget (see ClientOpenToolMenu) when the player picks
	// an option from the tool menu - Mop, Wrench, or None. Client-side entry
	// point, same role as Interact()/SwingAction()/ToolUseAction(): forwards
	// to the authoritative ServerSetEquippedTool, then closes ToolMenuWidget
	// locally (every option, including None, always closes the menu - there's
	// no separate cancel path). Blocked while mid-swing/tool-use-action/slip,
	// same as the other action entry points, so a player can't switch tools
	// out from under an in-progress animation.
	UFUNCTION(BlueprintCallable, Category = "Tools")
	void RequestEquipTool(EEquippedTool NewTool);

	// Called by UShipToolMenuWidget when the player presses Escape, or the
	// same key currently bound to InteractAction, while the tool menu is
	// open - closes the menu without equipping anything (unlike
	// RequestEquipTool, which always equips NewTool, even
	// EEquippedTool::None, as part of closing). Client-only, same as
	// RequestEquipTool - the tool menu itself is never created on any
	// machine but the interacting player's.
	UFUNCTION(BlueprintCallable, Category = "Tools")
	void CloseToolMenu();

	// Called by APuddle::OnSlipTriggerBeginOverlap when this character walks
	// into a puddle's SlipTriggerSphere. Server-only (the overlap that calls
	// this is itself gated to HasAuthority() in APuddle, so there's no
	// client-originated call to validate against - unlike SwingAction/
	// ToolUseAction there's no matching client-side prediction/RPC pair
	// here). No-ops if already swinging, mid tool-use-action, mid-slip, or
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

	// Used by UShipToolMenuWidget to recognize whichever physical key is
	// currently bound to this action (queried live via the Enhanced Input
	// subsystem, not hardcoded) as a key that should also close the menu it
	// opened - see ClientOpenToolMenu/CloseToolMenu.
	FORCEINLINE UInputAction* GetInteractAction() const { return InteractAction; }
};