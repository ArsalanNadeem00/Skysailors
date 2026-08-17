#include "ShipCharacter.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EnhancedInputComponent.h"
#include "InputActionValue.h"
#include "SteeringWheel.h"
#include "ClosetDoor.h"
#include "Puddle.h"
#include "ShipBreakComponent.h"
#include "ShipActor.h"
#include "Mount.h"
#include "MountableItem.h"
#include "Cannon.h"
#include "ShipToolMenuWidget.h"
#include "Blueprint/UserWidget.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "Animation/AnimInstance.h"

DEFINE_LOG_CATEGORY(LogShipCharacter);

AShipCharacter::AShipCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	// Fixed to the capsule rather than the animated mesh, matching Unreal's First
	// Person template - the arms mesh is warped (via its Control Rig) to align
	// with this camera, instead of the camera following the mesh.
	FirstPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
	FirstPersonCamera->SetupAttachment(RootComponent);
	FirstPersonCamera->SetRelativeLocation(FVector(0.f, 0.f, BaseEyeHeight));
	FirstPersonCamera->bUsePawnControlRotation = true;

	// Hidden by default - OnRep_EquippedTool shows it while EquippedTool is
	// Mop. No collision since it's purely a carried prop.
	MopMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MopMesh"));
	MopMesh->SetupAttachment(GetMesh(), MopAttachSocketName);
	MopMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MopMesh->SetVisibility(false, true);

	// Hidden by default - OnRep_EquippedTool shows it while EquippedTool is
	// Wrench. No collision, same as MopMesh.
	WrenchMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WrenchMesh"));
	WrenchMesh->SetupAttachment(GetMesh(), WrenchAttachSocketName);
	WrenchMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WrenchMesh->SetVisibility(false, true);

	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.f, 540.f, 0.f);

	// Without this, CharacterMovementComponent skips its tick entirely for a
	// Character with no Controller (see UCharacterMovementComponent::TickComponent),
	// which means an uncontrolled character just sits frozen in place instead
	// of riding along as the ship it's standing on moves/turns underneath it -
	// exactly what happens to the player's body while they're at the helm.
	GetCharacterMovement()->bRunPhysicsWithNoController = true;

	bUseControllerRotationYaw = false;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;
}

void AShipCharacter::BeginPlay()
{
	Super::BeginPlay();
}

void AShipCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AShipCharacter, EquippedTool);
	DOREPLIFETIME(AShipCharacter, CarriedMountItem);
}

void AShipCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		if (MoveAction)
		{
			EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AShipCharacter::Move);
		}
		if (LookAction)
		{
			EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &AShipCharacter::Look);
		}
		if (JumpAction)
		{
			EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &AShipCharacter::DoJumpStart);
			EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &AShipCharacter::DoJumpEnd);
		}
		if (InteractAction)
		{
			EnhancedInputComponent->BindAction(InteractAction, ETriggerEvent::Started, this, &AShipCharacter::Interact);
		}
		if (SwingActionAction)
		{
			EnhancedInputComponent->BindAction(SwingActionAction, ETriggerEvent::Started, this, &AShipCharacter::SwingAction);
		}
		if (ToolUseActionAction)
		{
			EnhancedInputComponent->BindAction(ToolUseActionAction, ETriggerEvent::Started, this, &AShipCharacter::ToolUseAction);
		}
	}
	else
	{
		UE_LOG(LogShipCharacter, Error,
			TEXT("'%s' failed to find an Enhanced Input component. This class requires Enhanced Input - check Project Settings > Input > Default Input Component Class."),
			*GetNameSafe(this));
	}
}

void AShipCharacter::Move(const FInputActionValue& Value)
{
	// Expects a 2D axis Input Action (X = right/left, Y = forward/back)
	const FVector2D MovementVector = Value.Get<FVector2D>();
	DoMove(MovementVector.X, MovementVector.Y);
}

void AShipCharacter::Look(const FInputActionValue& Value)
{
	// Expects a 2D axis Input Action (X = yaw, Y = pitch)
	const FVector2D LookAxisVector = Value.Get<FVector2D>();
	DoLook(LookAxisVector.X, LookAxisVector.Y);
}

