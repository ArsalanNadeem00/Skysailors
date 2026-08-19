#include "SteeringWheel.h"
#include "ShipActor.h"
#include "ShipCharacter.h"
#include "Components/StaticMeshComponent.h"
#include "Camera/CameraComponent.h"
#include "EnhancedInputComponent.h"
#include "InputActionValue.h"
#include "Kismet/GameplayStatics.h"

ASteeringWheel::ASteeringWheel()
{
	PrimaryActorTick.bCanEverTick = false;

	WheelMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WheelMesh"));
	RootComponent = WheelMesh;
	WheelMesh->SetMobility(EComponentMobility::Movable);
	WheelMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	WheelMesh->SetCollisionResponseToAllChannels(ECR_Block);

	HelmCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("HelmCamera"));
	HelmCamera->SetupAttachment(WheelMesh);
	HelmCamera->SetRelativeLocation(FVector(-250.f, 0.f, 180.f));
	HelmCamera->SetRelativeRotation(FRotator(-15.f, 0.f, 0.f));

	bReplicates = true;

	// APawn defaults AutoPossessAI to PlacedInWorld, which would otherwise
	// spawn an AIController and possess this wheel the moment it's placed in
	// a level - making IsOccupied() permanently true and silently zeroing the
	// ship's helm throttle. Only TryEngage() should ever possess this pawn.
	AutoPossessAI = EAutoPossessAI::Disabled;
}

void ASteeringWheel::BeginPlay()
{
	Super::BeginPlay();

	// Attaches to the ship's hull so the wheel rides the deck as the ship
	// moves. Runs identically on server and clients since both resolve the
	// same level-placed AShipActor, so no attachment replication is needed.
	if (AShipActor* Ship = GetOrFindControlledShip())
	{
		AttachToComponent(Ship->ShipMesh, FAttachmentTransformRules::KeepWorldTransform);
	}
}

void ASteeringWheel::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		if (MoveAction)
		{
			EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ASteeringWheel::Steer);
			EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Completed, this, &ASteeringWheel::StopSteer);
		}
		if (InteractAction)
		{
			EnhancedInputComponent->BindAction(InteractAction, ETriggerEvent::Started, this, &ASteeringWheel::Interact);
		}
	}
}

void ASteeringWheel::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	if (AShipActor* Ship = GetOrFindControlledShip())
	{
		Ship->SetHelmControlled(true);
	}
}

void ASteeringWheel::UnPossessed()
{
	if (AShipActor* Ship = GetOrFindControlledShip())
	{
		Ship->SetHelmControlled(false);
	}

	Super::UnPossessed();
}

void ASteeringWheel::Steer(const FInputActionValue& Value)
{
	// Same 2D shape as AShipCharacter::Move: X (A/D) drives yaw, Y (W/S)
	// drives ascend/descend. There's no player throttle control.
	const FVector2D MovementVector = Value.Get<FVector2D>();
	ServerSetHelmInput(MovementVector.X, MovementVector.Y);
}

void ASteeringWheel::StopSteer()
{
	ServerSetHelmInput(0.f, 0.f);
}

void ASteeringWheel::Interact()
{
	ServerReleaseHelm();
}

void ASteeringWheel::ServerSetHelmInput_Implementation(float Yaw, float Vertical)
{
	if (AShipActor* Ship = GetOrFindControlledShip())
	{
		Ship->SetHelmYawInput(Yaw);
		Ship->SetHelmVerticalInput(Vertical);
	}
}

void ASteeringWheel::ServerReleaseHelm_Implementation()
{
	if (AController* CurrentController = GetController())
	{
		APawn* PawnToRestore = PreviousPawn;
		PreviousPawn = nullptr;

		if (PawnToRestore)
		{
			// Keeps AShipCharacter's own record of "what am I currently
			// piloting" in sync - see its CurrentlyPilotedPawn/
			// ClearCurrentlyPilotedPawn comments.
			if (AShipCharacter* Character = Cast<AShipCharacter>(PawnToRestore))
			{
				Character->ClearCurrentlyPilotedPawn();
			}

			CurrentController->Possess(PawnToRestore);
		}
	}
}

bool ASteeringWheel::TryEngage(AController* InstigatingController, APawn* InstigatingPawn)
{
	if (!InstigatingController || IsOccupied())
	{
		return false;
	}

	PreviousPawn = InstigatingPawn;
	InstigatingController->Possess(this);
	return true;
}

AShipActor* ASteeringWheel::GetOrFindControlledShip()
{
	if (IsValid(ControlledShip))
	{
		return ControlledShip;
	}

	// Assumes exactly one ship is placed in the level, matching AShipGameMode's
	// own lookup for the same reason.
	TArray<AActor*> FoundShips;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AShipActor::StaticClass(), FoundShips);
	if (FoundShips.Num() > 0)
	{
		ControlledShip = Cast<AShipActor>(FoundShips[0]);
	}

	return ControlledShip;
}
