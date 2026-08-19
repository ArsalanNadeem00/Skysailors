#include "Cannon.h"
#include "Mount.h"
#include "CannonProjectile.h"
#include "ShipCharacter.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/StaticMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputActionValue.h"
#include "Net/UnrealNetwork.h"

ACannon::ACannon()
{
	PrimaryActorTick.bCanEverTick = true;

	CannonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("CannonCamera"));
	CannonCamera->SetupAttachment(ItemMesh);

	BarrelMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BarrelMesh"));
	BarrelMesh->SetupAttachment(CannonCamera);
	BarrelMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	MuzzleLocation = CreateDefaultSubobject<USceneComponent>(TEXT("MuzzleLocation"));
	MuzzleLocation->SetupAttachment(BarrelMesh);
}

void ACannon::BeginPlay()
{
	Super::BeginPlay();

	RestCameraRotation = CannonCamera->GetRelativeRotation();
}

void ACannon::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Nothing to integrate while no aim input is held - the camera just
	// stays wherever it currently is.
	if (AimYawInput == 0.f && AimPitchInput == 0.f)
	{
		return;
	}

	AimYaw = FMath::Clamp(AimYaw + AimYawInput * AimRotationSpeed * DeltaTime, -MaxAimAngleDegrees, MaxAimAngleDegrees);
	AimPitch = FMath::Clamp(AimPitch + AimPitchInput * AimRotationSpeed * DeltaTime, -MaxAimAngleDegrees, MaxAimAngleDegrees);

	// Recomputed fresh from RestCameraRotation + the clamped offset every
	// time, not accumulated - see the class comment for why that matters.
	CannonCamera->SetRelativeRotation(RestCameraRotation + FRotator(AimPitch, AimYaw, 0.f));
}

void ACannon::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		if (MoveAction)
		{
			EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ACannon::Aim);
			EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Completed, this, &ACannon::StopAim);
		}
		if (InteractAction)
		{
			EnhancedInputComponent->BindAction(InteractAction, ETriggerEvent::Started, this, &ACannon::Interact);
		}
		if (FireAction)
		{
			EnhancedInputComponent->BindAction(FireAction, ETriggerEvent::Started, this, &ACannon::Fire);
		}
		if (ScrollUpAction)
		{
			EnhancedInputComponent->BindAction(ScrollUpAction, ETriggerEvent::Started, this, &ACannon::CycleAmmoNext);
		}
		if (ScrollDownAction)
		{
			EnhancedInputComponent->BindAction(ScrollDownAction, ETriggerEvent::Started, this, &ACannon::CycleAmmoPrevious);
		}
	}
}

void ACannon::UnPossessed()
{
	AimYaw = 0.f;
	AimPitch = 0.f;
	AimYawInput = 0.f;
	AimPitchInput = 0.f;
	CannonCamera->SetRelativeRotation(RestCameraRotation);

	Super::UnPossessed();
}

void ACannon::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ACannon, AimYawInput);
	DOREPLIFETIME(ACannon, AimPitchInput);
	DOREPLIFETIME(ACannon, CurrentAmmoTypeIndex);
	DOREPLIFETIME(ACannon, AmmoTypes);
}

void ACannon::Aim(const FInputActionValue& Value)
{
	// Same 2D shape as ASteeringWheel::Steer: X (A/D) drives yaw, Y (W/S)
	// drives pitch - W (forward, +Y) pitches the barrel up.
	const FVector2D AimVector = Value.Get<FVector2D>();
	ServerSetAimInput(AimVector.X, AimVector.Y);
}

void ACannon::StopAim()
{
	ServerSetAimInput(0.f, 0.f);
}

void ACannon::Interact()
{
	ServerReleaseCannon();
}

void ACannon::Fire()
{
	ServerFire();
}

void ACannon::CycleAmmoNext()
{
	ServerCycleAmmo(1);
}

void ACannon::CycleAmmoPrevious()
{
	ServerCycleAmmo(-1);
}