void AShipCharacter::DoMove(float Right, float Forward)
{
	if (bIsPerformingToolUseAction || bIsSlipping)
	{
		return;
	}

	if (Controller)
	{
		const FRotator ControlRotation(0.f, Controller->GetControlRotation().Yaw, 0.f);

		if (Forward != 0.f)
		{
			const FVector ForwardDirection = FRotationMatrix(ControlRotation).GetUnitAxis(EAxis::X);
			AddMovementInput(ForwardDirection, Forward);
		}

		if (Right != 0.f)
		{
			const FVector RightDirection = FRotationMatrix(ControlRotation).GetUnitAxis(EAxis::Y);
			AddMovementInput(RightDirection, Right);
		}
	}
}

void AShipCharacter::DoLook(float Yaw, float Pitch)
{
	if (Controller)
	{
		AddControllerYawInput(Yaw);
		AddControllerPitchInput(Pitch);
	}
}

void AShipCharacter::DoJumpStart()
{
	if (bIsPerformingToolUseAction || bIsSlipping)
	{
		return;
	}

	Jump();
}

void AShipCharacter::DoJumpEnd()
{
	StopJumping();
}

void AShipCharacter::Interact()
{
	ServerInteract();
}

FVector AShipCharacter::GetInteractionFacingDirection() const
{
	if (Controller)
	{
		return Controller->GetControlRotation().Vector().GetSafeNormal2D();
	}

	return GetActorForwardVector().GetSafeNormal2D();
}

ASteeringWheel* AShipCharacter::FindNearestSteeringWheel() const
{
	TArray<AActor*> Wheels;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), ASteeringWheel::StaticClass(), Wheels);

	ASteeringWheel* NearestWheel = nullptr;
	float NearestDistSq = FMath::Square(InteractionRange);

	for (AActor* Actor : Wheels)
	{
		ASteeringWheel* Wheel = Cast<ASteeringWheel>(Actor);
		if (!Wheel || Wheel->IsOccupied())
		{
			continue;
		}

		const float DistSq = FVector::DistSquared(GetActorLocation(), Wheel->GetActorLocation());
		if (DistSq > NearestDistSq)
		{
			continue;
		}

		// Require the character to be roughly facing the wheel, not just nearby,
		// so walking past one at close range doesn't accidentally engage it.
		const FVector ToWheel = (Wheel->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
		const float FacingDot = FVector::DotProduct(GetInteractionFacingDirection(), ToWheel);
		if (FacingDot < InteractionFacingDotThreshold)
		{
			continue;
		}

		NearestDistSq = DistSq;
		NearestWheel = Wheel;
	}

	return NearestWheel;
}

ACannon* AShipCharacter::FindNearestOperableCannon() const
{
	TArray<AActor*> Cannons;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), ACannon::StaticClass(), Cannons);

	ACannon* NearestCannon = nullptr;
	float NearestDistSq = FMath::Square(CannonInteractionRange);

	for (AActor* Actor : Cannons)
	{
		ACannon* Cannon = Cast<ACannon>(Actor);
		if (!Cannon || Cannon->IsOccupied() || !Cast<AMount>(Cannon->GetAttachParentActor()))
		{
			continue;
		}

		const float DistSq = FVector::DistSquared(GetActorLocation(), Cannon->GetActorLocation());
		if (DistSq > NearestDistSq)
		{
			continue;
		}

		// Same facing requirement as FindNearestSteeringWheel/FindNearestClosetDoor
		// - a cannon merely nearby, but off to the side or behind, shouldn't count.
		const FVector ToCannon = (Cannon->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
		const float FacingDot = FVector::DotProduct(GetInteractionFacingDirection(), ToCannon);
		if (FacingDot < InteractionFacingDotThreshold)
		{
			continue;
		}

		NearestDistSq = DistSq;
		NearestCannon = Cannon;
	}

	return NearestCannon;
}

