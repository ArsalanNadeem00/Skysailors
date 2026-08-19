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
class AMount;
class AMountableItem;
class ACannon;
class AShipActor;
class UShipToolMenuWidget;
class APlayerController;
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

	// Max distance (cm) in front of the character to look for an AMount, both
	// when removing its attached item with the wrench (TryRemoveMountItem)
	// and when placing a carried item onto an empty one
	// (TryPlaceCarriedMountItem). Kept separate from WrenchRange/
	// CleaningRange/InteractionRange for the same reason those are kept
	// separate from each other.
	UPROPERTY(EditDefaultsOnly, Category = "Mount")
	float MountInteractionRange = 150.f;

	// Max distance (cm) to a mounted, unoccupied ACannon for Interact to
	// begin operating it. Kept separate from MountInteractionRange/
	// WrenchRange/CleaningRange/InteractionRange for the same reason those
	// are kept separate from each other.
	UPROPERTY(EditDefaultsOnly, Category = "Cannon")
	float CannonInteractionRange = 150.f;

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

	// Socket on GetMesh() a carried AMountableItem (see CarriedMountItem)
	// attaches to. Deliberately a different socket than MopAttachSocketName/
	// WrenchAttachSocketName so a carried mount item doesn't visually overlap
	// whichever tool is also equipped - a player can carry a Cannon/Shield
	// while still holding the wrench, e.g. having just removed it from
	// another mount.
	UPROPERTY(EditDefaultsOnly, Category = "Mount")
	FName MountItemAttachSocketName = TEXT("carry");

	// The AMountableItem (ACannon/AShield) currently carried, if any - set by
	// TryRemoveMountItem (the wrench's tool-use action, when it finds an
	// occupied AMount instead of an active UShipBreakComponent) and cleared
	// by TryPlaceCarriedMountItem once it's handed off to an empty AMount.
	// Unlike EquippedTool, there's no separate mesh to toggle: the item IS a
	// AMountableItem actor, so simply attaching/detaching it (see both
	// functions) is enough for every machine to see it move via that actor's
	// own built-in attachment replication. Replicated anyway (plain
	// Replicated, no OnRep needed) purely so SwingAction/ToolUseAction can
	// check locally on the owning client whether to route left/right-click
	// into placing this instead of the equipped tool's normal action - see
	// both functions.
	UPROPERTY(Replicated)
	TObjectPtr<AMountableItem> CarriedMountItem;

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

	// Upper bound Health can ever reach - see SetHealth, which clamps
	// against this. Starts equal to Health's own starting value (100), so
	// Health starts full by default.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Replicated, Category = "Character|Stats")
	float MaxHealth = 100.f;

	// Upper bound Hunger can ever reach - see SetHunger. Same reasoning as
	// MaxHealth.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Replicated, Category = "Character|Stats")
	float MaxHunger = 100.f;

	UFUNCTION(BlueprintPure, Category = "Character|Stats")
	float GetHealth() const { return Health; }

	UFUNCTION(BlueprintPure, Category = "Character|Stats")
	float GetHunger() const { return Hunger; }

	// Sets Health, clamped to [0, MaxHealth] - the only way Health is ever
	// allowed to change, so it can never exceed MaxHealth (or drop below 0)
	// regardless of what calls this or by how much. Health itself is
	// protected specifically to force this - no other code, C++ or
	// Blueprint, can assign to it directly and bypass the clamp. No damage/
	// regen sources call this yet - still belongs here once designed, same
	// as Health/Hunger's own original comment. Server-only in practice once
	// something does call it (same "assumes caller is already
	// authoritative" convention as AShipActor::RepairHull/ApplyDamage), but
	// doesn't enforce that itself yet since nothing calls it at all.
	UFUNCTION(BlueprintCallable, Category = "Character|Stats")
	void SetHealth(float NewHealth);

	// Sets Hunger, clamped to [0, MaxHunger]. Same reasoning as SetHealth.
	UFUNCTION(BlueprintCallable, Category = "Character|Stats")
	void SetHunger(float NewHunger);

	// Drops Health by Amount, via SetHealth (so it stays clamped to
	// [0, MaxHealth]) - called by ACannonProjectile::OnHit when a hostile,
	// player-targeting projectile (see ACannonProjectile::
	// bDamagesPlayerCharacter, set by UEnemyGunComponent for ammo types with
	// FCannonAmmoType::bTargetPlayer) hits this character. No-ops entirely
	// while bIsDefeated (already at 0 and mid-respawn-sequence - see
	// StartDefeatSequence) so a defeated player can't take further damage or
	// re-trigger the sequence. Otherwise flashes ClientPlayHitFlash (if
	// Amount actually took effect) and, if Health just reached 0, starts the
	// defeat sequence. Server-only in practice, same
	// assumes-caller-is-authoritative convention as AShipActor::ApplyDamage/
	// AEnemyShip::ApplyDamage.
	UFUNCTION(BlueprintCallable, Category = "Character|Stats")
	void ApplyDamage(float Amount);

	// Every AShipCharacter currently in the world, added/removed in
	// BeginPlay/EndPlay - lets UEnemyGunComponent::FindVisiblePlayerTarget
	// scan player characters without a per-tick
	// UGameplayStatics::GetAllActorsOfClass call, same reasoning/pattern as
	// AEnemyShip::ActiveEnemyShips.
	static const TArray<TWeakObjectPtr<AShipCharacter>>& GetActiveShipCharacters() { return ActiveShipCharacters; }

	// True if a player is actively controlling this character, either
	// directly (IsPlayerControlled()) or by proxy while piloting a
	// ASteeringWheel/ACannon this character engaged (see
	// CurrentlyPilotedPawn) - this character's own body is simply left
	// standing wherever it was when they got in, still a real, positioned
	// actor in the world, so there's nothing else needed to keep it a valid
	// target/damage-recipient once this returns true for it. Used by
	// UEnemyGunComponent::FindVisiblePlayerTarget so a piloting player can
	// still be hit exactly as if they were still walking around.
	bool IsEffectivelyPlayerControlled() const;

	// Clears CurrentlyPilotedPawn - called by ASteeringWheel::
	// ServerReleaseHelm_Implementation/ACannon::ServerReleaseCannon_Implementation
	// right before they hand control back on a normal release (the
	// piloting player interacting again), so this character's own
	// bookkeeping of "what am I currently piloting" stays in sync with
	// reality. Public since CurrentlyPilotedPawn itself is protected and
	// those are different classes - ForceUnpilot (the other path that ends
	// piloting, this character reaching 0 Health while piloting) clears it
	// directly instead, since that has protected access from within this
	// class.
	void ClearCurrentlyPilotedPawn() { CurrentlyPilotedPawn = nullptr; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaTime) override;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// Player's health, 0-MaxHealth. Starts at 100 - no regen sources wired up
	// yet (still belongs here once designed), but damage (ApplyDamage) and
	// the resulting defeat/respawn sequence are - see StartDefeatSequence.
	// Replicated so UCharacterHUDWidget's health bar stays in sync, same
	// VisibleAnywhere/BlueprintReadOnly/Replicated shape as
	// AShipActor::HullIntegrity/AEnemyShip::HullIntegrity. Protected (not
	// public) - see SetHealth for why: it's the only permitted way to
	// change this, and being protected is what actually makes that a
	// guarantee rather than a convention. Read via GetHealth().
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category = "Character|Stats")
	float Health = 100.f;

	// Player's hunger, 0-MaxHunger. Starts at 100 - no depletion-over-time
	// or low-hunger effects wired up yet (still belongs here once
	// designed). Protected for the same reason as Health - see SetHunger.
	// Read via GetHunger().
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category = "Character|Stats")
	float Hunger = 100.f;

	// Backs GetActiveShipCharacters() - see its comment. A static registry
	// rather than a per-tick UGameplayStatics::GetAllActorsOfClass scan,
	// same reasoning as AEnemyShip::ActiveEnemyShips.
	static TArray<TWeakObjectPtr<AShipCharacter>> ActiveShipCharacters;

	// Montage played on every machine while the defeat sequence's movement
	// lock is in effect - see MulticastPlayDefeat. Optional, same as
	// SlipMontage/SwingMontage.
	UPROPERTY(EditDefaultsOnly, Category = "Defeat")
	UAnimMontage* DefeatMontage = nullptr;

	// How long (seconds) the screen fade to black takes, starting the
	// moment Health reaches 0 (alongside the movement lock/DefeatMontage,
	// not after it finishes - see MulticastPlayDefeat's comment for why).
	// Only the defeated player's own screen fades - see IsLocallyControlled()
	// in MulticastPlayDefeat_Implementation.
	UPROPERTY(EditDefaultsOnly, Category = "Defeat")
	float DefeatFadeOutDuration = 1.f;

	// How long (seconds) after Health reaches 0 the player respawns - see
	// StartDefeatSequence/RespawnCharacter. Measured from the start of the
	// defeat sequence, not from when the fade-out finishes.
	UPROPERTY(EditDefaultsOnly, Category = "Defeat")
	float RespawnDelaySeconds = 3.f;

	// How long (seconds) the screen takes to fade back in from black once
	// RespawnCharacter runs - see MulticastFinishDefeat.
	UPROPERTY(EditDefaultsOnly, Category = "Defeat")
	float DefeatFadeInDuration = 1.f;

	// World Z (cm) at or below which this character is considered to have
	// fallen off the ship (e.g. into the sea below deck level) - checked
	// every tick by CheckFallDeath. EditDefaultsOnly since the right value
	// depends entirely on where a given level places its ship/sea, unlike
	// the fixed gameplay durations above.
	UPROPERTY(EditDefaultsOnly, Category = "Defeat")
	float FallDeathZThreshold = 25000.f;

	// True on every machine while the defeat sequence (movement lock,
	// DefeatMontage, screen fade) is in effect - set by MulticastPlayDefeat
	// and cleared by MulticastFinishDefeat once RespawnCharacter runs, same
	// per-machine-cosmetic-but-also-movement-relevant pattern as bIsSlipping
	// (checked alongside it in DoMove/DoJumpStart). Not replicated - every
	// machine sets/clears it identically via those two NetMulticasts, same
	// reasoning as bIsSlipping.
	bool bIsDefeated = false;

	// The ASteeringWheel/ACannon this character is currently piloting, if
	// any - set in ServerInteract_Implementation right after this character
	// successfully engages one (TryEngage returning true), and cleared
	// either by ASteeringWheel::ServerReleaseHelm_Implementation/
	// ACannon::ServerReleaseCannon_Implementation on a normal release, or by
	// ForceUnpilot on a forced one. See IsEffectivelyPlayerControlled/
	// GetEffectiveController for why this exists: while piloting, this
	// character itself has no Controller (its own controller has possessed
	// the wheel/cannon instead), so there's nothing else linking this body
	// back to the player actually still responsible for it. Not replicated -
	// only ever read server-side (UEnemyGunComponent's targeting is
	// server-only, and GetEffectiveController/ForceUnpilot are only called
	// from other server-only paths).
	UPROPERTY()
	APawn* CurrentlyPilotedPawn = nullptr;

	// This character's own Controller if directly possessed, or otherwise
	// CurrentlyPilotedPawn's Controller (the same player, just currently
	// possessing the wheel/cannon this character engaged instead) - see
	// CurrentlyPilotedPawn's comment. Used by ClientPlayHitFlash_Implementation,
	// since GetController() alone would return null while piloting even
	// though the same player is still very much present and playing.
	APlayerController* GetEffectiveController() const;

	// Forces this character's player back into direct possession of this
	// character - the exact same "give control back" transition
	// ASteeringWheel::ServerReleaseHelm/ACannon::ServerReleaseCannon
	// normally perform on interact (Controller->Possess(this) triggers the
	// wheel/cannon's own UnPossessed() override identically either way),
	// just triggered by this character reaching 0 Health instead of the
	// player pressing the interact key. No-ops if CurrentlyPilotedPawn is
	// unset (already directly possessed). Called from StartDefeatSequence
	// before the rest of the sequence runs, so that sequence's
	// IsLocallyControlled() checks (see MulticastPlayDefeat/
	// MulticastFinishDefeat) correctly resolve against this character - they
	// couldn't while it remained unpossessed.
	void ForceUnpilot();

	// Server-only timer from StartDefeatSequence to RespawnCharacter, keyed
	// off RespawnDelaySeconds.
	FTimerHandle RespawnTimerHandle;

	// The ship this character originally spawned on, and this character's
	// spawn transform relative to that ship's root (both captured once in
	// BeginPlay) - see RespawnCharacter. Relative, not a frozen world-space
	// transform, specifically because the ship never stops moving (see
	// AShipActor's class comment) - respawning at a stale world coordinate
	// would drop the player in empty air/ocean far behind wherever the ship
	// actually is by respawn time. Recomputing RespawnRelativeTransform *
	// SpawnShip->GetActorTransform() at respawn time instead gives back the
	// same deck spot ("initial spawn location") regardless of how far the
	// ship has traveled/turned since. Auto-found the same
	// lazy-find-the-one-ship pattern as AMount/APuddle/AEnemyShip/etc.
	// (assumes exactly one ship in the level).
	UPROPERTY()
	AShipActor* SpawnShip;

	FTransform RespawnRelativeTransform;

	// Server-only - starts the defeat sequence once Health reaches 0. Calls
	// ForceUnpilot first (a no-op if this character wasn't piloting a
	// wheel/cannon), so the rest of the sequence always runs against an
	// actually-possessed character - see ForceUnpilot's comment for why
	// that matters. Then calls MulticastPlayDefeat (movement lock + montage
	// + fade-out all start together - see its comment) and starts
	// RespawnTimerHandle for RespawnDelaySeconds, after which
	// RespawnCharacter runs.
	void StartDefeatSequence();

	// Server-only, called every tick from Tick() - kills this character (see
	// FallDeathZThreshold) once they've fallen below the level's playable
	// space, e.g. off the edge of the ship into the sea. Routes through
	// ApplyDamage(GetHealth()) rather than calling SetHealth(0.f)/
	// StartDefeatSequence directly, so falling off the ship triggers exactly
	// the same hit-flash + defeat/respawn sequence any other damage source
	// does once Health reaches 0 - one single "Health hit zero" path rather
	// than a second one just for this. No-ops while already bIsDefeated (that
	// same check also lives inside ApplyDamage, but skipping it here as well
	// avoids recomputing GetActorLocation() every tick during the respawn
	// delay for no reason).
	void CheckFallDeath();

	// Locks movement (bIsDefeated, checked in DoMove/DoJumpStart) and plays
	// DefeatMontage on every machine, same "server decides, everyone plays
	// it" cosmetic replication as MulticastPlaySlip/MulticastPlaySwing.
	// Additionally, only on the defeated player's own local machine (see
	// IsLocallyControlled()), fades that player's own screen to black over
	// FadeOutDuration - deliberately not sent as a separate Client RPC the
	// way ACannon::ClientPlayFireCameraShake is; IsLocallyControlled()
	// inside this same Multicast is simpler and achieves the identical
	// "only the relevant player" scoping. Starts immediately alongside the
	// movement lock/montage (not delayed until the montage finishes) - so
	// the screen gradually darkens while the defeat animation plays, ending
	// fully black by the time FadeOutDuration elapses; retime
	// FadeOutDuration/DefeatMontage's own length against each other once
	// real montage art exists if a different feel is wanted.
	UFUNCTION(NetMulticast, Reliable)
	void MulticastPlayDefeat(UAnimMontage* MontageToPlay, float FadeOutDuration);

	// Server-only timer callback (RespawnTimerHandle) - teleports back to
	// the initial spawn location (see SpawnShip/RespawnRelativeTransform),
	// restores Health to MaxHealth, and calls MulticastFinishDefeat. The
	// screen is already fully held at black (see MulticastPlayDefeat) by
	// the time this runs, which is what keeps the teleport itself from ever
	// being visible, regardless of the small amount of time normal Character
	// movement replication takes to catch up on remote clients.
	void RespawnCharacter();

	// Clears the movement lock (bIsDefeated) on every machine and, only on
	// the respawned player's own local machine, fades their screen back in
	// from black over FadeInDuration - same IsLocallyControlled()-gated
	// scoping and "server decides, everyone plays it" shape as
	// MulticastPlayDefeat. Called only from RespawnCharacter, once the
	// teleport/Health reset have already happened.
	UFUNCTION(NetMulticast, Reliable)
	void MulticastFinishDefeat(float FadeInDuration);

	// Client RPC (routes to this character's own possessing connection
	// automatically, same as ACannon::ClientPlayFireCameraShake) - called by
	// ApplyDamage whenever damage actually lands, so just the damaged
	// player sees a brief red screen-edge flash (see
	// AShipGamePlayerController::PlayCharacterHitFlash/
	// UCharacterHUDWidget::PlayHitFlash) alerting them they've been hit.
	UFUNCTION(Client, Reliable)
	void ClientPlayHitFlash();

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

	/** Called for swing-action input - while carrying a mount item
	 *  (CarriedMountItem), tries to place it on the nearest empty AMount
	 *  instead (see TryPlaceCarriedMountItem); otherwise swings the equipped
	 *  tool if it supports swinging (see FToolDefinition::bCanSwing). */
	void SwingAction();

	/** Called for tool-use-action input - while carrying a mount item
	 *  (CarriedMountItem), tries to place it on the nearest empty AMount
	 *  instead (see TryPlaceCarriedMountItem); otherwise activates the
	 *  equipped tool's secondary effect if it has one (see
	 *  FToolDefinition::bCanUseToolUseAction). */
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

	// Finds the nearest AMount within MountInteractionRange, roughly in front
	// of the character (same facing check as the other FindNearest*
	// helpers), whose IsEmpty() matches bRequireEmpty - i.e. pass false to
	// find one to remove an item from (TryRemoveMountItem) or true to find
	// one to place a carried item onto (TryPlaceCarriedMountItem).
	// Server-only: called from both, so the range/facing check is
	// authoritative.
	AMount* FindNearestMount(bool bRequireEmpty) const;

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
	// hull. If there's no active UShipBreakComponent in range, falls back to
	// TryRemoveMountItem - both are "what the wrench does" on a tool-use
	// action, just against different nearby targets.
	void PerformWrenchFix();

	// Removes whatever's attached to the nearest occupied AMount within
	// MountInteractionRange (see FindNearestMount) and gives it to the
	// player to carry (CarriedMountItem), attaching it to
	// MountItemAttachSocketName. Server-only, called from PerformWrenchFix -
	// so, per that function's comment, only ever runs while the wrench is
	// equipped. No-ops if already carrying something or no occupied mount is
	// in range/facing.
	void TryRemoveMountItem();

	// Attaches CarriedMountItem to the nearest empty AMount within
	// MountInteractionRange (see FindNearestMount) and clears
	// CarriedMountItem. Server-only, called from ServerSwingAction_
	// Implementation/ServerToolUseAction_Implementation in place of their
	// usual tool behavior whenever CarriedMountItem is set - unlike
	// TryRemoveMountItem, placing doesn't require any particular tool to be
	// equipped. No-ops if not carrying anything or no empty mount is in
	// range/facing (CarriedMountItem is left untouched either way).
	void TryPlaceCarriedMountItem();

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

	// Finds the nearest unoccupied, currently-mounted ACannon within
	// CannonInteractionRange, roughly in front of the character (same
	// facing check as the other FindNearest* helpers), if any. A carried
	// (unmounted) cannon never matches - see ACannon::TryEngage. Server-only:
	// called from ServerInteract so the range/facing check is authoritative.
	ACannon* FindNearestOperableCannon() const;

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