void ACannon::ServerSetAimInput_Implementation(float YawInput, float PitchInput)
{
	AimYawInput = FMath::Clamp(YawInput, -1.f, 1.f);
	AimPitchInput = FMath::Clamp(PitchInput, -1.f, 1.f);
}

void ACannon::ServerReleaseCannon_Implementation()
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

void ACannon::ServerFire_Implementation()
{
	FCannonAmmoType* Ammo = GetCurrentAmmoType();
	if (!ProjectileClass || !MuzzleLocation || !Ammo)
	{
		return;
	}

	if (!Ammo->bInfiniteAmmo && Ammo->AmmoCount <= 0)
	{
		return;
	}

	const float CurrentTime = GetWorld()->GetTimeSeconds();
	if (CurrentTime - LastFireTime < Ammo->FireCooldown)
	{
		return;
	}
	LastFireTime = CurrentTime;

	if (!Ammo->bInfiniteAmmo)
	{
		Ammo->AmmoCount = FMath::Max(Ammo->AmmoCount - 1, 0);

		// AmmoTypes' own OnRep never fires locally on the server that just
		// made this change - same reasoning as ServerCycleAmmo's broadcast,
		// see the class comment.
		OnAmmoStateChanged.Broadcast();
	}

	const FTransform SpawnTransform(MuzzleLocation->GetComponentRotation(), MuzzleLocation->GetComponentLocation());

	// Deferred, not a plain SpawnActor - ConfigureAmmo below needs to set
	// this ammo type's damage/mesh/sound/impact effect on the projectile
	// BEFORE its own BeginPlay runs (BeginPlay is what plays FireSound). A
	// plain SpawnActor would run BeginPlay synchronously as part of the call
	// itself, on the server, before there's any chance to configure it -
	// clients don't have this problem (they apply replicated properties
	// before their own BeginPlay regardless), but the server's own local
	// BeginPlay does.
	ACannonProjectile* Projectile = GetWorld()->SpawnActorDeferred<ACannonProjectile>(ProjectileClass, SpawnTransform, this, GetInstigator(), ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (Projectile)
	{
		Projectile->ConfigureAmmo(Ammo->Damage, Ammo->ProjectileMesh, Ammo->FireSound, Ammo->ImpactEffect, Ammo->ImpactSound);
		Projectile->FinishSpawning(SpawnTransform);
	}

	ClientPlayFireCameraShake(Ammo->FireCameraShake);
}

void ACannon::ServerCycleAmmo_Implementation(int32 Direction)
{
	if (AmmoTypes.Num() == 0)
	{
		return;
	}

	// +AmmoTypes.Num() before the modulo keeps this non-negative regardless
	// of sign, given Direction is always +1/-1 - C++'s % can return negative
	// for a negative dividend otherwise.
	CurrentAmmoTypeIndex = (CurrentAmmoTypeIndex + Direction + AmmoTypes.Num()) % AmmoTypes.Num();

	// OnRep_CurrentAmmoTypeIndex never fires locally on the machine that just
	// made this change (that's only how it works for every OTHER machine,
	// once the new value replicates down to them) - broadcasting directly
	// here as well is what a listen-server host operating their own cannon
	// needs to see their own widget refresh too.
	OnAmmoStateChanged.Broadcast();
}

void ACannon::OnRep_CurrentAmmoTypeIndex()
{
	OnAmmoStateChanged.Broadcast();
}

void ACannon::OnRep_AmmoTypes()
{
	OnAmmoStateChanged.Broadcast();
}

void ACannon::ClientPlayFireCameraShake_Implementation(TSubclassOf<UCameraShakeBase> ShakeClass)
{
	if (!ShakeClass)
	{
		return;
	}

	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (PC->PlayerCameraManager)
		{
			PC->PlayerCameraManager->StartCameraShake(ShakeClass);
		}
	}
}

bool ACannon::TryEngage(AController* InstigatingController, APawn* InstigatingPawn)
{
	if (!InstigatingController || IsOccupied() || !Cast<AMount>(GetAttachParentActor()))
	{
		return false;
	}

	PreviousPawn = InstigatingPawn;
	InstigatingController->Possess(this);
	return true;
}