AClosetDoor* AShipCharacter::FindNearestClosetDoor() const
{
	TArray<AActor*> Doors;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AClosetDoor::StaticClass(), Doors);

	AClosetDoor* NearestDoor = nullptr;
	float NearestDistSq = FMath::Square(InteractionRange);

	for (AActor* Actor : Doors)
	{
		AClosetDoor* Door = Cast<AClosetDoor>(Actor);
		if (!Door)
		{
			continue;
		}

		const float DistSq = FVector::DistSquared(GetActorLocation(), Door->GetActorLocation());
		if (DistSq > NearestDistSq)
		{
			continue;
		}

		// Same facing requirement as FindNearestSteeringWheel - walking past
		// a closet at close range shouldn't accidentally toggle the mop.
		const FVector ToDoor = (Door->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
		const float FacingDot = FVector::DotProduct(GetInteractionFacingDirection(), ToDoor);
		if (FacingDot < InteractionFacingDotThreshold)
		{
			continue;
		}

		NearestDistSq = DistSq;
		NearestDoor = Door;
	}

	return NearestDoor;
}

APuddle* AShipCharacter::FindNearestPuddle() const
{
	TArray<AActor*> Puddles;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), APuddle::StaticClass(), Puddles);

	APuddle* NearestPuddle = nullptr;
	float NearestDistSq = FMath::Square(CleaningRange);

	for (AActor* Actor : Puddles)
	{
		APuddle* Puddle = Cast<APuddle>(Actor);
		if (!Puddle)
		{
			continue;
		}

		const float Dist = FVector::Dist(GetActorLocation(), Puddle->GetActorLocation());
		const FVector ToPuddle = (Puddle->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
		const float FacingDot = FVector::DotProduct(GetInteractionFacingDirection(), ToPuddle);
		UE_LOG(LogShipCharacter, Log, TEXT("FindNearestPuddle: candidate '%s' at %.0f cm (CleaningRange=%.0f), facing dot %.2f (min %.2f)"),
			*GetNameSafe(Puddle), Dist, CleaningRange, FacingDot, InteractionFacingDotThreshold);

		const float DistSq = Dist * Dist;
		if (DistSq > NearestDistSq)
		{
			continue;
		}

		// Same facing requirement as FindNearestSteeringWheel/FindNearestClosetDoor
		// - a puddle merely nearby, but off to the side or behind, shouldn't count.
		if (FacingDot < InteractionFacingDotThreshold)
		{
			continue;
		}

		NearestDistSq = DistSq;
		NearestPuddle = Puddle;
	}

	UE_LOG(LogShipCharacter, Log, TEXT("FindNearestPuddle: %d APuddle(s) in world, chose '%s'"),
		Puddles.Num(), NearestPuddle ? *GetNameSafe(NearestPuddle) : TEXT("none"));

	return NearestPuddle;
}

UShipBreakComponent* AShipCharacter::FindNearestActiveShipBreak() const
{
	// Assumes exactly one ship is placed in the level, matching
	// ASteeringWheel/AClosetDoor/APuddle's own ControlledShip lookup for the
	// same reason.
	TArray<AActor*> Ships;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AShipActor::StaticClass(), Ships);
	AShipActor* Ship = Ships.Num() > 0 ? Cast<AShipActor>(Ships[0]) : nullptr;
	if (!Ship)
	{
		return nullptr;
	}

	UShipBreakComponent* NearestBreak = nullptr;
	float NearestDistSq = FMath::Square(WrenchRange);

	for (UShipBreakComponent* Break : Ship->ShipBreakComponents)
	{
		if (!Break || !Break->IsBroken())
		{
			continue;
		}

		const float Dist = FVector::Dist(GetActorLocation(), Break->GetComponentLocation());
		const FVector ToBreak = (Break->GetComponentLocation() - GetActorLocation()).GetSafeNormal2D();
		const float FacingDot = FVector::DotProduct(GetInteractionFacingDirection(), ToBreak);
		UE_LOG(LogShipCharacter, Log, TEXT("FindNearestActiveShipBreak: candidate '%s' at %.0f cm (WrenchRange=%.0f), facing dot %.2f (min %.2f)"),
			*GetNameSafe(Break), Dist, WrenchRange, FacingDot, InteractionFacingDotThreshold);

		const float DistSq = Dist * Dist;
		if (DistSq > NearestDistSq)
		{
			continue;
		}

		// Same facing requirement as FindNearestPuddle - a break merely
		// nearby, but off to the side or behind, shouldn't count.
		if (FacingDot < InteractionFacingDotThreshold)
		{
			continue;
		}

		NearestDistSq = DistSq;
		NearestBreak = Break;
	}

	UE_LOG(LogShipCharacter, Log, TEXT("FindNearestActiveShipBreak: %d active UShipBreakComponent(s) on ship, chose '%s'"),
		Ship->ShipBreakComponents.Num(), NearestBreak ? *GetNameSafe(NearestBreak) : TEXT("none"));

	return NearestBreak;
}

