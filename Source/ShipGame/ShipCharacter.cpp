#include "ShipCharacter.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EnhancedInputComponent.h"
#include "InputActionValue.h"
#include "SteeringWheel.h"
#include "ClosetDoor.h"
#include "Puddle.h"
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

	// Hidden by default - ToggleMop/OnRep_HasMop show it while bHasMop is
	// true. No collision since it's purely a carried prop.
	MopMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MopMesh"));
	MopMesh->SetupAttachment(GetMesh(), MopAttachSocketName);
	MopMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MopMesh->SetVisibility(false, true);

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
		if (MainActionAction)
		{
			EnhancedInputComponent->BindAction(MainActionAction, ETriggerEvent::Started, this, &AShipCharacter::MainAction);
		}
		if (SecondaryActionAction)
		{
			EnhancedInputComponent->BindAction(SecondaryActionAction, ETriggerEvent::Started, this, &AShipCharacter::SecondaryAction);
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
	if (bIsPerformingSecondaryAction || bIsSlipping)
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
	if (bIsPerformingSecondaryAction || bIsSlipping)
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
		const float FacingDot = FVector::DotProduct(GetActorForwardVector().GetSafeNormal2D(), ToWheel);
		if (FacingDot < InteractionFacingDotThreshold)
		{
			continue;
		}

		NearestDistSq = DistSq;
		NearestWheel = Wheel;
	}

	return NearestWheel;
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
		const float FacingDot = FVector::DotProduct(GetActorForwardVector().GetSafeNormal2D(), ToDoor);
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
		const float FacingDot = FVector::DotProduct(GetActorForwardVector().GetSafeNormal2D(), ToPuddle);
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

void AShipCharacter::ToggleMop()
{
	EquippedTool = (EquippedTool == EEquippedTool::Mop) ? EEquippedTool::None : EEquippedTool::Mop;
	OnRep_EquippedTool();
}

void AShipCharacter::OnRep_EquippedTool()
{
	if (MopMesh)
	{
		MopMesh->SetVisibility(EquippedTool == EEquippedTool::Mop, true);
	}
}

void AShipCharacter::ServerInteract_Implementation()
{
	if (ASteeringWheel* Wheel = FindNearestSteeringWheel())
	{
		Wheel->TryEngage(GetController(), this);
		return;
	}

	if (FindNearestClosetDoor())
	{
		ToggleMop();
	}
}

const FToolDefinition* AShipCharacter::GetEquippedToolDefinition() const
{
	return ToolDefinitions.Find(EquippedTool);
}

bool AShipCharacter::CanSwingEquippedTool() const
{
	if (bIsSwinging || bIsPerformingSecondaryAction || bIsSlipping)
	{
		return false;
	}

	const FToolDefinition* ToolDef = GetEquippedToolDefinition();
	return ToolDef && ToolDef->bCanSwing;
}

bool AShipCharacter::CanUseSecondaryActionOnEquippedTool() const
{
	if (bIsSwinging || bIsPerformingSecondaryAction || bIsSlipping)
	{
		return false;
	}

	const FToolDefinition* ToolDef = GetEquippedToolDefinition();
	return ToolDef && ToolDef->bCanUseSecondaryAction;
}

void AShipCharacter::MainAction()
{
	if (!CanSwingEquippedTool())
	{
		return;
	}

	ServerMainAction();
}

void AShipCharacter::FinishSwing()
{
	bIsSwinging = false;
}

void AShipCharacter::ServerMainAction_Implementation()
{
	if (bIsSwinging || bIsPerformingSecondaryAction || bIsSlipping)
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

void AShipCharacter::SecondaryAction()
{
	if (!CanUseSecondaryActionOnEquippedTool())
	{
		return;
	}

	ServerSecondaryAction();
}

void AShipCharacter::FinishSecondaryAction()
{
	bIsPerformingSecondaryAction = false;
}

void AShipCharacter::ResolveSecondaryActionEffect()
{
	UE_LOG(LogShipCharacter, Log, TEXT("ResolveSecondaryActionEffect: EquippedTool=%d"), static_cast<int32>(EquippedTool));

	switch (EquippedTool)
	{
	case EEquippedTool::Mop:
		PerformMopClean();
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

void AShipCharacter::ServerSecondaryAction_Implementation()
{
	if (bIsSwinging || bIsPerformingSecondaryAction || bIsSlipping)
	{
		UE_LOG(LogShipCharacter, Warning, TEXT("ServerSecondaryAction: rejected - bIsSwinging=%d bIsPerformingSecondaryAction=%d bIsSlipping=%d"), bIsSwinging, bIsPerformingSecondaryAction, bIsSlipping);
		return;
	}

	const FToolDefinition* ToolDef = GetEquippedToolDefinition();
	if (!ToolDef || !ToolDef->bCanUseSecondaryAction)
	{
		UE_LOG(LogShipCharacter, Warning, TEXT("ServerSecondaryAction: rejected - no FToolDefinition for EquippedTool=%d, or bCanUseSecondaryAction=false"), static_cast<int32>(EquippedTool));
		return;
	}

	MulticastPlaySecondaryAction(ToolDef->SecondaryActionMontage, ToolDef->SecondaryActionDuration);

	// Server-authoritative resolution of the actual gameplay effect (e.g.
	// destroying a puddle), timed to land once the lock/montage duration
	// elapses - kept on its own timer, separate from the per-machine cosmetic
	// unlock each client (and the server itself) runs via
	// MulticastPlaySecondaryAction/FinishSecondaryAction, so the effect only
	// ever runs once.
	GetWorldTimerManager().SetTimer(SecondaryActionEffectTimerHandle, this, &AShipCharacter::ResolveSecondaryActionEffect, ToolDef->SecondaryActionDuration, false);
}

void AShipCharacter::MulticastPlaySecondaryAction_Implementation(UAnimMontage* MontageToPlay, float Duration)
{
	bIsPerformingSecondaryAction = true;
	GetWorldTimerManager().SetTimer(SecondaryActionTimerHandle, this, &AShipCharacter::FinishSecondaryAction, Duration, false);

	if (UAnimInstance* AnimInstance = MontageToPlay && GetMesh() ? GetMesh()->GetAnimInstance() : nullptr)
	{
		AnimInstance->Montage_Play(MontageToPlay);
	}
}

void AShipCharacter::TrySlip()
{
	if (bIsSwinging || bIsPerformingSecondaryAction || bIsSlipping || bIsImmuneToSlipping)
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