AMount* AShipCharacter::FindNearestMount(bool bRequireEmpty) const
{
	TArray<AActor*> Mounts;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AMount::StaticClass(), Mounts);

	AMount* NearestMount = nullptr;
	float NearestDistSq = FMath::Square(MountInteractionRange);

	for (AActor* Actor : Mounts)
	{
		AMount* Mount = Cast<AMount>(Actor);
		if (!Mount || Mount->IsEmpty() != bRequireEmpty)
		{
			continue;
		}

		const float DistSq = FVector::DistSquared(GetActorLocation(), Mount->GetActorLocation());
		if (DistSq > NearestDistSq)
		{
			continue;
		}

		// Same facing requirement as the other FindNearest* helpers - a
		// mount merely nearby, but off to the side or behind, shouldn't count.
		const FVector ToMount = (Mount->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
		const float FacingDot = FVector::DotProduct(GetInteractionFacingDirection(), ToMount);
		if (FacingDot < InteractionFacingDotThreshold)
		{
			continue;
		}

		NearestDistSq = DistSq;
		NearestMount = Mount;
	}

	return NearestMount;
}

void AShipCharacter::OnRep_EquippedTool()
{
	if (MopMesh)
	{
		MopMesh->SetVisibility(EquippedTool == EEquippedTool::Mop, true);
	}
	if (WrenchMesh)
	{
		WrenchMesh->SetVisibility(EquippedTool == EEquippedTool::Wrench, true);
	}
}

void AShipCharacter::ServerInteract_Implementation()
{
	if (ASteeringWheel* Wheel = FindNearestSteeringWheel())
	{
		Wheel->TryEngage(GetController(), this);
		return;
	}

	if (ACannon* Cannon = FindNearestOperableCannon())
	{
		Cannon->TryEngage(GetController(), this);
		return;
	}

	if (FindNearestClosetDoor())
	{
		ClientOpenToolMenu();
	}
}

void AShipCharacter::ClientOpenToolMenu_Implementation()
{
	if (!ToolMenuWidgetClass || ToolMenuWidget)
	{
		return;
	}

	APlayerController* PC = GetController<APlayerController>();
	if (!PC)
	{
		return;
	}

	ToolMenuWidget = CreateWidget<UShipToolMenuWidget>(PC, ToolMenuWidgetClass);
	if (ToolMenuWidget)
	{
		ToolMenuWidget->OwningCharacter = this;
		ToolMenuWidget->SetIsFocusable(true);
		ToolMenuWidget->AddToPlayerScreen(0);

		// Modal selection UI - route all input to it (and away from the
		// character's Enhanced Input bindings, which is why the widget itself
		// - not a Server RPC - has to detect Escape/Interact to close it
		// again, see UShipToolMenuWidget::NativeOnKeyDown) while it's open.
		FInputModeUIOnly InputMode;
		InputMode.SetWidgetToFocus(ToolMenuWidget->TakeWidget());
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PC->SetInputMode(InputMode);
		PC->SetShowMouseCursor(true);
	}
}

void AShipCharacter::RestoreGameInputModeAndCloseToolMenu()
{
	if (!ToolMenuWidget)
	{
		return;
	}

	ToolMenuWidget->RemoveFromParent();
	ToolMenuWidget = nullptr;

	if (APlayerController* PC = GetController<APlayerController>())
	{
		PC->SetInputMode(FInputModeGameOnly());
		PC->SetShowMouseCursor(false);
	}
}

void AShipCharacter::CloseToolMenu()
{
	RestoreGameInputModeAndCloseToolMenu();
}

void AShipCharacter::RequestEquipTool(EEquippedTool NewTool)
{
	if (bIsSwinging || bIsPerformingToolUseAction || bIsSlipping)
	{
		return;
	}

	ServerSetEquippedTool(NewTool);
	RestoreGameInputModeAndCloseToolMenu();
}

void AShipCharacter::ServerSetEquippedTool_Implementation(EEquippedTool NewTool)
{
	EquippedTool = NewTool;
	OnRep_EquippedTool();
}

const FToolDefinition* AShipCharacter::GetEquippedToolDefinition() const
{
	return ToolDefinitions.Find(EquippedTool);
}

bool AShipCharacter::CanSwingEquippedTool() const
{
	if (bIsSwinging || bIsPerformingToolUseAction || bIsSlipping)
	{
		return false;
	}

	const FToolDefinition* ToolDef = GetEquippedToolDefinition();
	return ToolDef && ToolDef->bCanSwing;
}

bool AShipCharacter::CanUseToolUseActionOnEquippedTool() const
{
	if (bIsSwinging || bIsPerformingToolUseAction || bIsSlipping)
	{
		return false;
	}

	const FToolDefinition* ToolDef = GetEquippedToolDefinition();
	return ToolDef && ToolDef->bCanUseToolUseAction;
}

void AShipCharacter::SwingAction()
{
	// While carrying a mount item, left/right-click always try to place it
	// (see ServerSwingAction_Implementation) regardless of what's equipped -
	// CarriedMountItem is replicated specifically so this local check works
	// on the owning client without an equipped tool that otherwise allows
	// swinging.
	if (!CarriedMountItem && !CanSwingEquippedTool())
	{
		return;
	}

	ServerSwingAction();
}

void AShipCharacter::FinishSwing()
{
	bIsSwinging = false;
}

void AShipCharacter::ServerSwingAction_Implementation()
{
	if (CarriedMountItem)
	{
		TryPlaceCarriedMountItem();
		return;
	}

	if (bIsSwinging || bIsPerformingToolUseAction || bIsSlipping)
	{
		return;
	}

	const FToolDefinition* ToolDef = GetEquippedToolDefinition();
	if (!ToolDef || !ToolDef->bCanSwing)
	{
		return;
	}

	MulticastPlaySwing(ToolDef->SwingMontage, ToolDef->SwingDuration);
}

void AShipCharacter::MulticastPlaySwing_Implementation(UAnimMontage* MontageToPlay, float Duration)
{
	bIsSwinging = true;
	GetWorldTimerManager().SetTimer(SwingTimerHandle, this, &AShipCharacter::FinishSwing, Duration, false);

	if (UAnimInstance* AnimInstance = MontageToPlay && GetMesh() ? GetMesh()->GetAnimInstance() : nullptr)
	{
		AnimInstance->Montage_Play(MontageToPlay);
	}
}

void AShipCharacter::ToolUseAction()
{
	if (bIsPerformingToolUseAction)
	{
		// Already mid tool-use-action - if the equipped tool allows canceling
		// it (see FToolDefinition::bToolUseActionInterruptible), a second
		// press interrupts instead of being ignored. ServerCancelToolUseAction
		// re-validates this server-side.
		const FToolDefinition* ToolDef = GetEquippedToolDefinition();
		if (ToolDef && ToolDef->bToolUseActionInterruptible)
		{
			ServerCancelToolUseAction();
		}
		return;
	}

	// While carrying a mount item, left/right-click always try to place it
	// (see ServerToolUseAction_Implementation) regardless of what's equipped -
	// same reasoning as SwingAction's CarriedMountItem check.
	if (!CarriedMountItem && !CanUseToolUseActionOnEquippedTool())
	{
		return;
	}

	ServerToolUseAction();
}

void AShipCharacter::FinishToolUseAction()
{
	bIsPerformingToolUseAction = false;
}

void AShipCharacter::ResolveToolUseActionEffect()
{
	UE_LOG(LogShipCharacter, Log, TEXT("ResolveToolUseActionEffect: EquippedTool=%d"), static_cast<int32>(EquippedTool));

	switch (EquippedTool)
	{
	case EEquippedTool::Mop:
		PerformMopClean();
		break;
	case EEquippedTool::Wrench:
		PerformWrenchFix();
		break;
	default:
		break;
	}
}

void AShipCharacter::PerformMopClean()
{
	APuddle* Puddle = FindNearestPuddle();
	UE_LOG(LogShipCharacter, Log, TEXT("PerformMopClean: %s"), Puddle ? *GetNameSafe(Puddle) : TEXT("no puddle in range/facing - nothing to destroy"));

	if (Puddle)
	{
		Puddle->Destroy();
	}
}

void AShipCharacter::PerformWrenchFix()
{
	UShipBreakComponent* BreakComponent = FindNearestActiveShipBreak();
	UE_LOG(LogShipCharacter, Log, TEXT("PerformWrenchFix: %s"), BreakComponent ? *GetNameSafe(BreakComponent) : TEXT("no active ShipBreak in range/facing - nothing to fix"));

	if (!BreakComponent)
	{
		// Nothing to repair in range - see if the wrench is instead being
		// pointed at an occupied AMount to strip its item.
		TryRemoveMountItem();
		return;
	}

	if (AShipActor* Ship = Cast<AShipActor>(BreakComponent->GetOwner()))
	{
		Ship->RepairHull(BreakComponent->HullRepairAmount);
	}

	// Turn it back off rather than destroying it - see UShipBreakComponent's
	// class comment - so the same break can activate again later.
	BreakComponent->SetBroken(false);
}

void AShipCharacter::TryRemoveMountItem()
{
	if (CarriedMountItem)
	{
		return;
	}

	AMount* Mount = FindNearestMount(/*bRequireEmpty=*/false);
	UE_LOG(LogShipCharacter, Log, TEXT("TryRemoveMountItem: %s"), Mount ? *GetNameSafe(Mount) : TEXT("no occupied Mount in range/facing"));

	if (!Mount)
	{
		return;
	}

	AMountableItem* Item = Mount->DetachItem();
	if (!Item)
	{
		return;
	}

	CarriedMountItem = Item;
	Item->AttachToComponent(GetMesh(), FAttachmentTransformRules::SnapToTargetIncludingScale, MountItemAttachSocketName);
}

void AShipCharacter::TryPlaceCarriedMountItem()
{
	if (!CarriedMountItem)
	{
		return;
	}

	AMount* Mount = FindNearestMount(/*bRequireEmpty=*/true);
	UE_LOG(LogShipCharacter, Log, TEXT("TryPlaceCarriedMountItem: %s"), Mount ? *GetNameSafe(Mount) : TEXT("no empty Mount in range/facing"));

	if (!Mount)
	{
		return;
	}

	if (Mount->AttachItem(CarriedMountItem))
	{
		CarriedMountItem = nullptr;
	}
}

void AShipCharacter::ServerToolUseAction_Implementation()
{
	if (CarriedMountItem)
	{
		TryPlaceCarriedMountItem();
		return;
	}

	if (bIsSwinging || bIsPerformingToolUseAction || bIsSlipping)
	{
		UE_LOG(LogShipCharacter, Warning, TEXT("ServerToolUseAction: rejected - bIsSwinging=%d bIsPerformingToolUseAction=%d bIsSlipping=%d"), bIsSwinging, bIsPerformingToolUseAction, bIsSlipping);
		return;
	}

	const FToolDefinition* ToolDef = GetEquippedToolDefinition();
	if (!ToolDef || !ToolDef->bCanUseToolUseAction)
	{
		UE_LOG(LogShipCharacter, Warning, TEXT("ServerToolUseAction: rejected - no FToolDefinition for EquippedTool=%d, or bCanUseToolUseAction=false"), static_cast<int32>(EquippedTool));
		return;
	}

	MulticastPlayToolUseAction(ToolDef->ToolUseActionMontage, ToolDef->ToolUseActionDuration);

	// Server-authoritative resolution of the actual gameplay effect (e.g.
	// destroying a puddle), timed to land once the lock/montage duration
	// elapses - kept on its own timer, separate from the per-machine cosmetic
	// unlock each client (and the server itself) runs via
	// MulticastPlayToolUseAction/FinishToolUseAction, so the effect only
	// ever runs once.
	GetWorldTimerManager().SetTimer(ToolUseActionEffectTimerHandle, this, &AShipCharacter::ResolveToolUseActionEffect, ToolDef->ToolUseActionDuration, false);
}

void AShipCharacter::MulticastPlayToolUseAction_Implementation(UAnimMontage* MontageToPlay, float Duration)
{
	bIsPerformingToolUseAction = true;
	GetWorldTimerManager().SetTimer(ToolUseActionTimerHandle, this, &AShipCharacter::FinishToolUseAction, Duration, false);

	if (UAnimInstance* AnimInstance = MontageToPlay && GetMesh() ? GetMesh()->GetAnimInstance() : nullptr)
	{
		AnimInstance->Montage_Play(MontageToPlay);
	}
}

void AShipCharacter::ServerCancelToolUseAction_Implementation()
{
	if (!bIsPerformingToolUseAction)
	{
		return;
	}

	const FToolDefinition* ToolDef = GetEquippedToolDefinition();
	if (!ToolDef || !ToolDef->bToolUseActionInterruptible)
	{
		UE_LOG(LogShipCharacter, Warning, TEXT("ServerCancelToolUseAction: rejected - no FToolDefinition for EquippedTool=%d, or bToolUseActionInterruptible=false"), static_cast<int32>(EquippedTool));
		return;
	}

	// Stops the pending ResolveToolUseActionEffect call - canceling always
	// skips the effect (e.g. the wrench's hull repair).
	GetWorldTimerManager().ClearTimer(ToolUseActionEffectTimerHandle);

	MulticastCancelToolUseAction();
}

void AShipCharacter::MulticastCancelToolUseAction_Implementation()
{
	bIsPerformingToolUseAction = false;
	GetWorldTimerManager().ClearTimer(ToolUseActionTimerHandle);

	if (UAnimInstance* AnimInstance = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr)
	{
		AnimInstance->Montage_Stop(0.25f);
	}
}

void AShipCharacter::TrySlip()
{
	if (bIsSwinging || bIsPerformingToolUseAction || bIsSlipping || bIsImmuneToSlipping)
	{
		return;
	}

	bIsImmuneToSlipping = true;
	GetWorldTimerManager().SetTimer(SlipImmunityTimerHandle, this, &AShipCharacter::ClearSlipImmunity, SlipImmunityDuration, false);

	MulticastPlaySlip(SlipMontage, SlipDuration);
}

void AShipCharacter::ClearSlipImmunity()
{
	bIsImmuneToSlipping = false;
}

void AShipCharacter::FinishSlip()
{
	bIsSlipping = false;
}

void AShipCharacter::MulticastPlaySlip_Implementation(UAnimMontage* MontageToPlay, float Duration)
{
	bIsSlipping = true;
	GetCharacterMovement()->StopMovementImmediately();
	GetWorldTimerManager().SetTimer(SlipTimerHandle, this, &AShipCharacter::FinishSlip, Duration, false);

	if (UAnimInstance* AnimInstance = MontageToPlay && GetMesh() ? GetMesh()->GetAnimInstance() : nullptr)
	{
		AnimInstance->Montage_Play(MontageToPlay);
	}
}
